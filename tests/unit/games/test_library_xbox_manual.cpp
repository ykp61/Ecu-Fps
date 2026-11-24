// EF-X11: Xbox paket envanteri ve elle EXE ekleme (T01/T02 fikstur matrisi). Gercek cihaz ornegi DEGILDIR.
#include <map>
#include <string>
#include <vector>

#include "core/games/library_scan.h"
#include "core/games/manual_games.h"
#include "core/games/watcher.h"
#include "core/settings/preferences.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {
struct PkgFs final : LibraryFs {
    PackageList pkgs;
    std::map<std::string, PathKind> kinds;     // tam yol -> tur (yoksa Missing)
    std::map<std::string, FileRead> files;     // tam yol -> okuma sonucu
    std::map<std::string, std::string> names;  // family -> gorunen ad
    SourceValue steam_root() override { return {SourceValue::Status::Missing, {}}; }
    SourceValue epic_manifest_dir() override { return {SourceValue::Status::Missing, {}}; }
    PathKind probe(const std::string& p) override {
        const auto it = kinds.find(p);
        return it == kinds.end() ? PathKind::Missing : it->second;
    }
    DirList list(const std::string&) override { return {DirList::Status::Missing, {}}; }
    FileRead read_file(const std::string& p, std::size_t) override {
        const auto it = files.find(p);
        return it == files.end() ? FileRead{FileRead::Status::Missing, {}} : it->second;
    }
    PackageList user_packages() override { return pkgs; }
    std::string package_display_name(const std::string& family) override {
        const auto it = names.find(family);
        return it == names.end() ? std::string() : it->second;
    }
};
const ProviderResult* of(const LibraryScanResult& r, const char* name) {
    for (const auto& p : r.providers)
        if (p.provider == name) return &p;
    return nullptr;
}
LibraryScanOptions xbox_on() {
    LibraryScanOptions o;
    o.xbox_packages = true;
    return o;
}
constexpr char kCfgPath[] = "C:\\XboxGames\\Racing\\Content\\MicrosoftGame.config";
constexpr char kConfig[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Game configVersion=\"1\">\n"
    "  <Identity Name=\"Contoso.Racing\" Publisher=\"CN=Contoso\" Version=\"1.0.0.0\"/>\n"
    "  <ExecutableList>\n    <Executable Name=\"bin\\Racing.exe\" Id=\"Game\" />\n  </ExecutableList>\n"
    "  <ShellVisuals DefaultDisplayName=\"Contoso Racing\" />\n</Game>\n";
PkgFs game_fs() {
    PkgFs f;
    f.pkgs.status = PackageList::Status::Ok;
    f.pkgs.items = {{"Contoso.Racing_8wekyb3d8bbwe", "C:\\XboxGames\\Racing\\Content", false},
                    {"Contoso.Notes_8wekyb3d8bbwe", "C:\\Program Files\\WindowsApps\\Contoso.Notes", false}};
    f.kinds[kCfgPath] = PathKind::File;
    f.files[kCfgPath] = {FileRead::Status::Ok, kConfig};
    f.names["Contoso.Racing_8wekyb3d8bbwe"] = "Contoso Racing";
    return f;
}
}  // namespace

EF_TEST(x11_xbox_default_follows_validation_flag) {
    auto f = game_fs();
    const auto r = scan_library(f);
    if constexpr (kXboxPackagesValidated) {
        EF_CHECK(of(r, "xbox")->status == ProviderStatus::Found && r.games.size() == 1);
    } else {
        EF_CHECK(of(r, "xbox")->status == ProviderStatus::Disabled && r.games.empty() && r.state == LibraryScanState::Empty);
    }
}

EF_TEST(x11_t01_xbox_valid_game_with_exe_hint_and_name) {
    auto f = game_fs();
    const auto r = scan_library(f, xbox_on());
    EF_CHECK(r.games.size() == 1 && r.trusted());
    EF_CHECK(r.games[0].launcher == "xbox" && r.games[0].app_id == "Contoso.Racing_8wekyb3d8bbwe");
    EF_CHECK(r.games[0].install_dir == "C:\\XboxGames\\Racing\\Content" && r.games[0].exe_hint == "bin\\Racing.exe");
    EF_CHECK(r.games[0].name == "Contoso Racing");
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Found && of(r, "xbox")->games == 1);
}

