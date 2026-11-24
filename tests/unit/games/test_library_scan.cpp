// A/B: kitaplik taramasi gercek ayristirici ve gercek tarama yolundan test edilir. Sahte olan yalniz dosya sistemi
// kaynagidir; ikinci bolumde ayni yol gercek Windows dosya sistemiyle (gecici klasor) calistirilir.
#include <windows.h>
#include <aclapi.h>

#include <filesystem>
#include <fstream>
#include <map>

#include "core/games/library_scan.h"
#include "core/games/protection_gate.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/win_env.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {

const char* acf(const char* appid, const char* name, const char* dir) {
    static std::string s;
    s = std::string("\"AppState\"\n{\n\t\"appid\"\t\t\"") + appid + "\"\n\t\"name\"\t\t\"" + name + "\"\n\t\"installdir\"\t\t\"" + dir +
        "\"\n\t\"SizeOnDisk\"\t\t\"1024\"\n\t\"buildid\"\t\t\"77\"\n}\n";
    return s.c_str();
}

class FakeFs final : public LibraryFs {
public:
    SourceValue steam{SourceValue::Status::Found, "S:\\steam"};
    SourceValue epic{SourceValue::Status::Missing, ""};
    std::map<std::string, PathKind> kinds;
    std::map<std::string, DirList> dirs;
    std::map<std::string, FileRead> files;

    void lib(const std::string& root) {
        kinds[root + "\\steamapps"] = PathKind::Directory;
        dirs[root + "\\steamapps"] = DirList{DirList::Status::Ok, {}};
    }
    void manifest(const std::string& root, const std::string& file, FileRead r) {
        dirs[root + "\\steamapps"].names.push_back(file);
        files[root + "\\steamapps\\" + file] = std::move(r);
    }
    void ok_manifest(const std::string& root, const std::string& file, const std::string& text) {
        manifest(root, file, FileRead{FileRead::Status::Ok, text});
    }
    void vdf(const std::string& text) {
        kinds["S:\\steam\\steamapps\\libraryfolders.vdf"] = PathKind::File;
        files["S:\\steam\\steamapps\\libraryfolders.vdf"] = FileRead{FileRead::Status::Ok, text};
    }

    SourceValue steam_root() override { return steam; }
    SourceValue epic_manifest_dir() override { return epic; }
    PathKind probe(const std::string& p) override {
        const auto it = kinds.find(p);
        return it == kinds.end() ? PathKind::Missing : it->second;
    }
    DirList list(const std::string& d) override {
        const auto it = dirs.find(d);
        return it == dirs.end() ? DirList{DirList::Status::Missing, {}} : it->second;
    }
    FileRead read_file(const std::string& p, std::size_t) override {
        const auto it = files.find(p);
        return it == files.end() ? FileRead{FileRead::Status::Missing, {}} : it->second;
    }
};

std::string two_libs() {
    return "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"S:\\\\steam\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"T:\\\\lib\"\n\t}\n}\n";
}

}  // namespace

EF_TEST(library_valid_game_is_ok) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted());
    EF_CHECK(r.games.size() == 1 && r.games[0].install_dir == "S:\\steam\\steamapps\\common\\cs2" && r.games[0].app_id == "730");
    EF_CHECK(r.skipped_manifests == 0 && r.excluded_non_games == 0);
}

// Bu makinedeki surekli Partial'in kok nedeni: Steamworks Common Redistributables (228980).
EF_TEST(library_helper_package_is_excluded_not_an_error) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.ok_manifest("S:\\steam", "appmanifest_228980.acf", acf("228980", "Steamworks Common Redistributables", "Steamworks Shared"));
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted());
    EF_CHECK(r.excluded_non_games == 1 && r.malformed_manifests == 0 && r.skipped_manifests == 0 && r.issues.empty());
    EF_CHECK(r.games.size() == 1);
}

EF_TEST(library_only_helper_package_is_trusted_empty) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.ok_manifest("S:\\steam", "appmanifest_228980.acf", acf("228980", "Steamworks Common Redistributables", "Steamworks Shared"));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Empty && r.trusted() && r.excluded_non_games == 1);
}

EF_TEST(library_corrupt_manifest_is_partial_with_issue) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.ok_manifest("S:\\steam", "appmanifest_1.acf", "bu bir manifest degil");
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial && !r.trusted());
    EF_CHECK(r.malformed_manifests == 1 && r.games.size() == 1);
    EF_CHECK(r.issues.size() == 1 && r.issues[0].kind == LibraryIssueKind::ManifestCorrupt);
}

EF_TEST(library_missing_required_field_is_corrupt) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.ok_manifest("S:\\steam", "appmanifest_5.acf", "\"AppState\"\n{\n\t\"appid\"\t\"5\"\n\t\"name\"\t\"X\"\n}\n");
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial && r.malformed_manifests == 1);
}

