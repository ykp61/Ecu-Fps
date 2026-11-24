#include "platform/windows/cleanup_channel.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#include "core/serialization/json.h"
#include "platform/windows/cleanup/win_cleanup.h"
#include "platform/windows/games/live_permit.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/pipe.h"

namespace ef::platform {

namespace {

using core::cleanup::AdminProgress;
using core::cleanup::AdminRunRequest;
using core::cleanup::AdminScanRequest;
using core::cleanup::CleanupCategory;
using core::cleanup::CleanupPreview;
using core::cleanup::CleanupReport;
using core::json::Limits;

constexpr std::uint32_t kSendTimeoutMs = 5000;
constexpr std::uint32_t kHandshakeTimeoutMs = 10000;
constexpr std::uint32_t kConnectWindowMs = 20000;
constexpr std::uint32_t kScanTimeoutMs = 60000;
constexpr std::uint32_t kRunTimeoutMs = 10 * 60 * 1000;
constexpr std::uint64_t kHelperExitWaitMs = 5000;
// Saha 2026-11-17: yukseltilmis temizlik isci sureci, istemci bagliyken ama hic istek gelmezse sonsuza dek
// beklememeli (UI kapatilsa/askida kalsa bile kapatilamayan yukseltilmis surec kalirdi). Bu kadar sure hic istek
// cercevesi gelmezse temiz cikis; her istek zamanlayiciyi sifirlar, calisan istek kesilmez.
constexpr std::chrono::minutes kWorkerIdleLimit{10};

Limits hello_limits() {
    Limits l;
    l.max_bytes = 1024;
    l.max_depth = 4;
    l.max_string = 128;
    l.max_items = 8;
    return l;
}

bool hello_ok(std::string_view payload, const std::string& nonce, std::uint32_t parent_pid) {
    auto doc = core::json::parse(payload, hello_limits());
    if (!doc.ok() || !doc.value().is(core::json::Value::Type::Object)) return false;
    if (doc.value().keys().size() != 2) return false;
    for (const auto& k : doc.value().keys())
        if (k != "nonce" && k != "pid") return false;
    auto n = core::json::get_string(doc.value(), "nonce", 64);
    auto p = core::json::get_int(doc.value(), "pid", 1, 0xFFFFFFFFll);
    return n.ok() && p.ok() && core::is_valid_nonce(n.value()) && n.value() == nonce &&
           static_cast<std::uint32_t>(p.value()) == parent_pid;
}

void send_error(PipeEnd& pipe, std::uint64_t seq, const std::string& detail) {
    auto o = core::json::Value::object();
    o.set("error", core::json::Value::string(detail));
    (void)pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Error, seq, core::json::write(o)}, kSendTimeoutMs);
}

std::string ascii_volume(const std::wstring& volume) {
    std::string s;
    s.reserve(volume.size());
    for (const wchar_t c : volume) {
        if (c < 0x20 || c > 0x7E) return {};
        s.push_back(static_cast<char>(c));
    }
    return s;
}

std::string frame_kind(std::string_view payload) {
    auto doc = core::json::parse(payload, Limits{32 * 1024, 4, 16, 32});
    if (!doc.ok() || !doc.value().is(core::json::Value::Type::Object)) return {};
    auto k = core::json::get_string(doc.value(), "kind", 16);
    return k.ok() ? k.value() : std::string{};
}

std::string error_detail(const core::ipc::Frame& f) {
    auto doc = core::json::parse(f.payload, Limits{4096, 4, 512, 16});
    if (!doc.ok() || !doc.value().is(core::json::Value::Type::Object)) return "cleanup rejected";
    const auto* e = doc.value().find("error");
    return e && e->is(core::json::Value::Type::String) ? e->as_string() : "cleanup rejected";
}

// Calistirma sirasinda her dosya isleminden once iptal cercevesini yoklar; boylece Durdur sonraki dosyada etkir.
class PollingFs final : public core::cleanup::CleanupFs {
public:
    PollingFs(core::cleanup::CleanupFs& inner, std::function<void()> poll) : inner_(inner), poll_(std::move(poll)) {}

