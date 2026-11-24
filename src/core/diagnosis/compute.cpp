#include "core/diagnosis/compute.h"

#include "core/diagnosis/game_limit.h"  // vrr_cap_candidates (tek tanim)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ef::core::diagnosis {

namespace {

void add_unique(std::vector<std::string>& v, std::string code) {
    if (std::find(v.begin(), v.end(), code) == v.end()) v.push_back(std::move(code));
}

// Pencerede metrigi arar. Yalniz RootCandidate deger dondurur; ContextOnly (MEAS-03) baglam olarak
// karsit kanita yazilir ve eksik sayilir.
struct MetricLookup {
    std::optional<double> value;
    bool context_only = false;
};
MetricLookup find_metric(const EvidenceWindow& w, std::string_view name) {
    MetricLookup out;
    for (const auto& m : w.metrics) {
        if (m.metric != name || !m.value) continue;
        if (m.attribution == measure::Attribution::RootCandidate) {
            out.value = m.value;
            out.context_only = false;
            return out;
        }
        if (m.attribution == measure::Attribution::ContextOnly) out.context_only = true;
    }
    return out;
}

bool limiting(ComputeClass c) noexcept {
    return c == ComputeClass::GpuBound || c == ComputeClass::CpuOrEngineBound || c == ComputeClass::Mixed;
}

ComputeVerdict insufficient(ComputeConfidence r, std::string code) {
    ComputeVerdict v;
    v.cls = ComputeClass::Insufficient;
    v.reason = r;
    v.counter.push_back(std::move(code));
    return v;
}

}  // namespace

