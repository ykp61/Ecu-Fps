// EF-X20: gruplu uc yonlu bolme, sizinti/yinelenmis makine, sinif dengesi, cihaz ailesi kapsami,
// genis etiket taksonomisi, model basina rapor, diagcal_v2 ve disa aktarma korumalari. Gercek dogruluk iddiasi YOK.
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/calibration.h"
#include "core/diagnosis/calibration_export.h"
#include "core/diagnosis/calibration_io.h"
#include "core/i18n/catalog.h"
#include "core/insights/history.h"
#include "core/measure/frame_stats.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::measure;
namespace i18n = ef::core::i18n;
using ef::contracts::ErrorCode;

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
RunComputeInputs open_run() {
    RunComputeInputs r;
    r.fps_cap_known_none = true;
    r.vsync = PresentSync::VsyncOff;
    r.display_refresh_hz = 144;
    return r;
}
// Opak test ozeti: makine adindan turetilir (kucuk harf hex, >= 8). Bos makine -> bos ozet.
std::string digest_of(const std::string& machine) {
    if (machine.empty()) return {};
    static constexpr char kHex[] = "0123456789abcdef";
    std::string d = "d16e";
    for (unsigned char c : machine) {
        d += kHex[c >> 4];
        d += kHex[c & 15];
    }
    while (d.size() < 8) d += '0';
    return d;
}
LabeledWindow sample(std::string id, Provenance p, CalibrationLabel l, std::string machine, std::string run, int k,
                     LabelMethod method = LabelMethod::ControlledExperiment) {
    LabeledWindow x;
    x.id = std::move(id);
    x.provenance = p;
    x.fine_label = l;
    x.label = expected_compute_class(l).value_or(ComputeClass::Insufficient);
    x.machine_digest = digest_of(machine);  // v2 kapi uygunlugu icin zorunlu (bulgu 3)
    x.machine_group = std::move(machine);
    x.run_group = std::move(run);
    x.label_method = method;
    x.family = {CpuVendor::Intel, GpuFamily::Nvidia, FormFactor::Desktop, StorageKind::Ssd};
    x.run = open_run();
    const double d = 0.01 * k;  // olcum izi tekil kalsin
    x.window = l == CalibrationLabel::GpuBound ? win(8.0 + d, 0.4, 97.0, 55.0) : win(6.0 + d, 0.3, 80.0, 70.0);
    return x;
}
std::vector<std::string> machines_with(bool holdout, std::size_t n) {
    std::vector<std::string> out;
    for (int i = 0; i < 4000 && out.size() < n; ++i) {
        LabeledWindow p;
        p.machine_group = "m" + std::to_string(i);
        p.run_group = "r";
        if ((split_for(p) == Split::Holdout) == holdout) out.push_back(p.machine_group);
    }
    return out;
}
bool has_issue(const SplitIntegrity& r, const char* code) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const SplitIssue& i) { return i.code == code; });
}
bool has_note(const SplitIntegrity& r, const char* code) {
    return std::any_of(r.notes.begin(), r.notes.end(), [&](const SplitIssue& i) { return i.code == code; });
}
CalibrationStats passing_stats() {
    CalibrationStats s;
    s.total = 40;
    s.real = 40;
    s.distinct_machines = kMinHoldoutMachines;
    s.positives = 20;
    s.negatives = 20;
    return s;
}
}  // namespace

EF_TEST(calibration_x20_t01_synthetic_never_opens_gate) {
    // EF-X20-T01: tam gruplu, dengeli, cok makineli SENTETIK veri hicbir bolmede kapiyi acmaz.
    std::vector<LabeledWindow> set;
    for (int m = 0; m < 30; ++m)
        for (int r = 0; r < 4; ++r)
            set.push_back(sample("syn-" + std::to_string(m) + "-" + std::to_string(r), Provenance::SyntheticFixture,
                                 r % 2 ? CalibrationLabel::GpuBound : CalibrationLabel::Healthy, "m" + std::to_string(m),
                                 "r" + std::to_string(m) + "-" + std::to_string(r), m * 4 + r));
    const auto integrity = validate_split_integrity(set);
    EF_CHECK(integrity.ok);
    std::size_t seen = 0;
    for (const Split sp : {Split::Tune, Split::Validation, Split::Holdout}) {
        const auto st = evaluate(set, sp);
        seen += st.total;
        EF_CHECK(st.real == 0);
        const auto g = release_status(st, integrity);
        EF_CHECK(!g.ready && g.synthetic_only && g.reason_key == "diag.gate.no_real_holdout");
    }
    EF_CHECK(seen == set.size());
    // 29 gercek + cok sentetik: sentetik eksigi kapatmaz.
    auto mixed = passing_stats();
    mixed.real = kMinRealHoldout - 1;
    mixed.synthetic = 500;
    mixed.total = mixed.real + mixed.synthetic;
    EF_CHECK(release_status(mixed).reason_key == "diag.gate.no_real_holdout");
    EF_CHECK(!validated_calibration().present && !diagnosis_release_status().ready);
    EF_CHECK(diagnosis_version_stamp() == "compute_v1:uncalibrated");
}

