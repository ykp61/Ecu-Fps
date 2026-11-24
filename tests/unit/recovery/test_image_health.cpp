// EF-X02: goruntu onarimi karar akisi (sahte servis API; gercek DISM calistirilmaz).
#include "core/broker/admin.h"
#include "core/maintenance/image_health.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::maintenance;

namespace {
struct Fake final : ImageServicingApi {
    std::optional<bool> reboot = false;
    std::vector<ImageHealth> states{ImageHealth::Repairable, ImageHealth::Healthy};
    std::uint32_t restore_hr = 0;
    bool unsupported = false;
    int checks = 0, restores = 0;
    std::optional<bool> pending_reboot() override { return reboot; }
    std::vector<bool> scans;
    bool rp_ok = true;
    bool create_restore_point() override { return rp_ok; }
    ef::contracts::Result<ImageHealth> check(bool scan) override {
        scans.push_back(scan);
        if (unsupported) return ef::contracts::Result<ImageHealth>::fail(ef::contracts::ErrorCode::Unsupported);
        const auto s = states[std::min<std::size_t>(checks, states.size() - 1)];
        ++checks;
        return s;
    }
    std::uint32_t restore_local_only() override {
        ++restores;
        return restore_hr;
    }
};
}  // namespace

EF_TEST(x02_t01_healthy_or_nonrepairable_never_starts_repair) {
    Fake f;
    f.states = {ImageHealth::Healthy};
    recovery::MemoryJournalFs fs;
    EF_CHECK(run_image_repair(f, fs, 1).outcome == MaintOutcome::Healthy && f.restores == 0);
    f.states = {ImageHealth::NonRepairable};
    EF_CHECK(run_image_repair(f, fs, 1).outcome == MaintOutcome::NonRepairable && f.restores == 0);
}

EF_TEST(x02_t03_source_missing_reboot_and_pending_are_distinct) {
    recovery::MemoryJournalFs fs;
    Fake pending;
    pending.reboot = true;
    EF_CHECK(run_image_repair(pending, fs, 1).outcome == MaintOutcome::PendingReboot && pending.checks == 0 && pending.restores == 0);
    Fake unknown;
    unknown.reboot = std::nullopt;  // okunamayan servis durumu: baslamaz
    EF_CHECK(run_image_check(unknown).outcome == MaintOutcome::PendingReboot);
    Fake src;
    src.restore_hr = 0x800F081Fu;
    EF_CHECK(run_image_repair(src, fs, 1).outcome == MaintOutcome::SourceMissing);
    Fake rb;
    rb.restore_hr = 3010u;
    EF_CHECK(run_image_repair(rb, fs, 1).outcome == MaintOutcome::RebootRequired);
    Fake none;
    none.unsupported = true;
    EF_CHECK(run_image_check(none).outcome == MaintOutcome::Unsupported);
}

EF_TEST(x02_t04_success_only_after_passing_recheck) {
    recovery::MemoryJournalFs fs;
    Fake ok;
    EF_CHECK(run_image_repair(ok, fs, 1).outcome == MaintOutcome::Repaired && ok.checks == 2);
    Fake still;
    still.states = {ImageHealth::Repairable, ImageHealth::Repairable};
    EF_CHECK(run_image_repair(still, fs, 1).outcome == MaintOutcome::RepairNotVerified);
    EF_CHECK(!fs.read(kRepairJobFile).ok());
}

EF_TEST(x02_interrupted_repair_is_reported_next_time) {
    recovery::MemoryJournalFs fs;
    (void)fs.write_atomic(kRepairJobFile, R"({"started_unix_ms":5})");  // onceki oturumda yarida kaldi
    Fake f;
    const auto r = run_image_repair(f, fs, 9);
    EF_CHECK(r.previous_interrupted && r.outcome == MaintOutcome::Repaired && !fs.read(kRepairJobFile).ok());
}

EF_TEST(x02_t02_broker_accepts_only_fixed_job_ids) {
    using ef::contracts::OperationId;
    using ef::contracts::OperationScope;
    ef::contracts::SettingOperation op{OperationId::MaintenanceJob, OperationScope::Machine, kImageCheckTarget, kMaintenanceValue, false, false};
    EF_CHECK(broker::admin_operation_allowed(op));
    op.target = kImageRepairTarget;
    EF_CHECK(broker::admin_operation_allowed(op));
    op.target = "maint:sfc /scannow";
    EF_CHECK(!broker::admin_operation_allowed(op));  // serbest komut yok
    op.target = kImageCheckTarget;
    op.new_value = "C:\\\\src";
    EF_CHECK(!broker::admin_operation_allowed(op));
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Denied;  // UAC reddi: broker hic calismadi
    EF_CHECK(broker::maintenance_notice_key(b) == "chk.notice.maint_denied");
}

EF_TEST(x02_success_needs_full_scan_and_restore_point) {
    recovery::MemoryJournalFs fs;
    Fake f;
    EF_CHECK(run_image_repair(f, fs, 1).outcome == MaintOutcome::Repaired && f.scans.size() == 2 && !f.scans[0] && f.scans[1]);
    Fake no_rp;
    no_rp.rp_ok = false;  // geri donus kosulu yok: onarim baslamaz
    EF_CHECK(run_image_repair(no_rp, fs, 1).outcome == MaintOutcome::NoRestorePoint && no_rp.restores == 0);
    Fake reload;
    reload.restore_hr = 1;  // oturum yenileme gerekli: basari denmez
    EF_CHECK(run_image_repair(reload, fs, 1).outcome == MaintOutcome::RepairNotVerified);
}

EF_TEST(x02_check_reports_interrupted_repair_and_notice_says_so) {
    recovery::MemoryJournalFs fs;
    (void)fs.write_atomic(kRepairJobFile, R"({"started_unix_ms":5})");
    Fake f;
    f.states = {ImageHealth::Healthy};
    const auto r = run_image_check(f, &fs);
    EF_CHECK(r.previous_interrupted && !fs.read(kRepairJobFile).ok());
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Started;
    b.steps = {{kImageCheckTarget, broker::StepState::Applied, false, "mj:repairable:prev_interrupted"}};
    EF_CHECK(broker::maintenance_notice_key(b) == "chk.notice.maint_prev_interrupted" && broker::maintenance_outcome_key(b) == "repairable");
    b.steps[0].detail = "mj:repair_not_verified";
    EF_CHECK(broker::maintenance_notice_key(b) == "chk.notice.maint_not_verified");  // "repair" onekine takilmaz
}
