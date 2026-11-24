#include "platform/windows/legacy_migration.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <atomic>
#include <system_error>

#include "core/legacy/legacy_names.h"
#include "core/legacy/machine_journal_migration.h"
#include "core/logging/log.h"
#include "core/product_info.h"
#include "core/recovery/sha256.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using core::legacy::FileDigest;
using core::legacy::FileInfo;
using core::legacy::FsCode;
using core::legacy::FsStatus;

namespace {
// Mutlak surucu yolu icin uzun yol oneki (\\?\): uzun / tuhaf dosya adlari MAX_PATH'e takilmaz.
std::wstring wpath(const std::string& p) {
    std::wstring w = to_wide(p);
    if (w.size() >= 3 && w[1] == L':' && w[2] == L'\\') return L"\\\\?\\" + w;
    return w;
}

FsStatus from_win32(DWORD e, const char* what) {
    FsCode c = FsCode::Io;
    switch (e) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: c = FsCode::NotFound; break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS: c = FsCode::Exists; break;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_USER_MAPPED_FILE:
        case ERROR_DELETE_PENDING: c = FsCode::Busy; break;
        case ERROR_ACCESS_DENIED: c = FsCode::Denied; break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: c = FsCode::DiskFull; break;
        default: break;
    }
    return FsStatus::fail(c, std::string(what) + " (" + std::to_string(e) + ")");
}

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
};

FsStatus open_read(const std::string& path, Handle& out) {
    // Yazma paylasimi verilmez: baska surec yaziyorsa Busy (yeniden denenir); yarim dosya ozetlenmez.
    out.h = CreateFileW(wpath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    return out.ok() ? FsStatus{} : from_win32(GetLastError(), "open");
}

FsStatus ensure_parent(const std::string& path) {
    const auto i = path.find_last_of('\\');
    if (i == std::string::npos) return {};
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(wpath(path.substr(0, i))), ec);
    if (ec) return FsStatus::fail(ec.value() == ERROR_DISK_FULL ? FsCode::DiskFull : FsCode::Io, "create parent");
    return {};
}

FsStatus list_rec(const std::wstring& dir, const std::string& rel, std::vector<FileInfo>& out) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND ? FsStatus{} : from_win32(e, "list");
    }
    FsStatus st;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::string child = rel.empty() ? to_utf8(name) : rel + "/" + to_utf8(name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            st = FsStatus::fail(FsCode::Denied, "reparse point");  // junction/symlink izlenmez
            break;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            st = list_rec(dir + L"\\" + name, child, out);
            if (!st.ok()) break;
            continue;
        }
        FileInfo fi;
        fi.rel = child;
        fi.size = (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        fi.read_only = (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        out.push_back(std::move(fi));
    } while (FindNextFileW(h, &fd));
    if (st.ok() && GetLastError() != ERROR_NO_MORE_FILES) st = from_win32(GetLastError(), "list next");
    FindClose(h);
    return st;
}
}  // namespace

core::legacy::MigrationFs::Kind WinMigrationFs::kind(const std::string& path) {
    const DWORD a = GetFileAttributesW(wpath(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return Kind::Missing;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return Kind::Other;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? Kind::Dir : Kind::File;
}

FsStatus WinMigrationFs::list_files(const std::string& dir, std::vector<FileInfo>& out) {
    out.clear();
    return list_rec(wpath(dir), {}, out);
}

FsStatus WinMigrationFs::hash_file(const std::string& path, FileDigest& out) {
    Handle h;
    if (auto s = open_read(path, h); !s.ok()) return s;
    core::recovery::Sha256Stream sh;
    std::string buf(1 << 20, '\0');
    std::uint64_t total = 0;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h.h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) return from_win32(GetLastError(), "read");
        if (got == 0) break;
        sh.update(std::string_view(buf.data(), got));
        total += got;
    }
    out.size = total;
    out.sha256 = sh.hex_final();
    return {};
}

