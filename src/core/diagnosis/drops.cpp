#include "core/diagnosis/drops.h"

#include "core/diagnosis/platform.h"  // F-61: evaluate_limits + perf-caps eslemesi

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ef::core::diagnosis {

namespace {

void add_unique(std::vector<std::string>& v, std::string code) {
    if (std::find(v.begin(), v.end(), code) == v.end()) v.push_back(std::move(code));
}

std::size_t index_of(DropSource s) noexcept { return static_cast<std::size_t>(s); }

double median_of_sorted(const std::vector<double>& sorted) noexcept {
    const std::size_t n = sorted.size();
    if (n == 0) return 0.0;
    if (n % 2 == 1) return sorted[n / 2];
    return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}

// frame_stats.cpp ile ayni nearest-rank tanimi.
double nearest_rank(const std::vector<double>& sorted, double pct) noexcept {
    const std::size_t n = sorted.size();
    if (n == 0) return 0.0;
    std::size_t rank = static_cast<std::size_t>(std::ceil(pct / 100.0 * static_cast<double>(n)));
    rank = std::clamp<std::size_t>(rank, 1, n);
    return sorted[rank - 1];
}

WindowScope scope_at(double t_ms, const std::vector<ScopeRange>& scopes) noexcept {
    for (const auto& s : scopes)
        if (t_ms >= s.start_ms && t_ms < s.end_ms) return s.scope;
    return WindowScope::Measured;
}

struct MetricProbe {
    std::optional<double> value;
    bool context_only = false;
};

MetricProbe probe_metric(const EvidenceWindow& w, std::string_view name) {
    MetricProbe out;
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

// Cagiranin ek metrik adlari + modulun bildigi adlar (tekrarsiz).
std::vector<std::string> all_metric_names(const std::vector<std::string>& extra) {
    std::vector<std::string> out = extra;
    const std::string_view known[] = {kMetricGpu3d,       kMetricCpuMaxCore,     kMetricCpuMainThread,
                                      kMetricGpuBusyMs,   kMetricDiskQueueLen,   kMetricDiskIoMs,
                                      kMetricOtherProcessCpu, kMetricCommitUsedPct, kMetricHardFaultRate};
    for (const std::string_view name : known) add_unique(out, std::string(name));
    return out;
}

double coefficient_of_variation(const std::vector<double>& v) noexcept {
    if (v.empty()) return 0.0;
    double sum = 0;
    for (double x : v) sum += x;
    const double mean = sum / static_cast<double>(v.size());
    if (!(mean > 0) || !std::isfinite(mean)) return 0.0;
    double var = 0;
    for (double x : v) {
        const double d = x - mean;
        var += d * d;
    }
    var /= static_cast<double>(v.size());
    const double cv = std::sqrt(var) / mean;
    return std::isfinite(cv) ? cv : 0.0;
}

struct CapPattern {
    bool matched = false;
    bool known = false;  // cap/VSync durumu biliniyor (fps cap veya VSync acik)
    bool via_fps_cap = false;
    bool via_refresh = false;
    bool vsync_on = false;
    bool multiple = false;  // aralik, periyodun >=2 kati (kacirilan sunum)
};

// Tum pencereler periyodun yakin tamsayi kati mi? (cap/VSync'e sabitlenme deseni)
bool matches_period(const std::vector<double>& intervals, double period_ms, double tolerance, bool& multiple) {
    multiple = false;
    if (!(period_ms > 0) || !std::isfinite(period_ms) || intervals.empty()) return false;
    bool all = true;
    bool any_multiple = false;
    for (double iv : intervals) {
        if (!std::isfinite(iv) || !(iv > 0)) {
            all = false;
            break;
        }
        const double ratio = iv / period_ms;
        if (!std::isfinite(ratio) || ratio > 1.0e12) {
            all = false;
            break;
        }
        const long long level = std::llround(ratio);
        if (level < 1) {
            all = false;
            break;
        }
        const double err = std::fabs(ratio - static_cast<double>(level)) / static_cast<double>(level);
        if (!(err <= tolerance)) {
            all = false;
            break;
        }
        if (level >= 2) any_multiple = true;
    }
    multiple = any_multiple;
    return all;
}

CapPattern match_cap_pattern(const std::vector<double>& intervals, const RunComputeInputs& run,
                             const DropThresholds& th) {
    CapPattern p;
    p.vsync_on = run.vsync == PresentSync::VsyncOn;
    double tolerance = th.refresh_tolerance;
    if (!std::isfinite(tolerance) || tolerance < 0) tolerance = 0.0;
    if (run.fps_cap && std::isfinite(*run.fps_cap) && *run.fps_cap > 0) {
        bool mult = false;
        if (matches_period(intervals, 1000.0 / *run.fps_cap, tolerance, mult)) {
            p.matched = true;
            p.via_fps_cap = true;
            p.multiple = mult;
        }
    }
    // VSync kapaliysa yenileme periyoduna sabitlenme present-wait kaniti degildir.
    if (!p.matched && run.display_refresh_hz && std::isfinite(*run.display_refresh_hz) &&
        *run.display_refresh_hz > 0 && run.vsync != PresentSync::VsyncOff) {
        bool mult = false;
        if (matches_period(intervals, 1000.0 / *run.display_refresh_hz, tolerance, mult)) {
            p.matched = true;
            p.via_refresh = true;
            p.multiple = mult;
        }
    }
    p.known = p.matched && (p.via_fps_cap || (p.via_refresh && p.vsync_on));
    return p;
}

void collect_metric_signal(const EvidenceWindow& w, std::string_view name, double threshold,
                           std::string_view high_code, std::string_view low_code, bool& signal,
                           std::vector<std::string>& sup, std::vector<std::string>& ctr,
                           std::vector<std::string>& notes) {
    const MetricProbe p = probe_metric(w, name);
    if (p.context_only) add_unique(ctr, std::string(kEvContextOnlyPrefix) + std::string(name));
    if (!p.value) {
        // Sayac yok: "yok" demek "sorun yok" demek degildir; not olarak kalir.
        add_unique(notes, std::string(kEvMetricMissingPrefix) + std::string(name));
        return;
    }
    if (*p.value >= threshold) {
        signal = true;
        add_unique(sup, std::string(high_code));
    } else {
        add_unique(ctr, std::string(low_code));
    }
}

bool any_root_candidate(const EvidenceWindow& w) {
    for (const auto& m : w.metrics)
        if (m.attribution == measure::Attribution::RootCandidate) return true;
    return false;
}

// Bir drop penceresini kanit penceresine cevirip kaynak sinifini doldurur.
void classify_drop_window(DropWindow& win, const std::vector<FramePoint>& frames, const std::vector<double>& frame_busy,
                          const std::vector<DropTelemetrySource>& aligned_sources,
                          const std::vector<std::string>& metric_names, const DropRunInputs& run,
                          const DropThresholds& th) {
    std::vector<FramePoint> slice;
    std::vector<double> window_intervals;
    const std::size_t n = win.last_frame - win.first_frame + 1;
    slice.reserve(n + 1);
    window_intervals.reserve(n);
    slice.push_back(FramePoint{win.start_ms, 0.0});  // pencere basi capa; istatistik asagida
    for (std::size_t i = win.first_frame; i <= win.last_frame; ++i) {
        slice.push_back(frames[i]);
        window_intervals.push_back(frames[i].interval_ms);
    }

    std::vector<std::pair<SourceInfo, std::vector<measure::TelemetrySample>>> pairs;
    pairs.reserve(aligned_sources.size());
    for (const auto& s : aligned_sources) pairs.emplace_back(s.info, s.samples);

    const double span = win.end_ms - win.start_ms;
    EvidenceWindow w;
    if (span > 0) {
        const Timeline tl = build_timeline(run.run_id, run.frame_domain, slice, pairs, metric_names, {}, span);
        if (!tl.windows.empty()) w = tl.windows.front();
    }
    w.run_id = run.run_id;
    w.start_ms = win.start_ms;
    w.end_ms = win.end_ms;
    w.scope = WindowScope::Measured;
    // Drop penceresi kisa olabilir: pencere ici istatistik icin asgari N=1 (kosu istatistigi ayri kalir).
    w.frames = measure::compute_frame_stats(window_intervals, 1);
    w.valid = w.frames.sufficient;
    w.unaligned_sources.clear();  // hizasiz kaynaklar cagiran tarafta suzuldu, kok neden degiller

    WindowComputeExtras extras;
    MetricProbe busy = probe_metric(w, kMetricGpuBusyMs);
    if (frame_busy.size() == frames.size() && !frames.empty()) {
        // Spec 2026-11-12 B: kare olayinin kendi GPU suresi; pencere ortalamasi = toplam / kare sayisi.
        busy = MetricProbe{};
        double sum = 0;
        bool known = true;
        for (std::size_t i = win.first_frame; i <= win.last_frame; ++i) {
            const double b = frame_busy[i];
            if (!std::isfinite(b) || b < 0.0) {
                known = false;
                break;
            }
            sum += b;
        }
        if (known) busy.value = sum / static_cast<double>(n);
    }
    if (busy.value) extras.gpu_busy_ms = busy.value;
    extras.frame_interval_cv = coefficient_of_variation(window_intervals);

    const ComputeVerdict v = classify_window(w, run.compute, extras);

    std::vector<std::string> sup = v.supporting;
    std::vector<std::string> ctr = v.counter;
    std::vector<std::string> notes;
    add_unique(notes, std::string(kEvComputeReasonPrefix) + std::string(to_string(v.reason)));
    if (busy.context_only) add_unique(ctr, std::string(kEvContextOnlyPrefix) + std::string(kMetricGpuBusyMs));

    const bool compute_conflict = v.cls == ComputeClass::Mixed;
    bool gpu_signal = v.cls == ComputeClass::GpuBound;
    bool cpu_signal = v.cls == ComputeClass::CpuOrEngineBound;
    bool cap_signal = v.cls == ComputeClass::FrameCapOrPresentWait;

    if (!run.other_cpu_times.empty()) {
        // B3 inceleme (bosluk 2): ham sure okumalarindan pencerenin kendi zamani; ornek bazli deger yerine gecer.
        std::erase_if(w.metrics, [](const measure::WindowContext& m) { return m.metric == kMetricOtherProcessCpu; });
        measure::WindowContext ctx;
        ctx.metric = std::string(kMetricOtherProcessCpu);
        const auto to_us = [](double ms) { return static_cast<std::int64_t>(std::llround(ms * 1000.0)); };
        const auto other = metrics::other_cpu_time_percent_around(
            run.other_cpu_times, metrics::TimeWindow{to_us(win.start_ms), to_us(win.end_ms)},
            to_us(std::max(1.0, th.other_process_cpu_min_span_ms)));
        if (other.has_value()) {
            ctx.value = *other.value();
            ctx.attribution = measure::Attribution::RootCandidate;
        }
        w.metrics.push_back(std::move(ctx));
    }

    bool bg_signal = false;
    collect_metric_signal(w, kMetricDiskQueueLen, th.disk_queue_high, kEvDiskQueueHigh, kEvDiskQueueLow,
                          bg_signal, sup, ctr, notes);
    collect_metric_signal(w, kMetricDiskIoMs, th.disk_io_ms_high, kEvDiskIoLatencyHigh, kEvDiskIoLatencyLow,
                          bg_signal, sup, ctr, notes);
    collect_metric_signal(w, kMetricOtherProcessCpu, th.other_process_cpu_high, kEvOtherProcessCpuHigh,
                          kEvOtherProcessCpuLow, bg_signal, sup, ctr, notes);

    bool mem_signal = false;
    collect_metric_signal(w, kMetricCommitUsedPct, th.commit_used_high, kEvCommitPressureHigh,
                          kEvCommitPressureLow, mem_signal, sup, ctr, notes);
    collect_metric_signal(w, kMetricHardFaultRate, th.hard_fault_rate_high, kEvHardFaultRateHigh,
                          kEvHardFaultRateLow, mem_signal, sup, ctr, notes);

    // Cap/VSync deseni: penceredeki TUM araliklar periyodun kati olmali (guclu, deterministik kanit).
    const CapPattern cap = match_cap_pattern(window_intervals, run.compute, th);
    if (cap.matched) {
        cap_signal = true;
        if (cap.via_fps_cap) {
            add_unique(sup, std::string(kEvFpsCapIntervalMatch));
            add_unique(sup, std::string(kEvFpsCapKnown));
        }
        if (cap.via_refresh) {
            add_unique(sup, std::string(kEvRefreshIntervalMatch));
            add_unique(sup, cap.vsync_on ? std::string(kEvVsyncOn) : std::string(kEvCapInferredFromRefresh));
        }
        if (cap.multiple) add_unique(sup, std::string(kEvPresentIntervalMultiple));
    }

    const int signal_count = (gpu_signal ? 1 : 0) + (cpu_signal ? 1 : 0) + (bg_signal ? 1 : 0) +
                             (mem_signal ? 1 : 0);

    if (compute_conflict) {
        win.source = DropSource::Insufficient;
        win.confidence = DropConfidence::Insufficient;
        add_unique(ctr, std::string(kEvConflictingSources));
    } else if (cap_signal) {
        if (signal_count > 0) {
            win.source = DropSource::Insufficient;
            win.confidence = DropConfidence::Insufficient;
            add_unique(ctr, std::string(kEvConflictingSources));
            add_unique(notes, std::string(kEvCapAndResourceSignal));
        } else {
            win.source = DropSource::FrameCapOrPresentWait;
            const bool strong = (cap.matched && cap.known) || v.reason == ComputeConfidence::CapKnown;
            win.confidence = strong ? DropConfidence::Corroborated : DropConfidence::SingleSignal;
        }
    } else if (signal_count == 1) {
        std::size_t positive_codes = 0;
        for (const auto& code : sup)
            if (code == kEvDiskQueueHigh || code == kEvDiskIoLatencyHigh || code == kEvOtherProcessCpuHigh ||
                code == kEvCommitPressureHigh || code == kEvHardFaultRateHigh)
                ++positive_codes;
        if (gpu_signal) {
            win.source = DropSource::GpuBound;
            win.confidence = (v.reason == ComputeConfidence::MultipleCorroborated ||
                              v.reason == ComputeConfidence::DirectGpuTiming)
                                 ? DropConfidence::Corroborated
                                 : DropConfidence::SingleSignal;
        } else if (cpu_signal) {
            win.source = DropSource::CpuOrEngineBound;
            win.confidence = (v.reason == ComputeConfidence::MultipleCorroborated ||
                              v.reason == ComputeConfidence::DirectGpuTiming)
                                 ? DropConfidence::Corroborated
                                 : DropConfidence::SingleSignal;
        } else if (bg_signal) {
            win.source = DropSource::BackgroundOrIo;
            win.confidence = positive_codes >= 2 ? DropConfidence::Corroborated : DropConfidence::SingleSignal;
        } else {
            win.source = DropSource::MemoryPressure;
            win.confidence = positive_codes >= 2 ? DropConfidence::Corroborated : DropConfidence::SingleSignal;
        }
    } else if (signal_count > 1) {
        win.source = DropSource::Insufficient;
        win.confidence = DropConfidence::Insufficient;
        add_unique(ctr, std::string(kEvConflictingSources));
    } else {
        win.source = DropSource::Insufficient;
        win.confidence = DropConfidence::Insufficient;
        if (aligned_sources.empty()) {
            add_unique(ctr, std::string(kEvNoAlignedMetrics));
        } else if (!any_root_candidate(w)) {
            add_unique(ctr, std::string(kEvMetricsMissing));
        } else {
            add_unique(ctr, std::string(kEvNoLimitingSignalInDrop));
        }
    }

    win.supporting = std::move(sup);
    win.counter = std::move(ctr);
    win.notes = std::move(notes);
}

struct Candidate {
    std::size_t first = 0;
    std::size_t last = 0;
    std::size_t frame_count = 0;
    std::size_t slow_count = 0;
    double start_ms = 0;
    double end_ms = 0;
    double duration_ms = 0;
    double worst_ms = 0;
    double avg_fps = 0;
    std::size_t lost = 0;
};

std::size_t saturating_add(std::size_t a, std::size_t b) noexcept {
    const std::size_t max = std::numeric_limits<std::size_t>::max();
    return a > max - b ? max : a + b;
}

double safe_avg_fps(std::size_t frames, double duration_ms) noexcept {
    if (!(duration_ms > 0) || !std::isfinite(duration_ms)) return 0.0;
    const double fps = static_cast<double>(frames) * 1000.0 / duration_ms;
    return std::isfinite(fps) ? fps : 0.0;
}

Candidate make_candidate(const std::vector<FramePoint>& frames, std::size_t first, std::size_t last,
                         std::size_t slow_count, double median_ms) {
    Candidate c;
    c.first = first;
    c.last = last;
    c.frame_count = last - first + 1;
    c.slow_count = slow_count;
    double sum = 0;
    double worst = 0;
    for (std::size_t i = first; i <= last; ++i) {
        const double iv = frames[i].interval_ms;
        sum += iv;
        if (iv > worst) worst = iv;
    }
    c.start_ms = frames[first].t_ms - frames[first].interval_ms;
    c.duration_ms = sum;  // yalniz olculen araliklar
    c.end_ms = c.start_ms + c.duration_ms;
    c.worst_ms = worst;
    c.avg_fps = safe_avg_fps(c.frame_count, c.duration_ms);
    if (median_ms > 0 && std::isfinite(median_ms)) {
        const double expected = std::round(sum / median_ms);
        if (std::isfinite(expected) && expected > static_cast<double>(c.frame_count)) {
            const double extra = expected - static_cast<double>(c.frame_count);
            // size_t ust sinirinin double yuvarlamasi ile UB'ye girmeden doyur.
            const double limit = static_cast<double>(std::numeric_limits<std::size_t>::max() / 2);
            c.lost = extra >= limit ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(extra);
        }
    }
    return c;
}

void consider_worst(std::vector<Candidate>& worst, const Candidate& c, std::size_t limit) {
    if (limit == 0) return;
    worst.push_back(c);
    std::stable_sort(worst.begin(), worst.end(), [](const Candidate& a, const Candidate& b) {
        if (a.worst_ms != b.worst_ms) return a.worst_ms > b.worst_ms;
        return a.duration_ms > b.duration_ms;
    });
    if (worst.size() > limit) worst.resize(limit);
}

DropWindow to_window(const Candidate& c) {
    DropWindow w;
    w.first_frame = c.first;
    w.last_frame = c.last;
    w.start_ms = c.start_ms;
    w.end_ms = c.end_ms;
    w.frame_count = c.frame_count;
    w.slow_frame_count = c.slow_count;
    w.worst_ms = c.worst_ms;
    w.avg_fps = c.avg_fps;
    w.estimated_lost_frames = c.lost;
    return w;
}

void add_common_notes(DropReport& r) {
    if (!r.unaligned_sources.empty()) add_unique(r.notes, std::string(kEvUnalignedSourcesIgnored));
    if (r.windows_truncated) add_unique(r.notes, std::string(kEvWindowLimitAggregated));
}

void merge_codes(std::vector<std::string>& dst, const std::vector<std::string>& src) {
    for (const auto& code : src) add_unique(dst, code);
}

bool contains_code(const std::vector<std::string>& v, std::string_view code) {
    return std::find(v.begin(), v.end(), code) != v.end();
}

// F-66: shader/PSO derleme takilmasi SUPHELI deseni. Kesin teshis degil:
//  - pencere kaynak sinyali tasimiyor (classify "no_limiting_signal_in_drop" dedi),
//  - yalitilmis (en fazla shader_max_slow_frames yavas kare),
//  - en az shader_min_windows kez tekrarliyor,
//  - en kotu kare buyuklukleri birbirine yakin.
// Kosul saglanirsa pencere Insufficient'tan PipelineCompileSuspected'a tasinir; kaynak sinyali olan
// pencereye asla dokunulmaz. Kanit tek desen oldugu icin guven en fazla SingleSignal'dir.
void mark_pipeline_compile_suspected(std::vector<DropWindow>& windows, const DropThresholds& th) {
    if (windows.size() < 2) return;
    const std::size_t max_slow = std::max<std::size_t>(th.shader_max_slow_frames, 1);
    const std::size_t max_frames = max_slow + 2;

    std::vector<std::size_t> candidates;
    std::vector<double> worsts;
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const DropWindow& w = windows[i];
        if (w.source != DropSource::Insufficient) continue;
        if (!contains_code(w.counter, kEvNoLimitingSignalInDrop)) continue;
        if (w.slow_frame_count > max_slow || w.frame_count > max_frames) continue;
        const double span = w.end_ms - w.start_ms;
        if (!std::isfinite(span) || span < 0 || span > th.shader_max_window_ms) continue;
        if (!(w.worst_ms > 0) || !std::isfinite(w.worst_ms)) continue;
        candidates.push_back(i);
        worsts.push_back(w.worst_ms);
    }
    if (candidates.size() < std::max<std::size_t>(th.shader_min_windows, 2)) return;

    std::sort(worsts.begin(), worsts.end());
    const double ref = median_of_sorted(worsts);
    if (!(ref > 0) || !std::isfinite(ref)) return;
    for (const double w : worsts)
        if (std::fabs(w - ref) / ref > th.shader_worst_tolerance) return;

    for (const std::size_t i : candidates) {
        DropWindow& w = windows[i];
        w.source = DropSource::PipelineCompileSuspected;
        w.confidence = DropConfidence::SingleSignal;
        add_unique(w.supporting, std::string(kEvPipelineCompileSuspected));
        add_unique(w.notes, std::string(kEvPipelineCompileAdvice));
    }
}

}  // namespace

std::string_view to_string(DropSource s) noexcept {
    switch (s) {
        case DropSource::NoDrops: return "no_drops";
        case DropSource::GpuBound: return "gpu_bound";
        case DropSource::CpuOrEngineBound: return "cpu_or_engine_bound";
        case DropSource::FrameCapOrPresentWait: return "frame_cap_or_present_wait";
        case DropSource::BackgroundOrIo: return "background_or_io";
        case DropSource::MemoryPressure: return "memory_pressure";
        case DropSource::PipelineCompileSuspected: return "pipeline_compile_suspected";
        case DropSource::Insufficient: return "insufficient";
    }
    return "insufficient";
}

std::string_view to_string(DropConfidence c) noexcept {
    switch (c) {
        case DropConfidence::Corroborated: return "corroborated";
        case DropConfidence::SingleSignal: return "single_signal";
        case DropConfidence::Insufficient: return "insufficient";
    }
    return "insufficient";
}

std::string_view to_string(DropOutcome o) noexcept {
    switch (o) {
        case DropOutcome::Insufficient: return "insufficient";
        case DropOutcome::NoDrops: return "no_drops";
        case DropOutcome::DropsFound: return "drops_found";
    }
    return "insufficient";
}

std::string_view drop_source_key(DropSource s) noexcept {
    switch (s) {
        case DropSource::NoDrops: return "drop.source.no_drops";
        case DropSource::GpuBound: return "drop.source.gpu_bound";
        case DropSource::CpuOrEngineBound: return "drop.source.cpu_or_engine_bound";
        case DropSource::FrameCapOrPresentWait: return "drop.source.frame_cap_or_present_wait";
        case DropSource::BackgroundOrIo: return "drop.source.background_or_io";
        case DropSource::MemoryPressure: return "drop.source.memory_pressure";
        case DropSource::PipelineCompileSuspected: return "drop.source.pipeline_compile_suspected";
        case DropSource::Insufficient: return "drop.source.insufficient";
    }
    return "drop.source.insufficient";
}

std::string_view drop_confidence_key(DropConfidence c) noexcept {
    switch (c) {
        case DropConfidence::Corroborated: return "drop.confidence.corroborated";
        case DropConfidence::SingleSignal: return "drop.confidence.single_signal";
        case DropConfidence::Insufficient: return "drop.confidence.insufficient";
    }
    return "drop.confidence.insufficient";
}

std::string_view drop_outcome_key(DropOutcome o) noexcept {
    switch (o) {
        case DropOutcome::Insufficient: return "drop.outcome.insufficient";
        case DropOutcome::NoDrops: return "drop.outcome.no_drops";
        case DropOutcome::DropsFound: return "drop.outcome.drops_found";
    }
    return "drop.outcome.insufficient";
}

std::string drop_evidence_key(std::string_view code) {
    // Kare/telemetri modullerinin urettigi kararli kodlar. UI bunlari yerellestirir; bilinmeyen
    // kod raw Ingilizce olarak gosterilmez.
    static constexpr std::pair<std::string_view, std::string_view> kExact[]{
        {"gpu_util_high", "drop.evidence.gpu_util_high"},
        {"gpu_util_low", "drop.evidence.gpu_util_low"},
        {"gpu_busy_near_frame_time", "drop.evidence.gpu_busy_near_frame_time"},
        {"gpu_busy_short_of_frame_time", "drop.evidence.gpu_busy_short_of_frame_time"},
        {"gpu_metric_missing", "drop.evidence.gpu_metric_missing"},
        {"cpu_core_saturated", "drop.evidence.cpu_core_saturated"},
        {"cpu_core_not_saturated", "drop.evidence.cpu_core_not_saturated"},
        {"cpu_metric_missing", "drop.evidence.cpu_metric_missing"},
        {"fps_at_cap", "drop.evidence.fps_at_cap"},
        {"vrr_cap_candidate", "drop.evidence.vrr_cap_candidate"},
        {"fps_cap_known", "drop.evidence.fps_cap_known"},
        {"vsync_on", "drop.evidence.vsync_on"},
        {"refresh_interval_match", "drop.evidence.refresh_interval_match"},
        {"fps_cap_interval_match", "drop.evidence.fps_cap_interval_match"},
        {"present_interval_multiple", "drop.evidence.present_interval_multiple"},
        {"cap_inferred_from_refresh", "drop.evidence.cap_inferred_from_refresh"},
        {"disk_queue_high", "drop.evidence.disk_queue_high"},
        {"disk_queue_low", "drop.evidence.disk_queue_low"},
        {"disk_io_latency_high", "drop.evidence.disk_io_latency_high"},
        {"disk_io_latency_low", "drop.evidence.disk_io_latency_low"},
        {"other_process_cpu_high", "drop.evidence.other_process_cpu_high"},
        {"other_process_cpu_low", "drop.evidence.other_process_cpu_low"},
        {"commit_pressure_high", "drop.evidence.commit_pressure_high"},
        {"commit_pressure_low", "drop.evidence.commit_pressure_low"},
        {"hard_fault_rate_high", "drop.evidence.hard_fault_rate_high"},
        {"hard_fault_rate_low", "drop.evidence.hard_fault_rate_low"},
        {"no_aligned_metrics", "drop.evidence.no_aligned_metrics"},
        {"metrics_missing", "drop.evidence.metrics_missing"},
        {"conflicting_sources", "drop.evidence.conflicting_sources"},
        {"no_limiting_signal_in_drop", "drop.evidence.no_limiting_signal_in_drop"},
        {"pipeline_compile_suspected", "drop.evidence.pipeline_compile_suspected"},
        {"pipeline_compile_advice", "drop.evidence.pipeline_compile_advice"},
        {"game_private_growth", "drop.evidence.game_private_growth"},
        {"game_restart_advice", "drop.evidence.game_restart_advice"},
        {"game_vram_near_capacity", "drop.evidence.game_vram_near_capacity"},
        {"game_vram_advice", "drop.evidence.game_vram_advice"},
        {"gpu_thermal_limit", "drop.evidence.gpu_thermal_limit"},
        {"gpu_power_limit", "drop.evidence.gpu_power_limit"},
        {"gpu_clock_drop_unattributed", "drop.evidence.gpu_clock_drop_unattributed"},
        {"gpu_thermal_advice", "drop.evidence.gpu_thermal_advice"},
        {"gpu_power_advice", "drop.evidence.gpu_power_advice"},
        {"gpu_limit_unattributed_advice", "drop.evidence.gpu_limit_unattributed_advice"},
        {"gpu_vendor_api_missing", "drop.evidence.gpu_vendor_api_missing"},
        {"game_api_d3d12", "drop.evidence.game_api_d3d12"},
        {"game_api_d3d11", "drop.evidence.game_api_d3d11"},
        {"game_api_vulkan", "drop.evidence.game_api_vulkan"},
        {"game_api_d3d9", "drop.evidence.game_api_d3d9"},
        {"cpu_placement_both", "drop.evidence.cpu_placement_both"},
        {"cpu_placement_p_only", "drop.evidence.cpu_placement_p_only"},
        {"cpu_placement_e_only", "drop.evidence.cpu_placement_e_only"},
        {"background_overlay", "drop.evidence.background_overlay"},
        {"background_rgb", "drop.evidence.background_rgb"},
        {"background_monitoring", "drop.evidence.background_monitoring"},
        {"game_disk_hdd", "drop.evidence.game_disk_hdd"},
        {"no_drop_windows", "drop.evidence.no_drop_windows"},
        {"windows_below_minimum", "drop.evidence.windows_below_minimum"},
        {"frames_insufficient", "drop.evidence.frames_insufficient"},
        {"windows_aggregated", "drop.evidence.windows_aggregated"},
        {"window_limit_not_classified", "drop.evidence.window_limit_not_classified"},
        {"unaligned_sources_ignored", "drop.evidence.unaligned_sources_ignored"},
        {"no_classified_drops", "drop.evidence.no_classified_drops"},
        {"window_classes_split", "drop.evidence.window_classes_split"},
        {"cap_and_resource_signal", "drop.evidence.cap_and_resource_signal"},
        {"context_only_metrics", "drop.evidence.context_only"},
    };
    auto exact = [&](std::string_view c) -> const char* {
        for (const auto& [k, v] : kExact)
            if (k == c) return v.data();
        return nullptr;
    };
    if (const char* k = exact(code)) return std::string(k);
    if (code.starts_with(kEvMetricMissingPrefix)) {
        const std::string_view metric = code.substr(kEvMetricMissingPrefix.size());
        static constexpr std::string_view kMetrics[] = {
            kMetricGpu3d,       kMetricCpuMaxCore,   kMetricCpuMainThread, kMetricDiskQueueLen,
            kMetricDiskIoMs,    kMetricCommitUsedPct, kMetricHardFaultRate, kMetricGameVramMib};
        for (const auto m : kMetrics)
            if (metric == m) return "drop.metric_missing." + std::string(m);
        return "drop.metric_missing.other";
    }
    if (code.starts_with(kEvContextOnlyPrefix)) return "drop.evidence.context_only";
    if (code.starts_with(kEvComputeReasonPrefix)) {
        const std::string_view why = code.substr(kEvComputeReasonPrefix.size());
        if (const char* k = exact(why)) return std::string(k);
        return "drop.evidence.compute_reason_other";
    }
    return "drop.evidence.unknown";
}

std::vector<DropTelemetrySource> drop_sources_from_live(const std::vector<LiveDropSample>& in) {
    if (in.empty()) return {};  // ornek yok: kaynak uydurulmaz
    // Tum ornekler ayni hiza durumundaysa kaynak hizali sayilir; tek bir hizasiz ornek bile
    // kaynagi hizasiz yapar (kok neden olarak kullanilamaz).
    const bool aligned_all = std::all_of(in.begin(), in.end(), [](const LiveDropSample& s) { return s.aligned; });
    // MEAS-03: ornekleme araligi gercek zaman damgalarindan (medyan fark) gelir; sabit varsayilmaz.
    // Tek ornek/vakit farki yoksa sonsuz aralik -> can_attribute false -> yalniz baglam.
    std::vector<double> deltas;
    deltas.reserve(in.size() > 1 ? in.size() - 1 : 0);
    for (std::size_t i = 1; i < in.size(); ++i) {
        const double d = in[i].t_ms - in[i - 1].t_ms;
        if (std::isfinite(d) && d > 0) deltas.push_back(d);
    }
    std::sort(deltas.begin(), deltas.end());
    const double sample_interval_ms =
        deltas.empty() ? std::numeric_limits<double>::infinity() : median_of_sorted(deltas);
    DropTelemetrySource src;
    src.info.id = "pdh";
    src.info.domain = aligned_all ? ClockDomain::Qpc : ClockDomain::Unknown;
    src.info.offset_ms = 0;
    src.info.offset_verified = aligned_all;

    auto missing_name = [](contracts::MissingReason r) -> std::string {
        switch (r) {
            case contracts::MissingReason::NotSupported: return "not_supported";
            case contracts::MissingReason::PermissionDenied: return "permission_denied";
            case contracts::MissingReason::NotSampled: return "not_sampled";
            case contracts::MissingReason::SourceError: return "source_error";
            case contracts::MissingReason::Invalid: return "invalid";
            case contracts::MissingReason::None: break;
        }
        return "not_sampled";
    };
    auto push = [&](std::string_view metric, double t_ms, const contracts::Measurement& m) {
        measure::TelemetrySample s;
        s.metric = std::string(metric);
        s.source = measure::MetricSource::Pdh;
        s.t_ms = t_ms;
        s.interval_ms = sample_interval_ms;
        if (m.has_value()) s.value = *m.value();
        else s.missing_reason = missing_name(m.missing_reason());
        src.samples.push_back(std::move(s));
    };
    src.samples.reserve(in.size() * 9);
    for (const auto& x : in) {
        // Adlar drop modulunun bildigi adlardir; okunmayan deger missing_reason ile tasinir.
        push(kMetricGpu3d, x.t_ms, x.snapshot.gpu_pct);
        push(kMetricCpuMaxCore, x.t_ms, x.snapshot.cpu_max_core_pct);
        push(kMetricCpuMainThread, x.t_ms, x.snapshot.cpu_main_thread_pct);  // DIAG-10: hedef oyun ana thread
        push(kMetricDiskQueueLen, x.t_ms, x.snapshot.disk_queue_len);
        push(kMetricDiskIoMs, x.t_ms, x.snapshot.disk_io_ms);
        push(kMetricCommitUsedPct, x.t_ms, x.snapshot.commit_used_pct);
        push(kMetricHardFaultRate, x.t_ms, x.snapshot.hard_fault_rate);
        push(kMetricOtherProcessCpu, x.t_ms, x.snapshot.other_process_cpu_pct);
        push(kMetricGamePrivateMib, x.t_ms, x.snapshot.game_private_mib);  // F-67: kosu duzeyi
        push(kMetricGameVramMib, x.t_ms, x.snapshot.game_vram_dedicated_mib);  // F-63: VRAM telemetrisi
    }
    return {std::move(src)};
}

std::vector<GpuLimitWindow> gpu_limit_windows_from_live(const std::vector<LiveDropSample>& in) {
    std::vector<GpuLimitWindow> out;
    out.reserve(in.size());
    for (const auto& x : in) {
        if (!x.aligned) continue;  // hizasiz ornek: kare ekseniyle eslenemez
        std::optional<std::uint32_t> mask;
        if (const auto m = x.snapshot.gpu_vendor_perf_decrease_mask.value();
            m && std::isfinite(*m) && *m >= 0.0 && *m <= 4294967295.0)
            mask = static_cast<std::uint32_t>(*m);
        std::optional<double> load, clock;
        if (const auto l = x.snapshot.gpu_vendor_utilization_pct.value(); l && std::isfinite(*l))
            load = *l;
        if (const auto c = x.snapshot.gpu_vendor_clock_mhz.value(); c && std::isfinite(*c) && *c > 0.0)
            clock = *c;
        if (!mask && !clock) continue;  // limit alani okunmadi: pencere uydurulmaz
        GpuLimitWindow w;
        w.t_ms = x.t_ms;
        w.perf_decrease_mask = mask;
        w.load_pct = load;
        w.clock_mhz = clock;
        out.push_back(std::move(w));
    }
    return out;
}

std::size_t source_count(const DropReport& r, DropSource s) noexcept {
    const std::size_t idx = index_of(s);
    return idx < r.distribution.size() ? r.distribution[idx] : 0;
}

// F-67: asagida tanimli; detect_drops'un tum cikis yollarinda kosu duzeyi sinyal eklenir.
void analyze_private_growth(DropReport& r, const std::vector<DropTelemetrySource>& sources,
                            const DropThresholds& th);
// F-63: yalniz drop bulunan kosuda, olculen tepe VRAM adaptor toplamina gore yuksekse baglam ekler.
void analyze_vram_pressure(DropReport& r, const std::vector<DropTelemetrySource>& sources,
                           const DropRunInputs& run, const DropThresholds& th);
// F-61: surucu bildirimli isi/guc limiti ve atfedilemeyen saat dususu; yalniz drop varken.
void analyze_gpu_limits(DropReport& r, const DropRunInputs& run, const DropThresholds& th);

DropReport detect_drops(const DropInput& in, const DropThresholds& th) {
    DropReport r;
    r.run_id = in.run.run_id;

    std::vector<DropTelemetrySource> aligned_sources;
    aligned_sources.reserve(in.sources.size());
    for (const auto& s : in.sources) {
        if (aligned(s.info, in.run.frame_domain)) {
            aligned_sources.push_back(s);
        } else {
            add_unique(r.unaligned_sources, s.info.id);
        }
    }

    // F-67: drop penceresinden bagimsiz kosu duzeyi bellek buyumesi (erken cikislar dahil).
    analyze_private_growth(r, aligned_sources, th);

    // 1) Gecersiz kareleri suz, zaman eksenine gore sirala. Kare basi GPU suresi (varsa) kareyle birlikte tasinir.
    const bool busy_given = !in.frame_gpu_busy_ms.empty() && in.frame_gpu_busy_ms.size() == in.frames.size();
    std::vector<std::size_t> order;
    order.reserve(in.frames.size());
    for (std::size_t i = 0; i < in.frames.size(); ++i) {
        const auto& f = in.frames[i];
        if (!std::isfinite(f.t_ms) || !std::isfinite(f.interval_ms) || !(f.interval_ms > 0)) {
            ++r.excluded_frames;
            continue;
        }
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&in](std::size_t a, std::size_t b) { return in.frames[a].t_ms < in.frames[b].t_ms; });

    // 2) Kapsam disi (menu/yukleme/isima) kareleri cikar.
    std::vector<FramePoint> measured;
    std::vector<double> measured_busy;  // measured ile hizali; bos = olculmedi
    measured.reserve(order.size());
    if (busy_given) measured_busy.reserve(order.size());
    for (const std::size_t i : order) {
        const auto& f = in.frames[i];
        if (scope_at(f.t_ms, in.run.scopes) == WindowScope::Measured) {
            measured.push_back(f);
            if (busy_given) measured_busy.push_back(in.frame_gpu_busy_ms[i]);
        } else {
            ++r.scope_excluded_frames;
        }
    }
    r.valid_frames = measured.size();

    if (measured.size() < th.min_frames) {
        r.outcome = DropOutcome::Insufficient;
        add_unique(r.notes, std::string(kEvFramesInsufficient));
        add_common_notes(r);
        return r;
    }

    // 3) Taban: medyan ve p95; esik = max(spike_factor * medyan, p95).
    std::vector<double> intervals;
    intervals.reserve(measured.size());
    double measured_ms = 0;
    for (const auto& f : measured) {
        intervals.push_back(f.interval_ms);
        measured_ms += f.interval_ms;
    }
    r.measured_ms = measured_ms;
    std::vector<double> sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    r.median_interval_ms = median_of_sorted(sorted);
    r.p95_interval_ms = nearest_rank(sorted, 95.0);
    double spike_factor = th.spike_factor;
    if (!std::isfinite(spike_factor) || spike_factor <= 0) spike_factor = 2.0;
    r.threshold_ms = std::max(spike_factor * r.median_interval_ms, r.p95_interval_ms);

    std::vector<std::size_t> slow;
    for (std::size_t i = 0; i < intervals.size(); ++i)
        if (intervals[i] > r.threshold_ms) slow.push_back(i);
    if (slow.empty()) {
        r.outcome = DropOutcome::NoDrops;
        add_unique(r.notes, std::string(kEvNoDropWindows));
        add_common_notes(r);
        return r;
    }

    // 4) Yavas kareleri, kucuk bosluklari birlesitirerek pencerelere topla.
    const std::size_t max_windows = std::max<std::size_t>(th.max_windows, 1);
    const std::size_t worst_limit = th.worst_windows;
    const double median_ms = r.median_interval_ms;

    std::vector<Candidate> analyzed_candidates;
    std::vector<Candidate> worst_candidates;
    Candidate aggregate;
    bool have_aggregate = false;
    std::size_t aggregated_windows = 0;
    double drop_ms = 0;

    auto emit = [&](std::size_t first, std::size_t last, std::size_t slow_count) {
        const Candidate c = make_candidate(measured, first, last, slow_count, median_ms);
        const bool long_enough = c.duration_ms >= th.min_window_ms;
        const bool enough_slow = c.slow_count >= th.min_slow_frames;
        if (!long_enough && !enough_slow) return;
        ++r.total_windows;
        if (std::isfinite(c.duration_ms)) drop_ms += c.duration_ms;
        consider_worst(worst_candidates, c, worst_limit);
        if (analyzed_candidates.size() < max_windows) {
            analyzed_candidates.push_back(c);
            return;
        }
        ++aggregated_windows;
        if (!have_aggregate) {
            aggregate = c;
            have_aggregate = true;
            return;
        }
        aggregate.frame_count = saturating_add(aggregate.frame_count, c.frame_count);
        aggregate.slow_count = saturating_add(aggregate.slow_count, c.slow_count);
        aggregate.lost = saturating_add(aggregate.lost, c.lost);
        aggregate.duration_ms += c.duration_ms;
        if (c.worst_ms > aggregate.worst_ms) aggregate.worst_ms = c.worst_ms;
        if (c.start_ms < aggregate.start_ms) aggregate.start_ms = c.start_ms;
        if (c.last > aggregate.last) aggregate.last = c.last;
        aggregate.end_ms = aggregate.start_ms + aggregate.duration_ms;
        aggregate.avg_fps = safe_avg_fps(aggregate.frame_count, aggregate.duration_ms);
    };

    std::size_t group_first = slow[0];
    std::size_t group_last = slow[0];
    std::size_t group_slow = 1;
    for (std::size_t k = 1; k < slow.size(); ++k) {
        const std::size_t j = slow[k];
        const std::size_t gap = j - group_last - 1;
        if (gap <= th.merge_gap_frames) {
            group_last = j;
            ++group_slow;
        } else {
            emit(group_first, group_last, group_slow);
            group_first = j;
            group_last = j;
            group_slow = 1;
        }
    }
    emit(group_first, group_last, group_slow);

    if (r.total_windows == 0) {
        r.outcome = DropOutcome::NoDrops;
        add_unique(r.notes, std::string(kEvNoDropWindows));
        add_unique(r.notes, std::string(kEvWindowsBelowMinimum));
        add_common_notes(r);
        return r;
    }

    r.outcome = DropOutcome::DropsFound;
    r.analyzed_windows = analyzed_candidates.size();
    r.aggregated_windows = aggregated_windows;
    r.windows_truncated = aggregated_windows > 0;
    r.drop_ms = drop_ms;
    if (r.measured_ms > 0 && std::isfinite(r.measured_ms)) {
        const double ratio = drop_ms / r.measured_ms;
        if (std::isfinite(ratio)) {
            r.drop_time_ratio = std::clamp(ratio, 0.0, 1.0);
        } else if (drop_ms > 0) {
            r.drop_time_ratio = 1.0;
        }
    }

    if (have_aggregate) {
        aggregate.avg_fps = safe_avg_fps(aggregate.frame_count, aggregate.duration_ms);
        DropWindow aw = to_window(aggregate);
        aw.source = DropSource::Insufficient;
        aw.confidence = DropConfidence::Insufficient;
        add_unique(aw.notes, std::string(kEvWindowLimitAggregated));
        add_unique(aw.counter, std::string(kEvWindowLimitAggregated));
        r.truncated_aggregate = std::move(aw);
    }

    // 5) Sinir icindeki pencereleri siniflandir.
    std::vector<DropWindow> classified;
    classified.reserve(analyzed_candidates.size());
    const std::vector<std::string> metric_names = all_metric_names(in.metric_names);
    for (const auto& c : analyzed_candidates) {
        DropWindow w = to_window(c);
        classify_drop_window(w, measured, measured_busy, aligned_sources, metric_names, in.run, th);
        classified.push_back(std::move(w));
    }
    mark_pipeline_compile_suspected(classified, th);  // F-66

    std::array<std::size_t, kDropSourceCount> distribution{};
    for (const auto& w : classified) ++distribution[index_of(w.source)];
    r.distribution = distribution;

    // 6) En kotu pencereler (sinir disinda kalanlar siniflandirilmadi olarak isaretli).
    r.worst.reserve(worst_candidates.size());
    for (const auto& c : worst_candidates) {
        DropWindow w;
        bool found = false;
        for (const auto& cw : classified) {
            if (cw.first_frame == c.first && cw.last_frame == c.last) {
                w = cw;
                found = true;
                break;
            }
        }
        if (!found) {
            w = to_window(c);
            w.source = DropSource::Insufficient;
            w.confidence = DropConfidence::Insufficient;
            add_unique(w.counter, std::string(kEvWindowLimitNotClassified));
            add_unique(w.notes, std::string(kEvWindowLimitNotClassified));
        }
        r.worst.push_back(std::move(w));
    }

    // 7) Baskin kaynak + guven (siniflandirilan pencereler uzerinden).
    std::size_t classified_count = 0;
    for (const DropSource s : {DropSource::GpuBound, DropSource::CpuOrEngineBound,
                               DropSource::FrameCapOrPresentWait, DropSource::BackgroundOrIo,
                               DropSource::MemoryPressure, DropSource::PipelineCompileSuspected})
        classified_count += distribution[index_of(s)];

    if (classified_count == 0) {
        r.dominant = DropSource::Insufficient;
        r.confidence = DropConfidence::Insufficient;
        add_unique(r.notes, std::string(kEvNoClassifiedDrops));
    } else {
        std::size_t best_count = 0;
        DropSource best = DropSource::Insufficient;
        bool tie = false;
        for (const DropSource s : {DropSource::GpuBound, DropSource::CpuOrEngineBound,
                                   DropSource::FrameCapOrPresentWait, DropSource::BackgroundOrIo,
                                   DropSource::MemoryPressure, DropSource::PipelineCompileSuspected}) {
            const std::size_t c = distribution[index_of(s)];
            if (c > best_count) {
                best_count = c;
                best = s;
                tie = false;
            } else if (c == best_count && c > 0) {
                tie = true;
            }
        }
        const double share =
            static_cast<double>(best_count) / static_cast<double>(classified_count);
        if (tie || share < 0.6) {
            r.dominant = DropSource::Insufficient;
            r.confidence = DropConfidence::Insufficient;
            add_unique(r.notes, std::string(kEvWindowClassesSplit));
        } else {
            r.dominant = best;
            std::size_t corroborated_windows = 0;
            for (const auto& w : classified) {
                if (w.source != best) continue;
                merge_codes(r.supporting, w.supporting);
                merge_codes(r.counter, w.counter);
                merge_codes(r.notes, w.notes);
                if (w.confidence == DropConfidence::Corroborated) ++corroborated_windows;
            }
            r.confidence = (r.supporting.size() >= 2 && corroborated_windows > 0)
                               ? DropConfidence::Corroborated
                               : DropConfidence::SingleSignal;
        }
    }

    if (r.dominant.has_value() && *r.dominant == DropSource::Insufficient) {
        for (const auto& w : classified) {
            if (w.source != DropSource::Insufficient) continue;
            merge_codes(r.supporting, w.supporting);
            merge_codes(r.counter, w.counter);
            merge_codes(r.notes, w.notes);
        }
    }

    // F-63: drop penceresi varken olculen VRAM basinci kosu baglami olarak eklenir (teshis degil).
    analyze_vram_pressure(r, aligned_sources, in.run, th);
    // F-61: drop varken surucu limit bayraklari + yuk alti saat dususu (salt okuma, ariza iddiasi yok).
    analyze_gpu_limits(r, in.run, th);
    if (in.run.gpu_vendor_limit_api_missing && r.outcome == DropOutcome::DropsFound) add_unique(r.notes, std::string(kEvGpuVendorApiMissing));

    add_common_notes(r);
    return r;
}

