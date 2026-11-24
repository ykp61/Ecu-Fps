#include <cmath>

#include "benchmark/endurance/endurance.h"
#include "ef_test.h"

using namespace ef::benchmark;

namespace {
std::vector<EnduranceSample> run(double secs, bool sensors, bool flag) {
    std::vector<EnduranceSample> v;
    for (double t = 0; t <= secs; t += 1) {
        EnduranceSample s;
        s.t_s = t;
        const double heat = t / secs;  // isindikca dusus
        s.fps = 200.0 - 20.0 * heat;
        if (sensors) s.gpu_clock_mhz = 2800.0 - 300.0 * heat, s.gpu_load_pct = 98.0;  // neden icin esdeger yuk kaniti
        if (flag) s.vendor_thermal_flag = heat > 0.7;
        v.push_back(s);
    }
    return v;
}
}  // namespace

EF_TEST(endurance_budgets_and_cancel) {
    EnduranceBudget b;
    EnduranceSample s;
    s.t_s = 10;
    EF_CHECK(!budget_exceeded(b, s));
    s.t_s = 601;
    EF_CHECK(!budget_exceeded(b, s));            // planli sure doldu ama guvenlik tavani asilmadi
    EF_CHECK(planned_duration_reached(b, s));    // X21 gercek kabul: tam kosu Completed olur
    s.t_s = 599;
    EF_CHECK(!planned_duration_reached(b, s));
    s.t_s = 661;
    EF_CHECK(budget_exceeded(b, s) == EnduranceStop::TimeBudget);
    {
        EnduranceBudget bad;  // planli sure tavandan buyukse "tamamlandi" uretilmez
        bad.planned_seconds = 700;
        EF_CHECK(!planned_duration_reached(bad, s));
    }
    s.t_s = 1, s.bytes_written = (1ull << 30) + 1;
    EF_CHECK(budget_exceeded(b, s) == EnduranceStop::ByteBudget);
    s.bytes_written = 0, s.memory_bytes = 600ull << 20;
    EF_CHECK(budget_exceeded(b, s) == EnduranceStop::MemoryBudget);
    const auto r = analyze_endurance(run(30, true, false), EnduranceStop::Cancelled);
    EF_CHECK(r.stop == EnduranceStop::Cancelled && !r.cold_fps);  // kisa/iptal: soguk-isinmis karsilastirma yok
}

EF_TEST(endurance_warm_cold_and_cause_only_with_flag) {
    auto r = analyze_endurance(run(600, true, true), EnduranceStop::Completed);
    EF_CHECK(r.cold_fps && r.warm_fps && *r.fps_change_pct < -5 && r.frequency_dropped && r.cause == "vendor_thermal_flag");
    EF_CHECK(!r.is_health_report);
    r = analyze_endurance(run(600, true, false), EnduranceStop::Completed);  // bayrak desteklenmiyor
    EF_CHECK(r.frequency_dropped && r.cause.empty());                          // neden uydurulmaz
    r = analyze_endurance(run(600, false, false), EnduranceStop::Completed);  // sensor yok
    EF_CHECK(!r.sensors_available && !r.frequency_dropped && r.fps_change_pct);
}

static std::vector<EnduranceSample> x21_run(double cold_load, double warm_load, double fps_loss) {
    std::vector<EnduranceSample> v;
    for (int t = 0; t <= 300; ++t) {
        EnduranceSample e;
        const bool warm = t > 150;
        e.t_s = t;
        e.fps = warm ? 200.0 - fps_loss : 200.0;
        e.gpu_clock_mhz = warm ? 1800.0 : 2100.0;
        e.gpu_load_pct = warm ? warm_load : cold_load;
        e.vendor_thermal_flag = warm;
        v.push_back(e);
    }
    return v;
}

