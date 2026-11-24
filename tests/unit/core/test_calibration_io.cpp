// DIAG-06/BM-02: diagcal_v1 veri seti sozlesmesi — gecerli ornek, sinir disi, bilinmeyen alan,
// tekrarli id ve sentetik/gercek ayrimi. Yayin kapisi bu dosyadan ASLA "sentetik" ile acilmaz.
#include <string>

#include "core/diagnosis/calibration_io.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using ef::contracts::ErrorCode;

namespace {

std::string sample_json(const std::string& id, const std::string& provenance = "synthetic",
                        const std::string& label = "gpu_bound") {
    return R"({"id":")" + id + R"(","provenance":")" + provenance + R"(","label":")" + label +
           R"(","avg_fps":120.0,"p99_ms":9.0,"frames":240,"refresh_hz":144.0,)" +
           R"("fps_cap_known_none":true,"vsync":"off","frame_interval_cv":0.04,)" +
           R"("gpu_busy_ms":8.0,"metrics":{"gpu_engine_3d_pct":97.0,"cpu_max_core_pct":55.0}})";
}

std::string document(const std::string& samples) {
    return R"({"schema_version":1,"format":"diagcal_v1","samples":[)" + samples + "]}";
}

}  // namespace

EF_TEST(calibration_io_parses_and_roundtrips) {
    const auto doc = parse_calibration(document(sample_json("real-1", "real") + "," + sample_json("syn-2")));
    EF_CHECK(doc.ok());
    EF_CHECK(doc.value().samples.size() == 2);
    const auto& a = doc.value().samples[0];
    EF_CHECK(a.id == "real-1" && a.provenance == Provenance::RealHardware);
    EF_CHECK(a.label == ComputeClass::GpuBound);
    EF_CHECK(a.window.frames.sufficient && a.window.frames.n == 240);
    EF_CHECK(a.window.frames.avg_fps && *a.window.frames.avg_fps == 120.0);
    EF_CHECK(a.window.metrics.size() == 2);
    EF_CHECK(a.extras.gpu_busy_ms && *a.extras.gpu_busy_ms == 8.0);
    EF_CHECK(a.run.display_refresh_hz && *a.run.display_refresh_hz == 144.0);
    EF_CHECK(a.run.vsync == PresentSync::VsyncOff && a.run.fps_cap_known_none);
    EF_CHECK(doc.value().samples[1].provenance == Provenance::SyntheticFixture);

    const auto text = write_calibration(doc.value());
    EF_CHECK(text.ok());
    const auto again = parse_calibration(text.value());
    EF_CHECK(again.ok());
    EF_CHECK(again.value().samples.size() == 2);
    EF_CHECK(again.value().samples[0].id == "real-1");
    EF_CHECK(again.value().samples[0].window.metrics.size() == 2);
}

