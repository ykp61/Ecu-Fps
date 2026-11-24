// P9-B3: baslatici fixture korpusu (tests/fixtures/launchers/**). Gecerli / bozuk / kesik / asiri buyuk girdiler,
// saglayici basina fail-closed: okunamayan veya bozuk kayit asla "oyun yok" (Empty/Ok) sonucuna donusmez.
// Fixture'lar SENTETIKTIR (gercek launcher ornegi degildir); gercek cihaz dogrulamasi docs/19 ekindeki tabloda ayridir.
// Sahte olan yalniz dosya sistemi/kayit defteridir; ayristiricilar ve tarama yolu urunun gercek kodudur.
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

#include "core/games/library_scan.h"
#include "core/serialization/json.h"
#include "ef_test.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/win_env.h"

using namespace ef::core::games;
namespace json = ef::core::json;
namespace sf = std::filesystem;

namespace {

std::optional<std::string> read_fixture(const std::string& rel) {
    for (const char* base : {"tests/fixtures/launchers/", "../../tests/fixtures/launchers/", "../../../tests/fixtures/launchers/",
                             EF_SOURCE_ROOT "/tests/fixtures/launchers/"}) {
        std::ifstream f(std::string(base) + rel, std::ios::binary);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    return std::nullopt;
}
std::string fx(const std::string& rel) {
    const auto t = read_fixture(rel);
    EF_CHECK(t.has_value());  // eksik fixture sessizce gecmez
    return t.value_or(std::string());
}
FileRead ok(std::string s) { return {FileRead::Status::Ok, std::move(s)}; }
FileRead status_only(FileRead::Status s) { return {s, {}}; }

constexpr const char* kEpicDir = "C:\\Synthetic\\ProgramData\\Epic\\Manifests";
constexpr const char* kRiotMd = "C:\\Synthetic\\ProgramData\\Riot Games\\Metadata";
constexpr const char* kXboxPath = "C:\\Synthetic\\XboxGames\\Game\\Content";
constexpr const char* kXboxFamily = "Synthetic.XboxGame_8wekyb3d8bbwe";

class CorpusFs final : public LibraryFs {
public:
    SourceValue steam{SourceValue::Status::Missing, {}};
    SourceValue epic{SourceValue::Status::Missing, {}};
    SourceValue riot{SourceValue::Status::Missing, {}};
    std::map<std::string, PathKind> kinds;
    std::map<std::string, DirList> dirs;
    std::map<std::string, FileRead> files;
    std::map<std::string, RegistryList> registry;  // saglayici -> kayit defteri anlik goruntusu
    PackageList packages;                          // varsayilan: API yok
    std::map<std::string, std::string> display_names;
    std::vector<std::string> reads, lists, registry_asked;

    SourceValue steam_root() override { return steam; }
    SourceValue epic_manifest_dir() override { return epic; }
    SourceValue riot_metadata_dir() override { return riot; }
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
    RegistryList registry_installs(std::string_view provider) override {
        registry_asked.emplace_back(provider);
        const auto it = registry.find(std::string(provider));
        return it == registry.end() ? RegistryList{} : it->second;
    }
    PackageList user_packages() override { return packages; }
    std::string package_display_name(const std::string& family) override {
        const auto it = display_names.find(family);
        return it == display_names.end() ? std::string() : it->second;
    }

    void add_epic(const std::string& name, FileRead r) {
        epic = {SourceValue::Status::Found, kEpicDir};
        kinds[kEpicDir] = PathKind::Directory;
        auto& d = dirs[kEpicDir];
        d.status = DirList::Status::Ok;
        d.names.push_back(name);
        files[std::string(kEpicDir) + "\\" + name] = std::move(r);
    }
    // product: "valorant.live" | "league_of_legends.live"; exe_path: kurulum kokunde beklenen exe (varsa File)
    void add_riot(const std::string& product, FileRead yaml, const std::string& exe_path = {}) {
        riot = {SourceValue::Status::Found, kRiotMd};
        kinds[kRiotMd] = PathKind::Directory;
        files[std::string(kRiotMd) + "\\" + product + "\\" + product + ".product_settings.yaml"] = std::move(yaml);
        if (!exe_path.empty()) kinds[exe_path] = PathKind::File;
    }
    void add_xbox(FileRead cfg, PathKind cfg_kind = PathKind::File) {
        packages.status = PackageList::Status::Ok;
        packages.items.push_back({kXboxFamily, kXboxPath, false});
        display_names[kXboxFamily] = "Synthetic Xbox Game";
        const std::string p = std::string(kXboxPath) + "\\MicrosoftGame.config";
        kinds[p] = cfg_kind;
        files[p] = std::move(cfg);
    }
    void add_steam_lib(const std::string& root) {
        kinds[root + "\\steamapps"] = PathKind::Directory;
        dirs[root + "\\steamapps"] = DirList{DirList::Status::Ok, {}};
    }
    void add_steam_manifest(const std::string& root, const std::string& file, FileRead r) {
        dirs[root + "\\steamapps"].names.push_back(file);
        files[root + "\\steamapps\\" + file] = std::move(r);
    }
};

const ProviderResult* of(const LibraryScanResult& r, const char* name) {
    for (const auto& p : r.providers)
        if (p.provider == name) return &p;
    return nullptr;
}
bool has_issue(const LibraryScanResult& r, LibraryIssueKind k, const char* launcher) {
    for (const auto& i : r.issues)
        if (i.kind == k && i.launcher == launcher) return true;
    return false;
}
LibraryScanOptions all_on() {
    LibraryScanOptions o;
    o.registry_providers = true;
    o.xbox_packages = true;
    o.ea_registry = true;
    return o;
}

SourceValue::Status status_from(const std::string& s) {
    if (s == "found") return SourceValue::Status::Found;
    if (s == "missing") return SourceValue::Status::Missing;
    if (s == "denied") return SourceValue::Status::Denied;
    return SourceValue::Status::Error;
}
std::string str(const json::Value& o, const char* k) {
    const auto* v = o.find(k);
    return v && v->type() == json::Value::Type::String ? v->as_string() : std::string();
}
// Kayit defteri anlik goruntusu: {"status": ..., "items": [{id,name,path,exe}]}
RegistryList load_registry(const std::string& rel) {
    RegistryList out;
    const auto parsed = json::parse(fx(rel));
    EF_CHECK(parsed.ok());
    if (!parsed.ok()) return out;
    const auto& o = parsed.value();
    out.status = status_from(str(o, "status"));
    if (const auto* items = o.find("items"))
        for (const auto& it : items->items()) out.items.push_back({str(it, "id"), str(it, "name"), str(it, "path"), str(it, "exe")});
    return out;
}
// Battle.net: Uninstall anahtarlarinin ham girisleri.
std::vector<UninstallEntry> load_uninstall_entries(const std::string& rel, bool* enumeration_error = nullptr) {
    std::vector<UninstallEntry> out;
    const auto parsed = json::parse(fx(rel));
    EF_CHECK(parsed.ok());
    if (!parsed.ok()) return out;
    const auto& o = parsed.value();
    if (enumeration_error) {
        const auto* e = o.find("enumeration_error");
        *enumeration_error = e && e->type() == json::Value::Type::Bool && e->as_bool();
    }
    if (const auto* entries = o.find("entries"))
        for (const auto& e : entries->items())
            out.push_back({str(e, "key"), str(e, "display_name"), str(e, "publisher"), str(e, "uninstall_string"), str(e, "install_location")});
    return out;
}
// Kayit defteri kayitlari icin yollar gercekten var (klasor) kabul edilir.
void dirs_exist(CorpusFs& fs, const RegistryList& l) {
    for (const auto& it : l.items)
        if (!it.path.empty()) fs.kinds[it.path] = PathKind::Directory;
}

// Gercek Windows dosya sistemi (gecici klasor): asiri buyuk dosya sinirini gercek read_file uygular.
struct TempEpic {
    sf::path root;
    TempEpic() {
        root = sf::temp_directory_path() / (L"ef_b3_corpus_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
        sf::create_directories(root / L"epic");
    }
    ~TempEpic() {
        std::error_code ec;
        sf::remove_all(root, ec);
    }
    void write(const std::wstring& name, const std::string& data) const {
        std::ofstream f(root / L"epic" / name, std::ios::binary);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    LibraryScanResult scan() const {
        ef::platform::games::WinLibraryFs fs;
        fs.set_overrides({}, ef::platform::to_utf8((root / L"epic").wstring()));
        return scan_library(fs);
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------- Epic (.item)

EF_TEST(b3_epic_valid_fixtures_are_games) {
    CorpusFs fs;
    fs.add_epic("a.item", ok(fx("epic/valid_game.item")));
    fs.add_epic("b.item", ok(fx("epic/valid_minimal.item")));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == 2);
    EF_CHECK(r.skipped_manifests == 0 && r.source_errors == 0 && r.issues.empty());
    EF_CHECK(of(r, "epic")->status == ProviderStatus::Found && of(r, "epic")->games == 2);
    EF_CHECK(r.games.size() == 2);
    if (r.games.size() != 2) return;
    const auto& g = r.games[0];
    EF_CHECK(g.launcher == "epic" && g.name == "Synthetic Epic Game" && g.app_id == "SynthEpicApp001" && g.version == "1.2.3");
    EF_CHECK(g.install_dir == "C:\\Synthetic\\Epic\\SyntheticEpicGame" && g.size_bytes == 12345678);
    EF_CHECK(g.exe_hint == "Game\\Binaries\\Win64\\SyntheticEpicGame.exe");
    const auto& m = r.games[1];  // yalniz zorunlu alanlar: opsiyoneller bos = bilinmiyor (uydurulmaz)
    EF_CHECK(m.name == "Synthetic Minimal" && m.app_id.empty() && m.version.empty() && m.exe_hint.empty() && m.size_bytes == 0);
}

EF_TEST(b3_epic_bad_fixtures_fail_closed_never_empty) {
    for (const char* name : {"corrupt_not_json", "corrupt_missing_install_location", "corrupt_wrong_type_size", "corrupt_negative_size",
                             "corrupt_top_level_array", "truncated_mid_json", "truncated_mid_string", "empty"}) {
        CorpusFs fs;
        fs.add_epic("x.item", ok(fx(std::string("epic/") + name + ".item")));
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.malformed_manifests == 1 && r.skipped_manifests == 1);
        EF_CHECK(r.state == LibraryScanState::Partial && !r.trusted());  // "oyun yok" (Empty) DENMEZ
        EF_CHECK(has_issue(r, LibraryIssueKind::ManifestCorrupt, "epic") && of(r, "epic")->status == ProviderStatus::Partial);
        EF_CHECK(!watch_bootstrap(r).no_games);
    }
}

EF_TEST(b3_epic_bad_fixture_does_not_hide_good_one) {
    CorpusFs fs;
    fs.add_epic("good.item", ok(fx("epic/valid_game.item")));
    fs.add_epic("trunc.item", ok(fx("epic/truncated_mid_json.item")));
    const auto r = scan_library(fs);
    EF_CHECK(r.games.size() == 1 && r.malformed_manifests == 1 && r.state == LibraryScanState::Partial && !r.trusted());
}

EF_TEST(b3_epic_read_failures_are_unreadable_not_absent) {
    for (const auto st : {FileRead::Status::Denied, FileRead::Status::TooLarge, FileRead::Status::Truncated, FileRead::Status::Error}) {
        CorpusFs fs;
        fs.add_epic("x.item", status_only(st));
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.unreadable_manifests == 1 && r.malformed_manifests == 0);
        EF_CHECK(r.state == LibraryScanState::Partial && !r.trusted());
        EF_CHECK(has_issue(r, LibraryIssueKind::ManifestUnreadable, "epic"));
    }
    CorpusFs gone;  // liste ile okuma arasinda silindi: oyun artik yok (hata degil)
    gone.add_epic("x.item", status_only(FileRead::Status::Missing));
    const auto r = scan_library(gone);
    EF_CHECK(r.state == LibraryScanState::Empty && r.trusted() && r.skipped_manifests == 0);
}

EF_TEST(b3_epic_oversized_manifest_real_fs_is_rejected_by_size_cap) {
    TempEpic t;
    t.write(L"good.item", fx("epic/valid_game.item"));
    // Gecerli bildirim + >1 MiB bosluk: yalniz boyut siniri reddeder (ayristirici kabul ederdi).
    std::string huge = fx("epic/valid_game.item");
    huge.append((1u << 20) + 1, ' ');
    t.write(L"huge.item", huge);
    const auto r = t.scan();
    const auto* epic = of(r, "epic");
    EF_CHECK(epic != nullptr && epic->status == ProviderStatus::Partial);
    std::size_t epic_games = 0;
    for (const auto& g : r.games) epic_games += g.launcher == "epic";
    EF_CHECK(epic_games == 1 && r.unreadable_manifests == 1 && r.malformed_manifests == 0);
    EF_CHECK(has_issue(r, LibraryIssueKind::ManifestUnreadable, "epic") && !r.trusted());
}

EF_TEST(b3_epic_oversized_manifest_count_is_bounded_and_untrusted) {
    CorpusFs fs;
    fs.epic = {SourceValue::Status::Found, kEpicDir};
    fs.kinds[kEpicDir] = PathKind::Directory;
    auto& d = fs.dirs[kEpicDir];
    d.status = DirList::Status::Ok;
    const std::string text = fx("epic/valid_minimal.item");
    for (int i = 0; i < 4001; ++i) {
        const std::string name = "n" + std::to_string(i) + ".item";
        d.names.push_back(name);
        fs.files[std::string(kEpicDir) + "\\" + name] = ok(text);
    }
    const auto r = scan_library(fs);
    EF_CHECK(has_issue(r, LibraryIssueKind::ScanLimit, "epic") && !r.trusted() && r.source_errors >= 1);
    EF_CHECK(fs.reads.size() <= 4000);  // sinir asildiktan sonra okuma yok
}

// ---------------------------------------------------------------------------------------------------- Riot (yaml)

namespace {
constexpr const char* kValRoot = "C:\\Synthetic\\Riot Games\\VALORANT\\live";
constexpr const char* kValExe = "C:\\Synthetic\\Riot Games\\VALORANT\\live\\ShooterGame\\Binaries\\Win64\\VALORANT-Win64-Shipping.exe";
constexpr const char* kLolRoot = "C:\\Synthetic\\Riot Games\\League of Legends";
constexpr const char* kLolExe = "C:\\Synthetic\\Riot Games\\League of Legends\\Game\\League of Legends.exe";
}  // namespace

EF_TEST(b3_riot_valid_fixtures_are_games) {
    CorpusFs fs;
    fs.add_riot("valorant.live", ok(fx("riot/valorant.live.valid.yaml")), kValExe);
    fs.add_riot("league_of_legends.live", ok(fx("riot/league_of_legends.live.valid.yaml")), kLolExe);
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == 2);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Found && of(r, "riot")->games == 2);
    if (r.games.size() != 2) return;
    EF_CHECK(r.games[0].app_id == "valorant.live" && r.games[0].install_dir == kValRoot);
    EF_CHECK(r.games[1].app_id == "league_of_legends.live" && r.games[1].install_dir == kLolRoot);
}

EF_TEST(b3_riot_bad_fixtures_fail_closed_never_empty) {
    for (const char* name : {"riot/corrupt_relative_path.yaml", "riot/corrupt_traversal.yaml", "riot/corrupt_no_key.yaml",
                             "riot/corrupt_unquoted.yaml", "riot/corrupt_drive_root.yaml", "riot/truncated_unclosed_quote.yaml",
                             "riot/truncated_mid_key.yaml", "riot/empty.yaml"}) {
        CorpusFs fs;
        fs.add_riot("valorant.live", ok(fx(name)), kValExe);
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.malformed_manifests == 1);
        EF_CHECK(r.state == LibraryScanState::Partial && !r.trusted());
        EF_CHECK(has_issue(r, LibraryIssueKind::ManifestCorrupt, "riot") && of(r, "riot")->status == ProviderStatus::Partial);
    }
}

