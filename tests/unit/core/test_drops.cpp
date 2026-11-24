// DIAG-08: oyun ici kare dususu tespiti ve kanita dayali kaynak siniflandirmasi.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/diagnosis/drops.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::measure;

namespace {

std::vector<FramePoint> frames_from_intervals(const std::vector<double>& intervals) {
    std::vector<FramePoint> out;
    out.reserve(intervals.size());
    double t = 0;
    for (double d : intervals) {
        t += d;
        out.push_back({t, d});
    }
    return out;
}

std::vector<double> constant_intervals(std::size_t n, double d) { return std::vector<double>(n, d); }

std::vector<TelemetrySample> samples(std::string metric, double value, double until_ms,
                                     double interval_ms = 20.0) {
    std::vector<TelemetrySample> out;
    for (double t = interval_ms; t <= until_ms; t += interval_ms)
        out.push_back({metric, MetricSource::Pdh, t, interval_ms, value, {}});
    return out;
}

DropTelemetrySource source(std::string id, ClockDomain domain, std::string metric, double value,
                           double until_ms) {
    DropTelemetrySource s;
    s.info = SourceInfo{std::move(id), domain, 0.0, false};
    s.samples = samples(std::move(metric), value, until_ms);
    return s;
}

DropRunInputs no_cap_144() {
    DropRunInputs run;
    run.frame_domain = ClockDomain::Qpc;
    run.compute.display_refresh_hz = 144.0;
    run.compute.vsync = PresentSync::VsyncOff;
    run.compute.fps_cap_known_none = true;
    return run;
}

bool has(const std::vector<std::string>& v, std::string_view code) {
    for (const auto& s : v)
        if (s == code) return true;
    return false;
}

bool dominant_is(const DropReport& r, DropSource s) { return r.dominant.has_value() && *r.dominant == s; }

bool confidence_is(const DropReport& r, DropConfidence c) {
    return r.confidence.has_value() && *r.confidence == c;
}

}  // namespace

EF_TEST(drops_constant_frame_rate_has_no_drops) {
    DropInput in;
    in.run.run_id = "run-a";
    in.frames = frames_from_intervals(constant_intervals(300, 16.7));

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::NoDrops);
    EF_CHECK(r.total_windows == 0 && r.worst.empty());
    EF_CHECK(!r.dominant.has_value() && !r.confidence.has_value());
    EF_CHECK(has(r.notes, "no_drop_windows"));
    EF_CHECK(r.valid_frames == 300);
    EF_CHECK(std::fabs(r.median_interval_ms - 16.7) < 1e-9);
    EF_CHECK(r.threshold_ms > 33.0 && r.threshold_ms < 34.0);  // max(2*16.7, 16.7)
    EF_CHECK(r.drop_time_ratio == 0.0);
    EF_CHECK(r.distribution[static_cast<std::size_t>(DropSource::GpuBound)] == 0);
}

EF_TEST(drops_cpu_spikes_classified_cpu_or_engine) {
    std::vector<double> intervals = constant_intervals(240, 16.7);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) intervals[i] = 60.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-b";
    in.frames = frames_from_intervals(intervals);
    const double until = in.frames.back().t_ms + 20.0;
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "cpu_max_core_pct", 97.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_engine_3d_pct", 40.0, until));

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound);
    EF_CHECK(r.total_windows == 4 && r.analyzed_windows == 4);
    EF_CHECK(dominant_is(r, DropSource::CpuOrEngineBound));
    EF_CHECK(confidence_is(r, DropConfidence::Corroborated));
    EF_CHECK(source_count(r, DropSource::CpuOrEngineBound) == 4);
    EF_CHECK(has(r.supporting, "cpu_core_saturated"));
    EF_CHECK(has(r.supporting, "gpu_util_low"));
    EF_CHECK(r.worst.size() == 4);
    EF_CHECK(r.worst[0].worst_ms >= 59.9);
    EF_CHECK(r.worst[0].estimated_lost_frames > 0);
    EF_CHECK(r.drop_time_ratio > 0.0 && r.drop_time_ratio < 0.1);
}

EF_TEST(drops_gpu_high_long_frame_classified_gpu) {
    std::vector<double> intervals = constant_intervals(200, 16.7);
    intervals[150] = 100.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-c";
    in.frames = frames_from_intervals(intervals);
    const double until = in.frames.back().t_ms + 20.0;
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_engine_3d_pct", 98.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "cpu_max_core_pct", 55.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_busy_ms", 95.0, until));

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound && r.total_windows == 1);
    EF_CHECK(dominant_is(r, DropSource::GpuBound));
    EF_CHECK(confidence_is(r, DropConfidence::Corroborated));
    EF_CHECK(has(r.supporting, "gpu_util_high"));
    EF_CHECK(has(r.supporting, "gpu_busy_near_frame_time"));
    EF_CHECK(r.worst[0].worst_ms >= 99.9);
}

EF_TEST(drops_without_aligned_metrics_is_insufficient) {
    std::vector<double> intervals = constant_intervals(100, 16.7);
    intervals[60] = 60.0;
    DropInput in;
    in.run.run_id = "run-d";
    in.frames = frames_from_intervals(intervals);

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound && r.total_windows == 1);
    EF_CHECK(dominant_is(r, DropSource::Insufficient));
    EF_CHECK(confidence_is(r, DropConfidence::Insufficient));
    EF_CHECK(has(r.counter, "no_aligned_metrics"));
    EF_CHECK(has(r.notes, "no_classified_drops"));
}

EF_TEST(drops_context_only_metrics_are_not_root_cause) {
    std::vector<double> intervals = constant_intervals(100, 16.7);
    intervals[60] = 60.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-d-coarse";
    in.frames = frames_from_intervals(intervals);
    const double until = in.frames.back().t_ms + 1000.0;
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "cpu_max_core_pct", 99.0, until));  // 1 Hz
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_engine_3d_pct", 40.0, until));
    for (auto& s : in.sources[0].samples) s.interval_ms = 1000.0;
    for (auto& s : in.sources[1].samples) s.interval_ms = 1000.0;

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound);
    EF_CHECK(dominant_is(r, DropSource::Insufficient));
    EF_CHECK(has(r.counter, "context_only:cpu_max_core_pct"));
    EF_CHECK(has(r.counter, "metrics_missing"));
}

