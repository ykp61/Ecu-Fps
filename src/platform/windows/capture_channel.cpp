#include "platform/windows/capture_channel.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/ipc/protocol.h"
#include "core/serialization/json.h"
#include "measurement/frame/capture_result.h"
#include "measurement/frame/game_capture.h"
#include "platform/windows/elevation.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/pipe.h"

namespace ef::platform {

using contracts::ErrorCode;

namespace {

constexpr std::uint32_t kSendTimeoutMs = 5000;
constexpr std::uint32_t kHandshakeTimeoutMs = 10000;
constexpr std::uint32_t kChunkTimeoutMs = 30000;
constexpr std::uint64_t kConnectWindowMs = 20000;
constexpr std::uint64_t kHelperExitWaitMs = 5000;
constexpr double kMaxSeconds = 120.0;

core::json::Limits hello_limits() {
    core::json::Limits l;
    l.max_bytes = 1024;
    l.max_depth = 4;
    l.max_string = 128;
    l.max_items = 16;
    return l;
}

core::json::Limits request_limits() {
    core::json::Limits l;
    l.max_bytes = 2048;
    l.max_depth = 4;
    l.max_string = 256;
    l.max_items = 16;
    return l;
}

core::json::Limits error_limits() {
    core::json::Limits l;
    l.max_bytes = 2048;
    l.max_depth = 4;
    l.max_string = 512;
    l.max_items = 16;
    return l;
}

bool valid_app(std::string_view app) {
    if (app.empty() || app.size() > 128) return false;
    for (const char c : app) {
        if (c < 0x20 || c > 0x7E) return false;
        if (c == '\\' || c == '/' || c == ':') return false;  // uygulama kimligi, yol degil
    }
    return true;
}

struct CaptureRequest {
    std::uint32_t pid = 0;
    double seconds = 0;
    double min_seconds = 0;
    std::string app;
};

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

contracts::Result<double> request_number(const core::json::Value& obj, std::string_view key) {
    using R = contracts::Result<double>;
    const core::json::Value* v = obj.find(key);
    if (!v) return R::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(core::json::Value::Type::Int) && !v->is(core::json::Value::Type::Double))
        return R::fail(ErrorCode::ParseError, std::string(key) + " number expected");
    const double d = v->as_double();
    if (!std::isfinite(d)) return R::fail(ErrorCode::OutOfRange, std::string(key));
    return d;
}

contracts::Result<CaptureRequest> parse_request(std::string_view payload, std::uint32_t self_pid, std::uint32_t parent_pid) {
    using R = contracts::Result<CaptureRequest>;
    auto doc = core::json::parse(payload, request_limits());
    if (!doc.ok()) return R(doc.error());
    const core::json::Value& root = doc.value();
    if (!root.is(core::json::Value::Type::Object)) return R::fail(ErrorCode::ParseError, "request object expected");
    if (root.keys().size() != 5) return R::fail(ErrorCode::ParseError, "request fields");
    for (const auto& k : root.keys())
        if (k != "v" && k != "pid" && k != "seconds" && k != "min_seconds" && k != "app")
            return R::fail(ErrorCode::ParseError, "unknown request field " + k);

    auto v = core::json::get_int(root, "v", 1, 1000);
    if (!v.ok()) return R(v.error());
    if (v.value() != measurement::kCaptureResultVersion)
        return R::fail(v.value() > measurement::kCaptureResultVersion ? ErrorCode::SchemaTooNew : ErrorCode::InvalidEnum, "version");

    auto pid = core::json::get_int(root, "pid", 5, 0xFFFFFFFFll);  // pid > 4
    if (!pid.ok()) return R(pid.error());
    const auto target_pid = static_cast<std::uint32_t>(pid.value());
    if (target_pid == self_pid || target_pid == parent_pid) return R::fail(ErrorCode::OutOfRange, "target pid");

    auto seconds = request_number(root, "seconds");
    if (!seconds.ok()) return R(seconds.error());
    if (seconds.value() < 1.0 || seconds.value() > kMaxSeconds) return R::fail(ErrorCode::OutOfRange, "seconds");
    auto min_seconds = request_number(root, "min_seconds");
    if (!min_seconds.ok()) return R(min_seconds.error());
    if (min_seconds.value() < 0.0 || min_seconds.value() > kMaxSeconds) return R::fail(ErrorCode::OutOfRange, "min_seconds");
    if (min_seconds.value() > seconds.value()) return R::fail(ErrorCode::OutOfRange, "min_seconds > seconds");

    auto app = core::json::get_string(root, "app", 128);
    if (!app.ok() || !valid_app(app.value())) return R::fail(ErrorCode::ParseError, "app");

    CaptureRequest r;
    r.pid = target_pid;
    r.seconds = seconds.value();
    r.min_seconds = min_seconds.value();
    r.app = app.value();
    return r;
}

void send_error(PipeServer& pipe, std::uint64_t seq, const std::string& detail) {
    auto o = core::json::Value::object();
    o.set("error", core::json::Value::string(detail));
    (void)pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Error, seq, core::json::write(o)}, kSendTimeoutMs);
}

