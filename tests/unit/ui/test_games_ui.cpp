// Siki yerlesim (spec 2026-11-11 §5): Oyunlar sayfasi saf kurallari — izgara sutun esikleri, gorunum suzgeci,
// kisaltma kutusu metni, baslik seridi ogelerinin kimlikleri ve odak sirasi (Oyun hazirligi dahil).
// Cizim ui_snapshot'ta denetlenir.
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

namespace {
ViewModel::GameRowView game(const char* name, const char* protection, std::vector<std::string> risks) {
    ViewModel::GameRowView g;
    g.name = name;
    g.launcher = "steam";
    g.protection = protection;
    g.risk_keys = std::move(risks);
    return g;
}
std::ptrdiff_t pos(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) - v.begin(); }
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
}  // namespace

EF_TEST(games_grid_columns_by_width) {
    // govde >= 1100 -> 4 sutun, >= 820 -> 3, >= 540 -> 2, aksi 1
    static_assert(kGamesGrid4MinW == 1100.f && kGamesGrid3MinW == 820.f && kGamesGrid2MinW == 540.f);
    EF_CHECK(games_grid_columns(1280.f) == 4);
    EF_CHECK(games_grid_columns(1148.f) == 4);  // 1180 pencere - 2 x 16 kenar
    EF_CHECK(games_grid_columns(1100.f) == 4);
    EF_CHECK(games_grid_columns(1099.f) == 3);
    EF_CHECK(games_grid_columns(928.f) == 3);   // 960 pencere - 2 x 16 kenar
    EF_CHECK(games_grid_columns(820.f) == 3);
    EF_CHECK(games_grid_columns(819.f) == 2);
    EF_CHECK(games_grid_columns(540.f) == 2);
    EF_CHECK(games_grid_columns(539.f) == 1);
    EF_CHECK(games_grid_columns(0.f) == 1);
}

EF_TEST(games_filter_rows_by_protection_and_advice) {
    const std::vector<ViewModel::GameRowView> games = {
        game("Alpha", "", {}),
        game("Bravo", "Easy Anti-Cheat", {}),
        game("Charlie", "", {"risk.launch.high_priority"}),
        game("Delta", "BattlEye, Vanguard", {"risk.video.vsync", "risk.video.msaa"}),
    };
    EF_CHECK((games_filter_rows(games, GamesFilter::All) == std::vector<std::size_t>{0, 1, 2, 3}));
    EF_CHECK((games_filter_rows(games, GamesFilter::Protected) == std::vector<std::size_t>{1, 3}));
    EF_CHECK((games_filter_rows(games, GamesFilter::Advice) == std::vector<std::size_t>{2, 3}));
    EF_CHECK(games_filter_match(GamesFilter::All, games[0]));
    EF_CHECK(!games_filter_match(GamesFilter::Protected, games[0]) && !games_filter_match(GamesFilter::Advice, games[0]));
    // Suzgec veriyi degistirmez; bos listede bos doner.
    EF_CHECK(games_filter_rows({}, GamesFilter::All).empty());
    EF_CHECK(games_filter_rows({}, GamesFilter::Protected).empty());
    // Calisiyor / elle eklenmis olmak suzgeci etkilemez.
    auto running = game("Echo", "", {});
    running.running = true;
    running.manual_exe = "D:\\g\\e.exe";
    EF_CHECK(!games_filter_match(GamesFilter::Protected, running) && !games_filter_match(GamesFilter::Advice, running));
}

EF_TEST(games_filter_hits_and_automation_ids) {
    EF_CHECK(games_filter_hit(GamesFilter::All) == Hit::GamesFilterAll);
    EF_CHECK(games_filter_hit(GamesFilter::Protected) == Hit::GamesFilterProtected);
    EF_CHECK(games_filter_hit(GamesFilter::Advice) == Hit::GamesFilterAdvice);
    EF_CHECK(hit_automation_id(Hit::GamesFilterAll) == L"games.filter.all");
    EF_CHECK(hit_automation_id(Hit::GamesFilterProtected) == L"games.filter.protected");
    EF_CHECK(hit_automation_id(Hit::GamesFilterAdvice) == L"games.filter.advice");
    EF_CHECK(hit_automation_id(Hit::GamesRescan) == L"games.rescan");
    for (Hit h : {Hit::GamesFilterAll, Hit::GamesFilterProtected, Hit::GamesFilterAdvice, Hit::GamesRescan}) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), h) != std::end(kFixedHits));
    }
    // Mevcut kimlikler yerinde.
    EF_CHECK(hit_automation_id(Hit::GamesWhy) == L"games.why");
    EF_CHECK(hit_automation_id(Hit::GamesAddExe) == L"games.add_exe");
    EF_CHECK(hit_automation_id(Hit::GamesGuides) == L"games.guides");
    EF_CHECK(hit_automation_id(Hit::ChkRescan) == L"readiness.rescan");
    EF_CHECK(hit_automation_id(Hit::ChkNetTest) == L"readiness.nettest");
}

