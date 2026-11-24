// DIAG-02: CPU/GPU, cap ve sunum ayrimi.
#include <algorithm>

#include "core/diagnosis/calibration.h"  // BM-02: uretim kapisi sarmali
#include "core/diagnosis/compute.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::measure;

namespace {
EvidenceWindow window(double fps, double p99_ms) {
    EvidenceWindow w;
    w.run_id = "run-c";
    w.start_ms = 0;
    w.end_ms = 2000;
    w.valid = true;
    w.frames.n = 200;
    w.frames.sufficient = true;
    w.frames.avg_fps = fps;
    w.frames.p99_ms = p99_ms;
    return w;
}
void add(EvidenceWindow& w, std::string_view m, double v, Attribution a = Attribution::RootCandidate) {
    w.metrics.push_back({std::string(m), v, a});
}
bool has(const std::vector<std::string>& v, const char* code) { return std::find(v.begin(), v.end(), code) != v.end(); }
RunComputeInputs known_no_cap() {
    RunComputeInputs r;
    r.display_refresh_hz = 144;
    r.vsync = PresentSync::VsyncOff;
    r.fps_cap_known_none = true;
    return r;
}
}  // namespace

EF_TEST(diag_compute_release_gate_blocks_classification) {
    // BM-02: kapi kapaliyken siniflandirici yayinlanabilir sinif uretmez; kalibrasyon degerlendirmesi
    // (classify_window) kapiyi kullanmaz ki kapi acilabilsin.
    auto w = window(80, 13.0);
    add(w, kMetricGpu3d, 97);
    add(w, kMetricCpuMaxCore, 40);
    ReleaseStatus closed;
    closed.ready = false;
    const auto gated = classify_window_gated(w, known_no_cap(), {}, closed);
    EF_CHECK(gated.cls == ComputeClass::Insufficient && gated.reason == ComputeConfidence::CalibrationUnverified);
    EF_CHECK(has(gated.counter, "diagnosis_calibration_unverified"));
    EF_CHECK(to_string(gated.reason) == "calibration_unverified");
    ReleaseStatus open;
    open.ready = true;
    const auto openv = classify_window_gated(w, known_no_cap(), {}, open);
    EF_CHECK(openv.cls == ComputeClass::GpuBound);  // kapi acikken normal siniflandirma
    // Kosu duzeyi: pencere sayisi korunur, hepsi Insufficient.
    Timeline tl;
    tl.windows = {w, w};
    const auto run_closed = classify_run_gated(tl, known_no_cap(), {}, closed);
    EF_CHECK(run_closed.overall.cls == ComputeClass::Insufficient && run_closed.windows.size() == 2);
    for (const auto& x : run_closed.windows) EF_CHECK(x.reason == ComputeConfidence::CalibrationUnverified);
    // Uretim sarmali uretim kapisini kullanir (su an kapali).
    const auto prod = classify_run_with_production_gate(tl, known_no_cap());
    EF_CHECK(prod.overall.cls == ComputeClass::Insufficient &&
              prod.overall.reason == ComputeConfidence::CalibrationUnverified);
}

EF_TEST(diag_compute_low_gpu_alone_is_not_cpu) {
    auto w = window(80, 13.0);  // sabit kare akisi
    add(w, kMetricGpu3d, 45);  // yalniz dusuk GPU
    auto v = classify_window(w, known_no_cap());
    EF_CHECK(v.cls != ComputeClass::CpuOrEngineBound);
    EF_CHECK(v.cls == ComputeClass::Insufficient && has(v.counter, "cpu_metric_missing"));
    EF_CHECK(v.reason == ComputeConfidence::NoCorroboration);

    add(w, kMetricCpuMaxCore, 50);  // CPU olculdu ama doygun degil -> yine CPU degil
    v = classify_window(w, known_no_cap());
    EF_CHECK(v.cls == ComputeClass::Mixed && v.reason == ComputeConfidence::NoCorroboration);

    // Dogrulama: tek cekirdek doygun + cap dislanmis -> CpuOrEngineBound.
    w.metrics.back().value = 97;
    v = classify_window(w, known_no_cap());
    EF_CHECK(v.cls == ComputeClass::CpuOrEngineBound && v.reason == ComputeConfidence::MultipleCorroborated);
    EF_CHECK(has(v.supporting, "gpu_util_low") && has(v.supporting, "cpu_core_saturated"));

    // Cap durumu bilinmiyor ve kare akisi sabit -> kesin CPU karari yok.
    RunComputeInputs unknown;
    v = classify_window(w, unknown);
    EF_CHECK(v.cls == ComputeClass::Mixed && v.reason == ComputeConfidence::CapStateUnknown && has(v.counter, "cap_state_unknown"));
}

