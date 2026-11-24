// P9-B3: (1) EA saglayicisi: yalniz kayit defteri kurulum yolu; belgelenmemis/sifreli manifest OKUNMAZ; dogrulanamayan
// alanlar bos ("Unsupported") kalir. (2) Capraz kaynak tekillestirme: ayni kurulum klasorunu bildiren birden cok kaynak
// tek satira iner; guven hesabi (Partial/Failed) tekillestirmeden etkilenmez. Sahte FS; gercek launcher ornegi degildir.
#include <fstream>
#include <map>
#include <sstream>

#include "core/games/library_scan.h"
#include "core/serialization/json.h"
#include "ef_test.h"
#include "platform/windows/games/library_scan.h"

using namespace ef::core::games;

namespace {

class Fs final : public LibraryFs {
public:
    SourceValue steam{SourceValue::Status::Missing, {}};
    SourceValue epic{SourceValue::Status::Missing, {}};
    std::map<std::string, PathKind> kinds;
    std::map<std::string, DirList> dirs;
    std::map<std::string, FileRead> files;
    std::map<std::string, RegistryList> registry;
    std::vector<std::string> reads, lists, registry_asked;

    SourceValue steam_root() override { return steam; }
    SourceValue epic_manifest_dir() override { return epic; }
    PathKind probe(const std::string& p) override {
        const auto it = kinds.find(p);
        return it == kinds.end() ? PathKind::Missing : it->second;
    }
    DirList list(const std::string& d) override {
        lists.push_back(d);
        const auto it = dirs.find(d);
        return it == dirs.end() ? DirList{DirList::Status::Missing, {}} : it->second;
    }
    FileRead read_file(const std::string& p, std::size_t) override {
        reads.push_back(p);
        const auto it = files.find(p);
        return it == files.end() ? FileRead{FileRead::Status::Missing, {}} : it->second;
    }
    RegistryList registry_installs(std::string_view p) override {
        registry_asked.emplace_back(p);
        const auto it = registry.find(std::string(p));
        return it == registry.end() ? RegistryList{} : it->second;
    }