EF_TEST(calibration_x20_t02_fps_capped_healthy_system_not_cpu_fault) {
    // EF-X20-T02: 60 Hz VSync acik saglikli sistem: dusuk GPU + orta CPU -> kare siniri, ariza degil.
    RunComputeInputs vs;
    vs.display_refresh_hz = 60;
    vs.vsync = PresentSync::VsyncOn;
    vs.expected_fps = 60;
    auto v = classify_window(win(1000.0 / 60, 0.05, 35.0, 45.0), vs);
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && !v.is_fault);
    // Oyun FPS siniri 120; tek cekirdek %95 gorunse bile sinirda calismak CPU arizasi degildir.
    RunComputeInputs cap;
    cap.fps_cap = 120;
    cap.vsync = PresentSync::VsyncOff;
    cap.display_refresh_hz = 144;
    cap.expected_fps = 120;
    v = classify_window(win(1000.0 / 120, 0.05, 40.0, 95.0), cap);
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && !v.is_fault);
    // VSync durumu bilinmiyor, yenilemeye sabit: yine CPU denmez.
    RunComputeInputs unk;
    unk.display_refresh_hz = 144;
    v = classify_window(win(1000.0 / 144, 0.05, 50.0, 50.0), unk);
    EF_CHECK(v.cls == ComputeClass::FrameCapOrPresentWait && v.reason == ComputeConfidence::CapInferredFromRefresh);
    EF_CHECK(!v.is_fault);
    // Kalibrasyon: vsync/fps_cap/engine_frame_limit cap sinifina eslenir; CPU sutunu bos, FP yok.
    struct Row {
        CalibrationLabel l;
        RunComputeInputs r;
        double ms;
        double cpu;
    };
    const Row rows[] = {{CalibrationLabel::Vsync, vs, 1000.0 / 60, 45.0},
                        {CalibrationLabel::FpsCap, cap, 1000.0 / 120, 95.0},
                        {CalibrationLabel::EngineFrameLimit, cap, 1000.0 / 120, 50.0}};
    std::vector<LabeledWindow> set;
    int k = 0;
    for (const auto& row : rows) {
        auto x = sample("cap-" + std::to_string(k), Provenance::SyntheticFixture, row.l, "mc", "rc" + std::to_string(k), k);
        x.run = row.r;
        x.window = win(row.ms, 0.05, 40.0, row.cpu);
        set.push_back(x);
        ++k;
    }
    for (const Split sp : {Split::Tune, Split::Validation, Split::Holdout}) {
        const auto st = evaluate(set, sp);
        EF_CHECK(st.false_positive == 0);
        EF_CHECK(st.confusion[static_cast<std::size_t>(ComputeClass::FrameCapOrPresentWait)]
                              [static_cast<std::size_t>(ComputeClass::CpuOrEngineBound)] == 0);
    }
    // Uretim yolu kapali: ariza/sinif hic uretilmez.
    Timeline tl;
    tl.windows.push_back(win(1000.0 / 60, 0.05, 35.0, 45.0));
    const auto prod = classify_run_with_production_gate(tl, vs);
    EF_CHECK(prod.overall.cls == ComputeClass::Insufficient && !prod.overall.is_fault);
    EF_CHECK(prod.overall.reason == ComputeConfidence::CalibrationUnverified);
}