EF_TEST(drops_unaligned_source_ignored_as_root_cause) {
    std::vector<double> intervals = constant_intervals(120, 16.7);
    intervals[80] = 60.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-e";
    in.frames = frames_from_intervals(intervals);
    const double until = in.frames.back().t_ms + 20.0;

    DropTelemetrySource vendor;
    vendor.info = SourceInfo{"vendor", ClockDomain::SteadyClock, 0.0, false};
    vendor.samples = samples("gpu_engine_3d_pct", 99.0, until);  // hizasiz: kullanilmamali
    const auto vendor_cpu = samples("cpu_max_core_pct", 40.0, until);
    vendor.samples.insert(vendor.samples.end(), vendor_cpu.begin(), vendor_cpu.end());
    in.sources.push_back(std::move(vendor));

    const DropReport only_unaligned = detect_drops(in);
    EF_CHECK(only_unaligned.outcome == DropOutcome::DropsFound);
    EF_CHECK(dominant_is(only_unaligned, DropSource::Insufficient));
    EF_CHECK(has(only_unaligned.counter, "no_aligned_metrics"));
    EF_CHECK(only_unaligned.unaligned_sources.size() == 1);
    EF_CHECK(only_unaligned.unaligned_sources[0] == "vendor");
    EF_CHECK(has(only_unaligned.notes, "unaligned_sources_ignored"));

    // Hizali CPU yuksek / GPU bos: hizasiz kaynak GPU yuksek dese de celiski uretmemeli.
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "cpu_max_core_pct", 97.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_engine_3d_pct", 40.0, until));
    const DropReport aligned = detect_drops(in);
    EF_CHECK(dominant_is(aligned, DropSource::CpuOrEngineBound));
    EF_CHECK(!has(aligned.counter, "conflicting_sources"));
    EF_CHECK(aligned.unaligned_sources.size() == 1 && aligned.unaligned_sources[0] == "vendor");
}

EF_TEST(drops_refresh_locked_intervals_classified_frame_cap) {
    std::vector<double> intervals = constant_intervals(200, 3.0);
    for (std::size_t i = 100; i < 110; ++i) intervals[i] = 1000.0 / 144.0;
    DropInput in;
    in.run.run_id = "run-f";
    in.run.frame_domain = ClockDomain::Qpc;
    in.run.compute.display_refresh_hz = 144.0;
    in.run.compute.vsync = PresentSync::VsyncOn;
    in.frames = frames_from_intervals(intervals);

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound && r.total_windows == 1);
    EF_CHECK(dominant_is(r, DropSource::FrameCapOrPresentWait));
    EF_CHECK(confidence_is(r, DropConfidence::Corroborated));
    EF_CHECK(has(r.supporting, "refresh_interval_match"));
    EF_CHECK(has(r.supporting, "vsync_on"));

    // VSync bilinmiyor: yenilemeye sabitlenme zayif kanit (SingleSignal).
    in.run.compute.vsync = PresentSync::Unknown;
    const DropReport inferred = detect_drops(in);
    EF_CHECK(dominant_is(inferred, DropSource::FrameCapOrPresentWait));
    EF_CHECK(confidence_is(inferred, DropConfidence::SingleSignal));
    EF_CHECK(has(inferred.supporting, "cap_inferred_from_refresh"));
}

EF_TEST(drops_empty_and_too_few_frames_are_insufficient) {
    DropInput empty;
    DropReport r = detect_drops(empty);
    EF_CHECK(r.outcome == DropOutcome::Insufficient && r.total_windows == 0);
    EF_CHECK(has(r.notes, "frames_insufficient"));

    DropInput few;
    few.frames = frames_from_intervals(constant_intervals(4, 16.7));
    r = detect_drops(few);
    EF_CHECK(r.outcome == DropOutcome::Insufficient && r.valid_frames == 4);

    DropInput bad;
    bad.frames = {{0.0, std::numeric_limits<double>::quiet_NaN()},
                  {1.0, -5.0},
                  {std::numeric_limits<double>::infinity(), 16.0},
                  {3.0, 0.0}};
    r = detect_drops(bad);
    EF_CHECK(r.outcome == DropOutcome::Insufficient);
    EF_CHECK(r.excluded_frames == 4);

    DropInput tiny;
    tiny.frames = frames_from_intervals({16.7, 16.7, 16.7});
    DropTelemetrySource s;
    s.info = SourceInfo{"pdh", ClockDomain::Qpc, 0.0, true};
    s.samples.push_back({"cpu_max_core_pct", MetricSource::Pdh, 10.0, 1.0,
                         std::numeric_limits<double>::quiet_NaN(), {}});
    tiny.sources.push_back(std::move(s));
    r = detect_drops(tiny);
    EF_CHECK(r.outcome == DropOutcome::Insufficient);
}

EF_TEST(drops_window_merge_gap_and_min_window_boundary) {
    // Tek normal kare boslugu birlesir -> tek pencere.
    std::vector<double> merged = constant_intervals(100, 16.7);
    merged[40] = 60.0;
    merged[42] = 60.0;
    DropInput a;
    a.run.run_id = "run-h1";
    a.frames = frames_from_intervals(merged);
    DropReport r = detect_drops(a);
    EF_CHECK(r.total_windows == 1 && r.worst.size() == 1);
    EF_CHECK(r.worst[0].slow_frame_count == 2);
    EF_CHECK(r.worst[0].frame_count == 3);  // 40, 41, 42
    EF_CHECK(r.worst[0].worst_ms >= 59.9);

    // Iki normal kare boslugu birlesmez -> iki pencere.
    std::vector<double> split = constant_intervals(100, 16.7);
    split[40] = 60.0;
    split[43] = 60.0;
    DropInput b;
    b.run.run_id = "run-h2";
    b.frames = frames_from_intervals(split);
    r = detect_drops(b);
    EF_CHECK(r.total_windows == 2);

    // Tek 45 ms yavas kare: <50 ms ve tek kare -> drop penceresi degil.
    std::vector<double> below = constant_intervals(100, 16.7);
    below[50] = 45.0;
    DropInput c;
    c.run.run_id = "run-h3";
    c.frames = frames_from_intervals(below);
    r = detect_drops(c);
    EF_CHECK(r.outcome == DropOutcome::NoDrops);
    EF_CHECK(has(r.notes, "windows_below_minimum"));
}

