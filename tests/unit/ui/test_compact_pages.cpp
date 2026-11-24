// Siki yerlesim (spec 2026-11-11 §5): Olcumler, Sistem araclari, Gecmis ve Ayarlar sayfalarinin saf kurallari — sutun
// esikleri, kare suresi cubuklari, journal satiri ac/kapa kimlikleri ve baslik seridi ogeleriyle odak sirasi.
// Cizim ui_snapshot'ta denetlenir.
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

namespace {
std::ptrdiff_t pos(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) - v.begin(); }
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
bool close_to(double a, double b) { return std::fabs(a - b) < 1e-9; }
}  // namespace

EF_TEST(compact_measure_top_row_columns_by_width) {
    // Ust satir: govde >= 1000 uc es kart; daha darsa Sonuc | Internet hizi; iki kart 300'er DIP sigmiyorsa alt alta.
    EF_CHECK(measure_top_columns(1148.f, 10.f) == 3);  // 1180 pencere
    EF_CHECK(measure_top_columns(kMeasureTop3MinW, 10.f) == 3);
    EF_CHECK(measure_top_columns(kMeasureTop3MinW - 1.f, 10.f) == 2);
    EF_CHECK(measure_top_columns(928.f, 10.f) == 2);  // 960 pencere
    EF_CHECK(measure_top_columns(2 * kMeasureTopCardMinW + 10.f, 10.f) == 2);
    EF_CHECK(measure_top_columns(2 * kMeasureTopCardMinW + 9.f, 10.f) == 1);
    EF_CHECK(measure_top_columns(0.f, 10.f) == 1);
}

EF_TEST(compact_cleanup_settings_columns) {
    // Temizlik kategorileri: kart ic genisliginde iki sutun; sutun 380 DIP altina dusuyorsa tek sutun.
    EF_CHECK(cleanup_columns(1124.f, 24.f) == 2);  // 1180 pencere
    EF_CHECK(cleanup_columns(904.f, 24.f) == 2);   // 960 pencere
    EF_CHECK(cleanup_columns(2 * kCleanupColMinW + 24.f, 24.f) == 2);
    EF_CHECK(cleanup_columns(2 * kCleanupColMinW + 23.f, 24.f) == 1);
    // Ayarlar: esit satirlar (GENEL | GORUNUM, Destek | Kaldirma) iki sutun; sutun 400 DIP altina dusuyorsa tek sutun.
    EF_CHECK(settings_columns(1148.f, 10.f) == 2);
    EF_CHECK(settings_columns(928.f, 10.f) == 2);
    EF_CHECK(settings_columns(2 * kSettingsColMinW + 10.f, 10.f) == 2);
    EF_CHECK(settings_columns(2 * kSettingsColMinW + 9.f, 10.f) == 1);
    // Gecmis (spec 2026-11-12 §3): journal tablosu, test gecmisi ve akran karti tam genislikte alt alta durur; sutun
    // kurali yoktur (esit satir mekanizmasi test_equal_rows.cpp'de).
}