EF_TEST(game_initials_first_letters_of_two_words) {
    // Iki+ sozcuk: ilk iki sozcugun ilk harfleri.
    EF_CHECK(game_initials("Counter-Strike 2", true) == "CS");  // tire sozcuk ayirir
    EF_CHECK(game_initials("Contoso Racing", true) == "CR");
    EF_CHECK(game_initials("Microsoft Flight Simulator 2024 — Standard Edition", true) == "MF");
    EF_CHECK(game_initials("Half-Life: Alyx", false) == "HL");
    EF_CHECK(game_initials("  red   dead redemption  ", false) == "RD");
    EF_CHECK(game_initials("Assassin's Creed", false) == "AC");        // kesme isareti sozcugu bolmez
    EF_CHECK(game_initials("Assassin\xE2\x80\x99s Creed", false) == "AC");  // tipografik kesme (U+2019)
    EF_CHECK(game_initials("FIFA 23", false) == "F2");
    // Tek sozcuk: ilk iki harf, buyuk harfle.
    EF_CHECK(game_initials("Fortnite", true) == "FO");
    EF_CHECK(game_initials("DOOM", false) == "DO");
    EF_CHECK(game_initials("X", false) == "X");
    EF_CHECK(game_initials("2048", true) == "20");
    // Harf/rakam yoksa bos (kutu bos kalir; uydurma harf yok).
    EF_CHECK(game_initials("", true).empty());
    EF_CHECK(game_initials(" -- : ", true).empty());
    // Emoji / piktogram harf degildir: ayirici sayilir, kisaltmaya girmez.
    EF_CHECK(game_initials("\xF0\x9F\x8E\xAE" " Game Night", false) == "GN");  // U+1F3AE
    EF_CHECK(game_initials("Race\xF0\x9F\x8F\x81" "Day", false) == "RD");    // U+1F3C1 sozcuk ayirir
    EF_CHECK(game_initials("\xF0\x9F\x8E\xAE", true).empty());
}

EF_TEST(game_initials_turkish_safe) {
    // Turkce: i -> İ (U+0130), ı -> I; cok baytli harfler bolunmez.
    EF_CHECK(game_initials("istanbul", true) == "\xC4\xB0S");                    // "İS"
    EF_CHECK(game_initials("\xC4\xB1\xC5\x9F\xC4\xB1k", true) == "I\xC5\x9E");    // "ışık" -> "IŞ"
    EF_CHECK(game_initials("\xC3\xA7\xC4\xB1lg\xC4\xB1n \xC5\x9Fof\xC3\xB6r", true) == "\xC3\x87\xC5\x9E");  // "çılgın şoför" -> "ÇŞ"
    EF_CHECK(game_initials("\xC3\x96l\xC3\xBC \xC4\xB0\xC3\xA7g\xC3\xBC" "d\xC3\xBC", true) == "\xC3\x96\xC4\xB0");  // "Ölü İçgüdü" -> "Öİ"
    EF_CHECK(game_initials("Minecraft", true) == "M\xC4\xB0");  // Turkce arayuzde "Mİ"
    // Turkce disi arayuz: i -> I (noktali buyuk I uretilmez); ı -> I.
    EF_CHECK(game_initials("Minecraft", false) == "MI");
    EF_CHECK(game_initials("inside", false) == "IN");
    EF_CHECK(game_initials("\xC4\xB1\xC5\x9F\xC4\xB1k", false) == "I\xC5\x9E");
    // Gecersiz UTF-8 baytlari ayirici sayilir; cokme veya yarim karakter yok.
    EF_CHECK(game_initials("\xFF\xFE" "ab", false) == "AB");
    EF_CHECK(game_initials("a\xC3", false) == "A");
}