EF_TEST(drops_background_and_memory_sources) {
    std::vector<double> intervals = constant_intervals(100, 16.7);
    intervals[60] = 60.0;

    DropInput bg;
    bg.run = no_cap_144();
    bg.run.run_id = "run-bg";
    bg.frames = frames_from_intervals(intervals);
    const double until = bg.frames.back().t_ms + 20.0;
    bg.sources.push_back(source("pdh", ClockDomain::Qpc, "disk_queue_len", 4.0, until));
    bg.sources.push_back(source("pdh", ClockDomain::Qpc, "disk_io_ms", 80.0, until));
    bg.sources.push_back(source("pdh", ClockDomain::Qpc, "other_process_cpu_pct", 30.0, until));
    DropReport r = detect_drops(bg);
    EF_CHECK(dominant_is(r, DropSource::BackgroundOrIo));
    EF_CHECK(confidence_is(r, DropConfidence::Corroborated));
    EF_CHECK(has(r.supporting, "disk_queue_high"));
    EF_CHECK(has(r.supporting, "disk_io_latency_high"));
    EF_CHECK(has(r.supporting, "other_process_cpu_high"));

    DropInput mem;
    mem.run = no_cap_144();
    mem.run.run_id = "run-mem";
    mem.frames = bg.frames;
    mem.sources.push_back(source("pdh", ClockDomain::Qpc, "commit_used_pct", 97.0, until));
    mem.sources.push_back(source("pdh", ClockDomain::Qpc, "hard_fault_rate", 250.0, until));
    r = detect_drops(mem);
    EF_CHECK(dominant_is(r, DropSource::MemoryPressure));
    EF_CHECK(confidence_is(r, DropConfidence::Corroborated));
    EF_CHECK(has(r.supporting, "commit_pressure_high"));
    EF_CHECK(has(r.supporting, "hard_fault_rate_high"));
}

EF_TEST(drops_repeated_isolated_hitches_classified_pipeline_compile_suspected) {
    // F-66: kaynak sinyali yok, yalitilmis tek karelik ve benzer buyuklukte takilmalar.
    std::vector<double> intervals = constant_intervals(600, 16.7);
    for (const std::size_t i : {100u, 200u, 300u}) intervals[i] = 80.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-shader";
    in.frames = frames_from_intervals(intervals);
    const double until = in.frames.back().t_ms + 20.0;
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_engine_3d_pct", 75.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "cpu_max_core_pct", 50.0, until));

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound && r.total_windows == 3);
    EF_CHECK(dominant_is(r, DropSource::PipelineCompileSuspected));
    EF_CHECK(confidence_is(r, DropConfidence::SingleSignal));
    EF_CHECK(source_count(r, DropSource::PipelineCompileSuspected) == 3);
    EF_CHECK(has(r.supporting, "pipeline_compile_suspected"));
    EF_CHECK(has(r.counter, "no_limiting_signal_in_drop"));
    EF_CHECK(has(r.notes, "pipeline_compile_advice"));
    for (const auto& w : r.worst) EF_CHECK(w.source == DropSource::PipelineCompileSuspected);

    // En kotu kareler birbirinden cok farkliysa desen tekrari sayilmaz; suphe etiketi verilmez.
    std::vector<double> uneven = constant_intervals(600, 16.7);
    uneven[100] = 60.0;
    uneven[200] = 60.0;
    uneven[300] = 200.0;
    in.frames = frames_from_intervals(uneven);
    const DropReport diff = detect_drops(in);
    EF_CHECK(dominant_is(diff, DropSource::Insufficient));
    EF_CHECK(source_count(diff, DropSource::PipelineCompileSuspected) == 0);
    EF_CHECK(!has(diff.supporting, "pipeline_compile_suspected"));

    // Hizali metrik yoksa desen supheye yetmez: durust "yetersiz" kalir.
    DropInput no_metrics;
    no_metrics.run = no_cap_144();
    no_metrics.run.run_id = "run-shader-nometrics";
    no_metrics.frames = frames_from_intervals(intervals);
    const DropReport nm = detect_drops(no_metrics);
    EF_CHECK(dominant_is(nm, DropSource::Insufficient));
    EF_CHECK(has(nm.counter, "no_aligned_metrics"));

    // Uzun (yalitilmamis) pencereler suphe etiketi almaz.
    std::vector<double> long_windows = constant_intervals(600, 16.7);
    for (const std::size_t i : {100u, 101u, 102u, 300u, 301u, 302u}) long_windows[i] = 80.0;
    in.frames = frames_from_intervals(long_windows);
    const DropReport lw = detect_drops(in);
    EF_CHECK(dominant_is(lw, DropSource::Insufficient));
    EF_CHECK(source_count(lw, DropSource::PipelineCompileSuspected) == 0);
}

EF_TEST(drops_conflicting_signals_are_insufficient) {
    std::vector<double> intervals = constant_intervals(200, 16.7);
    intervals[150] = 100.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-conflict";
    in.frames = frames_from_intervals(intervals);
    const double until = in.frames.back().t_ms + 20.0;
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_engine_3d_pct", 98.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "cpu_max_core_pct", 55.0, until));
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "disk_queue_len", 5.0, until));

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound && r.total_windows == 1);
    EF_CHECK(dominant_is(r, DropSource::Insufficient));
    EF_CHECK(confidence_is(r, DropConfidence::Insufficient));
    EF_CHECK(has(r.counter, "conflicting_sources"));
}

EF_TEST(drops_window_limit_is_reported_not_silently_truncated) {
    std::vector<double> intervals;
    intervals.reserve(21021);
    for (std::size_t i = 0; i < 1001; ++i) {
        intervals.push_back(60.0);
        for (std::size_t k = 0; k < 20; ++k) intervals.push_back(16.7);
    }
    DropInput in;
    in.run.run_id = "run-limit";
    in.frames = frames_from_intervals(intervals);

    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound);
    EF_CHECK(r.total_windows == 1001);
    EF_CHECK(r.analyzed_windows == 1000);
    EF_CHECK(r.aggregated_windows == 1);
    EF_CHECK(r.windows_truncated);
    EF_CHECK(r.truncated_aggregate.has_value());
    EF_CHECK(r.truncated_aggregate->source == DropSource::Insufficient);
    EF_CHECK(has(r.truncated_aggregate->notes, "windows_aggregated"));
    EF_CHECK(has(r.notes, "windows_aggregated"));
}

