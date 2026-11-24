#include "core/games/protection_gate.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {
GateContext detection_ok_context() {
    GateContext c;
    c.detection_ok = true;
    c.detection_age_ms = 0;  // guncel guvenilir tarama
    return c;
}
}  // namespace

EF_TEST(gate_builtin_matrix_cs2_vac_and_faceit_unverified_no_capture) {
    const auto& m = builtin_matrix();
    EF_CHECK(m.size() == 10);  // F-60: CS2 + EAC/BattlEye oyunlari; T05: +VALORANT, LoL, 2 Call of Duty
    int cs2 = 0;
    for (const auto& e : m) {
        EF_CHECK(e.state == CompatState::Unverified && !e.live_capture_enabled);
        EF_CHECK(e.game_build.empty() && e.protection_build.empty() && e.collector_commit.empty() && e.os_driver_scope.empty());
        if (e.game_id == "730") ++cs2;
    }
    EF_CHECK(cs2 == 2);  // VAC/Trusted Mode + FACEIT
    EF_CHECK(m[2].protection == "Easy Anti-Cheat");
    EF_CHECK(m[3].protection == "BattlEye");
}

EF_TEST(gate_protection_status_maps_services_and_never_claims_absent_on_query_failure) {
    ProtectionEvidence failed;  // query_ok=false
    EF_CHECK(protection_status(failed).empty());
    ProtectionEvidence e;
    e.query_ok = true;
    e.eac_installed = true;
    e.eac_running = true;
    e.vanguard_installed = true;  // kurulu, calismiyor
    auto s = protection_status(e);
    EF_CHECK(s.size() == 3);
    EF_CHECK(s[0].system == ProtectionSystem::EasyAntiCheat && s[0].installed && s[0].running);
    EF_CHECK(s[1].system == ProtectionSystem::BattlEye && !s[1].installed && !s[1].running);
    EF_CHECK(s[2].system == ProtectionSystem::Vanguard && s[2].installed && !s[2].running);
}

EF_TEST(gate_live_capture_denied_for_cs2_and_unknown_games) {
    GateContext c = detection_ok_context();
    c.capture_game_id = "730";
    c.capture_game_build = "1.40.0.0";
    c.capture_protection_build = "1.0";
    c.capture_collector_commit = "abc";
    c.capture_os_driver_scope = "win11-nv-1";
    auto d = check(GuardedAction::LiveCapture, c);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unverified");
    c.capture_game_id = "some_other_game";
    d = check(GuardedAction::LiveCapture, c);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unknown");  // bilinmeyen kombinasyon kapali
    c.capture_game_id.clear();
    EF_CHECK(!check(GuardedAction::LiveCapture, c).allowed);
}