EF_TEST(compact_frame_time_bars_only_from_measured_series) {
    // ms = 1000 / FPS; gecersiz ornek (0, negatif, sonlu olmayan) atlanir, yerine deger uydurulmaz.
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const auto bars = frame_time_bars({100.0, 50.0, 0.0, nan, -5.0, 200.0, inf, 25.0}, 60);
    EF_CHECK(bars.size() == 4);
    EF_CHECK(close_to(bars[0], 10.0) && close_to(bars[1], 20.0) && close_to(bars[2], 5.0) && close_to(bars[3], 40.0));
    // Seri cubuk sayisindan uzunsa esit kovalarin ortalamasi (sira korunur).
    const auto two = frame_time_bars({100.0, 50.0, 200.0, 25.0}, 2);
    EF_CHECK(two.size() == 2 && close_to(two[0], 15.0) && close_to(two[1], 22.5));
    const auto three = frame_time_bars({100.0, 100.0, 50.0, 50.0, 25.0, 25.0, 10.0}, 3);
    EF_CHECK(three.size() == 3 && close_to(three[0], 10.0) && close_to(three[1], 20.0) && close_to(three[2], (40.0 + 40.0 + 100.0) / 3));
    // Kova ortalamasi en kotu saniyeyi oldugundan iyi gosterir (60 ms < 100 ms): "en iyi / en kotu" metni bu yuzden
    // kovalanmamis listeden (max_bars = seri uzunlugu) okunur; o liste her gecerli saniyeyi oldugu gibi tasir.
    const std::vector<double> long_series{100.0, 100.0, 50.0, 50.0, 25.0, 25.0, 10.0};
    const auto per_second = frame_time_bars(long_series, long_series.size());
    EF_CHECK(per_second.size() == long_series.size());
    EF_CHECK(close_to(*std::max_element(per_second.begin(), per_second.end()), 100.0));
    EF_CHECK(close_to(*std::min_element(per_second.begin(), per_second.end()), 10.0));
    EF_CHECK(*std::max_element(three.begin(), three.end()) < 100.0);
    EF_CHECK(frame_time_bars({}, 60).empty());
    EF_CHECK(frame_time_bars({0.0, nan}, 60).empty());
    EF_CHECK(frame_time_bars({60.0, 60.0}, 0).empty());
    static_assert(kFrameBarsMinPoints == 4);
}

EF_TEST(compact_history_row_toggle_ids) {
    // Journal satiri ac/kapa dugmesi: kendi kimlik blogu (view.rows indeksi), UIA "history.op.N"; blok disi -> None.
    const Hit h = hit_of(HitKind::HistoryRow, 5);
    EF_CHECK(classify_hit(h).kind == HitKind::HistoryRow && classify_hit(h).index == 5);
    EF_CHECK(hit_automation_id(h) == L"history.op.5");
    EF_CHECK(hit_of(HitKind::HistoryRow, static_cast<std::size_t>(kMaxHistoryRows)) == Hit::None);
    EF_CHECK(classify_hit(hit_of(HitKind::HistorySelect, 5)).kind == HitKind::HistorySelect);  // test secimi blogu ayri
    // Acik/kapali durumu indekse degil kapsam + islem kimligine baglidir (yeni kayit eklenince kaymaz).
    ef::core::recovery::HistoryTxRow a, b, c;
    a.scope = "user", a.id = "t1";
    b.scope = "startup", b.id = "t1";
    c.scope = "user", c.id = "t2";
    EF_CHECK(history_row_key(a) != history_row_key(b) && history_row_key(a) != history_row_key(c));
    EF_CHECK(history_row_key(a) == "user/t1");
}

