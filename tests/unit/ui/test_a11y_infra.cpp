// UI denetimi Track D (2026-11-05): HitMap etkinlik semantigi, Tab sirasi atlama, gorunur alana kaydirma,
// odak halkasi token'lari. Platform cizimi gerektirmez (saf mantik).
#include <algorithm>
#include <cstdio>

#include "ef_test.h"
#include "ui/shell/screen.h"
#include "ui/theme/system_palette.h"
#include "ui/theme/theme.h"

using namespace ef::ui;

namespace {
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }

HitMap nav_hits() {
    HitMap m;
    for (std::size_t i = 0; i < kPageCount; ++i) m.add(nav_hit(i), {0, 40.f * static_cast<float>(i), 100, 40}, L"nav", AccRole::TabItem);
    return m;
}
}  // namespace

EF_TEST(ui_hitmap_add_defaults_enabled_and_stores_disabled) {
    HitMap m;
    m.add(Hit::Toggle, {10, 10, 40, 40}, L"Performans", AccRole::ToggleSwitch, false, 1);
    m.add(Hit::Autostart, {100, 10, 40, 40}, L"Otomatik", AccRole::ToggleSwitch, false, 0, false);
    EF_CHECK(m.meta.at(Hit::Toggle).enabled);
    EF_CHECK(!m.meta.at(Hit::Autostart).enabled);
    EF_CHECK(m.meta.at(Hit::Autostart).toggle == 0);   // durum UIA'ya yine okunur
    EF_CHECK(m.items.size() == 2);                     // devre disi oge agacta kalir
    EF_CHECK(m.activatable(Hit::Toggle));
    EF_CHECK(!m.activatable(Hit::Autostart));
    EF_CHECK(!m.activatable(Hit::Yes));                // kayitsiz oge etkinlestirilemez
    EF_CHECK(m.rect_of(Hit::Autostart) && m.rect_of(Hit::Autostart)->x == 100);
    EF_CHECK(m.rect_of(Hit::Yes) == nullptr);
}

EF_TEST(ui_hitmap_pointer_skips_disabled_offscreen_and_readonly) {
    HitMap m;
    m.add(Hit::Autostart, {0, 0, 50, 50}, L"devre disi", AccRole::ToggleSwitch, false, 0, false);
    m.add(Hit::CleanScan, {100, 0, 50, 50}, L"tara", AccRole::Button);
    m.meta[Hit::CleanScan].offscreen = true;
    m.add_progress(Hit::Progress0, {200, 0, 50, 10}, L"Ilerleme", 140.0);
    m.add_text(Hit::Status0, {300, 0, 50, 20}, L"Durum");
    m.add(Hit::Yes, {400, 0, 50, 50}, L"Evet", AccRole::Button);
    // Fare: yalniz etkin, ekrandaki, etkilesimli oge
    EF_CHECK(m.at(10, 10) == Hit::None);
    EF_CHECK(m.at(110, 10) == Hit::None);
    EF_CHECK(m.at(210, 5) == Hit::None);
    EF_CHECK(m.at(310, 5) == Hit::None);
    EF_CHECK(m.at(410, 10) == Hit::Yes);
    // UIA noktadan oge: ekrandaki her oge (devre disi + salt okunur), ekran disi haric
    EF_CHECK(m.element_at(10, 10) == Hit::Autostart);
    EF_CHECK(m.element_at(110, 10) == Hit::None);
    EF_CHECK(m.element_at(210, 5) == Hit::Progress0);
    EF_CHECK(m.element_at(310, 5) == Hit::Status0);
    // Ilerleme 0..100'e sinirlanir; salt okunur roller etkinlestirilemez
    EF_CHECK(m.meta.at(Hit::Progress0).range_value == 100.0);
    EF_CHECK(m.meta.at(Hit::Progress0).role == AccRole::ProgressBar);
    EF_CHECK(!m.activatable(Hit::Progress0) && !m.activatable(Hit::Status0));
    EF_CHECK(!interactive(AccRole::ProgressBar) && !interactive(AccRole::Text) && interactive(AccRole::Button));
    // Ekran disi ama etkin oge klavyeyle etkinlestirilebilir (RDY-10: Tab ile gelir, kaydirilir)
    EF_CHECK(m.activatable(Hit::CleanScan));
}

