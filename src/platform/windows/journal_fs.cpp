#include "platform/windows/journal_fs.h"

#include <windows.h>
#include <aclapi.h>
#include <cstdio>
#include <knownfolders.h>
#include <sddl.h>
#include <shlobj.h>

#include "core/recovery/sha256.h"
#include "platform/windows/elevation.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using contracts::ErrorCode;
using contracts::Status;

namespace {
std::string err(const char* w, DWORD e) { return std::string(w) + " (" + std::to_string(e) + ")"; }
bool safe_name(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    return n.find("..") == std::string::npos;
}
ErrorCode map(DWORD e) {
    if (e == ERROR_ACCESS_DENIED) return ErrorCode::Denied;
    if (e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL) return ErrorCode::ParseLimit;
    return ErrorCode::IoError;
}

// CORE-01: makine journal dizini icin korumali ACL. SYSTEM + Yoneticiler tam (alt nesnelere miras),
// Kullanicilar yalniz okuma.
constexpr wchar_t kMachineDirSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FR;;;BU)";

std::string sid_to_string(PSID sid) {
    if (!sid) return "<none>";
    wchar_t* s = nullptr;
    if (!ConvertSidToStringSidW(sid, &s)) return "<unreadable>";
    std::string out = to_utf8(s);
    LocalFree(s);
    return out;
}

bool trusted_writer(PSID sid) {
    return sid && (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
}

// Bir allow ACE'in yazma izni sayilan bitleri. Okuma izinleri (FILE_GENERIC_READ) kapsam disi.
constexpr ACCESS_MASK kWriteAccess = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                     FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;

struct AllowedAcePrefix {
    ACE_HEADER header;
    ACCESS_MASK mask;
};

// DACL'de SYSTEM/Administrators disinda yazma izni veren allow ACE varsa tanim (yoksa bos).
std::string untrusted_write_ace(PACL dacl) {
    if (!dacl) return "NULL DACL grants everyone full control";
    for (WORD i = 0; i < dacl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(dacl, i, &raw) || !raw) return "unreadable DACL";
        const auto* ace = static_cast<const AllowedAcePrefix*>(raw);
        if ((ace->header.AceFlags & INHERIT_ONLY_ACE) != 0) continue;  // bu nesneye izin vermez
        PSID sid = nullptr;
        switch (ace->header.AceType) {
            case ACCESS_ALLOWED_ACE_TYPE:
                sid = reinterpret_cast<PSID>(&static_cast<ACCESS_ALLOWED_ACE*>(raw)->SidStart);
                break;
            case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
                sid = reinterpret_cast<PSID>(&static_cast<ACCESS_ALLOWED_OBJECT_ACE*>(raw)->SidStart);
                break;
            case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
                sid = reinterpret_cast<PSID>(&static_cast<ACCESS_ALLOWED_CALLBACK_ACE*>(raw)->SidStart);
                break;
            case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
                sid = reinterpret_cast<PSID>(&static_cast<ACCESS_ALLOWED_CALLBACK_OBJECT_ACE*>(raw)->SidStart);
                break;
            default: continue;  // deny/audit/mandatory label izin vermez
        }
        if ((ace->mask & kWriteAccess) == 0 || trusted_writer(sid)) continue;
        return "write ACE for " + sid_to_string(sid) + " (mask 0x" + [&] {
            char b[16];
            std::snprintf(b, sizeof b, "%08lx", static_cast<unsigned long>(ace->mask & kWriteAccess));
            return std::string(b);
        }() + ")";
    }
    return {};
}
}  // namespace

namespace detail {
bool transient_fs_error(unsigned long win32) noexcept {
    return win32 == ERROR_SHARING_VIOLATION || win32 == ERROR_ACCESS_DENIED || win32 == ERROR_LOCK_VIOLATION ||
           win32 == ERROR_USER_MAPPED_FILE || win32 == ERROR_DELETE_PENDING;
}
bool transient_read_error(unsigned long win32) noexcept {
    return win32 == ERROR_SHARING_VIOLATION || win32 == ERROR_LOCK_VIOLATION;
}
unsigned long fs_retry_delay_ms(int attempt) noexcept { return 15ul << (attempt < 0 ? 0 : attempt > 3 ? 3 : attempt); }
}  // namespace detail

namespace {
// Hata metni Win32 kodunu ve deneme sayisini tasir (gunluk/tanilama: hangi ajan engelledi izlenebilsin).
std::string err_retry(const char* w, DWORD e, int attempts) {
    return err(w, e) + (attempts > 1 ? " after " + std::to_string(attempts) + " attempts" : std::string());
}
}  // namespace