EF_TEST(drops_report_text_keys_exist_and_unknown_degrades) {
    // UI katalog anahtarlari: kaynak/guven/sonuc ve yaygin kanit kodlari iki dilde de bulunmali.
    for (auto s : {DropSource::NoDrops, DropSource::GpuBound, DropSource::CpuOrEngineBound,
                   DropSource::FrameCapOrPresentWait, DropSource::BackgroundOrIo, DropSource::MemoryPressure,
                   DropSource::PipelineCompileSuspected, DropSource::Insufficient})
        EF_CHECK(ef::core::i18n::id_of(drop_source_key(s)).has_value());
    for (auto c : {DropConfidence::Corroborated, DropConfidence::SingleSignal, DropConfidence::Insufficient})
        EF_CHECK(ef::core::i18n::id_of(drop_confidence_key(c)).has_value());
    for (auto o : {DropOutcome::Insufficient, DropOutcome::NoDrops, DropOutcome::DropsFound})
        EF_CHECK(ef::core::i18n::id_of(drop_outcome_key(o)).has_value());

    const char* exact[] = {
        "gpu_util_high", "gpu_util_low", "gpu_busy_near_frame_time", "gpu_busy_short_of_frame_time", "gpu_metric_missing",
        "cpu_core_saturated", "cpu_core_not_saturated", "cpu_metric_missing", "fps_at_cap", "vrr_cap_candidate", "fps_cap_known", "vsync_on",
        "refresh_interval_match", "fps_cap_interval_match", "present_interval_multiple", "cap_inferred_from_refresh",
        "disk_queue_high", "disk_queue_low", "disk_io_latency_high", "disk_io_latency_low", "other_process_cpu_high",
        "other_process_cpu_low", "commit_pressure_high", "commit_pressure_low", "hard_fault_rate_high",
        "hard_fault_rate_low", "no_aligned_metrics", "metrics_missing", "conflicting_sources",
        "no_limiting_signal_in_drop", "pipeline_compile_suspected", "pipeline_compile_advice",
        "no_drop_windows", "windows_below_minimum", "frames_insufficient",
        "windows_aggregated", "window_limit_not_classified", "unaligned_sources_ignored", "no_classified_drops",
        "window_classes_split", "cap_and_resource_signal", "context_only_metrics",
        "cpu_placement_both", "cpu_placement_p_only", "cpu_placement_e_only",
        "background_overlay", "background_rgb", "background_monitoring", "game_disk_hdd",
        "game_vram_near_capacity", "game_vram_advice", "gpu_thermal_limit", "gpu_power_limit",
        "gpu_clock_drop_unattributed", "gpu_thermal_advice", "gpu_power_advice",
        "gpu_limit_unattributed_advice"};
    for (const char* code : exact) {
        const auto key = drop_evidence_key(code);
        EF_CHECK(ef::core::i18n::id_of(key).has_value());
    }
    for (const char* m : {"gpu_engine_3d_pct", "cpu_max_core_pct", "cpu_main_thread_pct", "disk_queue_len",
                          "disk_io_ms", "commit_used_pct", "hard_fault_rate"}) {
        const auto key = drop_evidence_key(std::string("metric_missing:") + m);
        EF_CHECK(ef::core::i18n::id_of(key).has_value());
    }
    EF_CHECK(drop_evidence_key("metric_missing:unknown") == "drop.metric_missing.other");
    EF_CHECK(drop_evidence_key("context_only:gpu_busy_ms") == "drop.evidence.context_only");
    EF_CHECK(drop_evidence_key("compute_reason:conflicting_signals") == "drop.evidence.compute_reason_other");
    EF_CHECK(drop_evidence_key("totally_unknown_code") == "drop.evidence.unknown");
    EF_CHECK(ef::core::i18n::id_of(drop_evidence_key("totally_unknown_code")).has_value());
    EF_CHECK(ef::core::i18n::id_of("drop.headline.no_drops").has_value());
    EF_CHECK(ef::core::i18n::id_of("drop.headline.drops_found").has_value());
    EF_CHECK(ef::core::i18n::id_of("drop.headline.insufficient").has_value());
    EF_CHECK(ef::core::i18n::id_of("drop.ui.title").has_value());
}