EF_TEST(x11_t02_xbox_non_game_packages_are_not_partial) {
    auto f = game_fs();
    f.kinds.clear();  // hicbir pakette MicrosoftGame.config yok: uygulama paketleri hata degildir
    const auto r = scan_library(f, xbox_on());
    EF_CHECK(r.games.empty() && r.state == LibraryScanState::Empty && r.source_errors == 0 && r.skipped_manifests == 0);
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Missing);
}

EF_TEST(x11_t01_xbox_missing_denied_error_unavailable) {
    PkgFs f;
    f.pkgs.status = PackageList::Status::Unavailable;  // API yok: desteklenmiyor, hata degil
    auto r = scan_library(f, xbox_on());
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Unsupported && r.trusted());
    f.pkgs.status = PackageList::Status::Ok;  // paket yok
    r = scan_library(f, xbox_on());
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Missing && r.trusted());
    f.pkgs.status = PackageList::Status::Denied;
    r = scan_library(f, xbox_on());
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Denied && !r.trusted() && r.state == LibraryScanState::Failed);
    f.pkgs.status = PackageList::Status::Error;
    r = scan_library(f, xbox_on());
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Error && !r.trusted());
}

EF_TEST(x11_t01_xbox_unreadable_config_partial_oversize_and_corrupt_keep_game) {
    auto f = game_fs();
    f.kinds[kCfgPath] = PathKind::Denied;  // oyun mu bilinemez: "yok" denmez
    auto r = scan_library(f, xbox_on());
    EF_CHECK(r.games.empty() && r.state == LibraryScanState::Partial && r.unreadable_manifests == 1);
    EF_CHECK(of(r, "xbox")->status == ProviderStatus::Partial);
    f = game_fs();
    f.files[kCfgPath] = {FileRead::Status::TooLarge, {}};  // oversize: oyun, ipucu yok
    r = scan_library(f, xbox_on());
    EF_CHECK(r.games.size() == 1 && r.games[0].exe_hint.empty() && r.trusted());
    f.files[kCfgPath] = {FileRead::Status::Ok, "<Game><Executable"};  // bozuk
    r = scan_library(f, xbox_on());
    EF_CHECK(r.games.size() == 1 && r.games[0].exe_hint.empty() && r.trusted());
}

EF_TEST(x11_xbox_stub_truncated_and_limit) {
    auto f = game_fs();
    f.pkgs.items[0].stub = true;  // icerik kurulu degil
    auto r = scan_library(f, xbox_on());
    EF_CHECK(r.games.empty() && r.trusted());
    f = game_fs();
    f.pkgs.status = PackageList::Status::Truncated;
    r = scan_library(f, xbox_on());
    EF_CHECK(r.games.size() == 1 && !r.trusted() && of(r, "xbox")->status == ProviderStatus::Partial);
    f = game_fs();
    f.pkgs.items.assign(4100, {"X_1", "C:\\none", false});
    r = scan_library(f, xbox_on());
    EF_CHECK(!r.trusted() && r.source_errors == 1);
}

EF_TEST(x11_xbox_name_falls_back_to_family_not_resource_id) {
    auto f = game_fs();
    f.names["Contoso.Racing_8wekyb3d8bbwe"] = "ms-resource:AppName";
    const auto r = scan_library(f, xbox_on());
    EF_CHECK(r.games.size() == 1 && r.games[0].name == "Contoso.Racing_8wekyb3d8bbwe");
}

EF_TEST(x11_microsoftgame_config_parser) {
    EF_CHECK(parse_microsoftgame_config_exe(kConfig) == "bin\\Racing.exe");
    EF_CHECK(parse_microsoftgame_config_exe("<ExecutableList><Executable Id='G' Name='game.EXE'/></ExecutableList>") == "game.EXE");
    EF_CHECK(parse_microsoftgame_config_exe("<Executable Id=\"G\" OverrideDisplayName=\"x.exe\" />").empty());
    EF_CHECK(parse_microsoftgame_config_exe("<Executable Name=\"readme.txt\" />").empty());
    EF_CHECK(parse_microsoftgame_config_exe("<Executable Name=\"..\\evil.exe\" />").empty());
    EF_CHECK(parse_microsoftgame_config_exe("<Executable Name=\"C:\\x.exe\" />").empty());
    EF_CHECK(parse_microsoftgame_config_exe("<Executable Name=\"a.exe").empty());  // kesik
    EF_CHECK(parse_microsoftgame_config_exe("").empty());
}

