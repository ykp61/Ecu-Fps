#include <cmath>
#include <string>

#include "core/serialization/json.h"
#include "ef_test.h"
#include "ui/layout/layout.h"
#include "ui/motion/motion.h"
#include "ui/theme/theme.h"
#include "ui/theme/system_palette.h"

using namespace ef::ui;

EF_TEST(ui_theme_matches_embedded_tokens) {
    auto r = parse_theme(embedded_tokens_json());
    EF_CHECK(r.ok());
    const auto& t = r.value();
    // docs/07 renk tablosu ile token'lar ayni (kodda tekrar sabit yok).
    EF_CHECK(t.dark.background == *Color::from_hex("#05070B"));
    EF_CHECK(t.dark.accent == *Color::from_hex("#3DFF7A"));
    EF_CHECK(t.dark.surface == *Color::from_hex("#0B0F17"));
    EF_CHECK(t.dark.on_accent == *Color::from_hex("#04140A"));
    EF_CHECK(t.dark.orange == *Color::from_hex("#FF8A1F"));
    EF_CHECK(t.dark.cyan == *Color::from_hex("#00D4FF"));
    EF_CHECK(t.dark.hero == *Color::from_hex("#07110B"));
    EF_CHECK(t.dark.focus_ring == *Color::from_hex("#FFFFFF"));
    // Siki yerlesim (spec 2026-11-11 §2): ust menu 52, logo amblem 30 / yatay logo 200.
    EF_CHECK(t.topbar_h == 52 && t.strip_h == 3 && t.strip_period_ms == 8000 && t.strip_animation);
    EF_CHECK(t.topbar_tab_h == 36 && t.logo_mark_h == 30 && t.logo_lockup_w == 160);  // BOOST: marka asgarisi (maket 130 < 160)
    // ADR-033: kurulum penceresi olculeri (setup.*)
    EF_CHECK(t.setup.window_w == 480 && t.setup.window_h == 560 && t.setup.margin == 24 && t.setup.logo_w == 200 && t.setup.logo_h == 40);
    EF_CHECK(t.setup.row_h == 44 && t.setup.progress_h == 6 && t.setup.step_dot == 10);
    // Yazi rolleri: PageTitle 18/24, Metric 24, Body 13, Secondary 12, bolum etiketi 11.
    EF_CHECK(t.role(Role::PageTitle).size == 18 && t.role(Role::PageTitle).line == 24 &&
              t.role(Role::PageTitle).weight == Weight::Semibold);
    EF_CHECK(t.role(Role::Metric).size == 24 && t.role(Role::Metric).weight == Weight::Semibold);
    EF_CHECK(t.role(Role::Body).size == 13 && t.role(Role::Body).weight == Weight::Regular);
    EF_CHECK(t.role(Role::Secondary).size == 12 && t.role(Role::Secondary).weight == Weight::Regular);
    EF_CHECK(t.role(Role::Label).size == 11 && t.role(Role::Label).weight == Weight::Semibold);
    // B15: sonuc kartinin buyuk ortalama FPS sayisi (type.hero) 38/44 yari kalin.
    EF_CHECK(t.role(Role::Hero).size == 38 && t.role(Role::Hero).line == 44 && t.role(Role::Hero).weight == Weight::Semibold);
    // Her rolde satir yuksekligi yazi boyutundan buyuk (metin kirpilmaz).
    for (std::size_t i = 0; i < static_cast<std::size_t>(Role::Count_); ++i) EF_CHECK(t.type[i].line > t.type[i].size);
    // Denetimler: dugme 32 (satir ici 28), asgari genislik 72; anahtar 40x22, Performans seridinde 52x28.
    EF_CHECK(t.toggle_w == 40 && t.toggle_h == 22 && t.hero_toggle_w == 52 && t.hero_toggle_h == 28);
    EF_CHECK(t.button_h == 32 && t.button_row_h == 28 && t.button_min_w == 72);
    // Dokunma hedefi asgari 24 (WCAG 2.5.8); hicbir denetim olcusu bunun altinda degil, hicbiri ust sinirin ustunde degil.
    EF_CHECK(t.touch_min == 24 && t.control_max_h == 44);
    for (const float hgt : {t.button_h, t.button_row_h, t.hero_toggle_h, t.topbar_tab_h})
        EF_CHECK(hgt >= t.touch_min && hgt <= t.control_max_h);
    // Yogunluk: kenar 16, kart araligi 10, kart ic boslugu 12, kart kosesi 6, baslik seridi 36, tablo satiri 34/48,
    // icerik azami genisligi 1280.
    EF_CHECK(t.page_margin == 16 && t.card_gap == 10 && t.card_pad == 12 && t.radius_card == 6);
    EF_CHECK(t.header_h == 36 && t.row_h == 34 && t.row2_h == 48 && t.content_max_w == 1280);
    EF_CHECK(t.min_window_w == 960 && t.min_window_h == 540);
    EF_CHECK(t.motion.hover == 80 && t.motion.toggle == 120 && t.motion.page == 140);
    EF_CHECK(!t.continuous_idle_animation);
    EF_CHECK(t.font_families.size() >= 2 && t.font_families[0] == "Segoe UI Variable");
    EF_CHECK(default_theme().dark.surface == t.dark.surface);
}

