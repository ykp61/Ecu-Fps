// Oyun ikonu adaylari (spec 2026-11-12 §4): saf kural testleri. Sahte olan yalniz dosya sistemi kaynagidir.
#include <map>

#include "core/games/game_icon.h"
#include "ef_test.h"

using namespace ef::core::games;
using ef::core::inventory::GameInstall;

namespace {

constexpr const char* kHashA = "0123456789abcdef0123456789abcdef01234567.jpg";
constexpr const char* kHashB = "89abcdef0123456789abcdef0123456789abcdef.jpg";

class IconFs final : public LibraryFs {
public:
    std::map<std::string, DirList> dirs;
    std::vector<std::string> listed;  // hangi klasorler listelendi (yalniz Steam onbellegi olmali)

    SourceValue steam_root() override { return {SourceValue::Status::Found, "S:\\steam"}; }
    SourceValue epic_manifest_dir() override { return {SourceValue::Status::Missing, ""}; }
    PathKind probe(const std::string&) override { return PathKind::Missing; }
    DirList list(const std::string& d) override {
        listed.push_back(d);
        const auto it = dirs.find(d);
        return it == dirs.end() ? DirList{DirList::Status::Missing, {}} : it->second;
    }
    FileRead read_file(const std::string&, std::size_t) override { return {FileRead::Status::Missing, {}}; }
};

GameInstall steam_game(const char* appid = "730") {
    GameInstall g;
    g.name = "Counter-Strike 2";
    g.launcher = "steam";
    g.app_id = appid;
    g.install_dir = "T:\\lib\\steamapps\\common\\cs2";
    return g;
}

const std::string kCache730 = "S:\\steam\\appcache\\librarycache\\730";
const std::string kCs2Exe = "t:\\lib\\steamapps\\common\\cs2\\game\\bin\\win64\\cs2.exe";

}  // namespace

EF_TEST(game_icon_steam_file_name_rule) {
    EF_CHECK(steam_icon_file_name(kHashA));
    EF_CHECK(!steam_icon_file_name("0123456789ABCDEF0123456789abcdef01234567.jpg"));  // buyuk harf
    EF_CHECK(!steam_icon_file_name("0123456789abcdef0123456789abcdef01234567.JPG"));
    EF_CHECK(!steam_icon_file_name("g123456789abcdef0123456789abcdef01234567.jpg"));  // onaltilik degil
    EF_CHECK(!steam_icon_file_name("0123456789abcdef0123456789abcdef0123456.jpg"));   // 39 hane
    EF_CHECK(!steam_icon_file_name("0123456789abcdef0123456789abcdef012345678.jpg")); // 41 hane
    EF_CHECK(!steam_icon_file_name("0123456789abcdef0123456789abcdef01234567.png"));
    EF_CHECK(!steam_icon_file_name("header.jpg") && !steam_icon_file_name("library_600x900.jpg") && !steam_icon_file_name(""));
}

EF_TEST(game_icon_steam_single_hash_then_exe) {
    IconFs fs;
    fs.dirs[kCache730] = {DirList::Status::Ok, {"header.jpg", "library_600x900.jpg", "logo.png", kHashA}};
    const auto c = icon_candidates(steam_game(), kCs2Exe, "S:\\steam", fs);
    EF_CHECK(c.size() == 2);
    EF_CHECK(c.size() == 2 && c[0] == (IconCandidate{IconSourceKind::ImageFile, kCache730 + "\\" + kHashA}));
    EF_CHECK(c.size() == 2 && c[1] == (IconCandidate{IconSourceKind::ExeResource, kCs2Exe}));
    EF_CHECK(fs.listed == std::vector<std::string>{kCache730});  // oyun klasoru listelenmez
    // Kok sonundaki ayrac ve "/" ayni klasoru verir.
    IconFs fs2;
    fs2.dirs[kCache730] = fs.dirs[kCache730];
    const auto d = icon_candidates(steam_game(), "", "S:\\steam\\", fs2);
    EF_CHECK(d.size() == 1 && d[0].kind == IconSourceKind::ImageFile && d[0].path == kCache730 + "\\" + kHashA);
}

EF_TEST(game_icon_steam_zero_or_two_hash_files_are_skipped) {
    IconFs none;
    none.dirs[kCache730] = {DirList::Status::Ok, {"header.jpg", "logo.png"}};
    EF_CHECK(icon_candidates(steam_game(), "", "S:\\steam", none).empty());
    IconFs two;
    two.dirs[kCache730] = {DirList::Status::Ok, {kHashA, kHashB}};
    EF_CHECK(icon_candidates(steam_game(), "", "S:\\steam", two).empty());
    // Iki dosyada onbellek atlanir ama EXE adayi kalir.
    const auto c = icon_candidates(steam_game(), kCs2Exe, "S:\\steam", two);
    EF_CHECK(c.size() == 1 && c[0].kind == IconSourceKind::ExeResource);
}