FsStatus WinMigrationFs::copy_file(const std::string& from, const std::string& to) {
    Handle src;
    if (auto s = open_read(from, src); !s.ok()) return s;
    if (auto s = ensure_parent(to); !s.ok()) return s;
    const std::wstring wto = wpath(to);
    Handle dst;
    dst.h = CreateFileW(wto.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (!dst.ok()) return from_win32(GetLastError(), "create copy");
    std::string buf(1 << 20, '\0');
    FsStatus st;
    for (;;) {
        DWORD got = 0, put = 0;
        if (!ReadFile(src.h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            st = from_win32(GetLastError(), "read");
            break;
        }
        if (got == 0) break;
        if (!WriteFile(dst.h, buf.data(), got, &put, nullptr) || put != got) {
            st = from_win32(GetLastError(), "write copy");
            break;
        }
    }
    if (st.ok() && !FlushFileBuffers(dst.h)) st = from_win32(GetLastError(), "flush copy");
    if (st.ok()) {  // degistirilme zamani korunur (destek paketinde kayit sirasi anlamli kalir)
        FILETIME c{}, a{}, w{};
        if (GetFileTime(src.h, &c, &a, &w)) (void)SetFileTime(dst.h, &c, nullptr, &w);
    }
    CloseHandle(dst.h);
    dst.h = INVALID_HANDLE_VALUE;
    if (!st.ok()) DeleteFileW(wto.c_str());
    return st;
}

FsStatus WinMigrationFs::read_file(const std::string& path, std::size_t max_bytes, std::string& out) {
    Handle h;
    if (auto s = open_read(path, h); !s.ok()) return s;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h.h, &sz)) return from_win32(GetLastError(), "size");
    if (static_cast<std::uint64_t>(sz.QuadPart) > max_bytes) return FsStatus::fail(FsCode::Io, "too large");
    out.assign(static_cast<std::size_t>(sz.QuadPart), '\0');
    DWORD got = 0;
    if (!out.empty() && !ReadFile(h.h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr)) return from_win32(GetLastError(), "read");
    out.resize(got);
    return {};
}

FsStatus WinMigrationFs::write_file(const std::string& path, const std::string& bytes) {
    if (auto s = ensure_parent(path); !s.ok()) return s;
    const std::wstring fin = wpath(path), tmp = wpath(path + ".tmp");
    {
        Handle h;
        h.h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (!h.ok()) return from_win32(GetLastError(), "create");
        DWORD put = 0;
        if (!WriteFile(h.h, bytes.data(), static_cast<DWORD>(bytes.size()), &put, nullptr) || put != bytes.size() || !FlushFileBuffers(h.h)) {
            const DWORD e = GetLastError();
            CloseHandle(h.h);
            h.h = INVALID_HANDLE_VALUE;
            DeleteFileW(tmp.c_str());
            return from_win32(e, "write");
        }
    }
    if (!MoveFileExW(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        return from_win32(e, "commit");
    }
    return {};
}

FsStatus WinMigrationFs::make_dirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(wpath(path)), ec);
    if (ec) return FsStatus::fail(FsCode::Io, "create dirs");
    return {};
}

FsStatus WinMigrationFs::rename_dir(const std::string& from, const std::string& to) {
    // Ayni birimde tek adim (atomik). Hedef varsa basarisiz (uzerine yazma yok). Tarayici tanitici tutuyorsa erisim
    // reddi gecicidir: Busy -> sinirli yeniden deneme.
    if (MoveFileExW(wpath(from).c_str(), wpath(to).c_str(), MOVEFILE_WRITE_THROUGH)) return {};
    const DWORD e = GetLastError();
    if (e == ERROR_ACCESS_DENIED) return FsStatus::fail(FsCode::Busy, "rename denied (handle open)");
    return from_win32(e, "rename");
}

FsStatus WinMigrationFs::move_file(const std::string& from, const std::string& to) {
    if (auto s = ensure_parent(to); !s.ok()) return s;
    if (MoveFileExW(wpath(from).c_str(), wpath(to).c_str(), MOVEFILE_WRITE_THROUGH)) return {};
    return from_win32(GetLastError(), "move");
}

FsStatus WinMigrationFs::remove_file(const std::string& path) {
    const std::wstring w = wpath(path);
    const DWORD a = GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return {};
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(w.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (DeleteFileW(w.c_str())) return {};
    const DWORD e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND ? FsStatus{} : from_win32(e, "delete");
}

FsStatus WinMigrationFs::remove_tree(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(wpath(path)), ec);  // baglantilari izlemez (bagi siler)
    if (ec) return FsStatus::fail(FsCode::Io, "remove tree");
    return {};
}