ComputeVerdict classify_window(const EvidenceWindow& w, const RunComputeInputs& run, const WindowComputeExtras& extras) {
    if (!w.valid || !w.frames.sufficient || !w.frames.avg_fps || !(*w.frames.avg_fps > 0))
        return insufficient(ComputeConfidence::MissingFrames, "frames_insufficient");
    if (!w.unaligned_sources.empty()) {
        auto v = insufficient(ComputeConfidence::MissingMetrics, "source_unaligned");
        for (const auto& s : w.unaligned_sources) v.counter.push_back("source_unaligned:" + s);
        return v;
    }

    ComputeVerdict v;
    const double fps = *w.frames.avg_fps;
    const double frame_ms = 1000.0 / fps;

    bool context_seen = false;
    auto metric = [&](std::string_view name) {
        const auto m = find_metric(w, name);
        if (!m.value && m.context_only) {
            context_seen = true;
            add_unique(v.counter, "context_only:" + std::string(name));
        }
        return m.value;
    };
    const auto gpu = metric(kMetricGpu3d);
    const auto core = metric(kMetricCpuMaxCore);
    const auto main_thread = metric(kMetricCpuMainThread);

    const bool cpu_known = core.has_value() || main_thread.has_value();
    const bool cpu_high = (core && *core >= kCpuCoreHighPct) || (main_thread && *main_thread >= kCpuCoreHighPct);
    const bool gpu_high = gpu && *gpu >= kGpuHighPct;
    const bool gpu_low = gpu && *gpu < kGpuLowPct;
    const bool busy_known = extras.gpu_busy_ms && *extras.gpu_busy_ms > 0;
    const double busy_ratio = busy_known ? *extras.gpu_busy_ms / frame_ms : 0.0;
    const bool busy_short = busy_known && busy_ratio <= kGpuBusyShortRatio;
    const bool busy_full = busy_known && busy_ratio >= kGpuBusyFullRatio;

    // Kare akisi sabit mi? Once acik CV, yoksa p99 / ortalama kare suresi.
    std::optional<bool> stable;
    if (extras.frame_interval_cv) stable = *extras.frame_interval_cv <= kLowVarianceCv;
    else if (w.frames.p99_ms) stable = *w.frames.p99_ms <= frame_ms * (1.0 + 1.5 * kLowVarianceCv);

    // 1) Kare siniri / sunum beklemesi.
    std::optional<double> limit;
    ComputeConfidence cap_reason = ComputeConfidence::CapKnown;
    if (run.fps_cap && *run.fps_cap > 0) {
        limit = run.fps_cap;
        add_unique(v.supporting, "fps_cap_known");
    } else if (run.display_refresh_hz && *run.display_refresh_hz > 0 && run.vsync != PresentSync::VsyncOff) {
        limit = run.display_refresh_hz;
        if (run.vsync == PresentSync::VsyncOn) add_unique(v.supporting, "vsync_on");
        else cap_reason = ComputeConfidence::CapInferredFromRefresh;
    }
    bool near_limit = limit && std::fabs(fps - *limit) / *limit <= kCapTolerance;
    // Uyumluluk B4 (bosluk 14): sinir yenileme hizindan geliyorsa VRR otomatik sinir adaylari (yenileme - yenileme^2/3600,
    // yenileme - 3) da denenir; yalniz acik kare araligi CV'si bilinip dusukken (sabit akis). Bilinen oyun siniri varsa
    // (run.fps_cap) o kullanilir, aday denenmez. Inceleme B4: GPU dolu gorunuyorsa (kare basi GPU suresi >= kare
    // suresinin kGpuBusyFullRatio'su ya da 3D kullanim >= kGpuHighPct) aday kabul edilmez; GPU sinirli dusus tesadufen
    // bu FPS'e denk gelebilir.
    if (!near_limit && limit && !run.fps_cap && extras.frame_interval_cv && *extras.frame_interval_cv <= kLowVarianceCv &&
        !busy_full && !gpu_high) {
        for (const double cand : vrr_cap_candidates(*limit))
            if (std::fabs(fps - cand) / cand <= kCapTolerance) {
                near_limit = true;
                add_unique(v.supporting, "vrr_cap_candidate");
                break;
            }
    }
    if (near_limit) {
        add_unique(v.supporting, "fps_at_cap");
        if (stable == std::optional<bool>(false)) {
            v.cls = ComputeClass::Mixed;
            v.reason = ComputeConfidence::ConflictingSignals;
            add_unique(v.counter, "frame_variance_high");
        } else {
            v.cls = ComputeClass::FrameCapOrPresentWait;
            if (stable) {
                add_unique(v.supporting, "frame_variance_low");
                v.reason = cap_reason;
            } else {
                add_unique(v.counter, "frame_variance_unknown");
                v.reason = ComputeConfidence::SingleSignal;
            }
            if (gpu_high) add_unique(v.counter, "gpu_util_high");
        }
        v.is_fault = false;  // sinirda calismak ariza degildir
        return v;
    }
    if (limit) add_unique(v.counter, "fps_below_cap");

    const bool cap_state_known = (run.fps_cap.has_value() || run.fps_cap_known_none) && run.vsync != PresentSync::Unknown;
    if (!cap_state_known) add_unique(v.counter, "cap_state_unknown");

    // Kanit kodlari.
    if (gpu_high) add_unique(v.supporting, "gpu_util_high");
    if (gpu_low) add_unique(v.supporting, "gpu_util_low");
    if (busy_full) add_unique(v.supporting, "gpu_busy_near_frame_time");
    if (busy_short) add_unique(v.supporting, "gpu_busy_short_of_frame_time");
    if (cpu_high) add_unique(v.supporting, "cpu_core_saturated");

    const bool gpu_signal = gpu_high || busy_full;
    const bool cpu_signal = cpu_high || busy_short;

    if (gpu_signal && cpu_signal) {
        v.cls = ComputeClass::Mixed;
        v.reason = ComputeConfidence::ConflictingSignals;
    } else if (gpu_signal) {
        v.cls = ComputeClass::GpuBound;
        if (cpu_known) add_unique(v.supporting, "cpu_core_not_saturated");
        else add_unique(v.counter, "cpu_metric_missing");
        if (gpu_high && busy_full) v.reason = ComputeConfidence::MultipleCorroborated;
        else if (busy_full) v.reason = cpu_known ? ComputeConfidence::MultipleCorroborated : ComputeConfidence::DirectGpuTiming;
        else v.reason = cpu_known ? ComputeConfidence::MultipleCorroborated : ComputeConfidence::SingleSignal;
    } else if (gpu_low || busy_short) {
        if (cpu_signal) {
            // Dusuk GPU + (tek cekirdek doygun veya GPU zaman damgasi kisa). Cap dislanmali.
            if (cap_state_known) {
                v.cls = ComputeClass::CpuOrEngineBound;
                if (busy_short && !cpu_high) v.reason = ComputeConfidence::DirectGpuTiming;
                else v.reason = ComputeConfidence::MultipleCorroborated;
            } else if (stable == std::optional<bool>(false)) {
                // Degisken kare akisi sabit bir sinirlayici ile uyusmaz; yine de zayif.
                v.cls = ComputeClass::CpuOrEngineBound;
                v.reason = ComputeConfidence::CapStateUnknown;
            } else {
                v.cls = ComputeClass::Mixed;
                v.reason = ComputeConfidence::CapStateUnknown;
            }
        } else if (!cpu_known) {
            v.cls = ComputeClass::Insufficient;
            v.reason = context_seen ? ComputeConfidence::ContextOnlyMetrics : ComputeConfidence::NoCorroboration;
            add_unique(v.counter, "cpu_metric_missing");
        } else {
            v.cls = ComputeClass::Mixed;
            v.reason = ComputeConfidence::NoCorroboration;
            add_unique(v.counter, "cpu_core_not_saturated");
        }
    } else {
        // Sinirlayici isaret yok: yalniz gerekli veriler varsa Healthy; eksikse "darbogaz yok" denmez.
        const bool gpu_known = gpu.has_value() || busy_known;
        if (!gpu_known || !cpu_known) {
            v.cls = ComputeClass::Insufficient;
            v.reason = context_seen ? ComputeConfidence::ContextOnlyMetrics : ComputeConfidence::MissingMetrics;
            if (!gpu_known) add_unique(v.counter, "gpu_metric_missing");
            if (!cpu_known) add_unique(v.counter, "cpu_metric_missing");
        } else if (cpu_high) {
            v.cls = ComputeClass::Mixed;  // GPU orta seviye; tek basina CPU karari yok
            v.reason = ComputeConfidence::NoCorroboration;
        } else {
            v.cls = ComputeClass::Healthy;
            v.reason = ComputeConfidence::NoLimitingSignal;
        }
    }

    // Ariza yalniz beklenen performansin altinda. Yuksek kullanim kendi basina ariza degildir.
    if (run.expected_fps && *run.expected_fps > 0) {
        const bool below = fps < *run.expected_fps * kFaultBelowExpected;
        if (below) {
            add_unique(v.supporting, "fps_below_expected");
            if (limiting(v.cls)) v.is_fault = true;
            else if (v.cls == ComputeClass::Healthy) {
                v.cls = ComputeClass::Mixed;
                v.reason = ComputeConfidence::NoCorroboration;
                add_unique(v.counter, "below_expected_without_limiter");
            }
        } else {
            add_unique(v.supporting, "fps_meets_expected");
        }
    } else if (limiting(v.cls)) {
        add_unique(v.counter, "expected_fps_unknown");
    }
    return v;
}