EF_TEST(x11_manual_exe_validation) {
    ManualExeContext ctx;
    ctx.windows_dir = "C:\\Windows";
    ctx.exact_roots = {"C:\\Program Files", "C:\\Users\\u"};
    const std::vector<std::string> none;
    EF_CHECK(check_manual_exe("D:\\Games\\Racer\\racer.exe", ctx, none) == ManualExeCheck::Ok);
    EF_CHECK(check_manual_exe("C:\\Program Files\\Racer\\Racer.EXE", ctx, none) == ManualExeCheck::Ok);
    EF_CHECK(check_manual_exe("", ctx, none) == ManualExeCheck::Empty);
    EF_CHECK(check_manual_exe("racer.exe", ctx, none) == ManualExeCheck::NotAbsolute);
    EF_CHECK(check_manual_exe("\\\\server\\share\\g.exe", ctx, none) == ManualExeCheck::Network);
    EF_CHECK(check_manual_exe("D:\\Games\\..\\x.exe", ctx, none) == ManualExeCheck::ParentComponent);
    EF_CHECK(check_manual_exe("D:\\Games\\racer.bat", ctx, none) == ManualExeCheck::NotExe);
    EF_CHECK(check_manual_exe("D:\\Games\\.exe", ctx, none) == ManualExeCheck::NotExe);
    EF_CHECK(check_manual_exe("D:\\racer.exe", ctx, none) == ManualExeCheck::DriveRoot);
    EF_CHECK(check_manual_exe("c:\\windows\\system32\\notepad.exe", ctx, none) == ManualExeCheck::SystemDir);
    EF_CHECK(check_manual_exe("C:\\Windows\\explorer.exe", ctx, none) == ManualExeCheck::SystemDir);
    EF_CHECK(check_manual_exe("C:\\Program Files\\tool.exe", ctx, none) == ManualExeCheck::ProtectedRoot);
    EF_CHECK(check_manual_exe("C:\\Users\\u\\game.exe", ctx, none) == ManualExeCheck::ProtectedRoot);
    EF_CHECK(check_manual_exe(std::string(600, 'a'), ctx, none) == ManualExeCheck::TooLong);
    const std::vector<std::string> one{"D:\\Games\\Racer\\racer.exe"};
    EF_CHECK(check_manual_exe("d:/games/racer/RACER.exe", ctx, one) == ManualExeCheck::Duplicate);
    std::vector<std::string> full;
    for (std::size_t i = 0; i < kMaxManualGames; ++i) full.push_back("D:\\G" + std::to_string(i) + "\\g.exe");
    EF_CHECK(check_manual_exe("D:\\Other\\o.exe", ctx, full) == ManualExeCheck::Limit);
}

EF_TEST(x11_manual_exe_name_and_dir) {
    EF_CHECK(manual_exe_name("D:\\Games\\Racer\\Racer.EXE") == "Racer");
    EF_CHECK(manual_exe_dir("D:\\Games\\Racer\\Racer.exe") == "D:\\Games\\Racer");
    EF_CHECK(manual_exe_dir("D:\\x.exe") == "D:\\");
    EF_CHECK(manual_exe_dir("x.exe").empty());
}

EF_TEST(x11_manual_games_join_library_fail_safe) {
    PkgFs f;
    LibraryScanOptions o;
    o.manual_exes = {"D:\\Games\\Racer\\Racer.exe", "E:\\Gone\\gone.exe", "F:\\Locked\\locked.exe"};
    f.kinds["D:\\Games\\Racer\\Racer.exe"] = PathKind::File;
    f.kinds["F:\\Locked\\locked.exe"] = PathKind::Denied;  // okunamayan dosya da izlenir
    const auto r = scan_library(f, o);
    // Kayitli her giris bir satirdir: yok olan EXE "bulunamadi" olarak kalir ki kaldirilabilsin (sessizce kaybolmaz).
    EF_CHECK(r.trusted() && r.games.size() == 3);
    EF_CHECK(r.games[0].launcher == "manual" && r.games[0].install_dir == "D:\\Games\\Racer" && r.games[0].exe_hint == "D:\\Games\\Racer\\Racer.exe");
    EF_CHECK(r.games[0].name == "Racer" && r.games[0].app_id == "d:\\games\\racer\\racer.exe");
    EF_CHECK(r.games[0].manual_exe == "D:\\Games\\Racer\\Racer.exe" && !r.games[0].manual_missing);
    EF_CHECK(r.games[1].manual_exe == "E:\\Gone\\gone.exe" && r.games[1].manual_missing);
    EF_CHECK(r.games[2].manual_exe == "F:\\Locked\\locked.exe" && !r.games[2].manual_missing);
    EF_CHECK(of(r, "manual") && of(r, "manual")->status == ProviderStatus::Partial && of(r, "manual")->games == 2);
    const LibraryScanOptions empty;
    EF_CHECK(!of(scan_library(f, empty), "manual"));  // liste bossa satir yok
}

