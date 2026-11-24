// R13/INS-03: uzun test raporunun urun gorunumu + rapor eslemesi.
// Kural: olculmeyen deger sayi URETMEZ; sensor yoksa GPU saati/frekans yok; saglik raporu degil.
#include <algorithm>

#include "app/endurance_report_map.h"
#include "benchmark/endurance/endurance.h"
#include "core/measure/endurance_view.h"
#include "core/measure/view.h"
#include "ef_test.h"

using namespace ef::core::measure;

namespace {
bool has_key(const std::vector<std::string>& keys, std::string_view k) {
    return std::any_of(keys.begin(), keys.end(), [&](const std::string& s) { return std::string_view(s) == k; });
}
bool has_label(const std::vector<EnduranceRow>& rows, std::string_view k) {
    return std::any_of(rows.begin(), rows.end(), [&](const EnduranceRow& r) { return std::string_view(r.label_key) == k; });
}
}  // namespace

EF_TEST(endurance_view_missing_measurement_produces_no_numbers) {
    // Bos rapor: soguk/isinmis satirlari vardir ama deger yoktur; metrik/gecmis kaydi sayi uretmez.
    const EnduranceView v;
    EF_CHECK(!v.has_comparison());
    const auto rows = endurance_rows(v);
    EF_CHECK(has_label(rows, "measure.endurance.cold") && has_label(rows, "measure.endurance.warm"));
    for (const auto& r : rows) EF_CHECK(!r.value.has_value());  // 0 veya tahmin yok
    EF_CHECK(endurance_metrics(v).empty());
    const auto notes = endurance_note_keys(v);
    EF_CHECK(has_key(notes, "measure.endurance.sensors_missing"));
    EF_CHECK(has_key(notes, "measure.endurance.insufficient"));
    EF_CHECK(has_key(notes, "measure.endurance.not_health"));
    EF_CHECK(has_key(endurance_exclusions(v), "endurance.sensors_unavailable"));
    EF_CHECK(has_key(endurance_exclusions(v), "endurance.insufficient_windows"));
}

EF_TEST(endurance_report_mapping_does_not_invent_values) {
    // Varsayilan rapor = analiz degeri yok. Esleme sayi URETMEMELI; neden de yazilmamali.
    ef::benchmark::EnduranceReport rep;
    const auto v = ef::app::endurance_view_from_report(rep);
    EF_CHECK(!v.cold_fps && !v.warm_fps && !v.fps_change_pct && !v.cold_gpu_mhz && !v.warm_gpu_mhz);
    EF_CHECK(!v.sensors_available && !v.frequency_dropped && !v.cause_vendor_thermal);
    EF_CHECK(endurance_metrics(v).empty());
    const auto notes = endurance_note_keys(v);
    EF_CHECK(has_key(notes, "measure.endurance.sensors_missing"));
    EF_CHECK(!has_key(notes, "measure.endurance.cause") && !has_key(notes, "measure.endurance.cause_unknown"));
}

EF_TEST(endurance_report_mapping_carries_only_measured_sensor_values) {
    ef::benchmark::EnduranceReport rep;
    rep.stop = ef::benchmark::EnduranceStop::TimeBudget;
    rep.cold_fps = 120.0;
    rep.warm_fps = 110.0;
    rep.fps_change_pct = -8.3;
    const auto v = ef::app::endurance_view_from_report(rep);
    EF_CHECK(v.stop == EnduranceStopKind::TimeBudget && v.has_comparison());
    const auto metrics = endurance_metrics(v);
    EF_CHECK(metrics.size() == 3 && metrics.at("cold_fps") == 120.0 && metrics.at("warm_fps") == 110.0);
    EF_CHECK(!metrics.contains("gpu_clock_cold_mhz"));  // sensor yok
    const auto ex = endurance_exclusions(v);
    EF_CHECK(has_key(ex, "endurance.time_budget") && has_key(ex, "endurance.sensors_unavailable"));
    const auto rows = endurance_rows(v);
    EF_CHECK(!has_label(rows, "measure.endurance.gpu_cold") && !has_label(rows, "measure.endurance.gpu_warm"));
    EF_CHECK(has_key(endurance_note_keys(v), "measure.endurance.sensors_missing"));
}

