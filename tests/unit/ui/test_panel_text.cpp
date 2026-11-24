// Panel (spec 2026-11-12 B / D / F): saf bicim ve yerlesim yardimcilari, veri rolu / hareket token'lari, hareket
// kapilari, tasinan kartlarin odak sirasi. Cizim ui_snapshot'ta denetlenir.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/shell/screen.h"
#include "ui/theme/system_palette.h"

using namespace ef::ui;
namespace cm = ef::core::measure;
using BV = ef::core::diagnosis::BalanceVerdict;

namespace {
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
std::ptrdiff_t pos(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) - v.begin(); }
}  // namespace

EF_TEST(panel_perf_side_groups_drop_below_when_narrow) {
    const float d = default_theme().power_button_d;
    EF_CHECK(d == 92.f);
    // Yan grup icin kalan yer = (govde - dugme) / 2 - bosluk; iki grup da sigmali.
    EF_CHECK(panel_perf_side_by_side(1148.f, d, 40.f, 170.f, 190.f));  // 1180 pencere
    EF_CHECK(panel_perf_side_by_side(928.f, d, 40.f, 170.f, 190.f));   // 960 pencere
    EF_CHECK(panel_perf_side_by_side(2 * (190.f + 40.f) + d, d, 40.f, 170.f, 190.f));        // tam sinir
    EF_CHECK(!panel_perf_side_by_side(2 * (190.f + 40.f) + d - 1.f, d, 40.f, 170.f, 190.f)); // bir DIP dar: alta iner
    EF_CHECK(!panel_perf_side_by_side(2 * (190.f + 40.f) + d - 1.f, d, 40.f, 190.f, 170.f)); // sol grup genis olsa da
    EF_CHECK(!panel_perf_side_by_side(468.f, d, 40.f, 170.f, 190.f));  // 500 DIP pencere
    EF_CHECK(!panel_perf_side_by_side(0.f, d, 40.f, 10.f, 10.f));
    // Koyu tema yuva ic boslugu (8 DIP): dis kenarda da yer kalmali; tam sinir 8 DIP daha genis.
    EF_CHECK(panel_perf_side_by_side(2 * (190.f + 40.f + 8.f) + d, d, 40.f, 170.f, 190.f, 8.f));
    EF_CHECK(!panel_perf_side_by_side(2 * (190.f + 40.f + 8.f) + d - 1.f, d, 40.f, 170.f, 190.f, 8.f));
    EF_CHECK(!panel_perf_side_by_side(2 * (190.f + 40.f) + d, d, 40.f, 170.f, 190.f, 8.f));
}

EF_TEST(panel_tile_cpu_core_count_from_inventory_detail) {
    EF_CHECK(cpu_core_count("8C/16T") == 8);
    EF_CHECK(cpu_core_count("24C/24T") == 24);
    EF_CHECK(cpu_core_count("128C/256T") == 128);
    // Bicim taninmiyorsa sayi uydurulmaz.
    EF_CHECK(!cpu_core_count(""));
    EF_CHECK(!cpu_core_count("0C/0T"));
    EF_CHECK(!cpu_core_count("C/16T"));
    EF_CHECK(!cpu_core_count("8 cores"));
    EF_CHECK(!cpu_core_count("16T"));
    EF_CHECK(!cpu_core_count("8"));
}

EF_TEST(panel_tile_ram_name_line) {
    EF_CHECK(ram_tile_name("64 GB · 2×32 GB DDR5 6000 MT/s") == "64 GB DDR5 · 6000 MT/s");
    EF_CHECK(ram_tile_name("32 GB · 4×8 GB DDR4 3200 MT/s") == "32 GB DDR4 · 3200 MT/s");
    // Okunamayan parca yazilmaz: hiz yok, tur yok, modul boyutu yok, ayrinti yok.
    EF_CHECK(ram_tile_name("64 GB · 2×32 GB DDR5") == "64 GB DDR5");
    EF_CHECK(ram_tile_name("64 GB · 2×32 GB 6000 MT/s") == "64 GB · 6000 MT/s");
    EF_CHECK(ram_tile_name("16 GB · 2×? DDR4 2666 MT/s") == "16 GB DDR4 · 2666 MT/s");
    EF_CHECK(ram_tile_name("16 GB · 2×8 GB") == "16 GB");
    EF_CHECK(ram_tile_name("16 GB") == "16 GB");
    EF_CHECK(ram_tile_name("").empty());
}

EF_TEST(panel_tile_display_mode_parts) {
    EF_CHECK(display_mode_text("DELL S2721DGF · 2560×1440 · 165 Hz") == "2560×1440 · 165 Hz");
    EF_CHECK(display_mode_text("2560×1440 · 480 Hz") == "2560×1440 · 480 Hz");  // ekran adi okunamadi
    EF_CHECK(display_mode_text("LG ULTRAGEAR · 3840×2160") == "3840×2160");    // yenileme hizi okunamadi
    EF_CHECK(display_mode_text("Generic PnP · 59.94 Hz") == "59.94 Hz");
    // Ekran adi cozunurluk / hiz gibi gorunse de sayi ile baslamiyorsa girmez; hicbiri yoksa bos.
    EF_CHECK(display_mode_text("Odyssey G9 · 5120×1440 · 240 Hz") == "5120×1440 · 240 Hz");
    EF_CHECK(display_mode_text("DELL S2721DGF").empty());
    EF_CHECK(display_mode_text("").empty());
}