// ---- Istemci yardimcilari ----

struct ProcessGuard {
    HANDLE h = nullptr;
    ProcessGuard() = default;
    ~ProcessGuard() {
        if (h) CloseHandle(h);
    }
    ProcessGuard(const ProcessGuard&) = delete;
    ProcessGuard& operator=(const ProcessGuard&) = delete;

    void terminate() {
        if (!h) return;
        DWORD code = 0;
        if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE) (void)TerminateProcess(h, 1);
        (void)WaitForSingleObject(h, static_cast<DWORD>(kHelperExitWaitMs));
    }
};

// Iptal bekcisi: uzun yakalama/aktarim okumasi tek parca cerceve butunluguyle bloklanirken,
// cancel goruldugunde yardimci sureci sonlandirir. Boylece pipe okumasi hata ile hemen cozulur.
class CancelWatchdog {
public:
    CancelWatchdog(const std::atomic<bool>& cancel, HANDLE proc, DWORD pid)
        : cancel_(cancel), proc_(proc), pid_(pid), thread_([this] { run(); }) {}
    ~CancelWatchdog() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    CancelWatchdog(const CancelWatchdog&) = delete;
    CancelWatchdog& operator=(const CancelWatchdog&) = delete;

    void stop() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        while (!stop_.load()) {
            if (cancel_.load()) {
                DWORD code = 0;
                if (GetExitCodeProcess(proc_, &code) && code == STILL_ACTIVE) {
                    if (!TerminateProcess(proc_, 1)) {  // ShellExecuteEx tanitici izni yetersizse ayri tanitici ac
                        HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid_);
                        if (p) {
                            (void)TerminateProcess(p, 1);
                            CloseHandle(p);
                        }
                    }
                }
                return;
            }
            Sleep(50);
        }
    }

    const std::atomic<bool>& cancel_;
    HANDLE proc_;
    DWORD pid_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

struct BlockingRead {
    contracts::Result<core::ipc::Frame> frame;
    bool cancelled = false;
};

BlockingRead read_blocking(PipeClient& pipe, std::uint32_t timeout_ms, const std::atomic<bool>& cancel, HANDLE proc) {
    auto f = pipe.read_frame(timeout_ms);
    if (cancel.load()) {  // bekci yardimciyi sonlandirmis olabilir; okuma hatasi iptal sayilir
        return {contracts::Result<core::ipc::Frame>::fail(ErrorCode::Denied, "cancelled"), true};
    }
    if (!f.ok()) {
        DWORD code = 0;
        if (GetExitCodeProcess(proc, &code) && code != STILL_ACTIVE)
            return {contracts::Result<core::ipc::Frame>::fail(ErrorCode::IoError, "helper exited (" + std::to_string(code) + ")"), false};
    }
    return {std::move(f), false};
}

std::string frame_error_detail(const contracts::Result<core::ipc::Frame>& r) {
    return r.ok() ? std::string() : r.error().detail;
}

struct SeriesRead {
    bool cancelled = false;
    std::string error;  // bos = basari
};