EF_TEST(calibration_x20_t03_unsupported_device_and_balance_rules_insufficient) {
    // EF-X20-T03: kapsam disi cihaz -> kapi kapali -> Insufficient (CalibrationUnverified).
    ValidatedCalibration v{};
    v.present = true;
    v.real_holdout = 40;
    v.holdout_total = 40;
    v.correct = 36;
    v.holdout_machines = 4;
    v.holdout_positives = 20;
    v.holdout_negatives = 20;
    const DeviceFamily covered{CpuVendor::Intel, GpuFamily::Nvidia, FormFactor::Desktop, StorageKind::Ssd};
    v.covered_families = family_bits(covered);
    EF_CHECK(family_bits(DeviceFamily{}) == 0);
    const auto base = release_status_from_validated(v);
    EF_CHECK(base.ready);
    EF_CHECK(release_status_for_device(base, v.covered_families, covered).ready);
    for (const DeviceFamily d : {DeviceFamily{CpuVendor::Amd, GpuFamily::Nvidia, FormFactor::Desktop, StorageKind::Ssd},
                                 DeviceFamily{CpuVendor::Intel, GpuFamily::Nvidia, FormFactor::Laptop, StorageKind::Ssd},
                                 DeviceFamily{CpuVendor::Intel, GpuFamily::Nvidia, FormFactor::Desktop, StorageKind::Hdd},
                                 DeviceFamily{}}) {
        const auto s = release_status_for_device(base, v.covered_families, d);
        EF_CHECK(!s.ready && s.reason_key == "diag.gate.device_family_uncovered");
        const auto verdict = classify_window_gated(win(8.0, 0.4, 97.0, 55.0), open_run(), WindowComputeExtras{}, s);
        EF_CHECK(verdict.cls == ComputeClass::Insufficient && verdict.reason == ComputeConfidence::CalibrationUnverified);
    }
    // Sinif dengesi (docs/18 kural 5): tek sinif gercek ornek kapiyi acmaz.
    auto one_class = passing_stats();
    one_class.positives = 40;
    one_class.negatives = 0;
    EF_CHECK(release_status(one_class).reason_key == "diag.gate.class_imbalance");
    auto one_machine = passing_stats();
    one_machine.distinct_machines = 1;
    EF_CHECK(release_status(one_machine).reason_key == "diag.gate.machine_coverage");
    auto ineligible = passing_stats();
    ineligible.real_ineligible = 11;  // 29 uygun
    EF_CHECK(release_status(ineligible).reason_key == "diag.gate.label_or_group_missing");
    EF_CHECK(release_status(passing_stats()).ready);
    // evaluate aile kapsamini yalniz yeterli uygun gercek ornekten VE >= 2 farkli makineden turetir
    // (EF-X20 dogrulama gozlemi: tek PC'nin 10 ornegi aileyi kapsamaz).
    const auto hold = machines_with(true, 2);
    EF_CHECK(hold.size() == 2);
    std::vector<LabeledWindow> fam;
    for (int i = 0; i < static_cast<int>(kMinFamilyHoldoutSamples); ++i)
        fam.push_back(sample("f" + std::to_string(i), Provenance::RealHardware, CalibrationLabel::Healthy, hold[0],
                             "fr" + std::to_string(i), i));
    auto st = evaluate(fam, Split::Holdout);
    EF_CHECK(st.distinct_machines == 1 && st.covered_families == 0);  // tek makine: kapsam yok
    fam.push_back(sample("f-other", Provenance::RealHardware, CalibrationLabel::Healthy, hold[1], "fr-other", 99));
    st = evaluate(fam, Split::Holdout);
    EF_CHECK(st.covered_families == family_bits(covered) && st.distinct_machines == 2);
    fam.erase(fam.begin(), fam.begin() + 2);  // hala 2 makine, ama toplam 9 ornek
    st = evaluate(fam, Split::Holdout);
    EF_CHECK(st.distinct_machines == 2 && st.covered_families == 0);
    // Ozetsiz (machine_digest yok) ornekler aileyi kapsamaz ve uygun sayilmaz.
    auto no_digest = fam;
    no_digest.push_back(sample("f-x1", Provenance::RealHardware, CalibrationLabel::Healthy, hold[0], "fr-x1", 97));
    no_digest.push_back(sample("f-x2", Provenance::RealHardware, CalibrationLabel::Healthy, hold[1], "fr-x2", 98));
    for (auto& x : no_digest) x.machine_digest.clear();
    st = evaluate(no_digest, Split::Holdout);
    EF_CHECK(st.covered_families == 0 && st.distinct_machines == 0 && st.real_ineligible == no_digest.size());
    EF_CHECK(st.real_missing_digest == no_digest.size() && eligible_real(st) == 0);
    EF_CHECK(!diagnosis_release_status_for(covered).ready);  // uretim: cihazdan bagimsiz kapali
    for (const auto* key : {"diag.gate.label_or_group_missing", "diag.gate.machine_coverage", "diag.gate.class_imbalance",
                            "diag.gate.device_family_uncovered", "diag.gate.split_leakage"})
        EF_CHECK(i18n::id_of(key).has_value());
}