std::optional<std::uint64_t> WinMigrationFs::free_bytes(const std::string& path) {
    std::filesystem::path p(to_wide(path));
    std::error_code ec;
    while (!p.empty() && !std::filesystem::exists(p, ec)) {
        const auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    ULARGE_INTEGER avail{};
    if (p.empty() || !GetDiskFreeSpaceExW(p.c_str(), &avail, nullptr, nullptr)) return std::nullopt;
    return avail.QuadPart;
}

namespace {
std::wstring env_value(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    if (got == 0 || got >= n) return {};
    v.resize(got);
    return v;
}

bool mutex_exists(const std::wstring& name) {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (h) {
        CloseHandle(h);
        return true;
    }
    return GetLastError() != ERROR_FILE_NOT_FOUND;  // erisim reddi = var (yukseltilmis eski ornek)
}

bool old_app_running() {
    const auto u = inventory::read_user();
    const std::wstring sid = to_wide(u.process_user_sid.value.value_or("unknown"));
    return mutex_exists(std::wstring(core::legacy::kOldSingleInstanceMutexPrefix) + sid) ||
           mutex_exists(std::wstring(core::legacy::kOldSetupMutexName));
}

std::int64_t now_unix_ms() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const auto ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<std::int64_t>(ticks / 10'000) - 11'644'473'600'000LL;
}

void write_log(const std::filesystem::path& data_dir, const std::string& line) {
    std::string out = core::logging::format_utc(core::UtcTime{now_unix_ms()});
    out += " pid=" + std::to_string(GetCurrentProcessId()) + " " + line;
    core::logging::RotatingFileSink sink(data_dir / L"logs", "migration", 32 * 1024, 2);
    sink.write_line(out);
}

core::legacy::MigrationReport run_once() {
    core::legacy::MigrationReport skipped;
    skipped.outcome = core::legacy::MigrationOutcome::Skipped;
    const auto data = app_data_dir();
    if (!data) {
        skipped.error = "no data dir";
        return skipped;
    }
    std::filesystem::path old_dir;
    const bool test_bg = env_value(L"EF_TEST_BACKGROUND") == L"1";
    if (app_data_dir_overridden()) {
        // Test kipi: eski klasor yalniz acikca verilirse ve %TEMP% altindaysa.
        const std::wstring v = env_value(L"EF_LEGACY_DATA_DIR");
        wchar_t tmp[MAX_PATH + 2]{};
        const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
        std::filesystem::path root;
        if (n != 0 && n <= MAX_PATH + 1) root = std::filesystem::path(std::wstring(tmp, n));
        auto p = v.empty() ? std::nullopt : data_dir_override(v, true, root);
        if (!p) {
            skipped.error = "test mode without legacy dir";
            return skipped;
        }
        old_dir = *p;
    } else if (test_bg) {
        skipped.error = "test mode: real profile not migrated";  // ctest asla gercek profili gocurmez
        return skipped;
    } else {
        old_dir = data->parent_path() / std::wstring(core::legacy::kOldDataDirName);
    }
    // Ayni kullanicinin iki yeni sureci (kurulum penceresi + Ui) ayni anda gocurmesin.
    HANDLE lock = CreateMutexW(nullptr, FALSE, L"Local\\EcuFps.Migration");
    if (lock && WaitForSingleObject(lock, 60'000) == WAIT_TIMEOUT) {
        CloseHandle(lock);
        skipped.error = "lock timeout";
        return skipped;
    }
    WinMigrationFs fs;
    core::legacy::MigrationEnv env;
    env.now_unix_ms = now_unix_ms();
    {
        const auto v = core::product_version();
        env.app_version = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    }
    env.to_name = to_utf8(data->filename().wstring());
    env.from_name = to_utf8(core::legacy::kOldDataDirName);
    env.machine_id = machine_id();
    env.old_app_running = old_app_running();
    env.sleep = [](int attempt) { Sleep(static_cast<DWORD>(15u << (attempt > 4 ? 4 : attempt))); };
    auto r = core::legacy::migrate_user_data(fs, {to_utf8(old_dir.wstring()), to_utf8(data->wstring())}, env);
    // Eski makine journal'i (yalniz gercek kipte; salt okuma, Kullanicilar okuma iznine sahip). Yazma yok.
    if (!app_data_dir_overridden()) {
        PWSTR pd = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) {
            const auto old_machine = std::filesystem::path(pd) / std::wstring(core::legacy::kOldDataDirName);
            const auto new_machine = std::filesystem::path(pd) / L"EcuFps";
            // R-F2: yukseltilmis yukseltme makine journal'ini aktardiysa (isaret) eski kayitlar yeni journal'da gorunur.
            r.machine_legacy = core::legacy::machine_legacy_after_upgrade(fs, {to_utf8(old_machine.wstring()), to_utf8(new_machine.wstring())},
                                                                          env.machine_id);
        }
        if (pd) CoTaskMemFree(pd);
    }
    if (lock) {
        ReleaseMutex(lock);
        CloseHandle(lock);
    }
    using O = core::legacy::MigrationOutcome;
    if (r.outcome == O::Migrated || r.outcome == O::Failed || r.outcome == O::NoLegacyData || r.legacy_pending())
        write_log(*data, core::legacy::migration_log_line(r));
    OutputDebugStringA(("EcuFps " + core::legacy::migration_log_line(r) + "\n").c_str());
    return r;
}
}  // namespace

namespace {
std::atomic<const core::legacy::MigrationReport*> g_report{nullptr};
}

const core::legacy::MigrationReport& run_legacy_user_migration() {
    static const core::legacy::MigrationReport r = run_once();
    g_report.store(&r);
    return r;
}

// Gocu TETIKLEMEZ: yalniz bu surecte calismis gocun sonucu (journal acildiktan sonra gec goc yapilmaz).
bool legacy_user_data_pending() {
    const auto* r = g_report.load();
    return r && r->legacy_pending();
}

}  // namespace ef::platform