EF_TEST(diag_compute_gpu_timing_corroborates_cpu) {
    auto w = window(100, 11.0);  // 10 ms kare
    add(w, kMetricGpu3d, 50);
    const auto v = classify_window(w, known_no_cap(), {4.0, std::nullopt});  // GPU 4 ms mesgul << 10 ms
    EF_CHECK(v.cls == ComputeClass::CpuOrEngineBound && v.reason == ComputeConfidence::DirectGpuTiming);
    EF_CHECK(has(v.supporting, "gpu_busy_short_of_frame_time"));
}

EF_TEST(diag_compute_high_util_expected_fps_not_fault) {
    auto w = window(120, 9.5);
    add(w, kMetricGpu3d, 98);
    add(w, kMetricCpuMaxCore, 60);
    auto run = known_no_cap();
    run.expected_fps = 118;
    auto v = classify_window(w, run);
    EF_CHECK(v.cls == ComputeClass::GpuBound && !v.is_fault);
    EF_CHECK(v.reason == ComputeConfidence::MultipleCorroborated);
    EF_CHECK(has(v.supporting, "gpu_util_high") && has(v.supporting, "fps_meets_expected"));

    run.expected_fps = 160;  // beklenenin altinda -> ariza adayi
    v = classify_window(w, run);
    EF_CHECK(v.cls == ComputeClass::GpuBound && v.is_fault && has(v.supporting, "fps_below_expected"));

    run.expected_fps.reset();  // referans yok -> ariza denmez
    v = classify_window(w, run);
    EF_CHECK(!v.is_fault && has(v.counter, "expected_fps_unknown"));
}

EF_TEST(diag_compute_capped_144_is_frame_cap) {
    auto w = window(143.6, 7.3);
    add(w, kMetricGpu3d, 55);  // dusuk GPU ama cap'te: CPU degil
    add(w, kMetricCpuMaxCore, 40);
    RunComputeInputs run;
    run.display_refresh_hz = 144;
    run.vsync = PresentSync::VsyncOn;
    auto v = classify_window(w, run, {std::nullopt, 0.03});
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && v.reason == ComputeConfidence::CapKnown && !v.is_fault);
    EF_CHECK(has(v.supporting, "fps_at_cap") && has(v.supporting, "frame_variance_low"));

    RunComputeInputs cap;
    cap.fps_cap = 144;
    cap.vsync = PresentSync::VsyncOff;
    v = classify_window(w, cap, {std::nullopt, 0.03});
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && has(v.supporting, "fps_cap_known"));

    RunComputeInputs inferred;  // VSync bilinmiyor; yenileme hizina sabit
    inferred.display_refresh_hz = 144;
    v = classify_window(w, inferred, {std::nullopt, 0.03});
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && v.reason == ComputeConfidence::CapInferredFromRefresh);

    v = classify_window(w, run, {std::nullopt, 0.4});  // cap'te ama dalgali -> Mixed
    EF_CHECK(v.cls == ComputeClass::Mixed && has(v.counter, "frame_variance_high"));
}

EF_TEST(diag_compute_missing_data_is_insufficient) {
    auto w = window(90, 12.0);
    auto v = classify_window(w, known_no_cap());  // hic sayac yok
    EF_CHECK(v.cls == ComputeClass::Insufficient && v.reason == ComputeConfidence::MissingMetrics);
    EF_CHECK(has(v.counter, "gpu_metric_missing") && has(v.counter, "cpu_metric_missing"));

    // 1 Hz baglam (ContextOnly) ms kok neden olamaz -> eksik sayilir.
    add(w, kMetricGpu3d, 99, Attribution::ContextOnly);
    add(w, kMetricCpuMaxCore, 99, Attribution::ContextOnly);
    v = classify_window(w, known_no_cap());
    EF_CHECK(v.cls == ComputeClass::Insufficient && v.reason == ComputeConfidence::ContextOnlyMetrics);
    EF_CHECK(has(v.counter, "context_only:gpu_engine_3d_pct"));

    auto bad = window(90, 12.0);
    bad.frames.sufficient = false;
    v = classify_window(bad, known_no_cap());
    EF_CHECK(v.cls == ComputeClass::Insufficient && v.reason == ComputeConfidence::MissingFrames);

    Timeline empty;
    const auto r = classify_run(empty, known_no_cap());
    EF_CHECK(r.overall.cls == ComputeClass::Insufficient && has(r.overall.counter, "no_usable_windows"));
}

