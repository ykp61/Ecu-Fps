#include "platform/windows/games/library_scan.h"

#include <windows.h>
#include <shlobj.h>

#include <cctype>
#include <chrono>
#include <string>

#include "platform/windows/win_env.h"

namespace ef::platform::games {

using core::games::DirList;
using core::games::FileRead;
using core::games::PathKind;
using core::games::SourceValue;

namespace {

// Aygit/yol yok: kaynak gercekten yok (kaldirilmis klasor, takili olmayan surucu).
bool is_missing(DWORD e) {
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_DRIVE || e == ERROR_NOT_READY;
}

bool is_denied(DWORD e) { return e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION; }

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

SourceValue read_steam_path(const std::wstring& sid) {
    SourceValue out;
    HKEY root = HKEY_CURRENT_USER;
    std::wstring sub = L"Software\\Valve\\Steam";
    if (!sid.empty()) {
        root = HKEY_USERS;
        sub = sid + L"\\" + sub;
    }
    wchar_t buf[2048] = {};
    DWORD size = sizeof(buf);
    const LSTATUS st = RegGetValueW(root, sub.c_str(), L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size);
    if (st == ERROR_SUCCESS) {
        out.status = SourceValue::Status::Found;
        out.value = to_utf8(buf);
        return out;
    }
    if (st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND) out.status = SourceValue::Status::Missing;
    else if (st == ERROR_ACCESS_DENIED) out.status = SourceValue::Status::Denied;
    else out.status = SourceValue::Status::Error;
    return out;
}

}  // namespace

void WinLibraryFs::set_overrides(std::string steam_root, std::string epic_dir) {
    overridden_ = true;
    steam_override_ = std::move(steam_root);
    epic_override_ = std::move(epic_dir);
}

SourceValue WinLibraryFs::steam_root() {
    if (overridden_) {
        SourceValue v;
        v.status = steam_override_.empty() ? SourceValue::Status::Missing : SourceValue::Status::Found;
        v.value = steam_override_;
        return v;
    }
    return read_steam_path(sid_);
}

SourceValue WinLibraryFs::epic_manifest_dir() {
    SourceValue out;
    if (overridden_) {
        out.status = epic_override_.empty() ? SourceValue::Status::Missing : SourceValue::Status::Found;
        out.value = epic_override_;
        return out;
    }
    PWSTR pd = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) {
        out.status = SourceValue::Status::Error;
        return out;
    }
    const std::wstring dir = std::wstring(pd) + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    CoTaskMemFree(pd);
    out.status = SourceValue::Status::Found;
    out.value = to_utf8(dir);
    return out;
}

SourceValue WinLibraryFs::riot_metadata_dir() {
    SourceValue out;
    if (overridden_) {  // testler: ProgramData'ya dokunma
        out.status = SourceValue::Status::Missing;
        return out;
    }
    PWSTR pd = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) {
        out.status = SourceValue::Status::Error;
        return out;
    }
    const std::wstring dir = std::wstring(pd) + L"\\Riot Games\\Metadata";
    CoTaskMemFree(pd);
    out.status = SourceValue::Status::Found;
    out.value = to_utf8(dir);
    return out;
}

