// T05: Riot (product_settings.yaml) ve Battle.net (kayit listesi) saglayicilari. Sahte FS; gercek launcher ornegi degildir.
#include <map>
#include <string>
#include <vector>

#include "core/games/library_scan.h"
#include "core/games/protection_gate.h"
#include "core/games/watcher.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {
constexpr const char* kMd = "C:\\ProgramData\\Riot Games\\Metadata";
constexpr const char* kValYaml = "C:\\ProgramData\\Riot Games\\Metadata\\valorant.live\\valorant.live.product_settings.yaml";
constexpr const char* kLolYaml =
    "C:\\ProgramData\\Riot Games\\Metadata\\league_of_legends.live\\league_of_legends.live.product_settings.yaml";
constexpr const char* kValExe = "C:\\Riot Games\\VALORANT\\live\\ShooterGame\\Binaries\\Win64\\VALORANT-Win64-Shipping.exe";
constexpr const char* kLolExe = "C:\\Riot Games\\League of Legends\\Game\\League of Legends.exe";

struct Fake final : LibraryFs {
    SourceValue md{SourceValue::Status::Found, kMd};
    std::map<std::string, PathKind> kinds;
    std::map<std::string, FileRead> files;
    std::vector<std::string> reads;
    RegistryList bnet;
    SourceValue steam_root() override { return {SourceValue::Status::Missing, {}}; }
    SourceValue epic_manifest_dir() override { return {SourceValue::Status::Missing, {}}; }
    SourceValue riot_metadata_dir() override { return md; }
    PathKind probe(const std::string& p) override {
        const auto it = kinds.find(p);
        return it == kinds.end() ? PathKind::Missing : it->second;
    }
    DirList list(const std::string&) override { return {DirList::Status::Missing, {}}; }
    FileRead read_file(const std::string& p, std::size_t) override {
        reads.push_back(p);
        const auto it = files.find(p);
        return it == files.end() ? FileRead{FileRead::Status::Missing, {}} : it->second;
    }
    RegistryList registry_installs(std::string_view p) override { return p == "battlenet" ? bnet : RegistryList{}; }
};
const ProviderResult* of(const LibraryScanResult& r, const char* name) {
    for (const auto& p : r.providers)
        if (p.provider == name) return &p;
    return nullptr;
}
FileRead ok(std::string s) { return {FileRead::Status::Ok, std::move(s)}; }
Fake installed() {
    Fake f;
    f.kinds[kMd] = PathKind::Directory;
    f.files[kValYaml] = ok("install_tags: []\r\nproduct_install_full_path: \"C:/Riot Games/VALORANT/live\"\r\nshould_repair: false\r\n");
    f.files[kLolYaml] = ok("  product_install_full_path: \"C:\\\\Riot Games\\\\League of Legends\\\\\"\n");
    f.kinds[kValExe] = PathKind::File;
    f.kinds[kLolExe] = PathKind::File;
    return f;
}
}  // namespace

EF_TEST(t05_riot_parser_accepts_quoted_absolute_paths) {
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"C:/Riot Games/VALORANT/live\"\n") ==
              "C:\\Riot Games\\VALORANT\\live");
    EF_CHECK(parse_riot_product_install_path("a: 1\n  product_install_full_path: 'D:\\Games\\LoL\\'\r\n") == "D:\\Games\\LoL");
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"E:\\\\Riot\\\\Valorant\"") == "E:\\Riot\\Valorant");
}

EF_TEST(t05_riot_parser_rejects_missing_relative_or_unsafe_values) {
    EF_CHECK(parse_riot_product_install_path("").empty());
    EF_CHECK(parse_riot_product_install_path("product_install_root: \"C:/Riot Games\"\n").empty());   // farkli anahtar
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: C:/Riot Games\n").empty());  // tirnaksiz
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"C:/Riot Games\n").empty());  // kapanmamis
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"Riot Games/VALORANT\"\n").empty());  // goreli
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"C:/Riot/../Windows\"\n").empty());
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"C:/\"\n").empty());  // surucu koku
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"C:\\nRiot\"\n").empty());  // tanimsiz kacis
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"\\\\\\\\srv\\\\share\"\n").empty());  // UNC
    EF_CHECK(parse_riot_product_install_path("product_install_full_path: \"C:/" + std::string(300, 'a') + "\"\n").empty());
}

EF_TEST(t05_riot_found_emits_known_products_with_exe_hint) {
    Fake f = installed();
    const auto r = scan_library(f);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Found && of(r, "riot")->games == 2);
    EF_CHECK(r.games.size() == 2 && r.trusted());
    EF_CHECK(r.games[0].launcher == "riot" && r.games[0].app_id == "valorant.live" && r.games[0].name == "VALORANT");
    EF_CHECK(r.games[0].install_dir == "C:\\Riot Games\\VALORANT\\live");
    EF_CHECK(r.games[1].app_id == "league_of_legends.live" && r.games[1].install_dir == "C:\\Riot Games\\League of Legends");
    EF_CHECK(r.games[1].exe_hint == "Game\\League of Legends.exe");
    // Yalniz bilinen urunler okunur: riot_client (launcher) ve bilinmeyen urun klasoru hic okunmaz.
    for (const auto& p : f.reads) EF_CHECK(p.find("riot_client") == std::string::npos);
}

