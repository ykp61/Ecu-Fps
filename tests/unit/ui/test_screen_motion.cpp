#include <algorithm>
#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/motion/motion.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

EF_TEST(ui_reduced_motion_removes_all_transitions) {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    UiState s;
    s.reduced_motion = true;
    screen.set_hover(s, nav_hit(2), 1000);
    screen.go(s, Page::Measurements, 1000);
    PerfView before, after;
    after.phase = ef::core::perf::Phase::Applying;
    after.done = 1;
    after.total = 4;
    sync_perf_animations(s, before, after, theme, 1000);
    after.phase = ef::core::perf::Phase::On;
    after.switch_on = true;
    sync_perf_animations(s, before, after, theme, 1000);
    EF_CHECK(!screen.animating(s, 1000));  // hicbir gecis zamanlayici istemez
    EF_CHECK(s.toggle_knob.value(1000, theme.easing) == 1.f);
    EF_CHECK(s.page_fade.value(1000, theme.easing) == 1.f);

    // Normal harekette ayni olaylar kisa sureli gecis baslatir ve sure sonunda biter (bos animasyon yok).
    UiState n;
    screen.set_hover(n, nav_hit(2), 1000);
    screen.go(n, Page::Measurements, 1000);
    EF_CHECK(screen.animating(n, 1010));
    EF_CHECK(!screen.animating(n, 1000 + theme.motion.details + theme.motion.page + 1));
}

// Spec 2026-11-12 B4: eski Baslangic kimligi tek sayfayi Baslangic bolumune kaydirir; Gecmis'teki "Sistem araclari'na
// git" de ayni hedefi kullanir (sayfa basindaki Temizlik'e degil).
EF_TEST(ui_systools_startup_target_reveals_section) {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    Screen screen(default_theme(), cat.value());
    UiState s;
    screen.go(s, history_undo_page("startup"), 1000);
    EF_CHECK(systools_page(s.page));
    EF_CHECK(s.systools_reveal == 2);
    UiState c;
    screen.go(c, Page::Cleanup, 1000);
    EF_CHECK(c.systools_reveal == 0);
}

EF_TEST(ui_hover_and_press_feedback_are_per_element) {
    // DS-02: her oge kendi hover/basma gecisini tasir; bir ogeye gelmek digerini etkilemez (capraz hover yok).
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    UiState s;
    const std::int64_t t0 = 1000;
    screen.set_hover(s, Hit::Details, t0);
    const std::int64_t h1 = t0 + theme.motion.hover + 1;
    EF_CHECK(screen.hover_value(s, Hit::Details, h1) > 0.9f);
    EF_CHECK(screen.hover_value(s, Hit::No, h1) == 0.f);
    screen.set_hover(s, Hit::No, t0 + 200);
    const std::int64_t h2 = t0 + 200 + theme.motion.hover + 1;
    EF_CHECK(screen.hover_value(s, Hit::No, h2) > 0.9f);
    EF_CHECK(screen.hover_value(s, Hit::Details, h2) < 0.1f);  // eski oge soner

    screen.set_pressed(s, Hit::Yes, t0 + 400);
    const std::int64_t p1 = t0 + 400 + theme.motion.press + 1;
    EF_CHECK(screen.press_value(s, Hit::Yes, p1) > 0.9f);
    EF_CHECK(screen.press_value(s, Hit::No, p1) == 0.f);
    screen.set_pressed(s, Hit::None, t0 + 500);
    EF_CHECK(s.pressed == Hit::None);
    EF_CHECK(screen.press_value(s, Hit::Yes, t0 + 500 + theme.motion.press + 1) < 0.1f);
}

EF_TEST(ui_press_feedback_respects_reduced_motion) {
    // DS-02: azaltilmis harekette basma geri bildirimi aninda olur, zamanlayici istemez.
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    UiState s;
    s.reduced_motion = true;
    screen.set_hover(s, Hit::Details, 500);
    screen.set_pressed(s, Hit::Details, 500);
    EF_CHECK(screen.hover_value(s, Hit::Details, 500) == 1.f);
    EF_CHECK(screen.press_value(s, Hit::Details, 500) == 1.f);
    EF_CHECK(!screen.animating(s, 500));
}

EF_TEST(ui_game_card_lift_timing_and_gates) {
    // Spec 2026-11-12 B2 / F: oyun karti hover'da motion.card_lift (200 ms) ile yukselir. Oyun acikken / test surerken /
    // pilde (motion_paused) ve azaltilmis harekette gecis yoktur: zamanlayici istenmez, deger aninda oturur. Diger
    // ogelerin hover suresi (motion.hover) degismez.
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    const Hit card = hit_of(HitKind::GameCard, 1);
    EF_CHECK(theme.motion.card_lift > theme.motion.hover);
    UiState s;
    screen.set_hover(s, card, 1000);
    EF_CHECK(screen.animating(s, 1000 + theme.motion.hover + 1));  // kart gecisi dugme hover'indan uzun surer
    EF_CHECK(screen.hover_value(s, card, 1000 + theme.motion.hover + 1) < 1.f);
    EF_CHECK(!screen.animating(s, 1000 + theme.motion.card_lift + 1));
    EF_CHECK(screen.hover_value(s, card, 1000 + theme.motion.card_lift + 1) == 1.f);

    UiState p;
    p.motion_paused = true;
    screen.set_hover(p, card, 1000);
    EF_CHECK(!screen.animating(p, 1000));
    EF_CHECK(screen.hover_value(p, card, 1000) == 1.f);
    screen.set_hover(p, Hit::GamesRescan, 2000);  // karttan cikis da aninda; dugmenin kendi hover gecisi surer
    EF_CHECK(screen.hover_value(p, card, 2000) == 0.f);
    EF_CHECK(screen.animating(p, 2001));
    EF_CHECK(!screen.animating(p, 2000 + theme.motion.hover + 1));

    UiState r;
    r.reduced_motion = true;
    screen.set_hover(r, card, 1000);
    EF_CHECK(!screen.animating(r, 1000));
    EF_CHECK(screen.hover_value(r, card, 1000) == 1.f);
}

