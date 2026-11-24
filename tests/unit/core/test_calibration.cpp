// DIAG-06: etiketli sentetik fikstur seti (tests/fixtures/diagnosis karsiligi, kodda uretilir) ile
// tune/holdout ayrimi, yanlis pozitif/negatif/cekimser olcumu ve yayin kapisi.
#include <cstdio>
#include <string>

#include "core/diagnosis/calibration.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::measure;
namespace i18n = ef::core::i18n;

namespace {
EvidenceWindow win(double frame_ms, double jitter, std::optional<double> gpu, std::optional<double> cpu_core) {
    EvidenceWindow w;
    w.run_id = "fixture";
    w.scope = WindowScope::Measured;
    std::vector<double> iv;
    for (int i = 0; i < 240; ++i) iv.push_back(frame_ms + ((i % 7) - 3) * jitter);
    w.frames = compute_frame_stats(iv, 30);
    if (gpu) w.metrics.push_back({std::string(kMetricGpu3d), *gpu, Attribution::RootCandidate});
    if (cpu_core) w.metrics.push_back({std::string(kMetricCpuMaxCore), *cpu_core, Attribution::RootCandidate});
    w.valid = w.frames.sufficient;
    return w;
}

std::vector<LabeledWindow> fixtures() {
    std::vector<LabeledWindow> v;
    RunComputeInputs open;  // sinir yok, VSync kapali (dogrulanmis)
    open.fps_cap_known_none = true;
    open.vsync = PresentSync::VsyncOff;
    open.display_refresh_hz = 144;
    RunComputeInputs capped = open;
    capped.fps_cap = 144;
    capped.fps_cap_known_none = false;
    for (int i = 0; i < 40; ++i) {
        const double n = (i % 5) * 0.4;  // kucuk varyasyon
        const auto id = [&](const char* k) { return std::string(k) + "-" + std::to_string(i); };
        v.push_back({id("gpu"), Provenance::SyntheticFixture, ComputeClass::GpuBound, win(8.0 + n, 0.4, 97.0 - n, 55.0 + n), open, {}});
        v.push_back({id("cpu"), Provenance::SyntheticFixture, ComputeClass::CpuOrEngineBound, win(9.0 + n, 1.5, 48.0 + n, 98.0), open, {}});
        v.push_back({id("cap"), Provenance::SyntheticFixture, ComputeClass::FrameCapOrPresentWait, win(1000.0 / 144, 0.05, 60.0 + n, 50.0), capped, {}});
        v.push_back({id("healthy"), Provenance::SyntheticFixture, ComputeClass::Healthy, win(6.0 + n, 0.3, 80.0, 70.0), open, {}});
        v.push_back({id("lowgpu-only"), Provenance::SyntheticFixture, ComputeClass::Insufficient, win(9.0 + n, 1.0, 40.0, std::nullopt), open, {}});
        v.push_back({id("nodata"), Provenance::SyntheticFixture, ComputeClass::Insufficient, win(9.0, 1.0, std::nullopt, std::nullopt), open, {}});
    }
    return v;
}
}  // namespace

EF_TEST(calibration_tune_holdout_separate_and_release_gate) {
    const auto set = fixtures();
    std::size_t tune = 0, hold = 0;
    for (const auto& x : set) (split_of(x.id) == Split::Holdout ? hold : tune) += 1;
    EF_CHECK(tune > 60 && hold > 60 && tune + hold == set.size());  // ayni ornek iki kumede degil
    const auto t = evaluate(set, Split::Tune);
    const auto h = evaluate(set, Split::Holdout);
    std::printf("calibration: tune total=%zu correct=%zu fp=%zu fn=%zu abstain=%zu wrong=%zu | holdout total=%zu correct=%zu fp=%zu fn=%zu abstain=%zu wrong=%zu synthetic=%zu real=%zu\n",
                t.total, t.correct, t.false_positive, t.false_negative, t.abstained, t.wrong_class, h.total, h.correct, h.false_positive,
                h.false_negative, h.abstained, h.wrong_class, h.synthetic, h.real);
    EF_CHECK(h.false_positive == 0);   // "veri yok/dusuk GPU tek basina" -> asla sinirlayici sinif
    EF_CHECK(h.real == 0 && h.synthetic == h.total);  // gercek donanim orani ayri: su an 0
    EF_CHECK(!diagnosis_release_ready(h));           // yalniz sentetik: yayin bayragi kapali
    CalibrationStats enough = h;
    enough.real = kMinRealHoldout;
    EF_CHECK(!diagnosis_release_ready(enough));  // EF-X20: makine/sinif dengesi yok
    EF_CHECK(release_status(enough).reason_key == "diag.gate.machine_coverage");
    enough.distinct_machines = kMinHoldoutMachines;
    enough.positives = kMinHoldoutPerSide;
    enough.negatives = kMinHoldoutPerSide;
    EF_CHECK(diagnosis_release_ready(enough) == (h.false_positive == 0));
}

EF_TEST(calibration_release_status_reason_and_synthetic_exclusion) {
    // BM-02: kapi durumu neden anahtariyla raporlanir; sentetik-only asla hazir degildir.
    const auto empty = release_status(CalibrationStats{});
    EF_CHECK(!empty.ready && empty.synthetic_only && empty.reason_key == "diag.gate.no_real_holdout");
    CalibrationStats synthetic_only;
    synthetic_only.total = 120;
    synthetic_only.real = 0;
    synthetic_only.synthetic = 120;
    const auto s = release_status(synthetic_only);
    EF_CHECK(!s.ready && s.synthetic_only && s.reason_key == "diag.gate.no_real_holdout");
    // Yeterli gercek ornek + dusuk yanlis pozitif: acik.
    CalibrationStats ok;
    ok.total = 60;
    ok.real = kMinRealHoldout;
    ok.synthetic = 30;
    ok.false_positive = 0;
    ok.distinct_machines = kMinHoldoutMachines;
    ok.positives = kMinHoldoutPerSide;
    ok.negatives = kMinHoldoutPerSide;
    const auto o = release_status(ok);
    EF_CHECK(o.ready && !o.synthetic_only && o.reason_key == "diag.gate.ready");
    // Yanlis pozitif orani sinirin ustunde: kapali, neden ayri.
    CalibrationStats bad = ok;
    bad.false_positive = 6;
    bad.eligible_false_positive = 6;  // EF-X20 bulgu 1: payda uygun gercek (30): 6/30 = %20 > %5
    const auto b = release_status(bad);
    EF_CHECK(!b.ready && b.reason_key == "diag.gate.false_positive_rate");
    // Uretim kapisi: gercek donanim holdout kumesi yok -> hazir degil (sentetik gercek sayilmaz).
    const auto prod = diagnosis_release_status();
    EF_CHECK(!prod.ready && prod.synthetic_only && prod.reason_key == "diag.gate.no_real_holdout");
    for (const auto* k : {"diag.gate.ready", "diag.gate.no_real_holdout", "diag.gate.false_positive_rate"})
        EF_CHECK(i18n::id_of(k).has_value());
}
