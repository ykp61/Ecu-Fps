#include "core/perf/apply_progress.h"

namespace ef::core::perf {

ApplyPhase apply_phase(const recovery::Progress& p) noexcept {
    using recovery::Phase;
    switch (p.phase) {
        case Phase::Capturing: return ApplyPhase::Prepare;
        case Phase::Applying: return p.group == "plan.restore_point" ? ApplyPhase::RestorePoint : (p.step ? ApplyPhase::Apply : ApplyPhase::Prepare);
        case Phase::Verifying:
        case Phase::Done: return ApplyPhase::Verify;
        case Phase::RollingBack: return ApplyPhase::Apply;
    }
    return ApplyPhase::Prepare;
}

std::string_view apply_phase_key(ApplyPhase p) noexcept {
    switch (p) {
        case ApplyPhase::Prepare: return "perf.progress.phase.prepare";
        case ApplyPhase::RestorePoint: return "perf.progress.phase.restore_point";
        case ApplyPhase::Apply: return "perf.progress.phase.apply";
        case ApplyPhase::Verify: return "perf.progress.phase.verify";
    }
    return "perf.progress.phase.prepare";
}

bool has_step_line(const recovery::Progress& p) noexcept {
    return p.step >= 1 && p.step <= p.steps && !p.group.empty();
}

}  // namespace ef::core::perf