EF_TEST(b3_riot_bad_product_does_not_hide_good_one) {
    CorpusFs fs;
    fs.add_riot("valorant.live", ok(fx("riot/truncated_unclosed_quote.yaml")), kValExe);
    fs.add_riot("league_of_legends.live", ok(fx("riot/league_of_legends.live.valid.yaml")), kLolExe);
    const auto r = scan_library(fs);
    EF_CHECK(r.games.size() == 1 && r.games[0].app_id == "league_of_legends.live" && r.state == LibraryScanState::Partial && !r.trusted());
}

EF_TEST(b3_riot_read_failures_and_exe_states) {
    for (const auto st : {FileRead::Status::Denied, FileRead::Status::TooLarge, FileRead::Status::Truncated, FileRead::Status::Error}) {
        CorpusFs fs;
        fs.add_riot("valorant.live", status_only(st), kValExe);
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.unreadable_manifests == 1 && !r.trusted() && r.state == LibraryScanState::Partial);
    }
    CorpusFs removed;  // yaml var, exe yok: eski kayit (kaldirilmis oyun) hata degil
    removed.add_riot("valorant.live", ok(fx("riot/valorant.live.valid.yaml")));
    auto r = scan_library(removed);
    EF_CHECK(r.state == LibraryScanState::Empty && r.trusted());
    CorpusFs odd;  // exe yolu klasor: bozuk kayit, "yok" denmez
    odd.add_riot("valorant.live", ok(fx("riot/valorant.live.valid.yaml")));
    odd.kinds[kValExe] = PathKind::Directory;
    r = scan_library(odd);
    EF_CHECK(r.games.empty() && !r.trusted() && r.unreadable_manifests == 1);
    CorpusFs denied;  // exe yoklamasi reddedildi
    denied.add_riot("valorant.live", ok(fx("riot/valorant.live.valid.yaml")));
    denied.kinds[kValExe] = PathKind::Denied;
    EF_CHECK(!scan_library(denied).trusted());
}

