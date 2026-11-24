#include "benchmark/endurance/endurance.h"

#include <cmath>

namespace ef::benchmark {

std::string_view to_string(EnduranceStop s) noexcept {
    switch (s) {
        case EnduranceStop::Completed: return "completed";
        case EnduranceStop::Cancelled: return "cancelled";
        case EnduranceStop::TimeBudget: return "time_budget";
        case EnduranceStop::ByteBudget: return "byte_budget";
        case EnduranceStop::MemoryBudget: return "memory_budget";
        case EnduranceStop::DeviceLost: return "device_lost";
        case EnduranceStop::HardwareError: return "hardware_error";
        case EnduranceStop::SensorLost: return "sensor_lost";
    }
    return "completed";
}

bool planned_duration_reached(const EnduranceBudget& b, const EnduranceSample& s) noexcept {
    return b.planned_seconds > 0 && b.planned_seconds < b.max_seconds && s.t_s >= b.planned_seconds;
}

std::optional<EnduranceStop> budget_exceeded(const EnduranceBudget& b, const EnduranceSample& s) noexcept {
    if (s.t_s >= b.max_seconds) return EnduranceStop::TimeBudget;
    if (s.bytes_written > b.max_bytes_written) return EnduranceStop::ByteBudget;
    if (s.memory_bytes > b.max_memory_bytes) return EnduranceStop::MemoryBudget;
    return std::nullopt;
}

namespace {
template <class F>
std::optional<double> mean_in(const std::vector<EnduranceSample>& v, double a, double b, F get) {
    double sum = 0;
    int n = 0;
    for (const auto& s : v)
        if (s.t_s >= a && s.t_s < b)
            if (auto x = get(s)) sum += *x, ++n;
    if (n < 3) return std::nullopt;  // pencerede yeterli ornek yok
    return sum / n;
}
}  // namespace

void telemetry_in_window(const std::vector<TelemetryReading>& r, double a, double b, EnduranceSample& out) {
    double clock = 0, load = 0;
    int nc = 0, nl = 0;
    std::optional<bool> thermal, power;
    for (const auto& x : r) {
        if (x.t_s < a || x.t_s > b) continue;
        if (x.gpu_clock_mhz && std::isfinite(*x.gpu_clock_mhz)) clock += *x.gpu_clock_mhz, ++nc;
        if (x.gpu_load_pct && std::isfinite(*x.gpu_load_pct)) load += *x.gpu_load_pct, ++nl;
        if (x.thermal) thermal = thermal.value_or(false) || *x.thermal;
        if (x.power) power = power.value_or(false) || *x.power;
    }
    out.gpu_clock_mhz = nc ? std::optional<double>(clock / nc) : std::nullopt;
    out.gpu_load_pct = nl ? std::optional<double>(load / nl) : std::nullopt;
    out.vendor_thermal_flag = thermal;
    out.vendor_power_flag = power;
}

EnduranceReport analyze_endurance(const std::vector<EnduranceSample>& s, EnduranceStop stop, double w) {
    EnduranceReport r;
    r.stop = stop;
    if (s.empty()) return r;
    const double t0 = s.front().t_s, t1 = s.back().t_s;
    r.duration_s = t1 > 0 ? t1 : 0.0;
    if (t1 - t0 < 2 * w) return r;  // soguk ve isinmis pencere cakisir: karsilastirma yok
    auto fps = [](const EnduranceSample& x) { return x.fps; };
    auto gpu = [](const EnduranceSample& x) { return x.gpu_clock_mhz; };
    r.cold_fps = mean_in(s, t0, t0 + w, fps);
    r.warm_fps = mean_in(s, t1 - w, t1 + 1e-9, fps);
    if (r.cold_fps && r.warm_fps && *r.cold_fps > 0) r.fps_change_pct = (*r.warm_fps - *r.cold_fps) / *r.cold_fps * 100.0;
    r.cold_gpu_mhz = mean_in(s, t0, t0 + w, gpu);
    r.warm_gpu_mhz = mean_in(s, t1 - w, t1 + 1e-9, gpu);
    r.sensors_available = r.cold_gpu_mhz.has_value() && r.warm_gpu_mhz.has_value();
    if (r.sensors_available) r.frequency_dropped = *r.warm_gpu_mhz < *r.cold_gpu_mhz * 0.95;
    auto load = [](const EnduranceSample& x) { return x.gpu_load_pct; };
    const auto cold_load = mean_in(s, t0, t0 + w, load), warm_load = mean_in(s, t1 - w, t1 + 1e-9, load);
    constexpr double kFullLoadPct = 85.0, kLoadDeltaPct = 10.0;
    r.load_unknown = !cold_load || !warm_load;
    if (cold_load && warm_load) r.load_not_comparable = *cold_load < kFullLoadPct || std::abs(*warm_load - *cold_load) > kLoadDeltaPct;
    bool thermal = false, power = false;
    for (const auto& x : s) {
        if (x.t_s < t1 - w) continue;  // yalniz isinmis pencere: acilistaki anlik bayrak neden sayilmaz
        thermal = thermal || x.vendor_thermal_flag.value_or(false);
        power = power || x.vendor_power_flag.value_or(false);
    }
    const bool output_fell = r.fps_change_pct && *r.fps_change_pct < -1.0;  // yalniz sicaklik/saat degil, is ciktisi da dustu
    r.limit_flag_seen = thermal || power;
    if (r.frequency_dropped && !r.load_not_comparable && !r.load_unknown && output_fell) r.cause = thermal ? "vendor_thermal_flag" : power ? "vendor_power_flag" : "";
    // B14 sabitlik: sondan onceki pencere [t1-2w, t1-w) ile son pencere [t1-w, t1].
    const auto within = [](double a, double b) { return a > 0 && std::abs(b - a) / a <= kStableTolerance; };
    const auto prev_fps = mean_in(s, t1 - 2 * w, t1 - w, fps), last_fps = mean_in(s, t1 - w, t1 + 1e-9, fps);
    if (prev_fps && last_fps && *prev_fps > 0) {
        r.stability_checked = true;
        const auto prev_gpu = mean_in(s, t1 - 2 * w, t1 - w, gpu), last_gpu = mean_in(s, t1 - w, t1 + 1e-9, gpu);
        r.stability_fps_only = !prev_gpu || !last_gpu;
        r.stable = within(*prev_fps, *last_fps) && (r.stability_fps_only || within(*prev_gpu, *last_gpu));
    }
    return r;
}

}  // namespace ef::benchmark