EF_TEST(ui_hc_palette_maps_new_slots) {
    // Zehir yesili yeni yuvalari HC'de sistem renklerine baglanir (elle renk yok).
    SystemColorTable c;
    c.window = *Color::from_hex("#000000");
    c.button_face = *Color::from_hex("#1F1F1F");
    c.window_text = *Color::from_hex("#FFFFFF");
    c.highlight = *Color::from_hex("#1AEBFF");
    c.highlight_text = *Color::from_hex("#000000");
    c.hotlight = *Color::from_hex("#FFFF00");
    c.gray_text = *Color::from_hex("#3FF23F");
    const Palette p = high_contrast_palette_from(c);
    EF_CHECK(p.orange == c.hotlight);
    EF_CHECK(p.cyan == c.highlight);
    EF_CHECK(p.hero == c.window);
}

EF_TEST(ui_theme_label_role_optional_and_density_ranges) {
    // type.label yoksa 11/14 yari kalin (eski token dosyalari); varsa gecerli olmali.
    std::string no_label(embedded_tokens_json());
    const auto at = no_label.find("\"label\"");
    EF_CHECK(at != std::string::npos);
    no_label.replace(at, 7, "\"labe_\"");
    auto r = parse_theme(no_label);
    EF_CHECK(r.ok());
    EF_CHECK(r.value().role(Role::Label).size == 11 && r.value().role(Role::Label).line == 14);
    // B15: type.hero da istege baglidir (yoksa 38/44); varsa gecerli olmali. Renk "hero" anahtari ayri (colors.hero).
    std::string no_hero(embedded_tokens_json());
    const auto ht = no_hero.find("\"hero\": [");
    EF_CHECK(ht != std::string::npos);
    no_hero.replace(ht, 6, "\"her_\"");
    auto nh = parse_theme(no_hero);
    EF_CHECK(nh.ok());
    EF_CHECK(nh.value().role(Role::Hero).size == 38 && nh.value().role(Role::Hero).line == 44);
    std::string bad_hero(embedded_tokens_json());
    bad_hero.insert(bad_hero.find("\"hero\": [") + 7, " 5, \"x\": ");
    EF_CHECK(!parse_theme(bad_hero).ok());
    std::string bad_label(embedded_tokens_json());
    bad_label.insert(bad_label.find("\"label\"") + 8, " 5, \"x\": ");
    EF_CHECK(!parse_theme(bad_label).ok());
    // density blogu olmayan dosya: varsayilanlar spec degerleridir.
    std::string no_density(embedded_tokens_json());
    const auto d = no_density.find("\"density\"");
    EF_CHECK(d != std::string::npos);
    no_density.replace(d, 9, "\"densit_\"");
    auto nd = parse_theme(no_density);
    EF_CHECK(nd.ok());
    EF_CHECK(nd.value().page_margin == 16 && nd.value().card_gap == 10 && nd.value().card_pad == 12 &&
              nd.value().content_max_w == 1280 && nd.value().row_h == 34 && nd.value().row2_h == 48);
}