EF_TEST(calibration_x20_t04_leakage_and_duplicate_holdout_machine) {
    // EF-X20-T04: sizinti ve yinelenmis holdout makinesi yakalanir; kapi kapanir.
    const auto hold = machines_with(true, 2);
    const auto rest = machines_with(false, 1);
    EF_CHECK(hold.size() == 2 && rest.size() == 1);
    std::vector<LabeledWindow> clean;
    clean.push_back(sample("a1", Provenance::RealHardware, CalibrationLabel::GpuBound, hold[0], "run-a1", 1));
    clean.push_back(sample("b1", Provenance::RealHardware, CalibrationLabel::Healthy, rest[0], "run-b1", 2));
    EF_CHECK(validate_split_integrity(clean).ok);
    // 1) Ayni kosu iki makinede.
    auto leak = clean;
    leak.push_back(sample("b2", Provenance::RealHardware, CalibrationLabel::GpuBound, rest[0], "run-a1", 3));
    auto r = validate_split_integrity(leak);
    EF_CHECK(!r.ok && has_issue(r, "run_group_multi_machine"));
    // 2) Ayni olcum farkli kimlik/makine ile karsi kumede.
    auto copy = clean;
    auto dup = clean[0];
    dup.id = "b3";
    dup.machine_group = rest[0];
    dup.run_group = "run-b3";
    copy.push_back(dup);
    r = validate_split_integrity(copy);
    EF_CHECK(!r.ok && has_issue(r, "duplicate_window_cross_split"));
    // 3) Ayni donanim iki ad altinda; biri holdout.
    auto twin = clean;
    twin[0].machine_digest = "0123abcd";
    twin[1].machine_digest = "0123abcd";
    r = validate_split_integrity(twin);
    EF_CHECK(!r.ok && has_issue(r, "duplicate_holdout_machine"));
    // 3b) Holdout'ta iki ad ayni makine (makine sayisini sisirir).
    auto twin_hold = clean;
    twin_hold.push_back(sample("a2", Provenance::RealHardware, CalibrationLabel::Healthy, hold[1], "run-a2", 4));
    twin_hold[0].machine_digest = "feedbeef01";
    twin_hold.back().machine_digest = "feedbeef01";
    EF_CHECK(has_issue(validate_split_integrity(twin_hold), "duplicate_holdout_machine"));
    // 3c) Ayni olcum ayni bolmede (hepsi holdout) baska makine adlariyla: makine sayisini sisirir (bulgu 2).
    {
        const auto h3 = machines_with(true, 3);
        EF_CHECK(h3.size() == 3);
        std::vector<LabeledWindow> inflate;
        inflate.push_back(sample("w0", Provenance::RealHardware, CalibrationLabel::GpuBound, h3[0], "run-w0", 50));
        for (std::size_t i = 1; i < 3; ++i) {
            auto c = inflate[0];
            c.id = "w" + std::to_string(i);
            c.machine_group = h3[i];
            c.machine_digest = digest_of(c.machine_group);  // ozet de farkli: yalniz pencere izi yakalar
            c.run_group = "run-w" + std::to_string(i);
            inflate.push_back(c);
        }
        for (const auto& x : inflate) EF_CHECK(split_for(x) == Split::Holdout);
        const auto ri = validate_split_integrity(inflate);
        EF_CHECK(!ri.ok && has_issue(ri, "duplicate_window_multi_machine"));
        EF_CHECK(!has_issue(ri, "duplicate_window_cross_split") && !has_issue(ri, "duplicate_holdout_machine"));
        EF_CHECK(release_status(passing_stats(), ri).reason_key == "diag.gate.split_leakage");
        // 3d) Ayni makine ve bolmede yeni kimlik/run ile kopya (FP oranini sulandirir): yakalanir.
        auto same = inflate[0];
        same.id = "w0-copy";
        same.run_group = "run-w0-copy";
        const auto rs = validate_split_integrity({inflate[0], same});
        EF_CHECK(!rs.ok && has_issue(rs, "duplicate_window"));
        EF_CHECK(release_status(passing_stats(), rs).reason_key == "diag.gate.split_leakage");
        // Ayni makinede farkli olcumler sorun degil.
        std::vector<LabeledWindow> distinct;
        for (int i = 0; i < 3; ++i)
            distinct.push_back(sample("u" + std::to_string(i), Provenance::RealHardware, CalibrationLabel::GpuBound,
                                      h3[static_cast<std::size_t>(i)], "run-u" + std::to_string(i), 60 + i));
        EF_CHECK(validate_split_integrity(distinct).ok);
    }
    // 4) Grupsuz gercek ornek: sizinti DEGIL (bulgu 4); engellemeyen not, ornek kapiya uygun degil.
    auto ungrouped = clean;
    ungrouped.push_back(sample("c1", Provenance::RealHardware, CalibrationLabel::Healthy, "", "", 5));
    const auto ru = validate_split_integrity(ungrouped);
    EF_CHECK(ru.ok && ru.issues.empty() && has_note(ru, "ungrouped_real_sample"));
    EF_CHECK(!gate_eligible(ungrouped.back()));
    // Kapi: diger kurallar saglansa da sizinti varsa kapali.
    EF_CHECK(release_status(passing_stats()).ready);
    r = validate_split_integrity(leak);
    const auto g = release_status(passing_stats(), r);
    EF_CHECK(!g.ready && g.reason_key == "diag.gate.split_leakage");
    // Holdout makinesinin her kosusu holdout'ta.
    for (int i = 0; i < 40; ++i) {
        const auto x = sample("h" + std::to_string(i), Provenance::RealHardware, CalibrationLabel::Healthy, hold[0],
                              "run-" + std::to_string(i % 5), 10 + i);
        EF_CHECK(split_for(x) == Split::Holdout);
    }
}