// F-67: kosu duzeyi ozel bellek buyumesi. Yalniz gercek ornekler kullanilir; eksik ornek uydurulmaz.
// Ilk ve son uc ornegin medyani karsilastirilir; hem mutlak hem oransal esik birlikte gecmelidir.
// Kosul saglanirsa kosu destegine "game_private_growth", notlarina yeniden baslatma onerisi eklenir.
void analyze_private_growth(DropReport& r, const std::vector<DropTelemetrySource>& sources,
                            const DropThresholds& th) {
    std::vector<std::pair<double, double>> pts;  // (t_ms, MiB)
    for (const auto& s : sources) {
        for (const auto& m : s.samples) {
            if (m.metric != kMetricGamePrivateMib || !m.value) continue;
            const double v = *m.value;
            if (!std::isfinite(v) || v <= 0 || !std::isfinite(m.t_ms)) continue;
            pts.emplace_back(m.t_ms, v);
        }
    }
    const std::size_t min_n = std::max<std::size_t>(th.private_growth_samples, 6);
    if (pts.size() < min_n) return;
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const double span = pts.back().first - pts.front().first;
    if (!std::isfinite(span) || span < th.private_growth_min_span_ms) return;

    std::vector<double> head, tail;
    for (std::size_t i = 0; i < 3; ++i) head.push_back(pts[i].second);
    for (std::size_t i = pts.size() - 3; i < pts.size(); ++i) tail.push_back(pts[i].second);
    std::sort(head.begin(), head.end());
    std::sort(tail.begin(), tail.end());
    const double first = median_of_sorted(head);
    const double last = median_of_sorted(tail);
    if (!(first > 0) || !std::isfinite(first) || !std::isfinite(last)) return;

    const double growth = last - first;
    const double min_mib = std::max(th.private_growth_min_mib, 0.0);
    const double ratio = std::max(th.private_growth_ratio, 0.0);
    if (!(growth >= min_mib) || !(growth / first >= ratio)) return;

    add_unique(r.supporting, std::string(kEvGamePrivateGrowth));
    add_unique(r.notes, std::string(kEvGameRestartAdvice));
}

