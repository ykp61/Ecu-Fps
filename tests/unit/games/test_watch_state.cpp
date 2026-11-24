// B1: App'in kullandigi uretim yolu (WatchState) uzerinden oyun gecisleri.
// App::on_watch_result yalniz bu sonucu uygular; yan etki karari burada test edilir.
#include "core/games/watch_state.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {

WatchResult ok_scan(std::initializer_list<GameStart> running) {
    WatchResult r;
    r.detection = DetectionState::Ok;
    r.running = running;
    return r;
}

WatchResult failed_scan() {
    WatchResult r;
    r.detection = DetectionState::Failed;
    return r;
}

GameStart game(const std::string& dir, std::uint32_t pid, std::uint64_t create_time) {
    GameStart g;
    g.install_dir = dir;
    g.game_id = dir + "\\game.exe";
    g.pid = pid;
    g.create_time = create_time;
    return g;
}

// App::on_watch_result'un yan etki dagitimi: yalniz nihai guvenilir sonuctan.
struct Effects {
    int auto_restore = 0;      // perf_auto_restore cagrisi
    int pending_exit = 0;      // check_pending_exit cagrisi
    int pending_recovery = 0;  // run_pending_recovery cagrisi
    int mutation_calls = 0;    // oyun varken backend'e giden mutasyon (0 olmali)

    void apply(const WatchInstall& r) {
        if (!r.detection_ok) return;  // fail-closed: hicbir yan etki yok
        if (r.auto_restore) {
            ++auto_restore;
            // App'te otomatik geri alma yalniz oyun yokken ve kapi acikken calisir.
            if (r.running_dirs.empty()) ++mutation_calls;
        }
        if (r.last_game_closed) {
            ++pending_exit;
            ++pending_recovery;
        }
    }
};

}  // namespace

EF_TEST(watch_state_a_to_b_installs_final_state_atomically) {
    WatchState s;
    Effects fx;
    fx.apply(s.apply(ok_scan({game("c:\\games\\a", 10, 100)})));
    EF_CHECK(s.active() && s.active()->install_dir == "c:\\games\\a");

    // A kapandi, B acildi: watcher olaylari Stopped(A), Started(B) sirasinda uretir.
    WatchResult r = ok_scan({game("c:\\games\\b", 11, 200)});
    r.events.push_back({WatchEventKind::Stopped, game("c:\\games\\a", 10, 100)});
    r.events.push_back({WatchEventKind::Started, game("c:\\games\\b", 11, 200)});
    const auto st = s.apply(r);
    fx.apply(st);
    EF_CHECK(st.detection_ok);
    EF_CHECK(st.running_dirs.size() == 1 && st.running_dirs[0] == "c:\\games\\b");
    EF_CHECK(st.active && st.active->install_dir == "c:\\games\\b");
    EF_CHECK(!st.last_game_closed && !st.auto_restore);
    EF_CHECK(fx.mutation_calls == 0 && fx.pending_exit == 0 && fx.pending_recovery == 0);
}

EF_TEST(watch_state_pid_reuse_is_new_session_not_last_close) {
    WatchState s;
    Effects fx;
    fx.apply(s.apply(ok_scan({game("c:\\games\\a", 7, 100)})));
    // Ayni PID, farkli olusturma zamani: eski surec kapandi, yenisi basladi (tek oyun).
    WatchResult r = ok_scan({game("c:\\games\\a", 7, 200)});
    r.events.push_back({WatchEventKind::Stopped, game("c:\\games\\a", 7, 100)});
    r.events.push_back({WatchEventKind::Started, game("c:\\games\\a", 7, 200)});
    const auto st = s.apply(r);
    fx.apply(st);
    EF_CHECK(st.running_dirs.size() == 1);
    EF_CHECK(st.active && st.active->create_time == 200);
    EF_CHECK(!st.last_game_closed && !st.auto_restore);
    EF_CHECK(fx.mutation_calls == 0 && fx.pending_exit == 0);
}