EF_TEST(library_unreadable_manifest_is_partial_not_excluded) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.manifest("S:\\steam", "appmanifest_9.acf", FileRead{FileRead::Status::Denied, {}});
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial);
    EF_CHECK(r.unreadable_manifests == 1 && r.malformed_manifests == 0 && r.excluded_non_games == 0);
    EF_CHECK(r.issues[0].kind == LibraryIssueKind::ManifestUnreadable);
}

EF_TEST(library_truncated_read_and_cut_file_are_not_games) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.manifest("S:\\steam", "appmanifest_9.acf", FileRead{FileRead::Status::Truncated, {}});
    // Icerik gerekli tum alanlari tasir ama kapanis yok: bir sonraki alanlar kesilmis olabilir, oyun sayilmaz.
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", "\"AppState\"\n{\n\t\"appid\"\t\"730\"\n\t\"name\"\t\"CS\"\n\t\"installdir\"\t\"cs2\"\n\t\"SizeOnDisk\"\t\"10\"\n");
    // Kapanmayan dize de kesik dosyadir.
    fs.ok_manifest("S:\\steam", "appmanifest_6.acf", "\"AppState\"\n{\n\t\"appid\"\t\"6");
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial && r.games.empty());
    EF_CHECK(r.unreadable_manifests == 1 && r.malformed_manifests == 2);
}

EF_TEST(library_really_empty_is_trusted) {
    FakeFs fs;
    fs.lib("S:\\steam");
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Empty && r.trusted() && r.source_errors == 0);
    const auto b = watch_bootstrap(r);
    EF_CHECK(b.no_games && b.detection == DetectionState::Ok);
}

EF_TEST(library_one_of_two_unreadable_is_partial_keeps_other) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.vdf(two_libs());
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    fs.kinds["T:\\lib\\steamapps"] = PathKind::Directory;
    fs.dirs["T:\\lib\\steamapps"] = DirList{DirList::Status::Denied, {}};
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial && r.games.size() == 1 && r.source_errors == 1);
    EF_CHECK(r.issues.size() == 1 && r.issues[0].kind == LibraryIssueKind::SourceFolder && r.issues[0].path == "T:\\lib\\steamapps");
}

EF_TEST(library_unplugged_second_library_is_not_error) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.vdf(two_libs());  // T: yok (kinds'te Missing)
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Empty && r.source_errors == 0);
}

// B: "yok" ile "okunamadi" ayrimi.
EF_TEST(library_steam_not_installed_is_no_source) {
    FakeFs fs;
    fs.steam = {SourceValue::Status::Missing, ""};
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Empty && r.trusted());
}

EF_TEST(library_registry_denied_is_not_steam_absent) {
    FakeFs fs;
    fs.steam = {SourceValue::Status::Denied, ""};
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Failed && !r.trusted());
    EF_CHECK(r.issues.size() == 1 && r.issues[0].kind == LibraryIssueKind::SteamRegistry);
    EF_CHECK(!watch_bootstrap(r).no_games);
}

EF_TEST(library_denied_folder_is_not_missing_folder) {
    FakeFs denied;
    denied.kinds["S:\\steam\\steamapps"] = PathKind::Denied;
    const auto a = scan_library(denied);
    EF_CHECK(a.state == LibraryScanState::Failed && a.source_errors == 1);
    FakeFs missing;  // steamapps yok
    const auto b = scan_library(missing);
    EF_CHECK(b.state == LibraryScanState::Empty && b.source_errors == 0);
}

EF_TEST(library_incomplete_listing_is_partial) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.ok_manifest("S:\\steam", "appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    fs.dirs["S:\\steam\\steamapps"].status = DirList::Status::Incomplete;
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial && r.games.size() == 1 && r.source_errors == 1);
}

EF_TEST(library_truncated_library_list_is_error) {
    FakeFs fs;
    fs.lib("S:\\steam");
    fs.kinds["S:\\steam\\steamapps\\libraryfolders.vdf"] = PathKind::File;
    fs.files["S:\\steam\\steamapps\\libraryfolders.vdf"] = FileRead{FileRead::Status::Ok, "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\"S:\\\\steam\"\n"};
    const auto r = scan_library(fs);
    EF_CHECK(!r.trusted() && r.source_errors == 1 && r.issues[0].kind == LibraryIssueKind::LibraryList);
}

EF_TEST(library_epic_corrupt_and_missing_location) {
    FakeFs fs;
    fs.steam = {SourceValue::Status::Missing, ""};
    fs.epic = {SourceValue::Status::Found, "E:\\m"};
    fs.kinds["E:\\m"] = PathKind::Directory;
    fs.dirs["E:\\m"] = DirList{DirList::Status::Ok, {"a.item"}};
    fs.files["E:\\m\\a.item"] = FileRead{FileRead::Status::Ok, "{"};
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Partial && r.malformed_manifests == 1);
    FakeFs no_loc;
    no_loc.steam = {SourceValue::Status::Missing, ""};
    no_loc.epic = {SourceValue::Status::Error, ""};
    EF_CHECK(scan_library(no_loc).state == LibraryScanState::Failed);
}