EF_TEST(panel_tile_volume_fill_line_with_plus_n) {
    using ef::core::metrics::VolumeUse;
    const std::vector<VolumeUse> vols = {{"C:\\", 952, 305}, {"D:\\", 931, 130}, {"E:\\", 931, 875}, {"F:\\", 1862, 1662}};
    const auto f = volume_fills(vols);
    EF_CHECK(f.size() == 4);
    EF_CHECK((f[0] == VolumeFill{L"C:", 68}));  // (952 - 305) / 952 = %67,96
    EF_CHECK((f[1] == VolumeFill{L"D:", 86}));
    EF_CHECK((f[2] == VolumeFill{L"E:", 6}));
    EF_CHECK((f[3] == VolumeFill{L"F:", 11}));
    EF_CHECK(volume_fill_list(f, 3, true) == L"C: %68 · D: %86 · E: %6 +1");
    EF_CHECK(volume_fill_list(f, 4, true) == L"C: %68 · D: %86 · E: %6 · F: %11");
    EF_CHECK(volume_fill_list(f, 99, true) == volume_fill_list(f, 4, true));
    EF_CHECK(volume_fill_list(f, 1, true) == L"C: %68 +3");
    EF_CHECK(volume_fill_list(f, 2, false) == L"C: 68% · D: 86% +2");
    EF_CHECK(volume_fill_list(f, 0, true).empty());
    EF_CHECK(volume_fill_list({}, 3, true).empty());
    // Okunamayan birim (toplam / bos alan yok, toplam 0, bos > toplam) listeye girmez; 0 ya da uydurma yuzde yazilmaz.
    const std::vector<VolumeUse> partial = {{"C:\\", 100, 50}, {"D:\\", std::nullopt, 10}, {"E:\\", 100, std::nullopt}, {"F:\\", 0, 0},
                                            {"G:\\", 10, 20},  {"", 10, 5},               {"H:", 200, 0},            {"I:/", 3, 3}};
    const auto p = volume_fills(partial);
    EF_CHECK(p.size() == 3);
    EF_CHECK((p[0] == VolumeFill{L"C:", 50}) && (p[1] == VolumeFill{L"H:", 100}) && (p[2] == VolumeFill{L"I:", 0}));
    EF_CHECK(percent_text(7, true) == L"%7" && percent_text(7, false) == L"7%");
}

// Uyumluluk B6 (bosluk 28): upscaler olcek satiri Turkcede "%67" bicimindedir.
EF_TEST(panel_scale_percents_follow_locale) {
    const std::vector<double> s = {0.667, 0.58, 0.5};
    EF_CHECK(scale_percents_text(s, true) == L"%67 · %58 · %50");
    EF_CHECK(scale_percents_text(s, false) == L"67% · 58% · 50%");
    EF_CHECK(scale_percents_text({}, true).empty());
}

EF_TEST(panel_bottleneck_measured_share) {
    cm::BalanceView v;
    v.cpu_fps = 200.0;
    // CPU sinirliyor: N = round(100 x (1 - birlikte / gpu)) -> ekran kartinin bosta kalan payi.
    v.rows[0] = {BV::CpuLimited, "", 300.0, false, 204.0};
    EF_CHECK((balance_share(v, 0) == BalanceShare{32, false}));
    // GPU sinirliyor: N = round(100 x (1 - birlikte / cpu)) -> islemcide kalan pay.
    v.rows[1] = {BV::GpuLimited, "", 150.0, false, 140.0};
    EF_CHECK((balance_share(v, 1) == BalanceShare{30, false}));
    // Dengeli / belirlenemedi / olculemedi: pay yok.
    v.rows[2] = {BV::Balanced, "", 205.0, false, 198.0};
    EF_CHECK(!balance_share(v, 2));
    v.rows[2] = {BV::Undetermined, "on_battery", 205.0, false, 198.0};
    EF_CHECK(!balance_share(v, 2));
    v.rows[2] = {BV::Unmeasurable, "unknown", std::nullopt, false, std::nullopt};
    EF_CHECK(!balance_share(v, 2));
    EF_CHECK(!balance_share(v, 3));  // satir yok
    // Paydadaki deger alt sinirsa pay "en az"dir.
    v.rows[0] = {BV::CpuLimited, "", 1000.0, true, 200.0};
    EF_CHECK((balance_share(v, 0) == BalanceShare{80, true}));
    v.cpu_lower_bound = true;
    EF_CHECK((balance_share(v, 1) == BalanceShare{30, true}));
    v.cpu_lower_bound = false;
    // Negatif sonuc 0'a kirpilmaz: pay yazilmaz. Eksik / gecersiz deger: pay yok.
    v.rows[0] = {BV::CpuLimited, "", 190.0, false, 204.0};
    EF_CHECK(!balance_share(v, 0));
    v.rows[0] = {BV::CpuLimited, "", std::nullopt, false, 204.0};
    EF_CHECK(!balance_share(v, 0));
    v.rows[0] = {BV::CpuLimited, "", 300.0, false, std::nullopt};
    EF_CHECK(!balance_share(v, 0));
    v.rows[0] = {BV::CpuLimited, "", 0.0, false, 0.0};
    EF_CHECK(!balance_share(v, 0));
    v.rows[0] = {BV::CpuLimited, "", std::numeric_limits<double>::infinity(), false, 100.0};
    EF_CHECK(!balance_share(v, 0));
    v.rows[0] = {BV::CpuLimited, "", 300.0, false, std::numeric_limits<double>::quiet_NaN()};
    EF_CHECK(!balance_share(v, 0));
    v.cpu_fps.reset();
    EF_CHECK(!balance_share(v, 1));  // GPU sinirliyor ama CPU kapasitesi olculmedi
    // Yuvarlama: yarim yukari; tam esitlik %0 (negatif degil: yazilir).
    v.rows[0] = {BV::CpuLimited, "", 200.0, false, 199.0};
    EF_CHECK((balance_share(v, 0) == BalanceShare{1, false}));  // %0,5 -> %1
    v.rows[0] = {BV::CpuLimited, "", 200.0, false, 200.0};
    EF_CHECK((balance_share(v, 0) == BalanceShare{0, false}));
}

