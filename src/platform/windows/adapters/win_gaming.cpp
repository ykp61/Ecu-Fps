#include "platform/windows/adapters/win_gaming.h"

#include <windows.h>

#include <string>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using core::adapters::windows::RegRaw;

namespace {
constexpr std::size_t kMaxBytes = 4096;  // bu hedeflerin hepsi kucuk; sinir disi deger okunmaz/yazilmaz
}

contracts::Result<std::optional<RegRaw>> WinUserRegistryApi::read(std::wstring_view subkey, std::wstring_view name) {
    using R = contracts::Result<std::optional<RegRaw>>;
    const std::wstring sk(subkey), nm(name);
    DWORD type = 0, size = 0;
    LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, sk.c_str(), nm.c_str(), RRF_RT_ANY | RRF_NOEXPAND, &type, nullptr, &size);
    if (s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return std::optional<RegRaw>{};
    if (s != ERROR_SUCCESS) return R::fail(s == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "RegGetValue " + std::to_string(s));
    if (size > kMaxBytes) return R::fail(ErrorCode::ParseLimit, "value too large");
    RegRaw r;
    r.bytes.resize(size);
    s = RegGetValueW(HKEY_CURRENT_USER, sk.c_str(), nm.c_str(), RRF_RT_ANY | RRF_NOEXPAND, &type, size ? r.bytes.data() : nullptr, &size);
    if (s != ERROR_SUCCESS) return R::fail(ErrorCode::IoError, "RegGetValue " + std::to_string(s));
    r.bytes.resize(size);
    r.type = type;
    return std::optional<RegRaw>{std::move(r)};
}

contracts::Status WinUserRegistryApi::write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) {
    if (v.bytes.size() > kMaxBytes || (v.type != REG_DWORD && v.type != REG_SZ && v.type != REG_EXPAND_SZ))
        return contracts::Status::fail(ErrorCode::OutOfRange, "unsupported value");
    const std::wstring sk(subkey), nm(name);
    const LSTATUS s = RegSetKeyValueW(HKEY_CURRENT_USER, sk.c_str(), nm.c_str(), v.type, v.bytes.empty() ? nullptr : v.bytes.data(),
                                      static_cast<DWORD>(v.bytes.size()));
    if (s != ERROR_SUCCESS) return contracts::Status::fail(s == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "RegSetKeyValue " + std::to_string(s));
    return contracts::ok_status();
}

contracts::Status WinUserRegistryApi::remove(std::wstring_view subkey, std::wstring_view name) {
    const std::wstring sk(subkey), nm(name);
    const LSTATUS s = RegDeleteKeyValueW(HKEY_CURRENT_USER, sk.c_str(), nm.c_str());
    if (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND || s == ERROR_PATH_NOT_FOUND) return contracts::ok_status();
    return contracts::Status::fail(s == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "RegDeleteKeyValue " + std::to_string(s));
}

}  // namespace ef::platform::adapters
