// C/D/E + B1: gozlem zamani, bos kitaplik yasam dongusu, gercek neden ve App'in kullandigi yan etki yolu.
// PerfController + JournalBackend gercektir; sahte olan yalniz ayar adaptoru ve bellek ici journal'dir.
#include "core/games/detection_coordinator.h"
#include "core/perf/admin_sequencing.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using namespace ef::core::games;
namespace perf = ef::core::perf;
namespace rec = ef::core::recovery;

namespace {

GameStart game(const std::string& dir, std::uint32_t pid, std::uint64_t ct) {
    GameStart g;
    g.install_dir = dir;
    g.game_id = dir + "\\g.exe";
    g.pid = pid;
    g.create_time = ct;
    return g;
}

LibraryScanResult lib(LibraryScanState st, std::size_t games = 0) {
    LibraryScanResult r;
    r.state = st;
    r.games.resize(games);
    return r;
}

// Sahte arka uc: Evet/Hayir ve Performans durum makinesi gercek; backend cagrilari sayilir.
struct CountingBackend final : perf::PerfBackend {
    int plans = 0, applies = 0, restores = 0;
    ef::contracts::Result<perf::Plan> plan() override {
        ++plans;
        perf::Plan p;
        perf::PlanItem i;
        i.op = {ef::contracts::OperationId::GamingToggleSet, ef::contracts::OperationScope::CurrentUser, "t", "1", false, false};
        i.title_key = "plan.x";
        p.items.push_back(i);
        return p;
    }
    perf::Outcome apply(const perf::Plan&, const perf::ProgressFn&, const std::atomic<bool>&) override {
        ++applies;
        return {perf::OutcomeKind::Active, 1, {}};
    }
    perf::Outcome restore(const perf::ProgressFn&) override {
        ++restores;
        return {perf::OutcomeKind::Restored, 0, {}};
    }
    std::size_t active_changes() override { return 0; }
    bool recovery_needed() override { return false; }
};

// App::perf_auto_restore / check_pending_exit / run_pending_recovery ile ayni gecis.
struct Rig {
    std::int64_t now = 100000;
    CountingBackend backend;
    perf::PerfController controller{backend, [](std::function<void()> w) { w(); }, [](std::function<void()> f) { f(); }};
    int exits = 0, recoveries = 0;
    bool admin_busy = false;
    DetectionCoordinator det;

    Rig()
        : det([this] { return now; },
              DetectionCoordinator::Hooks{[this] {
                                              switch (perf::auto_restore_when(!det.gate(GuardedAction::SettingMutation).allowed, admin_busy)) {
                                                  case perf::DeferredRun::Now: controller.auto_restore(); break;
                                                  default: break;
                                              }
                                          },
                                          [this] { ++exits; },
                                          [this] { ++recoveries; }}) {
        det.begin_session(1);
        det.set_library(lib(LibraryScanState::Ok, 2));
    }
    // Otomatik Acik durumuna getir (oyun basladi senaryosu).
    void auto_on() {
        controller.auto_apply();
        EF_CHECK(controller.phase() == perf::Phase::On && controller.auto_active());
    }
    void scan(std::initializer_list<GameStart> running) {
        WatchResult r;
        r.detection = DetectionState::Ok;
        r.running = running;
        r.observed_at_ms = now;
        det.on_result(1, r);
    }
    void bad(DetectionState s) {
        WatchResult r;
        r.detection = s;
        r.observed_at_ms = now;
        det.on_result(1, r);
    }
};

}  // namespace

// ---- D: zaman damgasi ----
EF_TEST(detect_age_uses_observation_time_not_queue_time) {
    Rig r;
    WatchResult w;
    w.detection = DetectionState::Ok;
    w.observed_at_ms = r.now;
    r.now += 20000;  // pencere takildi: gozlem kuyrukta bekledi
    r.det.on_result(1, w);
    EF_CHECK(r.det.detection_age_ms() == 20000);
    EF_CHECK(!r.det.gate(GuardedAction::SettingMutation).allowed);
    EF_CHECK(r.det.cause() == BlockCause::Stale);
}

