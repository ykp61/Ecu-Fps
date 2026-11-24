// 3. bolum: kullanici eylemi -> islem -> gorunur sonuc zincirinde bulunan hatalarin regresyonlari.
#include "core/measure/frame_stats.h"
#include "core/measure/view.h"
#include "core/perf/journal_backend.h"
#include "core/recovery/conflicts.h"
#include "core/recovery/uninstall.h"
#include "ef_test.h"

using namespace ef;
namespace rec = core::recovery;
namespace perf = core::perf;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::TxState;

namespace {
contracts::SettingOperation op(const std::string& target) { return {OperationId::GamingToggleSet, OperationScope::CurrentUser, target, "1", false, false}; }

struct Chan final : core::broker::AdminChannel {
    int runs = 0;
    core::ipc::RequestAction last = core::ipc::RequestAction::Apply;
    std::vector<contracts::SettingOperation> active, recovery, conflict, sent;
    core::ElevationOutcome elevation = core::ElevationOutcome::Started;
    core::broker::BatchResult run(core::ipc::RequestAction a, const std::vector<contracts::SettingOperation>& ops) override {
        ++runs;
        last = a;
        sent = ops;
        core::broker::BatchResult b;
        b.elevation = elevation;
        for (const auto& o : ops) b.steps.push_back({o.target, core::broker::StepState::Restored, false, ""});
        return b;
    }
    std::vector<contracts::SettingOperation> active_ops() override { return active; }
    std::vector<contracts::SettingOperation> perf_recovery_ops() override { return recovery; }
    std::vector<contracts::SettingOperation> perf_conflict_ops() override { return conflict; }
    bool recovery_needed() override { return !recovery.empty(); }
};

struct Env {
    rec::MemoryJournalFs fs;
    rec::JournalStore store{fs, "machine"};
    rec::FakeAdapter adapter;
    rec::AdapterRegistry reg;
    rec::TransactionEngine engine{store, reg};
    Chan chan;
    perf::JournalBackendDeps deps;
    Env() {
        reg.add(OperationId::GamingToggleSet, &adapter);
        deps.scope = {"user", &store, &engine};
        deps.new_tx_id = [] { return std::string(32, 'd'); };
        deps.admin = &chan;
    }
    rec::JournalTx applied(const char* target) {
        adapter.values[target] = "0";
        rec::JournalTx tx;
        tx.id = std::string(32, 'e');
        rec::JournalStep s;
        s.op = op(target);
        tx.steps.push_back(std::move(s));
        return engine.apply(std::move(tx)).value();
    }
};
auto sync_run = [](std::function<void()> w) { w(); };
auto sync_post = [](std::function<void()> f) { f(); };
}  // namespace

EF_TEST(flow_retry_restore_sends_unverified_machine_records) {
    Env e;
    e.chan.recovery = {op("adm")};
    perf::JournalBackend b(e.deps);
    const auto o = b.restore({});
    EF_CHECK(e.chan.runs == 1 && e.chan.last == core::ipc::RequestAction::Restore && e.chan.sent.size() == 1);
    EF_CHECK(o.kind == perf::OutcomeKind::Restored);
}

EF_TEST(flow_nothing_to_do_is_not_off_when_recovery_remains) {
    struct B final : perf::PerfBackend {
        contracts::Result<perf::Plan> plan() override { return perf::Plan{}; }
        perf::Outcome apply(const perf::Plan&, const perf::ProgressFn&, const std::atomic<bool>&) override { return {}; }
        perf::Outcome restore(const perf::ProgressFn&) override { return {perf::OutcomeKind::NothingToDo, 0, {}}; }
        std::size_t active_changes() override { return 0; }
        bool recovery_needed() override { return true; }
    } b;
    perf::PerfController c(b, sync_run, sync_post);
    c.init();
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded);
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded && c.notice() == perf::Notice::RestoreFailed);
}

EF_TEST(flow_external_change_has_an_exit_and_is_not_overwritten) {
    Env e;
    auto tx = e.applied("a");
    e.adapter.values["a"] = "user";  // kullanici disaridan degistirdi
    perf::JournalBackend b(e.deps);
    EF_CHECK(b.restore({}).kind == perf::OutcomeKind::RecoveryRequired);
    EF_CHECK(e.adapter.values["a"] == "user" && b.external_conflict());
    perf::PerfController c(b, sync_run, sync_post);
    c.init();
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded && c.external_conflict());
    c.keep_external();
    EF_CHECK(c.phase() == perf::Phase::Off && !c.external_conflict());
    EF_CHECK(e.adapter.values["a"] == "user");  // ayar yazilmadi
    EF_CHECK(e.store.scan().transactions[0].state == TxState::Restored);
}