EF_TEST(calibration_x20_label_taxonomy_maps_without_changing_classifier) {
    EF_CHECK(expected_compute_class(CalibrationLabel::CpuMainThread) == ComputeClass::CpuOrEngineBound);
    EF_CHECK(expected_compute_class(CalibrationLabel::Vsync) == ComputeClass::FrameCapOrPresentWait);
    EF_CHECK(expected_compute_class(CalibrationLabel::FpsCap) == ComputeClass::FrameCapOrPresentWait);
    EF_CHECK(expected_compute_class(CalibrationLabel::EngineFrameLimit) == ComputeClass::FrameCapOrPresentWait);
    for (const auto l : {CalibrationLabel::RamPressure, CalibrationLabel::StorageWait, CalibrationLabel::ThermalPower,
                         CalibrationLabel::Unknown, CalibrationLabel::Legacy})
        EF_CHECK(!expected_compute_class(l).has_value());
    EF_CHECK(label_domain(CalibrationLabel::RamPressure) == LabelDomain::MemoryStorage);
    EF_CHECK(label_domain(CalibrationLabel::ThermalPower) == LabelDomain::PlatformLimits);
    for (const auto c : {ComputeClass::GpuBound, ComputeClass::CpuOrEngineBound, ComputeClass::FrameCapOrPresentWait,
                         ComputeClass::Mixed, ComputeClass::Healthy, ComputeClass::Insufficient}) {
        const auto f = calibration_label_from(to_string(c));
        EF_CHECK(f.has_value() && expected_compute_class(*f) == c);
    }
    EF_CHECK(!calibration_label_from("legacy").has_value() && !calibration_label_from("gpu_bottleneck").has_value());
}

EF_TEST(calibration_x20_confusion_and_per_model_report) {
    const auto hold = machines_with(true, 1);
    std::vector<LabeledWindow> set;
    set.push_back(sample("g", Provenance::SyntheticFixture, CalibrationLabel::GpuBound, hold[0], "r1", 1));
    set.push_back(sample("h", Provenance::SyntheticFixture, CalibrationLabel::Healthy, hold[0], "r2", 2));
    set.push_back(sample("m", Provenance::SyntheticFixture, CalibrationLabel::RamPressure, hold[0], "r3", 3));
    set.push_back(sample("t", Provenance::SyntheticFixture, CalibrationLabel::ThermalPower, hold[0], "r4", 4));
    const auto s = evaluate(set, Split::Holdout);
    EF_CHECK(s.total == 2 && s.out_of_scope == 2);
    std::size_t sum = 0;
    for (const auto& row : s.confusion)
        for (const auto n : row) sum += n;
    EF_CHECK(sum == s.total);
    EF_CHECK(s.confusion[static_cast<std::size_t>(ComputeClass::GpuBound)][static_cast<std::size_t>(ComputeClass::GpuBound)] == 1);
    const auto models = evaluate_models(set, Split::Holdout);
    EF_CHECK(models.size() == 3 && models[0].model == "compute" && models[0].scored && models[0].labeled == 2);
    EF_CHECK(models[1].model == "memory_storage" && !models[1].scored && models[1].labeled == 1 && models[1].stats.total == 0);
    EF_CHECK(models[2].model == "platform_limits" && !models[2].scored && models[2].labeled == 1);
}

namespace {
std::string v2_sample(const std::string& id, const std::string& method = "controlled_experiment",
                      const std::string& label = "vsync") {
    return R"({"id":")" + id + R"(","provenance":"real","label":")" + label + R"(","label_method":")" + method +
           R"(","machine_group":"m-a","machine_digest":"0a1b2c3d","run_group":"run-)" + id +
           R"(","cpu_vendor":"amd","gpu_vendor":"nvidia",)" +
           R"("form_factor":"laptop","storage_kind":"hdd","avg_fps":60.0,"frames":240,"refresh_hz":60.0,"vsync":"on",)" +
           R"("metrics":{"gpu_engine_3d_pct":40.0,"cpu_max_core_pct":45.0}})";
}
std::string v2_doc(const std::string& s) { return R"({"schema_version":2,"format":"diagcal_v2","samples":[)" + s + "]}"; }
}  // namespace

