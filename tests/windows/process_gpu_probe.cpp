// MEAS-03: PID'ye bagli GPU sayaci gercek okuma (salt okuma, yonetici gerekmez).
// Hedef: dwm.exe (masaustu bilesimi her oturumda GPU kullanir). Var olmayan PID nullopt + neden vermeli.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>

#include "measurement/telemetry/process_gpu.h"

namespace {
DWORD find_pid(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{sizeof(pe)};
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, exe) == 0) {
            pid = pe.th32ProcessID;
            break;
        }
    CloseHandle(snap);
    return pid;
}
}  // namespace

int main() {
    const DWORD dwm = find_pid(L"dwm.exe");
    if (!dwm) {
        std::printf("process_gpu: dwm.exe yok (oturumsuz ortam) - atlandi\n");
        return 0;
    }
    ef::measurement::ProcessGpuSampler s(dwm);
    if (!s.open()) {
        std::printf("process_gpu: sayac acilamadi (GPU sayaclari yok) - atlandi\n");
        return 0;
    }
    Sleep(1000);
    auto r = s.sample();
    Sleep(1000);
    r = s.sample();
    std::printf("process_gpu: pid=%lu dedicated=%s%llu engine3d=%s%.2f luid=%s adapters=%zu reason=%s\n", dwm, r.dedicated_bytes ? "" : "null:",
                r.dedicated_bytes.value_or(0), r.engine_3d_pct ? "" : "null:", r.engine_3d_pct.value_or(0),
                r.engine_luid.empty() ? "-" : r.engine_luid.c_str(), r.engine_adapters, r.missing_reason.c_str());
    // Spec 2026-11-12 B: 3D motor degeri LUID bazindadir; deger varsa secilen bagdastirici da bildirilir
    // ("hhhhhhhh:llllllll"), deger yoksa LUID uydurulmaz.
    if (r.engine_3d_pct.has_value() != !r.engine_luid.empty()) return 1;
    if (r.engine_3d_pct && (r.engine_luid.size() != 17 || r.engine_luid[8] != ':' || r.engine_adapters == 0 || *r.engine_3d_pct < 0)) return 1;
    // Var olmayan PID: deger uydurulmamali.
    ef::measurement::ProcessGpuSampler none(0xFFFFFFF0u);
    none.open();
    Sleep(200);
    none.sample();
    const auto n = none.sample();
    std::printf("process_gpu: missing pid -> dedicated=%d engine=%d reason=%s\n", n.dedicated_bytes.has_value(), n.engine_3d_pct.has_value(),
                n.missing_reason.c_str());
    if (n.dedicated_bytes || n.engine_3d_pct || n.missing_reason.empty()) return 1;
    // dwm.exe icin surec sayaci adaptor toplamini asabiliyor (gozlendi): ya makul deger ya da acik neden.
    if (!r.dedicated_bytes && r.missing_reason != "process_exceeds_adapter_usage") return 1;
    // explorer.exe: makul surec degeri beklenir (GPU kullaniyorsa).
    if (const DWORD ex = find_pid(L"explorer.exe")) {
        ef::measurement::ProcessGpuSampler e(ex);
        e.open();
        Sleep(500);
        e.sample();
        const auto x = e.sample();
        std::printf("process_gpu: explorer pid=%lu dedicated=%s%llu reason=%s\n", ex, x.dedicated_bytes ? "" : "null:", x.dedicated_bytes.value_or(0),
                    x.missing_reason.c_str());
    }
    return 0;
}