EF_TEST(b3_riot_metadata_source_failures_are_not_missing) {
    CorpusFs denied;
    denied.riot = {SourceValue::Status::Denied, {}};
    auto r = scan_library(denied);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Denied && !r.trusted());
    CorpusFs err;
    err.riot = {SourceValue::Status::Found, kRiotMd};
    err.kinds[kRiotMd] = PathKind::File;  // klasor degil
    r = scan_library(err);
    EF_CHECK(of(r, "riot")->status == ProviderStatus::Error && !r.trusted());
}

// ---------------------------------------------------------------------------------------------------- Xbox (MicrosoftGame.config)

EF_TEST(b3_xbox_valid_fixtures_yield_exe_hint) {
    CorpusFs fs;
    fs.add_xbox(ok(fx("xbox/valid_MicrosoftGame.config")));
    auto r = scan_library(fs, all_on());
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == 1);
    EF_CHECK(r.games[0].launcher == "xbox" && r.games[0].app_id == kXboxFamily && r.games[0].name == "Synthetic Xbox Game");
    EF_CHECK(r.games[0].install_dir == kXboxPath && r.games[0].exe_hint == "SyntheticGame.exe");
    CorpusFs fs2;
    fs2.add_xbox(ok(fx("xbox/valid_single_quote_MicrosoftGame.config")));
    r = scan_library(fs2, all_on());
    EF_CHECK(r.games.size() == 1 && r.games[0].exe_hint == "Bin\\Synth.exe");
    CorpusFs fs3;
    fs3.add_xbox(ok(fx("xbox/commented_then_real.config")));
    r = scan_library(fs3, all_on());
    EF_CHECK(r.games.size() == 1 && r.games[0].exe_hint == "Real.exe");  // yorum satirindaki Ghost.exe sayilmaz
}