EF_TEST(x11_manual_exe_already_listed_is_not_duplicated) {
    auto f = game_fs();
    auto o = xbox_on();
    o.manual_exes = {"C:\\XboxGames\\Racing\\Content\\bin\\Racing.exe"};
    f.kinds[o.manual_exes[0]] = PathKind::File;
    const auto r = scan_library(f, o);
    // Ikinci satir acilmaz ama giris launcher satirina baglanir: "Listeden cikar" gorunur kalir.
    EF_CHECK(r.games.size() == 1 && r.games[0].launcher == "xbox" && r.trusted());
    EF_CHECK(r.games[0].manual_exe == o.manual_exes[0] && !r.games[0].manual_missing);
    EF_CHECK(of(r, "manual")->status == ProviderStatus::Found && of(r, "manual")->games == 1);
}

EF_TEST(x11_manual_exes_persist_and_filter_invalid) {
    ef::core::settings::Preferences p;
    p.manual_game_exes = {"D:\\Games\\Racer\\racer.exe", "D:\\Games\\Racer\\RACER.exe", "racer.exe", "D:\\x.bat"};
    const auto r = ef::core::settings::Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().manual_game_exes.size() == 1 && r.value().manual_game_exes[0] == "D:\\Games\\Racer\\racer.exe");
    const auto old = ef::core::settings::Preferences::parse("{\"schema_version\":1}");
    EF_CHECK(old.ok() && old.value().manual_game_exes.empty());
}

EF_TEST(x11_provider_status_keys_distinguish_states) {
    EF_CHECK(provider_status_key(ProviderStatus::Found) == "games.source.found");
    EF_CHECK(provider_status_key(ProviderStatus::Missing) == "games.source.none");
    EF_CHECK(provider_status_key(ProviderStatus::Denied) == "games.source.unreadable");
    EF_CHECK(provider_status_key(ProviderStatus::Error) == "games.source.unreadable");
    EF_CHECK(provider_status_key(ProviderStatus::Partial) == "games.source.partial");
    EF_CHECK(provider_status_key(ProviderStatus::Unsupported) == "games.source.unsupported");
    EF_CHECK(provider_status_key(ProviderStatus::Disabled) == "games.source.disabled");
}

// EF-X11 dogrulama D1: hicbir kayitli giris gorunmez kalmaz.
EF_TEST(x11_d1_every_persisted_manual_entry_is_visible) {
    PkgFs f;
    LibraryScanOptions o;
    // Ayni klasorde iki elle eklenen EXE birbirine baglanmaz; klasor yolu (Directory) da "bulunamadi" sayilir.
    o.manual_exes = {"D:\\Tools\\a.exe", "D:\\Tools\\b.exe", "D:\\Tools\\dir.exe", "d:\\tools\\A.EXE"};
    f.kinds["D:\\Tools\\a.exe"] = PathKind::File;
    f.kinds["D:\\Tools\\b.exe"] = PathKind::File;
    f.kinds["D:\\Tools\\dir.exe"] = PathKind::Directory;
    const auto r = scan_library(f, o);
    EF_CHECK(r.games.size() == 3);  // elle duzenlenmis tekrar tek satir
    EF_CHECK(r.games[0].manual_exe == "D:\\Tools\\a.exe" && r.games[1].manual_exe == "D:\\Tools\\b.exe");
    EF_CHECK(r.games[2].manual_missing && r.games[2].manual_exe == "D:\\Tools\\dir.exe");
    // Tumu yoksa saglayici "yok" degil kismi: satir kaldirilabilir durumda kalir.
    PkgFs none;
    LibraryScanOptions g;
    g.manual_exes = {"E:\\Gone\\gone.exe"};
    const auto m = scan_library(none, g);
    EF_CHECK(m.games.size() == 1 && m.games[0].manual_missing && of(m, "manual")->status == ProviderStatus::Partial);
}

