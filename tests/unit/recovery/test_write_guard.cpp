// 2. bolum: onaydan gercek yazima. Kapi motorda, broker'da, Performans arka ucunda, temizlik ve baslangic
// oturumlarinda UI'dan bagimsiz uygulanir. Yarim islem geri alinir; sureli acil geri donus engellenmez.
#include "core/broker/admin.h"
#include "core/cleanup/session.h"
#include "core/perf/journal_backend.h"
#include "core/recovery/engine.h"
#include "core/recovery/reconcile.h"
#include "core/startup/session.h"
#include "ef_test.h"

using namespace ef;
namespace rec = core::recovery;
using contracts::ErrorCode;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::TxState;

namespace {

contracts::SettingOperation op(const std::string& target, const std::string& v = "1") {
    return {OperationId::GamingToggleSet, OperationScope::CurrentUser, target, v, false, false};
}

rec::JournalTx make_tx(std::initializer_list<const char*> targets) {
    rec::JournalTx tx;
    tx.id = std::string(32, 'a');
    for (const auto* t : targets) {
        rec::JournalStep s;
        s.op = op(t);
        tx.steps.push_back(std::move(s));
    }
    return tx;
}

struct Env {
    rec::MemoryJournalFs fs;
    rec::JournalStore store{fs, "machine"};
    rec::FakeAdapter adapter;
    rec::AdapterRegistry reg;
    rec::TransactionEngine engine{store, reg};
    Env() { reg.add(OperationId::GamingToggleSet, &adapter); }
};

}  // namespace

EF_TEST(guard_denied_before_first_write_touches_nothing) {
    Env e;
    e.adapter.values["a"] = "0";
    e.engine.set_guard([] { return rec::WriteDecision{false, "gate.game_running"}; });
    auto r = e.engine.apply(make_tx({"a"}));
    EF_CHECK(!r.ok() && r.code() == ErrorCode::GateBlocked && r.error().detail == "gate.game_running");
    EF_CHECK(e.adapter.applies == 0 && e.adapter.values["a"] == "0");
    EF_CHECK(e.store.scan().transactions.empty());  // journal da yazilmadi
}

EF_TEST(guard_change_mid_transaction_rolls_back_consistently) {
    Env e;
    e.adapter.values["a"] = "0";
    e.adapter.values["b"] = "0";
    int calls = 0;
    e.engine.set_guard([&] { return rec::WriteDecision{++calls < 2, "gate.game_running"}; });  // ilk adim izinli, ikincide oyun acildi
    auto r = e.engine.apply(make_tx({"a", "b"}));
    EF_CHECK(!r.ok() && r.code() == ErrorCode::GateBlocked);
    EF_CHECK(e.adapter.values["a"] == "0" && e.adapter.values["b"] == "0");  // yarim islem kalmadi
    EF_CHECK(e.adapter.applies == 1 && e.adapter.restores == 1);
    const auto l = e.store.scan();
    EF_CHECK(l.transactions.size() == 1 && l.transactions[0].state == TxState::Restored);
}

EF_TEST(guard_mid_transaction_unverified_rollback_is_recovery_required) {
    Env e;
    e.adapter.values["a"] = "0";
    e.adapter.values["b"] = "0";
    e.adapter.fail_restore_target = "a";
    int calls = 0;
    e.engine.set_guard([&] { return rec::WriteDecision{++calls < 2, "gate.game_running"}; });
    auto r = e.engine.apply(make_tx({"a", "b"}));
    EF_CHECK(r.ok() && r.value().state == TxState::RecoveryRequired);  // sessiz basari/gizli yarim islem yok
}

EF_TEST(guard_blocks_user_rollback_without_state_change) {
    Env e;
    e.adapter.values["a"] = "0";
    auto tx = e.engine.apply(make_tx({"a"}));
    EF_CHECK(tx.ok() && tx.value().state == TxState::Active);
    e.engine.set_guard([] { return rec::WriteDecision{false, "gate.detection_stale"}; });
    auto r = e.engine.rollback(tx.value());
    EF_CHECK(!r.ok() && r.code() == ErrorCode::GateBlocked);
    EF_CHECK(e.adapter.values["a"] == "1" && e.adapter.restores == 0);
    EF_CHECK(e.store.scan().transactions[0].state == TxState::Active);
}

EF_TEST(guard_timed_display_revert_is_not_blocked) {
    Env e;
    e.adapter.values["a"] = "0";
    auto tx = e.engine.apply(make_tx({"a"}));
    e.engine.set_guard([] { return rec::WriteDecision{false, "gate.game_running"}; });
    rec::EngineOptions emergency;
    emergency.bypass_guard = true;
    auto r = e.engine.rollback(tx.value(), emergency);
    EF_CHECK(r.ok() && r.value().state == TxState::Restored && e.adapter.values["a"] == "0");
}

