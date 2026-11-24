#include "platform/windows/adapters/win_fast_startup.h"

#include <windows.h>

#include <string>

namespace ef::platform::adapters {

using contracts::ErrorCode;

namespace {
constexpr wchar_t kKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power";
constexpr wchar_t kValue[] = L"HiberbootEnabled";

contracts::Status fail(const char* what, LSTATUS s) {
    return contracts::Status::fail(s == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, std::string(what) + " " + std::to_string(s));
}
}  // namespace

contracts::Result<std::optional<std::uint32_t>> WinFastStartupApi::read_pref() {
    using R = contracts::Result<std::optional<std::uint32_t>>;
    DWORD type = 0, data = 0, size = sizeof(data);
    const LSTATUS s = RegGetValueW(HKEY_LOCAL_MACHINE, kKey, kValue, RRF_RT_ANY | RRF_NOEXPAND, &type, &data, &size);
    if (s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return std::optional<std::uint32_t>{};
    if (s == ERROR_MORE_DATA) return R::fail(ErrorCode::ParseError, "HiberbootEnabled is not a DWORD");
    if (s != ERROR_SUCCESS) return R::fail(s == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "RegGetValue " + std::to_string(s));
    if (type != REG_DWORD || size != sizeof(DWORD)) return R::fail(ErrorCode::ParseError, "HiberbootEnabled is not a DWORD");  // dokunulmaz
    return std::optional<std::uint32_t>{static_cast<std::uint32_t>(data)};
}

contracts::Status WinFastStartupApi::write_pref(std::uint32_t v) {
    const DWORD d = v;
    const LSTATUS s = RegSetKeyValueW(HKEY_LOCAL_MACHINE, kKey, kValue, REG_DWORD, &d, sizeof(d));
    return s == ERROR_SUCCESS ? contracts::ok_status() : fail("RegSetKeyValue", s);
}

contracts::Status WinFastStartupApi::remove_pref() {
    const LSTATUS s = RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, kKey, kValue);
    if (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND) return contracts::ok_status();
    return fail("RegDeleteKeyValue", s);
}

}  // namespace ef::platform::adapters
