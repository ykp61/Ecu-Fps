// SEC-03 Windows testi:
//   lock <name> <hold_ms> <out_file>   -> kilidi al, "acquired <tick>"/"busy" yaz, hold_ms tut
//   hive                               -> HKU\<oturum SID>\Control Panel\International == HKCU degerleri
//   file <root> <relative>             -> open_verified_file sonucunu yaz (ok / reject:<neden>)

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <fstream>
#include <string>

#include "platform/windows/inventory/os_user.h"
#include "platform/windows/secure_targets.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"

using namespace ef;

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const std::wstring mode = argv[1];
    if (mode == L"lock" && argc >= 5) {
        auto l = platform::OperationLock::acquire(argv[2], 200);
        std::ofstream out(argv[4], std::ios::app);
        if (!l.ok()) {
            out << "busy\n";
            return 3;
        }
        out << "acquired " << GetTickCount64() << "\n";
        out.close();
        Sleep(static_cast<DWORD>(std::wcstoul(argv[3], nullptr, 10)));
        return 0;
    }
    if (mode == L"hive") {
        const auto u = platform::inventory::read_user();
        if (!u.target_user_sid.has()) return 4;
        auto k = platform::open_user_key(*u.target_user_sid.value, L"Control Panel\\International", false);
        if (!k.ok()) return 5;
        wchar_t a[128] = {}, b[128] = {};
        DWORD sa = sizeof(a), sb = sizeof(b);
        const auto ra = RegGetValueW(static_cast<HKEY>(k.value()), nullptr, L"LocaleName", RRF_RT_REG_SZ, nullptr, a, &sa);
        const auto rb = RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\International", L"LocaleName", RRF_RT_REG_SZ, nullptr, b, &sb);
        RegCloseKey(static_cast<HKEY>(k.value()));
        if (ra != ERROR_SUCCESS || rb != ERROR_SUCCESS) return 6;
        std::printf("hive LocaleName=%s\n", platform::to_utf8(a).c_str());
        if (std::wcscmp(a, b) != 0) return 7;
        // Kullanici olmayan SID reddedilir
        if (platform::open_user_key("S-1-5-18", L"", false).ok()) return 8;
        return 0;
    }
    if (mode == L"autostart") {
        // Gercek Run anahtarina dokunmadan ayri test anahtari kullanilir ve sonunda silinir.
        const std::wstring key = L"Software\\EcuFpsTest\\Run";
        if (auto pre = platform::autostart_enabled(key); pre.ok() && pre.value()) return 10;
        if (!platform::set_autostart(true, key).ok()) return 11;
        auto on = platform::autostart_enabled(key);
        if (!on.ok() || !on.value()) return 12;
        if (!platform::set_autostart(false, key).ok()) return 13;
        auto off = platform::autostart_enabled(key);
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\EcuFpsTest");
        if (!off.ok() || off.value()) return 14;
        std::printf("autostart ok: %s\n", platform::to_utf8(platform::autostart_command()).c_str());
        return 0;
    }
    if (mode == L"file" && argc >= 4) {
        auto f = platform::open_verified_file(argv[2], argv[3], false);
        if (f.ok()) {
            std::printf("ok %s\n", platform::to_utf8(f.value().final_path).c_str());
            platform::close_verified(f.value());
            return 0;
        }
        std::printf("reject:%s\n", f.error().detail.c_str());
        return 9;
    }
    return 2;
}