EF_TEST(ui_app_animations_setting_forces_reduced_motion) {
    // UI-11: tercih kapali -> sistem animasyonlari acik olsa bile hareket yok; varsayilan acik.
    EF_CHECK(!reduced_motion_for(true, true));
    EF_CHECK(reduced_motion_for(true, false));
    EF_CHECK(reduced_motion_for(false, true));
    EF_CHECK(reduced_motion_for(false, false));
    EF_CHECK(effective_ms(220, reduced_motion_for(true, false)) == 0);
    // R33: uygulama anahtari, Performans'in kaydettigi ozgun Windows degeri acik olsa bile hareketi kapatir.
    EF_CHECK(effective_ms(220, reduced_motion_for(app_motion_enabled(false, true), false)) == 0);
    EF_CHECK(effective_ms(220, reduced_motion_for(app_motion_enabled(false, true), true)) > 0);
}

EF_TEST(ui_every_page_control_is_in_focus_order) {
    UiState s;
    for (std::size_t p = 0; p < kPageCount; ++p) {
        s.page = static_cast<Page>(p);
        const auto order = focus_order(s);
        EF_CHECK(order.size() >= kNavTabCount);  // ust menu her sayfada klavyeyle erisilebilir (7 sekme)
    }
    s.page = Page::Overview;
    s.perf_phase = ef::core::perf::Phase::Confirming;
    auto o = focus_order(s);
    EF_CHECK(std::find(o.begin(), o.end(), Hit::Yes) != o.end() && std::find(o.begin(), o.end(), Hit::No) != o.end());
    s.perf_phase = ef::core::perf::Phase::RecoveryNeeded;
    o = focus_order(s);
    EF_CHECK(std::find(o.begin(), o.end(), Hit::Retry) != o.end());
    // Panel: kahraman karti (anahtar) -> SON TEST -> Oyun hazirligi ozeti gorsel sirasiyla.
    s.perf_phase = ef::core::perf::Phase::Off;
    o = focus_order(s);
    const auto at = [&](Hit h) { return std::find(o.begin(), o.end(), h) - o.begin(); };
    EF_CHECK(at(Hit::Toggle) < at(Hit::OpenMeasure) && at(Hit::OpenMeasure) < at(Hit::OpenReadiness) &&
              at(Hit::OpenReadiness) < static_cast<std::ptrdiff_t>(o.size()));
}

EF_TEST(ui_app_motion_follows_original_value_while_performance_on) {
    using ef::ui::app_motion_enabled;
    EF_CHECK(app_motion_enabled(true, std::nullopt));    // Performans yok: sistemi izle
    EF_CHECK(!app_motion_enabled(false, std::nullopt));  // kullanici Windows'ta kapatmis
    EF_CHECK(app_motion_enabled(false, true));           // Performans kapatti, ozgun deger acik
    EF_CHECK(!app_motion_enabled(false, false));         // ozgun deger zaten kapaliydi
}

// Zehir yesili RGB serit: yalniz tum kosullar uygunsa kayar; her tek kosul onu durdurur.
EF_TEST(strip_animates_only_when_allowed) {
    StripInputs all;
    all.visible = all.foreground = true;
    all.game_blocked = all.test_running = all.on_battery = all.reduced_motion = false;
    all.app_animations = all.strip_setting = true;
    all.high_contrast = false;
    EF_CHECK(strip_should_animate(all));
    bool StripInputs::*flags[] = {&StripInputs::visible,        &StripInputs::foreground,     &StripInputs::game_blocked,
                                  &StripInputs::test_running,   &StripInputs::on_battery,     &StripInputs::reduced_motion,
                                  &StripInputs::app_animations, &StripInputs::strip_setting,  &StripInputs::high_contrast};
    for (auto f : flags) {
        StripInputs in = all;
        in.*f = !(in.*f);
        EF_CHECK(!strip_should_animate(in));
    }
    EF_CHECK(!strip_should_animate(StripInputs{}));  // varsayilan (gorunmez) durur
}

EF_TEST(strip_phase_wraps) {
    EF_CHECK(strip_phase(0, 8000) == 0.f);
    EF_CHECK(strip_phase(4000, 8000) == 0.5f);
    EF_CHECK(strip_phase(8000, 8000) == 0.f);
    EF_CHECK(strip_phase(12000, 8000) == 0.5f);
    const float f = strip_phase(-2000, 8000);
    EF_CHECK(f >= 0.f && f < 1.f);
    EF_CHECK(strip_phase(1234, 0) == 0.f);
}

// Serit kendi 20 kare/sn zamanlayicisini kullanir: oturmus ekranda 60 Hz gecis zamanlayicisi istenmez
// (serit kapali da acik da). Boylece bos durumda yalniz serit dikdortgeni yenilenir.
EF_TEST(ui_settled_screen_not_animating_with_strip_state) {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    UiState s;
    s.strip_live = false;
    EF_CHECK(!screen.animating(s, 100000));
    s.strip_live = true;
    EF_CHECK(!screen.animating(s, 100000));
    EF_CHECK(theme.strip_period_ms == 8000 && theme.strip_h == 3.f);
}