EF_TEST(detect_sleep_wake_jump_is_stale_then_recovers) {
    Rig r;
    r.scan({});
    EF_CHECK(r.det.gate(GuardedAction::DiskCleanup).allowed);
    r.now += 8 * 3600 * 1000;  // uyku/uyanma
    EF_CHECK(r.det.gate(GuardedAction::DiskCleanup).reason_key == "gate.detection_stale");
    r.scan({});
    EF_CHECK(r.det.gate(GuardedAction::DiskCleanup).allowed);
}

EF_TEST(detect_scanner_thread_stopped_goes_stale) {
    Rig r;
    r.scan({});
    r.now += kMaxDetectionAgeMs;
    EF_CHECK(r.det.gate(GuardedAction::HeavyBenchmark).allowed);
    r.now += 1;
    EF_CHECK(!r.det.gate(GuardedAction::HeavyBenchmark).allowed);
}

EF_TEST(detect_old_generation_and_out_of_order_discarded) {
    Rig r;
    r.scan({game("a", 1, 1)});
    WatchResult old_gen;
    old_gen.detection = DetectionState::Ok;
    old_gen.observed_at_ms = r.now + 1;
    EF_CHECK(r.det.on_result(99, old_gen).discarded);
    EF_CHECK(r.det.running().size() == 1);
    r.now += 3000;
    r.scan({game("a", 1, 1)});
    WatchResult stale;  // daha once alinmis "oyun yok" gozlemi yeni "oyun var"i ezmez
    stale.detection = DetectionState::Ok;
    stale.observed_at_ms = r.now - 2000;
    EF_CHECK(r.det.on_result(1, stale).discarded);
    EF_CHECK(r.det.running().size() == 1 && r.exits == 0);
}

EF_TEST(detect_result_without_timestamp_is_never_fresh) {
    Rig r;
    WatchResult w;
    w.detection = DetectionState::Ok;
    r.det.on_result(1, w);
    EF_CHECK(r.det.gate(GuardedAction::SettingMutation).reason_key == "gate.detection_stale");
}

// ---- C: guvenilir bos kitaplik ----
EF_TEST(detect_empty_library_stays_open_with_reverification) {
    Rig r;
    r.det.begin_session(2);
    r.det.set_library(lib(LibraryScanState::Empty));
    for (int i = 0; i < 40; ++i) {  // 200 sn
        WatchResult w;
        w.detection = DetectionState::Ok;
        w.observed_at_ms = r.now;
        r.det.on_result(2, w);
        EF_CHECK(r.det.gate(GuardedAction::SettingMutation).allowed);
        r.now += kLibraryPollEmptyMs;
        EF_CHECK(r.det.gate(GuardedAction::SettingMutation).allowed);  // tur arasinda da acik
    }
}

EF_TEST(detect_game_installed_after_start_moves_to_process_tracking) {
    Rig r;
    r.det.begin_session(2);
    r.det.set_library(lib(LibraryScanState::Empty));
    WatchResult w;
    w.detection = DetectionState::Ok;
    w.observed_at_ms = r.now;
    r.det.on_result(2, w);
    r.det.set_library(lib(LibraryScanState::Ok, 1));  // yeni kurulum
    r.now += 3000;
    w.running = {game("new", 5, 5)};
    w.observed_at_ms = r.now;
    r.det.on_result(2, w);
    EF_CHECK(r.det.gate(GuardedAction::SettingMutation).reason_key == "gate.game_running");
}