EF_TEST(guard_reconcile_is_callers_decision) {
    Env e;
    e.adapter.values["a"] = "0";
    auto tx = make_tx({"a"});
    tx.state = TxState::Prepared;
    EF_CHECK(e.store.begin(tx).ok());
    tx.state = TxState::Applying;
    tx.steps[0].state = rec::StepState::Applied;
    tx.steps[0].snapshot.existed = true;
    tx.steps[0].snapshot.old_value = "0";
    tx.steps[0].last_written = "1";
    EF_CHECK(e.store.persist(tx).ok());
    e.adapter.values["a"] = "1";
    e.engine.set_guard([] { return rec::WriteDecision{false, "x"}; });
    const auto rep = rec::reconcile(e.store, e.engine);  // cagiran kapiyi dogruladi; motor ikinci kez reddetmez
    EF_CHECK(rep.restored.size() == 1 && e.adapter.values["a"] == "0");
}

// ---- Performans arka ucu ----
namespace {
struct PerfEnv : Env {
    bool allow = true;
    int permit_calls = 0;
    core::perf::JournalBackendDeps deps;
    struct Chan final : core::broker::AdminChannel {
        int runs = 0;
        core::broker::StepState result = core::broker::StepState::Applied;
        core::broker::BatchResult run(core::ipc::RequestAction, const std::vector<contracts::SettingOperation>& ops) override {
            ++runs;
            core::broker::BatchResult b;
            b.elevation = core::ElevationOutcome::Started;
            for (const auto& o : ops) b.steps.push_back({o.target, result, false, "gate.game_running"});
            return b;
        }
        std::vector<contracts::SettingOperation> active_ops() override { return {op("adm")}; }
    } chan;
    PerfEnv() {
        deps.scope = {"user", &store, &engine};
        deps.new_tx_id = [] { return std::string(32, 'b'); };
        deps.admin = &chan;
        deps.permit = [this] {
            ++permit_calls;
            return rec::WriteDecision{allow, "gate.game_running"};
        };
    }
    core::perf::Plan plan(bool user, bool admin) {
        core::perf::Plan p;
        if (user) {
            core::perf::PlanItem i;
            i.op = op("u");
            p.items.push_back(i);
        }
        if (admin) {
            core::perf::PlanItem i;
            i.op = op("adm");
            i.needs_admin = true;
            p.items.push_back(i);
        }
        return p;
    }
};
std::atomic<bool> no_cancel{false};
}  // namespace

EF_TEST(perf_backend_denied_at_apply_boundary) {
    PerfEnv e;
    e.allow = false;
    core::perf::JournalBackend b(e.deps);
    const auto o = b.apply(e.plan(true, true), {}, no_cancel);
    EF_CHECK(o.kind == core::perf::OutcomeKind::GateBlocked && o.gate_reason == "gate.game_running");
    EF_CHECK(e.adapter.applies == 0 && e.chan.runs == 0);
}

EF_TEST(perf_backend_game_starts_before_uac_no_prompt) {
    PerfEnv e;
    int n = 0;
    e.deps.permit = [&n] { return rec::WriteDecision{++n < 2, "gate.game_running"}; };
    e.adapter.values["u"] = "0";
    core::perf::JournalBackend b(e.deps);
    const auto o = b.apply(e.plan(true, true), {}, no_cancel);
    EF_CHECK(e.chan.runs == 0);  // UAC istenmedi
    EF_CHECK(o.kind == core::perf::OutcomeKind::Active && o.gate_blocked && o.changes == 1);
}

EF_TEST(perf_backend_broker_gate_after_uac) {
    PerfEnv e;
    e.chan.result = core::broker::StepState::GateBlocked;
    core::perf::JournalBackend b(e.deps);
    const auto o = b.apply(e.plan(false, true), {}, no_cancel);
    EF_CHECK(o.kind == core::perf::OutcomeKind::GateBlocked && e.chan.runs == 1 && o.gate_reason == "gate.game_running");
}

EF_TEST(perf_backend_restore_denied_keeps_active) {
    PerfEnv e;
    e.adapter.values["u"] = "0";
    {
        core::perf::JournalBackend b(e.deps);
        EF_CHECK(b.apply(e.plan(true, false), {}, no_cancel).kind == core::perf::OutcomeKind::Active);
    }
    e.allow = false;
    core::perf::JournalBackend b(e.deps);
    const auto o = b.restore({});
    EF_CHECK(o.kind == core::perf::OutcomeKind::GateBlocked && e.adapter.values["u"] == "1" && e.chan.runs == 0);
}