// F-63: kosu duzeyi VRAM basinci. Yalniz drop penceresi bulunan kosuda cagrilir; adaptor toplam VRAM'i
// bilinmiyorsa veya paylasimli/yazilim adaptor kadar kucukse sinyal uretilmez. Olcum "oyunun tepe ayrik
// VRAM kullanimi / adaptorun toplam ayrik VRAM'i" oranidir; isletim sisteminin surec basina butcesi
// degildir ve bu yuzden metin kesin baski iddiasinda bulunmaz.
void analyze_vram_pressure(DropReport& r, const std::vector<DropTelemetrySource>& sources,
                           const DropRunInputs& run, const DropThresholds& th) {
    if (!run.adapter_dedicated_vram_mib.has_value()) return;
    const double total = *run.adapter_dedicated_vram_mib;
    if (!std::isfinite(total) || total < th.vram_pressure_min_adapter_mib) return;

    const std::size_t min_n = std::max<std::size_t>(th.vram_pressure_min_samples, 1);
    std::size_t n = 0;
    double peak = 0;
    for (const auto& s : sources) {
        for (const auto& m : s.samples) {
            if (m.metric != kMetricGameVramMib || !m.value) continue;
            const double v = *m.value;
            if (!std::isfinite(v) || v <= 0) continue;
            peak = std::max(peak, v);
            ++n;
        }
    }
    if (n < min_n) return;
    const double ratio = std::clamp(th.vram_pressure_ratio, 0.0, 1.0);
    if (peak < total * ratio) return;

    add_unique(r.supporting, std::string(kEvGameVramNearCapacity));
    add_unique(r.notes, std::string(kEvGameVramAdvice));
}

