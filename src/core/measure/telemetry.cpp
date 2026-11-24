#include "core/measure/telemetry.h"

#include <cmath>

namespace ef::core::measure {

bool can_attribute(double interval_ms, double event_duration_ms) noexcept {
    if (!(interval_ms > 0) || !(event_duration_ms > 0)) return false;
    return interval_ms <= event_duration_ms / 2.0;
}

WindowContext context_for_window(const std::vector<TelemetrySample>& samples, std::string_view metric, double start_ms, double end_ms) {
    WindowContext w;
    w.metric = std::string(metric);
    const double mid = (start_ms + end_ms) / 2.0;
    const TelemetrySample* best = nullptr;
    double best_d = 0;
    for (const auto& s : samples) {
        if (s.metric != metric || !s.value) continue;
        // Ornek [t - interval, t] araligini temsil eder; pencereyle kesismeyen ornek kullanilmaz.
        if (s.t_ms < start_ms || s.t_ms - s.interval_ms > end_ms) continue;
        const double d = std::fabs(s.t_ms - mid);
        if (!best || d < best_d) best = &s, best_d = d;
    }
    if (!best) return w;  // NoData: tahmin yok
    w.value = best->value;
    w.attribution = can_attribute(best->interval_ms, end_ms - start_ms) ? Attribution::RootCandidate : Attribution::ContextOnly;
    return w;
}

bool vram_valid_for_game(const VramReading& r, std::uint32_t game_pid) noexcept {
    if (!r.bytes) return false;
    switch (r.scope) {
        case VramScope::OwnProcessDxgiBudget: return false;  // bu uygulamanin butcesi, oyunun degil
        case VramScope::ProcessUsage: return game_pid != 0 && r.pid == game_pid;
        case VramScope::AdapterTotal: return false;  // yalniz baglam; oyuna atfedilmez
    }
    return false;
}

}  // namespace ef::core::measure