EF_TEST(panel_bottleneck_share_wording) {
    // Turkce iyelik eki sayinin okunusuna gore: "ekran kartinin %32'si bosta kaliyor".
    EF_CHECK(tr_possessive_suffix(0) == L"'ı");    // sifir
    EF_CHECK(tr_possessive_suffix(1) == L"'i");    // bir
    EF_CHECK(tr_possessive_suffix(2) == L"'si");   // iki
    EF_CHECK(tr_possessive_suffix(3) == L"'ü");    // uc
    EF_CHECK(tr_possessive_suffix(4) == L"'ü");    // dort
    EF_CHECK(tr_possessive_suffix(5) == L"'i");    // bes
    EF_CHECK(tr_possessive_suffix(6) == L"'sı");   // alti
    EF_CHECK(tr_possessive_suffix(7) == L"'si");   // yedi
    EF_CHECK(tr_possessive_suffix(8) == L"'i");    // sekiz
    EF_CHECK(tr_possessive_suffix(9) == L"'u");    // dokuz
    EF_CHECK(tr_possessive_suffix(10) == L"'u");   // on
    EF_CHECK(tr_possessive_suffix(20) == L"'si");  // yirmi
    EF_CHECK(tr_possessive_suffix(30) == L"'u");   // otuz
    EF_CHECK(tr_possessive_suffix(40) == L"'ı");   // kirk
    EF_CHECK(tr_possessive_suffix(50) == L"'si");  // elli
    EF_CHECK(tr_possessive_suffix(60) == L"'ı");   // altmis
    EF_CHECK(tr_possessive_suffix(70) == L"'i");   // yetmis
    EF_CHECK(tr_possessive_suffix(80) == L"'i");   // seksen
    EF_CHECK(tr_possessive_suffix(90) == L"'ı");   // doksan
    EF_CHECK(tr_possessive_suffix(100) == L"'ü");  // yuz
    EF_CHECK(tr_possessive_suffix(32) == L"'si" && tr_possessive_suffix(86) == L"'sı" && tr_possessive_suffix(13) == L"'ü");
    EF_CHECK(tr_possessive_suffix(-1).empty());
    EF_CHECK(share_text({32, false}, true, true, L"en az {v}") == L"%32'si");
    EF_CHECK(share_text({30, true}, true, true, L"en az {v}") == L"en az %30'u");
    EF_CHECK(share_text({30, false}, true, false, L"en az {v}") == L"%30");  // "islemcide %30 pay": ek yok
    EF_CHECK(share_text({30, true}, true, false, L"en az {v}") == L"en az %30");
    EF_CHECK(share_text({32, false}, false, true, L"at least {v}") == L"32%");  // Ingilizcede ek yok
    EF_CHECK(share_text({32, true}, false, true, L"at least {v}") == L"at least 32%");
    // Kisa hukum sinifi: CPU sinirliyor -> "Islemci darbogazi"; GPU sinirliyor ve Dengeli -> "Darbogaz yok".
    EF_CHECK(bottleneck_word(BV::CpuLimited) == BottleneckWord::Cpu);
    EF_CHECK(bottleneck_word(BV::GpuLimited) == BottleneckWord::None && bottleneck_word(BV::Balanced) == BottleneckWord::None);
    EF_CHECK(bottleneck_word(BV::Undetermined) == BottleneckWord::Other && bottleneck_word(BV::Unmeasurable) == BottleneckWord::Other);
}