EF_TEST(b3_xbox_config_comment_handling_is_fail_closed) {
    EF_CHECK(parse_microsoftgame_config_exe("<Game><Executable Name=\"A.exe\"/><!-- <Executable Name=\"B.exe\"/> --></Game>") == "A.exe");
    EF_CHECK(parse_microsoftgame_config_exe("<Game><!-- acik yorum <Executable Name=\"B.exe\"/></Game>").empty());  // kapanmayan yorum: sonrasi guvenilmez
    EF_CHECK(parse_microsoftgame_config_exe("<Game><!--a--><!--b--><Executable Name=\"C.exe\"/></Game>") == "C.exe");
    EF_CHECK(parse_microsoftgame_config_exe("<Game><!----><Executable\n Name = \"D.exe\" /></Game>") == "D.exe");
}

EF_TEST(b3_xbox_bad_config_keeps_game_but_never_invents_exe) {
    // Paket oyun (config var); config icerigi bozuk/kesik: exe ipucu bos kalir, klasor eslesmesi surer, hata degildir.
    for (const char* name : {"corrupt_no_executable", "corrupt_exe_traversal", "corrupt_exe_not_exe", "corrupt_exe_absolute", "commented_executable",
                             "truncated_mid_tag", "truncated_mid_value", "empty"}) {
        CorpusFs fs;
        fs.add_xbox(ok(fx(std::string("xbox/") + name + ".config")));
        const auto r = scan_library(fs, all_on());
        EF_CHECK(r.games.size() == 1 && r.games[0].exe_hint.empty() && r.games[0].install_dir == kXboxPath);
        EF_CHECK(r.trusted() && of(r, "xbox")->status == ProviderStatus::Found);
    }
    for (const auto st : {FileRead::Status::Denied, FileRead::Status::TooLarge, FileRead::Status::Truncated, FileRead::Status::Error}) {
        CorpusFs fs;
        fs.add_xbox(status_only(st));
        const auto r = scan_library(fs, all_on());
        EF_CHECK(r.games.size() == 1 && r.games[0].exe_hint.empty());
    }
}