// F-61: surucu limit bayraklari + yuk alti saat. Yalniz drop bulunan kosuda cagrilir. Isi/guc sinifi
// `evaluate_limits` esiklerine (yuk >= %60, bayrak yuk pencerelerinin >= %25'i, saat referansin
// <%90'i) gore belirlenir. Referans, ayni kosuda YUK ALTINDA gozlenen en yuksek saattir; surucu boost
// degeri uydurulmaz. En az `gpu_limit_min_windows` yuk penceresi yoksa hicbir kanit uretilmez.
// Isi/guc bayragi donanim arizasi iddiasi degildir; metinler bunu acikca soyler.
void analyze_gpu_limits(DropReport& r, const DropRunInputs& run, const DropThresholds& th) {
    if (run.gpu_limit_windows.empty()) return;
    std::vector<LimitSample> windows;
    windows.reserve(run.gpu_limit_windows.size());
    std::size_t loaded = 0;
    std::optional<double> reference_clock_mhz;
    for (const auto& w : run.gpu_limit_windows) {
        if (!std::isfinite(w.t_ms)) continue;
        // Menu/yukleme/isima penceresi limit teshisine katilmaz; yalniz olculen kapsam.
        if (scope_at(w.t_ms, run.scopes) != WindowScope::Measured) continue;
        LimitSample s;
        if (w.perf_decrease_mask) s.caps = perf_caps_from_mask(*w.perf_decrease_mask);
        if (w.load_pct && std::isfinite(*w.load_pct)) s.load_pct = *w.load_pct;
        if (w.clock_mhz && std::isfinite(*w.clock_mhz) && *w.clock_mhz > 0.0) s.clock_mhz = *w.clock_mhz;
        if (s.load_pct && *s.load_pct >= kGpuLoadPct) {
            ++loaded;
            if (s.clock_mhz && (!reference_clock_mhz || *s.clock_mhz > *reference_clock_mhz))
                reference_clock_mhz = *s.clock_mhz;
        }
        windows.push_back(std::move(s));
    }
    // Tek bir okuma (veya cok az) ile kosu duzeyi teshis uretilmez; en az 2 pencere savunma siniri.
    const std::size_t min_windows = std::max<std::size_t>(th.gpu_limit_min_windows, 2);
    if (loaded < min_windows) return;

    const PlatformFinding f = evaluate_limits(windows, reference_clock_mhz);
    bool thermal = false, power = false, clock_drop = false;
    for (const auto e : f.supporting) {
        if (e == PlatformEvidence::PerfCapThermal) thermal = true;
        else if (e == PlatformEvidence::PerfCapPower) power = true;
        else if (e == PlatformEvidence::ClockDropUnderLoad) clock_drop = true;
    }
    if (thermal) {
        add_unique(r.supporting, std::string(kEvGpuThermalLimit));
        add_unique(r.notes, std::string(kEvGpuThermalAdvice));
    }
    if (power) {
        add_unique(r.supporting, std::string(kEvGpuPowerLimit));
        add_unique(r.notes, std::string(kEvGpuPowerAdvice));
    }
    if (clock_drop && !thermal && !power) {
        // Saat dustu ama surucu isi/guc limiti bildirmedi: neden atfedilmez.
        add_unique(r.supporting, std::string(kEvGpuClockDropUnattributed));
        add_unique(r.notes, std::string(kEvGpuLimitUnattributedAdvice));
    }
}

}  // namespace ef::core::diagnosis
