#include "platform/windows/install/setup_worker_channel.h"

#include <windows.h>

#include <memory>
#include <utility>

#include "core/serialization/json.h"
#include "platform/windows/elevation.h"
#include "platform/windows/pipe.h"

namespace ef::platform::install {

namespace {

constexpr std::uint32_t kSendTimeoutMs = 5000;
constexpr std::uint32_t kHandshakeTimeoutMs = 10000;
// Isci pipe'i acmadan once makine kilidi (10 sn) + yarim kurulum kurtarmasi (hizmet durdur/baslat 30 sn) yapabilir.
constexpr ULONGLONG kConnectWindowMs = 180000;
// Iki ilerleme cercevesi arasi en uzun adim: hizmet durdur + baslat (2 x 30 sn) ve dosya kopyasi; genis pay.
constexpr std::uint32_t kFrameTimeoutMs = 5 * 60 * 1000;
constexpr DWORD kExitWaitMs = 5000;

struct Proc {
    HANDLE h = nullptr;
    ~Proc() {
        if (!h) return;
        (void)WaitForSingleObject(h, kExitWaitMs);  // isci Bye ile kendisi cikar; zorla sonlandirma yok (journal yarida kalmasin)
        CloseHandle(h);
    }
};

std::string error_detail(const core::ipc::Frame& f) {
    auto doc = core::json::parse(f.payload, core::json::Limits{4096, 4, 512, 16});
    if (!doc.ok() || !doc.value().is(core::json::Value::Type::Object)) return "setup worker error";
    const auto* e = doc.value().find("error");
    return e && e->is(core::json::Value::Type::String) ? e->as_string() : "setup worker error";
}

}  // namespace

SetupRun run_setup_worker_request(core::setup::SetupKind kind,
                                  const std::function<void(const core::setup::SetupProgress&)>& on_progress) {
    SetupRun out;
    const auto self_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
    const std::string nonce = random_nonce();
    if (!core::is_valid_nonce(nonce)) {
        out.detail = "nonce generation failed";
        return out;
    }

    // Denetim 3.3: UAC'den isci bitene kadar setup dosyasi yazilamaz / yeniden adlandirilamaz.
    const auto image_lock = SelfImageLock::acquire({});
    if (!image_lock.ok()) {
        out.detail = "setup file busy or replaced (lock failed, error " + std::to_string(image_lock.error()) + ")";
        return out;  // UAC acilmadi; hicbir sey yapilmadi
    }

    ShellElevationLauncher launcher;
    core::LaunchArgs args;
    args.role = core::Role::SetupWorker;
    args.parent_pid = self_pid;
    args.nonce = nonce;
    std::string launch_detail;
    out.elevation = launcher.launch_elevated(args, launch_detail);
    if (out.elevation != core::ElevationOutcome::Started) {
        out.detail = !launch_detail.empty() ? launch_detail
                     : out.elevation == core::ElevationOutcome::Denied ? "UAC denied"
                                                                       : "elevation failed";
        return out;  // isci baslamadi: hicbir sey yapilmadi
    }

    Proc proc{static_cast<HANDLE>(launcher.take_process_handle())};
    if (!proc.h) {
        out.detail = "setup worker handle missing";
        return out;
    }
    const DWORD worker_pid = GetProcessId(proc.h);
    if (worker_pid == 0) {
        out.detail = "setup worker pid unavailable";
        return out;
    }

    std::unique_ptr<PipeClient> pipe;
    const ULONGLONG deadline = GetTickCount64() + kConnectWindowMs;
    for (;;) {
        auto c = PipeClient::connect(pipe_name_for(nonce), worker_pid, 250);
        if (c.ok()) {
            pipe = std::move(c).value();
            break;
        }
        if (c.code() == contracts::ErrorCode::Denied) {  // pipe baska surece ait (ad gaspi): veri gonderilmez
            out.detail = "setup pipe is not the worker process";
            return out;
        }
        if (WaitForSingleObject(proc.h, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            (void)GetExitCodeProcess(proc.h, &code);
            out.detail = "setup worker exited early (" + std::to_string(code) + ")";
            out.worker_exit = static_cast<std::uint32_t>(code);
            return out;
        }
        if (GetTickCount64() >= deadline) {
            out.detail = "setup pipe connect timeout";
            return out;
        }
    }

    if (auto s = pipe->write_frame(core::ipc::make_hello(nonce, self_pid, 1), kSendTimeoutMs); !s.ok()) {
        out.detail = "hello send failed: " + s.error().detail;
        return out;
    }
    auto ack = pipe->read_frame(kHandshakeTimeoutMs);
    if (!ack.ok() || ack.value().type != core::ipc::MsgType::HelloAck) {
        out.detail = ack.ok() ? "unexpected hello ack" : "hello ack: " + ack.error().detail;
        return out;
    }
    std::uint64_t last_in = ack.value().seq;

    const core::setup::SetupRequest request{kind, false};
    // Buradan once her hata request_sent=false (isci istegi almadi: NotStarted). Yazma denemesinden itibaren true:
    // yarim yazilmis bir cerceve de "gitmedi" diye varsayilmaz (yanit yoksa sonuc bilinmez).
    out.request_sent = true;
    if (auto s = pipe->write_frame(core::ipc::Frame{core::ipc::MsgType::Request, 2, core::setup::encode_request(request)}, kSendTimeoutMs);
        !s.ok()) {
        out.detail = "request send failed: " + s.error().detail;  // istek gitmedi: isci hicbir sey yapmaz
        return out;
    }

    // Bu noktadan sonra yanit gelmezse sonuc bilinmez (reply=nullopt); basari uydurulmaz.
    for (;;) {
        auto fr = pipe->read_frame(kFrameTimeoutMs);
        if (!fr.ok()) {
            out.detail = "setup result unknown: " + fr.error().detail;
            return out;
        }
        const auto& f = fr.value();
        if (f.seq <= last_in) {
            out.detail = "setup result unknown: sequence violation";
            return out;
        }
        last_in = f.seq;
        if (f.type == core::ipc::MsgType::Error) {
            out.detail = "setup rejected: " + error_detail(f);
            return out;
        }
        if (f.type != core::ipc::MsgType::Response) {
            out.detail = "setup result unknown: unexpected frame";
            return out;
        }
        auto r = core::setup::decode_response(f.payload);
        if (!r.ok()) {
            out.detail = "setup result unknown: " + r.error().detail;
            return out;
        }
        if (r.value().progress) {
            if (on_progress) on_progress(*r.value().progress);
            continue;
        }
        out.reply = std::move(r.value().reply);
        (void)pipe->write_frame(core::ipc::Frame{core::ipc::MsgType::Bye, 3, "{}"}, kSendTimeoutMs);
        return out;
    }
}

}  // namespace ef::platform::install
