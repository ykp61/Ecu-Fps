#include "platform/windows/adapters/win_startup.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <filesystem>

#include "platform/windows/win_env.h"

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Status;
namespace cs = core::adapters::startup;

namespace {
constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedBase[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";

struct Where {
    HKEY root;
    REGSAM view;           // KEY_WOW64_64KEY / KEY_WOW64_32KEY (HKLM)
    const wchar_t* approved;  // StartupApproved alt anahtari
    bool folder;
};

Where where(cs::Location l) {
    switch (l) {
        case cs::Location::UserRun: return {HKEY_CURRENT_USER, 0, L"Run", false};
        case cs::Location::MachineRun64: return {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"Run", false};
        case cs::Location::MachineRun32: return {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"Run32", false};
        case cs::Location::UserFolder: return {HKEY_CURRENT_USER, 0, L"StartupFolder", true};
        case cs::Location::CommonFolder: return {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"StartupFolder", true};
    }
    return {HKEY_CURRENT_USER, 0, L"Run", false};
}

std::filesystem::path folder_of(cs::Location l) {
    PWSTR p = nullptr;
    const auto& id = l == cs::Location::UserFolder ? FOLDERID_Startup : FOLDERID_CommonStartup;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &p))) return {};
    std::filesystem::path out(p);
    CoTaskMemFree(p);
    return out;
}

ErrorCode map(LSTATUS e) { return e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError; }

std::string to_hex(const BYTE* b, DWORD n) {
    static constexpr char hx[] = "0123456789abcdef";
    std::string s;
    for (DWORD i = 0; i < n; ++i) {
        s += hx[b[i] >> 4];
        s += hx[b[i] & 15];
    }
    return s;
}
}  // namespace

contracts::Result<std::vector<cs::Entry>> WinStartupApi::list(cs::Location l) {
    using R = contracts::Result<std::vector<cs::Entry>>;
    std::vector<cs::Entry> out;
    const auto w = where(l);
    if (w.folder) {
        std::error_code ec;
        const auto dir = folder_of(l);
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const auto fn = e.path().filename().wstring();
            if (_wcsicmp(fn.c_str(), L"desktop.ini") == 0) continue;
            cs::Entry x{l, to_utf8(fn), to_utf8(e.path().wstring()), cs::Approval::Enabled};
            auto raw = get_raw(l, x.name);
            if (raw.ok() && raw.value()) x.approval = cs::approval_from_raw(*raw.value());
            out.push_back(std::move(x));
        }
        return out;
    }
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(w.root, kRun, 0, KEY_READ | w.view, &k);
    if (st == ERROR_FILE_NOT_FOUND) return out;
    if (st != ERROR_SUCCESS) return R::fail(map(st), "open Run");
    for (DWORD i = 0; i < 1024; ++i) {
        wchar_t name[16384];
        DWORD nlen = 16384, type = 0, dlen = 0;
        if (RegEnumValueW(k, i, name, &nlen, nullptr, &type, nullptr, &dlen) != ERROR_SUCCESS) break;
        std::wstring data(dlen / sizeof(wchar_t) + 1, L'\0');
        DWORD n2 = 16384;
        DWORD d2 = static_cast<DWORD>(data.size() * sizeof(wchar_t));
        RegEnumValueW(k, i, name, &n2, nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &d2);
        data.resize(wcslen(data.c_str()));
        cs::Entry x{l, to_utf8(name), (type == REG_SZ || type == REG_EXPAND_SZ) ? to_utf8(data) : std::string(), cs::Approval::Enabled};
        auto raw = get_raw(l, x.name);
        if (raw.ok() && raw.value()) x.approval = cs::approval_from_raw(*raw.value());
        out.push_back(std::move(x));
    }
    RegCloseKey(k);
    return out;
}

contracts::Result<bool> WinStartupApi::exists(cs::Location l, const std::string& name) {
    const auto w = where(l);
    if (w.folder) {
        std::error_code ec;
        return std::filesystem::is_regular_file(folder_of(l) / to_wide(name), ec);
    }
    const LSTATUS st = RegGetValueW(w.root, kRun, to_wide(name).c_str(), RRF_RT_ANY | (w.view == KEY_WOW64_32KEY ? RRF_SUBKEY_WOW6432KEY : (w.view ? RRF_SUBKEY_WOW6464KEY : 0)),
                                    nullptr, nullptr, nullptr);
    if (st == ERROR_FILE_NOT_FOUND) return false;
    if (st != ERROR_SUCCESS && st != ERROR_MORE_DATA) return contracts::Result<bool>::fail(map(st), "Run value");
    return true;
}

contracts::Result<std::optional<std::string>> WinStartupApi::get_raw(cs::Location l, const std::string& name) {
    using R = contracts::Result<std::optional<std::string>>;
    const auto w = where(l);
    const std::wstring key = std::wstring(kApprovedBase) + w.approved;
    BYTE buf[64];
    DWORD n = sizeof(buf);
    // StartupApproved anahtarlari 64 bit gorunumde (Run32 ayri alt anahtar).
    const LSTATUS st = RegGetValueW(w.root, key.c_str(), to_wide(name).c_str(), RRF_RT_REG_BINARY | (w.root == HKEY_LOCAL_MACHINE ? RRF_SUBKEY_WOW6464KEY : 0),
                                    nullptr, buf, &n);
    if (st == ERROR_FILE_NOT_FOUND) return std::optional<std::string>{};
    if (st != ERROR_SUCCESS) return R::fail(map(st), "StartupApproved read");
    if (n != 12) return R::fail(ErrorCode::ParseError, "unexpected StartupApproved size");
    return std::optional<std::string>{to_hex(buf, n)};
}

Status WinStartupApi::set_raw(cs::Location l, const std::string& name, const std::string& hex) {
    if (hex.size() != 24) return Status::fail(ErrorCode::ParseError, "raw size");
    BYTE b[12];
    for (int i = 0; i < 12; ++i) b[i] = static_cast<BYTE>(std::stoi(hex.substr(static_cast<std::size_t>(2 * i), 2), nullptr, 16));
    const auto w = where(l);
    const std::wstring key = std::wstring(kApprovedBase) + w.approved;
    HKEY k = nullptr;
    LSTATUS st = RegCreateKeyExW(w.root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE | (w.root == HKEY_LOCAL_MACHINE ? KEY_WOW64_64KEY : 0),
                                 nullptr, &k, nullptr);
    if (st != ERROR_SUCCESS) return Status::fail(map(st), "StartupApproved open");
    st = RegSetValueExW(k, to_wide(name).c_str(), 0, REG_BINARY, b, sizeof(b));
    RegCloseKey(k);
    return st == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(map(st), "StartupApproved write");
}

Status WinStartupApi::remove_raw(cs::Location l, const std::string& name) {
    const auto w = where(l);
    const std::wstring key = std::wstring(kApprovedBase) + w.approved;
    HKEY k = nullptr;
    LSTATUS st = RegOpenKeyExW(w.root, key.c_str(), 0, KEY_SET_VALUE | (w.root == HKEY_LOCAL_MACHINE ? KEY_WOW64_64KEY : 0), &k);
    if (st == ERROR_FILE_NOT_FOUND) return contracts::ok_status();
    if (st != ERROR_SUCCESS) return Status::fail(map(st), "StartupApproved open");
    st = RegDeleteValueW(k, to_wide(name).c_str());
    RegCloseKey(k);
    return (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? contracts::ok_status() : Status::fail(map(st), "StartupApproved delete");
}

std::uint64_t WinStartupApi::now_filetime() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

}  // namespace ef::platform::adapters