    core::cleanup::DirState probe_dir(const std::wstring& path) override {
        poll_();
        return inner_.probe_dir(path);
    }
    contracts::Result<std::vector<core::cleanup::FsEntry>> list(const std::wstring& dir) override {
        poll_();
        return inner_.list(dir);
    }
    core::cleanup::DeleteResult delete_file(const std::wstring& root, const std::wstring& path,
                                            const core::cleanup::FileExpect& expect) override {
        poll_();
        return inner_.delete_file(root, path, expect);
    }
    bool remove_empty_dir(const std::wstring& root, const std::wstring& path) override {
        poll_();
        return inner_.remove_empty_dir(root, path);
    }
    bool remove_owned_empty_dir(const std::wstring& path) override {
        poll_();
        return inner_.remove_owned_empty_dir(path);
    }
    contracts::Result<std::string> read_prefix(const std::wstring& path, std::size_t max_bytes) override {
        poll_();
        return inner_.read_prefix(path, max_bytes);
    }
    std::optional<std::wstring> volume_of(const std::wstring& path) override {
        poll_();
        return inner_.volume_of(path);
    }
    std::optional<std::uint64_t> free_bytes(const std::wstring& volume) override {
        poll_();
        return inner_.free_bytes(volume);
    }
    std::optional<core::cleanup::VolumeId> volume_id(const std::wstring& volume) override {
        poll_();
        return inner_.volume_id(volume);
    }
    contracts::Result<core::cleanup::RecycleBinInfo> query_recycle_bin(const std::wstring& volume) override {
        poll_();
        return inner_.query_recycle_bin(volume);
    }
    contracts::Status empty_recycle_bin(const std::wstring& volume) override {
        poll_();
        return inner_.empty_recycle_bin(volume);
    }

private:
    core::cleanup::CleanupFs& inner_;
    std::function<void()> poll_;
};

}  // namespace

struct CleanupAdminClient::Impl {
    std::unique_ptr<PipeClient> pipe;
    HANDLE proc = nullptr;
    std::uint64_t seq = 2;
    bool closed = false;

    ~Impl() {
        if (proc) CloseHandle(proc);
    }

    void terminate() {
        if (!proc) return;
        DWORD code = 0;
        if (GetExitCodeProcess(proc, &code) && code == STILL_ACTIVE) (void)TerminateProcess(proc, 1);
        (void)WaitForSingleObject(proc, static_cast<DWORD>(kHelperExitWaitMs));
    }
};