EF_TEST(gate_live_capture_requires_detection_evidence_scope_and_build_match) {
    std::vector<CompatEntry> m{{"g", "G", "A", CompatState::VerifiedForBuild, "b1", "p1", "c1", "scope-1", true},
                               {"g", "G", "B", CompatState::Unverified, "", "", "", "", false}};
    GateContext c = detection_ok_context();
    c.capture_game_id = "g";
    c.capture_game_build = "b1";
    c.capture_protection_build = "p1";
    c.capture_collector_commit = "c1";
    c.capture_os_driver_scope = "scope-1";
    EF_CHECK(!check(GuardedAction::LiveCapture, c, m).allowed);  // ikinci koruma dogrulanmadi
    m[1] = {"g", "G", "B", CompatState::VerifiedForBuild, "b1", "p1", "c1", "scope-1", true};
    EF_CHECK(check(GuardedAction::LiveCapture, c, m).allowed);
    c.capture_game_build = "b2";  // oyun guncellendi: eslesme yok
    auto d = check(GuardedAction::LiveCapture, c, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_stale");
    c.capture_game_build = "b1";
    c.capture_protection_build = "p2";
    d = check(GuardedAction::LiveCapture, c, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_stale");
    c.capture_protection_build = "p1";
    c.capture_collector_commit.clear();
    d = check(GuardedAction::LiveCapture, c, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_evidence");  // gozlenen kimlik eksik
    c.capture_collector_commit = "c1";
    c.capture_os_driver_scope = "scope-2";
    EF_CHECK(check(GuardedAction::LiveCapture, c, m).reason_key == "gate.capture_stale");
    c.capture_os_driver_scope = "scope-1";
    m[0].protection_build.clear();
    EF_CHECK(check(GuardedAction::LiveCapture, c, m).reason_key == "gate.capture_evidence");  // matris kaniti eksik
    m[0].protection_build = "p1";
    m[0].state = CompatState::Blocked;
    d = check(GuardedAction::LiveCapture, c, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_blocked");
    m[0].state = CompatState::Stale;
    EF_CHECK(check(GuardedAction::LiveCapture, c, m).reason_key == "gate.capture_stale");
    c.detection_ok = false;
    EF_CHECK(check(GuardedAction::LiveCapture, c, m).reason_key == "gate.detection_failed");
}

EF_TEST(gate_capture_source_requires_running_game_and_verified_matrix) {
    // Kaynak yok: uydurma yerine acik neden (Oyunda Test devre disi).
    auto d = capture_source({});
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_no_game");
    // Calisan CS2 (gomulu matris dogrulanmamis): canli olcum kapali, ret nedeni gorunur.
    d = capture_source({{"730", "1.40.0.0"}});
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unverified");
    // Gozlenen surum bilinmiyorsa da acilmaz.
    d = capture_source({{"730", ""}});
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unverified");
    // Ileri tarihli dogrulanmis matris: yalniz dort kanit alani da eslesen oyun acilir.
    std::vector<CompatEntry> m{{"g", "G", "P", CompatState::VerifiedForBuild, "b1", "p1", "c1", "scope-1", true}};
    EF_CHECK(!capture_source({{"g", "b0", "p1", "c1", "scope-1"}}, m).allowed);
    d = capture_source({{"other", ""}, {"g", "b1", "p1", "c1", "scope-1"}}, m);
    EF_CHECK(d.allowed && d.reason_key.empty());
    // Oyun surumu biliniyor ama koruma/toplayici/kapsam gozlenmedi: kanit eksik.
    d = capture_source({{"g", "b1"}}, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_evidence");
    // Bilinmeyen oyun ret nedeni korunur.
    d = capture_source({{"x", "b1"}}, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unknown");
}

EF_TEST(gate_detection_unknown_or_failed_is_fail_closed) {
    // Varsayilan baglam guvenilir tarama bildirmez: hicbir korumali islem acilmaz.
    GateContext c;
    for (auto a : {GuardedAction::SettingMutation, GuardedAction::DiskCleanup, GuardedAction::HeavyBenchmark, GuardedAction::LiveCapture}) {
        const auto d = check(a, c);
        EF_CHECK(!d.allowed && d.reason_key == "gate.detection_failed");
    }
    c.detection_ok = true;
    c.detection_age_ms = 0;
    for (auto a : {GuardedAction::SettingMutation, GuardedAction::DiskCleanup, GuardedAction::HeavyBenchmark})
        EF_CHECK(check(a, c).allowed);  // guvenilir tarama, oyun yok: izinli
    c.running_game_ids = {"730"};
    for (auto a : {GuardedAction::SettingMutation, GuardedAction::DiskCleanup, GuardedAction::HeavyBenchmark}) {
        const auto d = check(a, c);
        EF_CHECK(!d.allowed && d.reason_key == "gate.game_running");
    }
    c.detection_ok = false;  // algilama hatasi "oyun yok" degildir; calisan oyun bilgisi de yetmez
    EF_CHECK(check(GuardedAction::SettingMutation, c).reason_key == "gate.detection_failed");
}

EF_TEST(gate_stale_detection_is_fail_closed_at_action_boundary) {
    // Yas bilinmiyorsa (-1) guvenilir tarama bildirimi yetmez: eski izinle islem baslatilmaz.
    GateContext c;
    c.detection_ok = true;
    EF_CHECK(!detection_fresh(c.detection_age_ms));
    for (auto a : {GuardedAction::SettingMutation, GuardedAction::DiskCleanup, GuardedAction::HeavyBenchmark, GuardedAction::LiveCapture})
        EF_CHECK(check(a, c).reason_key == "gate.detection_stale");
    // Sinirda kabul, sinirdan sonra red.
    c.detection_age_ms = kMaxDetectionAgeMs;
    EF_CHECK(check(GuardedAction::SettingMutation, c).allowed);
    c.detection_age_ms = kMaxDetectionAgeMs + 1;
    EF_CHECK(check(GuardedAction::SettingMutation, c).reason_key == "gate.detection_stale");
    c.detection_age_ms = -5;
    EF_CHECK(check(GuardedAction::SettingMutation, c).reason_key == "gate.detection_stale");
    // Taze baglamda calisan oyun yine oncelikli neden.
    c.detection_age_ms = 0;
    c.running_game_ids = {"730"};
    EF_CHECK(check(GuardedAction::SettingMutation, c).reason_key == "gate.game_running");
}

// EF-X18-T01: gozlenebilen kapsam doldurulsa bile korunan/bilinmeyen oyunlar kapali; override yok.
EF_TEST(x18_t01_observed_scope_never_opens_builtin_or_unknown_games) {
    const std::string commit = observed_collector_commit("469a68c0abcd");
    const std::string scope = observed_os_driver_scope("26300.9711", "32.0.15.6094");
    EF_CHECK(commit == "469a68c0abcd");
    EF_CHECK(scope == "os=26300.9711;gpu_driver=32.0.15.6094");
    for (const auto& e : builtin_matrix()) {
        GateContext c = detection_ok_context();
        c.capture_game_id = e.game_id;
        c.capture_game_build = "1.0";
        c.capture_collector_commit = commit;
        c.capture_os_driver_scope = scope;  // koruma surumu gozlenmez: bos
        const auto d = check(GuardedAction::LiveCapture, c);
        EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unverified");
        EF_CHECK(e.renderer == GuideRenderer::Unknown && e.state == CompatState::Unverified && !e.live_capture_enabled);
    }
    std::vector<CompatEntry> m{{"g", "G", "P", CompatState::VerifiedForBuild, "b1", "p1", commit, scope, true}};
    auto d = capture_source({{"g", "b1", "", commit, scope}}, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_evidence");  // dogrulanmis satir + gozlenmeyen koruma: kapali
    d = capture_source({{"unknown_game", "b1", "", commit, scope}}, m);
    EF_CHECK(!d.allowed && d.reason_key == "gate.capture_unknown");
}

EF_TEST(x18_observed_scope_fail_closed_on_unknown_inputs) {
    EF_CHECK(observed_collector_commit("unknown").empty());
    EF_CHECK(observed_collector_commit("469a68c0abcd-dirty").empty());
    EF_CHECK(observed_collector_commit("").empty());
    EF_CHECK(observed_collector_commit("XYZ1234").empty());
    EF_CHECK(observed_os_driver_scope("?.?", "32.0").empty());
    EF_CHECK(observed_os_driver_scope("26300.?", "32.0").empty());
    EF_CHECK(observed_os_driver_scope("26300.9711", "").empty());
    EF_CHECK(observed_os_driver_scope("26300.9711", "32.0\n").empty());
}