EF_TEST(measure_bottleneck_tiles_and_test_card_helpers) {
    // Spec 2026-11-12 C: birincil ekranin cozunurlugune denk gelen kutu vurgulanir; eslesme yoksa vurgu yok.
    EF_CHECK(balance_display_row("DELL S2721DGF \xC2\xB7 2560\xC3\x97" "1440 \xC2\xB7 165 Hz") == std::optional<std::size_t>{1});
    EF_CHECK(balance_display_row("LG 27GL850 \xC2\xB7 1920x1080 \xC2\xB7 144 Hz") == std::optional<std::size_t>{0});
    EF_CHECK(balance_display_row("3840X2160") == std::optional<std::size_t>{2});
    EF_CHECK(!balance_display_row("AW3423 \xC2\xB7 3440\xC3\x97" "1440 \xC2\xB7 175 Hz"));  // ultra genis: denk kutu yok
    EF_CHECK(!balance_display_row("19200x1080") && !balance_display_row("1920 x 1080") && !balance_display_row("") && !balance_display_row("165 Hz"));
    // Uc es kutu: dar kartta alt alta.
    EF_CHECK(bottleneck_tile_columns(3 * kBottleneckTileMinW + 20.f, 10.f) == 3);
    EF_CHECK(bottleneck_tile_columns(3 * kBottleneckTileMinW + 19.f, 10.f) == 1);
    // "kalan N sn": yalniz toplam sure biliniyorsa; yukari yuvarlanir, 0'in altina inmez.
    EF_CHECK(test_remaining_s(60, 0) == std::optional<int>{60} && test_remaining_s(60, 12.2) == std::optional<int>{48});
    EF_CHECK(test_remaining_s(60, 60) == std::optional<int>{0} && test_remaining_s(60, 75) == std::optional<int>{0});
    EF_CHECK(!test_remaining_s(0, 5) && !test_remaining_s(std::numeric_limits<double>::quiet_NaN(), 1));
    // "Son 3 test" acik satiri: yeni test basa eklenince acik ayrinti ayni testi izler; dusen test kapanir.
    const std::vector<std::int64_t> h3{30, 20, 10}, h4{40, 30, 20}, dup{40, 30, 30};
    EF_CHECK(follow_open_row(-1, h3, h4) == -1);
    EF_CHECK(follow_open_row(0, h3, h4) == 1 && follow_open_row(1, h3, h4) == 2);
    EF_CHECK(follow_open_row(2, h3, h4) == -1);  // en eski test diskten silindi: ayrinti kapanir
    EF_CHECK(follow_open_row(2, h3, h3) == 2 && follow_open_row(3, h3, h3) == -1);
    EF_CHECK(follow_open_row(0, h3, {}) == -1 && follow_open_row(0, {}, h3) == -1);
    EF_CHECK(follow_open_row(1, h4, dup) == -1);  // ayni zamanli iki kayit: hangisi belirsiz -> kapali
    // Token'lar: 70 DIP yuvarlak dugme, sayac <= 700 ms, cubuk dolumu 700 ms (design/tokens.json; kodda sabit yok).
    const auto& t = default_theme();
    EF_CHECK(t.test_button_d == 70.f && t.motion.result_count == 700 && t.motion.result_count <= 700 && t.motion.bars_fill == 700);
}

EF_TEST(measure_result_card_mode_never_all_dash_with_record) {
    // Test suruyor: canli sayilar (kayit olsa da).
    EF_CHECK(result_card_mode(true, true, true) == ResultCardMode::Numbers);
    EF_CHECK(result_card_mode(true, false, false) == ResultCardMode::Numbers);
    // En yeni kayit denge testi -> hukum + FPS cifti; Genel / Oyunda Test -> dort sayi; kayit yok -> bos durum.
    EF_CHECK(result_card_mode(false, true, true) == ResultCardMode::Balance);
    EF_CHECK(result_card_mode(false, true, false) == ResultCardMode::Numbers);
    EF_CHECK(result_card_mode(false, false, false) == ResultCardMode::Empty);
}

EF_TEST(measure_result_card_balance_summary_from_record) {
    cm::BalanceView b;
    b.cpu_fps = 212.4;
    b.cpu_lower_bound = false;
    b.rows[0] = {BV::CpuLimited, "", 388.1, true, 208.0};
    b.rows[1] = {BV::GpuLimited, "", 150.2, false, 148.0};
    b.rows[2] = {BV::Undetermined, "on_battery", std::nullopt, false, std::nullopt};
    // Baslik: birincil ekranin cozunurlugu (1440p).
    auto s = balance_result_summary(b, "DELL S2721DGF \xC2\xB7 2560\xC3\x97" "1440 \xC2\xB7 165 Hz");
    EF_CHECK(s.headline == 1 && s.headline_is_display);
    EF_CHECK(s.lines[1].word == BottleneckWord::None && s.lines[1].verdict == BV::GpuLimited);
    // FPS cifti yalniz kayittan: CPU tum satirlarda ayni olcum; GPU satir basina; alt sinir bayragi korunur.
    EF_CHECK(s.lines[0].row == 0 && s.lines[0].word == BottleneckWord::Cpu);
    EF_CHECK(s.lines[0].cpu_fps == std::optional<double>{212.4} && !s.lines[0].cpu_at_least);
    EF_CHECK(s.lines[0].gpu_fps == std::optional<double>{388.1} && s.lines[0].gpu_at_least);
    EF_CHECK(s.lines[1].gpu_fps == std::optional<double>{150.2} && !s.lines[1].gpu_at_least);
    // Olculmeyen deger uydurulmaz (ekranda "—"); "en az" bayragi degersiz kalmaz.
    EF_CHECK(!s.lines[2].gpu_fps && !s.lines[2].gpu_at_least && s.lines[2].word == BottleneckWord::Other);
    // Ekran eslesmiyor: ilk kesin hukum (1080p); 4K ekran: 4K satiri (Belirlenemedi olsa da gercek ekran).
    s = balance_result_summary(b, "AW3423 \xC2\xB7 3440\xC3\x97" "1440 \xC2\xB7 175 Hz");
    EF_CHECK(s.headline == 0 && !s.headline_is_display);
    s = balance_result_summary(b, "3840x2160");
    EF_CHECK(s.headline == 2 && s.headline_is_display);
    // Hicbir satirda kesin hukum yok ve ekran bilinmiyor: 1080p.
    cm::BalanceView u;
    u.rows[0].verdict = u.rows[1].verdict = BV::Undetermined;
    u.rows[2].verdict = BV::Unmeasurable;
    u.rows[1].gpu_fps = 90.0;
    s = balance_result_summary(u, "");
    EF_CHECK(s.headline == 0 && !s.headline_is_display);
    EF_CHECK(!s.lines[0].cpu_fps && s.lines[1].gpu_fps == std::optional<double>{90.0});
    // Gecersiz kayit degeri (NaN / negatif) gosterilmez.
    u.cpu_fps = std::numeric_limits<double>::quiet_NaN();
    u.cpu_lower_bound = true;
    u.rows[0].gpu_fps = -5.0;
    u.rows[0].gpu_lower_bound = true;
    s = balance_result_summary(u, "");
    EF_CHECK(!s.lines[0].cpu_fps && !s.lines[0].cpu_at_least && !s.lines[0].gpu_fps && !s.lines[0].gpu_at_least);
}

