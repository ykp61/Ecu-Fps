#include "platform/windows/install/program_remover.h"

namespace ef::platform::install {

core::recovery::ProgramRemover::Outcome program_outcome(const SetupRun& run) {
    core::recovery::ProgramRemover::Outcome o;
    o.detail = run.detail;
    if (run.elevation == core::ElevationOutcome::Denied) {
        o.denied = true;
        return o;
    }
    if (run.elevation != core::ElevationOutcome::Started) return o;  // baslatilamadi: hicbir sey yapilmadi
    o.attempted = true;
    if (!run.reply) {
        o.detail = "no reply from setup worker; result unknown";
        return o;
    }
    if (!run.reply->error.empty()) {
        o.detail = run.reply->error;
        return o;
    }
    o.removed = run.reply->result.ok;
    o.reboot_pending = o.removed && run.reply->result.reboot_pending;
    return o;
}

core::recovery::ProgramRemover::Outcome WorkerProgramRemover::remove_program() {
    return program_outcome(run_setup_worker_request(core::setup::SetupKind::Uninstall, {}));
}

}  // namespace ef::platform::install
