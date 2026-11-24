// Esit boylu kart satirlari (spec 2026-11-12 §1-§2): saf equal_row_frames / equal_widths ve onlari kullanan EqualRow /
// CardFlow. Kart islevleri sahte CardFn'lerdir: olcum gecisinin cizim yapmadigi, yan yana kartlarin ayni ust kenar ve
// yuksekligi aldigi, alt yiginda artan boslugun son karta gittigi ve dar modda dogal boylarin korundugu dogrulanir.
// Gercek cizim ui_snapshot'ta (ROW_UNEQUAL / ORPHAN_CARD) denetlenir.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;
namespace cr = ef::core::readiness;

namespace {
bool same(const RectF& r, float x, float y, float w, float h) {
    return std::fabs(r.x - x) < 0.01f && std::fabs(r.y - y) < 0.01f && std::fabs(r.w - w) < 0.01f && std::fabs(r.h - h) < 0.01f;
}
// Sahte kart: olcum ve cizim gecislerini sayar; cizimde aldigi cerceveyi ve cizim sirasini kaydeder.
struct Probe {
    float natural = 0;
    int measured = 0, drawn = 0, order = -1;
    RectF frame;
};
CardFn card(Probe& p, int* counter = nullptr) {
    return [&p, counter](CardSlot& s) {
        if (p.natural <= 0) return;  // kart yok: place() cagrilmaz
        RectF r;
        if (!s.place(p.natural, r)) {
            ++p.measured;
            return;
        }
        ++p.drawn;
        p.frame = r;
        if (counter) p.order = (*counter)++;
    };
}
}  // namespace

EF_TEST(equal_row_frames_cells_share_top_and_height) {
    // Panel satiri (1180 pencere): "ne sinirliyor" 140, hazirlik ozeti 104 -> ikisi de 140, ayni ust kenar.
    const auto f = equal_row_frames({{140.f}, {104.f}}, {569.f, 569.f}, 10.f);
    EF_CHECK(f.height == 140.f);
    EF_CHECK(f.cells.size() == 2 && f.cards.size() == 2);
    EF_CHECK(same(f.cells[0], 0, 0, 569, 140) && same(f.cells[1], 579, 0, 569, 140));
    EF_CHECK(same(f.cards[0][0], 0, 0, 569, 140) && same(f.cards[1][0], 579, 0, 569, 140));
    // Uc hucre: hepsi en uzun hucrenin boyunda; x'ler genislik + bosluk ile ilerler.
    const auto t = equal_row_frames({{244.f}, {180.f}, {230.f}}, {376.f, 376.f, 376.f}, 10.f);
    EF_CHECK(t.height == 244.f);
    for (std::size_t c = 0; c < 3; ++c) EF_CHECK(same(t.cells[c], static_cast<float>(c) * 386.f, 0, 376, 244) && t.cards[c][0].h == 244.f);
    // Bos giris: sifir yukseklik.
    EF_CHECK(equal_row_frames({}, {}, 10.f).height == 0.f);
}

EF_TEST(equal_row_frames_sub_stack_slack_goes_to_last_card) {
    // Alt yigin kisa: kartlar + aralarindaki bosluk satir yuksekligine tamamlanir; artan bosluk SON karta verilir.
    const auto f = equal_row_frames({{400.f}, {186.f, 142.f}}, {300.f, 300.f}, 10.f);
    EF_CHECK(f.height == 400.f);
    EF_CHECK(same(f.cards[1][0], 310, 0, 300, 186));
    EF_CHECK(same(f.cards[1][1], 310, 196, 300, 204));
    EF_CHECK(f.cards[1][0].h + 10.f + f.cards[1][1].h == f.height);
    EF_CHECK(same(f.cells[0], 0, 0, 300, 400) && same(f.cells[1], 310, 0, 300, 400));
    // Alt yigin uzun: tek kart yiginin toplam boyuna uzar (186 + 10 + 142 = 338).
    const auto g = equal_row_frames({{182.f}, {186.f, 142.f}}, {300.f, 300.f}, 10.f);
    EF_CHECK(g.height == 338.f && g.cards[0][0].h == 338.f && g.cards[1][0].h == 186.f && g.cards[1][1].h == 142.f);
    // Olmayan kart (<= 0): cerceve yok, bosluk ayrilmaz; artan bosluk var olan son karta.
    const auto h = equal_row_frames({{100.f}, {0.f, 60.f, -1.f}}, {300.f, 300.f}, 10.f);
    EF_CHECK(h.height == 100.f && h.cards[1][0].h == 0.f && h.cards[1][2].h == 0.f);
    EF_CHECK(same(h.cards[1][1], 310, 0, 300, 100));
    // Karti olmayan hucre: yuksekligi 0 (cagiran satirdan dusurur).
    const auto e = equal_row_frames({{100.f}, {}}, {300.f, 300.f}, 10.f);
    EF_CHECK(e.height == 100.f && e.cells[1].h == 0.f && e.cells[0].h == 100.f);
}