EF_TEST(drops_live_sources_carry_alignment_and_missing_reasons) {
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;
    ef::core::metrics::LiveSnapshot snap;
    snap.gpu_pct = Measurement::of(77.0, Unit::Percent);
    snap.cpu_max_core_pct = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    snap.disk_queue_len = Measurement::of(3.0, Unit::Count);
    snap.disk_io_ms = Measurement::missing(Unit::Milliseconds, MissingReason::NotSupported);
    snap.commit_used_pct = Measurement::of(64.0, Unit::Percent);
    snap.hard_fault_rate = Measurement::of(12.0, Unit::Count);
    snap.other_process_cpu_pct = Measurement::of(18.0, Unit::Percent);
    snap.cpu_main_thread_pct = Measurement::of(93.0, Unit::Percent);  // DIAG-10: hedef oyun ana thread
    snap.game_private_mib = Measurement::of(2048.0, Unit::MiB);       // F-67: ozel bellek
    snap.game_vram_dedicated_mib = Measurement::of(6144.0, Unit::MiB);  // F-63: ayrik VRAM
    const std::vector<LiveDropSample> live{{1000.0, true, snap}, {2000.0, true, snap}};
    const auto srcs = drop_sources_from_live(live);
    EF_CHECK(srcs.size() == 1);
    EF_CHECK(srcs[0].info.id == "pdh");
    EF_CHECK(srcs[0].info.domain == ClockDomain::Qpc && srcs[0].info.offset_ms == 0 && srcs[0].info.offset_verified);
    EF_CHECK(srcs[0].samples.size() == live.size() * 10);  // F-63: +game_vram_mib
    auto find_metric = [&](std::string_view name) -> const TelemetrySample* {
        for (const auto& s : srcs[0].samples)
            if (s.metric == name) return &s;
        return nullptr;
    };
    const auto* gpu = find_metric("gpu_engine_3d_pct");
    const auto* cpu = find_metric("cpu_max_core_pct");
    const auto* disk = find_metric("disk_io_ms");
    EF_CHECK(gpu && gpu->value && *gpu->value == 77.0 && gpu->source == MetricSource::Pdh && gpu->t_ms == 1000.0);
    EF_CHECK(gpu && gpu->interval_ms == 1000.0);  // medyan ornek araligi varsayilmaz, veriden gelir
    EF_CHECK(cpu && !cpu->value && cpu->missing_reason == "not_sampled");
    EF_CHECK(disk && !disk->value && disk->missing_reason == "not_supported");
    EF_CHECK(find_metric("disk_queue_len") && find_metric("disk_queue_len")->value);
    EF_CHECK(find_metric("commit_used_pct") && find_metric("commit_used_pct")->value);
    EF_CHECK(find_metric("hard_fault_rate") && find_metric("hard_fault_rate")->value);
    const auto* other = find_metric("other_process_cpu_pct");
    EF_CHECK(other && other->value && *other->value == 18.0);
    const auto* main_thread = find_metric("cpu_main_thread_pct");
    EF_CHECK(main_thread && main_thread->value && *main_thread->value == 93.0);
    const auto* private_mib = find_metric("game_private_mib");
    EF_CHECK(private_mib && private_mib->value && *private_mib->value == 2048.0);
    const auto* vram_mib = find_metric("game_vram_mib");
    EF_CHECK(vram_mib && vram_mib->value && *vram_mib->value == 6144.0);

    // Tek bir hizasiz ornek kaynagi kok neden olarak kullanilamaz hale getirir; sessizce hizali sayilmaz.
    const std::vector<LiveDropSample> partial{{1000.0, true, snap}, {2000.0, false, snap}};
    const auto bad = drop_sources_from_live(partial);
    EF_CHECK(bad.size() == 1 && bad[0].info.domain == ClockDomain::Unknown && !bad[0].info.offset_verified);
    EF_CHECK(drop_sources_from_live({}).empty());  // ornek yok: kaynak uydurulmaz

    // 20 Hz (50 ms) ornekler: medyan aralik 50 ms; 100 ms pencereye tek ornek dusse bile
    // MEAS-03 (50 <= 100/2) saglanir ve metrik kok neden adayi olabilir.
    const std::vector<LiveDropSample> fast{{0.0, true, snap}, {50.0, true, snap}, {100.0, true, snap}};
    const auto fast_srcs = drop_sources_from_live(fast);
    EF_CHECK(fast_srcs.size() == 1 && fast_srcs[0].samples.size() == fast.size() * 10);
    const auto* fast_gpu = [&]() -> const TelemetrySample* {
        for (const auto& s : fast_srcs[0].samples)
            if (s.metric == "gpu_engine_3d_pct") return &s;
        return nullptr;
    }();
    EF_CHECK(fast_gpu && fast_gpu->interval_ms == 50.0);

    // Tek ornek: aralik bilinemez -> sonsuz (kanit yetersiz, uydurma kazanc yok).
    const std::vector<LiveDropSample> one{{500.0, true, snap}};
    const auto one_srcs = drop_sources_from_live(one);
    EF_CHECK(one_srcs.size() == 1 && std::isinf(one_srcs[0].samples.front().interval_ms));
}

EF_TEST(drops_private_growth_flagged_only_with_measured_sustained_increase) {
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;

    DropInput base;
    base.run.run_id = "run-private";
    base.frames = frames_from_intervals(constant_intervals(600, 16.7));

    auto build_live = [](const std::vector<double>& mibs, double step_ms, bool aligned,
                         bool missing) {
        std::vector<LiveDropSample> out;
        out.reserve(mibs.size());
        for (std::size_t i = 0; i < mibs.size(); ++i) {
            ef::core::metrics::LiveSnapshot snap;
            snap.game_private_mib = missing
                                        ? Measurement::missing(Unit::MiB, MissingReason::PermissionDenied)
                                        : Measurement::of(mibs[i], Unit::MiB);
            out.push_back({step_ms * static_cast<double>(i), aligned, snap});
        }
        return out;
    };

    // 8 ornek / 7 s: ilk uc medyan 1010, son uc medyan 1300 -> +290 MiB (%28,7), iki esik de gecer.
    DropThresholds th;
    th.private_growth_min_span_ms = 1000.0;
    DropInput grow = base;
    grow.sources =
        drop_sources_from_live(build_live({1000, 1010, 1020, 1100, 1180, 1260, 1290, 1300}, 1000.0, true, false));
    const DropReport grow_r = detect_drops(grow, th);
    EF_CHECK(grow_r.outcome == DropOutcome::NoDrops);  // bellek buyumesi drop penceresi uydurmaz
    EF_CHECK(has(grow_r.supporting, "game_private_growth"));
    EF_CHECK(has(grow_r.notes, "game_restart_advice"));
    EF_CHECK(drop_evidence_key("game_private_growth") == "drop.evidence.game_private_growth");
    EF_CHECK(drop_evidence_key("game_restart_advice") == "drop.evidence.game_restart_advice");
    EF_CHECK(ef::core::i18n::id_of(drop_evidence_key("game_private_growth")).has_value());
    EF_CHECK(ef::core::i18n::id_of(drop_evidence_key("game_restart_advice")).has_value());
    EF_CHECK(kPrivateGrowthRules == "private_growth_v1");

    // Oransal esik gecilmez (+%6): isaret yok.
    DropInput slow = base;
    slow.sources =
        drop_sources_from_live(build_live({1000, 1010, 1020, 1030, 1040, 1050, 1055, 1060}, 1000.0, true, false));
    const DropReport slow_r = detect_drops(slow, th);
    EF_CHECK(!has(slow_r.supporting, "game_private_growth"));
    EF_CHECK(!has(slow_r.notes, "game_restart_advice"));

    // Mutlak esik gecilmez (+55 MiB; oran yuksek): isaret yok.
    DropInput tiny = base;
    tiny.sources =
        drop_sources_from_live(build_live({10, 10, 10, 30, 40, 60, 65, 70}, 1000.0, true, false));
    const DropReport tiny_r = detect_drops(tiny, th);
    EF_CHECK(!has(tiny_r.supporting, "game_private_growth"));

    // Varsayilan 120 s sure kosulu: 7 s'lik kosuda isaret yok.
    DropInput shortrun = base;
    shortrun.sources =
        drop_sources_from_live(build_live({1000, 1010, 1020, 1100, 1180, 1260, 1290, 1300}, 1000.0, true, false));
    const DropReport short_r = detect_drops(shortrun);  // varsayilan esikler
    EF_CHECK(!has(short_r.supporting, "game_private_growth"));

    // Eksik ornekler (erisim reddedildi) uydurma artisa cevrilmez.
    DropInput miss = base;
    miss.sources = drop_sources_from_live(build_live(std::vector<double>(8, 0.0), 1000.0, true, true));
    const DropReport miss_r = detect_drops(miss, th);
    EF_CHECK(!has(miss_r.supporting, "game_private_growth"));
    EF_CHECK(!has(miss_r.notes, "game_restart_advice"));

    // Hizasiz kaynak kanit sayilmaz.
    DropInput unaligned = base;
    unaligned.sources =
        drop_sources_from_live(build_live({1000, 1010, 1020, 1100, 1180, 1260, 1290, 1300}, 1000.0, false, false));
    const DropReport unaligned_r = detect_drops(unaligned, th);
    EF_CHECK(!has(unaligned_r.supporting, "game_private_growth"));

    // Yetersiz ornek (5): isaret yok.
    DropInput few = base;
    few.sources =
        drop_sources_from_live(build_live({1000, 1010, 1020, 1290, 1300}, 1000.0, true, false));
    const DropReport few_r = detect_drops(few, th);
    EF_CHECK(!has(few_r.supporting, "game_private_growth"));
}