EF_TEST(game_version_label_keeps_short_and_cuts_long) {
    // Kisa surumler oldugu gibi kalir (sinir dahil).
    EF_CHECK(game_version_label("") == "");
    EF_CHECK(game_version_label("26445667") == "26445667");
    EF_CHECK(game_version_label("1.0.1158.16_v2") == "1.0.1158.16_v2");
    EF_CHECK(game_version_label("123456789012345678901234") == "123456789012345678901234");  // tam 24
    // Uzun ham launcher surumu: ilk 24 karakter + "…".
    EF_CHECK(game_version_label("++Fortnite+Release-42.40-CL-59086525-Windows") == "++Fortnite+Release-42.40\xE2\x80\xA6");
    EF_CHECK(game_version_label("1234567890123456789012345") == "123456789012345678901234\xE2\x80\xA6");  // 25
    EF_CHECK(game_version_label("0.10.0.9.32-22d39a16-release") == "0.10.0.9.32-22d39a16-rel\xE2\x80\xA6");
    static_assert(kGameVersionMaxChars == 24);
    // Kesim yerindeki bosluk atilir; cok baytli karakter bolunmez (karakter = kod noktasi).
    EF_CHECK(game_version_label("build 2026 release ab cd ef", 6) == "build\xE2\x80\xA6");
    EF_CHECK(game_version_label("\xC3\xA7\xC4\xB1\xC5\x9F\xC3\xB6\xC3\xBC", 3) == "\xC3\xA7\xC4\xB1\xC5\x9F\xE2\x80\xA6");  // "çışöü" -> "çış…"
    EF_CHECK(game_version_label("\xC3\xA7\xC4\xB1\xC5\x9F", 3) == "\xC3\xA7\xC4\xB1\xC5\x9F");
    EF_CHECK(game_version_label("a\xF0\x9F\x8E\xAE" "bc", 2) == "a\xF0\x9F\x8E\xAE\xE2\x80\xA6");  // 4 baytlik karakter butun kalir
    // Gecersiz UTF-8: bayt tek karakter sayilir; cokme yok, sinir asilmaz.
    EF_CHECK(game_version_label("\xFF\xFE" "abc", 3) == "\xFF\xFE" "a\xE2\x80\xA6");
    EF_CHECK(game_version_label("ab\xC3", 3) == "ab\xC3");
    EF_CHECK(game_version_label("abc", 0) == "\xE2\x80\xA6");
}

EF_TEST(game_button_widths_side_by_side_when_they_fit) {
    const float gap = 8, pad = 12, compact = 8, min_w = 72;
    // Dogal ic boslukla sigiyor: dogal genislikler, tek satir.
    auto w = game_button_widths({109, 138}, 320, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 2 && w[0] == 133 && w[1] == 162);
    EF_CHECK(game_button_rows(w, 320, gap) == 1);
    // Sahibin goruntusundeki kart (ic genislik 288): dogal 133 + 8 + 162 = 303 sigmaz, siki 125 + 8 + 154 = 287 sigar.
    w = game_button_widths({109, 138}, 288, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 2 && w[0] == 125 && w[1] == 154);
    EF_CHECK(game_button_rows(w, 288, gap) == 1);
    // Siki boyutla da sigmiyor: dogal genislikler kalir, iki satir (bosuna daraltilmaz).
    w = game_button_widths({109, 138}, 251, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 2 && w[0] == 133 && w[1] == 162);
    EF_CHECK(game_button_rows(w, 251, gap) == 2);
    // Uc dugme: siki boyut satir sayisini 3'ten 2'ye indiriyorsa kullanilir.
    w = game_button_widths({109, 138, 100}, 288, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 3 && w[0] == 125 && w[1] == 154 && w[2] == 116);
    EF_CHECK(game_button_rows(w, 288, gap) == 2);
    // Tek dugme hic daraltilmaz; asgari genislik ve kart genisligi siniri korunur; kesirli metin yukari yuvarlanir.
    w = game_button_widths({109.2f}, 100, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 1 && w[0] == 100);
    w = game_button_widths({20, 400}, 288, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 2 && w[0] == 72 && w[1] == 288);
    w = game_button_widths({30.4f, 30.4f}, 200, gap, pad, compact, min_w);
    EF_CHECK(w.size() == 2 && w[0] == 72 && w[1] == 72);
    EF_CHECK(game_button_widths({}, 288, gap, pad, compact, min_w).empty());
    EF_CHECK(game_button_rows({}, 288, gap) == 0);
    // Siki ic bosluk dogal olandan kucuk degilse kural devre disi.
    w = game_button_widths({109, 138}, 288, gap, pad, pad, min_w);
    EF_CHECK(w.size() == 2 && w[0] == 133 && w[1] == 162);
}

