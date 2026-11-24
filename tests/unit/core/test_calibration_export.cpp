// DIAG-10: uygulama kosusundan diagcal_v1 ornegi — zorunlu alanlar, olculmeyen degerin yazilmamasi,
// etiketin disaridan gelmesi ve mevcut dosyaya guvenli ekleme.
#include <string>

#include "core/diagnosis/calibration_export.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using ef::contracts::ErrorCode;

namespace {

SceneCalibrationSample good_sample() {
    SceneCalibrationSample s;
    s.id = "app-1762600000000-0";
    s.provenance = Provenance::RealHardware;
    s.label = ComputeClass::GpuBound;  // deney tasarimi: bilinen GPU yuku; siniflandirici ciktisi degil
    s.avg_fps = 118.0;
    s.frames = 240.0;
    s.p99_ms = 9.5;
    s.gpu_busy_ms = 8.1;
    s.gpu_engine_3d_pct = 97.0;
    s.cpu_max_core_pct = 61.0;
    s.cpu_main_thread_pct = 44.0;
    return s;
}

}  // namespace

EF_TEST(calibration_export_builds_valid_window) {
    const auto w = make_labeled_window(good_sample());
    EF_CHECK(w.ok());
    const auto& x = w.value();
    EF_CHECK(x.id == "app-1762600000000-0" && x.provenance == Provenance::RealHardware);
    EF_CHECK(x.label == ComputeClass::GpuBound);
    EF_CHECK(x.window.valid && x.window.frames.sufficient && x.window.frames.n == 240);
    EF_CHECK(x.window.frames.avg_fps && *x.window.frames.avg_fps == 118.0);
    EF_CHECK(x.window.frames.p99_ms && *x.window.frames.p99_ms == 9.5);
    EF_CHECK(x.extras.gpu_busy_ms && *x.extras.gpu_busy_ms == 8.1);
    EF_CHECK(x.run.fps_cap_known_none && x.run.vsync == PresentSync::VsyncOff);
    EF_CHECK(x.window.metrics.size() == 3);
    bool gpu = false, core = false, main_thread = false;
    for (const auto& m : x.window.metrics) {
        gpu = gpu || (m.metric == kMetricGpu3d && m.value && *m.value == 97.0);
        core = core || (m.metric == kMetricCpuMaxCore && m.value && *m.value == 61.0);
        main_thread = main_thread || (m.metric == kMetricCpuMainThread && m.value && *m.value == 44.0);
        EF_CHECK(m.attribution == ef::core::measure::Attribution::RootCandidate);
    }
    EF_CHECK(gpu && core && main_thread);
}

EF_TEST(calibration_export_omits_unmeasured_values) {
    // gpu_busy_ms olculmediyse yazilmaz; kullanim yuzdesi gecersizse yazilmaz. 0 uydurulmaz.
    auto s = good_sample();
    s.gpu_busy_ms = std::nullopt;
    s.p99_ms = std::nullopt;
    s.gpu_engine_3d_pct = 0.0;        // 0 gecerli olcum olabilir (bosta) — yine yazilir
    s.cpu_max_core_pct = 140.0;       // aralik disi: kanit degil
    s.cpu_main_thread_pct = std::nullopt;
    const auto w = make_labeled_window(s);
    EF_CHECK(w.ok());
    EF_CHECK(!w.value().extras.gpu_busy_ms);
    EF_CHECK(!w.value().window.frames.p99_ms);
    EF_CHECK(w.value().window.metrics.size() == 1);  // yalniz gpu=0
    EF_CHECK(w.value().window.metrics[0].metric == kMetricGpu3d && *w.value().window.metrics[0].value == 0.0);
}

EF_TEST(calibration_export_rejects_invalid_samples) {
    auto s = good_sample();
    s.id.clear();
    EF_CHECK(!make_labeled_window(s).ok());  // bos id
    s = good_sample();
    s.avg_fps = std::nullopt;
    EF_CHECK(make_labeled_window(s).code() == ErrorCode::MissingValue);
    s = good_sample();
    s.avg_fps = 0.0;
    EF_CHECK(make_labeled_window(s).code() == ErrorCode::MissingValue);
    s = good_sample();
    s.frames = 99.0;  // kMinIntervals alti
    EF_CHECK(make_labeled_window(s).code() == ErrorCode::OutOfRange);
    s.frames = 100.5;  // kesirli kare sayisi
    EF_CHECK(make_labeled_window(s).code() == ErrorCode::OutOfRange);
    s = good_sample();
    s.id = std::string(97, 'a');
    EF_CHECK(make_labeled_window(s).code() == ErrorCode::OutOfRange);
}

EF_TEST(calibration_export_appends_and_validates_existing) {
    const auto empty = append_sample_json("", good_sample());
    EF_CHECK(empty.ok());
    const auto doc = parse_calibration(empty.value());  // cikti dogrulayicidan gecer
    EF_CHECK(doc.ok() && doc.value().samples.size() == 1);
    EF_CHECK(doc.value().samples[0].provenance == Provenance::RealHardware);

    auto second = good_sample();
    second.id = "app-1762600005000-1";
    second.provenance = Provenance::SyntheticFixture;
    const auto two = append_sample_json(empty.value(), second);
    EF_CHECK(two.ok());
    const auto doc2 = parse_calibration(two.value());
    EF_CHECK(doc2.ok() && doc2.value().samples.size() == 2);
    EF_CHECK(doc2.value().samples[1].provenance == Provenance::SyntheticFixture);

    // Ayni id ikinci kez eklenmez.
    EF_CHECK(!append_sample_json(two.value(), good_sample()).ok());
    // Bozuk mevcut dosya sessizce ezilmez.
    const auto corrupt = append_sample_json("{not json", good_sample());
    EF_CHECK(!corrupt.ok());
}

// Yalniz sahnesi olculmus, iptal edilmemis kosu kalibrasyon veri setine yazilir.
EF_TEST(calibration_export_needs_measured_uncancelled_run) {
    EF_CHECK(calibration_export_eligible({true, false}));
    EF_CHECK(!calibration_export_eligible({false, false}));  // sahne olculmedi
    EF_CHECK(!calibration_export_eligible({true, true}));    // iptal
}
