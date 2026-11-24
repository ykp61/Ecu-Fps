#include "core/measure/run_manifest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace ef::core::measure {

std::string_view to_string(Exclusion e) noexcept {
    switch (e) {
        case Exclusion::None: return "none";
        case Exclusion::EtwEventsLost: return "etw_events_lost";
        case Exclusion::TooFewFrames: return "too_few_frames";
        case Exclusion::TargetMismatch: return "target_mismatch";
        case Exclusion::Overlay: return "overlay";
        case Exclusion::RuntimeMixed: return "runtime_mixed";
        case Exclusion::RuntimeOther: return "runtime_other";
        case Exclusion::RuntimeUnknown: return "runtime_unknown";
        case Exclusion::Cancelled: return "cancelled";
        case Exclusion::TooShort: return "too_short";
        case Exclusion::GameExited: return "game_exited";
    }
    return "none";
}

std::string fingerprint(const RunManifest& m) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::string_view s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= 0xFF;  // alan ayirici
        h *= 1099511628211ull;
    };
    mix(m.source);
    mix(m.app);
    for (const auto& [k, v] : m.context) {  // std::map: sirali
        mix(k);
        mix(v);
    }
    mix(m.stats_definition);
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

RunManifest make_manifest(std::string source, std::string app, std::map<std::string, std::string> context, const FrameSummary& s,
                          const FrameStats& st, std::uint32_t events_lost, double min_seconds, bool cancelled) {
    RunManifest m;
    m.source = std::move(source);
    m.app = std::move(app);
    m.context = std::move(context);
    m.valid_seconds = st.valid_seconds;
    m.intervals = st.n;
    m.excluded_intervals = st.excluded;
    m.events_lost = events_lost;
    auto add = [&](Exclusion e) {
        if (std::find(m.exclusions.begin(), m.exclusions.end(), e) == m.exclusions.end()) m.exclusions.push_back(e);
    };
    for (const auto& r : s.reasons) {
        if (r == "etw_events_lost") add(Exclusion::EtwEventsLost);
        else if (r == "too_few_frames" || r == "no_frames") add(Exclusion::TooFewFrames);
        // BM-01: dusuk guven sinyali manifestoya tasinir; boylece kosu "gecerli" gorunmez.
        else if (r == "runtime_mixed") add(Exclusion::RuntimeMixed);
        else if (r == "runtime_other") add(Exclusion::RuntimeOther);
    }
    if (events_lost > 0) add(Exclusion::EtwEventsLost);
    if (!st.sufficient) add(Exclusion::TooFewFrames);
    if (cancelled) add(Exclusion::Cancelled);
    if (st.sufficient && st.valid_seconds < min_seconds) add(Exclusion::TooShort);
    // Gecerlilik yalniz tam "Valid" ozet icin: LowConfidence kosu da gecerli sayilmaz.
    m.valid = m.exclusions.empty() && s.validity == Validity::Valid;
    return m;
}

std::string_view to_string(CaptureOutcome o) noexcept {
    switch (o) {
        case CaptureOutcome::NotStarted: return "not_started";
        case CaptureOutcome::Complete: return "complete";
        case CaptureOutcome::Cancelled: return "cancelled";
        case CaptureOutcome::GameExited: return "game_exited";
        case CaptureOutcome::TargetChanged: return "target_changed";
    }
    return "not_started";
}

CaptureOutcome classify_capture_outcome(bool cancelled, bool target_exited, bool identity_ok) noexcept {
    if (target_exited) return CaptureOutcome::GameExited;  // iptal/kimlik durumundan bagimsiz: tamamlanmadi
    if (!identity_ok) return CaptureOutcome::TargetChanged;
    if (cancelled) return CaptureOutcome::Cancelled;
    return CaptureOutcome::Complete;
}

void apply_capture_outcome(RunManifest& m, CaptureOutcome o) {
    auto add = [&m](Exclusion e) {
        if (std::find(m.exclusions.begin(), m.exclusions.end(), e) == m.exclusions.end()) m.exclusions.push_back(e);
    };
    switch (o) {
        case CaptureOutcome::Complete: return;
        case CaptureOutcome::GameExited: add(Exclusion::GameExited); break;
        case CaptureOutcome::TargetChanged: add(Exclusion::TargetMismatch); break;
        case CaptureOutcome::Cancelled: add(Exclusion::Cancelled); break;
        case CaptureOutcome::NotStarted: break;
    }
    m.valid = false;
}

namespace {
std::string context_value(const std::string& v) {
    if (v.empty() || v.size() > 160) return std::string(kUnknownContext);
    for (const char c : v)
        if (c < 0x20 || c > 0x7E) return std::string(kUnknownContext);
    return v;
}
}  // namespace

std::map<std::string, std::string> make_run_context(const RunContextInput& in) {
    return {{"scene", context_value(in.scene)},
            {"game_build", context_value(in.game_build)},
            {"resolution", context_value(in.resolution)},
            {"render_scale", context_value(in.render_scale)},
            {"fps_limit", context_value(in.fps_limit)},
            {"sync", context_value(in.sync)},
            {"upscaler", context_value(in.upscaler)},
            {"frame_gen", context_value(in.frame_gen)},
            {"renderer", context_value(in.renderer)},
            {"api_confidence", context_value(in.api_confidence)},
            {"os_build", context_value(in.os_build)},
            {"gpu_driver", context_value(in.gpu_driver)},
            {"collector_commit", context_value(in.collector_commit)}};
}

