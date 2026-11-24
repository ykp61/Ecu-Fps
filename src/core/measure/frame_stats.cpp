#include "core/measure/frame_stats.h"

#include <algorithm>
#include <cmath>

namespace ef::core::measure {

namespace {
double nearest_rank(const std::vector<double>& sorted, double pct) {
    const auto n = sorted.size();
    auto rank = static_cast<std::size_t>(std::ceil(pct / 100.0 * static_cast<double>(n)));
    rank = std::clamp<std::size_t>(rank, 1, n);
    return sorted[rank - 1];
}
}  // namespace

std::string_view to_string(FramePattern p) noexcept {
    switch (p) {
        case FramePattern::Insufficient: return "insufficient";
        case FramePattern::Stable: return "stable";
        case FramePattern::MicroStutter: return "micro_stutter";
        case FramePattern::Spiky: return "spiky";
    }
    return "insufficient";
}

FramePattern frame_pattern_from_string(std::string_view s) noexcept {
    if (s == "stable") return FramePattern::Stable;
    if (s == "micro_stutter") return FramePattern::MicroStutter;
    if (s == "spiky") return FramePattern::Spiky;
    return FramePattern::Insufficient;
}

FrameStats compute_frame_stats(const std::vector<double>& in, std::size_t min_n) {
    FrameStats s;
    std::vector<double> v;
    v.reserve(in.size());
    for (double d : in) {
        if (!std::isfinite(d) || d <= 0) {
            ++s.excluded;
            continue;
        }
        v.push_back(d);
    }
    s.n = v.size();
    s.sufficient = s.n >= min_n && s.n > 0;
    if (!s.sufficient) return s;  // tahmin yok
    double total = 0;
    for (double d : v) total += d;
    s.valid_seconds = total / 1000.0;
    s.avg_fps = static_cast<double>(s.n) / s.valid_seconds;
    std::sort(v.begin(), v.end());
    s.median_ms = nearest_rank(v, 50);
    s.p95_ms = nearest_rank(v, 95);
    s.p99_ms = nearest_rank(v, 99);
    s.max_ms = v.back();
    if (s.n >= kMinIntervalsLow01) {
        s.p99_9_ms = nearest_rank(v, 99.9);
        const auto k01 = static_cast<std::size_t>(std::ceil(static_cast<double>(s.n) * 0.001));
        double slow01 = 0;
        for (std::size_t i = 0; i < k01; ++i) slow01 += v[v.size() - 1 - i];
        s.low01_fps = 1000.0 / (slow01 / static_cast<double>(k01));
    }
    const auto k = static_cast<std::size_t>(std::ceil(static_cast<double>(s.n) * 0.01));
    double slow = 0;
    for (std::size_t i = 0; i < k; ++i) slow += v[v.size() - 1 - i];
    s.low1_fps = 1000.0 / (slow / static_cast<double>(k));
    // Desen: kare suresi dagiliminin bicimi, hedef FPS iddiasi degil.
    const double spike_threshold = 2.0 * s.median_ms;
    const auto spikes = static_cast<std::size_t>(v.end() - std::upper_bound(v.begin(), v.end(), spike_threshold));
    const bool rare_spikes = spikes > 0 && static_cast<double>(spikes) <= static_cast<double>(s.n) * 0.01;
    if (spikes == 0 && *s.p99_ms <= s.median_ms * 1.25)
        s.pattern = FramePattern::Stable;
    else if (rare_spikes && *s.p99_ms <= s.median_ms * 2.0)
        s.pattern = FramePattern::MicroStutter;
    else
        s.pattern = FramePattern::Spiky;
    return s;
}

std::vector<double> downsample_for_chart(const std::vector<double>& series, std::size_t max_points) {
    if (max_points == 0 || series.size() <= max_points) return series;
    std::vector<double> out;
    out.reserve(max_points);
    for (std::size_t k = 0; k < max_points; ++k) {
        const std::size_t a = k * series.size() / max_points, b = (k + 1) * series.size() / max_points;
        double sum = 0;
        for (std::size_t i = a; i < b; ++i) sum += series[i];
        out.push_back(sum / static_cast<double>(b - a));
    }
    return out;
}

std::vector<double> fps_per_second(const std::vector<double>& frame_ms, std::size_t max_points) {
    std::vector<double> out;
    double acc = 0;
    std::size_t frames = 0;
    for (const double ms : frame_ms) {
        if (!(ms > 0.0)) continue;
        acc += ms;
        ++frames;
        if (acc >= 1000.0) {
            out.push_back(static_cast<double>(frames) * 1000.0 / acc);
            acc = 0;
            frames = 0;
        }
    }
    if (out.size() > max_points && max_points > 0) {  // uzun kosu: bloklar ortalanir, deger uydurulmaz
        std::vector<double> reduced;
        const double step = static_cast<double>(out.size()) / static_cast<double>(max_points);
        for (std::size_t i = 0; i < max_points; ++i) {
            const auto a = static_cast<std::size_t>(static_cast<double>(i) * step);
            const auto b = std::max(a + 1, static_cast<std::size_t>(static_cast<double>(i + 1) * step));
            double sum = 0;
            for (std::size_t k = a; k < b && k < out.size(); ++k) sum += out[k];
            reduced.push_back(sum / static_cast<double>(b - a));
        }
        return reduced;
    }
    return out;
}

ChartedFrameStats frame_stats_with_series(const std::vector<double>& frame_ms, std::size_t max_points, std::size_t min_n) {
    ChartedFrameStats c;
    c.stats = compute_frame_stats(frame_ms, min_n);         // ham kare sureleri
    c.fps_series = fps_per_second(frame_ms, max_points);    // ayni ham kaynak; yalniz cizim
    return c;
}

}  // namespace ef::core::measure