EF_TEST(b3_xbox_config_unprobeable_is_unreadable_not_not_a_game) {
    for (const auto kind : {PathKind::Directory, PathKind::Denied, PathKind::Error}) {
        CorpusFs fs;
        fs.add_xbox(ok(fx("xbox/valid_MicrosoftGame.config")), kind);
        const auto r = scan_library(fs, all_on());
        EF_CHECK(r.games.empty() && r.unreadable_manifests == 1 && !r.trusted());
        EF_CHECK(of(r, "xbox")->status == ProviderStatus::Partial && has_issue(r, LibraryIssueKind::ManifestUnreadable, "xbox"));
    }
    CorpusFs app;  // MicrosoftGame.config yok = uygulama paketi (oyun degil): hata degil
    app.add_xbox(ok(fx("xbox/valid_MicrosoftGame.config")), PathKind::Missing);
    const auto r = scan_library(app, all_on());
    EF_CHECK(r.games.empty() && r.trusted() && r.state == LibraryScanState::Empty);
}

EF_TEST(b3_xbox_package_list_failures_fail_closed) {
    CorpusFs unavailable;  // bu Windows'ta API yok: desteklenmiyor (hata degil)
    auto r = scan_library(unavailable, all_on());
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Unsupported && r.trusted());
    for (const auto st : {PackageList::Status::Denied, PackageList::Status::Error}) {
        CorpusFs fs;
        fs.packages.status = st;
        r = scan_library(fs, all_on());
        EF_CHECK(!r.trusted() && (of(r, "xbox")->status == ProviderStatus::Denied || of(r, "xbox")->status == ProviderStatus::Error));
    }
    CorpusFs trunc;  // bellek siniri: liste tam degil
    trunc.add_xbox(ok(fx("xbox/valid_MicrosoftGame.config")));
    trunc.packages.status = PackageList::Status::Truncated;
    r = scan_library(trunc, all_on());
    EF_CHECK(r.games.size() == 1 && !r.trusted() && of(r, "xbox")->status == ProviderStatus::Partial);
    CorpusFs stub;  // icerigi kurulu olmayan kabuk paket oyun sayilmaz
    stub.add_xbox(ok(fx("xbox/valid_MicrosoftGame.config")));
    stub.packages.items[0].stub = true;
    r = scan_library(stub, all_on());
    EF_CHECK(r.games.empty() && r.trusted());
}