// EF-X18: bir aralik serisini (render veya ekran) parcalar halinde okur; seq iki seri boyunca ortak sayac.
SeriesRead read_series(PipeClient& client, const std::atomic<bool>& cancel, HANDLE proc, std::uint64_t& seq,
                       std::size_t total_bytes, std::size_t count, std::vector<double>& out,
                       measurement::ChunkKind kind = measurement::ChunkKind::Interval) {
    out.reserve(count);
    std::size_t received = 0;
    while (received < total_bytes) {
        auto chunk_frame = read_blocking(client, kChunkTimeoutMs, cancel, proc);
        if (chunk_frame.cancelled) return {true, {}};
        if (!chunk_frame.frame.ok()) return {false, "chunk read: " + frame_error_detail(chunk_frame.frame)};
        const core::ipc::Frame& cf = chunk_frame.frame.value();
        if (cf.type != core::ipc::MsgType::Response || cf.seq != seq) return {false, "unexpected data frame"};
        ++seq;
        const std::size_t remaining = total_bytes - received;
        if (cf.payload.empty() || cf.payload.size() % sizeof(float) != 0 || cf.payload.size() > measurement::kMaxChunkBytes ||
            cf.payload.size() > remaining)
            return {false, "invalid chunk size"};
        auto values = measurement::decode_chunk(cf.payload, remaining / sizeof(float), kind);
        if (!values.ok()) return {false, "chunk decode: " + values.error().detail};
        received += cf.payload.size();
        out.insert(out.end(), values.value().begin(), values.value().end());
    }
    if (received != total_bytes || out.size() != count) return {false, "interval transfer mismatch"};
    return {};
}

}  // namespace