EF_TEST(endurance_report_mapping_sensor_frequency_and_cause) {
    ef::benchmark::EnduranceReport rep;
    rep.stop = ef::benchmark::EnduranceStop::Completed;
    rep.cold_fps = 100.0;
    rep.warm_fps = 90.0;
    rep.fps_change_pct = -10.0;
    rep.sensors_available = true;
    rep.cold_gpu_mhz = 1800.0;
    rep.warm_gpu_mhz = 1500.0;
    rep.frequency_dropped = true;
    rep.cause = "vendor_thermal_flag";
    const auto v = ef::app::endurance_view_from_report(rep);
    EF_CHECK(v.sensors_available && v.frequency_dropped && v.cause_vendor_thermal);
    const auto metrics = endurance_metrics(v);
    EF_CHECK(metrics.contains("gpu_clock_cold_mhz") && metrics.contains("gpu_clock_warm_mhz"));
    const auto rows = endurance_rows(v);
    EF_CHECK(has_label(rows, "measure.endurance.gpu_cold") && has_label(rows, "measure.endurance.gpu_warm"));
    const auto notes = endurance_note_keys(v);
    EF_CHECK(has_key(notes, "measure.endurance.cause") && !has_key(notes, "measure.endurance.sensors_missing"));
    EF_CHECK(has_key(endurance_exclusions(v), "endurance.frequency_dropped"));

    // Ayni olcum, neden bayragi yok: "neden" yazilmaz (yalniz gozlenen bayrak yazilir).
    rep.cause.clear();
    const auto v2 = ef::app::endurance_view_from_report(rep);
    const auto notes2 = endurance_note_keys(v2);
    EF_CHECK(!v2.cause_vendor_thermal);
    EF_CHECK(has_key(notes2, "measure.endurance.cause_unknown") && !has_key(notes2, "measure.endurance.cause"));

    // Sensor hic okunmadiysa bayrak/rapor alanlari gelse bile frekans dususu ve neden URETILMEZ.
    rep.sensors_available = false;
    rep.cause = "vendor_thermal_flag";
    const auto v3 = ef::app::endurance_view_from_report(rep);
    EF_CHECK(!v3.frequency_dropped && !v3.cause_vendor_thermal);
    EF_CHECK(!has_key(endurance_exclusions(v3), "endurance.frequency_dropped"));
    const auto notes3 = endurance_note_keys(v3);
    EF_CHECK(!has_key(notes3, "measure.endurance.cause") && !has_key(notes3, "measure.endurance.cause_unknown"));
    EF_CHECK(has_key(notes3, "measure.endurance.sensors_missing"));
}

EF_TEST(endurance_stop_keys_are_distinct) {
    EF_CHECK(endurance_stop_key(EnduranceStopKind::Completed).empty());
    // B14 incelemesi: erken durdurmada kismi / en az 2 dakika notu yazilir; ayri iptal satiri (ve dizgisi) yok.
    EF_CHECK(endurance_stop_key(EnduranceStopKind::Cancelled).empty());
    EF_CHECK(endurance_stop_key(EnduranceStopKind::TimeBudget) == "measure.endurance.stop.time_budget");
    EF_CHECK(endurance_stop_key(EnduranceStopKind::ByteBudget) == "measure.endurance.stop.byte_budget");
    EF_CHECK(endurance_stop_key(EnduranceStopKind::MemoryBudget) == "measure.endurance.stop.memory_budget");
}