EF_TEST(equal_row_frames_stacked_keeps_natural_heights) {
    // Dar mod: tek sutun, okuma sirasi, dogal boylar; uzatma yok.
    const auto f = equal_row_frames({{140.f}, {104.f, 0.f, 50.f}}, {500.f, 500.f}, 10.f, true);
    EF_CHECK(same(f.cards[0][0], 0, 0, 500, 140));
    EF_CHECK(same(f.cards[1][0], 0, 150, 500, 104));
    EF_CHECK(f.cards[1][1].h == 0.f);
    EF_CHECK(same(f.cards[1][2], 0, 264, 500, 50));
    EF_CHECK(f.height == 314.f);
    EF_CHECK(same(f.cells[0], 0, 0, 500, 140) && same(f.cells[1], 0, 150, 500, 164));
    // Ilk hucre bos: bosluk basa eklenmez.
    const auto g = equal_row_frames({{0.f}, {80.f}}, {500.f, 500.f}, 10.f, true);
    EF_CHECK(g.height == 80.f && same(g.cards[1][0], 0, 0, 500, 80) && g.cells[0].h == 0.f);
}

EF_TEST(equal_widths_fill_the_row_on_whole_dips) {
    EF_CHECK((equal_widths(1148.f, 2, 10.f) == std::vector<float>{569.f, 569.f}));
    EF_CHECK((equal_widths(1148.f, 3, 10.f) == std::vector<float>{376.f, 376.f, 376.f}));
    // Bolunmeyen genislik: sutunlar tam DIP, artan piksel son sutunda; toplam degismez.
    EF_CHECK((equal_widths(929.f, 2, 10.f) == std::vector<float>{459.f, 460.f}));
    EF_CHECK((equal_widths(1000.f, 3, 10.f) == std::vector<float>{326.f, 326.f, 328.f}));
    EF_CHECK((equal_widths(700.f, 1, 10.f) == std::vector<float>{700.f}));
    EF_CHECK(equal_widths(700.f, 0, 10.f).empty());
    EF_CHECK((equal_widths(5.f, 2, 10.f) == std::vector<float>{0.f, 0.f}));  // negatif genislik uretilmez
}