EF_TEST(ui_focus_order_skips_disabled_and_missing_elements) {
    // TH-01: baslangic kaydi okunamadi -> Otomatik baslat anahtari devre disi; Tab duragi olmamali.
    UiState s;
    s.page = Page::Settings;
    auto hits = nav_hits();
    hits.add(Hit::Autostart, {300, 100, 56, 44}, L"Otomatik", AccRole::ToggleSwitch, false, 0, false);
    hits.add(Hit::AppAnimations, {300, 200, 56, 44}, L"Animasyonlar", AccRole::ToggleSwitch, false, 0, true);
    const auto intent = focus_order(s);
    const auto order = focus_order(s, hits);
    EF_CHECK(has(intent, Hit::Autostart));   // niyet sirasinda var
    EF_CHECK(!has(order, Hit::Autostart));   // gercek Tab sirasinda yok (devre disi)
    EF_CHECK(has(order, Hit::AppAnimations));
    EF_CHECK(!has(order, Hit::SupportDumps));  // cizilmemis (ogesi yok) durak atlanir
    for (const Page pg : kNavOrder) EF_CHECK(has(order, nav_hit(static_cast<std::size_t>(pg))));
    // Baslangic ust seritte yok (Sistem araclari alt sekmesi); Ayarlar'da Tab duragi degil.
    EF_CHECK(!has(order, nav_hit(static_cast<std::size_t>(Page::Startup))));
    // Sira korunur: ust menu once, sayfa ogeleri sonra
    EF_CHECK(order.size() == kNavTabCount + 1 && order.back() == Hit::AppAnimations);
    // Ayni anahtar etkinlesince geri gelir
    hits.meta[Hit::Autostart].enabled = true;
    EF_CHECK(has(focus_order(s, hits), Hit::Autostart));
}

EF_TEST(ui_focus_order_busy_controls_disabled_not_in_tab) {
    // DS-05: is surerken Performans halkasi agacta (devre disi) ama Tab sirasinda degil.
    UiState s;
    s.page = Page::Overview;
    s.perf_phase = ef::core::perf::Phase::Off;
    auto hits = nav_hits();
    hits.add(Hit::Toggle, {300, 100, 44, 44}, L"Performans", AccRole::ToggleSwitch, false, 0, false);
    hits.add(Hit::Details, {360, 100, 120, 40}, L"Ayrintilar", AccRole::Button);
    const auto order = focus_order(s, hits);
    EF_CHECK(has(focus_order(s), Hit::Toggle));
    EF_CHECK(!has(order, Hit::Toggle));
    EF_CHECK(has(order, Hit::Details));
}

EF_TEST(ui_focus_order_hides_yes_when_game_running) {
    // EXT-AC-02 ile uyum: oyun calisirken Evet cizilmez; olu Tab duragi da olmaz.
    UiState s;
    s.page = Page::Cleanup;
    s.cleanup_phase = ef::core::cleanup::SessionPhase::Confirming;
    EF_CHECK(has(focus_order(s), Hit::CleanYes));
    s.game_blocked = true;
    EF_CHECK(!has(focus_order(s), Hit::CleanYes) && has(focus_order(s), Hit::CleanNo));
    s.page = Page::Overview;
    s.perf_phase = ef::core::perf::Phase::Confirming;
    EF_CHECK(!has(focus_order(s), Hit::Yes) && has(focus_order(s), Hit::No));
}

