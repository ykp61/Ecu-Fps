#include "platform/windows/run_key_store.h"

#include <windows.h>

#include "core/logging/log.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using contracts::ErrorCode;

WinRunKeyStore::WinRunKeyStore(std::wstring key_path) : key_(std::move(key_path)) {}

WinRunKeyStore WinRunKeyStore::production() { return WinRunKeyStore(kRunKey); }

contracts::Result<std::optional<std::wstring>> WinRunKeyStore::read(std::wstring_view name) {
    using R = contracts::Result<std::optional<std::wstring>>;
    const std::wstring n(name);
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    const LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, key_.c_str(), n.c_str(), RRF_RT_REG_SZ, nullptr, buf, &size);
    if (st == ERROR_FILE_NOT_FOUND) return std::optional<std::wstring>{};
    if (st == ERROR_MORE_DATA) return std::optional<std::wstring>{std::wstring(L"?")};  // bizim komutumuz degil
    if (st != ERROR_SUCCESS) return R::fail(ErrorCode::IoError, "Run value read");
    return std::optional<std::wstring>{std::wstring(buf)};
}

contracts::Status WinRunKeyStore::write(std::wstring_view name, std::wstring_view command) {
    HKEY k = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
    if (st != ERROR_SUCCESS) return contracts::Status::fail(ErrorCode::IoError, "open Run key");
    const std::wstring n(name), cmd(command);
    st = RegSetValueExW(k, n.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return st == ERROR_SUCCESS ? contracts::ok_status() : contracts::Status::fail(ErrorCode::IoError, "Run value write");
}

contracts::Status WinRunKeyStore::remove(std::wstring_view name) {
    HKEY k = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, KEY_SET_VALUE, &k);
    if (st == ERROR_FILE_NOT_FOUND) return contracts::ok_status();  // anahtar yok: deger de yok
    if (st != ERROR_SUCCESS) return contracts::Status::fail(ErrorCode::IoError, "open Run key");
    const std::wstring n(name);
    st = RegDeleteValueW(k, n.c_str());
    RegCloseKey(k);
    if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    return st == ERROR_SUCCESS ? contracts::ok_status() : contracts::Status::fail(ErrorCode::IoError, "Run value delete");
}

namespace {
void append_named_log(const char* name, std::string_view line, std::size_t max_bytes = 32 * 1024) {
    const auto dir = app_data_dir();
    if (!dir) return;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const auto ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;  // 100 ns, 1601
    const std::int64_t unix_ms = static_cast<std::int64_t>(ticks / 10'000) - 11'644'473'600'000LL;
    std::string out = core::logging::format_utc(core::UtcTime{unix_ms});
    out += " pid=" + std::to_string(GetCurrentProcessId()) + " ";
    out += line;
    core::logging::RotatingFileSink sink(*dir / L"logs", name, max_bytes, 2);
    sink.write_line(out);
}
}  // namespace

void append_exit_log(std::string_view line) { append_named_log("exit", line); }

void append_balance_log(std::string_view line) { append_named_log("balance", line, 64 * 1024); }

void append_restore_log(std::string_view line) { append_named_log("restore", line, 64 * 1024); }

void append_autostart_log(std::string_view line) {
    const auto dir = app_data_dir();
    if (!dir) return;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const auto ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;  // 100 ns, 1601
    const std::int64_t unix_ms = static_cast<std::int64_t>(ticks / 10'000) - 11'644'473'600'000LL;
    std::string out = core::logging::format_utc(core::UtcTime{unix_ms});
    out += " pid=" + std::to_string(GetCurrentProcessId()) + " ";
    out += line;
    core::logging::RotatingFileSink sink(*dir / L"logs", "autostart", 32 * 1024, 2);
    sink.write_line(out);
}

}  // namespace ef::platform