EF_TEST(games_header_focus_order_follows_visual_order) {
    UiState s;
    s.page = Page::Games;
    s.games_filters = true;
    s.games_why = true;
    s.games_rescan = true;
    s.detection_retry = true;
    s.game_fs = {true};
    const auto ord = focus_order(s);
    const Hit last_tab = nav_hit(static_cast<std::size_t>(Page::Settings));
    // Sekmeler -> suzgec cipleri -> "Neden?" -> "Yeniden tara" -> govde (engel karti, oyun kartlari, kaynaklar, rehberler).
    EF_CHECK(pos(ord, last_tab) < pos(ord, Hit::GamesFilterAll));
    EF_CHECK(pos(ord, Hit::GamesFilterAll) < pos(ord, Hit::GamesFilterProtected));
    EF_CHECK(pos(ord, Hit::GamesFilterProtected) < pos(ord, Hit::GamesFilterAdvice));
    EF_CHECK(pos(ord, Hit::GamesFilterAdvice) < pos(ord, Hit::GamesWhy));
    EF_CHECK(pos(ord, Hit::GamesWhy) < pos(ord, Hit::GamesRescan));
    EF_CHECK(pos(ord, Hit::GamesRescan) < pos(ord, Hit::DetectionRetry));
    EF_CHECK(pos(ord, Hit::DetectionRetry) < pos(ord, hit_of(HitKind::GameFullscreen, 0)));
    EF_CHECK(pos(ord, hit_of(HitKind::GameFullscreen, 0)) < pos(ord, Hit::GamesAddExe));
    EF_CHECK(pos(ord, Hit::GamesAddExe) < pos(ord, Hit::GamesGuides));
    // BOOST maketi: "Oyun EXE'si ekle" seritte cizildiyse odak sirasi seritte ("Yeniden tara"dan once), govdede yok.
    {
        UiState h = s;
        h.games_add_header = true;
        const auto hord = focus_order(h);
        EF_CHECK(pos(hord, Hit::GamesFilterAdvice) < pos(hord, Hit::GamesAddExe));
        EF_CHECK(pos(hord, Hit::GamesAddExe) + 1 == pos(hord, Hit::GamesRescan));
        EF_CHECK(pos(hord, Hit::GamesRescan) < pos(hord, hit_of(HitKind::GameFullscreen, 0)));
        EF_CHECK(std::count(hord.begin(), hord.end(), Hit::GamesAddExe) == 1);
    }
    // Seritte cizilmeyen oge odak sirasina girmez (olu Tab duragi yok).
    s.games_filters = s.games_why = s.games_rescan = false;
    const auto bare = focus_order(s);
    for (Hit h : {Hit::GamesFilterAll, Hit::GamesFilterProtected, Hit::GamesFilterAdvice, Hit::GamesWhy, Hit::GamesRescan})
        EF_CHECK(!has(bare, h));
    // Baska sayfada Oyunlar serit ogeleri yok.
    s.games_filters = s.games_why = s.games_rescan = true;
    s.page = Page::Overview;
    EF_CHECK(!has(focus_order(s), Hit::GamesFilterAll) && !has(focus_order(s), Hit::GamesRescan));
}

EF_TEST(readiness_header_focus_order) {
    UiState s;
    s.page = Page::Readiness;
    s.readiness_net = true;
    s.readiness_actions = {true};
    const auto ord = focus_order(s);
    // Baslik seridi: "Agi test et" solda, "Yeniden tara" sagda; ikisi de satir dugmelerinden once.
    EF_CHECK(pos(ord, nav_hit(static_cast<std::size_t>(Page::Settings))) < pos(ord, Hit::ChkNetTest));
    EF_CHECK(pos(ord, Hit::ChkNetTest) < pos(ord, Hit::ChkRescan));
    EF_CHECK(pos(ord, Hit::ChkRescan) < pos(ord, hit_of(HitKind::ChkAction, 0)));
    s.readiness_net = false;
    EF_CHECK(!has(focus_order(s), Hit::ChkNetTest) && has(focus_order(s), Hit::ChkRescan));
    // Puan artik kart degil, listenin ustunde kartsiz hazirlik halkasidir (spec 2026-11-12 B3); renk bandi kurali
    // test_equal_rows.cpp'de (readiness_ring_band).
}