EF_TEST(watch_state_one_of_two_games_closing_does_not_restore) {
    WatchState s;
    Effects fx;
    fx.apply(s.apply(ok_scan({game("c:\\games\\a", 10, 1), game("c:\\games\\b", 11, 1)})));
    EF_CHECK(s.active() && s.active()->install_dir == "c:\\games\\a");
    // B kapandi; A calismaya devam ediyor. Kapi icin liste A'yi korur, mutasyon yok.
    WatchResult r = ok_scan({game("c:\\games\\a", 10, 1)});
    r.events.push_back({WatchEventKind::Stopped, game("c:\\games\\b", 11, 1)});
    const auto st = s.apply(r);
    fx.apply(st);
    EF_CHECK(st.running_dirs.size() == 1 && st.running_dirs[0] == "c:\\games\\a");
    EF_CHECK(st.active && st.active->install_dir == "c:\\games\\a");
    EF_CHECK(!st.last_game_closed && !st.auto_restore);
    EF_CHECK(fx.mutation_calls == 0 && fx.pending_exit == 0 && fx.pending_recovery == 0);
}

EF_TEST(watch_state_last_game_closing_fires_pending_work_exactly_once) {
    WatchState s;
    Effects fx;
    fx.apply(s.apply(ok_scan({game("c:\\games\\a", 10, 1)})));
    WatchResult close = ok_scan({});
    close.events.push_back({WatchEventKind::Stopped, game("c:\\games\\a", 10, 1)});
    auto st = s.apply(close);
    fx.apply(st);
    EF_CHECK(st.last_game_closed && st.auto_restore);
    EF_CHECK(fx.mutation_calls == 1 && fx.pending_exit == 1 && fx.pending_recovery == 1);
    // Sonraki guvenilir bos taramalar ayni isi TEKRAR baslatmaz.
    st = s.apply(ok_scan({}));
    fx.apply(st);
    st = s.apply(ok_scan({}));
    fx.apply(st);
    EF_CHECK(!st.last_game_closed && !st.auto_restore);
    EF_CHECK(fx.mutation_calls == 1 && fx.pending_exit == 1 && fx.pending_recovery == 1);
}

EF_TEST(watch_state_failed_scan_preserves_then_recovery_close_is_once) {
    WatchState s;
    Effects fx;
    fx.apply(s.apply(ok_scan({game("c:\\games\\a", 10, 1)})));
    // Tarama basarisiz: liste/etkin oyun korunur, kapi kapali, yan etki yok.
    auto st = s.apply(failed_scan());
    fx.apply(st);
    EF_CHECK(!st.detection_ok && st.detection == DetectionState::Failed);
    EF_CHECK(st.running_dirs.size() == 1 && st.running_dirs[0] == "c:\\games\\a");
    EF_CHECK(st.active && st.active->install_dir == "c:\\games\\a");
    EF_CHECK(!st.last_game_closed && !st.auto_restore);
    EF_CHECK(fx.mutation_calls == 0 && fx.pending_exit == 0 && fx.pending_recovery == 0);
    // Toparlanma: oyun gercekten kapanmis -> gecis bir kez bildirilir.
    st = s.apply(ok_scan({}));
    fx.apply(st);
    EF_CHECK(st.last_game_closed && fx.mutation_calls == 1 && fx.pending_exit == 1);
}

EF_TEST(watch_state_first_safe_scan_empty_restores_once_not_last_close) {
    WatchState s;
    Effects fx;
    s.reset();
    EF_CHECK(s.detection() == DetectionState::Unknown);
    // Ilk guvenilir tarama bos: onceki oturumdan kalan otomatik Acik geri alinir.
    auto st = s.apply(ok_scan({}));
    fx.apply(st);
    EF_CHECK(st.first_safe_scan && st.auto_restore && !st.last_game_closed);
    EF_CHECK(fx.mutation_calls == 1 && fx.pending_exit == 0 && fx.pending_recovery == 0);
    st = s.apply(ok_scan({}));
    fx.apply(st);
    EF_CHECK(!st.first_safe_scan && !st.auto_restore && fx.mutation_calls == 1);
}

EF_TEST(watch_state_unknown_scan_does_not_finish_first_scan) {
    WatchState s;
    s.reset();
    WatchResult u;
    u.detection = DetectionState::Unknown;
    const auto st = s.apply(u);
    EF_CHECK(!st.detection_ok && !st.first_safe_scan && !st.auto_restore);
    EF_CHECK(!s.first_safe_scan_done());
}