EF_TEST(b3_xbox_oversized_package_list_is_bounded_and_untrusted) {
    CorpusFs fs;
    fs.packages.status = PackageList::Status::Ok;
    for (int i = 0; i < 4001; ++i) fs.packages.items.push_back({"Fam" + std::to_string(i), "C:\\Synthetic\\Pkg" + std::to_string(i), false});
    const auto r = scan_library(fs, all_on());
    EF_CHECK(has_issue(r, LibraryIssueKind::ScanLimit, "xbox") && !r.trusted() && of(r, "xbox")->status == ProviderStatus::Partial);
}

// ---------------------------------------------------------------------------------------------------- Steam (acf + vdf)

namespace {
void steam_fs(CorpusFs& fs) {
    fs.steam = {SourceValue::Status::Found, "S:\\steam"};
    fs.add_steam_lib("S:\\steam");
}
}  // namespace

EF_TEST(b3_steam_valid_and_helper_fixtures) {
    CorpusFs fs;
    steam_fs(fs);
    fs.add_steam_manifest("S:\\steam", "appmanifest_4000001.acf", ok(fx("steam/valid_appmanifest.acf")));
    fs.add_steam_manifest("S:\\steam", "appmanifest_228980.acf", ok(fx("steam/helper_package_appmanifest.acf")));
    const auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == 1 && r.excluded_non_games == 1 && r.skipped_manifests == 0);
    EF_CHECK(r.games[0].app_id == "4000001" && r.games[0].version == "101" && r.games[0].size_bytes == 2048);
    EF_CHECK(r.games[0].install_dir == "S:\\steam\\steamapps\\common\\SyntheticSteamGame");
}

EF_TEST(b3_steam_bad_fixtures_fail_closed_never_empty) {
    for (const char* name : {"corrupt_missing_fields", "corrupt_not_vdf", "truncated_mid_block", "truncated_missing_close", "empty"}) {
        CorpusFs fs;
        steam_fs(fs);
        fs.add_steam_manifest("S:\\steam", "appmanifest_1.acf", ok(fx(std::string("steam/") + name + ".acf")));
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.malformed_manifests == 1 && r.state == LibraryScanState::Partial && !r.trusted());
        EF_CHECK(has_issue(r, LibraryIssueKind::ManifestCorrupt, "steam"));
    }
    for (const auto st : {FileRead::Status::Denied, FileRead::Status::TooLarge, FileRead::Status::Truncated, FileRead::Status::Error}) {
        CorpusFs fs;
        steam_fs(fs);
        fs.add_steam_manifest("S:\\steam", "appmanifest_1.acf", status_only(st));
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.unreadable_manifests == 1 && !r.trusted());
    }
}

EF_TEST(b3_steam_libraryfolders_fixtures) {
    CorpusFs fs;
    steam_fs(fs);
    fs.add_steam_lib("T:\\lib");
    fs.kinds["S:\\steam\\steamapps\\libraryfolders.vdf"] = PathKind::File;
    fs.files["S:\\steam\\steamapps\\libraryfolders.vdf"] = ok(fx("steam/valid_libraryfolders.vdf"));
    fs.add_steam_manifest("T:\\lib", "appmanifest_4000001.acf", ok(fx("steam/valid_appmanifest.acf")));
    auto r = scan_library(fs);
    EF_CHECK(r.state == LibraryScanState::Ok && r.games.size() == 1 && r.games[0].install_dir.rfind("T:\\lib\\", 0) == 0);
    for (const char* name : {"truncated_libraryfolders.vdf", "corrupt_libraryfolders.vdf"}) {
        fs.files["S:\\steam\\steamapps\\libraryfolders.vdf"] = ok(fx(std::string("steam/") + name));
        r = scan_library(fs);
        EF_CHECK(has_issue(r, LibraryIssueKind::LibraryList, "steam") && !r.trusted());  // kitaplik listesi eksik: "yok" denmez
    }
}

