#include "core/diagnosis/report.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;

namespace {
ReleaseStatus open_gate() {
    ReleaseStatus s;
    s.ready = true;
    s.synthetic_only = false;
    s.reason_key = "diag.gate.ready";
    return s;
}
ReleaseStatus closed_gate() {
    ReleaseStatus s;
    s.ready = false;
    s.synthetic_only = true;
    s.reason_key = "diag.gate.no_real_holdout";
    return s;
}
}  // namespace

EF_TEST(report_no_data_never_says_no_bottleneck) {
    auto r = make_report(std::nullopt, std::nullopt, {}, {}, open_gate());
    EF_CHECK(r.overall == ReportStatus::Insufficient && !r.has_runtime_evidence);
    // Envanter temiz ama calisma verisi yok: yine "sorun yok" degil
    std::vector<ComponentFinding> comps{{Component::Ram, Verdict::NoIssueFound, {"speed_at_rated"}, "", false},
                                        {Component::Cpu, Verdict::Abstain, {}, "no_runtime_evidence", false}};
    r = make_report(std::nullopt, std::nullopt, {}, comps, open_gate());
    EF_CHECK(r.overall == ReportStatus::Insufficient);
    EF_CHECK(r.items[1].status == ReportStatus::Insufficient && r.items[1].counter[0] == "no_runtime_evidence");
}

EF_TEST(report_runtime_healthy_and_limits_have_reasons) {
    ComputeVerdict h;
    h.cls = ComputeClass::Healthy;
    h.reason = ComputeConfidence::NoLimitingSignal;
    auto r = make_report(h, std::nullopt, {}, {}, open_gate());
    EF_CHECK(r.overall == ReportStatus::Healthy && r.items[0].confidence == to_string(ComputeConfidence::NoLimitingSignal));
    ComputeVerdict g;
    g.cls = ComputeClass::GpuBound;
    g.is_fault = false;
    g.reason = ComputeConfidence::MultipleCorroborated;
    g.supporting = {"gpu_util_high"};
    r = make_report(g, std::nullopt, {}, {}, open_gate());
    EF_CHECK(r.overall == ReportStatus::Limiting && r.items[0].physical_limit && r.items[0].fix == FixGroup::Hardware);
    EF_CHECK(!r.items[0].supporting.empty() && !r.items[0].confidence.empty());  // neden var, yuzde yok
    std::vector<ComponentFinding> comps{{Component::Ram, Verdict::Limiting, {"xmp:4800<6000"}, "", true}};
    r = make_report(h, std::nullopt, {}, comps, open_gate());
    EF_CHECK(r.overall == ReportStatus::Limiting && r.items[1].fix == FixGroup::Bios && !r.items[1].physical_limit);
}

EF_TEST(report_closed_release_gate_never_publishes_class) {
    // BM-02: kapi kapaliyken gecerli bir siniflandirma bile yayinlanmaz; rapor durumu acikca tasir.
    ComputeVerdict g;
    g.cls = ComputeClass::GpuBound;
    g.is_fault = false;
    g.reason = ComputeConfidence::MultipleCorroborated;
    g.supporting = {"gpu_util_high"};
    g.counter = {"cpu_metric_missing"};
    const auto r = make_report(g, std::nullopt, {}, {}, closed_gate());
    EF_CHECK(!r.release_ready && r.release_key == "diag.gate.no_real_holdout");
    EF_CHECK(!r.has_runtime_evidence && r.overall == ReportStatus::Insufficient);
    EF_CHECK(r.items.size() == 1 && r.items[0].area == "compute");
    EF_CHECK(r.items[0].status == ReportStatus::Insufficient);
    EF_CHECK(r.items[0].confidence == "calibration_unverified");
    EF_CHECK(r.items[0].supporting.size() == 1 && r.items[0].supporting[0] == "gpu_util_high");
    EF_CHECK(r.items[0].counter.size() == 2 && r.items[0].counter[1] == "diagnosis_calibration_unverified");
    // Kapi acikken ayni girdi normal rapor uretir (kapi gercek bir kez acilirsa davranis bozulmaz).
    const auto o = make_report(g, std::nullopt, {}, {}, open_gate());
    EF_CHECK(o.release_ready && o.release_key == "diag.gate.ready" && o.overall == ReportStatus::Limiting);
}