EF_TEST(detect_library_trust_lost_later_blocks) {
    Rig r;
    r.scan({});
    EF_CHECK(r.det.gate(GuardedAction::SettingMutation).allowed);
    r.det.set_library(lib(LibraryScanState::Partial, 1));
    EF_CHECK(r.det.cause() == BlockCause::LibraryError);
    EF_CHECK(r.det.gate(GuardedAction::SettingMutation).reason_key == "gate.library_error");
    r.det.set_library(lib(LibraryScanState::Ok, 1));
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.det.gate(GuardedAction::SettingMutation).allowed);
}

// ---- E: dogru neden ----
EF_TEST(detect_each_cause_has_own_reason) {
    Rig r;
    r.det.begin_session(3);
    EF_CHECK(r.det.cause() == BlockCause::FirstScan);
    EF_CHECK(reason_key(BlockCause::FirstScan) == "gate.detection_pending");
    r.det.set_library(lib(LibraryScanState::Failed));
    EF_CHECK(r.det.cause() == BlockCause::LibraryError);  // "surec kimligi" denmez
    WatchResult w;
    w.detection = DetectionState::Ok;
    w.observed_at_ms = r.now;
    r.det.on_result(3, w);  // izleyici Ok der ama kitaplik guvenilmez
    EF_CHECK(r.det.cause() == BlockCause::LibraryError);
    r.det.set_library(lib(LibraryScanState::Ok, 1));
    w.detection = DetectionState::Incomplete;
    r.det.on_result(3, w);
    EF_CHECK(r.det.cause() == BlockCause::ProcessIdentity);
    EF_CHECK(reason_key(BlockCause::ProcessIdentity) == "gate.detection_incomplete");
    w.detection = DetectionState::Failed;
    r.det.on_result(3, w);
    EF_CHECK(r.det.cause() == BlockCause::ProcessListFailed);
    w.detection = DetectionState::Ok;
    r.det.on_result(3, w);
    EF_CHECK(r.det.cause() == BlockCause::None);
    r.now += kMaxDetectionAgeMs + 1;
    EF_CHECK(r.det.cause() == BlockCause::Stale);
    // Her nedenin kullanici metni farkli anahtardir.
    EF_CHECK(reason_key(BlockCause::LibraryError) != reason_key(BlockCause::ProcessIdentity));
    EF_CHECK(reason_key(BlockCause::Stale) != reason_key(BlockCause::ProcessListFailed));
}

// ---- B1: gercek yan etki yolu ----
EF_TEST(b1_a_to_b_no_restore_until_last_closes) {
    Rig r;
    r.auto_on();
    r.scan({game("a", 1, 1)});
    r.now += 3000;
    r.scan({game("b", 2, 2)});  // A kapandi, ayni tarama B'yi gosterdi
    EF_CHECK(r.backend.restores == 0 && r.exits == 0 && r.recoveries == 0);
    EF_CHECK(r.det.running().size() == 1 && r.det.running()[0] == "b");
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1 && r.exits == 1 && r.recoveries == 1);
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1 && r.exits == 1);  // tam bir kez
}

EF_TEST(b1_pid_reuse_is_new_session_without_restore) {
    Rig r;
    r.auto_on();
    r.scan({game("a", 7, 100)});
    r.now += 3000;
    r.scan({game("a", 7, 200)});  // ayni PID, farkli olusturma zamani
    EF_CHECK(r.det.active() && r.det.active()->create_time == 200);
    EF_CHECK(r.backend.restores == 0);
}

EF_TEST(b1_one_of_two_games_closing_keeps_gate_shut) {
    Rig r;
    r.auto_on();
    r.scan({game("a", 1, 1), game("b", 2, 2)});
    r.now += 3000;
    r.scan({game("b", 2, 2)});
    EF_CHECK(r.backend.restores == 0);
    EF_CHECK(r.det.gate(GuardedAction::SettingMutation).reason_key == "gate.game_running");
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1);
}

