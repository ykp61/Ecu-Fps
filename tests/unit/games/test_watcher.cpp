#include "core/games/protection_gate.h"
#include "core/games/watcher.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {

ProcessSnapshot ok(std::initializer_list<ProcessInfo> processes, std::size_t inaccessible = 0) {
    ProcessSnapshot s;
    s.state = DetectionState::Ok;
    s.processes = processes;
    s.inaccessible_count = inaccessible;
    return s;
}

ProcessInfo proc(std::uint32_t pid, std::uint64_t create_time, std::string path, std::string name = {}) {
    ProcessInfo p;
    p.pid = pid;
    p.create_time = create_time;
    p.path_readable = !path.empty();
    if (name.empty()) {
        const auto slash = path.find_last_of("\\/");
        name = slash == std::string::npos ? path : path.substr(slash + 1);
    }
    p.exe_name = std::move(name);
    p.exe_path = std::move(path);
    return p;
}

// Uretim zinciri: tarama sonucu -> izleyici -> kapi baglami. App bu iki karari ayni kosuda kullanir.
GateDecision gate_after_scan(const WatchResult& r, GuardedAction a = GuardedAction::SettingMutation) {
    GateContext c;
    c.detection_ok = r.detection == DetectionState::Ok;
    c.detection_age_ms = 0;  // bu karar guncel tarama sonucudur
    for (const auto& g : r.running) c.running_game_ids.push_back(g.install_dir);
    return check(a, c);
}

}  // namespace

EF_TEST(watcher_start_stop_and_pid_reuse) {
    GameWatcher w({"E:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive\\"});
    const std::string exe = "e:\\steamlibrary\\steamapps\\common\\counter-strike global offensive\\game\\bin\\win64\\cs2.exe";
    EF_CHECK(w.update(ok({proc(100, 5, "c:\\windows\\explorer.exe")})).events.empty());
    auto r = w.update(ok({proc(100, 5, "c:\\windows\\explorer.exe"), proc(4242, 77, exe)}));
    EF_CHECK(r.detection == DetectionState::Ok);
    EF_CHECK(r.events.size() == 1 && r.events[0].kind == WatchEventKind::Started && r.events[0].game.pid == 4242 && w.active());
    EF_CHECK(r.running.size() == 1 && r.running[0].pid == 4242);
    EF_CHECK(w.update(ok({proc(4242, 77, exe)})).events.empty());  // calismaya devam
    r = w.update(ok({proc(4242, 99, exe)}));  // ayni PID, farkli olusturma zamani: eski kapandi, yeni basladi
    EF_CHECK(r.events.size() == 2 && r.events[0].kind == WatchEventKind::Stopped && r.events[1].kind == WatchEventKind::Started &&
              r.events[1].game.create_time == 99);
    r = w.update(ok({}));
    EF_CHECK(r.events.size() == 1 && r.events[0].kind == WatchEventKind::Stopped && !w.active());
    EF_CHECK(gate_after_scan(r).allowed);  // guvenilir tarama, oyun yok: islem serbest
}

EF_TEST(watcher_ignores_helpers_overlays_and_unknown) {
    GameWatcher w({"f:\\steamlibrary\\steamapps\\common\\red dead redemption 2"});
    auto r = w.update(ok({proc(1, 1, "f:\\steamlibrary\\steamapps\\common\\red dead redemption 2\\playrdr2.exe"),
                          proc(2, 1, "f:\\steamlibrary\\steamapps\\common\\red dead redemption 2\\easyanticheat.exe"),
                          proc(3, 1, "f:\\steamlibrary\\steamapps\\common\\red dead redemption 2\\_commonredist\\vc_redist.x64.exe"),
                          proc(5, 1, "")}));
    EF_CHECK(r.events.size() == 1);  // yalniz playrdr2 (helper listesinde degil) -> bir oyun
    // B2: yolu okunan ama olusturma zamani olmayan oyun klasoru sureci yokluk degildir (belirsizlik).
    r = w.update(ok({proc(1, 1, "f:\\steamlibrary\\steamapps\\common\\red dead redemption 2\\playrdr2.exe"),
                     proc(4, 0, "f:\\steamlibrary\\steamapps\\common\\red dead redemption 2\\rdr2.exe")}));
    EF_CHECK(r.detection == DetectionState::Incomplete && r.events.empty());
    GameWatcher none({});
    EF_CHECK(!none.has_games() && none.update(ok({proc(1, 1, "c:\\x\\game.exe")})).events.empty());
    EF_CHECK(is_helper_exe("unins000.exe") && is_helper_exe("crashreportclient.exe") && !is_helper_exe("cs2.exe"));
}

