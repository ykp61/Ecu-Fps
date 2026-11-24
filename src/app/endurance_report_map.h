#pragma once
// R13/INS-03: benchmark::EnduranceReport -> urun gorunumu eslemesi (baslik-only; app ve birim test kullanir).
//  - ANALIZ YENIDEN HESAPLANMAZ: yalniz analyze_endurance ciktisi tasinir.
//  - Olculmeyen deger nullopt kalir; "neden" yalniz gercek destekli termal bayrak gozlendiginde yazilir.

#include "benchmark/endurance/endurance.h"
#include "core/measure/endurance_view.h"

namespace ef::app {

inline core::measure::EnduranceView endurance_view_from_report(const benchmark::EnduranceReport& r) {
    core::measure::EnduranceView v;
    switch (r.stop) {
        case benchmark::EnduranceStop::Completed: v.stop = core::measure::EnduranceStopKind::Completed; break;
        case benchmark::EnduranceStop::Cancelled: v.stop = core::measure::EnduranceStopKind::Cancelled; break;
        case benchmark::EnduranceStop::TimeBudget: v.stop = core::measure::EnduranceStopKind::TimeBudget; break;
        case benchmark::EnduranceStop::ByteBudget: v.stop = core::measure::EnduranceStopKind::ByteBudget; break;
        case benchmark::EnduranceStop::MemoryBudget: v.stop = core::measure::EnduranceStopKind::MemoryBudget; break;
        case benchmark::EnduranceStop::DeviceLost: v.stop = core::measure::EnduranceStopKind::DeviceLost; break;
        case benchmark::EnduranceStop::HardwareError: v.stop = core::measure::EnduranceStopKind::HardwareError; break;
        case benchmark::EnduranceStop::SensorLost: v.stop = core::measure::EnduranceStopKind::SensorLost; break;
    }
    v.cold_fps = r.cold_fps;
    v.warm_fps = r.warm_fps;
    v.fps_change_pct = r.fps_change_pct;
    v.cold_gpu_mhz = r.cold_gpu_mhz;
    v.warm_gpu_mhz = r.warm_gpu_mhz;
    v.sensors_available = r.sensors_available;
    // Frekans dususu/neden yalniz sensor gercekten okunduysa tasinir (sensor yokken uydurulmaz).
    v.frequency_dropped = r.sensors_available && r.frequency_dropped;
    v.cause_vendor_thermal = v.frequency_dropped && r.cause == "vendor_thermal_flag";  // yalniz gercek destekli bayrak
    v.cause_vendor_power = v.frequency_dropped && r.cause == "vendor_power_flag";
    v.load_not_comparable = r.load_not_comparable;
    v.load_unknown = r.load_unknown;
    v.limit_flag_seen = r.limit_flag_seen;
    v.stability_checked = r.stability_checked;
    v.stable = r.stability_checked && r.stable;
    v.stability_fps_only = r.stability_checked && r.stability_fps_only;
    v.duration_s = r.duration_s;
    v.telemetry_unsupported = r.telemetry_unsupported;
    return v;
}

}  // namespace ef::app