EF_TEST(b1_error_then_recovery_no_false_close) {
    Rig r;
    r.auto_on();
    r.scan({game("a", 1, 1)});
    r.now += 3000;
    r.bad(DetectionState::Failed);  // "oyun kapandi" sayilmaz
    EF_CHECK(r.backend.restores == 0 && r.det.running().size() == 1);
    r.now += 3000;
    r.bad(DetectionState::Incomplete);
    EF_CHECK(r.backend.restores == 0);
    r.now += 3000;
    r.scan({game("a", 1, 1)});  // ayni oyun hala acik
    EF_CHECK(r.backend.restores == 0);
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1 && r.exits == 1);
}

EF_TEST(b1_owed_work_survives_stale_delivery_and_runs_once) {
    Rig r;
    r.auto_on();
    r.scan({game("a", 1, 1)});
    // Son kapanis gozlemi gec islendi: kanit bayat, yan etki calismaz ama kaybolmaz.
    WatchResult w;
    w.detection = DetectionState::Ok;
    w.observed_at_ms = r.now + 1;
    r.now += kMaxDetectionAgeMs + 5000;
    r.det.on_result(1, w);
    EF_CHECK(r.backend.restores == 0 && r.exits == 0);
    r.scan({});  // ilk taze guvenilir sonuc
    EF_CHECK(r.backend.restores == 1 && r.exits == 1 && r.recoveries == 1);
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1 && r.exits == 1);
}

EF_TEST(b1_owed_work_waits_if_game_reopens) {
    Rig r;
    r.scan({game("a", 1, 1)});
    WatchResult w;
    w.detection = DetectionState::Ok;
    w.observed_at_ms = r.now + 1;
    r.now += kMaxDetectionAgeMs + 5000;
    r.det.on_result(1, w);
    r.scan({game("c", 3, 3)});
    EF_CHECK(r.exits == 0);
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.exits == 1 && r.recoveries == 1);
}

EF_TEST(b1_first_scan_auto_restore_once) {
    Rig r;
    r.auto_on();
    r.scan({});
    EF_CHECK(r.backend.restores == 1);
    EF_CHECK(r.exits == 0);  // "son oyun kapandi" degil
}

EF_TEST(b1_owe_recovery_runs_on_next_fresh_scan) {
    Rig r;
    r.det.owe_recovery();
    EF_CHECK(r.recoveries == 0);
    r.scan({game("a", 1, 1)});
    EF_CHECK(r.recoveries == 0);
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.recoveries == 1);
}

// Inceleme regresyonlari: eski "oyun calisiyor" listesi gercek nedeni gizlemez; borclu is kaybolmaz.
EF_TEST(review_stale_running_list_does_not_hide_real_cause) {
    Rig r;
    r.scan({game("a", 1, 1)});
    EF_CHECK(r.det.cause() == BlockCause::GameRunning);
    r.now += 3000;
    r.bad(DetectionState::Failed);
    EF_CHECK(r.det.cause() == BlockCause::ProcessListFailed);  // yeniden tarama yolu acik
    EF_CHECK(!r.det.gate(GuardedAction::SettingMutation).allowed);
    EF_CHECK(r.det.running().size() == 1);  // ama "oyun kapandi" da sayilmadi
    r.scan({game("a", 1, 1)});
    r.det.set_library(lib(LibraryScanState::Partial, 1));
    EF_CHECK(r.det.cause() == BlockCause::LibraryError);
    r.det.set_library(lib(LibraryScanState::Ok, 1));
    r.now += kMaxDetectionAgeMs + 1;
    EF_CHECK(r.det.cause() == BlockCause::Stale);
}

EF_TEST(review_owe_restore_retries_on_next_fresh_scan_once) {
    Rig r;
    r.auto_on();
    r.scan({game("a", 1, 1)});
    r.det.owe_restore();
    EF_CHECK(r.backend.restores == 0);  // oyun varken yok
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1);
    r.now += 3000;
    r.scan({});
    EF_CHECK(r.backend.restores == 1);
}
