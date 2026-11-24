#include "core/diagnosis/evidence.h"

#include <algorithm>

namespace ef::core::diagnosis {

bool aligned(const SourceInfo& s, ClockDomain frame_domain) noexcept {
    if (s.domain == ClockDomain::Unknown || frame_domain == ClockDomain::Unknown) return false;
    return s.domain == frame_domain || s.offset_verified;
}

std::vector<const EvidenceWindow*> Timeline::usable() const {
    std::vector<const EvidenceWindow*> out;
    for (const auto& w : windows)
        if (w.valid && w.unaligned_sources.empty()) out.push_back(&w);
    return out;
}

Timeline build_timeline(std::string run_id, ClockDomain frame_domain, const std::vector<FramePoint>& frames,
                        const std::vector<std::pair<SourceInfo, std::vector<measure::TelemetrySample>>>& sources,
                        const std::vector<std::string>& metric_names, const std::vector<ScopeRange>& scopes, double window_ms) {
    Timeline tl;
    tl.run_id = run_id;
    tl.frame_domain = frame_domain;
    if (frames.empty() || !(window_ms > 0)) return tl;
    double t0 = frames.front().t_ms, t1 = frames.back().t_ms;
    for (double s = t0; s < t1; s += window_ms) {
        EvidenceWindow w;
        w.run_id = run_id;
        w.start_ms = s;
        w.end_ms = std::min(s + window_ms, t1);
        for (const auto& sc : scopes)
            if (s >= sc.start_ms && s < sc.end_ms) w.scope = sc.scope;
        std::vector<double> iv;
        for (const auto& f : frames)
            if (f.t_ms > w.start_ms && f.t_ms <= w.end_ms) iv.push_back(f.interval_ms);
        // Pencere basina N kucuk olabilir: pencere istatistigi icin esik dusurulur (30), kosu istatistigi ayri.
        w.frames = measure::compute_frame_stats(iv, 30);
        for (const auto& [src, samples] : sources) {
            if (!aligned(src, frame_domain)) {
                w.unaligned_sources.push_back(src.id);
                continue;
            }
            std::vector<measure::TelemetrySample> shifted = samples;
            for (auto& x : shifted) x.t_ms += src.offset_verified ? src.offset_ms : 0.0;
            for (const auto& m : metric_names) {
                auto c = measure::context_for_window(shifted, m, w.start_ms, w.end_ms);
                if (c.attribution != measure::Attribution::NoData) w.metrics.push_back(std::move(c));
            }
        }
        w.valid = w.scope == WindowScope::Measured && w.frames.sufficient;
        tl.windows.push_back(std::move(w));
    }
    return tl;
}

}  // namespace ef::core::diagnosis
