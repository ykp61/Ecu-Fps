// T12: salt okuma kullanim olgulari. Yazma yok; hizmet baslatilmaz (yazici okumasi spooler'a degil kayit defterine gider).
#include "platform/windows/readiness/feature_usage_reader.h"

#include <windows.h>
#include <devguid.h>
#include <setupapi.h>

#include <cwctype>
#include <filesystem>
#include <string>

namespace ef::platform::readiness {

using core::perf::FeatureUsage;

namespace {

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return s;
}

bool starts_with_ci(const std::wstring& s, const wchar_t* prefix) {
    const std::wstring p(prefix);
    if (s.size() < p.size()) return false;
    for (std::size_t i = 0; i < p.size(); ++i)
        if (std::towlower(s[i]) != std::towlower(p[i])) return false;
    return true;
}

struct DevSet {
    HDEVINFO h = INVALID_HANDLE_VALUE;
    ~DevSet() {
        if (h != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(h);
    }
};

// Eslesmis Bluetooth cihazi: Bluetooth sinifinda BTHENUM\DEV_* / BTHLE\DEV_* aygit dugumu.
std::optional<bool> bluetooth_devices() {
    DevSet set;
    set.h = SetupDiGetClassDevsW(&GUID_DEVCLASS_BLUETOOTH, nullptr, nullptr, DIGCF_PRESENT);
    if (set.h == INVALID_HANDLE_VALUE) return std::nullopt;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA d{};
        d.cbSize = sizeof(d);
        if (!SetupDiEnumDeviceInfo(set.h, i, &d)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            return std::nullopt;
        }
        wchar_t id[512]{};
        if (!SetupDiGetDeviceInstanceIdW(set.h, &d, id, 512, nullptr)) return std::nullopt;
        const std::wstring w(id);
        if (starts_with_ci(w, L"BTHENUM\\DEV_") || starts_with_ci(w, L"BTHLE\\DEV_")) return true;
    }
    return false;
}

// Tanimli yazicilar kayit defterinden (Print\Printers alt anahtarlari + Port); sanal yazicilar sayilmaz.
std::optional<bool> real_printers() {
    HKEY key = nullptr;
    const LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Print\\Printers", 0,
                                  KEY_READ | KEY_WOW64_64KEY, &key);
    if (rc == ERROR_FILE_NOT_FOUND) return false;
    if (rc != ERROR_SUCCESS) return std::nullopt;
    std::optional<bool> result = false;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256]{};
        DWORD n = 256;
        const LONG e = RegEnumKeyExW(key, i, name, &n, nullptr, nullptr, nullptr, nullptr);
        if (e == ERROR_NO_MORE_ITEMS) break;
        if (e != ERROR_SUCCESS) {
            result = std::nullopt;
            break;
        }
        wchar_t port[256]{};
        DWORD sz = sizeof(port) - sizeof(wchar_t);
        std::string port_utf8;
        if (RegGetValueW(key, name, L"Port", RRF_RT_REG_SZ, nullptr, port, &sz) == ERROR_SUCCESS) port_utf8 = narrow(port);
        if (!core::perf::is_virtual_printer(narrow(name), port_utf8)) {
            result = true;  // okunamayan port da gercek sayilir (temkinli: kullanimda)
            break;
        }
    }
    RegCloseKey(key);
    return result;
}

std::optional<bool> gaming_services_present() {
    HKEY key = nullptr;
    const LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\GamingServices", 0, KEY_READ | KEY_WOW64_64KEY, &key);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    if (rc == ERROR_FILE_NOT_FOUND) return false;
    return std::nullopt;
}

// Dizin var ve bos degil mi. Yok -> false; okunamadi -> nullopt.
std::optional<bool> dir_has_entries(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return ec ? std::nullopt : std::optional<bool>(false);
    std::filesystem::directory_iterator it(p, ec);
    if (ec) return std::nullopt;
    return it != std::filesystem::directory_iterator();
}

// Sabit suruculerde \XboxGames (Xbox uygulamasi/Game Pass oyun kurulumlari).
std::optional<bool> xbox_games_installed() {
    const DWORD mask = GetLogicalDrives();
    std::optional<bool> result = false;
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        const std::wstring root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        const auto r = dir_has_entries(root + L"XboxGames");
        if (r == std::optional<bool>(true)) return true;
        if (!r) result = std::nullopt;
    }
    return result;
}

// Xbox denetleyici / kablosuz adaptor: surucu hizmeti xboxgip, xb1usb veya xusb22 olan mevcut aygit.
std::optional<bool> xbox_peripherals() {
    DevSet set;
    set.h = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set.h == INVALID_HANDLE_VALUE) return std::nullopt;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA d{};
        d.cbSize = sizeof(d);
        if (!SetupDiEnumDeviceInfo(set.h, i, &d)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            return std::nullopt;
        }
        wchar_t svc[128]{};
        DWORD type = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(set.h, &d, SPDRP_SERVICE, &type, reinterpret_cast<PBYTE>(svc), sizeof(svc) - sizeof(wchar_t), nullptr))
            continue;  // hizmeti olmayan aygit
        const std::wstring s(svc);
        if (starts_with_ci(s, L"xboxgip") || starts_with_ci(s, L"xb1usb") || starts_with_ci(s, L"xusb22")) return true;
    }
    return false;
}

// Game Bar ekran/oyun kayitlari: Videos\Captures bos degil.
std::optional<bool> gamebar_captures() {
    wchar_t prof[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::nullopt;
    return dir_has_entries(std::filesystem::path(prof) / L"Videos" / L"Captures");
}

}  // namespace

FeatureUsage read_feature_usage() {
    FeatureUsage u;
    u.bluetooth_devices = bluetooth_devices();
    u.printers = real_printers();
    u.xbox_games = core::perf::combine_usage({gaming_services_present(), xbox_games_installed(), xbox_peripherals(), gamebar_captures()});
    return u;
}

}  // namespace ef::platform::readiness