// ---- Spec 2026-11-12 B2: ince oyun karti, secili oyun seridi ----

EF_TEST(game_status_chips_at_most_two_by_priority) {
    const auto is = [](const GameChips& c, std::initializer_list<GameChip> want) {
        return c.count == want.size() && std::equal(want.begin(), want.end(), c.items.begin());
    };
    // Duz oyun: EXE yolu biliniyor -> Hazir; bilinmiyor -> Bir kez baslat.
    auto g = game("Alpha", "", {});
    EF_CHECK(is(game_status_chips(g), {GameChip::StartOnce}));
    g.fs_known = true;
    EF_CHECK(is(game_status_chips(g), {GameChip::Ready}));
    // Calisiyor yasam durumunun yerini alir (Hazir / Bir kez baslat yazilmaz).
    g.running = true;
    EF_CHECK(is(game_status_chips(g), {GameChip::Running}));
    g.fs_known = false;
    EF_CHECK(is(game_status_chips(g), {GameChip::Running}));
    // Korumali / onerili: yasam durumu once, nitelik sonra.
    auto p = game("Bravo", "Easy Anti-Cheat", {});
    p.fs_known = true;
    EF_CHECK(is(game_status_chips(p), {GameChip::Ready, GameChip::Protected}));
    p.fs_known = false;
    EF_CHECK(is(game_status_chips(p), {GameChip::StartOnce, GameChip::Protected}));
    auto a = game("Charlie", "", {"risk.video.vsync"});
    a.fs_known = true;
    EF_CHECK(is(game_status_chips(a), {GameChip::Ready, GameChip::Advice}));
    // Uc aday: en cok iki etiket — Calisiyor > Korumali > oneri > yasam durumu.
    auto both = game("Delta", "Vanguard", {"risk.video.vsync", "risk.video.msaa"});
    both.fs_known = true;
    EF_CHECK(is(game_status_chips(both), {GameChip::Protected, GameChip::Advice}));
    both.running = true;
    EF_CHECK(is(game_status_chips(both), {GameChip::Running, GameChip::Protected}));
    auto run_advice = game("Echo", "", {"risk.video.vsync"});
    run_advice.running = true;
    EF_CHECK(is(game_status_chips(run_advice), {GameChip::Running, GameChip::Advice}));
    // Her durumda en az bir, en cok iki etiket.
    for (const bool running : {false, true})
        for (const bool known : {false, true})
            for (const char* prot : {"", "BattlEye"})
                for (const bool advice : {false, true}) {
                    auto x = game("X", prot, advice ? std::vector<std::string>{"risk.video.vsync"} : std::vector<std::string>{});
                    x.running = running;
                    x.fs_known = known;
                    const auto c = game_status_chips(x);
                    EF_CHECK(c.count >= 1 && c.count <= 2);
                    if (c.count == 2) EF_CHECK(static_cast<int>(c.items[0]) < static_cast<int>(c.items[1]));  // gorunen sira
                }
}

EF_TEST(game_selection_key_survives_reordering) {
    auto a = game("Alpha", "", {});
    a.dir_key = "d:/games/alpha";
    auto b = game("Bravo", "", {});
    b.dir_key = "d:/games/bravo";
    auto c = game("Alpha", "", {});  // ayni ad, baska kaynak / klasor
    c.launcher = "epic";
    c.dir_key = "e:/epic/alpha";
    EF_CHECK(game_row_key(a) != game_row_key(b) && game_row_key(a) != game_row_key(c));
    std::vector<ViewModel::GameRowView> games = {a, b, c};
    const std::string key = game_row_key(c);
    EF_CHECK(games_selected_row(games, key) == std::optional<std::size_t>{2});
    // Yeniden taramada sira degisir: secim ayni oyunda kalir.
    games = {c, a, b};
    EF_CHECK(games_selected_row(games, key) == std::optional<std::size_t>{0});
    // Oyun listeden cikti / secim yok.
    games = {a, b};
    EF_CHECK(!games_selected_row(games, key));
    EF_CHECK(!games_selected_row(games, ""));
    EF_CHECK(!games_selected_row({}, key));
}