EF_TEST(measure_pretest_warning_only_before_start) {
    // Uygulama Testi baslatilabilirken uyari; disk secili karisik yukte birlesik disk uyarisi.
    EF_CHECK(pretest_warning(true, false, true, false, false, false) == PretestWarning::Stress);
    EF_CHECK(pretest_warning(true, false, true, false, false, true) == PretestWarning::StressDisk);
    // Test surerken gizli; kullanilamiyor / uzun test kilidi / disk riski: neden notu yeterli, uyari yok.
    EF_CHECK(pretest_warning(true, true, true, false, false, false) == PretestWarning::None);
    EF_CHECK(pretest_warning(true, false, false, false, false, false) == PretestWarning::None);
    EF_CHECK(pretest_warning(true, false, true, true, false, false) == PretestWarning::None);
    EF_CHECK(pretest_warning(true, false, true, false, true, true) == PretestWarning::None);
    // Oyunda Test oyunu yalniz izler: uyari yok.
    EF_CHECK(pretest_warning(false, false, true, false, false, false) == PretestWarning::None);
}

EF_TEST(panel_data_role_and_motion_tokens_present) {
    const auto& t = default_theme();
    const auto& p = t.dark;
    // Spec F: adli veri rolu token'lari (kodda sabit renk yok).
    EF_CHECK(p.data_cpu == *Color::from_hex("#FFB020"));       // CPU serileri: kehribar
    EF_CHECK(p.data_gpu == *Color::from_hex("#00D4FF"));       // GPU serileri / kare hizi: camgobegi
    EF_CHECK(p.data_net_down == *Color::from_hex("#00D4FF"));  // ag indirme: camgobegi
    EF_CHECK(p.data_net_up == *Color::from_hex("#B266FF"));    // ag yukleme: mor
    EF_CHECK(p.data_info == *Color::from_hex("#8A8A8A"));      // notr bilgi: gri
    // Yesil yalniz marka / ana dugme / "iyi": veri rolleri vurgu renginden farklidir.
    for (const Color* c : {&p.data_cpu, &p.data_gpu, &p.data_net_down, &p.data_net_up, &p.data_info}) {
        EF_CHECK(!(*c == p.accent));
        for (const Color* bg : {&p.background, &p.surface, &p.surface_raised}) EF_CHECK(contrast_ratio(*c, *bg) >= 4.5);  // metin
    }
    // Hareket: deger gecisi 250 ms, sayac <= 300 ms, nefes dongusu; yuvarlak dugme 92 DIP.
    EF_CHECK(t.motion.value_ease == 250);
    EF_CHECK(t.motion.count > 0 && t.motion.count <= 300);
    EF_CHECK(t.motion.breathe_period >= 1000);
    EF_CHECK(t.motion.heartbeat_period == 1500);  // spec B.1: 1,5 sn'de cift vurus
    EF_CHECK(t.power_button_d == 92.f && t.power_button_d > t.control_max_h);
    // Token yoksa mevcut renklere duser; varsa gecerli olmali.
    const char* base = R"({"colors":{"background":"#000000","sidebar":"#000000","surface":"#000000","surface_raised":"#000000",
        "border":"#000000","text_primary":"#FFFFFF","text_secondary":"#AAAAAA","accent":"#112233","on_accent":"#000000",
        "violet":"#445566","success":"#000000","warning":"#778899","error":"#000000","cyan":"#00AACC"%s},
        "type":{"page_title":[26,34,"Semibold"],"state_title":[22,30,"Semibold"],"section":[18,26,"Semibold"],
        "body":[14,20,"Regular"],"secondary":[12,18,"Regular"],"metric":[28,34,"Semibold"]},
        "motion_ms":{"count":%s}})";
    const auto make = [&](const char* extra, const char* count) {
        char buf[2048];
        std::snprintf(buf, sizeof(buf), base, extra, count);
        return parse_theme(buf);
    };
    const auto a = make("", "200");
    EF_CHECK(a.ok());
    if (a.ok()) {
        const auto& q = a.value().dark;
        EF_CHECK(q.data_cpu == q.warning && q.data_gpu == q.cyan && q.data_net_down == q.cyan && q.data_net_up == q.violet &&
                  q.data_info == q.text_secondary);
        EF_CHECK(a.value().motion.count == 200);
    }
    const auto b = make(R"(,"data_cpu":"#010203","data_net_up":"#040506")", "900");  // sayac 300 ms'yi asamaz -> varsayilan
    EF_CHECK(b.ok() && b.value().dark.data_cpu == *Color::from_hex("#010203") && b.value().dark.data_net_up == *Color::from_hex("#040506") &&
              b.value().motion.count == 300);
    EF_CHECK(!make(R"(,"data_gpu":"cyan")", "200").ok());
    // Yuksek kontrast: seri rengi yok; sayilar sistem metin rengi, notr bilgi GRAYTEXT.
    SystemColorTable c;
    c.window = *Color::from_hex("#000000");
    c.button_face = *Color::from_hex("#1F1F1F");
    c.window_text = *Color::from_hex("#FFFFFF");
    c.highlight = *Color::from_hex("#1AEBFF");
    c.highlight_text = *Color::from_hex("#000000");
    c.hotlight = *Color::from_hex("#FFFF00");
    c.gray_text = *Color::from_hex("#3FF23F");
    const Palette hc = high_contrast_palette_from(c);
    EF_CHECK(hc.data_cpu == c.window_text && hc.data_gpu == c.window_text && hc.data_net_down == c.window_text && hc.data_net_up == c.window_text);
    EF_CHECK(hc.data_info == c.gray_text);
}

