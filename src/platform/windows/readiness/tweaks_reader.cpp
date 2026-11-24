// F-52: NetworkThrottlingIndex / SystemResponsiveness ve GlobalTimerResolutionRequests
// degerlerini salt okuma olarak alir. Erisim/okuma hatasinda alan bilinmiyor kalir; varsayilan
// atanmaz. Deger yoksa kurallar Windows varsayilanini kullanir.
#include "platform/windows/readiness/tweaks_reader.h"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace ef::platform::readiness {

namespace {

// nullopt + error=false: deger yok (varsayilan gecerli); nullopt + error=true: okuma hatasi.
std::optional<std::uint32_t> reg_dword(HKEY root, const wchar_t* key, const wchar_t* name, bool& error) {
    DWORD v = 0, sz = sizeof(v);
    const LSTATUS st = RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    if (st == ERROR_SUCCESS) return static_cast<std::uint32_t>(v);
    if (st != ERROR_FILE_NOT_FOUND && st != ERROR_PATH_NOT_FOUND) error = true;
    return std::nullopt;
}

}  // namespace

core::readiness::SystemTweaksInfo read_system_tweaks() {
    core::readiness::SystemTweaksInfo t;
    {
        // Belgelendirilmis multimedya profil anahtari; varsayilanlar 10 ve 20.
        constexpr const wchar_t* kProfile =
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
        bool err = false;
        t.network_throttling_index = reg_dword(HKEY_LOCAL_MACHINE, kProfile, L"NetworkThrottlingIndex", err);
        t.system_responsiveness = reg_dword(HKEY_LOCAL_MACHINE, kProfile, L"SystemResponsiveness", err);
        t.network_readable = !err;  // deger yoksa varsayilan gecerli
    }
    {
        // Topluluk kaynaklarinda yaygin tweak; genel belgelenmis API'si yok. Salt okuma.
        constexpr const wchar_t* kKernel = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";
        bool err = false;
        t.global_timer_resolution_requests =
            reg_dword(HKEY_LOCAL_MACHINE, kKernel, L"GlobalTimerResolutionRequests", err);
        t.timer_readable = !err;
    }
    return t;
}

}  // namespace ef::platform::readiness