// ---------------------------------------------------------------------------------------------------- Kayit defteri kaynakli: GOG / Ubisoft

namespace {
void registry_valid_case(const char* provider, const char* file, std::size_t expected_games) {
    CorpusFs fs;
    fs.registry[provider] = load_registry(file);
    dirs_exist(fs, fs.registry[provider]);
    const auto r = scan_library(fs, all_on());
    EF_CHECK(r.state == LibraryScanState::Ok && r.trusted() && r.games.size() == expected_games);
    EF_CHECK(of(r, provider)->status == ProviderStatus::Found && of(r, provider)->games == expected_games);
    for (const auto& g : r.games) EF_CHECK(g.launcher == provider && !g.app_id.empty() && !g.install_dir.empty() && g.version.empty() && g.size_bytes == 0);
}
void registry_bad_cases(const char* provider, const char* corrupt, const char* truncated, const char* denied, std::size_t corrupt_items) {
    {  // bozuk kayitlar: sayilir, guvenilmez, asla Empty
        CorpusFs fs;
        fs.registry[provider] = load_registry(corrupt);
        dirs_exist(fs, fs.registry[provider]);
        const auto r = scan_library(fs, all_on());
        EF_CHECK(r.games.empty() && r.malformed_manifests == corrupt_items && r.state == LibraryScanState::Partial && !r.trusted());
    }
    {  // numaralandirma yarida kesildi: o ana kadar okunan kayit LISTEYE ALINMAZ, "yok" denmez
        CorpusFs fs;
        fs.registry[provider] = load_registry(truncated);
        dirs_exist(fs, fs.registry[provider]);
        const auto r = scan_library(fs, all_on());
        EF_CHECK(r.games.empty() && r.state == LibraryScanState::Failed && !r.trusted() && r.source_errors == 1);
        EF_CHECK(of(r, provider)->status == ProviderStatus::Error && has_issue(r, LibraryIssueKind::ProviderSource, provider));
    }
    {
        CorpusFs fs;
        fs.registry[provider] = load_registry(denied);
        const auto r = scan_library(fs, all_on());
        EF_CHECK(of(r, provider)->status == ProviderStatus::Denied && !r.trusted() && r.games.empty());
    }
    {  // klasoru okunamayan kayit: yok sayilmaz; klasoru silinmis kayit: eski kayit (hata degil)
        CorpusFs fs;
        fs.registry[provider] = load_registry(corrupt);
        fs.registry[provider].items = {{"1", "x", "C:\\Synthetic\\Gone", ""}};
        fs.kinds["C:\\Synthetic\\Gone"] = PathKind::Denied;
        EF_CHECK(!scan_library(fs, all_on()).trusted());
        fs.kinds["C:\\Synthetic\\Gone"] = PathKind::Missing;
        EF_CHECK(scan_library(fs, all_on()).trusted());
    }
    {  // asiri buyuk liste: 4000 ile sinirli, guvenilmez
        CorpusFs fs;
        RegistryList big;
        big.status = SourceValue::Status::Found;
        for (int i = 0; i < 4001; ++i) {
            const std::string p = "C:\\Synthetic\\Big\\" + std::to_string(i);
            big.items.push_back({"id" + std::to_string(i), "n", p, ""});
            fs.kinds[p] = PathKind::Directory;
        }
        fs.registry[provider] = big;
        const auto r = scan_library(fs, all_on());
        EF_CHECK(r.games.size() == 4000 && !r.trusted() && has_issue(r, LibraryIssueKind::ScanLimit, provider));
    }
}
}  // namespace

EF_TEST(b3_gog_registry_corpus) {
    registry_valid_case("gog", "registry/gog_valid.json", 2);
    registry_bad_cases("gog", "registry/gog_corrupt.json", "registry/gog_truncated_enumeration.json", "registry/gog_denied.json", 2);
    CorpusFs fs;
    fs.registry["gog"] = load_registry("registry/gog_valid.json");
    dirs_exist(fs, fs.registry["gog"]);
    const auto r = scan_library(fs, all_on());
    EF_CHECK(!r.games.empty() && r.games[0].exe_hint == "Game.exe" && r.games[0].name == "Synthetic GOG Game");  // GOG kaydi exe verir
    CorpusFs none;
    none.registry["gog"] = load_registry("registry/gog_missing.json");
    EF_CHECK(scan_library(none, all_on()).state == LibraryScanState::Empty);
}