EF_TEST(flow_keep_external_machine_denied_stays_recovery) {
    Env e;
    e.chan.recovery = {op("adm")};
    e.chan.conflict = {op("adm")};
    e.chan.elevation = core::ElevationOutcome::Denied;
    perf::JournalBackend b(e.deps);
    perf::PerfController c(b, sync_run, sync_post);
    c.init();
    c.keep_external();
    EF_CHECK(e.chan.last == core::ipc::RequestAction::KeepCurrent);
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded && c.notice() == perf::Notice::Denied);
}

namespace {
struct OkRemover final : rec::DataRemover {
    int user = 0;
    contracts::Status remove_autostart() override { return contracts::ok_status(); }
    contracts::Status remove_machine_data() override { return contracts::ok_status(); }
    contracts::Status remove_user_data() override {
        ++user;
        return contracts::ok_status();
    }
};
}  // namespace

EF_TEST(flow_uninstall_keeps_external_and_finishes) {
    Env e;
    (void)e.applied("a");
    e.adapter.values["a"] = "user";
    OkRemover rm;
    const auto r = rec::run_uninstall({{"user", &e.store, &e.engine}}, rm);
    EF_CHECK(r.restored_all && r.data_removed && rm.user == 1);
    EF_CHECK(e.adapter.values["a"] == "user");
    EF_CHECK(r.notes.size() == 1 && r.notes[0] == "externally changed values were kept");
}

EF_TEST(flow_uninstall_counts_unrestored_once) {
    Env e;
    (void)e.applied("a");
    e.adapter.fail_restore_target = "a";
    OkRemover rm;
    const auto r = rec::run_uninstall({{"user", &e.store, &e.engine}}, rm);
    EF_CHECK(!r.restored_all && !r.data_removed && r.unrestored.size() == 1 && rm.user == 0);
}

EF_TEST(flow_fps_series_real_seconds_only) {
    using core::measure::fps_per_second;
    std::vector<double> frames(2500, 10.0);  // 25 sn, 100 FPS... 2500*10ms = 25 s
    const auto s = fps_per_second(frames);
    EF_CHECK(s.size() == 25 && std::abs(s[0] - 100.0) < 0.01);
    EF_CHECK(fps_per_second({}).empty());
    EF_CHECK(fps_per_second({5.0, 5.0}).empty());  // tam saniye yok: uydurma nokta yok
    std::vector<double> many(100000, 10.0);
    EF_CHECK(fps_per_second(many, 120).size() == 120);
}

EF_TEST(flow_smoothness_key_from_measured_values) {
    using contracts::Measurement;
    using contracts::Unit;
    using core::measure::FramePattern;
    using core::measure::smoothness_key;
    const auto a = Measurement::of(100, Unit::Fps);
    EF_CHECK(smoothness_key(a, Measurement::of(90, Unit::Fps), FramePattern::Insufficient) == "smooth.steady");
    EF_CHECK(smoothness_key(a, Measurement::of(70, Unit::Fps), FramePattern::Insufficient) == "smooth.dips");
    EF_CHECK(smoothness_key(a, Measurement::of(30, Unit::Fps), FramePattern::Insufficient) == "smooth.heavy");
    const auto none = Measurement::missing(Unit::Fps, contracts::MissingReason::NotSampled);
    EF_CHECK(smoothness_key(a, none, FramePattern::Insufficient) == "smooth.unknown");
    EF_CHECK(smoothness_key(none, none, FramePattern::Spiky) == "smooth.heavy");  // desen olculduyse o esastir
}

EF_TEST(flow_uac_denied_keeps_recovery_state) {
    struct B final : perf::PerfBackend {
        contracts::Result<perf::Plan> plan() override { return perf::Plan{}; }
        perf::Outcome apply(const perf::Plan&, const perf::ProgressFn&, const std::atomic<bool>&) override { return {}; }
        perf::Outcome restore(const perf::ProgressFn&) override { return {perf::OutcomeKind::Denied, 1, {}}; }
        std::size_t active_changes() override { return 0; }
        bool recovery_needed() override { return true; }
    } b;
    perf::PerfController c(b, sync_run, sync_post);
    c.init();
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded && c.notice() == perf::Notice::Denied);
}

EF_TEST(flow_gate_midway_counts_remaining_changes) {
    Env e;
    auto t1 = e.applied("a");
    (void)t1;
    rec::JournalTx t2;
    t2.id = std::string(32, 'f');
    rec::JournalStep s;
    s.op = op("b");
    t2.steps.push_back(std::move(s));
    e.adapter.values["b"] = "0";
    EF_CHECK(e.engine.apply(std::move(t2)).ok());
    int calls = 0;
    e.engine.set_guard([&calls] { return rec::WriteDecision{++calls < 2, "gate.game_running"}; });
    e.deps.admin = nullptr;
    perf::JournalBackend b(e.deps);
    const auto o = b.restore({});
    EF_CHECK(o.kind == perf::OutcomeKind::GateBlocked && o.changes == 1);
}
