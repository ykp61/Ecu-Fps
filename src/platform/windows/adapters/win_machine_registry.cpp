#include "platform/windows/adapters/win_machine_registry.h"

#include <windows.h>

#include <string>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using core::adapters::windows::RegRaw;

namespace {
constexpr std::size_t kMaxBytes = 4096;
constexpr REGSAM kView = KEY_WOW64_64KEY;

ErrorCode code_of(LSTATUS s) { return s == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError; }
}  // namespace

contracts::Result<std::optional<RegRaw>> WinMachineRegistryApi::read(std::wstring_view subkey, std::wstring_view name) {
    using R = contracts::Result<std::optional<RegRaw>>;
    const std::wstring sk(subkey), nm(name);
    HKEY k = nullptr;
    LSTATUS s = RegOpenKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, KEY_QUERY_VALUE | kView, &k);
    if (s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return std::optional<RegRaw>{};
    if (s != ERROR_SUCCESS) return R::fail(code_of(s), "RegOpenKeyEx " + std::to_string(s));
    DWORD type = 0, size = 0;
    s = RegQueryValueExW(k, nm.c_str(), nullptr, &type, nullptr, &size);
    if (s == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(k);
        return std::optional<RegRaw>{};
    }
    if (s != ERROR_SUCCESS) {
        RegCloseKey(k);
        return R::fail(code_of(s), "RegQueryValueEx " + std::to_string(s));
    }
    if (size > kMaxBytes) {
        RegCloseKey(k);
        return R::fail(ErrorCode::ParseLimit, "value too large");
    }
    RegRaw r;
    r.bytes.resize(size);
    s = RegQueryValueExW(k, nm.c_str(), nullptr, &type, size ? r.bytes.data() : nullptr, &size);
    RegCloseKey(k);
    if (s != ERROR_SUCCESS) return R::fail(code_of(s), "RegQueryValueEx " + std::to_string(s));
    r.bytes.resize(size);
    r.type = type;
    return std::optional<RegRaw>{std::move(r)};
}

contracts::Status WinMachineRegistryApi::write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) {
    if (v.bytes.size() > kMaxBytes || (v.type != REG_DWORD && v.type != REG_SZ && v.type != REG_EXPAND_SZ))
        return contracts::Status::fail(ErrorCode::OutOfRange, "unsupported value");
    const std::wstring sk(subkey), nm(name);
    HKEY k = nullptr;
    LSTATUS s = RegCreateKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, nullptr, 0, KEY_SET_VALUE | kView, nullptr, &k, nullptr);
    if (s != ERROR_SUCCESS) return contracts::Status::fail(code_of(s), "RegCreateKeyEx " + std::to_string(s));
    s = RegSetValueExW(k, nm.c_str(), 0, v.type, v.bytes.empty() ? nullptr : v.bytes.data(), static_cast<DWORD>(v.bytes.size()));
    RegCloseKey(k);
    if (s != ERROR_SUCCESS) return contracts::Status::fail(code_of(s), "RegSetValueEx " + std::to_string(s));
    return contracts::ok_status();
}

contracts::Status WinMachineRegistryApi::remove(std::wstring_view subkey, std::wstring_view name) {
    const std::wstring sk(subkey), nm(name);
    HKEY k = nullptr;
    LSTATUS s = RegOpenKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, KEY_SET_VALUE | kView, &k);
    if (s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return contracts::ok_status();
    if (s != ERROR_SUCCESS) return contracts::Status::fail(code_of(s), "RegOpenKeyEx " + std::to_string(s));
    s = RegDeleteValueW(k, nm.c_str());
    RegCloseKey(k);
    if (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND) return contracts::ok_status();
    return contracts::Status::fail(code_of(s), "RegDeleteValue " + std::to_string(s));
}

contracts::Result<bool> WinMachineRegistryApi::key_exists(std::wstring_view subkey) {
    using R = contracts::Result<bool>;
    const std::wstring sk(subkey);
    HKEY k = nullptr;
    const LSTATUS s = RegOpenKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, KEY_QUERY_VALUE | kView, &k);
    if (s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return false;
    if (s != ERROR_SUCCESS) return R::fail(code_of(s), "RegOpenKeyEx " + std::to_string(s));
    RegCloseKey(k);
    return true;
}

contracts::Status WinMachineRegistryApi::remove_key_if_empty(std::wstring_view subkey) {
    const std::wstring sk(subkey);
    HKEY k = nullptr;
    LSTATUS s = RegOpenKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, KEY_QUERY_VALUE | kView, &k);
    if (s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return contracts::ok_status();
    if (s != ERROR_SUCCESS) return contracts::Status::fail(code_of(s), "RegOpenKeyEx " + std::to_string(s));
    DWORD subkeys = 0, values = 0;
    s = RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    if (s != ERROR_SUCCESS) return contracts::Status::fail(code_of(s), "RegQueryInfoKey " + std::to_string(s));
    if (subkeys != 0 || values != 0) return contracts::ok_status();  // dolu: dokunma
    s = RegDeleteKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), kView, 0);
    if (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND) return contracts::ok_status();
    return contracts::Status::fail(code_of(s), "RegDeleteKeyEx " + std::to_string(s));
}

}  // namespace ef::platform::adapters