    void steam_game(const std::string& appid, const std::string& dir) {
        steam = {SourceValue::Status::Found, "S:\\steam"};
        kinds["S:\\steam\\steamapps"] = PathKind::Directory;
        auto& d = dirs["S:\\steam\\steamapps"];
        d.status = DirList::Status::Ok;
        const std::string name = "appmanifest_" + appid + ".acf";
        d.names.push_back(name);
        files["S:\\steam\\steamapps\\" + name] = {FileRead::Status::Ok,
                                                  "\"AppState\"\n{\n\t\"appid\"\t\t\"" + appid + "\"\n\t\"name\"\t\t\"G" + appid +
                                                      "\"\n\t\"installdir\"\t\t\"" + dir + "\"\n\t\"SizeOnDisk\"\t\t\"10\"\n}\n"};
    }
    void epic_game(const std::string& name, const std::string& location) {
        epic = {SourceValue::Status::Found, "E:\\epic"};
        kinds["E:\\epic"] = PathKind::Directory;
        auto& d = dirs["E:\\epic"];
        d.status = DirList::Status::Ok;
        d.names.push_back(name + ".item");
        std::string esc;
        for (const char c : location) esc += c == '\\' ? std::string("\\\\") : std::string(1, c);
        files["E:\\epic\\" + name + ".item"] = {FileRead::Status::Ok,
                                                "{\"DisplayName\":\"" + name + "\",\"InstallLocation\":\"" + esc + "\",\"InstallSize\":5,\"AppName\":\"" + name + "\"}"};
    }
    void registry_game(const char* provider, const std::string& id, const std::string& path) {
        auto& l = registry[provider];
        l.status = SourceValue::Status::Found;
        l.items.push_back({id, id, path, "x.exe"});
        kinds[path] = PathKind::Directory;
    }
};

const ProviderResult* of(const LibraryScanResult& r, const char* name) {
    for (const auto& p : r.providers)
        if (p.provider == name) return &p;
    return nullptr;
}
LibraryScanOptions on() {
    LibraryScanOptions o;
    o.registry_providers = true;
    o.ea_registry = true;
    return o;
}
std::string read_fixture(const std::string& rel) {
    for (const char* base : {"tests/fixtures/launchers/", "../../tests/fixtures/launchers/", "../../../tests/fixtures/launchers/",
                             EF_SOURCE_ROOT "/tests/fixtures/launchers/"}) {
        std::ifstream f(std::string(base) + rel, std::ios::binary);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    EF_CHECK(false);  // eksik fixture sessizce gecmez
    return {};
}
RegistryList load_registry(const std::string& rel) {
    namespace json = ef::core::json;
    RegistryList out;
    const auto parsed = json::parse(read_fixture(rel));
    EF_CHECK(parsed.ok());
    if (!parsed.ok()) return out;
    const auto& o = parsed.value();
    const auto s = [](const json::Value& v, const char* k) {
        const auto* x = v.find(k);
        return x && x->type() == json::Value::Type::String ? x->as_string() : std::string();
    };
    const std::string st = s(o, "status");
    out.status = st == "found"    ? SourceValue::Status::Found
                 : st == "missing" ? SourceValue::Status::Missing
                 : st == "denied"  ? SourceValue::Status::Denied
                                   : SourceValue::Status::Error;
    if (const auto* items = o.find("items"))
        for (const auto& it : items->items()) out.items.push_back({s(it, "id"), s(it, "name"), s(it, "path"), s(it, "exe")});
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------- EA

EF_TEST(b3_ea_stays_unsupported_by_default_and_registry_is_not_even_asked) {
    static_assert(!kEaRegistryValidated, "EA kayit defteri gercek cihazda dogrulanana kadar kapali kalir");
    Fs fs;
    fs.registry["ea"] = load_registry("registry/ea_valid.json");
    fs.kinds["C:\\Synthetic\\EA\\Game"] = PathKind::Directory;
    const auto r = scan_library(fs);
    EF_CHECK(of(r, "ea")->status == ProviderStatus::Unsupported && r.games.empty() && r.state == LibraryScanState::Empty && r.trusted());
    for (const auto& p : fs.registry_asked) EF_CHECK(p != "ea");
    EF_CHECK(fs.reads.empty() && fs.lists.empty());
    EF_CHECK(provider_status_key(ProviderStatus::Unsupported) == "games.source.unsupported");
}

EF_TEST(b3_ea_enabled_reads_only_install_path_and_leaves_unverified_fields_unsupported) {
    Fs fs;
    fs.registry["ea"] = load_registry("registry/ea_valid.json");
    fs.kinds["C:\\Synthetic\\EA\\Game"] = PathKind::Directory;
    const auto r = scan_library(fs, on());
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == 1);
    EF_CHECK(of(r, "ea")->status == ProviderStatus::Found && of(r, "ea")->games == 1);
    if (r.games.size() != 1) return;
    const auto& g = r.games[0];
    EF_CHECK(g.launcher == "ea" && g.app_id == "Synthetic EA Game" && g.install_dir == "C:\\Synthetic\\EA\\Game");
    // Kayit defteri exe verse bile EA icin exe/surum/boyut DOGRULANMADI: bos ("Unsupported"), uydurulmaz.
    EF_CHECK(g.exe_hint.empty() && g.version.empty() && g.size_bytes == 0 && g.manual_exe.empty());
    EF_CHECK(!kEaUnsupportedFields.empty());
    // Hicbir dosya okunmaz, hicbir klasor listelenmez (belgelenmemis/sifreli manifest dahil).
    EF_CHECK(fs.reads.empty() && fs.lists.empty());
    EF_CHECK(game_folder_trusted(g) && !game_watch_key(g).empty());
}

EF_TEST(b3_ea_bad_fixtures_fail_closed) {
    {
        Fs fs;
        fs.registry["ea"] = load_registry("registry/ea_corrupt.json");
        const auto r = scan_library(fs, on());
        EF_CHECK(r.games.empty() && r.malformed_manifests == 1 && r.state == LibraryScanState::Partial && !r.trusted());
    }
    {  // numaralandirma yarida kesildi
        Fs fs;
        fs.registry["ea"] = load_registry("registry/ea_truncated_enumeration.json");
        fs.kinds["C:\\Synthetic\\EA\\Game"] = PathKind::Directory;
        const auto r = scan_library(fs, on());
        EF_CHECK(r.games.empty() && r.state == LibraryScanState::Failed && !r.trusted() && of(r, "ea")->status == ProviderStatus::Error);
    }
    {
        Fs fs;
        fs.registry["ea"] = load_registry("registry/ea_denied.json");
        const auto r = scan_library(fs, on());
        EF_CHECK(of(r, "ea")->status == ProviderStatus::Denied && !r.trusted());
    }
    {  // kurulu degil: hata degil, guvenilir "yok"
        Fs fs;
        fs.registry["ea"] = load_registry("registry/ea_missing.json");
        const auto r = scan_library(fs, on());
        EF_CHECK(of(r, "ea")->status == ProviderStatus::Missing && r.trusted() && r.state == LibraryScanState::Empty);
    }
    {  // klasoru okunamayan / silinmis kayit
        Fs fs;
        fs.registry["ea"] = load_registry("registry/ea_valid.json");
        fs.kinds["C:\\Synthetic\\EA\\Game"] = PathKind::Denied;
        EF_CHECK(!scan_library(fs, on()).trusted());
        fs.kinds["C:\\Synthetic\\EA\\Game"] = PathKind::Missing;
        EF_CHECK(scan_library(fs, on()).trusted());
    }
}

EF_TEST(b3_ea_real_registry_read_is_read_only_and_well_formed) {
    // Gercek kayit defteri, yalniz okuma. Bu testin sonucu EA'nin gercek cihazda dogrulandigi anlamina GELMEZ
    // (EA kurulu degilse Missing doner); yalniz hata/bozuk bicim uretmedigini ve yazmadigini sinar.
    ef::platform::games::WinLibraryFs fs;
    const auto l = fs.registry_installs("ea");
    EF_CHECK(l.status == SourceValue::Status::Missing || l.status == SourceValue::Status::Found || l.status == SourceValue::Status::Denied ||
              l.status == SourceValue::Status::Error);
    if (l.status != SourceValue::Status::Found) EF_CHECK(l.items.empty() || l.status == SourceValue::Status::Error);
    for (const auto& it : l.items) EF_CHECK(!it.id.empty() && it.path.find('\0') == std::string::npos && (it.path.empty() || it.path.back() != '\\'));
}

// ---------------------------------------------------------------------------------------------------- Capraz kaynak tekillestirme

EF_TEST(b3_dedupe_same_install_dir_from_two_sources_is_one_row) {
    Fs fs;
    fs.steam_game("4000001", "Shared Game");  // S:\steam\steamapps\common\Shared Game
    // Ubisoft kaydi ayni klasore isaret eder (yazim farki: buyuk/kucuk harf, '/' ayraci, sondaki ayrac).
    fs.registry_game("ubisoft", "6000001", "s:/STEAM/steamapps/common/shared game/");
    fs.kinds["S:\\steam\\steamapps\\common\\Shared Game"] = PathKind::Directory;
    fs.kinds["s:/STEAM/steamapps/common/shared game/"] = PathKind::Directory;
    const auto r = scan_library(fs, on());
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == 1);
    if (r.games.size() != 1) return;
    EF_CHECK(r.games[0].launcher == "steam" && r.games[0].app_id == "4000001");  // tarama sirasinda ilk gelen (Steam) kalir
    EF_CHECK(r.duplicates_merged == 1);
    EF_CHECK(of(r, "ubisoft")->status == ProviderStatus::Found && of(r, "ubisoft")->games == 1);  // kaynak yine de gordugunu bildirir
    EF_CHECK(game_watch_key(r.games[0]) == "s:\\steam\\steamapps\\common\\shared game");
}

EF_TEST(b3_dedupe_keeps_distinct_installs_and_is_order_stable) {
    Fs fs;
    fs.steam_game("1", "A");
    fs.epic_game("EpicOne", "S:\\steam\\steamapps\\common\\A");  // Epic ayni klasoru bildirir
    fs.epic_game("EpicTwo", "E:\\Games\\B");                       // farkli klasor
    fs.registry_game("gog", "g1", "G:\\GOG\\C");
    const auto r = scan_library(fs, on());
    EF_CHECK(r.games.size() == 3 && r.duplicates_merged == 1);
    if (r.games.size() != 3) return;
    EF_CHECK(r.games[0].launcher == "steam" && r.games[1].app_id == "EpicTwo" && r.games[2].launcher == "gog");
    const auto again = scan_library(fs, on());
    EF_CHECK(library_fingerprint(again) == library_fingerprint(r));  // tekrar tarama ayni sonuc
}

EF_TEST(b3_dedupe_does_not_hide_untrusted_state) {
    Fs fs;
    fs.steam_game("1", "A");
    fs.registry_game("ubisoft", "u1", "S:\\steam\\steamapps\\common\\A");
    fs.registry_game("gog", "g1", "G:\\GOG\\X");
    fs.registry["gog"].items.push_back({"", "bozuk", "G:\\GOG\\Y", ""});  // bozuk kayit hala sayilir
    const auto r = scan_library(fs, on());
    EF_CHECK(r.games.size() == 2 && r.duplicates_merged == 1 && r.malformed_manifests == 1);
    EF_CHECK(r.state == LibraryScanState::Partial && !r.trusted());
}

EF_TEST(b3_dedupe_manual_exe_attaches_to_the_merged_row) {
    Fs fs;
    fs.steam_game("1", "A");
    fs.registry_game("ubisoft", "u1", "S:\\steam\\steamapps\\common\\A");
    const std::string exe = "S:\\steam\\steamapps\\common\\A\\a.exe";
    fs.kinds[exe] = PathKind::File;
    auto opts = on();
    opts.manual_exes = {exe};
    const auto r = scan_library(fs, opts);
    EF_CHECK(r.games.size() == 1 && r.games[0].launcher == "steam" && r.games[0].manual_exe == exe);
}

EF_TEST(b3_dedupe_never_merges_manual_rows_with_launcher_rows_or_each_other) {
    Fs fs;
    fs.steam_game("1", "A");
    auto opts = on();
    const std::string m1 = "D:\\Tools\\one.exe", m2 = "D:\\Tools\\two.exe";  // ortak klasor: iki ayri elle eklenen oyun
    fs.kinds[m1] = PathKind::File;
    fs.kinds[m2] = PathKind::File;
    opts.manual_exes = {m1, m2};
    const auto r = scan_library(fs, opts);
    std::size_t manual = 0;
    for (const auto& g : r.games) manual += g.launcher == "manual";
    EF_CHECK(manual == 2 && r.duplicates_merged == 0);
}