int run_capture_worker(const core::LaunchArgs& a, const ParentIdentity& parent) {
    if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;
    const auto self_pid = static_cast<std::uint32_t>(GetCurrentProcessId());

    // Pipe ACL: yukseltilmis surec olsa da istemci oturum kullanicisidir (broker_channel deseni).
    const auto user = inventory::read_user();
    const std::string sid = user.target_user_sid.value.value_or(user.session_user_sid.value.value_or(""));
    if (sid.empty()) return core::kExitBadArgs;

    auto srv = PipeServer::create(pipe_name_for(a.nonce), sid);
    if (!srv.ok()) return core::kExitHandshakeFailed;
    auto& pipe = *srv.value();
    if (!pipe.accept(static_cast<std::uint32_t>(kConnectWindowMs)).ok()) return core::kExitHandshakeFailed;

    // Ilk ve tek istemci: baslangicta dogrulanan ust surecle birebir ayni olmali (PID + olusturma zamani +
    // oturum + EXE). Yalniz PID yetmez; yeniden kullanilan PID ile araya girilemez.
    auto identity = pipe.client_identity();
    if (!identity.ok() || !peer_matches(identity.value(), parent.pid, parent.create_time, parent.session, parent.image))
        return core::kExitParentInvalid;

    std::string nonce = a.nonce;  // tek kullanimlik: Hello dogrulaninca silinir
    auto hello = pipe.read_frame(kHandshakeTimeoutMs);
    if (!hello.ok() || hello.value().type != core::ipc::MsgType::Hello || hello.value().seq != 1) return core::kExitHandshakeFailed;
    if (!hello_ok(hello.value().payload, nonce, *a.parent_pid)) return core::kExitHandshakeFailed;
    nonce.clear();

    if (!pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::HelloAck, 2, "{\"v\":1}"}, kSendTimeoutMs).ok())
        return core::kExitHandshakeFailed;

    auto request_frame = pipe.read_frame(kHandshakeTimeoutMs);
    if (!request_frame.ok() || request_frame.value().type != core::ipc::MsgType::Request || request_frame.value().seq != 2)
        return core::kExitHandshakeFailed;
    auto req = parse_request(request_frame.value().payload, self_pid, *a.parent_pid);
    if (!req.ok()) {
        send_error(pipe, 3, "request rejected: " + req.error().detail);
        return core::kExitHandshakeFailed;
    }

    // Hedef kimligi OS'tan okunur; overlay/bilinmeyen/yeniden kullanilmis PID yakalamaya girmez.
    const auto target = measurement::query_identity(req.value().pid);
    const auto target_error = core::measure::check_target(target, target);

    measurement::CaptureResultHeader header;
    std::vector<double> intervals, display_intervals, gpu_busy;
    if (target_error == core::measure::TargetError::None) {
        std::atomic<bool> never_cancel{false};  // iptal yalniz istemcinin bu sureci sonlandirmasiyla
        const auto result =
            measurement::capture_game(target, req.value().seconds, never_cancel, req.value().app, req.value().min_seconds);
        intervals = result.summary.render_intervals_ms;
        display_intervals = result.summary.display_intervals_ms;  // EF-X18: ekran araliklari da aktarilir
        gpu_busy = result.summary.render_gpu_busy_ms;  // v5: kare basi GPU mesgul suresi (bos = izlenmedi)
        header = measurement::header_from(result, target);
    } else {
        measurement::GameCaptureResult empty;
        header = measurement::header_from(empty, target);
        header.target_error = target_error;
        header.interval_count = 0;
        header.total_bytes = 0;
        header.display_interval_count = 0;
        header.display_total_bytes = 0;
        header.gpu_busy_count = 0;
        header.gpu_busy_total_bytes = 0;
    }

    // Baslik gonderilmeden once tum araliklar dogrulanir; yarim/bozuk aktarim baslamaz.
    for (const auto* series : {&intervals, &display_intervals})
        for (const double v : *series)
            if (!measurement::is_valid_interval(v)) {
                send_error(pipe, 3, "invalid interval data");
                return core::kExitHandshakeFailed;
            }
    for (const double v : gpu_busy)
        if (!measurement::is_valid_gpu_busy(v)) {
            send_error(pipe, 3, "invalid gpu busy data");
            return core::kExitHandshakeFailed;
        }
    if (header.interval_count > measurement::kMaxIntervalCount || header.total_bytes > measurement::kMaxTotalBytes ||
        header.display_interval_count > measurement::kMaxIntervalCount ||
        header.display_total_bytes > measurement::kMaxTotalBytes || header.gpu_busy_count > measurement::kMaxIntervalCount ||
        header.gpu_busy_total_bytes > measurement::kMaxTotalBytes ||
        (header.gpu_busy_count != 0 && header.gpu_busy_count != header.interval_count)) {
        send_error(pipe, 3, "capture result too large");
        return core::kExitHandshakeFailed;
    }
    const std::string head = measurement::encode_header(header);
    if (head.size() > core::ipc::kMaxPayload) {
        send_error(pipe, 3, "capture header too large");
        return core::kExitHandshakeFailed;
    }
    if (!pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Response, 3, head}, kSendTimeoutMs).ok())
        return core::kExitHandshakeFailed;

    std::uint64_t seq = 4;
    for (const auto* series : {&intervals, &display_intervals, &gpu_busy}) {  // render, ekran (EF-X18), GPU mesgul (v5)
        const auto kind = series == &gpu_busy ? measurement::ChunkKind::GpuBusy : measurement::ChunkKind::Interval;
        for (std::size_t off = 0; off < series->size(); off += measurement::kMaxChunkValues) {
            const std::size_t n = std::min(measurement::kMaxChunkValues, series->size() - off);
            std::string chunk;
            if (auto s = measurement::encode_chunk(series->data() + off, n, chunk, kind); !s.ok()) {
                send_error(pipe, seq, "interval encode failed");
                return core::kExitHandshakeFailed;
            }
            if (!pipe.write_frame(core::ipc::Frame{core::ipc::MsgType::Response, seq++, std::move(chunk)}, kSendTimeoutMs).ok())
                return core::kExitHandshakeFailed;
        }
    }

    // Istemci Bye gonderene kadar bekle (aktarimi okudugunu bildirir); zaman asiminda cik (yetim yok).
    (void)pipe.read_frame(kHandshakeTimeoutMs);
    return core::kExitOk;
}