EF_TEST(equal_row_measures_without_drawing_then_draws_at_row_height) {
    Probe a{140.f}, b{104.f};
    RowLog log;
    EqualRow row;
    row.area = {16, 100, 1148, 0};
    row.gap = 10;
    row.log = &log;
    row.cells = {{card(a)}, {card(b)}};
    EF_CHECK(row.run() == 140.f);
    // Her kart bir kez olculur (cizimsiz), bir kez cizilir; ikisi de ayni ust kenarda ve satir yuksekliginde.
    EF_CHECK(a.measured == 1 && a.drawn == 1 && b.measured == 1 && b.drawn == 1);
    EF_CHECK(same(a.frame, 16, 100, 569, 140) && same(b.frame, 595, 100, 569, 140));
    EF_CHECK(log.size() == 1 && log[0].size() == 2 && same(log[0][0], 16, 100, 569, 140) && same(log[0][1], 595, 100, 569, 140));

    // Alt yigin: sagdaki iki kart alt alta; artan bosluk son karta.
    Probe big{400.f}, s1{186.f}, s2{142.f};
    log.clear();
    row.cells = {{card(big)}, {card(s1), card(s2)}};
    EF_CHECK(row.run() == 400.f);
    EF_CHECK(same(big.frame, 16, 100, 569, 400) && same(s1.frame, 595, 100, 569, 186) && same(s2.frame, 595, 296, 569, 204));
    EF_CHECK(log.size() == 1 && same(log[0][1], 595, 100, 569, 400));

    // Dar pencere: tek sutun, dogal boy, tam genislik; satir kaydi yok.
    Probe n1{140.f}, n2{104.f};
    log.clear();
    row.stacked = true;
    row.cells = {{card(n1)}, {card(n2)}};
    EF_CHECK(row.run() == 254.f);
    EF_CHECK(same(n1.frame, 16, 100, 1148, 140) && same(n2.frame, 16, 250, 1148, 104));
    EF_CHECK(log.empty());
    row.stacked = false;

    // Karti olmayan hucre satirdan duser: kalan kart tam genislikte, dogal boyunda; satir kaydi yok.
    Probe gone{0.f}, alone{90.f};
    row.cells = {{card(gone)}, {card(alone)}};
    EF_CHECK(row.run() == 90.f);
    EF_CHECK(gone.drawn == 0 && alone.drawn == 1 && same(alone.frame, 16, 100, 1148, 90));
    EF_CHECK(log.empty());
    // Hic kart yok: yukseklik 0.
    Probe g1{0.f}, g2{0.f};
    row.cells = {{card(g1)}, {card(g2)}};
    EF_CHECK(row.run() == 0.f);
}

EF_TEST(card_slot_reports_natural_height_and_stretches_only_when_drawing) {
    CardSlot m;
    m.area = {0, 0, 300, 0};
    m.measuring = true;
    RectF out{1, 2, 3, 4};
    EF_CHECK(!m.place(120.f, out));           // olcum gecisi: cerceve verilmez
    EF_CHECK(m.natural == 120.f && same(out, 1, 2, 3, 4));
    CardSlot d;
    d.area = {10, 20, 300, 0};
    d.row_h = 200.f;
    EF_CHECK(d.place(120.f, out) && same(out, 10, 20, 300, 200) && d.natural == 120.f);  // satir boyuna uzar
    d.row_h = 0.f;
    EF_CHECK(d.place(120.f, out) && same(out, 10, 20, 300, 120));  // dogal boy
    d.row_h = 80.f;
    EF_CHECK(d.place(120.f, out) && out.h == 120.f);  // kart asla dogal boyundan kisa cizilmez
}