RunComputeVerdict classify_run(const Timeline& tl, const RunComputeInputs& run, const std::vector<WindowComputeExtras>& extras) {
    RunComputeVerdict out;
    const bool use_extras = extras.size() == tl.windows.size();
    std::array<std::size_t, 6> counts{};
    std::size_t usable = 0, faults = 0;
    for (std::size_t i = 0; i < tl.windows.size(); ++i) {
        const auto& w = tl.windows[i];
        auto v = classify_window(w, run, use_extras ? extras[i] : WindowComputeExtras{});
        if (w.valid && w.unaligned_sources.empty()) {
            ++usable;
            ++counts[static_cast<std::size_t>(v.cls)];
            if (v.is_fault) ++faults;
        }
        out.windows.push_back(std::move(v));
    }
    if (usable == 0) {
        out.overall = insufficient(ComputeConfidence::MissingFrames, "no_usable_windows");
        return out;
    }
    const auto dominant_it = std::max_element(counts.begin(), counts.end());
    const auto dominant = static_cast<ComputeClass>(dominant_it - counts.begin());
    if (static_cast<double>(*dominant_it) < 0.6 * static_cast<double>(usable)) {
        out.overall.cls = ComputeClass::Mixed;
        out.overall.reason = ComputeConfidence::ConflictingSignals;
        out.overall.supporting.push_back("window_classes_split");
    } else {
        out.overall.cls = dominant;
        bool first = true;
        for (std::size_t i = 0; i < tl.windows.size(); ++i) {
            const auto& w = tl.windows[i];
            if (!w.valid || !w.unaligned_sources.empty() || out.windows[i].cls != dominant) continue;
            if (first) {
                out.overall.reason = out.windows[i].reason;
                first = false;
            }
            for (const auto& s : out.windows[i].supporting) add_unique(out.overall.supporting, s);
            for (const auto& c : out.windows[i].counter) add_unique(out.overall.counter, c);
        }
    }
    out.overall.is_fault = static_cast<double>(faults) >= 0.6 * static_cast<double>(usable);
    return out;
}