EF_TEST(panel_strings_are_cataloged) {
    using ef::core::i18n::StringId;
    for (const auto loc : {ef::core::i18n::Locale::TrTR, ef::core::i18n::Locale::EnUS}) {
        auto cat = ef::core::i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const StringId id :
             {StringId::NavSystem, StringId::PanelPerfOnLine, StringId::PanelTest, StringId::PanelFix, StringId::PanelBottleneckTitle,
              StringId::PanelBottleneckScope, StringId::PanelBottleneckCpu, StringId::PanelBottleneckNone, StringId::PanelBottleneckCpuShare,
              StringId::PanelBottleneckGpuShare, StringId::PanelBottleneckBalanced, StringId::PanelBottleneckAtLeast,
              StringId::PanelBottleneckEmpty, StringId::PanelReadinessCounts, StringId::PanelReadinessEmpty, StringId::LiveCpuCores,
              StringId::LiveCpuBusiest, StringId::LiveRamInUse, StringId::LiveVolFill, StringId::LiveVolFillA11y, StringId::LiveNetLinkName,
              StringId::LiveNetUpLine, StringId::LiveNetUpA11y})
            EF_CHECK(!cat.value().get(id).empty());
        // Kaliplar yer tutucularini tasir (bicimleyici onlari doldurur).
        const auto has_key = [&](StringId id, std::string_view key) { return cat.value().get(id).find(key) != std::string_view::npos; };
        EF_CHECK(has_key(StringId::PanelPerfOnLine, "{count}") && has_key(StringId::PanelBottleneckCpuShare, "{share}") &&
                  has_key(StringId::PanelBottleneckGpuShare, "{share}") && has_key(StringId::PanelBottleneckAtLeast, "{v}") &&
                  has_key(StringId::LiveVolFill, "{list}") && has_key(StringId::LiveCpuCores, "{n}") && has_key(StringId::LiveNetLinkName, "{link}"));
    }
}