int run_cleanup_worker(const core::LaunchArgs& a, const ParentIdentity& parent) {
    if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;

    // Pipe ACL: yukseltilmis surec olsa da istemci oturum kullanicisidir (broker_channel deseni).
    const auto user = inventory::read_user();
    const std::string sid = user.target_user_sid.value.value_or(user.session_user_sid.value.value_or(""));
    if (sid.empty()) return core::kExitBadArgs;

    auto srv = PipeServer::create(pipe_name_for(a.nonce), sid);
    if (!srv.ok()) return core::kExitHandshakeFailed;
    auto& pipe = *srv.value();
    if (!pipe.accept(kConnectWindowMs).ok()) return core::kExitHandshakeFailed;

    auto identity = pipe.client_identity();
    if (!identity.ok() || !peer_matches(identity.value(), parent.pid, parent.create_time, parent.session, parent.image))
        return core::kExitParentInvalid;

    std::string nonce = a.nonce;
    auto hello = pipe.read_frame(kHandshakeTimeoutMs);
    if (!hello.ok() || hello.value().type != core::ipc::MsgType::Hello || hello.value().seq != 1)
        return core::kExitHandshakeFailed;
    if (!hello_ok(hello.value().payload, nonce, *a.parent_pid)) return core::kExitHandshakeFailed;
    nonce.clear();

    if (!pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::HelloAck, 2, "{\"v\":1}"}, kSendTimeoutMs).ok())
        return core::kExitHandshakeFailed;

    platform::cleanup::WinCleanupFs fs;
    auto env = platform::cleanup::discover_env();
    core::cleanup::CleanupPlan plan;
    bool scanned = false;
    // Silme istegi istemciden gelir; oyun kapisi yukseltilmis tarafta da bagimsiz denetlenir.
    games::LiveGamePermit permit(std::wstring(sid.begin(), sid.end()));
    std::string gate_reason;
    const auto gate_open = [&permit, &gate_reason] {
        const auto d = permit.verify(core::games::GuardedAction::DiskCleanup);
        if (!d.allowed) gate_reason = d.reason_key;
        return d.allowed;
    };

    auto last_activity = std::chrono::steady_clock::now();
    for (;;) {
        auto fr = pipe.read_frame(5000);
        if (!fr.ok()) {
            if (fr.error().detail == "timeout") {  // istemci hala bagli; yetim degil
                if (std::chrono::steady_clock::now() - last_activity >= kWorkerIdleLimit) return core::kExitOk;  // bosta: temiz cikis
                continue;
            }
            return core::kExitOk;  // istemci kapandi
        }
        last_activity = std::chrono::steady_clock::now();
        const auto& f = fr.value();
        if (f.type == core::ipc::MsgType::Bye) return core::kExitOk;
        if (f.type != core::ipc::MsgType::Request) {
            send_error(pipe, f.seq, "request expected");
            return core::kExitHandshakeFailed;
        }

        if (!scanned) {
            auto req = core::cleanup::decode_admin_scan_request(f.payload);
            if (!req.ok()) {
                send_error(pipe, f.seq, "scan request rejected: " + req.error().detail);
                return core::kExitHandshakeFailed;
            }
            std::wstring volume(req.value().volume.begin(), req.value().volume.end());
            plan = core::cleanup::preview(req.value().categories, env, fs, volume);
            scanned = true;
            std::vector<CleanupPreview> previews;
            for (const auto* p : [&] {
                     std::vector<const core::cleanup::CategoryPlan*> v;
                     for (const auto& cp : plan.categories) v.push_back(&cp);
                     return v;
                 }())
                previews.push_back(p->preview);
            if (!pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Response, f.seq,
                                                   core::cleanup::encode_admin_scan_result(previews)},
                                  kSendTimeoutMs)
                     .ok())
                return core::kExitHandshakeFailed;
            continue;
        }

        auto req = core::cleanup::decode_admin_run_request(f.payload);
        if (!req.ok()) {
            send_error(pipe, f.seq, "run request rejected: " + req.error().detail);
            return core::kExitHandshakeFailed;
        }
        std::atomic<bool> cancel{!gate_open()};  // kapaliysa hic silinmez, rapor durdurulmus doner
        auto last_gate = std::chrono::steady_clock::now();
        PollingFs polled(fs, [&] {
            if (cancel.load()) return;
            if (const auto now = std::chrono::steady_clock::now(); now - last_gate >= std::chrono::seconds(2)) {
                last_gate = now;
                if (!gate_open()) {
                    cancel.store(true);
                    return;
                }
            }
            auto p = pipe.poll_frame();
            if (!p.ok()) return;
            if (p.value() && p.value()->type == core::ipc::MsgType::Cancel) cancel.store(true);
        });
        core::cleanup::RunOptions opt;
        opt.cancel = &cancel;
        opt.on_progress = [&](const core::cleanup::CleanupProgress& pr) {
            const AdminProgress ap{pr.category, pr.done, pr.total};
            (void)pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Response, f.seq, core::cleanup::encode_admin_progress(ap)},
                                   kSendTimeoutMs);
        };
        auto rep = core::cleanup::run(plan, req.value().categories, polled, opt);
        if (rep.ok()) rep.value().gate_reason = gate_reason;
        last_activity = std::chrono::steady_clock::now();  // uzun calisan istek bitince bosta sayaci yeniden baslar
        if (!rep.ok()) {
            send_error(pipe, f.seq, "run failed: " + rep.error().detail);
            continue;
        }
        if (!pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Response, f.seq, core::cleanup::encode_admin_report(rep.value())},
                              kSendTimeoutMs)
                 .ok())
            return core::kExitHandshakeFailed;
    }
}