EF_TEST(games_arrow_keys_move_between_cards) {
    // 7 gorunen kart, 3 sutun; suzgec sirasi oyun indeksinden farkli olabilir.
    const std::vector<std::size_t> order = {4, 0, 9, 2, 7, 5, 1};
    const auto card = [](std::size_t i) { return hit_of(HitKind::GameCard, i); };
    // Sol / sag: gorunen sirada komsu kart (satir sonunda alt / ust satira gecer); uclarda durur.
    EF_CHECK(games_arrow_target(order, 3, card(4), 1, 0) == card(0));
    EF_CHECK(games_arrow_target(order, 3, card(9), 1, 0) == card(2));
    EF_CHECK(games_arrow_target(order, 3, card(2), -1, 0) == card(9));
    EF_CHECK(games_arrow_target(order, 3, card(4), -1, 0) == Hit::None);
    EF_CHECK(games_arrow_target(order, 3, card(1), 1, 0) == Hit::None);
    // Yukari / asagi: ayni sutunda bir satir; satir yoksa durur.
    EF_CHECK(games_arrow_target(order, 3, card(4), 0, 1) == card(2));
    EF_CHECK(games_arrow_target(order, 3, card(2), 0, 1) == card(1));
    EF_CHECK(games_arrow_target(order, 3, card(1), 0, -1) == card(2));
    EF_CHECK(games_arrow_target(order, 3, card(7), 0, 1) == Hit::None);  // alt satirda o sutun bos
    EF_CHECK(games_arrow_target(order, 3, card(0), 0, -1) == Hit::None);
    // Tek sutun: yukari / asagi komsu kart.
    EF_CHECK(games_arrow_target(order, 1, card(0), 0, 1) == card(9));
    // Odak kart degilse ya da kart gorunen sirada yoksa tus islenmez.
    EF_CHECK(games_arrow_target(order, 3, Hit::GamesRescan, 1, 0) == Hit::None);
    EF_CHECK(games_arrow_target(order, 3, hit_of(HitKind::GameFullscreen, 4), 1, 0) == Hit::None);
    EF_CHECK(games_arrow_target(order, 3, card(3), 1, 0) == Hit::None);
    EF_CHECK(games_arrow_target({}, 3, card(0), 1, 0) == Hit::None);
    EF_CHECK(games_arrow_target(order, 0, card(4), 0, 1) == Hit::None);
}

EF_TEST(games_cards_and_strip_actions_in_tab_order) {
    UiState s;
    s.page = Page::Games;
    s.games_filters = s.games_rescan = true;
    // Bes oyun; gorunen sira 3, 0, 4, 1 (2 suzulmus). Secili oyun 4: dort islemi de var.
    s.game_cards = {true, true, false, true, true};
    s.game_card_order = {3, 0, 4, 1};
    s.game_fs = {false, false, false, false, true};
    s.game_guides = {false, false, false, false, true};
    s.game_nv = {false, false, false, false, true};
    s.game_manual = {false, false, false, false, true};
    const auto ord = focus_order(s);
    const auto card = [](std::size_t i) { return hit_of(HitKind::GameCard, i); };
    // Her kart bir Tab duragi, gorunen sirada; suzulen oyunun karti yok.
    EF_CHECK(pos(ord, Hit::GamesRescan) < pos(ord, card(3)));
    EF_CHECK(pos(ord, card(3)) + 1 == pos(ord, card(0)));
    EF_CHECK(pos(ord, card(0)) + 1 == pos(ord, card(4)));
    EF_CHECK(!has(ord, card(2)));
    // Secili kartin HEMEN ardindan serit islemleri (gorunen dugme sirasi), sonra siradaki kart.
    EF_CHECK(pos(ord, card(4)) + 1 == pos(ord, hit_of(HitKind::GameNvidia, 4)));
    EF_CHECK(pos(ord, card(4)) + 2 == pos(ord, hit_of(HitKind::GameFullscreen, 4)));
    EF_CHECK(pos(ord, card(4)) + 3 == pos(ord, hit_of(HitKind::GameGuide, 4)));
    EF_CHECK(pos(ord, card(4)) + 4 == pos(ord, hit_of(HitKind::GameManualRemove, 4)));
    EF_CHECK(pos(ord, card(4)) + 5 == pos(ord, card(1)));
    EF_CHECK(pos(ord, card(1)) < pos(ord, Hit::GamesAddExe));
    EF_CHECK(pos(ord, Hit::GamesAddExe) < pos(ord, Hit::GamesGuides));
    // "Neden?" satiri kalkti: cizim bayragi yokken odak sirasinda da yok.
    EF_CHECK(!has(ord, Hit::GamesWhy));
    // Baska sayfada oyun kartlari sirada yok.
    s.page = Page::Overview;
    EF_CHECK(!has(focus_order(s), card(3)));
}