EF_TEST(perf_controller_gate_block_notice_and_state) {
    struct B final : core::perf::PerfBackend {
        contracts::Result<core::perf::Plan> plan() override {
            core::perf::Plan p;
            core::perf::PlanItem i;
            i.op = op("x");
            i.title_key = "plan.x";
            p.items.push_back(i);
            return p;
        }
        core::perf::Outcome apply(const core::perf::Plan&, const core::perf::ProgressFn&, const std::atomic<bool>&) override {
            return {core::perf::OutcomeKind::GateBlocked, 0, {}};
        }
        core::perf::Outcome restore(const core::perf::ProgressFn&) override { return {core::perf::OutcomeKind::GateBlocked, 2, {}}; }
        std::size_t active_changes() override { return 2; }
        bool recovery_needed() override { return false; }
    } b;
    core::perf::PerfController c(b, [](std::function<void()> w) { w(); }, [](std::function<void()> f) { f(); });
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == core::perf::Phase::Off && c.notice() == core::perf::Notice::GameBlocked);
    c.init();
    EF_CHECK(c.phase() == core::perf::Phase::On);
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == core::perf::Phase::On && c.notice() == core::perf::Notice::GameBlocked && c.active_changes() == 2);
}

// ---- Broker: UAC sonrasi bagimsiz kontrol ----
namespace {
struct BrokerEnv : Env {
    core::broker::ExecutorDeps deps;
    rec::MemoryJournalFs fixes_fs;
    rec::JournalStore fixes{fixes_fs, "machine"};
    rec::TransactionEngine fixes_engine{fixes, reg};
    bool allow = true;
    BrokerEnv() {
        reg.add(OperationId::HagsSet, &adapter);
        deps.perf_store = &store;
        deps.perf_engine = &engine;
        deps.fixes_store = &fixes;
        deps.fixes_engine = &fixes_engine;
        deps.new_tx_id = [] { return std::string(32, 'c'); };
        deps.ctx.elevated = true;
        deps.ctx.machine_lock_held = true;
        deps.permit = [this] { return rec::WriteDecision{allow, "gate.game_running"}; };
    }
    static contracts::SettingOperation hags() { return {OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", false, true}; }
};
}  // namespace

EF_TEST(broker_rechecks_permit_after_uac) {
    BrokerEnv e;
    e.allow = false;
    core::broker::AdminExecutor x(e.deps);
    const auto r = x.handle(core::ipc::RequestAction::Fix, BrokerEnv::hags());
    EF_CHECK(r.state == core::broker::StepState::GateBlocked && r.detail == "gate.game_running");
    EF_CHECK(e.adapter.applies == 0 && e.fixes.scan().transactions.empty());
}

EF_TEST(broker_restore_blocked_keep_current_allowed) {
    BrokerEnv e;
    e.adapter.values["hags:hwschmode"] = "1";
    core::broker::AdminExecutor x(e.deps);
    EF_CHECK(x.handle(core::ipc::RequestAction::Fix, BrokerEnv::hags()).state == core::broker::StepState::Applied);
    e.allow = false;
    EF_CHECK(x.handle(core::ipc::RequestAction::UndoFix, BrokerEnv::hags()).state == core::broker::StepState::GateBlocked);
    EF_CHECK(e.adapter.values["hags:hwschmode"] == "2");
    // KeepCurrent ayar yazmaz: kapiya takilmaz.
    EF_CHECK(x.handle(core::ipc::RequestAction::KeepCurrent, BrokerEnv::hags()).state != core::broker::StepState::GateBlocked);
}

EF_TEST(broker_reconcile_deferred_when_gate_closed) {
    BrokerEnv e;
    e.allow = false;
    core::broker::AdminExecutor x(e.deps);
    const auto s = x.reconcile_on_start();
    EF_CHECK(!s.ran && s.deferred);
    EF_CHECK(x.handle(core::ipc::RequestAction::Recover, BrokerEnv::hags()).state == core::broker::StepState::GateBlocked);
}

EF_TEST(broker_gate_blocked_roundtrip_and_notice) {
    core::broker::StepResult r{"t", core::broker::StepState::GateBlocked, false, "gate.game_running"};
    const auto d = core::broker::decode_result(core::broker::encode_result("r1", r));
    EF_CHECK(d.ok() && d.value().state == core::broker::StepState::GateBlocked);
    core::broker::BatchResult b;
    b.elevation = core::ElevationOutcome::Started;
    b.steps.push_back(r);
    for (auto a : {core::ipc::RequestAction::Fix, core::ipc::RequestAction::UndoFix, core::ipc::RequestAction::Recover})
        EF_CHECK(core::broker::admin_notice_key(a, b) == "chk.notice.gate_blocked");
}

// ---- Temizlik oturumu ----
namespace {
core::cleanup::SessionDeps cleanup_deps(int& runs, std::string& deny) {
    core::cleanup::SessionDeps d;
    d.preview = [](const std::vector<core::cleanup::CleanupCategory>&, const std::wstring&, const core::cleanup::PreviewOptions&) {
        core::cleanup::CleanupPlan p;
        core::cleanup::CategoryPlan cp;
        cp.preview.category = core::cleanup::kProductCategories[0];
        cp.preview.status = core::cleanup::PreviewStatus::Ready;
        cp.preview.files = 3;
        cp.preview.bytes = 300;
        p.categories.push_back(cp);
        return p;
    };
    d.run = [&runs](const core::cleanup::CleanupPlan&, const std::vector<core::cleanup::CleanupCategory>&, const core::cleanup::RunOptions& o) {
        ++runs;
        core::cleanup::CleanupReport rep;
        for (std::size_t i = 0; i < 3; ++i) {
            if (o.cancel && o.cancel->load()) {
                rep.cancelled = true;
                break;
            }
            ++rep.processed;
            if (o.on_progress) o.on_progress({core::cleanup::kProductCategories[0], i + 1, 3});
        }
        return contracts::Result<core::cleanup::CleanupReport>(rep);
    };
    d.run_background = [](std::function<void()> w) { w(); };
    d.post_ui = [](std::function<void()> f) { f(); };
    d.gate_denied = [&deny] { return deny; };
    d.gate_poll_ms = 0;
    return d;
}
void ready_to_confirm(core::cleanup::CleanupSession& s) {
    s.scan();
    s.set_selected(core::cleanup::kProductCategories[0], true);
    s.request_run();
}
}  // namespace

EF_TEST(cleanup_game_started_while_waiting_for_yes_deletes_nothing) {
    int runs = 0;
    std::string deny = "gate.game_running";
    core::cleanup::CleanupSession s(cleanup_deps(runs, deny));
    ready_to_confirm(s);
    EF_CHECK(s.phase() == core::cleanup::SessionPhase::Confirming);
    s.answer(true);
    EF_CHECK(runs == 0 && s.phase() == core::cleanup::SessionPhase::Ready && s.gate_reason() == "gate.game_running");
}

EF_TEST(cleanup_game_starts_mid_run_stops_and_reports) {
    int runs = 0;
    std::string deny;
    auto d = cleanup_deps(runs, deny);
    int polls = 0;
    d.gate_denied = [&polls] { return ++polls >= 2 ? std::string("gate.game_running") : std::string(); };
    core::cleanup::CleanupSession s(std::move(d));
    ready_to_confirm(s);
    s.answer(true);
    EF_CHECK(runs == 1 && s.phase() == core::cleanup::SessionPhase::Done && s.gate_reason() == "gate.game_running");
    EF_CHECK(s.report() && s.report()->cancelled && s.report()->processed < 3);
}

// ---- Baslangic oturumu ----
EF_TEST(startup_gate_stops_remaining_entries) {
    namespace st = core::startup;
    namespace sa = core::adapters::startup;
    int changes = 0, checks = 0;
    st::StartupDeps d;
    d.scan = [] {
        st::ScanResult r;
        for (const char* n : {"a", "b", "c"}) {
            st::StartupRow row;
            row.entry.name = n;
            row.entry.location = sa::Location::UserRun;
            row.entry.approval = sa::Approval::Enabled;
            r.rows.push_back(row);
        }
        return contracts::Result<st::ScanResult>(r);
    };
    d.change = [&changes](const sa::Entry&, bool) {
        ++changes;
        return contracts::ok_status();
    };
    d.run_background = [](std::function<void()> w) { w(); };
    d.post_ui = [](std::function<void()> f) { f(); };
    d.gate_denied = [&checks] { return ++checks >= 2 ? std::string("gate.game_running") : std::string(); };
    st::StartupSession s(std::move(d));
    s.scan();
    for (std::size_t i = 0; i < 3; ++i) s.set_desired(i, false);
    s.request_apply();
    s.answer(true);
    EF_CHECK(changes == 1 && s.last() && s.last()->applied == 1 && s.last()->skipped == 2);
    EF_CHECK(s.last()->gate_reason == "gate.game_running");
}

EF_TEST(guard_restore_matching_reports_gate_not_recovery) {
    Env e;
    e.adapter.values["a"] = "0";
    auto tx = e.engine.apply(make_tx({"a"}));
    EF_CHECK(tx.ok());
    e.engine.set_guard([] { return rec::WriteDecision{false, "gate.game_running"}; });
    const auto r = core::broker::restore_matching(&e.store, &e.engine, [](const contracts::SettingOperation&) { return true; }, "a");
    EF_CHECK(r.state == core::broker::StepState::GateBlocked && r.detail == "gate.game_running");
    EF_CHECK(e.store.scan().transactions[0].state == TxState::Active);
}