EF_TEST(drops_vram_pressure_only_with_drops_known_adapter_and_measured_peak) {
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;

    std::vector<double> intervals = constant_intervals(200, 16.7);
    intervals[150] = 100.0;  // en az bir drop penceresi
    DropInput base;
    base.run = no_cap_144();
    base.run.run_id = "run-vram";
    base.frames = frames_from_intervals(intervals);

    auto build_live = [](const std::vector<std::optional<double>>& mibs) {
        std::vector<LiveDropSample> out;
        for (std::size_t i = 0; i < mibs.size(); ++i) {
            ef::core::metrics::LiveSnapshot snap;
            snap.game_vram_dedicated_mib =
                mibs[i] ? Measurement::of(*mibs[i], Unit::MiB)
                        : Measurement::missing(Unit::MiB, MissingReason::NotSupported);
            out.push_back({1000.0 * static_cast<double>(i), true, snap});
        }
        return out;
    };

    DropInput high = base;
    high.run.adapter_dedicated_vram_mib = 8192.0;
    high.sources = drop_sources_from_live(build_live({7000, 7400, 7600, 7500}));  // tepe %92.8
    const DropReport high_r = detect_drops(high);
    EF_CHECK(high_r.outcome == DropOutcome::DropsFound);
    EF_CHECK(has(high_r.supporting, "game_vram_near_capacity"));
    EF_CHECK(has(high_r.notes, "game_vram_advice"));
    EF_CHECK(drop_evidence_key("game_vram_near_capacity") == "drop.evidence.game_vram_near_capacity");
    EF_CHECK(drop_evidence_key("game_vram_advice") == "drop.evidence.game_vram_advice");
    EF_CHECK(ef::core::i18n::id_of("drop.evidence.game_vram_near_capacity").has_value());
    EF_CHECK(ef::core::i18n::id_of("drop.evidence.game_vram_advice").has_value());

    // Adaptor toplami bilinmiyorsa esik uydurulmaz.
    DropInput unknown = high;
    unknown.run.adapter_dedicated_vram_mib.reset();
    EF_CHECK(!has(detect_drops(unknown).supporting, "game_vram_near_capacity"));

    // Paylasimli/yazilim adaptor kadar kucuk toplam guvenilmez.
    DropInput small = high;
    small.run.adapter_dedicated_vram_mib = 256.0;
    EF_CHECK(!has(detect_drops(small).supporting, "game_vram_near_capacity"));

    // Tepe esigin altinda: baglam eklenmez.
    DropInput low = high;
    low.sources = drop_sources_from_live(build_live({3000, 3600, 4000, 4200}));  // %51
    EF_CHECK(!has(detect_drops(low).supporting, "game_vram_near_capacity"));

    // Yetersiz ornek: iki ornek yetmez.
    DropInput few = high;
    few.sources = drop_sources_from_live(build_live({7600, 7600}));
    EF_CHECK(!has(detect_drops(few).supporting, "game_vram_near_capacity"));

    // Eksik degerler uydurma basinca cevrilmez.
    DropInput miss = high;
    miss.sources = drop_sources_from_live(
        build_live({std::nullopt, std::nullopt, std::nullopt, std::nullopt}));
    EF_CHECK(!has(detect_drops(miss).supporting, "game_vram_near_capacity"));

    // Drop yoksa VRAM dolulugu tek basina sinyal uretmez (oyunlar VRAM'i bilerek doldurur).
    DropInput nodrops = high;
    nodrops.frames = frames_from_intervals(constant_intervals(200, 16.7));
    const DropReport nodrops_r = detect_drops(nodrops);
    EF_CHECK(nodrops_r.outcome == DropOutcome::NoDrops);
    EF_CHECK(!has(nodrops_r.supporting, "game_vram_near_capacity"));
}

EF_TEST(drops_gpu_limit_windows_from_live_filters_unaligned_and_empty) {
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;

    ef::core::metrics::LiveSnapshot full;
    full.gpu_vendor_utilization_pct = Measurement::of(97.0, Unit::Percent);
    full.gpu_vendor_clock_mhz = Measurement::of(1875.0, Unit::MHz);
    full.gpu_vendor_perf_decrease_mask = Measurement::of(3.0, Unit::Count);
    ef::core::metrics::LiveSnapshot empty;
    empty.gpu_vendor_perf_decrease_mask =
        Measurement::missing(Unit::Count, MissingReason::NotSupported);

    const std::vector<LiveDropSample> in{
        {100.0, true, full},
        {200.0, false, full},   // hizasiz: kullanilmaz
        {300.0, true, empty},   // limit alani yok: pencere uretmez
        {400.0, true, full},
    };
    const auto windows = gpu_limit_windows_from_live(in);
    EF_CHECK(windows.size() == 2);
    EF_CHECK(windows[0].t_ms == 100.0 && windows[0].perf_decrease_mask == 3u);
    EF_CHECK(windows[0].load_pct == 97.0 && windows[0].clock_mhz == 1875.0);
    EF_CHECK(windows[1].t_ms == 400.0);
    EF_CHECK(gpu_limit_windows_from_live({}).empty());  // uydurma pencere yok
}

