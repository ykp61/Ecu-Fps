// GPU-01 Windows testi: ef_vendor_probe [harden]
// Her uretici runtime'ini guvenli yukleyicinin nasil cozdugunu yazar; ayrica naif LoadLibrary'nin
// taklit DLL'i yukleyip yuklemedigini gosterir (karsilastirma).
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "core/vendors/dispatch.h"
#include "platform/windows/vendor_runtime.h"
#include "platform/windows/win_env.h"

using namespace ef;

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wcscmp(argv[1], L"harden") == 0) platform::harden_dll_search();
    int fake_loaded = 0;
    for (auto v : {core::vendors::Vendor::Nvidia, core::vendors::Vendor::Amd, core::vendors::Vendor::Intel}) {
        auto r = platform::load_vendor_runtime(v);
        std::printf("%s state=%s version=%s path=%s detail=%s\n", core::vendors::to_string(v).data(),
                    core::vendors::to_string(r.info.state).data(), r.info.file_version.c_str(), r.info.path.c_str(),
                    r.info.detail.c_str());
        // Yuklu modul EXE klasorundeki taklit olmamali
        if (r.module) {
            wchar_t p[MAX_PATH];
            GetModuleFileNameW(static_cast<HMODULE>(r.module), p, MAX_PATH);
            wchar_t sys[MAX_PATH];
            GetSystemDirectoryW(sys, MAX_PATH);
            if (_wcsnicmp(p, sys, wcslen(sys)) != 0) fake_loaded = 1;
        }
        platform::unload_vendor_runtime(r);
    }
    return fake_loaded ? 1 : 0;
}