EF_TEST(endurance_view_start_guards_and_real_elapsed) {
    MeasureState m;
    EF_CHECK(!endurance_available(m) && !endurance_can_start(m));  // motor bagli degil
    m.builtin_available = true;
    EF_CHECK(endurance_available(m) && endurance_can_start(m) && can_start(m));
    m.game_running = true;  // oyun calisirken agir test yok
    m.gate_blocked = true;
    EF_CHECK(!endurance_available(m) && !endurance_can_start(m));
    m.game_running = false;
    m.gate_blocked = false;
    m.phase = TestPhase::Running;  // baska test suruyorken uzun test baslamaz
    EF_CHECK(endurance_available(m) && !endurance_can_start(m));
    m.phase = TestPhase::Idle;
    m.endurance.running = true;
    EF_CHECK(!endurance_can_start(m) && !can_start(m));  // ikinci test yok
    // Ilerleme yalniz GERCEK duvar saatinden: taban yoksa 0; taban varsa artar, geri saymaz.
    EF_CHECK(!m.endurance.running || endurance_elapsed_s(m, 100000) == 0.0);
    m.endurance.started_ms = 100000;
    EF_CHECK(endurance_elapsed_s(m, 100000) == 0.0);
    EF_CHECK(endurance_elapsed_s(m, 101500) == 1.5);
    EF_CHECK(endurance_elapsed_s(m, 99000) == 0.0);  // taban oncesi sahte deger yok
    m.endurance.elapsed_s = 2.0;                       // geri bildirilen deger de korunur (azalmaz)
    EF_CHECK(endurance_elapsed_s(m, 101000) == 2.0);
    m.endurance.running = false;
    EF_CHECK(endurance_elapsed_s(m, 200000) == 2.0);
    m.endurance.elapsed_s = -1.0;
    EF_CHECK(endurance_elapsed_s(m, 200000) == 0.0);
}

EF_TEST(x21_new_stops_are_recorded_with_reasons) {
    using namespace ef::core::measure;
    const auto has = [](const std::vector<std::string>& v, const char* k) { return std::find(v.begin(), v.end(), k) != v.end(); };
    EnduranceView v;
    v.stop = EnduranceStopKind::DeviceLost;
    EF_CHECK(has(endurance_exclusions(v), "endurance.device_lost") && has(endurance_note_keys(v), "measure.endurance.stop.device_lost"));
    v.stop = EnduranceStopKind::HardwareError;
    EF_CHECK(has(endurance_exclusions(v), "endurance.hardware_error"));
    v.stop = EnduranceStopKind::SensorLost;
    EF_CHECK(has(endurance_exclusions(v), "endurance.sensor_lost"));
    v.sensors_available = true;
    v.frequency_dropped = true;
    v.limit_flag_seen = true;
    EF_CHECK(has(endurance_note_keys(v), "measure.endurance.flag_seen_output_held"));
    EF_CHECK(!has(endurance_note_keys(v), "measure.endurance.cause_unknown"));
    v.load_unknown = true;
    EF_CHECK(has(endurance_note_keys(v), "measure.endurance.load_unknown"));
}

EF_TEST(b14_phase_labels_follow_runner_clock) {
    EF_CHECK(endurance_phase(false, 0, 600) == EndurancePhase::Preparing);  // sahne kuruluyor
    EF_CHECK(endurance_phase(true, 0, 600) == EndurancePhase::Cold);
    EF_CHECK(endurance_phase(true, 59.9, 600) == EndurancePhase::Cold);
    EF_CHECK(endurance_phase(true, 60, 600) == EndurancePhase::Warming);
    EF_CHECK(endurance_phase(true, 539.9, 600) == EndurancePhase::Warming);
    EF_CHECK(endurance_phase(true, 540, 600) == EndurancePhase::Warm);
    EF_CHECK(endurance_phase(true, -1, 600) == EndurancePhase::Preparing);
    EF_CHECK(endurance_phase_key(EndurancePhase::Cold) == "measure.endurance.phase.cold");
    EF_CHECK(endurance_phase_key(EndurancePhase::Warming) == "measure.endurance.phase.warming");
    EF_CHECK(endurance_phase_key(EndurancePhase::Warm) == "measure.endurance.phase.warm");
    EF_CHECK(endurance_phase_key(EndurancePhase::Preparing) == "measure.endurance.phase.preparing");
}

EF_TEST(b14_partial_and_too_short_notes) {
    EnduranceView v;
    v.stop = EnduranceStopKind::Cancelled;  // 2 dakikadan once: karsilastirma yok
    auto n = endurance_note_keys(v);
    EF_CHECK(has_key(n, "measure.endurance.too_short") && !has_key(n, "measure.endurance.partial"));
    EF_CHECK(!has_key(n, "measure.endurance.stable") && !has_key(n, "measure.endurance.not_stable"));
    EF_CHECK(!v.partial());
    v.cold_fps = 150.0;  // 2 dakikadan sonra: pencereler dolu -> kismi sonuc, satirlar degerli
    v.warm_fps = 146.0;
    v.duration_s = 241;
    v.stability_checked = true;  // kismi kosuda sabitlik hukmu verilmez
    v.stable = true;
    n = endurance_note_keys(v);
    EF_CHECK(v.partial() && has_key(n, "measure.endurance.partial") && !has_key(n, "measure.endurance.too_short"));
    EF_CHECK(!has_key(n, "measure.endurance.stable") && !has_key(n, "measure.endurance.insufficient"));
    const auto rows = endurance_rows(v);
    EF_CHECK(rows.size() >= 2 && rows[0].value && rows[1].value);
    EF_CHECK(has_key(endurance_exclusions(v), "endurance.cancelled"));  // gecmiste gecerli kosu sayilmaz
}

