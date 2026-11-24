#include "platform/windows/cleanup/win_cleanup.h"

#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include "platform/windows/elevation.h"
#include "platform/windows/maintenance_win.h"

namespace ef::platform::cleanup {

using contracts::ErrorCode;
using core::cleanup::DeleteOutcome;
using core::cleanup::DeleteResult;
using core::cleanup::DirState;
using core::cleanup::FsEntry;

namespace {

// Uzun yol: normalize "X:\..." yollarina "\\?\" eklenir (MAX_PATH siniri yok).
std::wstring ext(const std::wstring& p) {
    if (p.size() < 240 || p.rfind(L"\\\\?\\", 0) == 0) return p;
    return L"\\\\?\\" + p;
}

std::wstring strip_prefix(std::wstring p) {
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) return {};  // ag yolu: kok olamaz
    if (p.rfind(L"\\\\?\\", 0) == 0) p.erase(0, 4);
    return p;
}

std::wstring final_path_of(HANDLE h) {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return strip_prefix(std::move(buf));
}

bool starts_with_ci(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()),
                                TRUE) == CSTR_EQUAL;
}

bool under_root(const std::wstring& final_path, const std::wstring& root_final) {
    if (final_path.empty() || root_final.empty()) return false;
    std::wstring prefix = root_final;
    if (prefix.back() != L'\\') prefix += L'\\';
    return final_path.size() > prefix.size() && starts_with_ci(final_path, prefix);
}

// Tanitici uzerinden silme isaretle: once POSIX anlami (ad hemen kalkar), desteklenmezse klasik.
DWORD mark_delete(HANDLE h) {
    FILE_DISPOSITION_INFO_EX ex{};
    ex.Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS;
    // FileDispositionInfoEx (=21) NTDDI_WIN10_RS1 ile bildirilir; proje NTDDI_WIN10 hedefler. Eski surumde
    // cagri ERROR_INVALID_PARAMETER doner ve klasik isarete dusulur.
    constexpr auto kDispositionInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(21);
    if (SetFileInformationByHandle(h, kDispositionInfoEx, &ex, sizeof ex)) return ERROR_SUCCESS;
    DWORD e = GetLastError();
    if (e != ERROR_INVALID_PARAMETER && e != ERROR_NOT_SUPPORTED && e != ERROR_INVALID_FUNCTION) return e;
    FILE_DISPOSITION_INFO di{};
    di.DeleteFile = TRUE;
    if (SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof di)) return ERROR_SUCCESS;
    return GetLastError();
}

DeleteOutcome outcome_of(DWORD e) {
    switch (e) {
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_USER_MAPPED_FILE: return DeleteOutcome::Locked;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: return DeleteOutcome::Missing;
        case ERROR_ACCESS_DENIED: return DeleteOutcome::Denied;
        default: return DeleteOutcome::Error;
    }
}

std::wstring known_folder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p))) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

}  // namespace