bool context_comparable(const RunManifest& m) {
    static constexpr std::array<std::string_view, 8> kRequired{"scene",     "game_build", "resolution", "render_scale",
                                                               "fps_limit", "sync",       "upscaler",   "frame_gen"};
    for (const auto k : kRequired) {
        const auto it = m.context.find(std::string(k));
        if (it == m.context.end() || it->second.empty() || it->second == kUnknownContext) return false;
    }
    return true;
}

TargetExitObservation resolve_target_exit(std::optional<double> in_loop_exit_s, bool exited_after_stop, double loop_end_s, double seconds,
                                          double now_s) noexcept {
    TargetExitObservation o;
    if (in_loop_exit_s) {
        if (*in_loop_exit_s < seconds)
            o.early_exit_s = in_loop_exit_s;
        else
            o.exited_after_window = true;  // son dongu diliminde, pencere dolduktan sonra
        return o;
    }
    if (!exited_after_stop) return o;
    if (loop_end_s < seconds)
        o.early_exit_s = now_s;  // iptal/erken bitis + cikis: tamamlanmadi
    else
        o.exited_after_window = true;
    return o;
}

namespace {
std::uint32_t sat32(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, 0xFFFFFFFFu)); }
}  // namespace

std::uint32_t stream_events_lost(const CaptureLossCounters& c) noexcept {
    return sat32(std::uint64_t{c.events_lost} + c.overflowed_presents + c.dropped_presents);
}

std::uint32_t manifest_loss_total(const CaptureLossCounters& c) noexcept {
    return sat32(std::uint64_t{stream_events_lost(c)} + c.buffers_lost);
}

std::string capture_counters_line(std::string_view build_id, const CaptureLossCounters& c) {
    bool clean = !build_id.empty() && build_id.size() < 64;
    for (const char ch : build_id)
        if (ch <= 0x20 || ch > 0x7E) clean = false;
    const std::string id = clean ? std::string(build_id) : std::string("unknown");
    char buf[256];
    std::snprintf(buf, sizeof buf, "build_id=%s events_lost=%u buffers_lost=%u overflowed=%u dropped=%llu manifest_loss_total=%u", id.c_str(),
                  c.events_lost, c.buffers_lost, c.overflowed_presents, static_cast<unsigned long long>(c.dropped_presents),
                  manifest_loss_total(c));
    return buf;
}

OverheadComparison compare_overhead(const std::vector<OverheadPair>& pairs) {
    OverheadComparison out;
    auto fail = [&out](OverheadVerdict v, const char* why) {
        out.verdict = v;
        out.reason = why;
        return out;
    };
    auto positive = [](const std::optional<double>& f) { return f.has_value() && std::isfinite(*f) && *f > 0.0; };
    // 1) Cift duzeyi: gecersiz cift atilir, kume reddedilmez.
    std::vector<const OverheadPair*> valid;
    for (const auto& p : pairs) {
        const char* why = nullptr;
        if (!p.on.collector_on || p.off.collector_on)
            why = "arm_order";
        else if (!p.on.run_valid || !p.off.run_valid)
            why = "pair_invalid";
        else if (p.on.source != p.off.source)
            why = "source_mismatch";
        else if (p.on.condition_fingerprint.empty() || p.on.condition_fingerprint != p.off.condition_fingerprint)
            why = "condition_mismatch";
        else if (!positive(p.on.avg_fps) || !positive(p.off.avg_fps))
            why = "missing_fps";
        if (why)
            out.discard_reasons.emplace_back(why);
        else
            valid.push_back(&p);
    }
    out.pairs_discarded = out.discard_reasons.size();
    if (valid.size() < kMinOverheadPairs) return fail(OverheadVerdict::InsufficientPairs, "too_few_pairs");
    // 2) Kume duzeyi: gecerli ciftler ayni kosul ve ayni kaynak olmali; model kaynagi gercek hukum vermez.
    const auto& ref = valid.front()->on;
    for (const auto* p : valid) {
        if (p->on.condition_fingerprint != ref.condition_fingerprint) return fail(OverheadVerdict::NotComparable, "condition_mismatch");
        if (p->on.source != ref.source) return fail(OverheadVerdict::NotComparable, "source_mismatch");
    }
    if (ref.source != OverheadSource::GameReported) return fail(OverheadVerdict::NotComparable, "model_only_source");
    std::vector<double> d_fps, d_p99;
    bool p99_all = true;
    for (const auto* p : valid) {
        d_fps.push_back((*p->on.avg_fps - *p->off.avg_fps) / *p->off.avg_fps * 100.0);
        if (positive(p->on.p99_ms) && positive(p->off.p99_ms))
            d_p99.push_back(*p->on.p99_ms - *p->off.p99_ms);
        else
            p99_all = false;
    }
    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        const std::size_t n = v.size();
        return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
    };
    out.verdict = OverheadVerdict::Measured;
    out.pairs_used = valid.size();
    out.median_avg_fps_delta_pct = median(d_fps);
    out.min_avg_fps_delta_pct = *std::min_element(d_fps.begin(), d_fps.end());
    out.max_avg_fps_delta_pct = *std::max_element(d_fps.begin(), d_fps.end());
    if (p99_all) out.median_p99_delta_ms = median(d_p99);
    return out;
}

}  // namespace ef::core::measure
