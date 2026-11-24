#include "core/measure/endurance_view.h"

#include <cmath>

namespace ef::core::measure {

std::string_view endurance_stop_key(EnduranceStopKind s) noexcept {
    switch (s) {
        case EnduranceStopKind::Completed: return "";
        case EnduranceStopKind::Cancelled: return "";  // B14: kismi / en az 2 dakika notu yerine gecer
        case EnduranceStopKind::TimeBudget: return "measure.endurance.stop.time_budget";
        case EnduranceStopKind::ByteBudget: return "measure.endurance.stop.byte_budget";
        case EnduranceStopKind::MemoryBudget: return "measure.endurance.stop.memory_budget";
        case EnduranceStopKind::DeviceLost: return "measure.endurance.stop.device_lost";
        case EnduranceStopKind::HardwareError: return "measure.endurance.stop.hardware_error";
        case EnduranceStopKind::SensorLost: return "measure.endurance.stop.sensor_lost";
    }
    return "";
}

namespace {
bool finite_value(const std::optional<double>& v) { return v.has_value() && std::isfinite(*v); }
}  // namespace

std::vector<EnduranceRow> endurance_rows(const EnduranceView& v) {
    std::vector<EnduranceRow> rows;
    // Soguk/isinmis pencereler analizde yeterli ornek yoksa nullopt doner: satir yine gorunur, deger yok.
    rows.push_back({"measure.endurance.cold", finite_value(v.cold_fps) ? v.cold_fps : std::nullopt, 1, "FPS"});
    rows.push_back({"measure.endurance.warm", finite_value(v.warm_fps) ? v.warm_fps : std::nullopt, 1, "FPS"});
    if (finite_value(v.fps_change_pct))
        rows.push_back({"measure.endurance.change", v.fps_change_pct, 1, "%"});
    // GPU saati satirlari yalniz sensor gercekten okuduysa eklenir; yoksa sensor notu yeterlidir.
    if (v.sensors_available) {
        rows.push_back({"measure.endurance.gpu_cold", finite_value(v.cold_gpu_mhz) ? v.cold_gpu_mhz : std::nullopt, 0, "MHz"});
        rows.push_back({"measure.endurance.gpu_warm", finite_value(v.warm_gpu_mhz) ? v.warm_gpu_mhz : std::nullopt, 0, "MHz"});
    }
    return rows;
}

EndurancePhase endurance_phase(bool started, double elapsed_s, double planned_s, double window_s) noexcept {
    if (!started || !std::isfinite(elapsed_s) || elapsed_s < 0) return EndurancePhase::Preparing;
    if (elapsed_s < window_s) return EndurancePhase::Cold;
    if (std::isfinite(planned_s) && planned_s > 2 * window_s && elapsed_s >= planned_s - window_s) return EndurancePhase::Warm;
    return EndurancePhase::Warming;
}

std::string_view endurance_phase_key(EndurancePhase p) noexcept {
    switch (p) {
        case EndurancePhase::Preparing: return "measure.endurance.phase.preparing";
        case EndurancePhase::Cold: return "measure.endurance.phase.cold";
        case EndurancePhase::Warming: return "measure.endurance.phase.warming";
        case EndurancePhase::Warm: return "measure.endurance.phase.warm";
    }
    return "measure.endurance.phase.preparing";
}

std::vector<std::string> endurance_note_keys(const EnduranceView& v) {
    std::vector<std::string> notes;
    if (v.stop == EnduranceStopKind::Cancelled) {
        // B14: erken durdurma. Pencereler dolduysa kismi sonuc ({e} sn), dolmadiysa 2 dakika siniri yazilir.
        notes.emplace_back(v.partial() ? "measure.endurance.partial" : "measure.endurance.too_short");
    } else {
        const auto stop = endurance_stop_key(v.stop);
        if (!stop.empty()) notes.emplace_back(stop);
        if (!v.has_comparison()) notes.emplace_back("measure.endurance.insufficient");
    }
    // Sabitlik etiketi yalniz planli 10 dakikayi tamamlayan kosuda (kismi/butce durdurmasinda hukum yok).
    if (v.stop == EnduranceStopKind::Completed && v.has_comparison() && v.stability_checked) {
        notes.emplace_back(v.stable ? "measure.endurance.stable" : "measure.endurance.not_stable");
        // Desteklenmeyen sensor ile okunamayan sensor ayri yazilir.
        if (v.stability_fps_only)
            notes.emplace_back(v.telemetry_unsupported ? "measure.endurance.stable_fps_only_unsupported" : "measure.endurance.stable_fps_only");
    }
    if (v.sensors_available) {
        if (v.frequency_dropped) {
            // Neden yalniz desteklenen uretici bayragi gercekten gozlendiyse yazilir.
            notes.emplace_back(v.load_not_comparable ? "measure.endurance.load_changed" : v.load_unknown ? "measure.endurance.load_unknown"
                               : v.cause_vendor_thermal ? "measure.endurance.cause" : v.cause_vendor_power ? "measure.endurance.cause_power"
                               : v.limit_flag_seen ? "measure.endurance.flag_seen_output_held" : "measure.endurance.cause_unknown");
        }
    } else {
        notes.emplace_back(v.telemetry_unsupported ? "measure.endurance.sensors_unsupported" : "measure.endurance.sensors_missing");
    }
    notes.emplace_back("measure.endurance.not_health");
    return notes;
}

std::map<std::string, double> endurance_metrics(const EnduranceView& v) {
    std::map<std::string, double> m;
    auto add = [&m](const char* key, const std::optional<double>& value) {
        if (finite_value(value)) m[key] = *value;  // yalniz olculen deger
    };
    add("cold_fps", v.cold_fps);
    add("warm_fps", v.warm_fps);
    add("fps_change_pct", v.fps_change_pct);
    if (v.sensors_available) {
        add("gpu_clock_cold_mhz", v.cold_gpu_mhz);
        add("gpu_clock_warm_mhz", v.warm_gpu_mhz);
    }
    return m;
}

std::vector<std::string> endurance_exclusions(const EnduranceView& v) {
    std::vector<std::string> x;
    switch (v.stop) {
        case EnduranceStopKind::Completed: break;
        case EnduranceStopKind::Cancelled: x.emplace_back("endurance.cancelled"); break;
        case EnduranceStopKind::TimeBudget: x.emplace_back("endurance.time_budget"); break;
        case EnduranceStopKind::ByteBudget: x.emplace_back("endurance.byte_budget"); break;
        case EnduranceStopKind::MemoryBudget: x.emplace_back("endurance.memory_budget"); break;
        case EnduranceStopKind::DeviceLost: x.emplace_back("endurance.device_lost"); break;
        case EnduranceStopKind::HardwareError: x.emplace_back("endurance.hardware_error"); break;
        case EnduranceStopKind::SensorLost: x.emplace_back("endurance.sensor_lost"); break;
    }
    if (!v.sensors_available) x.emplace_back("endurance.sensors_unavailable");
    if (!v.has_comparison()) x.emplace_back("endurance.insufficient_windows");
    if (v.frequency_dropped) x.emplace_back("endurance.frequency_dropped");
    return x;
}

}  // namespace ef::core::measure
