#include "core/net/speed_test.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ef::core::net {

std::optional<double> chunk_mbps(const ChunkSample& s) noexcept {
    if (s.bytes == 0 || !std::isfinite(s.seconds) || s.seconds <= 0.0) return std::nullopt;
    const double v = static_cast<double>(s.bytes) * 8.0 / s.seconds / 1e6;
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<double> percentile90_mbps(std::span<const ChunkSample> chunks) {
    std::vector<double> v;
    v.reserve(chunks.size());
    for (const auto& c : chunks) {
        if (const auto m = chunk_mbps(c)) v.push_back(*m);
    }
    if (v.empty()) return std::nullopt;
    std::sort(v.begin(), v.end());
    // En-yakin-sira: rank = ceil(0.9 * n), 1 tabanli.
    auto rank = static_cast<std::size_t>(std::ceil(0.9 * static_cast<double>(v.size())));
    rank = std::clamp<std::size_t>(rank, 1, v.size());
    return v[rank - 1];
}

std::optional<double> median_ms(std::span<const double> values) {
    std::vector<double> v;
    v.reserve(values.size());
    for (const double x : values) {
        if (std::isfinite(x)) v.push_back(x);
    }
    if (v.empty()) return std::nullopt;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

std::uint64_t SpeedPlan::next_chunk(SpeedPhase phase, std::size_t chunks_done, double phase_elapsed_s,
                                    std::uint64_t total_bytes, double total_elapsed_s) const noexcept {
    if (phase != SpeedPhase::Download && phase != SpeedPhase::Upload) return 0;
    if (!std::isfinite(phase_elapsed_s) || phase_elapsed_s >= kPhaseSeconds) return 0;
    if (!std::isfinite(total_elapsed_s) || total_elapsed_s >= kSpeedMaxSeconds) return 0;
    const std::size_t idx = std::min(chunks_done, kChunkBytes.size() - 1);
    const std::uint64_t chunk = kChunkBytes[idx];
    if (total_bytes > kSpeedMaxBytes || chunk > kSpeedMaxBytes - total_bytes) return 0;
    return chunk;
}

double gauge_fraction(double mbps) noexcept {
    static constexpr std::array<double, 9> kScale{0, 5, 10, 50, 100, 250, 500, 750, 1000};
    if (!std::isfinite(mbps) || mbps <= kScale.front()) return 0.0;
    if (mbps >= kScale.back()) return 1.0;
    const double seg = 1.0 / static_cast<double>(kScale.size() - 1);
    for (std::size_t i = 1; i < kScale.size(); ++i) {
        if (mbps <= kScale[i]) {
            const double t = (mbps - kScale[i - 1]) / (kScale[i] - kScale[i - 1]);
            return (static_cast<double>(i - 1) + t) * seg;
        }
    }
    return 1.0;
}

double mbps_to_mbs(double mbps) noexcept { return mbps / 8.0; }

std::optional<double> loaded_median_ms(std::span<const double> samples) {
    std::vector<double> v;
    v.reserve(samples.size());
    for (const double x : samples)
        if (std::isfinite(x) && x >= 0.0) v.push_back(x);
    if (v.size() < kLoadedMinSamples) return std::nullopt;
    return median_ms(v);
}

BloatGrade bloat_grade(std::optional<double> idle_ms, std::optional<double> down_loaded_ms, std::optional<double> up_loaded_ms) noexcept {
    if (!idle_ms || !std::isfinite(*idle_ms)) return BloatGrade::Insufficient;
    double worst = -1.0;
    for (const auto& l : {down_loaded_ms, up_loaded_ms})
        if (l && std::isfinite(*l)) worst = std::max(worst, std::max(0.0, *l - *idle_ms));
    if (worst < 0.0) return BloatGrade::Insufficient;
    if (worst < kBloatLowMs) return BloatGrade::Low;
    if (worst < kBloatMediumMs) return BloatGrade::Medium;
    return BloatGrade::High;
}

std::string_view bloat_key(BloatGrade g) noexcept {
    switch (g) {
        case BloatGrade::Low: return "speed.bloat.low";
        case BloatGrade::Medium: return "speed.bloat.medium";
        case BloatGrade::High: return "speed.bloat.high";
        case BloatGrade::Insufficient: break;
    }
    return "speed.bloat.none";
}

SpeedResult sanitize_result(SpeedResult r) noexcept {
    if (r.phase != SpeedPhase::Done) {
        r.down_mbps.reset();
        r.up_mbps.reset();
        r.latency_ms.reset();
        r.down_loaded_ms.reset();
        r.up_loaded_ms.reset();
    }
    return r;
}

std::string_view speed_fail_key(SpeedFail f) noexcept {
    switch (f) {
        case SpeedFail::Offline: return "speed.fail.offline";
        case SpeedFail::Timeout: return "speed.fail.timeout";
        case SpeedFail::Tls: return "speed.fail.tls";
        case SpeedFail::Http: return "speed.fail.http";
        case SpeedFail::Budget: return "speed.fail.budget";
        case SpeedFail::Gated: return "speed.fail.gated";
        case SpeedFail::None: break;
    }
    return {};
}

}  // namespace ef::core::net
