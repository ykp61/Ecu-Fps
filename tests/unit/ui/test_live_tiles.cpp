// Siki yerlesim (spec 2026-11-11 §4): Panel canli kutucuklari ve Sistem karti — saf mantik (izgara sutun sayisi, ag
// hizi MB/s metni, baglanti hizi metni, envanter metni bolme). Cizgi grafik / ornek gecmisi kaldirildi.
#include <cmath>
#include <limits>

#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

EF_TEST(live_tile_grid_columns) {
    const float gap = default_theme().card_gap;  // 10
    // Tek sirada bes esit sutun: kutucuk >= 170 DIP.
    EF_CHECK(live_tile_columns(1148.f, gap) == 5);  // 1180 pencere
    EF_CHECK(live_tile_columns(928.f, gap) == 5);   // asgari pencere (960): 177,6 DIP
    EF_CHECK(live_tile_columns(5 * kLiveTileMinW + 4 * gap, gap) == 5);
    // Kutucuk 170'in altina dusuyorsa 3 + 2.
    EF_CHECK(live_tile_columns(5 * kLiveTileMinW + 4 * gap - 1.f, gap) == 3);
    EF_CHECK(live_tile_columns(3 * kLiveTileMinW + 2 * gap, gap) == 3);
    // O da darsa tek sutun.
    EF_CHECK(live_tile_columns(3 * kLiveTileMinW + 2 * gap - 1.f, gap) == 1);
    EF_CHECK(live_tile_columns(0.f, gap) == 1);
    static_assert(kLiveTileCount == 5);
}

EF_TEST(net_value_is_megabytes_per_second) {
    // Deger = Mbit/s / 8, her zaman bir ondalik.
    EF_CHECK(net_mbs_text(1000.0, true) == L"125,0");
    EF_CHECK(net_mbs_text(1000.0, false) == L"125.0");
    EF_CHECK(net_mbs_text(912.4, true) == L"114,1");  // 114,05 -> yarim yukari
    EF_CHECK(net_mbs_text(48.25, true) == L"6,0");
    EF_CHECK(net_mbs_text(0.0, true) == L"0,0");
    EF_CHECK(net_mbs_text(8.0, false) == L"1.0");
    EF_CHECK(net_mbs_text(74.0, true) == L"9,3");  // 9,25 -> 9,3 (printf 9,2 yazardi)
    EF_CHECK(net_mbs_text(10000.0, true) == L"1250,0");
    // Gecersiz deger sayi uretmez.
    EF_CHECK(net_mbs_text(-1.0, true) == L"—");
    EF_CHECK(net_mbs_text(std::numeric_limits<double>::infinity(), true) == L"—");
    EF_CHECK(net_mbs_text(std::numeric_limits<double>::quiet_NaN(), false) == L"—");
}

EF_TEST(net_link_text_units) {
    const auto mbps100 = net_link_text(100.0, true);
    EF_CHECK(mbps100.value == L"100" && !mbps100.gbps);
    const auto mbps866 = net_link_text(866.7, true);
    EF_CHECK(mbps866.value == L"867" && !mbps866.gbps);
    const auto g1 = net_link_text(1000.0, true);
    EF_CHECK(g1.value == L"1" && g1.gbps);
    const auto g25 = net_link_text(2500.0, true);
    EF_CHECK(g25.value == L"2,5" && g25.gbps);
    EF_CHECK(net_link_text(2500.0, false).value == L"2.5");
    const auto g10 = net_link_text(10000.0, false);
    EF_CHECK(g10.value == L"10" && g10.gbps);
    // 999,6 Mbit/s 1 Gbps'e yuvarlanir ("1000 Mbps" yazilmaz).
    const auto edge = net_link_text(999.6, true);
    EF_CHECK(edge.value == L"1" && edge.gbps);
    // Bilinmiyor / gecersiz: bos deger (cagiran "—" yazar).
    EF_CHECK(net_link_text(0.0, true).value.empty());
    EF_CHECK(net_link_text(-5.0, true).value.empty());
    EF_CHECK(net_link_text(std::numeric_limits<double>::quiet_NaN(), true).value.empty());
    EF_CHECK(net_link_text(std::numeric_limits<double>::infinity(), true).value.empty());
}

