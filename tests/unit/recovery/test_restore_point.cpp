// EF-X16: sahte API ile akis testleri. Gercek nokta olusturma kullanici katilimli UAC turudur.
#include "core/broker/admin.h"
#include "core/recovery/restore_point.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::recovery;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
struct FakeApi final : RestorePointApi {
    std::uint32_t begin_status = 0, end_status = 0, cancel_status = 0;
    std::vector<RpItem> items;
    bool add_point = true, inv_fail = false;
    int begins = 0, ends = 0, cancels = 0;
    std::int64_t last_cancel = 0;
    RpCall begin(const std::string& d) override {
        ++begins;
        desc = d;
        return {begin_status, 42};
    }
    std::uint32_t end(std::int64_t) override {
        ++ends;
        if (end_status == 0 && add_point) items.push_back({42, desc});
        return end_status;
    }
    std::uint32_t cancel(std::int64_t s) override {
        ++cancels;
        last_cancel = s;
        return cancel_status;
    }
    std::optional<std::vector<RpItem>> inventory() override {
        if (inv_fail) return std::nullopt;
        return items;
    }
    std::string desc;
};
const std::string kDesc = "Ecu Fps";
}  // namespace

EF_TEST(x16_t01_success_requires_new_point_in_inventory) {
    FakeApi a;
    MemoryJournalFs fs;
    EF_CHECK(run_restore_point(a, fs, kDesc).outcome == RestorePointOutcome::Created);
    FakeApi skipped;
    skipped.add_point = false;  // API basarili ama yeni nokta yok (siklik siniri)
    EF_CHECK(run_restore_point(skipped, fs, kDesc).outcome == RestorePointOutcome::SkippedByFrequency);
    FakeApi noinv;
    noinv.inv_fail = true;
    EF_CHECK(run_restore_point(noinv, fs, kDesc).outcome == RestorePointOutcome::Unverified);
    EF_CHECK(noinv.cancels == 0 && !fs.read(kRestorePointJobFile).ok());  // yeni oldugu kanitlanmayan numara iptale yazilmaz
    FakeApi other;  // ayni anda baska uygulamanin noktasi "bizimki" sayilmaz
    other.add_point = false;
    other.items.push_back({7, "Windows Update"});
    EF_CHECK(run_restore_point(other, fs, kDesc).outcome == RestorePointOutcome::SkippedByFrequency);
}

EF_TEST(x16_t02_begin_failures_are_distinct_and_touch_nothing) {
    const auto with = [](std::uint32_t st) {
        FakeApi a;
        a.begin_status = st;
        MemoryJournalFs fs;
        const auto r = run_restore_point(a, fs, kDesc);
        EF_CHECK(a.ends == 0 && a.cancels == 0 && !fs.read(kRestorePointJobFile).ok());
        return r.outcome;
    };
    EF_CHECK(with(1058) == RestorePointOutcome::ProtectionOff);
    EF_CHECK(with(112) == RestorePointOutcome::DiskFull);
    EF_CHECK(with(5) == RestorePointOutcome::Denied);
    EF_CHECK(with(1450) == RestorePointOutcome::ApiFailed);
}

EF_TEST(x16_t04_end_failure_stays_recoverable_and_next_run_cancels) {
    FakeApi a;
    a.end_status = 1450;
    MemoryJournalFs fs;
    EF_CHECK(run_restore_point(a, fs, kDesc).outcome == RestorePointOutcome::ApiFailed);
    EF_CHECK(fs.read(kRestorePointJobFile).ok());  // kapatilmamis is kalici
    a.end_status = 0;
    EF_CHECK(run_restore_point(a, fs, kDesc).outcome == RestorePointOutcome::Created);
    EF_CHECK(a.cancels == 1 && a.last_cancel == 42 && !fs.read(kRestorePointJobFile).ok());
}

EF_TEST(x16_unresolved_previous_job_blocks_second_begin) {
    FakeApi a;
    a.end_status = 1450;
    MemoryJournalFs fs;
    (void)run_restore_point(a, fs, kDesc);
    a.cancel_status = 1450;  // iptal de basarisiz: ust uste BEGIN yok
    const int before = a.begins;
    EF_CHECK(run_restore_point(a, fs, kDesc).outcome == RestorePointOutcome::ApiFailed);
    EF_CHECK(a.begins == before);
    (void)recover_pending_restore_point(a, fs);
    (void)recover_pending_restore_point(a, fs);
    EF_CHECK(!fs.read(kRestorePointJobFile).ok());  // sinirli deneme: kalici kilit yok
}