PathKind WinLibraryFs::probe(const std::string& path) {
    const DWORD a = GetFileAttributesW(to_wide(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        if (is_missing(e)) return PathKind::Missing;
        return is_denied(e) ? PathKind::Denied : PathKind::Error;
    }
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? PathKind::Directory : PathKind::File;
}

DirList WinLibraryFs::list(const std::string& dir) {
    DirList out;
    WIN32_FIND_DATAW fd;
    std::wstring pattern = to_wide(dir);
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern.push_back(L'\\');
    pattern.push_back(L'*');
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (is_missing(e)) out.status = DirList::Status::Missing;
        else out.status = is_denied(e) ? DirList::Status::Denied : DirList::Status::Error;
        return out;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        out.names.push_back(to_utf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    const DWORD last = GetLastError();
    FindClose(h);
    out.status = last == ERROR_NO_MORE_FILES ? DirList::Status::Ok : DirList::Status::Incomplete;
    return out;
}

// T05: Battle.net oyunlari Uninstall anahtarinda Blizzard/Activision yayinci + Battle.net kaldiriciyla kayitlidir.
// 32 ve 64 bit gorunum taranir; launcher'in kendisi atlanir. EXE ipucu bos birakilir (exe_resolver gozlemle cozer).
core::games::RegistryList WinLibraryFs::battlenet_installs() {
    using St = core::games::SourceValue::Status;
    core::games::RegistryList out;
    const auto lower = [](std::string v) {
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return v;
    };
    const auto read = [](HKEY k, const wchar_t* name) {
        wchar_t buf[2048] = {};
        DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return std::string();
        return to_utf8(buf);
    };
    bool any_view = false, enum_error = false;
    std::vector<core::games::UninstallEntry> entries;
    for (const REGSAM view : {static_cast<REGSAM>(KEY_WOW64_32KEY), static_cast<REGSAM>(KEY_WOW64_64KEY)}) {
        HKEY base = nullptr;
        const LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &base);
        if (st == ERROR_FILE_NOT_FOUND) continue;
        if (st != ERROR_SUCCESS) {
            out.status = st == ERROR_ACCESS_DENIED ? St::Denied : St::Error;
            return out;
        }
        any_view = true;
        for (DWORD i = 0; i < 8192; ++i) {
            wchar_t name[256];
            DWORD len = 256;
            const LSTATUS e = RegEnumKeyExW(base, i, name, &len, nullptr, nullptr, nullptr, nullptr);
            if (e == ERROR_NO_MORE_ITEMS) break;
            if (e != ERROR_SUCCESS) {
                enum_error = true;
                break;
            }
            HKEY sub = nullptr;
            if (RegOpenKeyExW(base, name, 0, KEY_READ | view, &sub) != ERROR_SUCCESS) continue;  // baska yayincinin anahtari olabilir
            core::games::UninstallEntry entry;
            entry.key = to_utf8(name);
            entry.uninstall_string = read(sub, L"UninstallString");
            entry.publisher = read(sub, L"Publisher");
            entry.display_name = read(sub, L"DisplayName");
            entry.install_location = read(sub, L"InstallLocation");
            RegCloseKey(sub);
            // Yalniz ham ipucu ayirimi burada (bellek sinirli); suzme kurali core'da (fixture ile test edilir).
            if (lower(entry.uninstall_string).find("battle.net") == std::string::npos) continue;
            entries.push_back(std::move(entry));
        }
        RegCloseKey(base);
        if (enum_error) {
            out = core::games::battlenet_list_from_uninstall_entries(entries);
            out.status = St::Error;  // numaralandirma kesildi: yarim liste guvenilmez
            return out;
        }
    }
    if (!any_view) return out;  // Missing
    return core::games::battlenet_list_from_uninstall_entries(entries);
}

// P9-B3: EA app. DOGRULANMADI: bu PC'de EA kurulu degil; kayit yerlesimi kamuya acik bilgiye dayanir ve urunde kapalidir
// (kEaRegistryValidated). YALNIZ HKLM altindaki kurulum yolu degeri ("Install Dir") okunur; ProgramData\EA Desktop ve
// Origin LocalContent'teki belgelenmemis/sifreli manifestlere hic dokunulmaz. Yazma yok.
core::games::RegistryList WinLibraryFs::ea_installs() {
    using St = core::games::SourceValue::Status;
    core::games::RegistryList out;
    static const wchar_t* const kRoots[] = {L"SOFTWARE\\WOW6432Node\\EA Games", L"SOFTWARE\\WOW6432Node\\Electronic Arts", L"SOFTWARE\\EA Games",
                                           L"SOFTWARE\\Electronic Arts"};
    const auto read = [](HKEY k, const wchar_t* value) {
        wchar_t buf[2048] = {};
        DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return std::string();
        return to_utf8(buf);
    };
    bool any_root = false;
    for (const wchar_t* root : kRoots) {
        HKEY base = nullptr;
        const LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &base);
        if (st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND) continue;
        if (st != ERROR_SUCCESS) {
            out.status = st == ERROR_ACCESS_DENIED ? St::Denied : St::Error;
            return out;
        }
        any_root = true;
        for (DWORD i = 0; i < 4096; ++i) {
            wchar_t name[256];
            DWORD len = 256;
            const LSTATUS e = RegEnumKeyExW(base, i, name, &len, nullptr, nullptr, nullptr, nullptr);
            if (e == ERROR_NO_MORE_ITEMS) break;
            if (e != ERROR_SUCCESS) {
                RegCloseKey(base);
                out.status = St::Error;  // yarim numaralandirma: guvenilmez
                return out;
            }
            HKEY sub = nullptr;
            const LSTATUS open_st = RegOpenKeyExW(base, name, 0, KEY_READ, &sub);
            if (open_st != ERROR_SUCCESS) {
                if (open_st != ERROR_FILE_NOT_FOUND) out.items.push_back({to_utf8(name), {}, {}, {}});  // bos yol: core bozuk sayar
                continue;
            }
            std::string dir = read(sub, L"Install Dir");
            RegCloseKey(sub);
            if (dir.empty()) continue;  // kurulum yolu degeri olmayan anahtar oyun kaydi sayilmaz (bicim DOGRULANMADI)
            while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
            const std::string id = to_utf8(name);
            bool dup = false;
            for (const auto& it : out.items) dup = dup || (it.id == id && it.path == dir);
            if (!dup) out.items.push_back({id, id, dir, {}});  // ad = anahtar adi; exe/surum/boyut okunmaz (Unsupported)
        }
        RegCloseKey(base);
    }
    out.status = !any_root || out.items.empty() ? St::Missing : St::Found;
    return out;
}