Status WinJournalFs::write_atomic(const std::string& name, const std::string& bytes) {
    if (!safe_name(name)) return Status::fail(ErrorCode::ParseError, "journal name");
    const auto final_path = dir_ / to_wide(name);
    const auto tmp = dir_ / to_wide(name + ".tmp");
    const auto pause = [](unsigned long ms) { Sleep(static_cast<DWORD>(ms)); };
    HANDLE h = INVALID_HANDLE_VALUE;
    int attempts = 0;
    const DWORD ce = detail::retry_transient(
        [&]() -> unsigned long {
            h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            return h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        },
        pause, &attempts);
    if (ce != 0) return Status::fail(map(ce), err_retry("create journal temp", ce, attempts));
    DWORD written = 0;
    const BOOL ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() &&
                    FlushFileBuffers(h);
    const DWORD e = GetLastError();
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return Status::fail(map(e), err("write journal", e));
    }
    const DWORD me = detail::retry_transient(
        [&]() -> unsigned long {
            return MoveFileExW(tmp.c_str(), final_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : GetLastError();
        },
        pause, &attempts);
    if (me != 0) {
        DeleteFileW(tmp.c_str());
        return Status::fail(map(me), err_retry("commit journal", me, attempts));
    }
    return contracts::ok_status();
}

contracts::Result<std::string> WinJournalFs::read(const std::string& name) {
    using R = contracts::Result<std::string>;
    if (!safe_name(name)) return R::fail(ErrorCode::ParseError, "journal name");
    // Uyumluluk #21: geri okuma da ayni gecici paylasim/kilit hatasinda tekrar denenir (yok dosya hemen doner).
    HANDLE h = INVALID_HANDLE_VALUE;
    int attempts = 0;
    const DWORD oe = detail::retry_transient_if(
        [&]() -> unsigned long {
            h = CreateFileW((dir_ / to_wide(name)).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            return h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        },
        [](unsigned long e) { return detail::transient_read_error(e); }, [](unsigned long ms) { Sleep(static_cast<DWORD>(ms)); },
        &attempts);
    if (oe != 0) return R::fail(oe == ERROR_FILE_NOT_FOUND ? ErrorCode::MissingValue : map(oe), err_retry("open journal", oe, attempts));
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 1024 * 1024) {
        CloseHandle(h);
        return R::fail(ErrorCode::ParseLimit, "journal file too large");
    }
    std::string buf(static_cast<std::size_t>(sz.QuadPart), '\0');
    DWORD got = 0;
    const BOOL ok = buf.empty() || ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok) return R::fail(ErrorCode::IoError, "read journal");
    buf.resize(got);
    return buf;
}

contracts::Result<std::vector<std::string>> WinJournalFs::list() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const auto u = e.path().filename().u8string();
        std::string n(reinterpret_cast<const char*>(u.data()), u.size());
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) out.push_back(n);
    }
    if (ec) return contracts::Result<std::vector<std::string>>::fail(ErrorCode::IoError, "list journal");
    return out;
}

Status WinJournalFs::remove(const std::string& name) {
    if (!safe_name(name)) return Status::fail(ErrorCode::ParseError, "journal name");
    if (!DeleteFileW((dir_ / to_wide(name)).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
        return Status::fail(map(GetLastError()), "delete journal");
    return contracts::ok_status();
}

std::vector<std::string> WinJournalFs::temp_files() {
    // CORE-05: atomik yazimin yarim kalan *.json.tmp dosyalari (commit edilmemis gecici veri; journal kaydi degil).
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const auto u = e.path().filename().u8string();
        std::string n(reinterpret_cast<const char*>(u.data()), u.size());
        if (n.size() > 9 && n.compare(n.size() - 9, 9, ".json.tmp") == 0) out.push_back(std::move(n));
    }
    return out;
}

contracts::Result<std::filesystem::path> user_journal_dir() {
    using R = contracts::Result<std::filesystem::path>;
    auto base = app_data_dir();
    if (!base) return R::fail(ErrorCode::IoError, "LocalAppData unavailable");
    const auto d = *base / L"journal";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    if (ec) return R::fail(ErrorCode::Denied, "create user journal dir");
    return d;
}

namespace detail {
contracts::Status check_journal_dir_security(const std::filesystem::path& dir, const std::vector<std::string>& allowed_owner_sids) {
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return contracts::Status::fail(ErrorCode::MissingValue, "machine journal dir missing");
        return contracts::Status::fail(ErrorCode::Denied, err("machine journal dir attributes", e));
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return contracts::Status::fail(ErrorCode::Denied, "machine journal path is not a directory");
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return contracts::Status::fail(ErrorCode::Denied, "machine journal dir is a reparse point (junction/symlink)");
    HANDLE h = CreateFileW(dir.c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return contracts::Status::fail(ErrorCode::Denied, err("machine journal dir security open", e));
    }
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD got = GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                                      nullptr, &dacl, nullptr, &sd);
    CloseHandle(h);
    if (got != ERROR_SUCCESS) return contracts::Status::fail(ErrorCode::Denied, err("machine journal dir security info", got));
    const std::string owner_sid = sid_to_string(owner);
    std::string problem;
    bool owner_ok = false;
    for (const auto& allowed : allowed_owner_sids) owner_ok = owner_ok || owner_sid == allowed;
    if (!owner_ok)
        problem = "owner " + owner_sid + " is not SYSTEM/Administrators";
    else
        problem = untrusted_write_ace(dacl);
    if (sd) LocalFree(sd);
    if (!problem.empty()) return contracts::Status::fail(ErrorCode::Denied, "machine journal dir insecure: " + problem);
    return contracts::ok_status();
}
}  // namespace detail