const std::wstring& WinCleanupFs::root_final(const std::wstring& root) {
    if (const auto it = root_final_.find(root); it != root_final_.end()) return it->second;
    std::wstring fp;
    HANDLE h = CreateFileW(ext(root).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION bi{};
        if (GetFileInformationByHandle(h, &bi) && (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fp = final_path_of(h);
        CloseHandle(h);
    }
    return root_final_.emplace(root, std::move(fp)).first->second;
}

DirState WinCleanupFs::probe_dir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(ext(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? DirState::Missing : DirState::Denied;
    }
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return DirState::Reparse;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DirState::NotDirectory;
    return DirState::Directory;
}

contracts::Result<std::vector<FsEntry>> WinCleanupFs::list(const std::wstring& dir) {
    using R = contracts::Result<std::vector<FsEntry>>;
    // Dizin tanitici ile acilir; baglanti kontrolu ayni tanitici uzerinden (ad -> hedef yarisi yok).
    HANDLE d = CreateFileW(ext(dir).c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (d == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return R::fail(ErrorCode::MissingValue, "dir missing");
        return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "open dir " + std::to_string(e));
    }
    struct Closer {
        HANDLE h;
        ~Closer() { CloseHandle(h); }
    } closer{d};
    BY_HANDLE_FILE_INFORMATION di{};
    if (!GetFileInformationByHandle(d, &di)) return R::fail(ErrorCode::IoError, "dir info");
    if (di.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return R::fail(ErrorCode::Denied, "reparse point");
    if (!(di.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return R::fail(ErrorCode::InvalidTransition, "not a directory");
    // Handoff v1 EXT-CLN-02: girdi basina dosya kimligi + son yazma + boyut (silme oncesi karsilastirma icin).
    std::vector<FsEntry> out;
    alignas(8) static thread_local unsigned char buf[64 * 1024];
    FILE_INFO_BY_HANDLE_CLASS cls = FileIdBothDirectoryRestartInfo;
    for (;;) {
        if (!GetFileInformationByHandleEx(d, cls, buf, sizeof buf)) {
            const DWORD e = GetLastError();
            if (e == ERROR_NO_MORE_FILES || e == ERROR_FILE_NOT_FOUND) break;
            return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "dir enum " + std::to_string(e));
        }
        cls = FileIdBothDirectoryInfo;
        std::size_t off = 0;
        for (;;) {
            const auto* fi = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buf + off);
            const std::wstring name(fi->FileName, fi->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                FsEntry en;
                en.name = name;
                en.is_dir = (fi->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                en.is_reparse = (fi->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                en.size = static_cast<std::uint64_t>(fi->EndOfFile.QuadPart);
                en.last_write = static_cast<std::uint64_t>(fi->LastWriteTime.QuadPart);
                en.file_id = static_cast<std::uint64_t>(fi->FileId.QuadPart);
                en.volume_serial = di.dwVolumeSerialNumber;
                en.has_id = true;
                en.allocation = static_cast<std::uint64_t>(fi->AllocationSize.QuadPart);
                out.push_back(std::move(en));
            }
            if (fi->NextEntryOffset == 0) break;
            off += fi->NextEntryOffset;
            if (off >= sizeof buf) break;
        }
    }
    return out;
}

DeleteResult WinCleanupFs::delete_file(const std::wstring& root, const std::wstring& path, const core::cleanup::FileExpect& expect) {
    const std::wstring& rf = root_final(root);
    if (rf.empty()) return {DeleteOutcome::OutsideRoot, 0};
    HANDLE h = CreateFileW(ext(path).c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {outcome_of(GetLastError()), 0};
    auto done = [&](DeleteOutcome o, std::uint64_t b = 0) {
        CloseHandle(h);
        return DeleteResult{o, b};
    };
    BY_HANDLE_FILE_INFORMATION bi{};
    if (!GetFileInformationByHandle(h, &bi)) return done(DeleteOutcome::Error);
    if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return done(DeleteOutcome::Reparse);
    if (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return done(DeleteOutcome::Error);
    if (bi.nNumberOfLinks != 1) return done(DeleteOutcome::HardLink);
    if (!under_root(final_path_of(h), rf)) return done(DeleteOutcome::OutsideRoot);
    const std::uint64_t size = (static_cast<std::uint64_t>(bi.nFileSizeHigh) << 32) | bi.nFileSizeLow;
    // Taramadaki kimlikle ayni dosya mi? (ayni adla yeniden olusturulmus/yazilmis dosya silinmez)
    const std::uint64_t mtime = (static_cast<std::uint64_t>(bi.ftLastWriteTime.dwHighDateTime) << 32) | bi.ftLastWriteTime.dwLowDateTime;
    const std::uint64_t id = (static_cast<std::uint64_t>(bi.nFileIndexHigh) << 32) | bi.nFileIndexLow;
    if (size != expect.bytes || (expect.last_write != 0 && mtime != expect.last_write) ||
        (expect.has_id && (id != expect.file_id || bi.dwVolumeSerialNumber != expect.volume_serial)))
        return done(DeleteOutcome::Changed);
    const DWORD e = mark_delete(h);
    if (e != ERROR_SUCCESS) return done(outcome_of(e) == DeleteOutcome::Missing ? DeleteOutcome::Error : outcome_of(e));
    return done(DeleteOutcome::Deleted, size);
}

bool WinCleanupFs::remove_empty_dir(const std::wstring& root, const std::wstring& path) {
    const std::wstring& rf = root_final(root);
    if (rf.empty()) return false;
    HANDLE h = CreateFileW(ext(path).c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION bi{};
    bool ok = GetFileInformationByHandle(h, &bi) && (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
              !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && under_root(final_path_of(h), rf);
    // Dolu dizinde isaret ERROR_DIR_NOT_EMPTY ile reddedilir: icerik asla toplu silinmez.
    if (ok) ok = mark_delete(h) == ERROR_SUCCESS;
    CloseHandle(h);
    return ok;
}

bool WinCleanupFs::remove_owned_empty_dir(const std::wstring& path) {
    // Yalniz cagiranin sahipligini dogruladigi klasorun kendisi icin; yol kok kontrolu gerekmez.
    // Dizin/baglanti kontrolu tanitici uzerinden; dolu dizin mark_delete tarafindan reddedilir.
    HANDLE h = CreateFileW(ext(path).c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION bi{};
    bool ok = GetFileInformationByHandle(h, &bi) && (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
              !(bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    if (ok) ok = mark_delete(h) == ERROR_SUCCESS;
    CloseHandle(h);
    return ok;
}

contracts::Result<std::string> WinCleanupFs::read_prefix(const std::wstring& path, std::size_t max_bytes) {
    using R = contracts::Result<std::string>;
    if (max_bytes == 0) return std::string();
    HANDLE h = CreateFileW(ext(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "open for read " + std::to_string(e));
    }
    BY_HANDLE_FILE_INFORMATION bi{};
    if (!GetFileInformationByHandle(h, &bi) || (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(h);
        return R::fail(ErrorCode::Denied, "not a plain file");
    }
    std::string out(max_bytes, '\0');
    DWORD got = 0;
    const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok) return R::fail(ErrorCode::IoError, "read prefix");
    out.resize(got);
    return out;
}

std::optional<std::wstring> WinCleanupFs::volume_of(const std::wstring& path) {
    wchar_t buf[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(path.c_str(), buf, MAX_PATH + 1)) return std::nullopt;
    return std::wstring(buf);
}

std::optional<std::uint64_t> WinCleanupFs::free_bytes(const std::wstring& volume) {
    ULARGE_INTEGER total_free{};
    if (!GetDiskFreeSpaceExW(volume.c_str(), nullptr, nullptr, &total_free)) return std::nullopt;
    return total_free.QuadPart;
}

std::optional<core::cleanup::VolumeId> WinCleanupFs::volume_id(const std::wstring& volume) {
    if (volume.size() < 3 || volume[1] != L':' || volume.back() != L'\\') return std::nullopt;
    if (GetDriveTypeW(volume.c_str()) != DRIVE_FIXED) return std::nullopt;
    wchar_t label[MAX_PATH + 1] = {}, fsname[MAX_PATH + 1] = {};
    DWORD serial = 0, maxlen = 0, flags = 0;
    if (!GetVolumeInformationW(volume.c_str(), label, MAX_PATH + 1, &serial, &maxlen, &flags, fsname, MAX_PATH + 1)) return std::nullopt;
    core::cleanup::VolumeId id;
    id.root = volume;
    id.serial = serial;
    id.label = label;
    id.fs = fsname;
    DWORD spc = 0, bps = 0, nfree = 0, ntotal = 0;
    if (GetDiskFreeSpaceW(volume.c_str(), &spc, &bps, &nfree, &ntotal)) id.cluster = spc * bps;
    id.known = true;
    return id;
}

contracts::Result<std::uint64_t> WinCleanupFs::standby_bytes() { return read_standby_bytes(); }

contracts::Result<core::cleanup::RecycleBinInfo> WinCleanupFs::query_recycle_bin(const std::wstring& volume) {
    using R = contracts::Result<core::cleanup::RecycleBinInfo>;
    SHQUERYRBINFO q{};
    q.cbSize = sizeof q;
    const HRESULT hr = SHQueryRecycleBinW(volume.empty() ? nullptr : volume.c_str(), &q);
    if (FAILED(hr)) return R::fail(ErrorCode::IoError, "SHQueryRecycleBin " + std::to_string(static_cast<long>(hr)));
    return core::cleanup::RecycleBinInfo{static_cast<std::uint64_t>(q.i64NumItems), static_cast<std::uint64_t>(q.i64Size)};
}

contracts::Status WinCleanupFs::empty_recycle_bin(const std::wstring& volume) {
    const HRESULT hr = SHEmptyRecycleBinW(nullptr, volume.empty() ? nullptr : volume.c_str(),
                                          SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    if (SUCCEEDED(hr)) return contracts::ok_status();
    // Zaten bos kutu bazi surumlerde hata dondurur: sonuc yeniden sorgu ile dogrulanir.
    auto q = query_recycle_bin(volume);
    if (q.ok() && q.value().items == 0) return contracts::ok_status();
    return contracts::Status::fail(ErrorCode::IoError, "SHEmptyRecycleBin " + std::to_string(static_cast<long>(hr)));
}

core::cleanup::CleanupEnv discover_env() {
    core::cleanup::CleanupEnv env;
    env.local_app_data = known_folder(FOLDERID_LocalAppData);
    env.windir = known_folder(FOLDERID_Windows);
    env.program_data = known_folder(FOLDERID_ProgramData);
    wchar_t tmp[MAX_PATH + 1] = {};
    const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
    if (n > 0 && n <= MAX_PATH) {
        std::wstring t(tmp, n);
        std::wstring lng(32768, L'\0');
        const DWORD m = GetLongPathNameW(t.c_str(), lng.data(), static_cast<DWORD>(lng.size()));
        if (m > 0 && m < lng.size()) {
            lng.resize(m);
            t = std::move(lng);
        }
        while (t.size() > 3 && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
        env.temp = std::move(t);
    }
    env.elevated = is_process_elevated();
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    env.now_filetime = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    // EXT-CLN: uygulama test klasorleri yalniz yerel sabit birimlerde aranir (ag/cikarilabilir kapali).
    WinCleanupFs fs;
    for (wchar_t d = L'A'; d <= L'Z'; ++d) {
        const std::wstring root{d, L':', L'\\'};
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        if (fs.volume_id(root)) env.local_roots.push_back(root);
    }
    return env;
}

std::wstring system_volume() {
    const std::wstring win = known_folder(FOLDERID_Windows);
    if (win.empty()) return {};
    wchar_t buf[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(win.c_str(), buf, MAX_PATH + 1)) return {};
    return buf;
}

}  // namespace ef::platform::cleanup
