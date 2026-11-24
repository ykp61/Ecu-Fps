#include "platform/windows/secure_targets.h"

#include <windows.h>

#include "core/security/dispatch.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using contracts::ErrorCode;

namespace {
std::string err(const char* what, DWORD e) { return std::string(what) + " (" + std::to_string(e) + ")"; }

std::wstring final_path_of(HANDLE h) {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return buf;
}

bool starts_with_ci(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()),
                                TRUE) == CSTR_EQUAL;
}
}  // namespace

contracts::Result<std::unique_ptr<OperationLock>> OperationLock::acquire(const std::wstring& name, std::uint32_t timeout_ms) {
    using R = contracts::Result<std::unique_ptr<OperationLock>>;
    HANDLE h = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!h) {
        const DWORD e = GetLastError();
        return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, err("CreateMutex", e));
    }
    const DWORD w = WaitForSingleObject(h, timeout_ms);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) return std::unique_ptr<OperationLock>(new OperationLock(h, w == WAIT_ABANDONED));
    CloseHandle(h);
    return R::fail(ErrorCode::Denied, w == WAIT_TIMEOUT ? "operation lock busy" : err("WaitForSingleObject", GetLastError()));
}

OperationLock::~OperationLock() {
    if (h_) {
        ReleaseMutex(h_);
        CloseHandle(h_);
    }
}

contracts::Result<void*> open_user_key(const std::string& user_sid, const std::wstring& subkey, bool write) {
    using R = contracts::Result<void*>;
    if (!core::security::looks_like_user_sid(user_sid)) return R::fail(ErrorCode::Denied, "not a user SID");
    if (subkey.find(L"..") != std::wstring::npos || (!subkey.empty() && subkey.front() == L'\\'))
        return R::fail(ErrorCode::ParseError, "bad subkey");
    const std::wstring path = to_wide(user_sid) + (subkey.empty() ? L"" : L"\\" + subkey);
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(HKEY_USERS, path.c_str(), 0, write ? (KEY_READ | KEY_WRITE) : KEY_READ, &k);
    if (st == ERROR_FILE_NOT_FOUND) return R::fail(ErrorCode::MissingValue, "user hive or key not loaded");
    if (st == ERROR_ACCESS_DENIED) return R::fail(ErrorCode::Denied, "access denied");
    if (st != ERROR_SUCCESS) return R::fail(ErrorCode::IoError, err("RegOpenKeyEx", static_cast<DWORD>(st)));
    return static_cast<void*>(k);
}

contracts::Result<VerifiedFile> open_verified_file(const std::filesystem::path& root, const std::wstring& rel, bool write) {
    using R = contracts::Result<VerifiedFile>;
    if (rel.empty() || rel.find(L"..") != std::wstring::npos || rel.find(L':') != std::wstring::npos || rel.front() == L'\\' ||
        rel.front() == L'/')
        return R::fail(ErrorCode::ParseError, "relative path rejected");
    // Kokun kendisinin gercek yolu (kok de bir junction olabilir; gercek konum referans alinir).
    HANDLE rh = CreateFileW(root.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (rh == INVALID_HANDLE_VALUE) return R::fail(ErrorCode::MissingValue, err("open root", GetLastError()));
    std::wstring root_final = final_path_of(rh);
    CloseHandle(rh);
    if (root_final.empty()) return R::fail(ErrorCode::IoError, "root final path");
    if (root_final.back() != L'\\') root_final += L'\\';

    const std::filesystem::path full = root / rel;
    HANDLE h = CreateFileW(full.c_str(), write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::MissingValue, err("open file", e));
    }
    BY_HANDLE_FILE_INFORMATION info{};
    VerifiedFile vf{h, final_path_of(h)};
    auto reject = [&](const char* why) {
        CloseHandle(h);
        return R::fail(ErrorCode::Denied, why);
    };
    if (!GetFileInformationByHandle(h, &info)) return reject("file info");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return reject("reparse point");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return reject("is a directory");
    if (info.nNumberOfLinks != 1) return reject("hard link");
    if (vf.final_path.empty() || !starts_with_ci(vf.final_path, root_final)) return reject("path escapes root");
    return vf;
}

void close_verified(VerifiedFile& f) {
    if (f.handle) CloseHandle(f.handle);
    f.handle = nullptr;
}

}  // namespace ef::platform