EF_TEST(compact_measure_focus_order_follows_visual_order) {
    UiState s;
    s.page = Page::Measurements;
    s.measure.builtin_available = true;
    s.measure.storage_roots = {"C:\\"};
    s.speed_can_start = true;
    const auto ord = focus_order(s);
    const Hit last_tab = nav_hit(static_cast<std::size_t>(Page::Settings));
    // B10: sekmeler -> Test karti (yuvarlak baslat, kaynak haplari, kapsam haplari, kart ici Sure ve Depolama testi
    // satirlari) -> Internet hizi -> uzun test -> "Son 3 test" satirlari. "Test ayarlari" acilir bolumu yok.
    EF_CHECK(pos(ord, last_tab) < pos(ord, Hit::SourceBuiltin));
    EF_CHECK(pos(ord, last_tab) < pos(ord, Hit::SourceBuiltin));  // C: baslat dugmesi seridin sag ucunda
    EF_CHECK(pos(ord, hit_of(HitKind::StorageChoice, 1)) < pos(ord, Hit::TestStart));
    EF_CHECK(pos(ord, Hit::SourceBuiltin) < pos(ord, Hit::SourceGame));
    EF_CHECK(pos(ord, Hit::SourceGame) < pos(ord, Hit::ScopeBalance));
    EF_CHECK(pos(ord, Hit::ScopeBalance) < pos(ord, Hit::ModeQuick));
    EF_CHECK(pos(ord, Hit::ModeQuick) < pos(ord, Hit::ModeFull));
    EF_CHECK(pos(ord, Hit::ModeFull) < pos(ord, hit_of(HitKind::StorageChoice, 0)));
    EF_CHECK(pos(ord, hit_of(HitKind::StorageChoice, 0)) < pos(ord, hit_of(HitKind::StorageChoice, 1)));
    EF_CHECK(pos(ord, Hit::TestStart) < pos(ord, Hit::SpeedStart));
    EF_CHECK(pos(ord, Hit::SpeedStart) < pos(ord, Hit::EnduranceStart));
    // Uzun test karti her zaman cizilir: onceki sonuc olmadan da baslatma duragi var; hata kartinda dugme yok.
    EF_CHECK(has(ord, Hit::EnduranceStart));
    s.measure.endurance.failed = true;
    EF_CHECK(!has(focus_order(s), Hit::EnduranceStart));
    s.measure.endurance.failed = false;
    // Oyunda Test kaynaginda sure / depolama satirlari cizilmez: olu Tab duragi yok.
    s.measure.source = ef::core::measure::TestSource::InGame;
    {
        const auto game = focus_order(s);
        for (Hit h : {Hit::ModeQuick, Hit::ModeFull, hit_of(HitKind::StorageChoice, 0)}) EF_CHECK(!has(game, h));
    }
    s.measure.source = ef::core::measure::TestSource::Builtin;
    // "Son 3 test" satirlari en sonda, gorunen sirayla; yalniz cizilenler.
    s.measure_recent_rows = 2;
    {
        const auto rec = focus_order(s);
        EF_CHECK(pos(rec, Hit::SpeedStart) < pos(rec, hit_of(HitKind::MeasureRecent, 0)));
        EF_CHECK(pos(rec, hit_of(HitKind::MeasureRecent, 0)) < pos(rec, hit_of(HitKind::MeasureRecent, 1)));
        EF_CHECK(!has(rec, hit_of(HitKind::MeasureRecent, 2)));
        EF_CHECK(hit_automation_id(hit_of(HitKind::MeasureRecent, 1)) == L"measure.recent.1");
        EF_CHECK(hit_of(HitKind::MeasureRecent, static_cast<std::size_t>(kMaxMeasureRecent)) == Hit::None);
    }
    s.measure_recent_rows = 0;
    // Test surerken: Durdur; kaynak secimi Tab almaz.
    s.measure.endurance = {};
    s.measure.phase = ef::core::measure::TestPhase::Running;
    const auto busy = focus_order(s);
    EF_CHECK(has(busy, Hit::TestStop) && !has(busy, Hit::TestStart) && !has(busy, Hit::SourceBuiltin));
    EF_CHECK(!has(busy, Hit::ModeQuick) && !has(busy, hit_of(HitKind::StorageChoice, 0)));  // kart ici secenekler kilitli
}