EF_TEST(ui_focus_order_cleanup_scan_cancel_and_storage_settings) {
    // EXT-CLN: tarama surerken Iptal + depolama ayarlari; onay/calistirma sirasinda depolama dugmesi yok.
    UiState s;
    s.page = Page::Cleanup;
    s.cleanup_phase = ef::core::cleanup::SessionPhase::Scanning;
    const auto scanning = focus_order(s);
    EF_CHECK(has(scanning, Hit::CleanScanCancel) && has(scanning, Hit::CleanStorageSettings));
    EF_CHECK(!has(scanning, Hit::CleanScan) && !has(scanning, Hit::CleanRun));
    s.cleanup_phase = ef::core::cleanup::SessionPhase::Ready;
    EF_CHECK(!has(focus_order(s), Hit::CleanScanCancel) && has(focus_order(s), Hit::CleanStorageSettings));
    s.cleanup_phase = ef::core::cleanup::SessionPhase::Confirming;
    EF_CHECK(has(focus_order(s), Hit::CleanYes) && !has(focus_order(s), Hit::CleanStorageSettings));
    s.cleanup_phase = ef::core::cleanup::SessionPhase::Running;
    EF_CHECK(!has(focus_order(s), Hit::CleanStorageSettings) && has(focus_order(s), Hit::CleanStop));
}

EF_TEST(ui_settings_focus_order_has_no_dead_consent_toggles) {
    UiState s;
    s.page = Page::Settings;
    s.consent_rows = 3;
    const auto o = focus_order(s);
    EF_CHECK(!has(o, consent_toggle_hit(0)));  // oyun satirinda ayri anahtar cizilmiyor
    EF_CHECK(has(o, consent_reset_hit(2)));
}

EF_TEST(ui_reveal_scroll_brings_item_into_view) {
    // Gorunur govde: y 100..600, kenar payi 8.
    EF_CHECK(reveal_scroll(0, {10, 200, 100, 40}, 100, 600, 8) == 0);     // zaten gorunur
    EF_CHECK(reveal_scroll(0, {10, 700, 100, 40}, 100, 600, 8) == 148);   // asagida: alt kenar 592'ye
    EF_CHECK(reveal_scroll(300, {10, 50, 100, 40}, 100, 600, 8) == 242);  // yukarida: ust kenar 108'e
    // Alandan buyuk oge: ust kenari hizalanir (alt kenara gore kaydirip ustu kaybetmez)
    EF_CHECK(reveal_scroll(0, {10, 700, 100, 900}, 100, 600, 8) == 592);
}

EF_TEST(ui_focus_ring_tokens_parsed) {
    const auto& t = default_theme();
    EF_CHECK(t.focus_ring_w == 2.f);                           // controls.keyboard_focus_ring_width
    EF_CHECK(t.dark.focus_ring == *Color::from_hex("#FFFFFF"));  // colors.focus_ring (TH-02: accent'ten ayri)
    EF_CHECK(contrast_ratio(t.dark.focus_ring, t.dark.background) >= 3.0);  // WCAG 1.4.11
    EF_CHECK(contrast_ratio(t.dark.focus_ring, t.dark.surface) >= 3.0);
    // focus_ring yoksa accent'e duser; varsa gecersiz deger reddedilir
    const char* base = R"({"colors":{"background":"#000000","sidebar":"#000000","surface":"#000000","surface_raised":"#000000",
        "border":"#000000","text_primary":"#FFFFFF","text_secondary":"#FFFFFF","accent":"#112233","on_accent":"#000000",
        "violet":"#000000","success":"#000000","warning":"#000000","error":"#000000"%s},
        "type":{"page_title":[26,34,"Semibold"],"state_title":[22,30,"Semibold"],"section":[18,26,"Semibold"],
        "body":[14,20,"Regular"],"secondary":[12,18,"Regular"],"metric":[28,34,"Semibold"]},
        "controls":{"keyboard_focus_ring_width":%s}})";
    auto make = [&](const char* extra, const char* width) {
        char buf[2048];
        std::snprintf(buf, sizeof(buf), base, extra, width);
        return parse_theme(buf);
    };
    auto a = make("", "3");
    EF_CHECK(a.ok() && a.value().dark.focus_ring == *Color::from_hex("#112233") && a.value().focus_ring_w == 3.f);
    auto b = make(R"(,"focus_ring":"#ABCDEF")", "9");  // genislik sinir disi -> varsayilan 2
    EF_CHECK(b.ok() && b.value().dark.focus_ring == *Color::from_hex("#ABCDEF") && b.value().focus_ring_w == 2.f);
    EF_CHECK(!make(R"(,"focus_ring":"blue")", "2").ok());
    // DS-08: uygulamanin cizdigi kip penceresinin token'lari da okunur (spec 2026-11-12 E: onaylar uygulama ici pencere).
    EF_CHECK(t.radius_dialog == 12.f && t.motion.press == 60 && t.motion.dialog_open == 160 && t.motion.dialog_close == 100);
    // DS-13: bilesen geometrisi token'lardan okunur (design/tokens.json geometry.*).
    EF_CHECK(t.live_ring_d == 88.f && t.live_ring_thick == 8.f && t.power_ring_thick == 4.f &&
              t.scrollbar_thumb_w == 4.f && t.scrollbar_inset == 8.f);
}

