// MEAS-03 salt okuma araci (ctest disi): verilen surecin (exe adi) PID'ye bagli GPU kullanimi/VRAM'i 5 sn.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>

#include "measurement/telemetry/process_gpu.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, argv[1]) == 0) pid = pe.th32ProcessID;
    CloseHandle(snap);
    if (!pid) {
        std::printf("gpu_now: surec yok\n");
        return 3;
    }
    ef::measurement::ProcessGpuSampler s(pid);
    s.open();
    for (int i = 0; i < 6; ++i) {
        Sleep(1000);
        const auto r = s.sample();
        if (i == 0) continue;  // yuzde icin taban
        std::printf("gpu_now: pid=%lu engine3d=%s%.1f%% dedicated=%s%.0f MiB reason=%s\n", pid, r.engine_3d_pct ? "" : "null:",
                    r.engine_3d_pct.value_or(0), r.dedicated_bytes ? "" : "null:", r.dedicated_bytes ? *r.dedicated_bytes / 1048576.0 : 0.0,
                    r.missing_reason.c_str());
    }
    return 0;
}