EF_TEST(compact_history_focus_order_header_then_rows) {
    UiState s;
    s.page = Page::History;
    s.history_tabs = true;
    s.history_filters = true;
    s.history_filter_shown = {true, true, true, true};  // B13: "Tumu" alt cipi ilk cip
    s.history_export_enabled = true;
    s.history_op_rows = {2, 0};  // gorunen sira (suzgec sonrasi view.rows indeksleri)
    s.history_test_rows = 2;
    const auto ord = focus_order(s);
    const Hit last_tab = nav_hit(static_cast<std::size_t>(Page::Settings));
    // Spec 2026-11-14 B13: sekmeler -> gorunum cipleri -> "Disa aktar" (baslik seridi) -> degisiklik kartindaki kapsam alt
    // suzgeci -> journal satirlari -> test kayitlari.
    EF_CHECK(pos(ord, last_tab) < pos(ord, Hit::HistoryTabAll));
    EF_CHECK(pos(ord, Hit::HistoryTabAll) < pos(ord, Hit::HistoryTabChanges));
    EF_CHECK(pos(ord, Hit::HistoryTabChanges) < pos(ord, Hit::HistoryTabTests));
    EF_CHECK(pos(ord, Hit::HistoryTabTests) < pos(ord, Hit::HistoryExport));
    EF_CHECK(pos(ord, Hit::HistoryExport) < pos(ord, Hit::HistoryFilterAll));
    EF_CHECK(pos(ord, Hit::HistoryFilterAll) < pos(ord, Hit::HistoryFilterPerf));
    EF_CHECK(pos(ord, Hit::HistoryFilterPerf) < pos(ord, Hit::HistoryFilterFixes));
    EF_CHECK(pos(ord, Hit::HistoryFilterFixes) < pos(ord, Hit::HistoryFilterStartup));
    EF_CHECK(pos(ord, Hit::HistoryFilterStartup) < pos(ord, hit_of(HitKind::HistoryRow, 2)));
    EF_CHECK(pos(ord, hit_of(HitKind::HistoryRow, 2)) < pos(ord, hit_of(HitKind::HistoryRow, 0)));
    EF_CHECK(pos(ord, hit_of(HitKind::HistoryRow, 0)) < pos(ord, hit_of(HitKind::HistorySelect, 0)));
    EF_CHECK(!has(ord, hit_of(HitKind::HistoryRow, 1)));  // suzulen satirin dugmesi yok
    // Cipler cizilmediyse (kayit yok / yer yok) ve disa aktarma devre disiysa Tab duragi yok.
    s.history_tabs = false;
    s.history_filters = false;
    s.history_export_enabled = false;
    const auto bare = focus_order(s);
    EF_CHECK(!has(bare, Hit::HistoryTabAll) && !has(bare, Hit::HistoryFilterPerf) && !has(bare, Hit::HistoryExport));
    s.page = Page::Overview;
    s.history_tabs = true;
    s.history_filters = true;
    EF_CHECK(!has(focus_order(s), Hit::HistoryTabAll) && !has(focus_order(s), Hit::HistoryFilterPerf) &&
              !has(focus_order(s), hit_of(HitKind::HistoryRow, 2)));
}

EF_TEST(history_tabs_and_day_key) {
    // Spec 2026-11-14 B13: gorunum cipleri ardisik sabit kimlikler; Tumu iki karti da gosterir. Gun anahtari yerel takvim gunu.
    EF_CHECK(history_tab_hit(HistoryTab::All) == Hit::HistoryTabAll && history_tab_hit(HistoryTab::Tests) == Hit::HistoryTabTests);
    EF_CHECK(hit_automation_id(Hit::HistoryTabChanges) == L"history.tab.changes");
    EF_CHECK(history_shows_changes(HistoryTab::All) && history_shows_tests(HistoryTab::All));
    EF_CHECK(history_shows_changes(HistoryTab::Changes) && !history_shows_tests(HistoryTab::Changes));
    EF_CHECK(!history_shows_changes(HistoryTab::Tests) && history_shows_tests(HistoryTab::Tests));
    EF_CHECK(history_day_key(0) == 0);
    const int k = history_day_key(1762300000000);  // 2025-11-04/05 (yerel saat dilimine gore)
    EF_CHECK(k >= 20251104 && k <= 20251105);
    EF_CHECK(history_day_key(1762300000000 + 86400000LL) > k);
}