EF_TEST(b14_stability_notes_only_for_completed_runs) {
    EnduranceView v;
    v.stop = EnduranceStopKind::Completed;
    v.cold_fps = 150.0;
    v.warm_fps = 148.0;
    v.stability_checked = true;
    v.stable = true;
    auto n = endurance_note_keys(v);
    EF_CHECK(has_key(n, "measure.endurance.stable") && !has_key(n, "measure.endurance.not_stable"));
    EF_CHECK(!has_key(n, "measure.endurance.stable_fps_only"));
    v.stable = false;
    v.stability_fps_only = true;
    n = endurance_note_keys(v);
    EF_CHECK(has_key(n, "measure.endurance.not_stable") && has_key(n, "measure.endurance.stable_fps_only"));
    v.stability_checked = false;  // degerlendirilemedi: hukum yok
    n = endurance_note_keys(v);
    EF_CHECK(!has_key(n, "measure.endurance.stable") && !has_key(n, "measure.endurance.not_stable"));
    v.stability_checked = true;
    v.stop = EnduranceStopKind::TimeBudget;  // butce durdurmasi: hukum yok
    n = endurance_note_keys(v);
    EF_CHECK(!has_key(n, "measure.endurance.stable") && !has_key(n, "measure.endurance.not_stable"));

    // Esleme: analizdeki sabitlik ve sure tasinir; degerlendirilmediyse "sabit" uretilmez.
    ef::benchmark::EnduranceReport rep;
    rep.stop = ef::benchmark::EnduranceStop::Completed;
    rep.stable = true;  // stability_checked false: tasinmaz
    rep.duration_s = 600;
    auto mv = ef::app::endurance_view_from_report(rep);
    EF_CHECK(!mv.stable && !mv.stability_checked && mv.duration_s == 600);
    rep.stability_checked = true;
    rep.stability_fps_only = true;
    mv = ef::app::endurance_view_from_report(rep);
    EF_CHECK(mv.stable && mv.stability_checked && mv.stability_fps_only);
    EF_CHECK(!mv.telemetry_unsupported);
    rep.telemetry_unsupported = true;
    mv = ef::app::endurance_view_from_report(rep);
    EF_CHECK(mv.telemetry_unsupported);
}

// B14 incelemesi: desteklenmeyen sensor (NVIDIA disi / sahne baska kartta) "okunamadi" diye yazilmaz.
EF_TEST(b14_unsupported_telemetry_is_not_unreadable) {
    EnduranceView v;
    v.stop = EnduranceStopKind::Completed;
    v.cold_fps = 150.0;
    v.warm_fps = 148.0;
    v.stability_checked = true;
    v.stable = true;
    v.stability_fps_only = true;
    auto n = endurance_note_keys(v);  // destekli ama okunamadi
    EF_CHECK(has_key(n, "measure.endurance.stable_fps_only") && has_key(n, "measure.endurance.sensors_missing"));
    EF_CHECK(!has_key(n, "measure.endurance.stable_fps_only_unsupported") && !has_key(n, "measure.endurance.sensors_unsupported"));
    v.telemetry_unsupported = true;  // desteklenmiyor
    n = endurance_note_keys(v);
    EF_CHECK(has_key(n, "measure.endurance.stable_fps_only_unsupported") && has_key(n, "measure.endurance.sensors_unsupported"));
    EF_CHECK(!has_key(n, "measure.endurance.stable_fps_only") && !has_key(n, "measure.endurance.sensors_missing"));
    EF_CHECK(has_key(n, "measure.endurance.stable") && has_key(n, "measure.endurance.not_health"));
}