EF_TEST(card_flow_pairs_present_cards_in_reading_order) {
    // Sira: A, (yok), B, L (liste: tam genislik), C -> [A | B], L, C (esi yok: tam genislik).
    int n = 0;
    Probe a{100.f}, gone{0.f}, b{60.f}, list{300.f}, c{80.f};
    RowLog log;
    CardFlow flow;
    flow.area = {16, 100, 1148, 0};
    flow.gap = 10;
    flow.log = &log;
    flow.cursor = 50;  // ustteki kartlardan sonra
    flow.pairs({{card(a, &n)}, {card(gone, &n)}, {card(b, &n)}, {card(list, &n), true}, {card(c, &n)}});
    EF_CHECK(same(a.frame, 16, 150, 569, 100) && same(b.frame, 595, 150, 569, 100));  // esit satir
    EF_CHECK(same(list.frame, 16, 260, 1148, 300));
    EF_CHECK(same(c.frame, 16, 570, 1148, 80));
    EF_CHECK(a.order == 0 && b.order == 1 && list.order == 2 && c.order == 3);  // cizim (ve UIA) sirasi okuma sirasi
    EF_CHECK(flow.used() == 50.f + 100.f + 10.f + 300.f + 10.f + 80.f);
    // Eslenen kartlar yalniz bir kez olculur; tam genislik kartlar olcum gecisi gormez (dogrudan cizilir).
    EF_CHECK(a.measured == 1 && b.measured == 1 && list.measured == 0 && c.measured == 1);
    EF_CHECK(a.drawn == 1 && b.drawn == 1 && list.drawn == 1 && c.drawn == 1 && gone.drawn == 0);
    EF_CHECK(log.size() == 1 && same(log[0][0], 16, 150, 569, 100) && same(log[0][1], 595, 150, 569, 100));

    // Dar pencere: hepsi tek sutunda, dogal boyunda; olcum gecisi ve satir kaydi yok.
    Probe sa{100.f}, sb{60.f};
    CardFlow narrow;
    narrow.area = {16, 0, 468, 0};
    narrow.gap = 10;
    narrow.stacked = true;
    narrow.log = &log;
    log.clear();
    narrow.pairs({{card(sa)}, {card(sb)}});
    EF_CHECK(same(sa.frame, 16, 0, 468, 100) && same(sb.frame, 16, 110, 468, 60));
    EF_CHECK(sa.measured == 0 && sb.measured == 0 && log.empty() && narrow.used() == 170.f);

    // Benzer boy kurali (spec §1a): uzun kart kisa kartin pair_max_ratio katini asiyorsa eslenmez; ikisi de tam genislik.
    Probe tall{414.f}, shrt{123.f}, near1{108.f}, near2{123.f};
    CardFlow hist;
    hist.area = {16, 0, 1148, 0};
    hist.gap = 10;
    hist.log = &log;
    hist.pair_max_ratio = 1.5f;
    log.clear();
    hist.pairs({{card(tall)}, {card(shrt)}});
    EF_CHECK(same(tall.frame, 16, 0, 1148, 414) && same(shrt.frame, 16, 424, 1148, 123) && log.empty());
    EF_CHECK(tall.drawn == 1 && shrt.drawn == 1 && hist.used() == 547.f);
    hist.cursor = 0;
    hist.pairs({{card(near1)}, {card(near2)}});
    EF_CHECK(same(near1.frame, 16, 0, 569, 123) && same(near2.frame, 595, 0, 569, 123) && log.size() == 1);
    // Eslenmeyen ikinci kart siradaki kartla eslenir: uzun, kisa, kisa -> uzun tam genislik, sonra [kisa | kisa].
    // Okuma (cizim) sirasi degismez; eslenmeyen kart yeniden olculur ama yalniz bir kez cizilir.
    Probe t2{414.f}, b2{123.f}, c2{108.f};
    int order = 0;
    hist.cursor = 0;
    log.clear();
    hist.pairs({{card(t2, &order)}, {card(b2, &order)}, {card(c2, &order)}});
    EF_CHECK(same(t2.frame, 16, 0, 1148, 414));
    EF_CHECK(same(b2.frame, 16, 424, 569, 123) && same(c2.frame, 595, 424, 569, 123));
    EF_CHECK(t2.order == 0 && b2.order == 1 && c2.order == 2 && t2.drawn == 1 && b2.drawn == 1 && c2.drawn == 1);
    EF_CHECK(log.size() == 1 && hist.used() == 547.f);

    // full(): olmayan kart yer ayirmaz; row(): esit satir ilerletir.
    Probe none{0.f}, f1{40.f}, r1{70.f}, r2{90.f};
    CardFlow flow2;
    flow2.area = {0, 0, 1000, 0};
    flow2.gap = 10;
    EF_CHECK(flow2.full(card(none)) == 0.f && flow2.cursor == 0.f && flow2.used() == 0.f);
    EF_CHECK(flow2.full(card(f1)) == 40.f && flow2.cursor == 50.f);
    EF_CHECK(flow2.row({{card(r1)}, {card(r2)}}) == 90.f);
    EF_CHECK(same(r1.frame, 0, 50, 495, 90) && same(r2.frame, 505, 50, 495, 90) && flow2.used() == 140.f);
    flow2.advance(0.f);
    EF_CHECK(flow2.used() == 140.f);
}