EF_TEST(calibration_x20_io_v2_roundtrip_v1_compat_and_rule_derived_rejected) {
    const auto doc = parse_calibration(v2_doc(v2_sample("r1") + "," + v2_sample("r2", "expert_review", "ram_pressure")));
    EF_CHECK(doc.ok() && doc.value().schema_version == 2 && doc.value().samples.size() == 2);
    const auto& a = doc.value().samples[0];
    EF_CHECK(a.fine_label == CalibrationLabel::Vsync && a.label == ComputeClass::FrameCapOrPresentWait);
    EF_CHECK(a.label_method == LabelMethod::ControlledExperiment && a.machine_group == "m-a" && a.run_group == "run-r1");
    EF_CHECK(a.family.cpu == CpuVendor::Amd && a.family.form == FormFactor::Laptop && a.family.storage == StorageKind::Hdd);
    EF_CHECK(a.machine_digest == "0a1b2c3d" && gate_eligible(a));
    // Bulgu 3: v2'de machine_digest yoksa okunur ama kapiya uygun degil.
    {
        auto nd = v2_sample("r9");
        const std::string dk = R"("machine_digest":"0a1b2c3d",)";
        nd.erase(nd.find(dk), dk.size());
        const auto ndoc = parse_calibration(v2_doc(nd));
        EF_CHECK(ndoc.ok() && ndoc.value().samples[0].machine_digest.empty());
        EF_CHECK(!gate_eligible(ndoc.value().samples[0]));
    }
    EF_CHECK(doc.value().samples[1].fine_label == CalibrationLabel::RamPressure);
    const auto text = write_calibration(doc.value());
    EF_CHECK(text.ok() && text.value().find("diagcal_v2") != std::string::npos);
    const auto again = parse_calibration(text.value());
    EF_CHECK(again.ok() && again.value().samples[0].fine_label == CalibrationLabel::Vsync);
    EF_CHECK(again.value().samples[1].label_method == LabelMethod::ExpertReview);
    EF_CHECK(again.value().samples[0].machine_digest == "0a1b2c3d" && gate_eligible(again.value().samples[0]));
    // rule_derived acikca reddedilir.
    const auto rd = parse_calibration(v2_doc(v2_sample("r3", "rule_derived")));
    EF_CHECK(!rd.ok() && rd.code() == ErrorCode::ParseError && rd.error().detail.find("rule_derived") != std::string::npos);
    // v2'de etiket yontemi zorunlu.
    auto no_method = v2_sample("r4");
    const std::string method_key = R"("label_method":"controlled_experiment",)";
    no_method.erase(no_method.find(method_key), method_key.size());
    const auto nm = parse_calibration(v2_doc(no_method));
    EF_CHECK(!nm.ok() && nm.code() == ErrorCode::MissingValue);
    // Gecersiz grup karakteri.
    auto bad_group = v2_sample("r5");
    bad_group.replace(bad_group.find("m-a"), 3, "M A");
    EF_CHECK(!parse_calibration(v2_doc(bad_group)).ok());
    // v1 belge hala okunur; v2 alani v1'de bilinmeyen alandir; v1 gercek ornek kapiya uygun degildir.
    const auto v1 = parse_calibration(
        R"({"schema_version":1,"format":"diagcal_v1","samples":[{"id":"x","provenance":"real","label":"gpu_bound","avg_fps":120,"frames":240}]})");
    EF_CHECK(v1.ok() && v1.value().schema_version == 1);
    EF_CHECK(v1.value().samples[0].fine_label == CalibrationLabel::Legacy && !gate_eligible(v1.value().samples[0]));
    const auto v1_out = write_calibration(v1.value());
    EF_CHECK(v1_out.ok() && v1_out.value().find("diagcal_v1") != std::string::npos);
    const auto v1_with_v2_key = parse_calibration(
        R"({"schema_version":1,"format":"diagcal_v1","samples":[{"id":"x","provenance":"real","label":"gpu_bound","avg_fps":120,"frames":240,"machine_group":"m"}]})");
    EF_CHECK(!v1_with_v2_key.ok() && v1_with_v2_key.code() == ErrorCode::ParseError);
    EF_CHECK(parse_calibration(R"({"schema_version":2,"format":"diagcal_v1","samples":[]})").code() == ErrorCode::ParseError);
    EF_CHECK(parse_calibration(R"({"schema_version":3,"format":"diagcal_v2","samples":[]})").code() == ErrorCode::SchemaTooNew);
}