EF_TEST(system_cell_split) {
    // Envanter metni: ilk " · " ayiricisina kadar ana satir, kalani ayrinti.
    EF_CHECK((sys_cell_split("AMD Ryzen 7 9800X3D 8-Core Processor · 8C/16T") == SysCell{"AMD Ryzen 7 9800X3D 8-Core Processor", "8C/16T"}));
    EF_CHECK((sys_cell_split("64 GB · 2×32 GB DDR5 6000 MT/s") == SysCell{"64 GB", "2×32 GB DDR5 6000 MT/s"}));
    // Ikinci GPU ayrintiya eklenir.
    EF_CHECK((sys_cell_split("NVIDIA GeForce RTX 5080 · 16 GB\nAMD Radeon(TM) Graphics") ==
               SysCell{"NVIDIA GeForce RTX 5080", "16 GB · AMD Radeon(TM) Graphics"}));
    EF_CHECK((sys_cell_split("Intel(R) UHD Graphics\nNVIDIA GeForce RTX 4060 · 8 GB") ==
               SysCell{"Intel(R) UHD Graphics", "NVIDIA GeForce RTX 4060 · 8 GB"}));
    // Ekran ozeti: ad, cozunurluk, yenileme.
    EF_CHECK((sys_cell_split("DELL S2721DGF · 2560×1440 · 165 Hz") == SysCell{"DELL S2721DGF", "2560×1440 · 165 Hz"}));
    // Ayirici yok: yalniz ana satir. Bos: okunamadi (ana satir bos).
    EF_CHECK((sys_cell_split("16 GB") == SysCell{"16 GB", ""}));
    EF_CHECK((sys_cell_split("") == SysCell{}));
}

EF_TEST(system_os_split) {
    EF_CHECK((sys_os_split("Windows 11 Pro 26H2 (26300.9711)") == SysCell{"Windows 11 Pro 26H2", "26300.9711"}));
    EF_CHECK((sys_os_split("Windows 11 Home") == SysCell{"Windows 11 Home", ""}));
    EF_CHECK((sys_os_split("") == SysCell{}));
    EF_CHECK((sys_os_split("(26300)") == SysCell{"(26300)", ""}));  // ad yoksa bolunmez
}

EF_TEST(card_signature_border) {
    // Koyu tema kart cizgisi token degil, turetilmis renktir: metin disi sinir esigi (>= 3:1) kart yuzeyine ve arka
    // plana karsi turetilmis renk uzerinde denetlenir (yalniz border token'i degil).
    const auto& d = default_theme().dark;
    const Color stroke = signature_card_border(d);
    EF_CHECK(contrast_ratio(stroke, d.surface) >= 3.0);
    EF_CHECK(contrast_ratio(stroke, d.background) >= 3.0);
    // BOOST kart gradyaninin iki ucu (opak karsiligi): kenar cizgisi >= 3:1, metin rolleri >= 4.5:1 her iki uca karsi.
    for (const Color face : {depth::card_top(d), depth::card_bottom(d)}) {
        EF_CHECK(face.a == 1.f);
        EF_CHECK(contrast_ratio(stroke, face) >= 3.0);
        for (const Color& txt : {d.text_primary, d.text_secondary, d.accent, d.warning, d.error, d.cyan})
            EF_CHECK(contrast_ratio(txt, face) >= 4.5);
    }
    // Yuva zemini (rgba(4,6,10,.82)) gradyanin acik ucu uzerinde bile metin >= 4.5:1.
    const Color well = depth::over(depth::card_top(d), depth::kWellFill);
    EF_CHECK(contrast_ratio(d.text_secondary, well) >= 4.5);
    // Birincil dugme gradyani: etiket (on_accent) her iki uca karsi >= 4.5:1.
    EF_CHECK(contrast_ratio(d.on_accent, depth::kBtnTop) >= 4.5);
    EF_CHECK(contrast_ratio(d.on_accent, depth::kBtnBottom) >= 4.5);
}
