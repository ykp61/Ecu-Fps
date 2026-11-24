#include "platform/windows/install/secure_dir.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include "platform/windows/journal_fs.h"

namespace ef::platform::install {

using contracts::ErrorCode;
using contracts::Status;

namespace {
std::string win_err(const char* what, DWORD e) { return std::string(what) + " (" + std::to_string(e) + ")"; }

std::string sid_text(PSID sid) {
    if (!sid || !IsValidSid(sid)) return {};
    LPSTR s = nullptr;
    if (!ConvertSidToStringSidA(sid, &s)) return {};
    std::string out(s);
    LocalFree(s);
    return out;
}
}  // namespace

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool is_reparse_or_missing_dir(const std::filesystem::path& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

contracts::Result<core::setup::AclFacts> read_acl_facts(const std::filesystem::path& p) {
    using R = contracts::Result<core::setup::AclFacts>;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD e = GetNamedSecurityInfoW(p.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                          &owner, nullptr, &dacl, nullptr, &sd);
    if (e != ERROR_SUCCESS) return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, win_err("GetNamedSecurityInfoW", e));
    core::setup::AclFacts f;
    SECURITY_DESCRIPTOR_CONTROL ctl = 0;
    DWORD rev = 0;
    if (GetSecurityDescriptorControl(sd, &ctl, &rev)) f.dacl_protected = (ctl & SE_DACL_PROTECTED) != 0;
    f.dacl_present = dacl != nullptr;
    f.owner_sid = sid_text(owner);
    if (dacl) {
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* raw = nullptr;
            if (!GetAce(dacl, i, &raw)) {
                LocalFree(sd);
                return R::fail(ErrorCode::IoError, "GetAce");
            }
            const auto* h = static_cast<const ACE_HEADER*>(raw);
            core::setup::AceFact a;
            switch (h->AceType) {
                case ACCESS_ALLOWED_ACE_TYPE:
                case ACCESS_ALLOWED_CALLBACK_ACE_TYPE: {
                    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
                    a.allow = true;
                    a.mask = ace->Mask;
                    a.sid = sid_text(const_cast<DWORD*>(&ace->SidStart));
                    break;
                }
                case ACCESS_DENIED_ACE_TYPE:
                case ACCESS_DENIED_CALLBACK_ACE_TYPE:
                case ACCESS_DENIED_OBJECT_ACE_TYPE:
                case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
                    a.allow = false;
                    break;
                default:  // bilinmeyen/nesne izin ACE'si: SID cozulmeden en genis izin sayilir (guvenli ret)
                    a.allow = true;
                    a.mask = 0xFFFFFFFFu;
                    break;
            }
            f.aces.push_back(std::move(a));
        }
    }
    LocalFree(sd);
    return f;
}

Status ensure_protected_dir(const std::filesystem::path& dir, const wchar_t* sddl) {
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)))
        return Status::fail(ErrorCode::Denied, "not a plain directory");
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr))
        return Status::fail(ErrorCode::IoError, win_err("SDDL", GetLastError()));
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        if (!CreateDirectoryW(dir.c_str(), &sa) && GetLastError() != ERROR_ALREADY_EXISTS) {
            const DWORD e = GetLastError();
            LocalFree(sd);
            return Status::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, win_err("CreateDirectoryW", e));
        }
        if (is_reparse_or_missing_dir(dir)) {
            LocalFree(sd);
            return Status::fail(ErrorCode::Denied, "directory replaced during creation");
        }
    }
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) || !present || !dacl) {
        LocalFree(sd);
        return Status::fail(ErrorCode::IoError, "SDDL DACL");
    }
    BYTE admins[SECURITY_MAX_SID_SIZE];
    DWORD n = sizeof(admins);
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &n)) {
        LocalFree(sd);
        return Status::fail(ErrorCode::IoError, "admin SID");
    }
    std::wstring name = dir.native();
    const DWORD e = SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT,
                                          OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                          admins, nullptr, dacl, nullptr);
    LocalFree(sd);
    if (e != ERROR_SUCCESS) return Status::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, win_err("SetNamedSecurityInfoW", e));
    // Geri okuma: tahmin yok.
    auto facts = read_acl_facts(dir);
    if (!facts) return Status(facts.error());
    if (!core::setup::install_dir_acl_ok(facts.value())) return Status::fail(ErrorCode::Denied, "directory ACL read-back not admin-only");
    return contracts::ok_status();
}

Status ensure_machine_root(const std::filesystem::path& machine_dir) {
    if (machine_dir.empty()) return Status::fail(ErrorCode::MissingValue, "ProgramData root");
    const DWORD attrs = GetFileAttributesW(machine_dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kMachineRootSddl, SDDL_REVISION_1, &sd, nullptr))
            return Status::fail(ErrorCode::IoError, "SDDL");
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        const BOOL ok = CreateDirectoryW(machine_dir.c_str(), &sa) || GetLastError() == ERROR_ALREADY_EXISTS;
        const DWORD e = GetLastError();
        LocalFree(sd);
        if (!ok) return Status::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, win_err("create machine root", e));
    }
    return verify_machine_journal_dir(machine_dir);  // sahip SY/BA, kullaniciya yazma yok, reparse yok
}

}  // namespace ef::platform::install