EF_TEST(diag_compute_healthy_and_run_aggregate) {
    auto w = window(100, 11.0);
    add(w, kMetricGpu3d, 80);
    add(w, kMetricCpuMaxCore, 60);
    auto v = classify_window(w, known_no_cap());
    EF_CHECK(v.cls == ComputeClass::Healthy && v.reason == ComputeConfidence::NoLimitingSignal && !v.is_fault);

    auto g = window(100, 11.0);
    add(g, kMetricGpu3d, 97);
    add(g, kMetricCpuMaxCore, 50);
    Timeline tl;
    tl.windows = {g, g, g, w};
    auto r = classify_run(tl, known_no_cap());
    EF_CHECK(r.windows.size() == 4 && r.overall.cls == ComputeClass::GpuBound && !r.overall.is_fault);

    tl.windows = {g, g, w, w};  // %50 / %50 -> Mixed
    r = classify_run(tl, known_no_cap());
    EF_CHECK(r.overall.cls == ComputeClass::Mixed && r.overall.reason == ComputeConfidence::ConflictingSignals);

    auto warm = g;
    warm.valid = false;  // kapsam disi pencere sayilmaz
    tl.windows = {warm, w, w};
    r = classify_run(tl, known_no_cap());
    EF_CHECK(r.overall.cls == ComputeClass::Healthy && r.windows[0].cls == ComputeClass::Insufficient);
    EF_CHECK(to_string(r.overall.cls) == "healthy" && to_string(r.overall.reason) == "no_limiting_signal");
}

// Uyumluluk B4 (bosluk 14): 240 Hz + G-SYNC + Reflex otomatik siniri (224 FPS) yalniz dusuk kare araligi CV'si ile.
EF_TEST(diag_compute_vrr_auto_cap_needs_low_cv) {
    auto w = window(224.0, 4.6);
    add(w, kMetricGpu3d, 60);
    add(w, kMetricCpuMaxCore, 40);
    RunComputeInputs run;
    run.display_refresh_hz = 240;
    run.vsync = PresentSync::VsyncOn;
    auto v = classify_window(w, run, {std::nullopt, 0.03});
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && has(v.supporting, "vrr_cap_candidate") && has(v.supporting, "fps_at_cap"));
    // CV bilinmiyor (yalniz p99 vekili) ya da yuksek: aday denenmez, sinirin altinda sayilir.
    v = classify_window(w, run);
    EF_CHECK(v.cls != ComputeClass::FrameCapOrPresentWait && !has(v.supporting, "vrr_cap_candidate") && has(v.counter, "fps_below_cap"));
    v = classify_window(w, run, {std::nullopt, 0.2});
    EF_CHECK(v.cls != ComputeClass::FrameCapOrPresentWait && !has(v.supporting, "vrr_cap_candidate"));
    // "yenileme - 3" (237 FPS) de aday; bilinen oyun siniri varsa yalniz o kullanilir.
    auto w237 = window(237.0, 4.3);
    add(w237, kMetricGpu3d, 60);
    add(w237, kMetricCpuMaxCore, 40);
    v = classify_window(w237, run, {std::nullopt, 0.03});
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait);  // 237 yenilemenin %1,25 altinda: dogrudan eslesir
    RunComputeInputs capped = run;
    capped.fps_cap = 300;
    v = classify_window(w, capped, {std::nullopt, 0.03});
    EF_CHECK(!has(v.supporting, "vrr_cap_candidate") && v.cls != ComputeClass::FrameCapOrPresentWait);
    // VSync kapali: yenileme siniri yok, aday da yok.
    RunComputeInputs off = run;
    off.vsync = PresentSync::VsyncOff;
    v = classify_window(w, off, {std::nullopt, 0.03});
    EF_CHECK(!has(v.supporting, "vrr_cap_candidate"));
    // Inceleme B4: dusuk CV ama GPU kare basi mesgul ~ kare suresi (GPU sinirli dusus) -> aday kabul edilmez.
    v = classify_window(w, run, {1000.0 / 224.0 * 0.98, 0.03});
    EF_CHECK(!has(v.supporting, "vrr_cap_candidate") && !has(v.supporting, "fps_at_cap") && v.cls != ComputeClass::FrameCapOrPresentWait);
    // 60 Hz panelde 57 FPS GPU sinirli dusus de ayni.
    auto w57 = window(57.0, 17.5);
    add(w57, kMetricGpu3d, 60);
    add(w57, kMetricCpuMaxCore, 40);
    RunComputeInputs r60 = run;
    r60.display_refresh_hz = 60;
    v = classify_window(w57, r60, {1000.0 / 57.0 * 0.97, 0.03});
    EF_CHECK(!has(v.supporting, "fps_at_cap") && v.cls != ComputeClass::FrameCapOrPresentWait);
    // GPU kullanimi yuksek (>= %90) da adayi reddeder; GPU bekliyorsa aday kabul edilir.
    auto wbusy = window(224.0, 4.6);
    add(wbusy, kMetricGpu3d, 97);
    add(wbusy, kMetricCpuMaxCore, 40);
    v = classify_window(wbusy, run, {std::nullopt, 0.03});
    EF_CHECK(!has(v.supporting, "vrr_cap_candidate"));
    v = classify_window(w, run, {1000.0 / 224.0 * 0.5, 0.03});
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && has(v.supporting, "vrr_cap_candidate"));
}
