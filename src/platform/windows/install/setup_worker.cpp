#include "platform/windows/install/setup_worker.h"

#include <windows.h>

#include <objbase.h>

#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "core/product_info.h"
#include "core/setup/arp.h"
#include "core/setup/install_tx.h"
#include "core/setup/layout.h"
#include "core/setup/setup_protocol.h"
#include "platform/windows/broker_channel.h"
#include "platform/windows/install/setup_journal_fs.h"
#include "platform/windows/install/win_setup_ops.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/pipe.h"

namespace ef::platform::install {

namespace {

using core::setup::JournalEntry;
using core::setup::SetupJournal;

constexpr std::uint32_t kSendTimeoutMs = 5000;
constexpr std::uint32_t kAcceptWindowMs = 20000;
constexpr std::uint32_t kHelloTimeoutMs = 10000;
constexpr std::uint32_t kRequestTimeoutMs = 30000;
constexpr std::uint32_t kByeTimeoutMs = 10000;
constexpr unsigned kLockWaitMs = 10000;

// Her kalici journal kaydindan SONRA ilerleme bildirir (tahmini adim durumu yok).
class NotifyingJournal final : public SetupJournal {
public:
    NotifyingJournal(SetupJournal& inner, std::function<void(const JournalEntry&)> on_record)
        : inner_(inner), on_record_(std::move(on_record)) {}
    contracts::Status append(const JournalEntry& e) override {
        auto s = inner_.append(e);
        if (s && on_record_) on_record_(e);
        return s;
    }
    contracts::Result<std::vector<JournalEntry>> read() override { return inner_.read(); }
    contracts::Status clear() override { return inner_.clear(); }

private:
    SetupJournal& inner_;
    std::function<void(const JournalEntry&)> on_record_;
};

struct OwnedHandle {
    void* h = nullptr;
    ~OwnedHandle() {
        if (h) CloseHandle(static_cast<HANDLE>(h));
    }
};

struct Com {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Com() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

std::wstring version_text() {
    const auto v = core::product_version();
    return std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
}

std::wstring local_yyyymmdd() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t buf[16] = {};
    (void)swprintf(buf, 16, L"%04u%02u%02u", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay));
    return buf;
}

}  // namespace

int run_setup_worker(const core::LaunchArgs& a, const ParentIdentity& parent) {
    if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;

    // Kaynak goruntu en basta yazma/silme paylasimsiz acilir: sonrasinda yeniden adlandirilip degistirilemez.
    const auto self = normalized_self_exe();
    OwnedHandle source{open_own_image_deny_write(self)};
    const Com com;  // kisayol (IShellLinkW) icin

    const auto layout = core::setup::make_layout(known_roots());
    std::string pre_error;
    if (layout.exe.empty() || layout.machine_dir.empty() || layout.shortcut.empty() || layout.desktop_shortcut.empty()) pre_error = "known folders unavailable";
    if (pre_error.empty() && !source.h) pre_error = "own image not readable";

    // Makine islemleri kilidi (broker/kurtarma ile es zamanli yazici yok), sonra yarim kalmis kurulumun kurtarmasi.
    auto lock = MachineOpsLock::try_acquire(kLockWaitMs);
    if (pre_error.empty() && !lock) pre_error = "busy: another administrator operation is running";
    FsSetupJournal journal(layout);
    WinSetupOps ops(layout, self, source.h, version_text());
    core::setup::TxResult recovery;
    recovery.ok = true;
    if (pre_error.empty()) {
        auto rd = journal.read();
        if (!rd) {
            pre_error = "setup journal unreadable: " + rd.error().detail;
        } else if (!rd.value().empty()) {
            recovery = core::setup::recover_interrupted(ops, journal);
            if (!recovery.ok || !recovery.rollback_clean) pre_error = "previous setup could not be completed or rolled back";
        }
    }

    // Pipe ACL: istemci oturum kullanicisidir (broker_channel deseni).
    const auto user = inventory::read_user();
    const std::string sid = user.target_user_sid.value.value_or(user.session_user_sid.value.value_or(""));
    if (sid.empty()) return core::kExitBadArgs;
    auto srv = PipeServer::create(pipe_name_for(a.nonce), sid);
    if (!srv.ok()) return core::kExitHandshakeFailed;
    auto& pipe = *srv.value();
    if (!pipe.accept(kAcceptWindowMs).ok()) return core::kExitHandshakeFailed;
    auto identity = pipe.client_identity();
    if (!identity.ok() || !peer_matches(identity.value(), parent.pid, parent.create_time, parent.session, parent.image))
        return core::kExitParentInvalid;
    const std::uint32_t peer_pid = identity.value().pid;

    core::setup::SetupServerSession session(a.nonce, *a.parent_pid);
    auto hello = pipe.read_frame(kHelloTimeoutMs);
    if (!hello.ok()) return core::kExitHandshakeFailed;
    auto st = session.on_frame(hello.value(), peer_pid);
    if (st.send) (void)pipe.write_frame(*st.send, kSendTimeoutMs);
    if (!session.ready()) return core::kExitHandshakeFailed;

    auto req = pipe.read_frame(kRequestTimeoutMs);
    if (!req.ok()) return core::kExitHandshakeFailed;
    st = session.on_frame(req.value(), peer_pid);
    if (st.send) (void)pipe.write_frame(*st.send, kSendTimeoutMs);
    if (!st.run) return session.closed() && session.last_error().empty() ? core::kExitOk : core::kExitHandshakeFailed;

    core::setup::SetupReply reply;
    if (!pre_error.empty()) {
        reply.result = recovery;
        reply.result.ok = false;
        reply.error = pre_error;
    } else {
        // Ilerleme yazilamazsa (istemci gitti) islem yarida birakilmaz; journal + geri alma kurallari gecerli.
        NotifyingJournal notifying(journal, [&](const JournalEntry& e) {
            (void)pipe.write_frame(session.progress_frame(core::setup::SetupProgress{e.step, e.rec}), kSendTimeoutMs);
        });
        const std::string tx_id = random_nonce();
        switch (st.run->kind) {
            case core::setup::SetupKind::Install: {
                LARGE_INTEGER size{};
                const std::uint64_t bytes = GetFileSizeEx(static_cast<HANDLE>(source.h), &size) && size.QuadPart > 0
                                                ? static_cast<std::uint64_t>(size.QuadPart)
                                                : 0;
                const auto arp = core::setup::make_arp_values(layout, ops.version(), bytes, local_yyyymmdd());
                reply.result = core::setup::run_install(ops, notifying, arp, tx_id);
                break;
            }
            case core::setup::SetupKind::Uninstall:
                reply.result = core::setup::run_uninstall_program(ops, notifying, tx_id);
                break;
            case core::setup::SetupKind::Repair:
            {
                // Guncelleme sonrasi hizmet durmussa ARP DisplayVersion'u da calisan (onarimi yapan) surume ceker.
                core::setup::RepairOptions ro;
                ro.layout = &layout;
                ro.running_version = ops.version();
                reply.result = core::setup::run_repair(ops, notifying, tx_id, ro);
            }
                break;
        }
    }
    if (!pipe.write_frame(session.reply_frame(reply), kSendTimeoutMs).ok()) return core::kExitHandshakeFailed;

    auto bye = pipe.read_frame(kByeTimeoutMs);
    if (bye.ok()) {
        st = session.on_frame(bye.value(), peer_pid);
        if (st.send) (void)pipe.write_frame(*st.send, kSendTimeoutMs);
    }
    return core::kExitOk;
}

}  // namespace ef::platform::install
