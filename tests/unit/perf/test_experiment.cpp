#include "core/perf/experiment.h"
#include "ef_test.h"

using namespace ef::core::perf;

namespace {
std::vector<RunOutcome> runs(double a, double b, double noise, int n, const char* fp_b = "fp1") {
    std::vector<RunOutcome> v;
    for (int i = 0; i < n; ++i) {
        const double e = ((i % 3) - 1) * noise;
        v.push_back({Arm::A, "fp1", true, a + e});
        v.push_back({Arm::B, fp_b, true, b - e});
    }
    return v;
}
}  // namespace

EF_TEST(experiment_abba_schedule_balanced) {
    const auto s = abba_schedule(4, 1);
    EF_CHECK(s.size() == 9 && !s[0]);  // isinma puanlanmaz
    int a = 0, b = 0;
    for (const auto& x : s)
        if (x) (*x == Arm::A ? a : b) += 1;
    EF_CHECK(a == 4 && b == 4 && *s[1] == Arm::A && *s[2] == Arm::B && *s[3] == Arm::B && *s[4] == Arm::A);
}

EF_TEST(experiment_run_level_not_frames) {
    // Tek kosu cifti: binlerce kare olsa bile kosu sayisi 1 -> karar yok.
    auto r = evaluate_experiment(runs(100, 140, 0.5, 1));
    EF_CHECK(r.verdict == ExperimentVerdict::InsufficientRuns && r.valid_a == 1);
    r = evaluate_experiment(runs(100, 110, 0.5, 5));
    EF_CHECK(r.verdict == ExperimentVerdict::Improved && r.fps.improved && *r.fps.improved);
    r = evaluate_experiment(runs(100, 100.3, 3.0, 6));  // gurultu icinde
    EF_CHECK(r.verdict == ExperimentVerdict::Uncertain);
    r = evaluate_experiment(runs(110, 100, 0.5, 5));
    EF_CHECK(r.verdict == ExperimentVerdict::Regressed);
}

EF_TEST(experiment_conditions_differ_rejected) {
    auto v = runs(100, 120, 0.5, 5, "fp2");  // B kosulari farkli cozunurluk/surucu
    EF_CHECK(evaluate_experiment(v).verdict == ExperimentVerdict::ConditionsDiffer);
    v = runs(100, 120, 0.5, 5);
    v[3].valid = false;  // gecersiz kosu sayilmaz
    const auto r = evaluate_experiment(v, true);
    EF_CHECK(r.valid_b == 4 && r.verdict == ExperimentVerdict::InsufficientRuns && r.quality_tradeoff);
}
