// Steam manifesti EXE yolu vermez: izleyicinin gordugu yol kalici tutulur, tahmin edilmez.
#include "core/games/exe_resolver.h"
#include "core/games/watcher.h"
#include "core/perf/admin_plan.h"
#include "core/settings/preferences.h"
#include "ef_test.h"

using namespace ef::core;

EF_TEST(prefs_observed_exes_roundtrip_bounded) {
    settings::Preferences p;
    for (int i = 0; i < 70; ++i) p.observed_game_exes.push_back("e:\\g\\" + std::to_string(i) + ".exe");
    const auto parsed = settings::Preferences::parse(p.serialize());
    EF_CHECK(parsed.ok() && parsed.value().observed_game_exes.size() == 64);
    const auto old = settings::Preferences::parse(R"({"schema_version":1})");
    EF_CHECK(old.ok() && old.value().observed_game_exes.empty());
}

EF_TEST(observed_exe_resolves_steam_game_without_guessing) {
    const std::string dir = "e:\\steamlibrary\\steamapps\\common\\counter-strike global offensive";
    const std::vector<std::string> seen{dir + "\\game\\bin\\win64\\cs2.exe", "f:\\other\\x.exe"};
    const auto r = games::resolve_game_exe(dir, "", seen);
    EF_CHECK(r.status == games::ExeResolveStatus::Resolved && r.path == seen[0]);
    EF_CHECK(games::resolve_game_exe(dir, "", {}).status == games::ExeResolveStatus::NotObserved);
}

static games::WatchResult poll(games::DetectionState st, std::vector<games::GameStart> running = {}) {
    games::WatchResult r;
    r.detection = st;
    r.running = std::move(running);
    return r;
}

EF_TEST(only_reliable_empty_poll_proves_no_game) {
    using games::DetectionState;
    EF_CHECK(games::poll_proves_no_game(poll(DetectionState::Ok)));
    EF_CHECK(!games::poll_proves_no_game(poll(DetectionState::Ok, {games::GameStart{"e:\\g\\a.exe", "e:\\g", 7, 9}})));
    // Korumali surecin yolu okunamadi: oyun hala calisiyor olabilir, klasor taranmaz.
    EF_CHECK(!games::poll_proves_no_game(poll(DetectionState::Incomplete)));
    EF_CHECK(!games::poll_proves_no_game(poll(DetectionState::Failed)));
    EF_CHECK(!games::poll_proves_no_game(poll(DetectionState::Unknown)));
}

EF_TEST(library_rebuild_never_goes_deep_while_game_may_run) {
    const auto d = [](bool must, bool wanted, bool none) { return games::decide_library_rebuild({must, wanted, none}); };
    EF_CHECK(d(true, false, true).rebuild && d(true, false, true).deep);
    EF_CHECK(d(true, false, false).rebuild && !d(true, false, false).deep);  // liste guncellenir, derin is ertelenir
    EF_CHECK(!d(false, true, false).rebuild);                                 // oyun varken beklenir
    EF_CHECK(d(false, true, true).rebuild && d(false, true, true).deep);
    EF_CHECK(!d(false, false, true).rebuild);
}

EF_TEST(watcher_set_library_keeps_active_game_so_stop_is_reported) {
    games::GameWatcher w({"e:\\g"}, {});
    games::ProcessSnapshot snap;
    snap.state = games::DetectionState::Ok;
    snap.processes.push_back({7, 9, "e:\\g\\a.exe", "a.exe", true});
    EF_CHECK(w.update(snap).events.size() == 1);
    w.set_library({"e:\\g", "e:\\h"}, {});
    EF_CHECK(w.active().has_value());
    snap.processes.clear();
    const auto r = w.update(snap);
    EF_CHECK(r.events.size() == 1 && r.events[0].kind == games::WatchEventKind::Stopped);
}

EF_TEST(copied_watcher_keeps_history_so_unreadable_game_is_not_no_game) {
    // Yoklama gercek izleyicinin kopyasiyla yapilir: taze izleyici yol okunamayan oyunu "yok" sanar.
    games::GameWatcher w({"e:\\g"}, {});
    games::ProcessSnapshot seen;
    seen.state = games::DetectionState::Ok;
    seen.processes.push_back({7, 9, "e:\\g\\a.exe", "a.exe", true});
    (void)w.update(seen);
    games::ProcessSnapshot denied;
    denied.state = games::DetectionState::Ok;
    denied.processes.push_back({7, 0, "", "a.exe", false});
    games::GameWatcher copy = w;
    copy.set_library({"e:\\g"}, {});
    EF_CHECK(!games::poll_proves_no_game(copy.update(denied)));
    games::GameWatcher fresh({"e:\\g"}, {});
    EF_CHECK(games::poll_proves_no_game(fresh.update(denied)));  // kopya olmadan yanlis kanit (regresyon sebebi)
    EF_CHECK(w.active().has_value());                            // kopya asil durumu degistirmez
}

EF_TEST(observed_exe_needs_sustained_run_and_reports_once) {
    using games::DetectionState;
    const games::GameStart boot{"e:\\g\\boot.exe", "e:\\g", 7, 9};
    games::ObservedExeTracker t;
    const auto limit = games::ObservedExeTracker::kMinRunMs;
    EF_CHECK(t.update(poll(DetectionState::Ok, {boot}), 1000).empty());
    EF_CHECK(t.update(poll(DetectionState::Ok), 2000).empty());  // on-yukleyici kapandi
    EF_CHECK(t.update(poll(DetectionState::Ok, {boot}), 3000 + limit).empty());  // yeni gozlem, sayac bastan
    EF_CHECK(t.update(poll(DetectionState::Incomplete), 3000 + 2 * limit).empty());  // belirsiz tarama kayit uretmez
    const auto got = t.update(poll(DetectionState::Ok, {boot}), 3000 + 2 * limit);
    EF_CHECK(got.size() == 1 && got[0] == boot.game_id);
    EF_CHECK(t.update(poll(DetectionState::Ok, {boot}), 3000 + 3 * limit).empty());  // bir kez
}

EF_TEST(game_profile_record_never_claims_partial_or_conflict_as_set) {
    using perf::GameProfileRecord;
    const auto ops = perf::nvidia_game_profile_ops("a.exe");
    EF_CHECK(ops.size() == 2);
    const std::vector<std::string> none, one{ops[0].target}, both{ops[0].target, ops[1].target};
    EF_CHECK(perf::game_profile_record("a.exe", none, none, none, none) == GameProfileRecord::None);
    EF_CHECK(perf::game_profile_record("a.exe", both, none, none, none) == GameProfileRecord::Active);
    EF_CHECK(perf::game_profile_record("a.exe", one, none, none, none) == GameProfileRecord::Partial);
    EF_CHECK(perf::game_profile_record("a.exe", one, none, none, {ops[1].target}) == GameProfileRecord::Conflict);
    EF_CHECK(perf::game_profile_record("a.exe", none, one, none, one) == GameProfileRecord::Conflict);  // cikis "Koru"; Geri al degil
    EF_CHECK(perf::game_profile_record("a.exe", none, one, none, none) == GameProfileRecord::Attention);
    EF_CHECK(perf::game_profile_record("a.exe", none, none, one, none) == GameProfileRecord::Attention);
    EF_CHECK(perf::game_profile_record("b.exe", both, none, none, none) == GameProfileRecord::None);
}