EF_TEST(drops_gpu_limits_only_with_drops_under_load_and_enough_windows) {
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;

    std::vector<double> intervals = constant_intervals(240, 16.7);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) intervals[i] = 60.0;
    DropInput base;
    base.run = no_cap_144();
    base.run.run_id = "run-gpu-limit";
    base.frames = frames_from_intervals(intervals);

    // Termal bayrak + yuk altinda saat dususu: ayni yonde.
    DropInput thermal = base;
    for (std::size_t i = 0; i < 6; ++i) {
        thermal.run.gpu_limit_windows.push_back(
            {1000.0 + static_cast<double>(i) * 200.0, 1u, 98.0, i < 3 ? 1800.0 : 1400.0});
    }
    const DropReport thermal_r = detect_drops(thermal);
    EF_CHECK(thermal_r.outcome == DropOutcome::DropsFound);
    EF_CHECK(has(thermal_r.supporting, "gpu_thermal_limit"));
    EF_CHECK(has(thermal_r.notes, "gpu_thermal_advice"));
    EF_CHECK(!has(thermal_r.supporting, "gpu_power_limit"));

    // Guc bayragi; saat bilinmiyor: yalniz surucu bayragi, yine de kanit.
    DropInput power = base;
    for (std::size_t i = 0; i < 5; ++i)
        power.run.gpu_limit_windows.push_back(
            {1000.0 + static_cast<double>(i) * 200.0, 2u, 99.0, std::nullopt});
    const DropReport power_r = detect_drops(power);
    EF_CHECK(has(power_r.supporting, "gpu_power_limit"));
    EF_CHECK(has(power_r.notes, "gpu_power_advice"));
    EF_CHECK(!has(power_r.supporting, "gpu_clock_drop_unattributed"));

    // Bayrak yok ama saat dustu: neden atfedilmez.
    DropInput unattributed = base;
    const double clocks[] = {1800.0, 1800.0, 1800.0, 1400.0, 1400.0, 1400.0, 1400.0};
    for (std::size_t i = 0; i < std::size(clocks); ++i) {
        unattributed.run.gpu_limit_windows.push_back(
            {1000.0 + static_cast<double>(i) * 200.0, 0u, 98.0, clocks[i]});
    }
    const DropReport unattributed_r = detect_drops(unattributed);
    EF_CHECK(has(unattributed_r.supporting, "gpu_clock_drop_unattributed"));
    EF_CHECK(has(unattributed_r.notes, "gpu_limit_unattributed_advice"));
    EF_CHECK(!has(unattributed_r.supporting, "gpu_thermal_limit"));
    EF_CHECK(!has(unattributed_r.supporting, "gpu_power_limit"));

    // Yuk alti pencere sayisi esigin altinda: teshis uretilmez.
    DropInput few = base;
    for (std::size_t i = 0; i < 3; ++i)
        few.run.gpu_limit_windows.push_back(
            {1000.0 + static_cast<double>(i) * 200.0, 1u, 98.0, 1800.0});
    EF_CHECK(!has(detect_drops(few).supporting, "gpu_thermal_limit"));

    // Drop yoksa surucu bayragi tek basina rapora eklenmez.
    DropInput nodrops = base;
    nodrops.frames = frames_from_intervals(constant_intervals(240, 16.7));
    for (std::size_t i = 0; i < 6; ++i)
        nodrops.run.gpu_limit_windows.push_back(
            {1000.0 + static_cast<double>(i) * 200.0, 1u, 98.0, 1800.0});
    const DropReport nodrops_r = detect_drops(nodrops);
    EF_CHECK(nodrops_r.outcome == DropOutcome::NoDrops);
    EF_CHECK(!has(nodrops_r.supporting, "gpu_thermal_limit"));
}

EF_TEST(drops_gpu_limits_exclude_menu_scope_and_unknown_sensor) {
    std::vector<double> intervals = constant_intervals(240, 16.7);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) intervals[i] = 60.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-gpu-limit-scope";
    in.run.scopes.push_back({0.0, 3000.0, WindowScope::MenuOrLoading});
    in.frames = frames_from_intervals(intervals);
    // Menu penceresinde termal bayrak: olculen kapsam degil, kullanilmaz.
    for (std::size_t i = 0; i < 6; ++i)
        in.run.gpu_limit_windows.push_back(
            {500.0 + static_cast<double>(i) * 200.0, 1u, 98.0, 1800.0});
    // Olculen kapsamda yuk yok: limit degerlendirmesi icin yuk penceresi sayilmaz.
    for (std::size_t i = 0; i < 6; ++i)
        in.run.gpu_limit_windows.push_back(
            {3300.0 + static_cast<double>(i) * 100.0, 0u, 3.0, 300.0});
    const DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound);
    EF_CHECK(!has(r.supporting, "gpu_thermal_limit"));
    EF_CHECK(!has(r.supporting, "gpu_clock_drop_unattributed"));
}