EF_TEST(watcher_failed_or_unknown_scan_is_not_no_game) {
    GameWatcher w({"E:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive"});
    const std::string exe = "e:\\steamlibrary\\steamapps\\common\\counter-strike global offensive\\game\\bin\\win64\\cs2.exe";
    // Ilk tarama henuz yok: hicbir olay uretilmez, kapi kapali.
    ProcessSnapshot unknown;
    unknown.state = DetectionState::Unknown;
    auto u = w.update(unknown);
    EF_CHECK(u.events.empty() && u.detection == DetectionState::Unknown && !gate_after_scan(u).allowed);

    auto r = w.update(ok({proc(77, 8, exe)}));
    EF_CHECK(r.events.size() == 1 && r.events[0].kind == WatchEventKind::Started && w.active());
    const auto before = *w.active();

    // Tarama basarisiz: "oyun kapandi" URETILMEZ; etkin oyun korunur; kapi fail-closed.
    ProcessSnapshot failed;
    failed.state = DetectionState::Failed;
    failed.processes = {proc(77, 8, exe), proc(99, 1, "c:\\windows\\explorer.exe")};
    u = w.update(failed);
    EF_CHECK(u.events.empty() && u.running.empty() && u.detection == DetectionState::Failed);
    EF_CHECK(w.active() && w.active()->pid == before.pid && w.active()->create_time == before.create_time);
    const auto denied = gate_after_scan(u);
    EF_CHECK(!denied.allowed && denied.reason_key == "gate.detection_failed");

    // Toparlanma: guvenilir tarama yine oyunu goruyor -> sahte basla/dur yok.
    u = w.update(ok({proc(77, 8, exe)}));
    EF_CHECK(u.events.empty() && u.detection == DetectionState::Ok && u.running.size() == 1);
    EF_CHECK(gate_after_scan(u).reason_key == "gate.game_running");

    // Toparlanma sonrasi oyun gercekten kapandi: tek Stopped olayi.
    u = w.update(ok({}));
    EF_CHECK(u.events.size() == 1 && u.events[0].kind == WatchEventKind::Stopped && !w.active());
    EF_CHECK(gate_after_scan(u).allowed);
}

EF_TEST(watcher_incomplete_identity_blocks_without_locking_forever) {
    const std::string dir = "E:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive";
    const std::string exe = "e:\\steamlibrary\\steamapps\\common\\counter-strike global offensive\\game\\bin\\win64\\cs2.exe";
    // Bilinen oyun EXE adi tasiyan ama yolu okunamayan surec: belirsizlik -> kapi kapali.
    GameWatcher known({dir}, {"cs2.exe"});
    ProcessSnapshot snap;
    snap.state = DetectionState::Ok;
    snap.processes = {proc(500, 0, "", "cs2.exe"), proc(501, 9, "", "lsass.exe")};
    snap.inaccessible_count = 2;
    auto u = known.update(snap);
    EF_CHECK(u.events.empty() && u.detection == DetectionState::Incomplete && u.inaccessible_count == 2);
    EF_CHECK(!gate_after_scan(u).allowed);

    // B2: bilinen ad listesi BOS. cs2.exe daha once oyun olarak GOZLENDI; erisim kaybi yokluk degildir.
    GameWatcher observed({dir}, {});
    u = observed.update(ok({proc(700, 42, exe, "cs2.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.size() == 1 && u.running.size() == 1);
    const auto before = *observed.active();
    u = observed.update(ok({proc(700, 0, "", "cs2.exe"), proc(701, 9, "", "explorer.exe")}));
    EF_CHECK(u.detection == DetectionState::Incomplete && u.events.empty() && !gate_after_scan(u).allowed);
    EF_CHECK(observed.active() && observed.active()->pid == before.pid);  // etkin oyun korunur

    // Erisim geri gelince sahte basla/dur yok; kapi oyun calisiyor der.
    u = observed.update(ok({proc(700, 42, exe, "cs2.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.empty());
    EF_CHECK(gate_after_scan(u).reason_key == "gate.game_running");

    // Oyun gercekten kapandi: tek Stopped. Ardindan siradan sistem reddi kilitlemez.
    u = observed.update(ok({proc(701, 9, "", "explorer.exe"), proc(702, 9, "", "lsass.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.size() == 1 && u.events[0].kind == WatchEventKind::Stopped);
    u = observed.update(ok({proc(701, 9, "", "explorer.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.empty() && gate_after_scan(u).allowed);
}

EF_TEST(watcher_path_readable_without_create_time_is_uncertain) {
    const std::string dir = "E:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive";
    const std::string exe = "e:\\steamlibrary\\steamapps\\common\\counter-strike global offensive\\game\\bin\\win64\\cs2.exe";
    GameWatcher w({dir}, {});  // bilinen ad listesi bos
    // Yol okunuyor, olusturma zamani yok: oyun klasoru altinda yardimci olmayan surec -> belirsiz.
    auto u = w.update(ok({proc(800, 0, exe, "cs2.exe")}));
    EF_CHECK(u.detection == DetectionState::Incomplete && u.events.empty() && !gate_after_scan(u).allowed);
    // Ayni eksik zaman oyun klasoru disinda veya yardimci ise belirsizlik degildir.
    u = w.update(ok({proc(801, 0, "c:\\tools\\tool.exe", "tool.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.empty());
    u = w.update(ok({proc(802, 0, dir + "\\easyanticheat.exe", "easyanticheat.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.empty());
    // Zaman sonra okunursa oyun baslar (tek Started).
    u = w.update(ok({proc(800, 77, exe, "cs2.exe")}));
    EF_CHECK(u.detection == DetectionState::Ok && u.events.size() == 1 && u.events[0].kind == WatchEventKind::Started);
}

EF_TEST(watcher_reports_all_running_games_for_gate) {
    GameWatcher w({"c:\\games\\a", "c:\\games\\b"});
    auto r = w.update(ok({proc(10, 1, "c:\\games\\a\\a.exe"), proc(11, 1, "c:\\games\\b\\b.exe")}));
    EF_CHECK(r.events.size() == 1 && r.events[0].kind == WatchEventKind::Started);  // tek profil: ilk oyun etkin
    EF_CHECK(r.running.size() == 2);  // kapi TUM calisan oyunlari gorur
    GateContext c;
    c.detection_ok = r.detection == DetectionState::Ok;
    c.detection_age_ms = 0;
    for (const auto& g : r.running) c.running_game_ids.push_back(g.install_dir);
    EF_CHECK(check(GuardedAction::HeavyBenchmark, c).reason_key == "gate.game_running");
}