EF_TEST(systools_cleanup_visible_rows_collapse_keeps_selection) {
    // Spec 2026-11-14 B12: kapali listede ilk 3 satir (cols sutun); secili / dosya listesi acik son kategori gizlenmez.
    EF_CHECK(cleanup_visible_rows(8, 2, 8, false) == 6);
    EF_CHECK(cleanup_visible_rows(8, 1, 8, false) == 3);
    EF_CHECK(cleanup_visible_rows(6, 2, 6, false) == 6);   // gizlenecek yok
    EF_CHECK(cleanup_visible_rows(8, 2, 6, false) == 8);   // 7. kategori secili: tum satiri gorunur
    EF_CHECK(cleanup_visible_rows(9, 2, 6, false) == 8);
    EF_CHECK(cleanup_visible_rows(8, 1, 4, false) == 5);
    EF_CHECK(cleanup_visible_rows(8, 2, 1, true) == 8);    // acik liste
    EF_CHECK(cleanup_visible_rows(0, 2, 0, false) == 0);
}

EF_TEST(compact_cleanup_focus_order_strip_rows_then_actions) {
    using ef::core::cleanup::SessionPhase;
    UiState s;
    s.page = Page::Cleanup;
    s.cleanup_phase = SessionPhase::Ready;
    s.cleanup_volumes = 2;
    s.cleanup_toggles = {true, true};
    s.cleanup_file_buttons = {true, false};
    s.cleanup_can_run = true;
    const auto ord = focus_order(s);
    // Spec 2026-11-14 B12: kart baslik seridindeki eylemler (Depolama, Onizleme, Temizle) -> surucu haplari -> kategori
    // kutulari (anahtar, dosya listesi) -> "+N kategori daha".
    EF_CHECK(pos(ord, Hit::CleanStorageSettings) < pos(ord, Hit::CleanScan));
    EF_CHECK(pos(ord, Hit::CleanScan) < pos(ord, Hit::CleanRun));
    EF_CHECK(pos(ord, Hit::CleanRun) < pos(ord, hit_of(HitKind::CleanVolume, 0)));
    EF_CHECK(pos(ord, hit_of(HitKind::CleanVolume, 1)) < pos(ord, hit_of(HitKind::CleanToggle, 0)));
    EF_CHECK(pos(ord, hit_of(HitKind::CleanToggle, 0)) < pos(ord, hit_of(HitKind::CleanFiles, 0)));
    EF_CHECK(pos(ord, hit_of(HitKind::CleanFiles, 0)) < pos(ord, hit_of(HitKind::CleanToggle, 1)));
    EF_CHECK(!has(ord, hit_of(HitKind::CleanFiles, 1)));
    EF_CHECK(!has(ord, Hit::CleanMore));
    s.cleanup_more = true;
    EF_CHECK(pos(focus_order(s), hit_of(HitKind::CleanToggle, 1)) < pos(focus_order(s), Hit::CleanMore));
    s.cleanup_more = false;
    s.cleanup_can_run = false;
    EF_CHECK(!has(focus_order(s), Hit::CleanRun));
    // Onay seridi ustte: Evet / Hayir ilk sayfa ogeleri; alt eylem satiri yok.
    s.cleanup_phase = SessionPhase::Confirming;
    const auto confirm = focus_order(s);
    EF_CHECK(pos(confirm, Hit::CleanYes) < pos(confirm, Hit::CleanNo));
    EF_CHECK(!has(confirm, Hit::CleanScan) && !has(confirm, Hit::CleanRun) && !has(confirm, hit_of(HitKind::CleanToggle, 0)));
    s.cleanup_phase = SessionPhase::Idle;
    const auto idle = focus_order(s);
    EF_CHECK(has(idle, Hit::CleanScan) && pos(idle, Hit::CleanStorageSettings) < pos(idle, Hit::CleanScan));
}