EF_TEST(x11_d1_second_manual_under_same_launcher_row_gets_own_row) {
    auto f = game_fs();
    auto o = xbox_on();
    o.manual_exes = {"C:\\XboxGames\\Racing\\Content\\bin\\Racing.exe", "C:\\XboxGames\\Racing\\Content\\bin\\Tool.exe"};
    for (const auto& e : o.manual_exes) f.kinds[e] = PathKind::File;
    const auto r = scan_library(f, o);
    EF_CHECK(r.games.size() == 2 && r.games[0].launcher == "xbox" && r.games[0].manual_exe == o.manual_exes[0]);
    EF_CHECK(r.games[1].launcher == "manual" && r.games[1].manual_exe == o.manual_exes[1]);
    // Bagli giris parmak izine girer: ekleme/kaldirma satiri yeniden kurdurur.
    auto o1 = o;
    o1.manual_exes.resize(1);
    auto plain_fs = game_fs();
    const auto plain = scan_library(plain_fs, xbox_on());
    EF_CHECK(library_fingerprint(plain) != library_fingerprint(scan_library(f, o1)));
}

// EF-X11 dogrulama D2: elle eklenen oyun yalniz tam EXE ile eslesir; klasor yazma koku/taramasi olmaz.
EF_TEST(x11_d2_manual_game_matches_exact_exe_only) {
    ef::core::inventory::GameInstall m;
    m.launcher = "manual";
    m.install_dir = "C:\\Users\\u\\AppData\\Local\\Programs";
    m.exe_hint = "C:\\Users\\u\\AppData\\Local\\Programs\\Foo.exe";
    m.manual_exe = m.exe_hint;
    EF_CHECK(game_watch_key(m) == "c:\\users\\u\\appdata\\local\\programs\\foo.exe");
    EF_CHECK(!game_folder_trusted(m));
    ef::core::inventory::GameInstall st;
    st.launcher = "steam";
    st.install_dir = "D:\\Steam\\steamapps\\common\\Game\\";
    EF_CHECK(game_watch_key(st) == "d:\\steam\\steamapps\\common\\game" && game_folder_trusted(st));

    GameWatcher w({game_watch_key(m)}, {"foo.exe"});
    ProcessSnapshot snap;
    snap.state = DetectionState::Ok;
    snap.processes = {{10, 1, "c:\\users\\u\\appdata\\local\\programs\\discord\\discord.exe", "discord.exe", true},
                      {11, 1, "c:\\users\\u\\appdata\\local\\programs\\other.exe", "other.exe", true}};
    auto r = w.update(snap);
    EF_CHECK(r.detection == DetectionState::Ok && r.running.empty());  // ortak klasordeki baska surec oyun degil
    snap.processes.push_back({12, 1, "c:\\users\\u\\appdata\\local\\programs\\foo.exe", "foo.exe", true});
    r = w.update(snap);
    EF_CHECK(r.running.size() == 1 && r.running[0].pid == 12 && r.running[0].install_dir == game_watch_key(m));
    // Kullanicinin sectigi tam EXE yardimci adi tasisa da oyundur; klasor eslesmesinde yardimci dislanmaya devam eder.
    GameWatcher h({"d:\\games\\x\\launcher.exe", "d:\\games\\y"}, {});
    snap.processes = {{20, 1, "d:\\games\\x\\launcher.exe", "launcher.exe", true}, {21, 1, "d:\\games\\y\\launcher.exe", "launcher.exe", true}};
    r = h.update(snap);
    EF_CHECK(r.running.size() == 1 && r.running[0].pid == 20);
}

// EF-X11 dogrulama D3: diskten okunan liste ekleme kurallarindan gecer.
EF_TEST(x11_d3_stored_manual_list_is_revalidated) {
    ManualExeContext ctx;
    ctx.windows_dir = "C:\\Windows";
    ctx.exact_roots = {"C:\\Program Files"};
    const std::vector<std::string> stored{"C:\\Windows\\System32\\svchost.exe", "D:\\Games\\B\\b.exe", "C:\\Program Files\\tool.exe",
                                          "D:\\Games\\A\\a.exe", "d:\\games\\b\\B.EXE"};
    const auto kept = filter_manual_exes(stored, ctx);
    EF_CHECK(kept.size() == 2 && kept[0] == "D:\\Games\\B\\b.exe" && kept[1] == "D:\\Games\\A\\a.exe");
    std::vector<std::string> many;
    for (std::size_t i = 0; i < kMaxManualGames + 5; ++i) many.push_back("D:\\G" + std::to_string(i) + "\\g.exe");
    EF_CHECK(filter_manual_exes(many, ctx).size() == kMaxManualGames);
}