EF_TEST(calibration_io_rejects_bad_documents) {
    // Bos ornek listesi gecerli veri degildir.
    const auto empty = parse_calibration(document(""));
    EF_CHECK(!empty.ok() && empty.code() == ErrorCode::ParseError);
    // Bilinmeyen ornek alani sessizce yok sayilmaz.
    const auto unknown = parse_calibration(document(sample_json("a").substr(0, sample_json("a").size() - 1) +
                                                    R"(,"extra":1})"));
    EF_CHECK(!unknown.ok() && unknown.code() == ErrorCode::ParseError);
    // Bilinmeyen metrik adi reddedilir.
    const auto bad_metric = parse_calibration(document(
        R"({"id":"a","provenance":"real","label":"gpu_bound","avg_fps":120,"frames":240,"metrics":{"bogus":1}})"));
    EF_CHECK(!bad_metric.ok() && bad_metric.code() == ErrorCode::ParseError);
    // Gecersiz etiket.
    const auto bad_label = parse_calibration(document(sample_json("a", "real", "gpu_bottleneck")));
    EF_CHECK(!bad_label.ok() && bad_label.code() == ErrorCode::ParseError);
    // Sinir disi deger.
    const auto out_of_range = parse_calibration(document(
        R"({"id":"a","provenance":"real","label":"gpu_bound","avg_fps":0.0,"frames":240})"));
    EF_CHECK(!out_of_range.ok() && out_of_range.code() == ErrorCode::OutOfRange);
    // Tekrarli id.
    const auto dup = parse_calibration(document(sample_json("dup") + "," + sample_json("dup")));
    EF_CHECK(!dup.ok() && dup.code() == ErrorCode::ParseError);
    // Desteklenenden yeni sema tahmin edilmez.
    const auto newer = parse_calibration(R"({"schema_version":3,"format":"diagcal_v1","samples":[]})");
    EF_CHECK(!newer.ok() && newer.code() == ErrorCode::SchemaTooNew);
    // Yanlis bicim adi.
    auto bad_format = document(sample_json("a"));
    bad_format.replace(bad_format.find("diagcal_v1"), 10, "diagcal_v9");
    const auto bf = parse_calibration(bad_format);
    EF_CHECK(!bf.ok() && bf.code() == ErrorCode::ParseError);
}

EF_TEST(calibration_io_stats_helpers) {
    CalibrationStats s;
    EF_CHECK(accuracy_all(s) == 0.0 && accuracy_answered(s) == 0.0 && coverage(s) == 0.0);
    s.total = 10;
    s.correct = 6;
    s.abstained = 2;
    s.false_positive = 1;
    s.false_negative = 1;
    EF_CHECK(accuracy_all(s) == 0.6);
    EF_CHECK(accuracy_answered(s) == 6.0 / 8.0);
    EF_CHECK(coverage(s) == 0.8);
    CalibrationStats all_abstained;
    all_abstained.total = 5;
    all_abstained.abstained = 5;
    EF_CHECK(accuracy_answered(all_abstained) == 0.0 && coverage(all_abstained) == 0.0);
}

EF_TEST(validated_calibration_gate_rules) {
    ValidatedCalibration none;
    auto s = release_status_from_validated(none);
    EF_CHECK(!s.ready && s.reason_key == "diag.gate.no_real_holdout");
    ValidatedCalibration ok{};
    ok.present = true;
    ok.real_holdout = kMinRealHoldout;
    ok.holdout_total = 60;
    ok.correct = 55;
    ok.dataset_sha256 = "abc";
    ok.collected_on = "2026-11-08";
    ok.holdout_machines = kMinHoldoutMachines;
    ok.holdout_positives = kMinHoldoutPerSide;
    ok.holdout_negatives = kMinHoldoutPerSide;
    s = release_status_from_validated(ok);
    EF_CHECK(s.ready && !s.synthetic_only && s.reason_key == "diag.gate.ready");
    ValidatedCalibration fp = ok;
    fp.false_positive = 4;  // 4/60 > %5
    EF_CHECK(!release_status_from_validated(fp).ready);
    EF_CHECK(release_status_from_validated(fp).reason_key == "diag.gate.false_positive_rate");
    ValidatedCalibration too_few = ok;
    too_few.real_holdout = kMinRealHoldout - 1;
    EF_CHECK(!release_status_from_validated(too_few).ready);
    // Uretim ozeti bu depoda kasitli olarak bos: kapi kapali kalir.
    EF_CHECK(!validated_calibration().present);
    EF_CHECK(!diagnosis_release_status().ready);
}

EF_TEST(calibration_io_synthetic_never_opens_gate) {
    const auto doc = parse_calibration(document(sample_json("syn-a") + "," + sample_json("syn-b")));
    EF_CHECK(doc.ok());
    const auto holdout = evaluate(doc.value().samples, Split::Holdout);
    const auto status = release_status(holdout);
    EF_CHECK(!status.ready && status.synthetic_only && status.real_holdout == 0);
    EF_CHECK(status.reason_key == "diag.gate.no_real_holdout");
}