// Spec 2026-11-12 B4: Sistem araclari tek sayfa — sutun kurali, halka orani, bolum odak sirasi.
EF_TEST(systools_one_page_columns_ring_and_focus) {
    EF_CHECK(systools_columns(904.f, 16.f, 2, kSysToolsTileMinW) == 2);
    EF_CHECK(systools_columns(500.f, 16.f, 2, kSysToolsTileMinW) == 1);
    EF_CHECK(systools_columns(904.f, 16.f, 3, kSysToolsServiceMinW) == 3);
    EF_CHECK(systools_columns(600.f, 16.f, 3, kSysToolsServiceMinW) == 2);
    EF_CHECK(cleanup_ring_ratio(0, 0) == 0.f && cleanup_ring_ratio(50, 100) == 0.5f && cleanup_ring_ratio(200, 100) == 1.f);
    using ef::core::cleanup::SessionPhase;
    using ef::core::startup::StartupPhase;
    for (const Page pg : {Page::Cleanup, Page::Startup, Page::System}) {
        UiState s;
        s.page = pg;
        s.cleanup_phase = SessionPhase::Ready;
        s.cleanup_toggles = {true};
        s.startup_phase = StartupPhase::Ready;
        s.startup_can_apply = true;
        s.startup_toggles = {true};
        s.startup_service_toggles = {true};
        const auto o = focus_order(s);
        // Temizlik -> Baslangic ("Uygula", satirlar) -> hizmetler; hepsi tek sayfada.
        EF_CHECK(pos(o, hit_of(HitKind::CleanToggle, 0)) < pos(o, Hit::StartupApply));
        EF_CHECK(pos(o, Hit::StartupApply) < pos(o, hit_of(HitKind::StartupToggle, 0)));
        EF_CHECK(pos(o, hit_of(HitKind::StartupToggle, 0)) < pos(o, hit_of(HitKind::StartupServiceToggle, 0)));
    }
}

// Spec 2026-11-12 B6: zaman cizgisi nokta rengi, "Panel'e git" hedefi ve odak sirasi.
EF_TEST(history_timeline_dot_undo_target_and_focus) {
    namespace cr = ef::core::recovery;
    EF_CHECK(history_dot(cr::HistoryStatus::Active) == HistoryDot::Good);
    EF_CHECK(history_dot(cr::HistoryStatus::PartlyRestored) == HistoryDot::Partial);
    EF_CHECK(history_dot(cr::HistoryStatus::RecoveryRequired) == HistoryDot::Recovery);
    EF_CHECK(history_dot(cr::HistoryStatus::Restored) == HistoryDot::Neutral);
    EF_CHECK(history_undo_page("user") == Page::Overview && history_undo_page("machine") == Page::Overview);
    // Baslangic kaydi: Sistem araclari, Baslangic bolumune kaydirilmis (sayfa basina degil).
    EF_CHECK(history_undo_page("startup") == Page::Startup && systools_page(history_undo_page("startup")));
    EF_CHECK(history_undo_page("fixes") == Page::Readiness && history_undo_page("machine_fixes") == Page::Readiness);
    EF_CHECK(history_undo_shown(cr::HistoryStatus::Active) && history_undo_shown(cr::HistoryStatus::RecoveryRequired));
    EF_CHECK(!history_undo_shown(cr::HistoryStatus::Restored) && !history_undo_shown(cr::HistoryStatus::KeptCurrent));
    EF_CHECK(hit_automation_id(hit_of(HitKind::HistoryGo, 2)) == L"history.go.2");
    UiState s;
    s.page = Page::History;
    s.history_op_rows = {0, 1};
    s.history_go_rows = {0};
    s.history_test_rows = 2;
    const auto o = focus_order(s);
    EF_CHECK(pos(o, hit_of(HitKind::HistoryRow, 0)) + 1 == pos(o, hit_of(HitKind::HistoryGo, 0)));
    EF_CHECK(pos(o, hit_of(HitKind::HistoryGo, 0)) < pos(o, hit_of(HitKind::HistoryRow, 1)));
    EF_CHECK(!has(o, hit_of(HitKind::HistoryGo, 1)));
    EF_CHECK(pos(o, hit_of(HitKind::HistoryRow, 1)) < pos(o, hit_of(HitKind::HistorySelect, 0)));
    // Sayisi 0 olan suzgec cipi cizilmez: Tab sirasinda da yok.
    s.history_filters = true;
    s.history_filter_shown = {true, true, false, true};
    const auto f = focus_order(s);
    EF_CHECK(has(f, Hit::HistoryFilterAll) && !has(f, Hit::HistoryFilterFixes) && has(f, Hit::HistoryFilterStartup));
}