ComputeVerdict classify_window_gated(const EvidenceWindow& w, const RunComputeInputs& run, const WindowComputeExtras& extras,
                                     const ReleaseStatus& gate) {
    if (!gate.ready) return insufficient(ComputeConfidence::CalibrationUnverified, "diagnosis_calibration_unverified");
    return classify_window(w, run, extras);
}

RunComputeVerdict classify_run_gated(const Timeline& tl, const RunComputeInputs& run,
                                     const std::vector<WindowComputeExtras>& extras, const ReleaseStatus& gate) {
    if (gate.ready) return classify_run(tl, run, extras);
    RunComputeVerdict out;
    out.overall = insufficient(ComputeConfidence::CalibrationUnverified, "diagnosis_calibration_unverified");
    out.windows.assign(tl.windows.size(), out.overall);  // pencere sayisi/sirasi korunur
    return out;
}

std::string_view to_string(ComputeClass c) noexcept {
    switch (c) {
        case ComputeClass::GpuBound: return "gpu_bound";
        case ComputeClass::CpuOrEngineBound: return "cpu_or_engine_bound";
        case ComputeClass::FrameCapOrPresentWait: return "frame_cap_or_present_wait";
        case ComputeClass::Mixed: return "mixed";
        case ComputeClass::Healthy: return "healthy";
        case ComputeClass::Insufficient: return "insufficient";
    }
    return "insufficient";
}

std::string_view to_string(ComputeConfidence r) noexcept {
    switch (r) {
        case ComputeConfidence::MultipleCorroborated: return "multiple_corroborated";
        case ComputeConfidence::DirectGpuTiming: return "direct_gpu_timing";
        case ComputeConfidence::SingleSignal: return "single_signal";
        case ComputeConfidence::CapKnown: return "cap_known";
        case ComputeConfidence::CapInferredFromRefresh: return "cap_inferred_from_refresh";
        case ComputeConfidence::CapStateUnknown: return "cap_state_unknown";
        case ComputeConfidence::ConflictingSignals: return "conflicting_signals";
        case ComputeConfidence::NoCorroboration: return "no_corroboration";
        case ComputeConfidence::ContextOnlyMetrics: return "context_only_metrics";
        case ComputeConfidence::MissingMetrics: return "missing_metrics";
        case ComputeConfidence::MissingFrames: return "missing_frames";
        case ComputeConfidence::NoLimitingSignal: return "no_limiting_signal";
        case ComputeConfidence::CalibrationUnverified: return "calibration_unverified";
    }
    return "missing_metrics";
}

}  // namespace ef::core::diagnosis