EF_TEST(panel_motion_gates_follow_strip_rules) {
    StripInputs all;
    all.visible = all.foreground = true;
    all.app_animations = true;
    all.strip_setting = false;  // RGB serit ayari nefes halkasini etkilemez ("Animasyonlar" anahtari etkiler)
    EF_CHECK(breathe_should_animate(all, true));
    EF_CHECK(!breathe_should_animate(all, false));  // dugme etkin degil (is / onay suruyor) ya da gorunur govdede degil
    bool StripInputs::*stops[] = {&StripInputs::visible,      &StripInputs::foreground,     &StripInputs::game_blocked,
                                  &StripInputs::test_running, &StripInputs::on_battery,     &StripInputs::reduced_motion,
                                  &StripInputs::app_animations, &StripInputs::high_contrast};
    for (auto f : stops) {
        StripInputs in = all;
        in.*f = !(in.*f);
        EF_CHECK(!breathe_should_animate(in, true));
    }
    EF_CHECK(!breathe_should_animate(StripInputs{}, true));
    // Deger gecisleri: pencere gorunmuyor, oyun acik, test / olcum suruyor ya da pilde -> duraklar (aninda oturur).
    EF_CHECK(!motion_paused_for(all));
    for (auto f : {&StripInputs::visible, &StripInputs::game_blocked, &StripInputs::test_running, &StripInputs::on_battery}) {
        StripInputs in = all;
        in.*f = !(in.*f);
        EF_CHECK(motion_paused_for(in));
    }
    {
        StripInputs in = all;
        in.foreground = false;  // arka plandaki gorunur pencerede degerler yine yumusak gecer
        EF_CHECK(!motion_paused_for(in));
    }
    // Nefes dalgasi: 0 -> 1 -> 0, [0,1] araliginda; dongu basinda ve sonunda 0.
    const int period = default_theme().motion.breathe_period;
    EF_CHECK(std::fabs(breathe_wave(0, period)) < 1e-4f);
    EF_CHECK(std::fabs(breathe_wave(period / 2, period) - 1.f) < 1e-4f);
    EF_CHECK(std::fabs(breathe_wave(period, period)) < 1e-4f);
    for (int ms = 0; ms < 2 * period; ms += 37) {
        const float w = breathe_wave(ms, period);
        EF_CHECK(w >= 0.f && w <= 1.f);
    }
    EF_CHECK(breathe_wave(1234, 0) == 0.f);
}

EF_TEST(panel_heartbeat_is_a_double_beat_then_rest) {
    // Spec 2026-11-12 B.1: Performans kapaliyken 1,5 sn'de cift vurus — olcek 1 -> 1,10 -> 1 -> 1,07 -> 1 — ve disa
    // acilip sonen halka; dongunun kalani dinlenme (kare duragan: zamanlayici dugmeyi yeniden cizdirmez).
    const int period = default_theme().motion.heartbeat_period;
    const auto at = [&](float phase) { return heartbeat_frame(static_cast<std::int64_t>(phase * static_cast<float>(period) + 0.5f), period); };
    EF_CHECK(std::fabs(at(0.f).scale - 1.f) < 1e-4f && std::fabs(at(0.f).ring) < 1e-4f);
    EF_CHECK(std::fabs(at(kHeartbeatBeat / 2).scale - 1.10f) < 1e-3f);                     // birinci vurus tepesi
    EF_CHECK(std::fabs(at(kHeartbeatBeat).scale - 1.f) < 1e-3f);                           // arada 1'e doner
    EF_CHECK(std::fabs(at((kHeartbeatBeat + kHeartbeatSecond) / 2).scale - 1.f) < 1e-4f);
    EF_CHECK(std::fabs(at(kHeartbeatSecond + kHeartbeatBeat / 2).scale - 1.07f) < 1e-3f);  // ikinci vurus tepesi (daha kucuk)
    EF_CHECK(std::fabs(at(kHeartbeatSecond + kHeartbeatBeat).scale - 1.f) < 1e-3f);
    // Halka vurusla baslar, disa dogru ilerler ve soner; dinlenmede yoktur.
    EF_CHECK(at(0.1f).ring > 0.f && at(0.1f).ring < at(0.3f).ring && at(0.3f).ring < 1.f);
    EF_CHECK(at(kHeartbeatRing + 0.01f).ring == 1.f);
    float peak = 1.f;
    for (int ms = 0; ms < 2 * period; ms += 7) {
        const auto f = heartbeat_frame(ms, period);
        EF_CHECK(f.scale >= 1.f - 1e-4f && f.scale <= 1.10f + 1e-4f && f.ring >= 0.f && f.ring <= 1.f);
        peak = std::max(peak, f.scale);
        // Dinlenme = duragan kare (olcek 1, halka yok).
        if (heartbeat_resting(ms, period)) EF_CHECK(f.scale == 1.f && f.ring == 1.f);
        else EF_CHECK(f.ring < 1.f);
    }
    EF_CHECK(peak > 1.09f);
    EF_CHECK(!heartbeat_resting(0, period) && !heartbeat_resting(period / 4, period) && heartbeat_resting(period * 3 / 4, period));
    EF_CHECK(heartbeat_frame(period + 5, period).scale == heartbeat_frame(5, period).scale);  // dongu
    // Gecersiz dongu: dinlenme (hareket yok).
    EF_CHECK(heartbeat_frame(1234, 0).scale == 1.f && heartbeat_frame(1234, 0).ring == 1.f && heartbeat_resting(1234, 0));
}

