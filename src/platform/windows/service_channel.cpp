#include "platform/windows/service_channel.h"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "core/service/channel_choice.h"
#include "core/service/service_session.h"
#include "core/setup/layout.h"
#include "platform/windows/admin_exit_gate.h"
#include "platform/windows/elevation.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/pipe.h"

namespace ef::platform {

namespace {

using contracts::ErrorCode;
namespace broker = core::broker;
namespace svc = core::service;

constexpr std::uint32_t kConnectMs = 3000;
constexpr std::uint32_t kSendTimeoutMs = 5000;
// Hizmet Hello'dan once makine kilidini alir ve yarim kayitlari uzlastirir (broker ile ayni sira): bekleme payi.
constexpr std::uint32_t kHelloReplyMs = 30000;
constexpr std::uint32_t kReplyTimeoutMs = 30000;
constexpr std::uint32_t kRestorePointReplyMs = 300000;
constexpr std::uint64_t kLongReplyCapMs = 6ull * 3600ull * 1000ull;  // bakim / surucu kurulumu ust siniri
constexpr std::uint32_t kReplySliceMs = 1000;                         // B2: cikis kapisi yokleme araligi

std::uint64_t reply_budget_ms(core::ipc::RequestAction a, const std::vector<contracts::SettingOperation>& ops) {
    using A = core::ipc::RequestAction;
    // T08: Apply toplu isteginde geri yukleme noktasi varsa her yanit icin uzun payi (SRSetRestorePoint bir dakikayi bulabilir).
    for (const auto& op : ops)
        if (core::broker::request_action_for(a, op) == A::RestorePoint) return kRestorePointReplyMs;
    if (a == A::RestorePoint) return kRestorePointReplyMs;
    if (a == A::Maintenance || a == A::DriverInstall || a == A::VendorInstall) return kLongReplyCapMs;
    return kReplyTimeoutMs;
}

class ServicePipeTransport final : public broker::FrameTransport {
public:
    ServicePipeTransport(std::unique_ptr<PipeClient> c, std::uint64_t reply_ms) : c_(std::move(c)), reply_ms_(reply_ms) {}
    contracts::Status send(const core::ipc::Frame& f) override {
        if (admin_exit_gate().abandoned()) return contracts::Status::fail(ErrorCode::IoError, "abandoned at exit");
        return c_->write_frame(f, kSendTimeoutMs);
    }
    contracts::Result<core::ipc::Frame> receive() override {
        // B2: dilimli bekleme, her dilimde cikis kapisi. Uzun is: sure dolunca "bitti" sayilmaz; hizmet baglantiyi actigi
        // surece beklenir (makine kilidi ve journal hizmette; cikista birakilan isi hizmet bitirir).
        const auto start = GetTickCount64();
        for (;;) {
            if (admin_exit_gate().abandoned()) return contracts::Result<core::ipc::Frame>::fail(ErrorCode::IoError, "abandoned at exit");
            auto f = c_->read_frame(kReplySliceMs);
            if (f.ok() || f.error().detail != "timeout") return f;
            if (GetTickCount64() - start >= reply_ms_) return f;
        }
    }

private:
    std::unique_ptr<PipeClient> c_;
    std::uint64_t reply_ms_;
};

struct ScmFacts {
    bool exists = false, running = false, config_ok = false;
    std::uint32_t pid = 0;
};

struct ScHandle {
    SC_HANDLE h = nullptr;
    ~ScHandle() {
        if (h) CloseServiceHandle(h);
    }
};

// Salt okuma SCM sorgusu (SC_MANAGER_CONNECT + SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG). Hicbir sey degistirmez.
ScmFacts query_scm(const core::setup::InstallLayout& layout) {
    ScmFacts out;
    ScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!scm.h) return out;
    const std::wstring name(core::setup::kServiceName);
    ScHandle s{OpenServiceW(scm.h, name.c_str(), SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG)};
    if (!s.h) return out;
    out.exists = true;
    SERVICE_STATUS_PROCESS ssp{};
    DWORD n = 0;
    if (QueryServiceStatusEx(s.h, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &n)) {
        out.running = ssp.dwCurrentState == SERVICE_RUNNING;
        out.pid = ssp.dwProcessId;
    }
    DWORD need = 0;
    (void)QueryServiceConfigW(s.h, nullptr, 0, &need);
    if (need == 0 || need > 64 * 1024) return out;
    std::vector<std::uint8_t> buf(need);
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
    if (!QueryServiceConfigW(s.h, cfg, need, &need) || !cfg->lpBinaryPathName) return out;
    const std::wstring expected = core::setup::service_image_path(layout);
    const std::wstring actual = cfg->lpBinaryPathName;
    out.config_ok = !expected.empty() && actual.size() == expected.size() &&
                    CompareStringOrdinal(actual.c_str(), static_cast<int>(actual.size()), expected.c_str(),
                                         static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
    return out;
}

}  // namespace

std::unique_ptr<PipeClient> connect_helper_service(core::service::ServiceProbe& probe, std::uint32_t self_pid) {
    const auto layout = core::setup::make_layout(known_roots());
    probe = {};
    probe.ui_installed = core::setup::is_installed_location(normalized_self_exe(), layout);
    // K27: hizmet yalniz yonetici grubu uyelerine hizmet verir; standart hesap hic baglanmaz (hizmet de reddederdi).
    probe.admin_member = probe.ui_installed && self_admin_member();
    std::unique_ptr<PipeClient> client;
    if (probe.ui_installed && probe.admin_member) {
        const auto scm = query_scm(layout);
        probe.exists = scm.exists;
        probe.running = scm.running;
        probe.config_ok = scm.config_ok;
        probe.scm_pid = scm.pid;
        if (scm.exists && scm.running && scm.config_ok && scm.pid != 0) {
            // Sunucu PID'i == SCM PID'i degilse (ad gaspi) veri gonderilmeden kapanir.
            auto c = PipeClient::connect_service(std::wstring(core::setup::kPipeName), scm.pid, kConnectMs);
            if (c.ok()) {
                client = std::move(c).value();
                probe.connected = true;
                probe.pipe_server_pid = client->server_pid();
            }
        }
    }
    if (client && probe.pipe_server_pid == probe.scm_pid) {
        if (client->write_frame(svc::make_service_hello(self_pid, 1), kSendTimeoutMs).ok()) {
            auto ack = client->read_frame(kHelloReplyMs);
            probe.hello_ok = ack.ok() && ack.value().type == core::ipc::MsgType::HelloAck;
        }
    }
    if (svc::choose_path(probe) != svc::Path::Service) return nullptr;
    return client;
}

bool helper_service_ready() {
    const auto layout = core::setup::make_layout(known_roots());
    if (!core::setup::is_installed_location(normalized_self_exe(), layout) || !self_admin_member()) return false;
    const auto scm = query_scm(layout);
    return scm.exists && scm.running && scm.config_ok && scm.pid != 0;
}

broker::BatchResult ServiceFirstChannel::run(core::ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops) {
    // Surucu / resmi paket kurulumu ve DNS degistirme: her zaman UAC broker'i (channel_choice.h service_handles_op).
    AdminExitGate::Call in_call(admin_exit_gate());
    if (admin_exit_gate().abandoned()) {  // B2: cikis basladi; hizmete/broker'a yeni istek yok
        broker::BatchResult r;
        r.detail = "abandoned at exit";
        return r;
    }
    const bool only = service_only_.load(std::memory_order_relaxed);
    const auto no_uac = [&](const char* why) {  // deney: UAC acilmaz, hicbir istek gonderilmez
        broker::BatchResult r;
        r.detail = why;
        return r;
    };
    if (!svc::service_handles_ops(action, ops)) return only ? no_uac("service-only: operation needs UAC broker") : broker_.run(action, ops);
    const auto self_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
    svc::ServiceProbe probe;
    auto client = connect_helper_service(probe, self_pid);
    if (!client) {
        // SCM hazir gorundugu halde dusus (PID/Hello): UI'nin "UAC sorulmaz" metni artik dogru degil.
        fell_back_.store(probe.ui_installed && probe.admin_member && probe.exists && probe.running && probe.config_ok,
                         std::memory_order_relaxed);
        if (only) return no_uac("service-only: helper service unavailable");
        return broker_.run(action, ops);  // hic Request gonderilmedi: bugunku UAC broker yolu
    }
    fell_back_.store(false, std::memory_order_relaxed);
    // Hizmet yolu: buradan sonra geri donus YOK. Yanitsiz istek run_admin_batch'te unknown kalir.
    std::unique_ptr<broker::FrameTransport> pending = std::make_unique<svc::HandshakenTransport>(
        std::make_unique<ServicePipeTransport>(std::move(client), reply_budget_ms(action, ops)), 1);
    broker::Connector connect = [&pending]() -> contracts::Result<std::unique_ptr<broker::FrameTransport>> {
        using R = contracts::Result<std::unique_ptr<broker::FrameTransport>>;
        if (!pending) return R::fail(ErrorCode::IoError, "service transport already used");
        return std::move(pending);
    };
    svc::NoUacLauncher launcher;
    return broker::run_admin_batch(launcher, self_pid, random_nonce(), connect, action, ops);
}

}  // namespace ef::platform