EF_TEST(x21_t01_fps_cap_or_low_load_is_not_thermal) {
    const auto capped = analyze_endurance(x21_run(55, 55, 20), EnduranceStop::Completed);  // kare siniri: GPU tam yukte degil
    EF_CHECK(capped.frequency_dropped && capped.load_not_comparable && capped.cause.empty());
    const auto changed = analyze_endurance(x21_run(98, 70, 20), EnduranceStop::Completed);  // sahne yuku degisti
    EF_CHECK(changed.load_not_comparable && changed.cause.empty());
    const auto real = analyze_endurance(x21_run(98, 97, 20), EnduranceStop::Completed);
    EF_CHECK(!real.load_not_comparable && real.cause == "vendor_thermal_flag");
}

EF_TEST(x21_t02_unknown_cap_reason_stays_unknown) {
    auto v = x21_run(98, 97, 20);
    for (auto& e : v) e.vendor_thermal_flag = std::nullopt;
    const auto r = analyze_endurance(v, EnduranceStop::Completed);
    EF_CHECK(r.frequency_dropped && r.cause.empty());
    // Saat dustu ama is ciktisi dusmedi: sicaklik bayragi tek basina neden degil.
    EF_CHECK(analyze_endurance(x21_run(98, 97, 0), EnduranceStop::Completed).cause.empty());
}

EF_TEST(x21_power_limit_is_separate_cause) {
    auto v = x21_run(98, 97, 20);
    for (auto& e : v) {
        e.vendor_thermal_flag = false;
        e.vendor_power_flag = e.t_s > 150;
    }
    EF_CHECK(analyze_endurance(v, EnduranceStop::Completed).cause == "vendor_power_flag");
}

EF_TEST(x21_flag_only_in_cold_window_is_not_cause) {
    auto v = x21_run(98, 97, 20);
    for (auto& e : v) e.vendor_thermal_flag = e.t_s < 10;
    EF_CHECK(analyze_endurance(v, EnduranceStop::Completed).cause.empty());
}

EF_TEST(x21_unknown_load_never_states_a_cause) {
    auto v = x21_run(98, 97, 20);
    for (auto& e : v) e.gpu_load_pct = std::nullopt;
    const auto r = analyze_endurance(v, EnduranceStop::Completed);
    EF_CHECK(r.frequency_dropped && r.load_unknown && r.cause.empty() && r.limit_flag_seen);
}

EF_TEST(x21_t03_sensor_loss_after_seen_stops_never_before) {
    SensorWatch never;
    for (int i = 0; i < 50; ++i) EF_CHECK(!never.lost(false));  // hic okunmadiysa kayip degil: sensorsuz sinir raporlanir
    SensorWatch w;
    EF_CHECK(!w.lost(true));
    for (int i = 0; i < SensorWatch::kMaxMissing - 1; ++i) EF_CHECK(!w.lost(false));
    EF_CHECK(!w.lost(true));  // tek tuk kayip sifirlanir
    bool stopped = false;
    for (int i = 0; i < SensorWatch::kMaxMissing; ++i) stopped = w.lost(false);
    EF_CHECK(stopped);
}

// Uyumluluk B14: son iki 60 sn penceresi %2 icinde -> sabit; saat okunamazsa yalniz FPS.
static std::vector<EnduranceSample> settle_run(double secs, double last_fps_drift, double last_clock_drift, bool clock) {
    std::vector<EnduranceSample> v;
    for (double t = 1; t <= secs; t += 1) {
        EnduranceSample s;
        s.t_s = t;
        const bool last = t >= secs - 60;
        s.fps = 150.0 * (last ? 1.0 + last_fps_drift : 1.0);
        if (clock) s.gpu_clock_mhz = 2600.0 * (last ? 1.0 + last_clock_drift : 1.0), s.gpu_load_pct = 98.0;
        v.push_back(s);
    }
    return v;
}

