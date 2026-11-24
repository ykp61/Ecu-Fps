#include "platform/windows/readiness/security_reader.h"

#include <windows.h>
#include <tbs.h>

namespace ef::platform::readiness {

namespace {

// DWORD deger: yoksa {true, nullopt}; okunamazsa {false, nullopt}.
std::pair<bool, std::optional<std::uint32_t>> read_dword(const wchar_t* path, const wchar_t* name) {
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &k);
    if (st == ERROR_FILE_NOT_FOUND) return {true, std::nullopt};
    if (st != ERROR_SUCCESS) return {false, std::nullopt};
    DWORD v = 0, size = sizeof v, type = 0;
    const LSTATUS q = RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(&v), &size);
    RegCloseKey(k);
    if (q == ERROR_FILE_NOT_FOUND) return {true, std::nullopt};
    if (q != ERROR_SUCCESS || type != REG_DWORD) return {false, std::nullopt};  // beklenmeyen tur: dokunulmaz, bilinmiyor
    return {true, static_cast<std::uint32_t>(v)};
}

}  // namespace

core::readiness::PlatformSecurity read_platform_security() {
    core::readiness::PlatformSecurity s;
    TPM_DEVICE_INFO info{};
    const TBS_RESULT r = Tbsi_GetDeviceInfo(sizeof info, &info);
    if (r == TBS_SUCCESS) {  // aygit var: bilinmeyen surum "yok" degil
        if (info.tpmVersion == TPM_VERSION_20) s.tpm_version = 2;
        else if (info.tpmVersion == TPM_VERSION_12) s.tpm_version = 1;
    }
    else if (r == static_cast<TBS_RESULT>(TBS_E_TPM_NOT_FOUND)) s.tpm_version = 0;
    // Guvenli Onyukleme: Windows'un yayimladigi durum degeri (okuma ayricaligi gerekmez).
    const auto [ok, sb] = read_dword(L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", L"UEFISecureBootEnabled");
    if (ok) s.secure_boot = sb.value_or(0) != 0;
    // BitLocker durumu standart kullanici icin guvenilir bicimde okunamaz: bilinmiyor (kurtarma anahtari hic okunmaz).
    return s;
}

MpoState read_mpo_state() {
    MpoState m;
    const auto [ok, v] = read_dword(L"SOFTWARE\\Microsoft\\Windows\\Dwm", L"OverlayTestMode");
    m.read_ok = ok;
    m.overlay_test_mode = v;
    return m;
}

}  // namespace ef::platform::readiness