EF_TEST(b3_ubisoft_registry_corpus) {
    registry_valid_case("ubisoft", "registry/ubisoft_valid.json", 1);
    registry_bad_cases("ubisoft", "registry/ubisoft_corrupt.json", "registry/ubisoft_truncated_enumeration.json", "registry/ubisoft_denied.json", 1);
    CorpusFs fs;
    fs.registry["ubisoft"] = load_registry("registry/ubisoft_valid.json");
    dirs_exist(fs, fs.registry["ubisoft"]);
    const auto r = scan_library(fs, all_on());
    EF_CHECK(!r.games.empty() && r.games[0].name == r.games[0].app_id && r.games[0].exe_hint.empty());  // ad yoksa kimlik gosterilir; exe uydurulmaz
}

EF_TEST(b3_registry_providers_stay_disabled_by_default) {
    CorpusFs fs;
    fs.registry["gog"] = load_registry("registry/gog_valid.json");
    fs.registry["ubisoft"] = load_registry("registry/ubisoft_valid.json");
    dirs_exist(fs, fs.registry["gog"]);
    const auto r = scan_library(fs);  // varsayilan secenekler: gercek cihazda DOGRULANMADI -> kapali
    EF_CHECK(of(r, "gog")->status == ProviderStatus::Disabled && of(r, "ubisoft")->status == ProviderStatus::Disabled && r.games.empty());
    for (const auto& p : fs.registry_asked) EF_CHECK(p == "battlenet");  // kapali saglayicinin kaydi hic sorulmaz
}

// ---------------------------------------------------------------------------------------------------- Battle.net (Uninstall girisleri)

EF_TEST(b3_battlenet_uninstall_filter_and_corpus) {
    auto entries = load_uninstall_entries("registry/battlenet_valid.json");
    auto list = battlenet_list_from_uninstall_entries(entries);
    EF_CHECK(list.status == SourceValue::Status::Found && list.items.size() == 1);
    EF_CHECK(!list.items.empty() && list.items[0].id == "Synthetic_Blizzard_A" && list.items[0].path == "C:\\Synthetic\\Blizzard\\GameA" && list.items[0].exe.empty());
    // Launcher kendisi, baska yayinci, baska kaldirici ve tekrar elenir: tek oyun kalir.
    list = battlenet_list_from_uninstall_entries(load_uninstall_entries("registry/battlenet_filtered_out.json"));
    EF_CHECK(list.status == SourceValue::Status::Found && list.items.size() == 1 && !list.items.empty() && list.items[0].id == "Synthetic_Blizzard_A");
    EF_CHECK(battlenet_list_from_uninstall_entries({}).status == SourceValue::Status::Missing);  // hic kayit: kurulu degil
}

EF_TEST(b3_battlenet_scan_fail_closed) {
    {
        CorpusFs fs;  // gecerli
        fs.registry["battlenet"] = battlenet_list_from_uninstall_entries(load_uninstall_entries("registry/battlenet_valid.json"));
        dirs_exist(fs, fs.registry["battlenet"]);
        const auto r = scan_library(fs);
        EF_CHECK(r.state == LibraryScanState::Ok && r.games.size() == 1 && r.games[0].launcher == "battlenet");
        EF_CHECK(of(r, "battlenet")->status == ProviderStatus::Found);
    }
    {  // yolu olmayan giris: bozuk sayilir, "temiz" denmez
        CorpusFs fs;
        fs.registry["battlenet"] = battlenet_list_from_uninstall_entries(load_uninstall_entries("registry/battlenet_corrupt.json"));
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.malformed_manifests == 1 && !r.trusted() && r.state == LibraryScanState::Partial);
    }
    {  // numaralandirma kesildi: Error + yarim liste
        bool enum_err = false;
        CorpusFs fs;
        auto l = battlenet_list_from_uninstall_entries(load_uninstall_entries("registry/battlenet_truncated_enumeration.json", &enum_err));
        EF_CHECK(enum_err);
        l.status = SourceValue::Status::Error;
        fs.registry["battlenet"] = l;
        dirs_exist(fs, l);
        const auto r = scan_library(fs);
        EF_CHECK(r.games.empty() && r.state == LibraryScanState::Failed && !r.trusted() && of(r, "battlenet")->status == ProviderStatus::Error);
    }
    {
        CorpusFs fs;
        fs.registry["battlenet"].status = SourceValue::Status::Denied;
        const auto r = scan_library(fs);
        EF_CHECK(of(r, "battlenet")->status == ProviderStatus::Denied && !r.trusted());
    }
}