EF_TEST(ui_palette_non_text_contrast_meets_wcag) {
    // TH-05/TH-02/DS-10: kenarlik, kaydirma cubugu (border kullanir) ve odak halkasi tum yuzeylerde >= 3:1;
    // halka secim/vurgu renginden farkli. UI-14: uygulama tek koyu temayi kullanir.
    const auto& t = default_theme();
    auto check = [](const Palette& p) {
        for (const Color* bg : {&p.background, &p.surface, &p.surface_raised, &p.sidebar}) {
            EF_CHECK(contrast_ratio(p.border, *bg) >= 3.0);
            EF_CHECK(contrast_ratio(p.focus_ring, *bg) >= 3.0);
        }
        EF_CHECK(p.focus_ring != p.accent);
    };
    check(t.dark);
}

EF_TEST(ui_badge_text_meets_contrast_on_tinted_fill) {
    // DS-01/RDY-11: durum rozeti metni, %16 tonlu karisim zemini uzerinde >= 4.5:1; badge_text_color
    // gerekli duzeltmeyi yapar. UI-14: tek koyu tema.
    const auto& t = default_theme();
    auto check = [](const Palette& p) {
        for (const Color* tone : {&p.success, &p.warning, &p.error, &p.text_secondary}) {
            const Color fg = badge_text_color(p, *tone);
            const Color chip = mix(p.surface, *tone, alpha_scale::kBadgeTint);
            EF_CHECK(contrast_ratio(fg, chip) >= 4.5);
        }
    };
    check(t.dark);
}

EF_TEST(ui_chip_tones_meet_contrast_dark_and_high_contrast) {
    // Zehir yesili chip ciftleri (spec §2): zemin yuzey + ton (%12; notr %8), metin bu zemin uzerinde >= 4.5:1.
    // Koyu tema ve sistem HC tablosu (siyah/beyaz/sari/camgobegi) ayni kurali saglar.
    auto check = [](const Palette& p) {
        for (ChipTone tone : {ChipTone::Good, ChipTone::Warn, ChipTone::Danger, ChipTone::Neutral}) {
            const ChipColors c = chip_colors(p, tone);
            EF_CHECK(contrast_ratio(c.fg, c.bg) >= 4.5);
        }
        EF_CHECK(chip_colors(p, ChipTone::Good).bg == mix(p.surface, p.accent, alpha_scale::kChipTint));
        EF_CHECK(chip_colors(p, ChipTone::Neutral).bg == mix(p.surface, p.text_secondary, alpha_scale::kChipNeutralTint));
    };
    check(default_theme().dark);
    SystemColorTable c;
    c.window = *Color::from_hex("#000000");
    c.button_face = *Color::from_hex("#1F1F1F");
    c.window_text = *Color::from_hex("#FFFFFF");
    c.highlight = *Color::from_hex("#1AEBFF");
    c.highlight_text = *Color::from_hex("#000000");
    c.hotlight = *Color::from_hex("#FFFF00");
    c.gray_text = *Color::from_hex("#3FF23F");
    check(high_contrast_palette_from(c));
}