EF_TEST(library_poll_and_fingerprint) {
    LibraryScanResult r;
    r.state = LibraryScanState::Empty;
    EF_CHECK(library_poll_ms(r) == kLibraryPollEmptyMs);
    static_assert(kLibraryPollEmptyMs * 2 < kMaxDetectionAgeMs, "tek kacirilan tur kapiyi kilitlemez");
    const auto empty = library_fingerprint(r);
    r.state = LibraryScanState::Partial;
    EF_CHECK(library_poll_ms(r) == kLibraryPollUntrustedMs);
    EF_CHECK(library_fingerprint(r) != empty);  // guven degisimi de yenileme gerektirir
    r.state = LibraryScanState::Ok;
    r.games.resize(1);
    r.games[0].install_dir = "x";
    EF_CHECK(library_poll_ms(r) == kLibraryPollGamesMs);
}

// ---- Gercek Windows dosya sistemi (kullanici dosyasina dokunmaz; yalniz gecici klasor) ----
namespace {
namespace sf = std::filesystem;
struct TempTree {
    sf::path root;
    TempTree() {
        wchar_t t[MAX_PATH];
        GetTempPathW(MAX_PATH, t);
        root = sf::path(t) / (L"ef_libscan_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
        sf::create_directories(root / L"steam" / L"steamapps");
    }
    ~TempTree() {
        std::error_code ec;
        sf::remove_all(root, ec);
    }
    sf::path apps() const { return root / L"steam" / L"steamapps"; }
    void write(const sf::path& p, const std::string& text) const {
        std::ofstream f(p, std::ios::binary);
        f << text;
    }
    LibraryScanResult scan() const {
        ef::platform::games::WinLibraryFs fs;
        fs.set_overrides(ef::platform::to_utf8((root / L"steam").wstring()), {});
        return scan_library(fs);
    }
};
}  // namespace

EF_TEST(library_real_fs_valid_helper_corrupt_empty) {
    TempTree t;
    EF_CHECK(t.scan().state == LibraryScanState::Empty);  // gercekten bos
    t.write(t.apps() / L"appmanifest_228980.acf", acf("228980", "Steamworks Common Redistributables", "Steamworks Shared"));
    auto r = t.scan();
    EF_CHECK(r.state == LibraryScanState::Empty && r.excluded_non_games == 1);
    t.write(t.apps() / L"appmanifest_730.acf", acf("730", "Counter-Strike 2", "cs2"));
    r = t.scan();
    EF_CHECK(r.state == LibraryScanState::Ok && r.games.size() == 1);
    t.write(t.apps() / L"appmanifest_11.acf", "\"AppState\"\n{\n");  // kesilmis
    r = t.scan();
    EF_CHECK(r.state == LibraryScanState::Partial && r.malformed_manifests == 1 && r.games.size() == 1);
}

EF_TEST(library_real_fs_locked_manifest_is_unreadable) {
    TempTree t;
    const auto p = t.apps() / L"appmanifest_44.acf";
    t.write(p, acf("44", "G", "g"));
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, 0 /* paylasim yok */, nullptr, OPEN_EXISTING, 0, nullptr);
    EF_CHECK(h != INVALID_HANDLE_VALUE);
    const auto r = t.scan();
    CloseHandle(h);
    EF_CHECK(r.state == LibraryScanState::Partial && r.unreadable_manifests == 1 && r.malformed_manifests == 0);
    EF_CHECK(t.scan().state == LibraryScanState::Ok);  // kilit kalkinca toparlanir
}

EF_TEST(library_real_fs_denied_folder_not_missing) {
    TempTree t;
    // Yalniz gecici klasorde: bos korumali DACL listelemeyi reddeder (gercek erisim hatasi).
    ACL empty{};
    EF_CHECK(InitializeAcl(&empty, sizeof(empty), ACL_REVISION) != 0);
    std::wstring dir = t.apps().wstring();
    const auto st = SetNamedSecurityInfoW(dir.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, &empty, nullptr);
    EF_CHECK(st == ERROR_SUCCESS);
    const auto r = t.scan();
    EF_CHECK(r.state == LibraryScanState::Failed && r.source_errors == 1 && !r.trusted());
    (void)SetNamedSecurityInfoW(dir.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr);
    std::error_code ec;
    sf::remove_all(t.root, ec);
    EF_CHECK(t.scan().state == LibraryScanState::Empty);  // kaldirilmis klasor: kaynak yok (hata degil)
}
