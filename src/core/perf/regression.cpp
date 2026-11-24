#include "core/perf/regression.h"

namespace ef::core::perf {

std::string_view to_string(NextStep s) noexcept {
    switch (s) {
        case NextStep::KeepAndReportImproved: return "keep_and_report_improved";
        case NextStep::KeepNoClaim: return "keep_no_claim";
        case NextStep::Repeat: return "repeat";
        case NextStep::RollbackGroup: return "rollback_group";
        case NextStep::VerifyRollbackValue: return "verify_rollback_value";
        case NextStep::RemeasureAfterRollback: return "remeasure_after_rollback";
        case NextStep::Replan: return "replan";
        case NextStep::Done: return "done";
    }
    return "done";
}

std::vector<NextStep> next_steps(const ExperimentResult& r, int attempts, const RegressionPolicy& p) {
    switch (r.verdict) {
        case ExperimentVerdict::Improved:
            // Kalite takasiyla gelen kazanc "iyilesme" diye raporlanmaz (GAME-04 etiketi ayri).
            return {r.quality_tradeoff ? NextStep::KeepNoClaim : NextStep::KeepAndReportImproved, NextStep::Done};
        case ExperimentVerdict::Regressed:
            return {NextStep::RollbackGroup, NextStep::VerifyRollbackValue, NextStep::RemeasureAfterRollback, NextStep::Done};
        case ExperimentVerdict::ConditionsDiffer: return {NextStep::Replan};
        case ExperimentVerdict::Uncertain:
        case ExperimentVerdict::InsufficientRuns:
            if (attempts <= p.max_repeats) return {NextStep::Repeat};
            return {NextStep::KeepNoClaim, NextStep::Done};
    }
    return {NextStep::Done};
}

}  // namespace ef::core::perf