EF_TEST(game_card_hit_block_and_motion_tokens) {
    // Yeni kimlik blogu: games.card.N (mevcut bloklarla cakismaz: static_assert); eski kimlikler yerinde.
    EF_CHECK(classify_hit(hit_of(HitKind::GameCard, 0)) == (HitSlot{HitKind::GameCard, 0}));
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameCard, 7)) == L"games.card.7");
    EF_CHECK(hit_of(HitKind::GameCard, static_cast<std::size_t>(kMaxGameRows)) == Hit::None);
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameGuide, 7)) == L"games.pro.7");
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameFullscreen, 7)) == L"games.fs.7");
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameNvidia, 7)) == L"games.nv.7");
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameManualRemove, 7)) == L"games.remove.7");
    // Hareket token'lari: kart 200 ms'de 2 DIP yukselir (motion_policy: en cok 4 DIP); nabiz dongusu.
    const Theme& t = default_theme();
    EF_CHECK(t.motion.card_lift == 200 && t.card_lift == 2.f && t.card_lift <= 4.f);
    EF_CHECK(t.motion.pulse_period >= 600 && t.status_dot >= 4.f);
    // Ince kart (BOOST maketi .game-card): p-3 + 1 DIP kenarlik, ikon kutusu + ad (13px) / magaza (11px), gap-2.5, tek
    // satir etiket (11px + py-0.5). Maketin olculen kutusu 92,5 DIP.
    EF_CHECK(kGameCardInset == 13.f && kGameCardGap == 10.f && game_chip_h(t) == t.role(Role::Small11).line + 4.f);
    EF_CHECK(game_card_h(t) == kGameCardInset * 2 + std::max(kGameIconTile, t.role(Role::Semi13).line + t.role(Role::Small11).line) + kGameCardGap +
                                    game_chip_h(t));
    EF_CHECK(std::fabs(game_card_h(t) - 92.5f) < 0.01f);
}

EF_TEST(games_pulse_gates) {
    StripInputs in;
    in.visible = in.foreground = in.app_animations = true;
    // Nokta yalniz bir oyun acikken vardir: "oyun acik" kapisi nabzi durdurmaz; pencere on planda olmalidir.
    in.game_blocked = true;
    EF_CHECK(pulse_should_animate(in, true));
    EF_CHECK(!pulse_should_animate(in, false));  // ekranda nokta yok
    for (bool StripInputs::*off : {&StripInputs::visible, &StripInputs::foreground, &StripInputs::app_animations}) {
        StripInputs x = in;
        x.*off = false;
        EF_CHECK(!pulse_should_animate(x, true));
    }
    for (bool StripInputs::*on : {&StripInputs::test_running, &StripInputs::on_battery, &StripInputs::reduced_motion, &StripInputs::high_contrast,
                                  &StripInputs::modal}) {
        StripInputs x = in;
        x.*on = true;
        EF_CHECK(!pulse_should_animate(x, true));
    }
    // Opaklik: hareket kapaliyken hep 1; acikken dongu basinda 1, ortasinda en sonuk.
    EF_CHECK(pulse_alpha(false, 800, 1600) == 1.f);
    EF_CHECK(pulse_alpha(true, 0, 1600) == 1.f);
    EF_CHECK(std::fabs(pulse_alpha(true, 800, 1600) - (1.f - alpha_scale::kPulseDim)) < 0.001f);
    EF_CHECK(pulse_alpha(true, 400, 1600) < 1.f && pulse_alpha(true, 400, 1600) > pulse_alpha(true, 800, 1600));
    EF_CHECK(pulse_alpha(true, 800, 0) == 1.f);  // gecersiz dongu: sabit
}
