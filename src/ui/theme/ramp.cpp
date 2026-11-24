#include "ui/theme/ramp.h"

#include <cmath>
#include <optional>

namespace ef::ui {

namespace {
double sanitize(double pct) {
    if (std::isnan(pct)) return 0;
    if (pct < 0) return 0;
    if (pct > 100) return 100;
    return pct;
}
}  // namespace

Color usage_color(const Palette& p, double pct0) {
    const double pct = sanitize(pct0);
    struct Stop {
        double at;
        const Color* c;
    };
    const Stop stops[] = {{0, &p.accent}, {40, &p.accent}, {60, &p.warning}, {75, &p.orange}, {92, &p.error}};
    constexpr std::size_t n = sizeof(stops) / sizeof(stops[0]);
    if (pct >= stops[n - 1].at) return *stops[n - 1].c;
    for (std::size_t i = 1; i < n; ++i) {
        if (pct <= stops[i].at) {
            const double span = stops[i].at - stops[i - 1].at;
            const float t = span > 0 ? static_cast<float>((pct - stops[i - 1].at) / span) : 1.f;
            return mix(*stops[i - 1].c, *stops[i].c, t);
        }
    }
    return *stops[n - 1].c;
}

std::string_view usage_word_key(double pct0) {
    const double pct = std::isnan(pct0) ? 0 : pct0;
    if (pct < 55) return "live.usage.low";
    if (pct < 70) return "live.usage.mid";
    if (pct < 88) return "live.usage.high";
    return "live.usage.max";
}

bool usage_alarm(double pct) { return !std::isnan(pct) && pct >= 88; }

bool usage_glow(double pct) { return !std::isnan(pct) && pct >= 75; }

std::string_view usage_short_word_key(double pct0) {
    const double pct = std::isnan(pct0) ? 0 : pct0;
    if (pct < 55) return "live.usage_short.low";
    if (pct < 70) return "live.usage_short.mid";
    if (pct < 88) return "live.usage_short.high";
    return "live.usage_short.max";
}

double led_segment_pct(int i) {
    if (i < 0 || i >= kLedSegments) return 0;
    return static_cast<double>(i + 1) / static_cast<double>(kLedSegments) * 100.0;
}

bool led_segment_lit(int i, double pct) {
    if (i < 0 || i >= kLedSegments || std::isnan(pct)) return false;
    return led_segment_pct(i) <= sanitize(pct) + 4.0;
}

int led_lit_count(double pct) {
    int n = 0;
    for (int i = 0; i < kLedSegments; ++i)
        if (led_segment_lit(i, pct)) ++n;
    return n;
}

std::optional<double> led_peak_update(LedPeak& pk, std::optional<double> pct, std::int64_t now_ms, bool reduced_motion) {
    if (!pct || std::isnan(*pct)) {
        pk = {};
        return std::nullopt;
    }
    const double v = sanitize(*pct);
    if (pk.pct < 0 || v >= pk.pct) {
        pk = {v, now_ms};
        return v;
    }
    const std::int64_t over = now_ms - pk.at_ms - kLedPeakHoldMs;
    if (over <= 0) return pk.pct;  // bekleme suresi (saat geri gittiyse de bekler)
    const double shown = reduced_motion ? v : pk.pct - kLedPeakDecayPerSec * static_cast<double>(over) / 1000.0;
    if (shown <= v) {
        pk = {v, now_ms};
        return v;
    }
    return shown;
}

int led_peak_segment(double peak, double pct) {
    const int top = led_lit_count(peak) - 1;
    return top >= led_lit_count(pct) ? top : -1;
}

}  // namespace ef::ui