EF_TEST(ui_theme_rejects_bad_tokens) {
    EF_CHECK(!parse_theme("{}").ok());
    EF_CHECK(!parse_theme(R"({"colors":{"background":"#GG0000"}})").ok());
    EF_CHECK(!Color::from_hex("10151E") && !Color::from_hex("#10151") && !Color::from_hex("#1015ZZ"));
}

EF_TEST(ui_text_contrast_meets_wcag_aa) {
    const auto& p = default_theme().dark;
    // Normal metin >= 4.5:1 (docs/07). Ikincil metin de islevsel; ayni esik.
    for (const auto* bg : {&p.background, &p.surface, &p.surface_raised, &p.sidebar}) {
        EF_CHECK(contrast_ratio(p.text_primary, *bg) >= 4.5);
        EF_CHECK(contrast_ratio(p.text_secondary, *bg) >= 4.5);
    }
    EF_CHECK(contrast_ratio(p.on_accent, p.accent) >= 4.5);
    // Bilinen deger: siyah/beyaz 21:1
    EF_CHECK(std::fabs(contrast_ratio({0, 0, 0}, {1, 1, 1}) - 21.0) < 0.01);
}

EF_TEST(ui_dark_palette_status_contrast_and_light_key_ignored) {
    // UI-14: uygulama tek koyu temayi kullanir; durum renkleri metin olarak da AA esigini gecer.
    const auto& d = default_theme().dark;
    for (const auto* bg : {&d.background, &d.surface, &d.surface_raised, &d.sidebar})
        for (const auto* c : {&d.success, &d.warning, &d.error, &d.accent}) EF_CHECK(contrast_ratio(*c, *bg) >= 4.5);
    EF_CHECK(contrast_ratio(d.on_accent, d.accent) >= 4.5);
    // Eski token dosyasindaki colors_light alani bilinmeyen alan sayilir; parse hata vermez (tek tema koyu).
    std::string old_tokens(embedded_tokens_json());
    old_tokens.insert(1, "\"colors_light\":{},");
    EF_CHECK(parse_theme(old_tokens).ok());
}

namespace {
// Sabit olculer: genis etiketler (Panel, Oyunlar, Oyun hazirligi, Olcumler, Sistem, Gecmis, Ayarlar; spec 2026-11-12 B5:
// menudeki kisa ad "Sistem"). Logo olculeri murekkep kutusudur.
TopbarMetrics fixed_metrics(float lockup = 200) {
    TopbarMetrics m;
    m.label_w = {44, 62, 104, 70, 52, 58, 58};
    m.label_w_short_readiness = 66;
    m.lockup_w = lockup;
    m.lockup_h = 31;
    m.mark_w = 51;
    m.mark_h = 30;
    m.status_w = 128;
    m.status_dot_w = 16;
    return m;
}
// Sekiz sekme (B5 sirasi: ... Olcumler, Guncellemeler, Sistem ...): "Guncellemeler" sonraki gorevde eklenir; sigdirma
// kurali sekme sayisindan bagimsizdir.
TopbarWidths eight_tabs(float lockup = 190, float extra_label = 0) {
    TopbarWidths w;
    w.tabs = 8;
    w.labels = 31 + 45 + 81 + 52 + 95 + 45 + 41 + 40 + extra_label;
    w.short_saving = 81 - 50;
    w.lockup = lockup;
    w.mark = 47;
    w.status = 126;
    w.status_dot = 16;
    return w;
}
}  // namespace

EF_TEST(ui_shell_layout_bounds) {
    const auto& t = default_theme();
    const auto m = fixed_metrics();
    for (auto [w, h] : {std::pair{960.f, 540.f}, {1180.f, 720.f}, {2560.f, 1400.f}}) {
        const auto l = compute_shell(w, h, t, m);
        EF_CHECK(l.strip.x == 0 && l.strip.y == 0 && l.strip.h == 3 && std::fabs(l.strip.w - w) < 0.01f);
        EF_CHECK(l.topbar.y == 3 && l.topbar.h == 52 && std::fabs(l.topbar.w - w) < 0.01f);
        float prev_right = l.brand.right();
        for (std::size_t i = 0; i < kNavOrder.size(); ++i) {
            const auto& r = l.nav[static_cast<std::size_t>(kNavOrder[i])];
            EF_CHECK(r.w > 0 && r.x >= prev_right);  // soldan saga, cakismasiz
            EF_CHECK(r.y >= l.topbar.y && r.bottom() <= l.topbar.bottom() && r.right() <= l.status.x);
            EF_CHECK(r.h == t.topbar_tab_h && r.h >= t.touch_min && r.h <= t.control_max_h);
            prev_right = r.right();
        }
        EF_CHECK(l.nav[static_cast<std::size_t>(Page::Startup)].w == 0);
        EF_CHECK(l.content.y == l.topbar.bottom() && l.content.x == 0 && std::fabs(l.content.right() - w) < 0.01f);
        EF_CHECK(l.header.y > l.topbar.bottom());
        EF_CHECK(l.body.y > l.header.bottom() && l.body.bottom() <= h);
        // Baslik seridi tek satir (36 DIP); govde ile ayni sutunda. Alt kenarda sayfa kenar boslugu kalir.
        EF_CHECK(l.header.h == t.header_h && l.header.x == l.body.x && l.header.w == l.body.w);
        EF_CHECK(std::fabs(h - l.body.bottom() - t.page_margin) < 0.01f);
        // Icerik sutunu: en fazla 1280, pencerede ORTALI; yanlarda en az sayfa kenar boslugu.
        const float col_w = std::fmin(w - 2 * t.page_margin, t.content_max_w);
        EF_CHECK(std::fabs(l.body.w - col_w) < 0.01f);
        EF_CHECK(std::fabs(l.body.x - (w - col_w) / 2) < 0.01f && l.body.x >= t.page_margin - 0.01f);
        EF_CHECK(std::fabs((w - l.body.right()) - l.body.x) < 0.01f);
        // Ust menu ogeleri ayni sutuna hizali: marka sutunun solunda, durum gostergesi saginda biter.
        EF_CHECK(std::fabs(l.brand.x - l.body.x) < 0.01f && std::fabs(l.status.right() - l.body.right()) < 0.01f);
    }
    {
        const auto wide = compute_shell(2560, 1400, t, m);
        EF_CHECK(wide.body.w == 1280 && wide.body.x == 640);
        const auto def = compute_shell(1180, 720, t, m);
        EF_CHECK(def.body.x == 16 && def.body.w == 1148);
        // 1180x720 kabul olcutu icin govde yuksekligi: serit 3 + menu 52 + 8 + baslik 36 + 8 = 107'den baslar.
        EF_CHECK(def.body.y == 107 && def.body.h == 720 - 107 - 16);
    }
    const auto tiny = compute_shell(10, 10, t, m);
    EF_CHECK(tiny.body.w >= 0 && tiny.body.h >= 0 && tiny.content.w >= 0 && tiny.content.h >= 0);
    EF_CHECK(tiny.topbar.h >= 0 && tiny.strip.h >= 0 && tiny.brand.w >= 0 && tiny.status.w >= 0 && tiny.header.w >= 0);
    for (const auto& r : tiny.nav) EF_CHECK(r.w >= 0 && r.h >= 0);
}

EF_TEST(ui_topbar_compact_steps) {
    const auto& t = default_theme();
    EF_CHECK(compute_shell(1180, 720, t, fixed_metrics()).mode == TopbarMode::Full);
    const auto l = compute_shell(960, 540, t, fixed_metrics());
    EF_CHECK(l.mode != TopbarMode::Full);
    // 200 DIP yatay logo 960'ta sigmaz: yalniz amblem kalir; marka kutusu amblem genisligindedir.
    EF_CHECK(l.mode == TopbarMode::MarkOnly && l.brand.w == fixed_metrics().mark_w);
    EF_CHECK(compute_shell(1180, 720, t, fixed_metrics()).brand.w == 200);
    // Asgari genislikte tum sekmeler gostergeye carpmadan sigar.
    EF_CHECK(l.nav[static_cast<std::size_t>(kNavOrder.back())].right() <= l.status.x);
    EF_CHECK(l.status.right() <= 960);
    EF_CHECK(compute_shell(700, 540, t, fixed_metrics()).mode == TopbarMode::DotOnly);
}

EF_TEST(ui_topbar_alignment_b5) {
    // Spec 2026-11-12 B5 + BOOST maketi: logo sutunun sol kenarinda -> sekme grubu (es ic bosluk, aralarinda
    // kTopbarTabGap) logo ile durum gostergesinin arasinda ortali (justify-between; logoya en az kTopbarBrandGap) -> en
    // sagda durum gostergesi; logo dikeyde sekmelerin ortasinda.
    const auto& t = default_theme();
    const auto m = fixed_metrics();
    for (auto [w, h] : {std::pair{960.f, 540.f}, {1180.f, 720.f}, {2560.f, 1400.f}}) {
        const auto l = compute_shell(w, h, t, m);
        const bool mark = topbar_mark_only(l.mode);
        // Marka kutusu = murekkep kutusu: sol kenar govdeyle ayni; genislik / yukseklik verilen murekkep olculeri.
        EF_CHECK(l.brand.x == l.body.x && l.brand.w == (mark ? m.mark_w : m.lockup_w) && l.brand.h == (mark ? m.mark_h : m.lockup_h));
        const auto& first = l.nav[static_cast<std::size_t>(kNavOrder.front())];
        const auto& last = l.nav[static_cast<std::size_t>(kNavOrder.back())];
        const float lead = first.x - l.brand.right(), trail = l.status.x - last.right();
        EF_CHECK(lead >= kTopbarBrandGap - 0.01f);  // logoya en az sabit bosluk
        EF_CHECK(std::fabs(lead - kTopbarBrandGap) < 0.01f || (trail >= lead - 0.01f && trail <= lead + 1.01f));  // ortali (tam DIP)
        EF_CHECK(std::fabs((l.brand.y + l.brand.h / 2) - (first.y + first.h / 2)) < 0.01f);  // dikey orta
        EF_CHECK(std::fabs((first.y + first.h / 2) - (l.topbar.y + l.topbar.h / 2)) < 0.01f);
        EF_CHECK(l.tab_pad >= kTopbarTabPadMin && l.tab_pad <= kTopbarTabPad);
        float x = first.x;
        for (std::size_t i = 0; i < kNavOrder.size(); ++i) {
            const auto& r = l.nav[static_cast<std::size_t>(kNavOrder[i])];
            const float label = (topbar_short_labels(l.mode) && kNavOrder[i] == Page::Readiness) ? m.label_w_short_readiness : m.label_w[i];
            EF_CHECK(std::fabs(r.x - x) < 0.01f);                            // aralarinda tam kTopbarTabGap (itme yok)
            EF_CHECK(std::fabs(r.w - label - 2 * l.tab_pad) < 0.01f);       // her sekmede ayni ic bosluk
            x = r.right() + l.tab_gap;
        }
        EF_CHECK(std::fabs(l.status.right() - l.body.right()) < 0.01f);  // sag kenar payi = sol kenar payi
        EF_CHECK(std::fabs((w - l.status.right()) - l.brand.x) < 0.01f);
    }
    // Kesirli etiket genisligi tam DIP'e yuvarlanir: sekme kenarlari tam DIP'te kalir.
    auto frac = m;
    frac.label_w[0] = 30.4f;
    const auto lf = compute_shell(1180, 720, t, frac);
    EF_CHECK(lf.nav[static_cast<std::size_t>(kNavOrder[0])].w == 31 + 2 * kTopbarTabPad);
    // Murekkep yuksekligi verilmezse marka kutusu seridin tamamidir (negatif olcu yok).
    auto no_h = m;
    no_h.lockup_h = no_h.mark_h = 0;
    const auto ln = compute_shell(1180, 720, t, no_h);
    EF_CHECK(ln.brand.h == ln.topbar.h && ln.brand.y == ln.topbar.y);
}

EF_TEST(ui_topbar_fits_eight_tabs) {
    // B5: sekiz sekme asgari pencerede (960 DIP -> sutun 928) sigar. Daralma sirasi: once yatay logo ambleme iner,
    // sonra sekme ic boslugu kuculur, sonra kisa etiket, en son yalniz nokta.
    const float margin = 16;
    const auto wide = fit_topbar(1180 - 2 * margin, margin, eight_tabs());
    EF_CHECK(wide.fits && wide.mode == TopbarMode::Full && wide.tab_pad == kTopbarTabPad);
    const auto min = fit_topbar(960 - 2 * margin, margin, eight_tabs());
    EF_CHECK(min.fits && min.mode == TopbarMode::MarkOnly && min.tab_pad == kTopbarTabPad);  // amblem + tam ic bosluk
    // Yatay logo ic bosluk kuculterek tutulmaz: sigmadigi ilk genislikte amblem (tam ic boslukla) gelir.
    const float full_need = 190 + kTopbarBrandGap + eight_tabs().labels + 7 * kTopbarTabGap + 16 * kTopbarTabPad + margin + 126;
    EF_CHECK(fit_topbar(full_need, margin, eight_tabs()).mode == TopbarMode::Full);
    const auto just_below = fit_topbar(full_need - 1, margin, eight_tabs());
    EF_CHECK(just_below.mode == TopbarMode::MarkOnly && just_below.tab_pad == kTopbarTabPad);
    // Daha uzun etiketler (baska dil): amblemle birlikte sekme ic boslugu kuculur, etiketler tam kalir.
    const auto tight = fit_topbar(960 - 2 * margin, margin, eight_tabs(190, 80));  // ic bosluk 12 iken daralmayi zorlayan uzunluk
    EF_CHECK(tight.fits && tight.mode == TopbarMode::MarkOnly && tight.tab_pad < kTopbarTabPad && tight.tab_pad >= kTopbarTabPadMin);
    // Kullanilan genislik sutunu asmaz.
    const auto used = [&](const TopbarWidths& w, const TopbarFit& f) {
        return (topbar_mark_only(f.mode) ? w.mark : w.lockup) + kTopbarBrandGap + w.labels - (topbar_short_labels(f.mode) ? w.short_saving : 0.f) +
               static_cast<float>(w.tabs - 1) * kTopbarTabGap + 2 * static_cast<float>(w.tabs) * f.tab_pad + margin +
               (f.mode == TopbarMode::DotOnly ? w.status_dot : w.status);
    };
    EF_CHECK(used(eight_tabs(), min) <= 960 - 2 * margin && used(eight_tabs(190, 80), tight) <= 960 - 2 * margin);
    // Asgari pencerenin altinda: kisa etiket, sonra yalniz nokta; hic sigmazsa fits = false (ic bosluk asgaride kalir).
    const auto shorter = fit_topbar(750 + 7 * kTopbarTabGap, margin, eight_tabs());  // sekme araliklari kadar genis
    EF_CHECK(shorter.fits && shorter.mode == TopbarMode::ShortLabels && shorter.tab_pad >= kTopbarTabPadMin);
    const auto dot = fit_topbar(650, margin, eight_tabs());
    EF_CHECK(dot.fits && dot.mode == TopbarMode::DotOnly && dot.tab_pad >= kTopbarTabPadMin);
    const auto none = fit_topbar(300, margin, eight_tabs());
    EF_CHECK(!none.fits && none.mode == TopbarMode::DotOnly && none.tab_pad == kTopbarTabPadMin);
    // Sekmesiz girdi bolme hatasi uretmez.
    EF_CHECK(fit_topbar(500, margin, TopbarWidths{}).fits);
}

EF_TEST(ui_topbar_nav_icons_dropped_first_when_tight) {
    // Sahip secimi 2026-11-14: sekme ikonlari. Genis pencerede ikon + yatay logo; daralinca ikonlar amblemle kalir;
    // ikonlu hic sigmayinca once ikonlar birakilir (eski daralma sirasi aynen surer). Ikon yoksa sonuc eskisiyle ayni.
    const float margin = 16;
    auto w = eight_tabs();
    w.icons = 8 * (kNavIconSize + kNavIconGap);
    const auto wide = fit_topbar(1600 - 2 * margin, margin, w);
    EF_CHECK(wide.fits && wide.icons && wide.mode == TopbarMode::Full);
    const auto mid = fit_topbar(1180 - 2 * margin, margin, w);
    EF_CHECK(mid.fits && mid.icons && mid.mode == TopbarMode::MarkOnly && mid.tab_pad >= kTopbarTabPadMin);
    const auto narrow = fit_topbar(750 + 7 * kTopbarTabGap, margin, w);
    EF_CHECK(narrow.fits && !narrow.icons && narrow.mode == TopbarMode::ShortLabels);
    // Ikonlu yerlesim de sutunu asmaz.
    const float used = w.mark + kTopbarBrandGap + w.labels + w.icons + 7 * kTopbarTabGap + 16 * mid.tab_pad + margin + w.status;
    EF_CHECK(used <= 1180 - 2 * margin);
    EF_CHECK(!fit_topbar(1180 - 2 * margin, margin, eight_tabs()).icons);
}

EF_TEST(ui_content_columns_split_and_stack) {
    // UI-15: genis govde iki esit sutuna ayrilir; ara bosluk korunur, sutunlar cakismaz.
    const auto& t = default_theme();
    const RectF wide{10, 20, 1000, 500};
    const auto c = content_columns(wide, t);
    EF_CHECK(!c.stacked);
    EF_CHECK(std::fabs(c.left.w - c.right.w) < 0.01f);
    EF_CHECK(c.left.x == wide.x && c.left.y == wide.y && c.right.y == wide.y);
    EF_CHECK(c.right.x >= c.left.right());
    EF_CHECK(c.right.right() <= wide.right());
    const float gap = t.card_gap;  // kartlar arasi bosluk (10)
    EF_CHECK(std::fabs(c.right.x - (c.left.right() + gap)) < 0.01f);
    // Dar govde tek sutuna duser; sag sutun bos, alan kaybedilmez.
    const RectF narrow{10, 20, 700, 500};
    const auto n = content_columns(narrow, t);
    EF_CHECK(n.stacked && n.left.x == narrow.x && n.left.w == narrow.w && n.right.w == 0);
    // 1180x720 varsayilan pencerede govde (1148 DIP) iki sutun esigini gecer; eski dar govde (~924) de gecerdi.
    EF_CHECK(!content_columns({0, 0, 1148, 500}, t).stacked);
    EF_CHECK(!content_columns({0, 0, 924, 500}, t).stacked);
    EF_CHECK(content_columns({0, 0, 739, 500}, t).stacked);
}

// Baslik seridi yuvasi: sag yuva sagdan sola, basliga bagli ogeler soldan saga dizilir; sigmayan oge yer ayirmaz.
EF_TEST(ui_header_bar_slots) {
    const RectF strip{16, 63, 1148, 36};
    HeaderBar b = HeaderBar::make(strip, 100, 8);
    EF_CHECK(b.lead == 16 + 100 + 16 && b.trail == strip.right());
    const RectF a = b.take(120, 28);  // sag yuva: en sagda, dikey ortali
    EF_CHECK(a.right() == strip.right() && a.w == 120 && a.h == 28 && a.y == 63 + 4);
    const RectF c = b.take(60, 18);  // ikinci oge birincinin solunda, arada bosluk
    EF_CHECK(c.right() == a.x - 8 && c.y == 63 + 9);
    const RectF d = b.take_leading(80, 28);  // basligin hemen saginda
    EF_CHECK(d.x == 132 && d.w == 80);
    const RectF e = b.take_leading(70, 28);
    EF_CHECK(e.x == d.right() + 8);
    EF_CHECK(e.right() <= c.x);  // iki yon cakismaz
    // Yukseklik seridi asmaz.
    const RectF tall = b.take(40, 100);
    EF_CHECK(tall.h == 36 && tall.y == 63);
    // Sigmayan istek: bos kutu, yer ayrilmaz.
    const float left = b.remaining();
    const RectF none = b.take(left + 1, 20);
    EF_CHECK(none.w == 0 && none.h == 0 && b.remaining() == left);
    const RectF none2 = b.take_leading(left + 1, 20);
    EF_CHECK(none2.w == 0 && b.remaining() == left);
    EF_CHECK(b.take(0, 20).w == 0 && b.take(-5, 20).w == 0);
    // Tam sigan son oge: kalan alan negatife dusmez.
    const RectF last = b.take(left, 20);
    EF_CHECK(last.w == left && b.remaining() == 0);
    // Baslik seritten genis: yuva bos, negatif olcu yok.
    HeaderBar tiny = HeaderBar::make({0, 0, 50, 36}, 200, 8);
    EF_CHECK(tiny.remaining() == 0 && tiny.take(10, 20).w == 0 && tiny.take_leading(10, 20).w == 0);
}

EF_TEST(ui_easing_curve) {
    const std::array<float, 4> e{0.16f, 1.f, 0.3f, 1.f};
    EF_CHECK(cubic_bezier(e, 0) == 0 && cubic_bezier(e, 1) == 1);
    float prev = 0;
    for (int i = 1; i <= 20; ++i) {
        const float v = cubic_bezier(e, static_cast<float>(i) / 20.f);
        EF_CHECK(v >= prev - 1e-4f && v <= 1.0001f);  // monoton
        prev = v;
    }
    EF_CHECK(cubic_bezier(e, 0.5f) > 0.8f);  // hizli baslangic, yumusak bitis
    const std::array<float, 4> linear{0.25f, 0.25f, 0.75f, 0.75f};
    EF_CHECK(std::fabs(cubic_bezier(linear, 0.3f) - 0.3f) < 0.01f);
}

EF_TEST(ui_tween_and_reduced_motion) {
    const std::array<float, 4> e{0.16f, 1.f, 0.3f, 1.f};
    Tween tw(0);
    tw.retarget(1, 1000, 120);
    EF_CHECK(tw.active(1050) && !tw.active(1120));
    const float mid = tw.value(1060, e);
    EF_CHECK(mid > 0 && mid < 1);
    EF_CHECK(tw.value(1200, e) == 1);
    // Azaltilmis hareket: sure 0, deger aninda hedefte, zamanlayici gerekmez.
    Tween r(0);
    r.retarget(1, 5000, effective_ms(120, true));
    EF_CHECK(!r.active(5000) && r.value(5000, e) == 1);
    // Yari yolda yeniden hedef: sicrama yok (son konumdan devam).
    Tween j(0);
    j.retarget(1, 0, 100);
    const float at50 = j.value(50, e);
    j.retarget(0, 50, 100);
    EF_CHECK(std::fabs(j.value(50, e) - at50) < 1e-4f);
}