EF_TEST(settings_single_centred_list_width_and_focus_order) {
    // Spec 2026-11-12 B7: liste icerik alaninda ortali; en cok 780 (maket max-w-[780px]), dar pencerede tam genislik.
    EF_CHECK(std::fabs(settings_list_width(1148.f) - 780.f) < 1.f);  // 1180 pencere
    EF_CHECK(settings_list_width(1248.f) <= kSettingsListMaxW);
    EF_CHECK(settings_list_width(4000.f) == kSettingsListMaxW);
    EF_CHECK(settings_list_width(928.f) >= kSettingsListMinW && settings_list_width(928.f) < 928.f);  // 960 pencere
    EF_CHECK(settings_list_width(700.f) == 700.f);
    EF_CHECK(settings_list_width(0.f) == 0.f);
    // Odak sirasi B7 satir sirasiyla; cokme kaydi anahtari sayfada yok; oyun satirlari yalniz acikken.
    UiState s;
    s.page = Page::Settings;
    s.consent_rows = 2;
    auto o = focus_order(s);
    // P12-B3: Dil, Guncelleme, "Gelismis" (+ acikken alt satirlar), en sonda "Hakkinda ve lisanslar".
    s.settings_advanced_open = true;
    o = focus_order(s);
    const Hit rows[] = {Hit::SettingsLanguage, Hit::SettingsUpdateCheck, Hit::SettingsAdvanced, Hit::Autostart, Hit::AppAnimations,
                        Hit::SettingsStrip, Hit::SettingsCleanupHint, Hit::SettingsGamesShow, consent_reset_hit(0), consent_reset_hit(1), Hit::SupportCreate,
                        Hit::SettingsLicenses};
    for (std::size_t i = 1; i < std::size(rows); ++i) EF_CHECK(pos(o, rows[i - 1]) < pos(o, rows[i]));
    EF_CHECK(!has(o, Hit::SupportDumps) && !has(o, Hit::AutostartNoticeOff));
    s.consent_rows = 0;
    EF_CHECK(!has(focus_order(s), consent_reset_hit(0)));
    // Kapali "Gelismis": yalniz Dil, Guncelleme, Gelismis, Hakkinda Tab alir; alt satirlar yok.
    s.settings_advanced_open = false;
    s.consent_rows = 2;
    o = focus_order(s);
    const Hit visible[] = {Hit::SettingsLanguage, Hit::SettingsUpdateCheck, Hit::SettingsAdvanced, Hit::SettingsLicenses};
    for (std::size_t i = 1; i < std::size(visible); ++i) EF_CHECK(pos(o, visible[i - 1]) < pos(o, visible[i]));
    for (const Hit h : {Hit::Autostart, Hit::AppAnimations, Hit::SettingsStrip, Hit::SettingsCleanupHint, Hit::SettingsGamesShow, Hit::SupportCreate, consent_reset_hit(0)})
        EF_CHECK(!has(o, h));
    EF_CHECK(std::wstring(hit_automation_id(Hit::SettingsAdvanced)) == L"settings.advanced.toggle");
    s.settings_advanced_open = true;
    s.consent_rows = 0;
    // Spec G: bildirim seridinin "Kapat" dugmesi ust menuden hemen sonra, sayfadan once.
    s.autostart_notice = true;
    o = focus_order(s);
    const Hit last_tab = nav_hit(static_cast<std::size_t>(Page::Settings));
    EF_CHECK(pos(o, last_tab) + 1 == pos(o, Hit::AutostartNoticeOff) && pos(o, Hit::AutostartNoticeOff) < pos(o, Hit::Autostart));
}