EF_TEST(t05_riot_metadata_missing_is_not_an_error) {
    Fake f;
    f.md = {SourceValue::Status::Missing, {}};
    auto r = scan_library(f);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Missing && r.trusted() && r.source_errors == 0);
    Fake g;  // kaynak yolu var ama klasor yok
    r = scan_library(g);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Missing && r.trusted());
    g.kinds[kMd] = PathKind::Directory;  // klasor var, urun yok
    r = scan_library(g);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Missing && r.games.empty() && r.trusted());
}

EF_TEST(t05_riot_denied_or_error_source_is_not_trusted) {
    Fake f = installed();
    f.md = {SourceValue::Status::Denied, {}};
    auto r = scan_library(f);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Denied && !r.trusted() && r.source_errors == 1);
    Fake g = installed();
    g.kinds[kMd] = PathKind::Denied;
    r = scan_library(g);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Denied && !r.trusted());
    g.kinds[kMd] = PathKind::File;
    r = scan_library(g);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Error && !r.trusted());
}

EF_TEST(t05_riot_malformed_yaml_counts_and_keeps_other_product) {
    Fake f = installed();
    f.files[kValYaml] = ok("product_install_full_path: relative/path\n");
    auto r = scan_library(f);
    EF_CHECK(r.malformed_manifests == 1 && r.games.size() == 1 && r.games[0].app_id == "league_of_legends.live");
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Partial && !r.trusted());
    f = installed();
    f.files[kLolYaml] = {FileRead::Status::Denied, {}};
    r = scan_library(f);
    EF_CHECK(r.unreadable_manifests == 1 && r.games.size() == 1 && !r.trusted());
}

EF_TEST(t05_riot_missing_exe_is_stale_record_not_error) {
    Fake f = installed();
    f.kinds.erase(kValExe);  // kaldirilmis oyun: yaml kaldi
    auto r = scan_library(f);
    EF_CHECK(r.games.size() == 1 && r.games[0].app_id == "league_of_legends.live" && r.trusted());
    f.kinds[kValExe] = PathKind::Denied;  // okunamayan exe: "yok" denmez
    r = scan_library(f);
    EF_CHECK(r.games.size() == 1 && r.unreadable_manifests == 1 && !r.trusted());
}

EF_TEST(t05_battlenet_registry_list_found_missing_denied) {
    Fake f;
    f.md = {SourceValue::Status::Missing, {}};
    auto r = scan_library(f);  // kurulu degil
    EF_CHECK(of(r, "battlenet")->status == ProviderStatus::Missing && r.trusted());
    f.bnet.status = SourceValue::Status::Found;
    f.bnet.items = {{"Overwatch", "Overwatch", "C:\\Program Files (x86)\\Overwatch", ""}};
    f.kinds["C:\\Program Files (x86)\\Overwatch"] = PathKind::Directory;
    r = scan_library(f);
    EF_CHECK(of(r, "battlenet")->status == ProviderStatus::Found && r.games.size() == 1 && r.games[0].launcher == "battlenet");
    EF_CHECK(r.games[0].exe_hint.empty() && r.trusted());  // exe gozlemle cozulur
    f.bnet.items = {{"Diablo IV", "Diablo IV", "", ""}};  // yolsuz kayit: temiz denmez
    r = scan_library(f);
    EF_CHECK(r.games.empty() && r.malformed_manifests == 1 && !r.trusted());
    f.bnet.status = SourceValue::Status::Denied;
    r = scan_library(f);
    EF_CHECK(of(r, "battlenet")->status == ProviderStatus::Denied && !r.trusted());
}

EF_TEST(t05_launcher_processes_are_helpers_not_games) {
    for (const char* n : {"riotclientservices.exe", "riot client.exe", "riotclientux.exe", "leagueclient.exe", "battle.net.exe", "vgc.exe"})
        EF_CHECK(is_helper_exe(n));
    EF_CHECK(!is_helper_exe("valorant-win64-shipping.exe") && !is_helper_exe("league of legends.exe"));
}

EF_TEST(t05_riot_and_cod_rows_are_unverified_without_live_capture) {
    int found = 0;
    for (const auto& e : builtin_matrix())
        if (e.game_id == "valorant.live" || e.game_id == "league_of_legends.live" || e.protection == "Ricochet") {
            ++found;
            EF_CHECK(e.state == CompatState::Unverified && !e.live_capture_enabled);
        }
    EF_CHECK(found == 4);
}