EF_TEST(panel_counters_restart_on_page_entry_and_tiles_animate_separately) {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    UiState s;
    s.reduced_motion = true;
    s.panel_num_seen = {true, true, true};
    s.readiness_fill_seen = true;
    screen.go(s, Page::Games, 1000);
    EF_CHECK(s.panel_num_seen[0] && s.readiness_fill_seen);  // baska sayfaya gecis sayaclari sifirlamaz
    screen.go(s, Page::Overview, 2000);
    EF_CHECK(!s.panel_num_seen[0] && !s.panel_num_seen[1] && !s.panel_num_seen[2] && !s.readiness_fill_seen);
    // Sayac / cubuk gecisi tam kare ister; canli kutucuk gecisi ayri sayilir (yalniz kutucuk bolgesi yeniden cizilir).
    UiState n;
    EF_CHECK(!screen.animating(n, 5000) && !screen.live_animating(n, 5000));
    n.live_value[0].retarget(50.f, 5000, theme.motion.value_ease);
    EF_CHECK(screen.animating(n, 5010) && screen.live_animating(n, 5010) && !screen.animating(n, 5010, false));
    EF_CHECK(!screen.animating(n, 5000 + theme.motion.value_ease + 1));
    n.panel_num[0].retarget(240.f, 6000, theme.motion.count);
    EF_CHECK(screen.animating(n, 6010, false) && !screen.live_animating(n, 6010));
    EF_CHECK(!screen.animating(n, 6000 + theme.motion.count + 1));
    n.readiness_fill = Tween(0);
    n.readiness_fill.retarget(1.f, 7000, theme.motion.value_ease);
    EF_CHECK(screen.animating(n, 7010, false));
    EF_CHECK(!screen.animating(n, 7000 + theme.motion.value_ease + 1));
}

EF_TEST(panel_moved_cards_keep_ids_and_focus_order) {
    using ef::core::perf::CheckPhase;
    // Tek tik kontrol (OPT-07) Oyun hazirligi sayfasina tasindi: kimlikler ayni, odak sirasi kartla birlikte.
    EF_CHECK(hit_automation_id(Hit::CheckStart) == L"check.start" && hit_automation_id(Hit::CheckYes) == L"check.yes" &&
              hit_automation_id(Hit::CheckNo) == L"check.no" && hit_automation_id(hit_of(HitKind::CheckToggle, 2)) == L"check.item.2");
    UiState s;
    s.page = Page::Overview;
    s.check_phase = CheckPhase::Idle;
    auto o = focus_order(s);
    EF_CHECK(!has(o, Hit::CheckStart));
    // Panel: yuvarlak dugme -> Ayrinti -> "Testi baslat" (etkinse) -> "Test et" -> "Duzelt".
    EF_CHECK(has(o, Hit::Toggle) && pos(o, Hit::Toggle) < pos(o, Hit::Details) && pos(o, Hit::Details) < pos(o, Hit::OpenMeasure) &&
              pos(o, Hit::OpenMeasure) < pos(o, Hit::OpenReadiness) && o.back() == Hit::OpenReadiness);
    s.page = Page::Readiness;
    s.readiness_actions = {true, false};
    o = focus_order(s);
    EF_CHECK(has(o, Hit::CheckStart) && pos(o, hit_of(HitKind::ChkAction, 0)) < pos(o, Hit::CheckStart) && o.back() == Hit::CheckStart);
    s.check_phase = CheckPhase::Review;
    s.check_toggles = {true, false, true};
    o = focus_order(s);
    EF_CHECK(!has(o, Hit::CheckStart) && has(o, hit_of(HitKind::CheckToggle, 0)) && !has(o, hit_of(HitKind::CheckToggle, 1)) &&
              has(o, hit_of(HitKind::CheckToggle, 2)) && pos(o, Hit::CheckYes) + 1 == pos(o, Hit::CheckNo) && o.back() == Hit::CheckNo);
    s.game_blocked = true;  // oyun kilidi: Evet yok, Hayir kalir
    o = focus_order(s);
    EF_CHECK(!has(o, Hit::CheckYes) && has(o, Hit::CheckNo));
    s.game_blocked = false;
    s.check_phase = CheckPhase::Applying;
    o = focus_order(s);
    EF_CHECK(!has(o, Hit::CheckStart) && !has(o, Hit::CheckYes) && !has(o, Hit::CheckNo));

    // Sistem araclari: "Sistem" ilk alt sekme (Panel'den tasinan Sistem karti + Depolama); ust menude ayri sekme degil.
    const Hit system = nav_hit(static_cast<std::size_t>(Page::System));
    EF_CHECK(hit_automation_id(system) == L"nav.system");
    EF_CHECK(std::find(kNavOrder.begin(), kNavOrder.end(), Page::System) == kNavOrder.end());
    EF_CHECK(systools_page(Page::System) && systools_page(Page::Cleanup) && systools_page(Page::Startup) && !systools_page(Page::Overview));
    EF_CHECK(nav_tab_selected(Page::Cleanup, Page::System) && !nav_tab_selected(Page::Overview, Page::System));
    EF_CHECK(systools_subtab_selected(system, Page::System) && !systools_subtab_selected(system, Page::Cleanup) &&
              !systools_subtab_selected(Hit::SysTabCleanup, Page::System));
    const auto tabs = systools_tab_hits();
    EF_CHECK(tabs[0] == system && tabs[1] == Hit::SysTabCleanup && tabs[2] == nav_hit(static_cast<std::size_t>(Page::Startup)));
    for (const Page pg : {Page::System, Page::Cleanup, Page::Startup}) {
        UiState t;
        t.page = pg;
        const auto order = focus_order(t);
        // Spec 2026-11-12 B4: alt sekmeler cizilmez ve Tab sirasinda yoktur (kimlikler korunur).
        for (std::size_t i = 0; i < kSysToolsTabCount; ++i) EF_CHECK(!has(order, tabs[i]));
    }
    UiState p;
    p.page = Page::Overview;
    EF_CHECK(!has(focus_order(p), system));
}