core::games::RegistryList WinLibraryFs::registry_installs(std::string_view provider) {
    using St = core::games::SourceValue::Status;
    core::games::RegistryList out;
    if (provider == "battlenet") return battlenet_installs();
    if (provider == "ea") return ea_installs();
    const bool gog = provider == "gog";
    if (!gog && provider != "ubisoft") return out;
    const wchar_t* root = gog ? L"SOFTWARE\\WOW6432Node\\GOG.com\\Games" : L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs";
    HKEY base = nullptr;
    const LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &base);
    if (st == ERROR_FILE_NOT_FOUND) return out;
    if (st != ERROR_SUCCESS) {
        out.status = st == ERROR_ACCESS_DENIED ? St::Denied : St::Error;
        return out;
    }
    const auto read = [](HKEY k, const wchar_t* name) {
        wchar_t buf[1024] = {};
        DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return std::string();
        return to_utf8(buf);
    };
    out.status = St::Found;
    for (DWORD i = 0; i < 4096; ++i) {
        wchar_t name[256];
        DWORD len = 256;
        const LSTATUS e = RegEnumKeyExW(base, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (e == ERROR_NO_MORE_ITEMS) break;
        if (e != ERROR_SUCCESS) {
            out.status = St::Error;
            break;
        }
        HKEY sub = nullptr;
        if (const LSTATUS open_st = RegOpenKeyExW(base, name, 0, KEY_READ, &sub); open_st != ERROR_SUCCESS) {
            if (open_st != ERROR_FILE_NOT_FOUND) out.items.push_back({to_utf8(name), {}, {}, {}});  // bos yol: core bozuk sayar, "temiz" denmez
            continue;
        }
        core::games::RegistryInstall it;
        it.id = to_utf8(name);
        if (gog) {
            it.name = read(sub, L"gameName");
            it.path = read(sub, L"path");
            it.exe = read(sub, L"exe");
        } else {
            it.path = read(sub, L"InstallDir");
        }
        RegCloseKey(sub);
        out.items.push_back(std::move(it));
    }
    RegCloseKey(base);
    return out;
}

FileRead WinLibraryFs::read_file(const std::string& path, std::size_t max_bytes) {
    FileRead out;
    Handle f;
    f.h = CreateFileW(to_wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f.h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (is_missing(e)) out.status = FileRead::Status::Missing;
        else out.status = is_denied(e) ? FileRead::Status::Denied : FileRead::Status::Error;
        return out;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.h, &size) || size.QuadPart < 0) {
        out.status = FileRead::Status::Error;
        return out;
    }
    if (static_cast<unsigned long long>(size.QuadPart) > max_bytes) {
        out.status = FileRead::Status::TooLarge;
        return out;
    }
    out.data.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t total = 0;
    while (total < out.data.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(out.data.size() - total);
        if (!ReadFile(f.h, out.data.data() + total, want, &got, nullptr)) {
            out.status = is_denied(GetLastError()) ? FileRead::Status::Denied : FileRead::Status::Error;
            out.data.clear();
            return out;
        }
        if (got == 0) {
            out.status = FileRead::Status::Truncated;  // boyut bildirildi ama icerik eksik geldi
            out.data.clear();
            return out;
        }
        total += got;
    }
    out.status = FileRead::Status::Ok;
    return out;
}

core::games::ManualExeContext manual_exe_context() {
    core::games::ManualExeContext ctx;
    const auto known = [](REFKNOWNFOLDERID id) {
        PWSTR p = nullptr;
        std::string out;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = to_utf8(p);
        CoTaskMemFree(p);
        return out;
    };
    ctx.windows_dir = known(FOLDERID_Windows);
    for (const KNOWNFOLDERID* id : {&FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86, &FOLDERID_ProgramData, &FOLDERID_Profile,
                                    &FOLDERID_Desktop, &FOLDERID_Downloads})
        if (auto dir = known(*id); !dir.empty()) ctx.exact_roots.push_back(std::move(dir));
    return ctx;
}

ExeFileCheck check_exe_file(const std::string& path_utf8) {
    const std::wstring w = to_wide(path_utf8);
    const DWORD a = GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return is_missing(GetLastError()) ? ExeFileCheck::Missing : ExeFileCheck::Unreadable;
    if (a & FILE_ATTRIBUTE_DIRECTORY) return ExeFileCheck::NotFile;
    Handle f;
    f.h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f.h == INVALID_HANDLE_VALUE) return ExeFileCheck::Unreadable;
    char mz[2] = {};
    DWORD got = 0;
    if (!ReadFile(f.h, mz, 2, &got, nullptr)) return ExeFileCheck::Unreadable;
    return got == 2 && mz[0] == 'M' && mz[1] == 'Z' ? ExeFileCheck::Ok : ExeFileCheck::NotExecutable;
}

LibraryScanResult scan_installed_library(const std::wstring& steam_user_sid, const std::vector<std::string>& manual_exes) {
    WinLibraryFs fs(steam_user_sid);
    core::games::LibraryScanOptions options;
    options.manual_exes = manual_exes;
    auto out = core::games::scan_library(fs, options);
    out.scanned_at_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    return out;
}

std::vector<core::inventory::GameInstall> scan_installed_games() { return scan_installed_library().games; }

}  // namespace ef::platform::games
