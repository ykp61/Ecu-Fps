// MEAS-05: yakalama yasam dongusu. Tekrarli ac/iptal/kapat sonrasi acik tutamac ve ETW oturumu kalmamali;
// olcum maliyeti (CPU ms) ayri raporlanir. Yonetici degilse ETW yolu AccessDenied'dir ve o da sizinti
// birakmamali; yonetici ise gercek oturum acilip iptal edilir.
#include <windows.h>

#include <cstdio>
#include <atomic>
#include <string>

#include "measurement/frame/frame_session.h"
#include "measurement/frame/game_capture.h"
#include "measurement/telemetry/process_gpu.h"

namespace {
DWORD handles() {
    DWORD n = 0;
    GetProcessHandleCount(GetCurrentProcess(), &n);
    return n;
}
double cpu_ms() {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto t = [](FILETIME f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32 | f.dwLowDateTime) / 10000.0; };
    return t(k) + t(u);
}
}  // namespace

int main() {
    using namespace ef::measurement;
    // Isinma: ilk PDH/ETW yuklemesi tutamac acar (DLL, kayit anahtari); taban bundan sonra alinir.
    {
        ProcessGpuSampler s(GetCurrentProcessId());
        s.open();
        s.sample();
        FrameSession f;
        f.start_realtime();
        f.stop();
    }
    const DWORD h0 = handles();
    bool elevated_path = false;
    const double c0 = cpu_ms();
    for (int i = 0; i < 30; ++i) {
        FrameSession f;
        const auto r = f.start_realtime();
        if (r.ok()) {
            elevated_path = true;
            Sleep(100);
            f.poll();
            (void)f.take_presents();
        } else if (r.error != FrameSessionError::AccessDenied) {
            std::printf("capture_lifecycle: unexpected start error win32=%lu %s\n", r.win32, r.detail.c_str());
            return 1;
        }
        f.stop();  // iptal
        if (query_session(kFrameSessionName) == SessionPresence::Present) {
            std::printf("capture_lifecycle: orphan ETW session after stop (iter %d)\n", i);
            return 1;
        }
    }
    const double c1 = cpu_ms();
    for (int i = 0; i < 30; ++i) {
        ProcessGpuSampler s(GetCurrentProcessId());
        s.open();
        s.sample();
        s.close();
    }
    const double c2 = cpu_ms();
    // PDH tek sorgu 10 ornek maliyeti (kontrol/izleme maliyeti, 1 Hz)
    ProcessGpuSampler live(GetCurrentProcessId());
    live.open();
    const double c3 = cpu_ms();
    for (int i = 0; i < 10; ++i) live.sample();
    const double c4 = cpu_ms();
    live.close();
    const DWORD h1 = handles();
    std::printf("capture_lifecycle: elevated=%d etw_cycles=30 cpu_ms=%.1f pdh_cycles=30 cpu_ms=%.1f pdh_sample_avg_ms=%.2f handles %lu -> %lu\n",
                elevated_path, c1 - c0, c2 - c1, (c4 - c3) / 10.0, h0, h1);
    if (h1 > h0 + 4) {  // kucuk tolerans: CRT/loader ic tutamaclari
        std::printf("capture_lifecycle: handle leak\n");
        return 1;
    }
    // EF-X18/T04: hedef olcum bitmeden cikarsa kosu tamamlandi sayilmaz. Yalniz yonetici (ETW) yolunda;
    // pencere acmayan kisa omurlu cocuk surec (PING, CREATE_NO_WINDOW). Oyun degil: gercek oyun kabulu ayridir.
    if (elevated_path) {
        wchar_t sysdir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysdir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            std::printf("capture_lifecycle: exit_probe system directory unavailable\n");
            return 1;
        }
        std::wstring cmd = std::wstring(sysdir, n) + L"\\PING.EXE -n 3 127.0.0.1";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            std::printf("capture_lifecycle: exit_probe child not started (%lu)\n", GetLastError());
            return 1;
        }
        const auto id = query_identity(pi.dwProcessId);
        std::atomic<bool> never{false};
        const ULONGLONG t0 = GetTickCount64();
        const auto r = capture_game(id, 15.0, never, "exit_probe", 0.0);
        const double took_s = static_cast<double>(GetTickCount64() - t0) / 1000.0;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::printf("capture_lifecycle: exit_probe outcome=%s exited_after_s=%.2f took_s=%.2f valid=%d\n",
                    std::string(ef::core::measure::to_string(r.outcome)).c_str(),
                    r.target_exited_after_s ? *r.target_exited_after_s : -1.0, took_s, r.manifest.valid ? 1 : 0);
        if (r.outcome != ef::core::measure::CaptureOutcome::GameExited || !r.target_exited_after_s || took_s >= 14.0 ||
            r.manifest.valid)
            return 1;
        if (query_session(kFrameSessionName) == SessionPresence::Present) {
            std::printf("capture_lifecycle: orphan ETW session after exit_probe\n");
            return 1;
        }
    } else {
        std::printf("capture_lifecycle: exit_probe SKIP (not elevated)\n");
    }
    return 0;
}