EF_TEST(b14_stability_last_two_windows) {
    auto r = analyze_endurance(settle_run(600, -0.01, -0.015, true), EnduranceStop::Completed);
    EF_CHECK(r.stability_checked && r.stable && !r.stability_fps_only);
    EF_CHECK(std::abs(r.duration_s - 600.0) < 1e-9);
    r = analyze_endurance(settle_run(600, -0.05, 0.0, true), EnduranceStop::Completed);  // FPS %5 dustu
    EF_CHECK(r.stability_checked && !r.stable);
    r = analyze_endurance(settle_run(600, 0.0, -0.04, true), EnduranceStop::Completed);  // FPS sabit, saat %4 dustu
    EF_CHECK(r.stability_checked && !r.stable);
    r = analyze_endurance(settle_run(600, 0.019, 0.0, false), EnduranceStop::Completed);  // saat yok: yalniz FPS
    EF_CHECK(r.stability_checked && r.stable && r.stability_fps_only);
    r = analyze_endurance(settle_run(100, 0.0, 0.0, true), EnduranceStop::Cancelled);  // 2 pencere dolmadi
    EF_CHECK(!r.stability_checked && !r.stable && !r.cold_fps);
    EF_CHECK(std::abs(r.duration_s - 100.0) < 1e-9);
    r = analyze_endurance({}, EnduranceStop::Cancelled);
    EF_CHECK(r.duration_s == 0 && !r.stability_checked);
    // Sinir: fark tam %2 -> sabit (metin "en fazla %2"); %2'nin hemen ustu -> sabit degil.
    const auto edge = [](double last_fps) {
        std::vector<EnduranceSample> v;
        for (double t = 1; t <= 600; t += 1) {
            EnduranceSample s;
            s.t_s = t;
            s.fps = t >= 540 ? last_fps : 100.0;
            v.push_back(s);
        }
        return analyze_endurance(v, EnduranceStop::Completed);
    };
    r = edge(98.0);
    EF_CHECK(r.stability_checked && r.stable && r.stability_fps_only);
    r = edge(97.9);
    EF_CHECK(r.stability_checked && !r.stable);
}

EF_TEST(b14_partial_after_two_minutes_keeps_comparison) {
    // 240 sn sonra durdurulan kosu: soguk/isinmis pencere dolu, sonuc korunur (kismi etiket UI'da).
    const auto r = analyze_endurance(run(240, true, false), EnduranceStop::Cancelled);
    EF_CHECK(r.stop == EnduranceStop::Cancelled && r.cold_fps && r.warm_fps && r.fps_change_pct);
    const auto short_run = analyze_endurance(run(110, true, false), EnduranceStop::Cancelled);
    EF_CHECK(!short_run.cold_fps && !short_run.warm_fps);
}

EF_TEST(b14_telemetry_only_inside_render_window) {
    std::vector<TelemetryReading> rd;
    auto add = [&](double t, double clk, double load, bool thermal) {
        TelemetryReading x;
        x.t_s = t;
        x.gpu_clock_mhz = clk;
        x.gpu_load_pct = load;
        x.thermal = thermal;
        rd.push_back(x);
    };
    add(0.95, 300, 5, true);     // dilim oncesi bosluk: sayilmaz
    add(1.10, 2600, 98, false);
    add(1.50, 2700, 96, false);
    add(2.05, 500, 3, true);     // dilim sonu bosaltma: sayilmaz
    EnduranceSample s;
    telemetry_in_window(rd, 1.0, 2.0, s);
    EF_CHECK(s.gpu_clock_mhz && std::abs(*s.gpu_clock_mhz - 2650.0) < 1e-9);
    EF_CHECK(s.gpu_load_pct && std::abs(*s.gpu_load_pct - 97.0) < 1e-9);
    EF_CHECK(s.vendor_thermal_flag && !*s.vendor_thermal_flag);
    EF_CHECK(!s.vendor_power_flag);  // hic okunmadi: uydurulmaz
    EnduranceSample none;
    none.gpu_clock_mhz = 1.0;  // onceki deger tasinmaz
    telemetry_in_window(rd, 3.0, 4.0, none);
    EF_CHECK(!none.gpu_clock_mhz && !none.gpu_load_pct && !none.vendor_thermal_flag);
}