EF_TEST(game_icon_steam_uppercase_or_non_hex_names_do_not_count) {
    IconFs fs;
    fs.dirs[kCache730] = {DirList::Status::Ok,
                          {"0123456789ABCDEF0123456789ABCDEF01234567.jpg", "zz23456789abcdef0123456789abcdef01234567.jpg",
                           "0123456789abcdef0123456789abcdef01234567.jpeg", "0123456789abcdef0123456789abcdef01234567.jpg.tmp"}};
    EF_CHECK(icon_candidates(steam_game(), "", "S:\\steam", fs).empty());
    // Gecersiz adlar sayilmaz: yanlarindaki tek gecerli dosya yine "tek" dosyadir.
    fs.dirs[kCache730].names.push_back(kHashB);
    const auto c = icon_candidates(steam_game(), "", "S:\\steam", fs);
    EF_CHECK(c.size() == 1 && c[0].path == kCache730 + "\\" + kHashB);
}

EF_TEST(game_icon_steam_unreadable_listing_is_skipped) {
    for (const auto st : {DirList::Status::Denied, DirList::Status::Missing, DirList::Status::Error, DirList::Status::Incomplete}) {
        IconFs fs;
        fs.dirs[kCache730] = {st, {kHashA}};  // yarim/reddedilen listede gorunen tek dosya kanit degildir
        EF_CHECK(icon_candidates(steam_game(), "", "S:\\steam", fs).empty());
        const auto c = icon_candidates(steam_game(), kCs2Exe, "S:\\steam", fs);
        EF_CHECK(c.size() == 1 && c[0].kind == IconSourceKind::ExeResource && c[0].path == kCs2Exe);
    }
}

EF_TEST(game_icon_steam_bad_appid_or_root_never_lists) {
    for (const char* id : {"", "73a", "..", "730\\..\\x", "12345678901"}) {
        IconFs fs;
        EF_CHECK(icon_candidates(steam_game(id), "", "S:\\steam", fs).empty());
        EF_CHECK(fs.listed.empty());
    }
    for (const char* root : {"", "steam", "\\\\server\\share\\steam"}) {
        IconFs fs;
        EF_CHECK(icon_candidates(steam_game(), "", root, fs).empty());
        EF_CHECK(fs.listed.empty());
    }
}

EF_TEST(game_icon_epic_uses_resolved_exe_only) {
    IconFs fs;
    GameInstall g;
    g.name = "Fortnite";
    g.launcher = "epic";
    g.app_id = "Fortnite";
    g.install_dir = "D:\\Epic\\Fortnite";
    g.exe_hint = "FortniteGame/Binaries/Win64/FortniteLauncher.exe";
    const std::string exe = "d:\\epic\\fortnite\\fortnitegame\\binaries\\win64\\fortnitelauncher.exe";
    const auto c = icon_candidates(g, exe, "S:\\steam", fs);
    EF_CHECK(c.size() == 1 && c[0] == (IconCandidate{IconSourceKind::ExeResource, exe}));
    EF_CHECK(fs.listed.empty());  // Steam onbellegi yalniz Steam oyunlari icin okunur
    EF_CHECK(icon_candidates(g, "", "S:\\steam", fs).empty());  // EXE cozulmedi: tahmin yok
    for (const char* launcher : {"gog", "ubisoft", "xbox"}) {
        g.launcher = launcher;
        EF_CHECK(icon_candidates(g, exe, "", fs).size() == 1);
    }
}

EF_TEST(game_icon_exe_outside_install_dir_is_rejected) {
    IconFs fs;
    GameInstall g;
    g.launcher = "epic";
    g.install_dir = "D:\\Epic\\Fortnite";
    for (const char* exe : {"d:\\epic\\other\\game.exe", "d:\\epic\\fortnite\\..\\other\\game.exe", "d:\\epic\\fortnite2\\game.exe",
                            "c:\\windows\\explorer.exe", "game.exe", "d:\\epic\\fortnite\\game.dll", "d:\\epic\\fortnite"})
        EF_CHECK(icon_candidates(g, exe, "S:\\steam", fs).empty());
    // Reddedilen cozulmus EXE yerine bagli elle eklenen EXE'ye de dusulmez.
    g.manual_exe = "D:\\Epic\\Fortnite\\Launcher.exe";
    EF_CHECK(icon_candidates(g, "c:\\windows\\explorer.exe", "S:\\steam", fs).empty());
    // Steam: kurulum klasoru disindaki EXE reddedilir, onbellek adayi kalir.
    fs.dirs[kCache730] = {DirList::Status::Ok, {kHashA}};
    const auto c = icon_candidates(steam_game(), "c:\\windows\\system32\\notepad.exe", "S:\\steam", fs);
    EF_CHECK(c.size() == 1 && c[0].kind == IconSourceKind::ImageFile);
}

EF_TEST(game_icon_attached_manual_exe_is_fallback_under_install_dir) {
    IconFs fs;
    GameInstall g;
    g.launcher = "epic";
    g.install_dir = "D:\\Epic\\Game";
    g.manual_exe = "D:\\Epic\\Game\\bin\\game.exe";
    const auto c = icon_candidates(g, "", "", fs);
    EF_CHECK(c.size() == 1 && c[0] == (IconCandidate{IconSourceKind::ExeResource, "D:\\Epic\\Game\\bin\\game.exe"}));
    // Cozulmus EXE varsa o kullanilir (tek aday).
    const auto d = icon_candidates(g, "d:\\epic\\game\\start.exe", "", fs);
    EF_CHECK(d.size() == 1 && d[0].path == "d:\\epic\\game\\start.exe");
    g.manual_exe = "E:\\Other\\game.exe";  // klasor disi: aday degil
    EF_CHECK(icon_candidates(g, "", "", fs).empty());
}