EF_TEST(history_tests_card_layout_depends_on_rows_not_on_measured_height) {
    // Gecmis: test gecmisi karti satir tasiyorsa liste kartidir -> tam genislik; akran karti altinda tam genislik.
    // Kural satir sayisina baglidir: kartin boyu (secim, karsilastirma, disa aktarma durumu) yerlesimi degistirmez.
    static_assert(!history_tests_full_width(0) && history_tests_full_width(1) && history_tests_full_width(27));
    RowLog log;
    for (const float tests_h : {150.f, 181.f, 414.f}) {  // kisa da olsa (akran kartina benzer boy) eslenmez
        Probe tests{tests_h}, peer{123.f};
        CardFlow flow;
        flow.area = {16, 0, 1148, 0};
        flow.gap = 10;
        flow.log = &log;
        flow.pairs({{card(tests), history_tests_full_width(3)}, {card(peer)}});
        EF_CHECK(same(tests.frame, 16, 0, 1148, tests_h) && same(peer.frame, 16, tests_h + 10.f, 1148, 123));
        EF_CHECK(log.empty() && tests.measured == 0 && tests.drawn == 1 && peer.drawn == 1);
    }
    // Satir yok: iki kisa bilgi karti boy oranina bakilmadan esit satirdir (pair_max_ratio = 0).
    for (const float peer_h : {100.f, 123.f, 260.f}) {
        Probe tests{125.f}, peer{peer_h};
        CardFlow flow;
        flow.area = {16, 0, 1148, 0};
        flow.gap = 10;
        flow.log = &log;
        log.clear();
        flow.pairs({{card(tests), history_tests_full_width(0)}, {card(peer)}});
        const float row_h = std::max(125.f, peer_h);
        EF_CHECK(same(tests.frame, 16, 0, 569, row_h) && same(peer.frame, 595, 0, 569, row_h) && log.size() == 1);
    }
}

EF_TEST(readiness_summary_filler_uses_first_two_real_advice) {
    // Panel hazirlik ozeti (spec §1c): bos alana yalniz gercek Sorun/Oneri bulgulari, gorunen sirada, en cok `max`.
    ViewModel::ReadinessView rv;
    const auto mk = [](const char* id, cr::Level l) { return cr::Check{id, cr::Section::Display, l, "x", {}, cr::ActionKind::None, ""}; };
    rv.checks = {mk("g0", cr::Level::Good), mk("u1", cr::Level::Unknown), mk("a2", cr::Level::Advice), mk("i3", cr::Level::Info),
                 mk("p4", cr::Level::Problem), mk("a5", cr::Level::Advice)};
    EF_CHECK((readiness_top_advice(rv, 2) == std::vector<std::size_t>{2, 4}));
    EF_CHECK((readiness_top_advice(rv, 5) == std::vector<std::size_t>{2, 4, 5}));  // olculmeyen / iyi / bilgi girmez
    EF_CHECK(readiness_top_advice(rv, 0).empty());
    rv.checks = {mk("g0", cr::Level::Good), mk("u1", cr::Level::Unknown)};
    EF_CHECK(readiness_top_advice(rv, 2).empty());  // oneri yoksa satir uydurulmaz
    EF_CHECK(readiness_top_advice(ViewModel::ReadinessView{}, 2).empty());
}

EF_TEST(readiness_ring_band_follows_the_good_ratio) {
    // Spec 2026-11-12 B3: puan seridi yerine 92 DIP hazirlik halkasi; yay rengi oranla turuncu < %40 <= sari < %80 <= yesil.
    static_assert(kReadinessRingD == 84.f);  // BOOST maketi halkasi (eski 92)
    EF_CHECK(readiness_ring_band(0, 10) == ReadinessBand::Low);
    EF_CHECK(readiness_ring_band(3, 10) == ReadinessBand::Low);
    EF_CHECK(readiness_ring_band(4, 10) == ReadinessBand::Mid);   // tam %40: sari
    EF_CHECK(readiness_ring_band(7, 10) == ReadinessBand::Mid);
    EF_CHECK(readiness_ring_band(79, 100) == ReadinessBand::Mid);
    EF_CHECK(readiness_ring_band(8, 10) == ReadinessBand::High);  // tam %80: yesil
    EF_CHECK(readiness_ring_band(10, 10) == ReadinessBand::High);
    EF_CHECK(readiness_ring_band(0, 0) == ReadinessBand::None);   // sonuc yok: renk / sayi uretilmez
    EF_CHECK(readiness_ring_band(-1, 5) == ReadinessBand::None);
    static_assert(kPairMaxRatio > 1.f && kPairMaxRatio <= 2.f);  // esit satirda kisa kartin en cok yarisi bos uzar
}