EF_TEST(calibration_x20_export_v2_guards) {
    SceneCalibrationSample s;
    s.id = "app-x20-1";
    s.label = ComputeClass::GpuBound;
    s.avg_fps = 120.0;
    s.frames = 240.0;
    s.gpu_engine_3d_pct = 97.0;
    s.cpu_max_core_pct = 55.0;
    s.label_method = LabelMethod::ControlledExperiment;
    s.machine_group = "op-m1";
    s.run_group = "op-r1";
    s.fine_label = CalibrationLabel::GpuBound;
    const auto t = append_sample_json("", s);
    EF_CHECK(t.ok() && t.value().find("diagcal_v2") != std::string::npos);
    const auto back = parse_calibration(t.value());
    EF_CHECK(back.ok() && back.value().samples[0].machine_group == "op-m1");
    EF_CHECK(back.value().samples[0].family.cpu == CpuVendor::Unknown);  // aile tahmin edilmez
    // Bulgu 3: operator ozet vermediyse ornek yazilir ama kapiya uygun degil (otomatik parmak izi yok).
    EF_CHECK(back.value().samples[0].machine_digest.empty() && !gate_eligible(back.value().samples[0]));
    auto with_digest = s;
    with_digest.id = "app-x20-d";
    with_digest.machine_digest = "c0ffee1234";
    const auto td = append_sample_json("", with_digest);
    EF_CHECK(td.ok() && td.value().find(R"("machine_digest":"c0ffee1234")") != std::string::npos);
    const auto bd = parse_calibration(td.value());
    EF_CHECK(bd.ok() && bd.value().samples[0].machine_digest == "c0ffee1234" && gate_eligible(bd.value().samples[0]));
    for (const char* bad_digest : {"C0FFEE12", "abc", "zzzzzzzz"}) {
        auto bdg = with_digest;
        bdg.machine_digest = bad_digest;
        EF_CHECK(!append_sample_json("", bdg).ok());  // gecersiz bicim: yazma yok
    }
    // Ince etiket v1 etiketle celisirse reddedilir.
    auto mismatch = s;
    mismatch.fine_label = CalibrationLabel::Healthy;
    EF_CHECK(!make_labeled_window(mismatch).ok());
    // Bu yol FPS siniri/VSync olcmez: sinir ailesi etiketi uydurma veri olur -> reddedilir.
    auto capped = s;
    capped.label = ComputeClass::FrameCapOrPresentWait;
    capped.fine_label = CalibrationLabel::Vsync;
    const auto cw = make_labeled_window(capped);
    EF_CHECK(!cw.ok() && cw.error().detail == "cap_state_unmeasured");
    // v1 dosyasina v2 ornek karismaz.
    auto v1s = s;
    v1s.id = "app-x20-v1";
    v1s.label_method = LabelMethod::Unspecified;
    v1s.machine_group.clear();
    v1s.run_group.clear();
    v1s.fine_label = CalibrationLabel::Legacy;
    const auto f1 = append_sample_json("", v1s);
    EF_CHECK(f1.ok() && f1.value().find("diagcal_v1") != std::string::npos);
    const auto mix = append_sample_json(f1.value(), s);
    EF_CHECK(!mix.ok() && mix.code() == ErrorCode::MissingValue);
    // Okuyucunun reddedecegi grup adi yazilmaz.
    auto bad = s;
    bad.machine_group = "Bad Name";
    EF_CHECK(!append_sample_json("", bad).ok());
}

EF_TEST(history_diagnosis_version_stamp_roundtrip_and_old_records) {
    using ef::core::insights::History;
    using ef::core::insights::HistoryEntry;
    History h;
    HistoryEntry e;
    e.source = "builtin";
    e.diagnosis_version = diagnosis_version_stamp();
    h.add(e);
    const auto back = History::parse(h.serialize());
    EF_CHECK(back.ok() && back.value().entries()[0].diagnosis_version == "compute_v1:uncalibrated");
    EF_CHECK(h.export_json(1).find("\"diagnosis_version\"") != std::string::npos);
    const auto old = History::parse(R"({"schema_version":1,"entries":[{"source":"builtin","metrics":{}}]})");
    EF_CHECK(old.ok() && old.value().entries()[0].diagnosis_version.empty());
    const auto bad = History::parse(R"({"schema_version":1,"entries":[{"source":"builtin","diagnosis_version":"C:\\x y"}]})");
    EF_CHECK(bad.ok() && bad.value().entries()[0].diagnosis_version.empty());
    History plain;
    plain.add(HistoryEntry{});
    EF_CHECK(plain.serialize().find("diagnosis_version") == std::string::npos);
}