EF_TEST(ui_high_contrast_palette_uses_system_colors) {
    // TH-08: HC paleti elle degil sistem renk tablosundan uretilir; odak halkasi sistem metin rengidir ve
    // secim (COLOR_HIGHLIGHT) renginden ayrilir.
    SystemColorTable c;
    c.window = *Color::from_hex("#000000");
    c.button_face = *Color::from_hex("#1F1F1F");
    c.window_text = *Color::from_hex("#FFFFFF");
    c.highlight = *Color::from_hex("#1AEBFF");
    c.highlight_text = *Color::from_hex("#000000");
    c.hotlight = *Color::from_hex("#FFFF00");
    c.gray_text = *Color::from_hex("#3FF23F");
    const Palette p = high_contrast_palette_from(c);
    EF_CHECK(p.background == c.window && p.surface == c.window && p.sidebar == c.window);
    EF_CHECK(p.surface_raised == c.button_face);
    // DS-11: ikincil metin COLOR_GRAYTEXT; birincil metin WINDOWTEXT.
    EF_CHECK(p.text_primary == c.window_text && p.text_secondary == c.gray_text);
    EF_CHECK(p.text_secondary != p.text_primary);
    EF_CHECK(p.accent == c.highlight && p.on_accent == c.highlight_text);
    EF_CHECK(p.focus_ring == c.window_text && p.focus_ring != p.accent);
    EF_CHECK(contrast_ratio(p.border, p.background) >= 3.0);
    EF_CHECK(contrast_ratio(p.focus_ring, p.surface_raised) >= 3.0);
}


EF_TEST(ui_selected_hits_for_uia_selection) {
    // TH-04: secim konteyneri yalniz secili sekme/radyo ogelerini bildirir; anahtar/dugme secim sayilmaz.
    HitMap m;
    m.add(nav_hit(1), {0, 0, 10, 10}, L"Olcumler", AccRole::TabItem, true);
    m.add(Hit::SourceBuiltin, {0, 20, 10, 10}, L"Uygulama Testi", AccRole::RadioButton, true);
    m.add(Hit::Toggle, {0, 40, 10, 10}, L"Performans", AccRole::ToggleSwitch, true);
    m.add(Hit::No, {0, 60, 10, 10}, L"Hayir", AccRole::Button, false);
    const auto sel = selected_hits(m);
    EF_CHECK(sel.size() == 2 && sel[0] == nav_hit(1) && sel[1] == Hit::SourceBuiltin);
}

// EF-A11Y: cizilen bilgi satirlari (Gecmis karsilastirma, teshis karti, dogrulama karti) sirali Status kimligi
// alir; kimlikler tekil, UIA'da Text rolunde okunur, etkilesimsizdir; blok dolunca kayit atlanir (cizim degismez).
EF_TEST(ui_status_seq_assigns_unique_readonly_text_ids_and_caps) {
    HitMap m;
    StatusSeq seq{6};  // Olcumler sayfasi: 0..5 sabit durum metinleri
    const Hit a = seq.take();
    const Hit b = seq.take();
    EF_CHECK(a == static_cast<Hit>(static_cast<int>(Hit::Status0) + 6));
    EF_CHECK(b == static_cast<Hit>(static_cast<int>(Hit::Status0) + 7));
    m.add_text(a, {0, 0, 200, 20}, L"Koşullar uyuşmuyor: canlı panel");
    m.add_text(b, {0, 30, 200, 20}, L"İşlemci: Ölçülmedi");
    EF_CHECK(m.items.size() == 2);
    EF_CHECK(m.meta.at(a).role == AccRole::Text && m.meta.at(a).name == L"Koşullar uyuşmuyor: canlı panel");
    EF_CHECK(!m.activatable(a) && !m.activatable(b));
    EF_CHECK(m.at(10, 10) == Hit::None);         // fare hedefi degil
    EF_CHECK(m.element_at(10, 35) == b);          // UIA noktadan oge
    EF_CHECK(hit_automation_id(a) == L"status.6");
    EF_CHECK(hit_automation_id(b) != hit_automation_id(a));
    // Blok siniri: kMaxStatusItems sonrasi Hit::None ve kayit yok.
    StatusSeq full{static_cast<std::size_t>(kMaxStatusItems) - 1};
    const Hit last = full.take();
    EF_CHECK(last != Hit::None);
    const Hit over = full.take();
    EF_CHECK(over == Hit::None);
    m.add_text(over, {0, 60, 10, 10}, L"tasma");
    EF_CHECK(m.items.size() == 2);
}