CleanupAdminLaunch launch_cleanup_admin(const std::wstring& volume) {
    CleanupAdminLaunch out;
    auto fail = [&out](std::string detail) {
        out.status = CleanupAdminStatus::Error;
        out.detail = std::move(detail);
        return std::move(out);  // unique_ptr uyesi: kopya degil tasima
    };
    const auto self_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
    const std::string nonce = random_nonce();
    if (!core::is_valid_nonce(nonce)) return fail("nonce generation failed");

    ShellElevationLauncher launcher;
    std::string launch_detail;
    core::LaunchArgs args;
    args.role = core::Role::CleanupWorker;
    args.parent_pid = self_pid;
    args.nonce = nonce;
    const auto outcome = launcher.launch_elevated(args, launch_detail);
    if (outcome == core::ElevationOutcome::Denied) {
        out.status = CleanupAdminStatus::UacDenied;  // yardimci baslamadi, hic istek yok
        out.detail = launch_detail.empty() ? "UAC denied" : launch_detail;
        return std::move(out);
    }
    if (outcome != core::ElevationOutcome::Started) return fail(launch_detail.empty() ? "elevation failed" : launch_detail);

    auto impl = std::make_unique<CleanupAdminClient::Impl>();
    impl->proc = static_cast<HANDLE>(launcher.take_process_handle());
    if (!impl->proc) return fail("helper process handle missing");
    auto fail_helper = [&out, &impl](std::string detail) {
        impl->terminate();
        out.status = CleanupAdminStatus::Error;
        out.detail = std::move(detail);
        return std::move(out);
    };

    const DWORD helper_pid = GetProcessId(impl->proc);
    if (helper_pid == 0) return fail_helper("helper pid unavailable");

    const std::uint64_t deadline = GetTickCount64() + kConnectWindowMs;
    for (;;) {
        auto c = PipeClient::connect(pipe_name_for(nonce), helper_pid, 250);
        if (c.ok()) {
            impl->pipe = std::move(c).value();
            break;
        }
        if (c.code() == contracts::ErrorCode::Denied) return fail_helper("cleanup pipe is not the helper process");
        if (WaitForSingleObject(impl->proc, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            (void)GetExitCodeProcess(impl->proc, &code);
            return fail_helper("cleanup helper exited early (" + std::to_string(code) + ")");
        }
        if (GetTickCount64() >= deadline) return fail_helper("cleanup pipe connect timeout");
    }

    if (auto s = impl->pipe->write_frame(core::ipc::make_hello(nonce, self_pid, 1), kSendTimeoutMs); !s.ok())
        return fail_helper("hello send failed: " + s.error().detail);
    auto ack = impl->pipe->read_frame(kHandshakeTimeoutMs);
    if (!ack.ok()) return fail_helper("hello ack: " + ack.error().detail);
    if (ack.value().type != core::ipc::MsgType::HelloAck || ack.value().seq != 2) return fail_helper("unexpected hello ack");

    AdminScanRequest sr;
    sr.categories = core::cleanup::admin_product_categories();
    sr.volume = ascii_volume(volume);
    if (!volume.empty() && sr.volume.empty()) return fail_helper("volume encoding failed");
    if (!impl->pipe->write_frame(core::ipc::Frame{core::ipc::MsgType::Request, 2, core::cleanup::encode_admin_scan_request(sr)},
                                 kSendTimeoutMs)
             .ok())
        return fail_helper("scan request send failed");
    auto scan = impl->pipe->read_frame(kScanTimeoutMs);
    if (!scan.ok()) return fail_helper("scan read: " + scan.error().detail);
    if (scan.value().type == core::ipc::MsgType::Error) return fail_helper("scan rejected: " + error_detail(scan.value()));
    if (scan.value().type != core::ipc::MsgType::Response || scan.value().seq != 2) return fail_helper("unexpected scan frame");
    auto previews = core::cleanup::decode_admin_scan_result(scan.value().payload);
    if (!previews.ok()) return fail_helper("scan decode: " + previews.error().detail);

    out.status = CleanupAdminStatus::Ok;
    out.previews = std::move(previews).value();
    auto client = std::unique_ptr<CleanupAdminClient>(new CleanupAdminClient());
    client->impl_ = std::move(impl);
    client->impl_->seq = 3;
    out.client = std::move(client);
    return std::move(out);
}

CleanupAdminClient::~CleanupAdminClient() { close(); }

bool CleanupAdminClient::closed() const noexcept { return !impl_ || impl_->closed; }

void CleanupAdminClient::close() {
    if (!impl_ || impl_->closed) return;
    if (impl_->pipe)
        (void)impl_->pipe->write_frame(core::ipc::Frame{core::ipc::MsgType::Bye, impl_->seq, "{}"}, kSendTimeoutMs);
    impl_->pipe.reset();
    (void)WaitForSingleObject(impl_->proc, static_cast<DWORD>(kHelperExitWaitMs));
    impl_->terminate();
    impl_->closed = true;
}

contracts::Result<CleanupReport> CleanupAdminClient::run(const std::vector<CleanupCategory>& categories,
                                                         const core::cleanup::RunOptions& opt) {
    using R = contracts::Result<CleanupReport>;
    if (!impl_ || impl_->closed || !impl_->pipe) return R::fail(contracts::ErrorCode::Denied, "administrator helper not ready");

    AdminRunRequest rr;
    rr.categories = categories;
    const std::uint64_t seq = impl_->seq++;
    if (!impl_->pipe
             ->write_frame(core::ipc::Frame{core::ipc::MsgType::Request, seq, core::cleanup::encode_admin_run_request(rr)},
                           kSendTimeoutMs)
             .ok())
        return R::fail(contracts::ErrorCode::IoError, "run request send failed");

    std::atomic<bool> stop_watch{false};
    std::thread watch;
    if (opt.cancel) {
        const std::atomic<bool>* cancel = opt.cancel;
        PipeClient* pipe = impl_->pipe.get();
        watch = std::thread([cancel, pipe, seq, &stop_watch] {
            while (!stop_watch.load()) {
                if (cancel->load()) {
                    (void)pipe->write_frame(core::ipc::Frame{core::ipc::MsgType::Cancel, seq, "{}"}, kSendTimeoutMs);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }
    auto stop_watcher = [&] {
        stop_watch.store(true);
        if (watch.joinable()) watch.join();
    };

    for (;;) {
        auto fr = impl_->pipe->read_frame(kRunTimeoutMs);
        if (!fr.ok()) {
            stop_watcher();
            impl_->terminate();
            impl_->closed = true;
            return R::fail(contracts::ErrorCode::IoError, "run result read: " + fr.error().detail);
        }
        const auto& f = fr.value();
        if (f.type == core::ipc::MsgType::Error) {
            stop_watcher();
            return R::fail(contracts::ErrorCode::Denied, error_detail(f));
        }
        if (f.type != core::ipc::MsgType::Response || f.seq != seq) {
            stop_watcher();
            impl_->terminate();
            impl_->closed = true;
            return R::fail(contracts::ErrorCode::IoError, "unexpected run frame");
        }
        const std::string kind = frame_kind(f.payload);
        if (kind == "progress") {
            auto pr = core::cleanup::decode_admin_progress(f.payload);
            if (pr.ok() && opt.on_progress)
                opt.on_progress(core::cleanup::CleanupProgress{pr.value().category, pr.value().done, pr.value().total});
            continue;
        }
        if (kind == "report") {
            auto rep = core::cleanup::decode_admin_report(f.payload);
            stop_watcher();
            if (!rep.ok()) return R::fail(contracts::ErrorCode::ParseError, "report decode: " + rep.error().detail);
            return rep.value();
        }
        stop_watcher();
        impl_->terminate();
        impl_->closed = true;
        return R::fail(contracts::ErrorCode::IoError, "unknown run frame kind");
    }
}

}  // namespace ef::platform
