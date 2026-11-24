#include "core/perf/regression.h"
#include "ef_test.h"

using namespace ef::core::perf;

namespace {
ExperimentResult res(ExperimentVerdict v, bool q = false) {
    ExperimentResult r;
    r.verdict = v;
    r.quality_tradeoff = q;
    return r;
}
}  // namespace

EF_TEST(regression_rollback_then_verify_and_remeasure) {
    const auto s = next_steps(res(ExperimentVerdict::Regressed), 1);
    EF_CHECK(s.size() == 4 && s[0] == NextStep::RollbackGroup && s[1] == NextStep::VerifyRollbackValue && s[2] == NextStep::RemeasureAfterRollback);
}

EF_TEST(regression_uncertain_repeats_then_no_claim) {
    EF_CHECK(next_steps(res(ExperimentVerdict::Uncertain), 1)[0] == NextStep::Repeat);
    EF_CHECK(next_steps(res(ExperimentVerdict::InsufficientRuns), 2)[0] == NextStep::Repeat);
    const auto last = next_steps(res(ExperimentVerdict::Uncertain), 3);
    EF_CHECK(last[0] == NextStep::KeepNoClaim);  // basari iddiasi yok, otomatik geri alma da yok
    EF_CHECK(next_steps(res(ExperimentVerdict::Improved), 1)[0] == NextStep::KeepAndReportImproved);
    EF_CHECK(next_steps(res(ExperimentVerdict::Improved, true), 1)[0] == NextStep::KeepNoClaim);  // kalite takasi
    EF_CHECK(next_steps(res(ExperimentVerdict::ConditionsDiffer), 1)[0] == NextStep::Replan);
}