// EF-A11Y: UIA adi birlestirme: noktalama ile biten parcadan sonra ".." uretilmez; bos parca atlanir.
EF_TEST(ui_a11y_join_avoids_double_punctuation) {
    EF_CHECK(a11y_join(L"Akran karşılaştırması", L"Veri yok.") == L"Akran karşılaştırması. Veri yok.");
    EF_CHECK(a11y_join(L"Veri yok.", L"En az 20 örnek") == L"Veri yok. En az 20 örnek");
    EF_CHECK(a11y_join(L"Doğrulama:", L"Geçti") == L"Doğrulama: Geçti");
    EF_CHECK(a11y_join(L"Başlık", L"") == L"Başlık");
    EF_CHECK(a11y_join(L"", L"Metin") == L"Metin");
}

// Zehir yesili ust menu: Tab sirasi ust sekmeler (kNavOrder) -> Sistem araclari alt sekmeleri -> sayfa.
EF_TEST(ui_focus_order_top_tabs_then_systools_subtabs) {
    UiState s;
    s.page = Page::Overview;
    auto o = focus_order(s);
    EF_CHECK(o.size() >= kNavTabCount);
    for (std::size_t i = 0; i < kNavTabCount; ++i) EF_CHECK(o[i] == nav_hit(static_cast<std::size_t>(kNavOrder[i])));
    EF_CHECK(!has(o, nav_hit(static_cast<std::size_t>(Page::Startup))));
    EF_CHECK(!has(o, Hit::SysTabCleanup));
    std::size_t navs = 0;
    for (Hit h : o) navs += static_cast<int>(h) >= 0 && static_cast<int>(h) < static_cast<int>(kPageCount) ? 1u : 0u;
    EF_CHECK(navs == kNavTabCount);  // 7 ust sekme (8 degil)
    // Spec 2026-11-12 B4: Sistem araclari sekmesiz tek sayfa — eski alt sekmeler (Sistem, Temizlik, Baslangic) Tab
    // sirasinda yok; ust sekmelerden sonra dogrudan sayfa ogeleri gelir.
    for (const Page pg : {Page::System, Page::Cleanup, Page::Startup}) {
        s.page = pg;
        o = focus_order(s);
        for (std::size_t i = 0; i < kNavTabCount; ++i) EF_CHECK(o[i] == nav_hit(static_cast<std::size_t>(kNavOrder[i])));
        EF_CHECK(!has(o, nav_hit(static_cast<std::size_t>(Page::System))));
        EF_CHECK(!has(o, Hit::SysTabCleanup));
        EF_CHECK(!has(o, nav_hit(static_cast<std::size_t>(Page::Startup))));
    }
}

// Ust sekme "Sistem araclari" (nav_hit(Cleanup)) hem Temizlik hem Baslangic sayfasinda secilidir; alt sekmeler
// kendi sayfasinda secilidir. Cizim (topbar/systools_tabs) ayni saf kurali kullanir.
EF_TEST(ui_systools_tab_selected_on_cleanup_and_startup) {
    EF_CHECK(nav_tab_selected(Page::Cleanup, Page::Cleanup));
    EF_CHECK(nav_tab_selected(Page::Cleanup, Page::Startup));
    EF_CHECK(!nav_tab_selected(Page::Cleanup, Page::Overview));
    EF_CHECK(nav_tab_selected(Page::Games, Page::Games) && !nav_tab_selected(Page::Games, Page::Startup));
    EF_CHECK(systools_subtab_selected(Hit::SysTabCleanup, Page::Cleanup));
    EF_CHECK(!systools_subtab_selected(Hit::SysTabCleanup, Page::Startup));
    EF_CHECK(systools_subtab_selected(nav_hit(static_cast<std::size_t>(Page::Startup)), Page::Startup));
    EF_CHECK(!systools_subtab_selected(nav_hit(static_cast<std::size_t>(Page::Startup)), Page::Cleanup));
}