contracts::Status verify_machine_journal_dir(const std::filesystem::path& dir) {
    return detail::check_journal_dir_security(dir, {"S-1-5-18", "S-1-5-32-544"});  // SYSTEM, Administrators
}

namespace {
contracts::Status create_protected_dir(const std::filesystem::path& dir, const char* what) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kMachineDirSddl, SDDL_REVISION_1, &sd, nullptr))
        return contracts::Status::fail(ErrorCode::IoError, "SDDL");
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    const BOOL ok = CreateDirectoryW(dir.c_str(), &sa) || GetLastError() == ERROR_ALREADY_EXISTS;
    const DWORD e = GetLastError();
    LocalFree(sd);
    if (!ok) return contracts::Status::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, err(what, e));
    return contracts::ok_status();
}
}  // namespace

contracts::Result<std::filesystem::path> machine_journal_dir(bool create) {
    using R = contracts::Result<std::filesystem::path>;
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &p))) return R::fail(ErrorCode::IoError, "ProgramData");
    const std::filesystem::path root = std::filesystem::path(p) / L"EcuFps";
    CoTaskMemFree(p);
    const auto dir = root / L"journal";
    std::error_code ec;
    const bool root_exists = std::filesystem::exists(root, ec);
    if (!root_exists) {
        if (!create) return R::fail(ErrorCode::MissingValue, "machine journal not created");
        // Makine journal'ini yalniz yukseltilmis broker olusturur (normal kullanici ProgramData'da klasor
        // acabilir; bu yan etkiyi ve yanlis sahipligi onlemek icin acik engel).
        if (!is_process_elevated()) return R::fail(ErrorCode::Denied, "machine journal requires the elevated broker");
        if (auto s = create_protected_dir(root, "create machine journal root"); !s.ok()) return R(s.error());
    } else {
        // CORE-01: onceden var olan kok dizin de dogrulanir. Kullanici kok dizini ele gecirirse journal
        // dizinini degistirebilir/silebilir; SDDL'i yalniz yeni olusturmada uygulamak yeterli degildir.
        if (auto s = verify_machine_journal_dir(root); !s.ok()) {
            OutputDebugStringA(("EcuFps journal security: " + s.error().detail).c_str());
            return R(s.error());
        }
    }
    if (std::filesystem::exists(dir, ec)) {
        if (auto s = verify_machine_journal_dir(dir); !s.ok()) {
            OutputDebugStringA(("EcuFps journal security: " + s.error().detail).c_str());
            return R(s.error());
        }
        return dir;
    }
    if (!create) return R::fail(ErrorCode::MissingValue, "machine journal not created");
    if (!is_process_elevated()) return R::fail(ErrorCode::Denied, "machine journal requires the elevated broker");
    if (auto s = create_protected_dir(dir, "create machine journal"); !s.ok()) return R(s.error());
    // Yeni olusturulan dizin de dogrulanir (ACL gercekten uygulandi mi; tahmin edilmez).
    if (auto s = verify_machine_journal_dir(dir); !s.ok()) {
        OutputDebugStringA(("EcuFps journal security: " + s.error().detail).c_str());
        return R(s.error());
    }
    return dir;
}

std::string machine_id() {
    wchar_t guid[64] = {};
    DWORD sz = sizeof(guid);
    // 64 bit gorunum: 32 bit surec olsa bile ayni kimlik.
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, guid, &sz) != ERROR_SUCCESS)
        return "unknown-machine";
    return core::recovery::sha256_hex("itx-machine:" + to_utf8(guid)).substr(0, 32);
}

}  // namespace ef::platform