EF_TEST(calibration_x20_fp_rate_not_diluted_by_synthetic_or_ineligible) {
    // EF-X20 dogrulama bulgusu 1: 30 uygun gercek holdout ornegi, 3 yanlis pozitif (%10) -> kapali.
    // 30 dogru siniflanan sentetik (ve uygun olmayan gercek) ornek eklemek orani seyreltip kapiyi ACAMAZ.
    const auto h3 = machines_with(true, 3);
    EF_CHECK(h3.size() == 3);
    std::vector<LabeledWindow> real;
    int k = 0;
    for (int i = 0; i < 30; ++i, ++k) {
        const auto& m = h3[static_cast<std::size_t>(i % 3)];
        const auto l = i < 10 ? CalibrationLabel::GpuBound : CalibrationLabel::Healthy;
        auto x = sample("e" + std::to_string(i), Provenance::RealHardware, l, m, "re" + std::to_string(i), k);
        if (i >= 27) x.window = win(8.0 + 0.01 * k, 0.4, 97.0, 55.0);  // healthy etiket, GPU sinirli pencere: FP
        real.push_back(x);
    }
    auto padded = real;
    for (int i = 0; i < 30; ++i, ++k)
        padded.push_back(sample("s" + std::to_string(i), Provenance::SyntheticFixture, CalibrationLabel::Healthy,
                                h3[static_cast<std::size_t>(i % 3)], "rs" + std::to_string(i), k));
    const auto hr = evaluate(real, Split::Holdout);
    const auto hp = evaluate(padded, Split::Holdout);
    EF_CHECK(validate_split_integrity(padded).ok);
    EF_CHECK(hr.real == 30 && eligible_real(hr) == 30 && hr.distinct_machines == 3);
    EF_CHECK(hr.positives == 10 && hr.negatives == 20);
    EF_CHECK(hr.eligible_false_positive == 3 && hp.eligible_false_positive == 3);
    EF_CHECK(hp.total == 60 && hp.false_positive == 3);
    // Eski (karisik payda) hesap 3/60 = %5 olurdu: kapiyi acardi. Senaryonun gecerli oldugunu dogrula.
    EF_CHECK(static_cast<double>(hp.false_positive) / static_cast<double>(hp.total) <= kMaxFalsePositiveRate);
    const auto sr = release_status(hr);
    const auto sp = release_status(hp, validate_split_integrity(padded));
    EF_CHECK(!sr.ready && sr.reason_key == "diag.gate.false_positive_rate");
    EF_CHECK(!sp.ready && sp.reason_key == "diag.gate.false_positive_rate");
    // Uygun olmayan (ozetsiz) gercek dolgu da seyreltmez.
    auto ineligible_pad = real;
    for (int i = 0; i < 30; ++i, ++k) {
        auto x = sample("n" + std::to_string(i), Provenance::RealHardware, CalibrationLabel::Healthy,
                        h3[static_cast<std::size_t>(i % 3)], "rn" + std::to_string(i), k);
        x.machine_digest.clear();
        ineligible_pad.push_back(x);
    }
    const auto hi = evaluate(ineligible_pad, Split::Holdout);
    EF_CHECK(hi.real == 60 && eligible_real(hi) == 30 && hi.real_missing_digest == 30);
    EF_CHECK(release_status(hi).reason_key == "diag.gate.false_positive_rate");
    // Gomulu baslik yolu: payda uygun gercek holdout; holdout_total ile seyreltilmez.
    ValidatedCalibration v{};
    v.present = true;
    v.real_holdout = 30;
    v.holdout_total = 60;
    v.false_positive = 3;
    v.holdout_machines = 3;
    v.holdout_positives = 10;
    v.holdout_negatives = 20;
    const auto vs = release_status_from_validated(v);
    EF_CHECK(!vs.ready && vs.reason_key == "diag.gate.false_positive_rate");
    v.false_positive = 1;  // 1/30 <= %5
    EF_CHECK(release_status_from_validated(v).ready);
}

EF_TEST(calibration_x20_v1_real_dataset_is_ineligible_not_leakage) {
    // EF-X20 dogrulama bulgusu 4: v1 (grupsuz) gercek ornek sizinti degildir; neden anahtari
    // no_real_holdout / label_or_group_missing olur, split_leakage degil.
    const auto one = parse_calibration(
        R"({"schema_version":1,"format":"diagcal_v1","samples":[{"id":"x","provenance":"real","label":"healthy","avg_fps":120,"frames":240}]})");
    EF_CHECK(one.ok());
    const auto ri = validate_split_integrity(one.value().samples);
    EF_CHECK(ri.ok && ri.issues.empty() && has_note(ri, "ungrouped_real_sample"));
    const auto split = split_of("x");
    const auto st1 = release_status(evaluate(one.value().samples, split), ri);
    EF_CHECK(!st1.ready && st1.reason_key == "diag.gate.no_real_holdout");
    std::vector<LabeledWindow> v1;
    for (int i = 0; v1.size() < kMinRealHoldout + 5 && i < 4000; ++i) {
        const std::string id = "v1-" + std::to_string(i);
        if (split_of(id) != Split::Holdout) continue;
        auto x = sample(id, Provenance::RealHardware, CalibrationLabel::Healthy, "", "", i);
        x.fine_label = CalibrationLabel::Legacy;
        x.label_method = LabelMethod::Unspecified;
        v1.push_back(x);
    }
    EF_CHECK(v1.size() == kMinRealHoldout + 5);
    const auto integrity = validate_split_integrity(v1);
    EF_CHECK(integrity.ok && integrity.notes.size() == v1.size());
    const auto h = evaluate(v1, Split::Holdout);
    EF_CHECK(h.real == v1.size() && h.real_ineligible == v1.size() && eligible_real(h) == 0);
    const auto st = release_status(h, integrity);
    EF_CHECK(!st.ready && st.reason_key == "diag.gate.label_or_group_missing");
    // Gercek sizinti hala engelleyicidir.
    auto leak = v1;
    auto c = leak[0];
    c.id = "v1-copy";
    c.machine_group = machines_with(false, 1)[0];
    c.run_group = "rc";
    c.machine_digest = digest_of(c.machine_group);
    leak.push_back(c);
    const auto li = validate_split_integrity(leak);
    EF_CHECK(!li.ok && has_issue(li, "duplicate_window_multi_machine"));
    EF_CHECK(release_status(h, li).reason_key == "diag.gate.split_leakage");
}