// Spec 2026-11-12 B (D5): kare basi GPU mesgul suresi drop penceresine "gpu_busy / kare suresi" olarak verilir.
EF_TEST(d5_drops_per_frame_gpu_busy_feeds_window_ratio) {
    std::vector<double> intervals = constant_intervals(240, 10.0);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) intervals[i] = 60.0;
    auto base = [&] {
        DropInput in;
        in.run = no_cap_144();
        in.run.run_id = "run-d5";
        in.frames = frames_from_intervals(intervals);
        return in;
    };
    // Takilma karesinde GPU 6 ms calisti (oran 0,10 <= 0,60): GPU bekledi -> CPU / motor sinyali. PDH sayaci yok.
    DropInput cpu = base();
    cpu.frame_gpu_busy_ms.assign(intervals.size(), 6.0);
    DropReport r = detect_drops(cpu);
    EF_CHECK(r.outcome == DropOutcome::DropsFound && r.total_windows == 4);
    EF_CHECK(dominant_is(r, DropSource::CpuOrEngineBound));
    EF_CHECK(has(r.supporting, "gpu_busy_short_of_frame_time") && !has(r.supporting, "gpu_busy_near_frame_time"));
    // Takilma karesinde GPU karenin tamaminda mesgul (58 / 60 = 0,97 >= 0,90): GPU sinyali.
    DropInput gpu = base();
    gpu.frame_gpu_busy_ms.assign(intervals.size(), 9.0);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) gpu.frame_gpu_busy_ms[i] = 58.0;
    r = detect_drops(gpu);
    EF_CHECK(dominant_is(r, DropSource::GpuBound));
    EF_CHECK(has(r.supporting, "gpu_busy_near_frame_time") && !has(r.supporting, "gpu_busy_short_of_frame_time"));
    // Arada (0,75): ne kisa ne tam -> GPU suresi kaniti yok.
    DropInput mid = base();
    mid.frame_gpu_busy_ms.assign(intervals.size(), 45.0);
    r = detect_drops(mid);
    EF_CHECK(!has(r.supporting, "gpu_busy_near_frame_time") && !has(r.supporting, "gpu_busy_short_of_frame_time"));
    // Kare olayinin kendi suresi, "gpu_busy_ms" telemetri sayacinin yerini alir (sayac tam mesgul dese de).
    DropInput both = base();
    both.frame_gpu_busy_ms.assign(intervals.size(), 6.0);
    both.sources.push_back(source("pdh", ClockDomain::Qpc, "gpu_busy_ms", 59.0, both.frames.back().t_ms + 20.0));
    r = detect_drops(both);
    EF_CHECK(has(r.supporting, "gpu_busy_short_of_frame_time") && !has(r.supporting, "gpu_busy_near_frame_time"));
    // Boyutu kare sayisiyla esit olmayan seri yok sayilir; penceredeki gecersiz deger GPU suresini "bilinmiyor" yapar.
    DropInput misaligned = base();
    misaligned.frame_gpu_busy_ms.assign(intervals.size() - 1, 6.0);
    r = detect_drops(misaligned);
    EF_CHECK(!has(r.supporting, "gpu_busy_short_of_frame_time"));
    DropInput invalid = base();
    invalid.frame_gpu_busy_ms.assign(intervals.size(), 6.0);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) invalid.frame_gpu_busy_ms[i] = -1.0;
    r = detect_drops(invalid);
    EF_CHECK(r.total_windows == 4 && !has(r.supporting, "gpu_busy_short_of_frame_time"));
    // Seri verilmediginde sonuc onceki davranisla aynidir (GPU suresi kaniti yok).
    const DropReport plain = detect_drops(base());
    EF_CHECK(plain.total_windows == 4 && !dominant_is(plain, DropSource::CpuOrEngineBound) && !dominant_is(plain, DropSource::GpuBound));
    EF_CHECK(!has(plain.supporting, "gpu_busy_short_of_frame_time") && !has(plain.supporting, "gpu_busy_near_frame_time"));

    // Siralama / suzme ile birlikte tasinir: kareler ters sirada ve arada gecersiz kare varken de hiza korunur.
    DropInput shuffled = base();
    shuffled.frame_gpu_busy_ms.assign(intervals.size(), 9.0);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) shuffled.frame_gpu_busy_ms[i] = 58.0;
    std::reverse(shuffled.frames.begin(), shuffled.frames.end());
    std::reverse(shuffled.frame_gpu_busy_ms.begin(), shuffled.frame_gpu_busy_ms.end());
    shuffled.frames.insert(shuffled.frames.begin() + 10, FramePoint{123.0, -5.0});  // gecersiz kare: dislanir
    shuffled.frame_gpu_busy_ms.insert(shuffled.frame_gpu_busy_ms.begin() + 10, 0.0);
    r = detect_drops(shuffled);
    EF_CHECK(r.excluded_frames == 1 && r.total_windows == 4 && dominant_is(r, DropSource::GpuBound));
}

// B3 inceleme (bosluk 2): drop penceresinin "baska programlar CPU" degeri ham sure okumalarindan pencerenin kendi
// zamani icin hesaplanir. 200 ms'lik %60 arka plan patlamasi, ~1 sn kayan ortalamada %12'ye sulanir ve yanlis
// "other_process_cpu_low" karsi kaniti uretirdi; pencere hesabi yuksek sinyali korur.
EF_TEST(drops_short_background_burst_reaches_other_cpu_high) {
    std::vector<double> intervals = constant_intervals(100, 16.7);
    intervals[60] = 60.0;
    DropInput in;
    in.run = no_cap_144();
    in.run.run_id = "run-burst";
    in.frames = frames_from_intervals(intervals);
    const double drop_end = in.frames[60].t_ms;  // pencere [drop_end - 60, drop_end]
    const double until = in.frames.back().t_ms + 20.0;
    // Kayan ortalamadan gelen sulanmis ornek degeri (eski yol): esigin altinda.
    in.sources.push_back(source("pdh", ClockDomain::Qpc, "other_process_cpu_pct", 12.0, until));
    // 16 mantiksal islemci; oyun 6 cekirdek; diger 1,6 cekirdek, patlamada 9,6 cekirdek (%60).
    const auto make = [&](bool burst) {
        std::vector<ef::core::metrics::CpuTimeReading> r;
        ef::core::metrics::CpuTimes c{};
        constexpr double unit = 10'000'000.0 * 0.05;  // 50 ms, 100 ns
        r.push_back({0, c});
        for (std::int64_t t = 50'000; t <= static_cast<std::int64_t>(until * 1000.0); t += 50'000) {
            const double tm = static_cast<double>(t) / 1000.0;
            const double other = burst && tm > drop_end - 160.0 && tm <= drop_end + 40.0 ? 9.6 : 1.6;
            c.total += static_cast<std::uint64_t>(16.0 * unit);
            c.busy += static_cast<std::uint64_t>((6.0 + other) * unit);
            c.own += static_cast<std::uint64_t>(6.0 * unit);
            r.push_back({t, c});
        }
        return r;
    };
    in.run.other_cpu_times = make(true);
    DropReport r = detect_drops(in);
    EF_CHECK(r.outcome == DropOutcome::DropsFound);
    EF_CHECK(has(r.supporting, "other_process_cpu_high"));
    EF_CHECK(!has(r.counter, "other_process_cpu_low"));
    // Patlama yoksa ayni pencere %10: karsi kanit dogru.
    in.run.other_cpu_times = make(false);
    r = detect_drops(in);
    EF_CHECK(!has(r.supporting, "other_process_cpu_high"));
    EF_CHECK(has(r.counter, "other_process_cpu_low"));
    // Okumalar pencereyi kapsamiyorsa deger eksik: karsi kanit uydurulmaz.
    in.run.other_cpu_times = {{0, {}}, {50'000, {800'000, 60'000, 50'000, 0, 0}}};
    r = detect_drops(in);
    EF_CHECK(!has(r.counter, "other_process_cpu_low") && !has(r.supporting, "other_process_cpu_high"));
}