EF_TEST(x16_broker_allowlist_accepts_only_fixed_request) {
    ef::contracts::SettingOperation op{OperationId::RestorePointCreate, OperationScope::Machine, kRestorePointTarget, kRestorePointValue, false, false};
    EF_CHECK(broker::admin_operation_allowed(op));
    auto bad = op;
    bad.target = "restore_point:x";
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad = op;
    bad.new_value = "delete";
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad = op;
    bad.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(bad));
}

EF_TEST(x16_t03_notice_keys_never_call_skip_or_denial_created) {
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Denied;
    EF_CHECK(broker::restore_point_notice_key(b) == "chk.notice.rp_denied");
    b.elevation = ElevationOutcome::Failed;
    EF_CHECK(broker::restore_point_notice_key(b) == "chk.notice.admin_start_failed");
    b.elevation = ElevationOutcome::Started;
    const auto with = [&b](broker::StepState st, std::string detail) {
        b.steps = {{kRestorePointTarget, st, false, std::move(detail)}};
        return broker::restore_point_notice_key(b);
    };
    EF_CHECK(with(broker::StepState::Applied, "rp:created") == "chk.notice.rp_created");
    EF_CHECK(with(broker::StepState::NothingToDo, "rp:skipped_frequency") == "chk.notice.rp_skipped");
    EF_CHECK(with(broker::StepState::Failed, "rp:protection_off") == "chk.notice.rp_protection_off");
    EF_CHECK(with(broker::StepState::Failed, "rp:disk_full") == "chk.notice.rp_disk_full");
    EF_CHECK(with(broker::StepState::Failed, "rp:unverified") == "chk.notice.rp_unverified");
    EF_CHECK(with(broker::StepState::Failed, "rp:api_failed") == "chk.notice.rp_failed");
    EF_CHECK(with(broker::StepState::GateBlocked, "") == "chk.notice.gate_blocked");
}

EF_TEST(x16_throttled_begin_returns_old_point_and_it_is_never_cancelled) {
    FakeApi a;
    a.items.push_back({42, "Ecu Fps"});  // BEGIN onceki noktanin numarasini dondurur
    MemoryJournalFs fs;
    EF_CHECK(run_restore_point(a, fs, kDesc).outcome == RestorePointOutcome::SkippedByFrequency);
    EF_CHECK(a.ends == 0 && a.cancels == 0 && !fs.read(kRestorePointJobFile).ok());
}

EF_TEST(x16_stale_sequence_does_not_lock_future_points) {
    FakeApi a;
    a.end_status = 1450;
    MemoryJournalFs fs;
    (void)run_restore_point(a, fs, kDesc);
    a.cancel_status = 13;  // nokta artik yok
    a.end_status = 0;
    a.items.clear();
    EF_CHECK(run_restore_point(a, fs, kDesc).outcome == RestorePointOutcome::Created);
}

EF_TEST(x16_broker_executes_restore_point_through_dispatcher) {
    broker::ExecutorDeps d;
    d.ctx.elevated = true;
    d.ctx.machine_lock_held = true;
    d.ctx.broker_user_sid = "S-1-5-21-1";
    d.ctx.target_user_sid = "S-1-5-21-1";
    int calls = 0;
    d.create_restore_point = [&calls]() -> RestorePointRun { ++calls; return {RestorePointOutcome::Created, 42, ""}; };
    broker::AdminExecutor ex(d);
    const ef::contracts::SettingOperation op{OperationId::RestorePointCreate, OperationScope::Machine, kRestorePointTarget, kRestorePointValue, false, false};
    const auto r = ex.handle(ipc::RequestAction::RestorePoint, op);
    EF_CHECK(calls == 1 && r.state == broker::StepState::Applied && r.detail == "rp:created");
    EF_CHECK(ex.handle(ipc::RequestAction::Fix, op).state == broker::StepState::Rejected && calls == 1);  // ayar yolundan gecmez
    d.permit = []() -> WriteDecision { return {false, "gate.game_running"}; };
    broker::AdminExecutor closed(d);
    EF_CHECK(closed.handle(ipc::RequestAction::RestorePoint, op).state == broker::StepState::GateBlocked && calls == 1);
}