CaptureClientResult capture_game_elevated(const core::measure::ProcessIdentity& target, double seconds, double min_seconds,
                                          std::string app, const std::atomic<bool>& cancel) {
    CaptureClientResult out;
    auto fail_now = [&out](std::string detail) -> CaptureClientResult {
        out.status = CaptureClientStatus::Error;
        out.detail = std::move(detail);
        return out;
    };

    if (!std::isfinite(seconds) || seconds < 1.0 || seconds > kMaxSeconds) return fail_now("seconds out of range");
    if (!std::isfinite(min_seconds) || min_seconds < 0.0 || min_seconds > kMaxSeconds) return fail_now("min_seconds out of range");
    if (min_seconds > seconds) return fail_now("min_seconds > seconds");
    const auto self_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
    if (target.pid <= 4 || target.pid == self_pid) return fail_now("target pid invalid");
    if (!valid_app(app)) return fail_now("app invalid");
    if (cancel.load()) {
        out.status = CaptureClientStatus::Cancelled;
        out.detail = "cancelled before launch";
        return out;
    }

    const std::string nonce = random_nonce();
    if (!core::is_valid_nonce(nonce)) return fail_now("nonce generation failed");

    ShellElevationLauncher launcher;
    std::string launch_detail;
    core::LaunchArgs args;
    args.role = core::Role::CaptureWorker;
    args.parent_pid = self_pid;
    args.nonce = nonce;
    const auto outcome = launcher.launch_elevated(args, launch_detail);
    if (outcome == core::ElevationOutcome::Denied) {
        out.status = CaptureClientStatus::UacDenied;  // UAC reddi: yardimci baslamadi, istek yok
        out.detail = launch_detail.empty() ? "UAC denied" : launch_detail;
        return out;
    }
    if (outcome != core::ElevationOutcome::Started) return fail_now(launch_detail.empty() ? "elevation failed" : launch_detail);

    ProcessGuard proc;
    proc.h = static_cast<HANDLE>(launcher.take_process_handle());
    if (!proc.h) return fail_now("helper process handle missing");
    auto fail_helper = [&out, &proc](std::string detail) -> CaptureClientResult {
        proc.terminate();  // yetim yardimci birakma
        out.status = CaptureClientStatus::Error;
        out.detail = std::move(detail);
        return out;
    };

    const DWORD helper_pid = GetProcessId(proc.h);
    if (helper_pid == 0) return fail_helper("helper pid unavailable");
    CancelWatchdog watchdog(cancel, proc.h, helper_pid);
    auto cancelled_now = [&out, &proc](std::string detail) -> CaptureClientResult {
        proc.terminate();  // bekci de sonlandirir; iki cagri da guvenli (idempotent)
        out.status = CaptureClientStatus::Cancelled;
        out.detail = std::move(detail);
        return out;
    };

    std::unique_ptr<PipeClient> client;
    const std::uint64_t connect_deadline = GetTickCount64() + kConnectWindowMs;
    for (;;) {
        if (cancel.load()) return cancelled_now("cancelled during launch");
        auto c = PipeClient::connect(pipe_name_for(nonce), helper_pid, 250);
        if (c.ok()) {
            client = std::move(c).value();
            break;
        }
        if (c.code() == ErrorCode::Denied) return fail_helper("capture pipe is not the helper process");
        if (WaitForSingleObject(proc.h, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            (void)GetExitCodeProcess(proc.h, &code);
            return fail_helper("capture helper exited early (" + std::to_string(code) + ")");
        }
        if (GetTickCount64() >= connect_deadline) return fail_helper("capture pipe connect timeout");
    }

    const auto hello = core::ipc::make_hello(nonce, self_pid, 1);
    if (auto s = client->write_frame(hello, kSendTimeoutMs); !s.ok()) return fail_helper("hello send failed: " + s.error().detail);
    auto ack = read_blocking(*client, kHandshakeTimeoutMs, cancel, proc.h);
    if (ack.cancelled) return cancelled_now("cancelled during handshake");
    if (!ack.frame.ok()) return fail_helper("hello ack: " + frame_error_detail(ack.frame));
    if (ack.frame.value().type != core::ipc::MsgType::HelloAck || ack.frame.value().seq != 2)
        return fail_helper("unexpected hello ack");

    auto request = core::json::Value::object();
    request.set("v", core::json::Value::integer(measurement::kCaptureResultVersion));
    request.set("pid", core::json::Value::integer(static_cast<std::int64_t>(target.pid)));
    request.set("seconds", core::json::Value::number(seconds));
    request.set("min_seconds", core::json::Value::number(min_seconds));
    request.set("app", core::json::Value::string(app));
    if (!client->write_frame(core::ipc::Frame{core::ipc::MsgType::Request, 2, core::json::write(request)}, kSendTimeoutMs).ok())
        return fail_helper("request send failed");

    // Baslik, yakalama suresi + 30 sn icinde gelmeli (tek parca okuma; cerceve butunlugu korunur).
    const auto capture_timeout_ms = static_cast<std::uint32_t>((seconds + 30.0) * 1000.0);
    auto header_frame = read_blocking(*client, capture_timeout_ms, cancel, proc.h);
    if (header_frame.cancelled) return cancelled_now("cancelled during capture");
    if (!header_frame.frame.ok()) return fail_helper("header read: " + frame_error_detail(header_frame.frame));
    const core::ipc::Frame& hf = header_frame.frame.value();
    if (hf.type == core::ipc::MsgType::Error) {
        const auto doc = core::json::parse(hf.payload, error_limits());
        const auto* e = doc.ok() ? doc.value().find("error") : nullptr;
        return fail_helper(e && e->is(core::json::Value::Type::String) ? "capture rejected: " + e->as_string() : "capture rejected");
    }
    if (hf.type != core::ipc::MsgType::Response || hf.seq != 3) return fail_helper("unexpected header frame");
    auto header = measurement::decode_header(hf.payload);
    if (!header.ok()) return fail_helper("header decode: " + header.error().detail);
    const auto& h = header.value();
    if (h.target_pid != target.pid) return fail_helper("target pid mismatch");
    if (target.create_time != 0 && h.target_create_time != target.create_time) return fail_helper("target identity mismatch");
    if (!target.image_path.empty() && h.target_image != target.image_path) return fail_helper("target image mismatch");

    std::uint64_t seq = 4;
    std::vector<double> intervals, display_intervals, gpu_busy;
    if (auto s = read_series(*client, cancel, proc.h, seq, h.total_bytes, h.interval_count, intervals);
        s.cancelled || !s.error.empty())
        return s.cancelled ? cancelled_now("cancelled during transfer") : fail_helper(s.error);
    if (auto s = read_series(*client, cancel, proc.h, seq, h.display_total_bytes, h.display_interval_count, display_intervals);
        s.cancelled || !s.error.empty())
        return s.cancelled ? cancelled_now("cancelled during transfer") : fail_helper("display " + s.error);
    if (auto s = read_series(*client, cancel, proc.h, seq, h.gpu_busy_total_bytes, h.gpu_busy_count, gpu_busy,
                             measurement::ChunkKind::GpuBusy);
        s.cancelled || !s.error.empty())
        return s.cancelled ? cancelled_now("cancelled during transfer") : fail_helper("gpu busy " + s.error);

    auto result = measurement::result_from(h, std::move(intervals), std::move(display_intervals), std::move(gpu_busy));
    if (!result.ok()) return fail_helper("result decode: " + result.error().detail);

    watchdog.stop();  // protokol bitti; mesru cikisi beklerken iptal bayragi yardimciyi oldurmesin
    (void)client->write_frame(core::ipc::Frame{core::ipc::MsgType::Bye, seq, "{}"}, kSendTimeoutMs);
    client.reset();
    (void)WaitForSingleObject(proc.h, static_cast<DWORD>(kHelperExitWaitMs));
    DWORD code = 0;
    if (GetExitCodeProcess(proc.h, &code) && code == STILL_ACTIVE) proc.terminate();

    out.status = CaptureClientStatus::Ok;
    out.capture = std::move(result).value();
    return out;
}

}  // namespace ef::platform
