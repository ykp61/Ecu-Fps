#include "core/perf/experiment.h"

namespace ef::core::perf {

std::vector<std::optional<Arm>> abba_schedule(int pairs, int warmup_runs) {
    std::vector<std::optional<Arm>> s;
    for (int i = 0; i < warmup_runs; ++i) s.push_back(std::nullopt);  // isinma: puanlanmaz
    for (int i = 0; i < pairs; ++i) {
        const bool even = i % 2 == 0;
        s.push_back(even ? Arm::A : Arm::B);
        s.push_back(even ? Arm::B : Arm::A);
    }
    return s;
}

std::string_view to_string(ExperimentVerdict v) noexcept {
    switch (v) {
        case ExperimentVerdict::Improved: return "improved";
        case ExperimentVerdict::Regressed: return "regressed";
        case ExperimentVerdict::Uncertain: return "uncertain";
        case ExperimentVerdict::ConditionsDiffer: return "conditions_differ";
        case ExperimentVerdict::InsufficientRuns: return "insufficient_runs";
    }
    return "uncertain";
}

ExperimentResult evaluate_experiment(const std::vector<RunOutcome>& runs, bool quality_tradeoff) {
    ExperimentResult r;
    r.quality_tradeoff = quality_tradeoff;
    std::vector<double> a, b;
    std::string fp;
    bool fp_set = false, differ = false;
    for (const auto& x : runs) {
        if (!x.valid) continue;
        if (!fp_set) fp = x.condition_fingerprint, fp_set = true;
        else if (x.condition_fingerprint != fp) differ = true;
        (x.arm == Arm::A ? a : b).push_back(x.avg_fps);
    }
    r.valid_a = a.size();
    r.valid_b = b.size();
    r.fps = compare_metric("avg_fps", Direction::HigherIsBetter, a, b);
    if (differ) {
        r.verdict = ExperimentVerdict::ConditionsDiffer;  // farkli kosullarda iyilesme karari yok
        return r;
    }
    if (a.size() < kMinRunsPerArm || b.size() < kMinRunsPerArm) {
        r.verdict = ExperimentVerdict::InsufficientRuns;
        return r;
    }
    if (r.fps.verdict != Verdict::Significant || !r.fps.improved) {
        r.verdict = ExperimentVerdict::Uncertain;
        return r;
    }
    r.verdict = *r.fps.improved ? ExperimentVerdict::Improved : ExperimentVerdict::Regressed;
    return r;
}

}  // namespace ef::core::perf