EF_TEST(game_icon_manual_game_uses_its_exe_and_missing_gives_none) {
    IconFs fs;
    GameInstall g;
    g.launcher = "manual";
    g.install_dir = "D:\\Games\\Racing";
    g.exe_hint = "D:\\Games\\Racing\\racing.exe";
    g.manual_exe = "D:\\Games\\Racing\\racing.exe";
    const auto c = icon_candidates(g, "", "S:\\steam", fs);
    EF_CHECK(c.size() == 1 && c[0] == (IconCandidate{IconSourceKind::ExeResource, "D:\\Games\\Racing\\racing.exe"}));
    EF_CHECK(fs.listed.empty());
    g.manual_missing = true;  // dosya artik yok: aday uretilmez (harf kutusu)
    EF_CHECK(icon_candidates(g, "", "S:\\steam", fs).empty());
    g.manual_missing = false;
    g.manual_exe = "racing.exe";  // goreli yol kabul edilmez
    EF_CHECK(icon_candidates(g, "", "S:\\steam", fs).empty());
    g.manual_exe = "D:\\Games\\Racing\\racing.bat";
    EF_CHECK(icon_candidates(g, "", "S:\\steam", fs).empty());
}

EF_TEST(game_icon_jpeg_stream_must_reach_eoi) {
    using Bytes = std::vector<std::uint8_t>;
    const auto ok = [](const Bytes& b) { return jpeg_stream_complete(b.data(), b.size()); };
    // SOI, APP0 (len 4), DQT (len 3), SOS (len 3), tarama verisi (FF00 doldurma + RST0 dahil), EOI.
    const Bytes good = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x11, 0x22, 0xFF, 0xDB, 0x00, 0x03, 0x33,
                        0xFF, 0xDA, 0x00, 0x03, 0x44, 0x12, 0xFF, 0x00, 0x34, 0xFF, 0xD0, 0x56, 0xFF, 0xD9};
    EF_CHECK(ok(good));
    // Her kesik onek (baslikta, bolum icinde, tarama verisinde, EOI'nin ortasinda) eksiktir.
    for (std::size_t n = 0; n < good.size(); ++n) EF_CHECK(!ok(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n))));
    // EOI'den sonraki artik baytlar onemsizdir.
    Bytes padded = good;
    padded.insert(padded.end(), 64, 0x00);
    EF_CHECK(ok(padded));
    // Bolum icindeki gomulu kucuk resim (SOI..EOI) uzunlukla atlanir: disaridaki akis kesikse yine eksiktir.
    const Bytes thumb = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x08, 0xFF, 0xD8, 0xFF, 0xD9, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x02, 0x10, 0x20};
    EF_CHECK(!ok(thumb));
    Bytes thumb_ok = thumb;
    thumb_ok.insert(thumb_ok.end(), {0xFF, 0xD9});
    EF_CHECK(ok(thumb_ok));
    // Asamali JPEG: birden cok SOS; isaretciden onceki dolgu 0xFF baytlari.
    const Bytes multi = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x01, 0xFF, 0xC4, 0x00, 0x02, 0xFF, 0xFF, 0xDA, 0x00, 0x02, 0x02, 0xFF, 0xFF, 0xD9};
    EF_CHECK(ok(multi));
    // JPEG olmayan icerik, bozuk bolum uzunlugu, veri sinirini asan uzunluk.
    EF_CHECK(!ok({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}));
    EF_CHECK(!ok({0xFF, 0xD8, 0x00, 0x00, 0xFF, 0xD9}));
    EF_CHECK(!ok({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0xFF, 0xD9}));
    EF_CHECK(!ok({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9}));
    EF_CHECK(!ok({0xFF, 0xD8, 0xFF, 0xD8, 0xFF, 0xD9}));
    EF_CHECK(!jpeg_stream_complete(nullptr, 0));
    EF_CHECK(ok({0xFF, 0xD8, 0xFF, 0xD9}));  // bos ama yapisal olarak tam (piksel yok: kod cozucu reddeder)
}

EF_TEST(game_icon_image_pixels_validity) {
    ImagePixels p;
    EF_CHECK(!image_pixels_valid(p));
    p.w = 64, p.h = 64, p.bgra.assign(64 * 64 * 4, 0);
    EF_CHECK(image_pixels_valid(p));
    p.bgra.pop_back();
    EF_CHECK(!image_pixels_valid(p));  // eksik arabellek
    p.w = 257, p.h = 1, p.bgra.assign(257 * 4, 0);
    EF_CHECK(!image_pixels_valid(p));  // cizim siniri
    p.w = 0, p.h = 4, p.bgra.clear();
    EF_CHECK(!image_pixels_valid(p));
}
