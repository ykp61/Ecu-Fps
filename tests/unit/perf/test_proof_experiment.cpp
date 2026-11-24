// P1: ABBA kanit deneyi cekirdegi (saf; sahte yurutucu). Bu bir MOCK testidir: gercek Windows / GPU kosusu degildir.
#include <cmath>

#include "core/perf/proof_experiment.h"
#include "ef_test.h"

using namespace ef::core::perf;

namespace {

std::map<std::string, std::string> cond(const char* power = "ac") {
    return {{"arena", "arena_v1|x"}, {"adapter", "GPU"}, {"driver", "1.0"}, {"res", "1920x1080"}, {"power", power}, {"app", "0.7.1"}};
}

ProofRun make_run(ProofArm arm, double fps, double low = 0) {
    ProofRun r;
    r.arm = arm;
    r.valid = true;
    r.conditions = cond();
    r.avg_fps = fps;
    if (low > 0) r.low1_fps = low;
    r.p99_ms = 1000.0 / (low > 0 ? low : fps);
    return r;
}

// n kosu / kol; kol ortalamalari a (Kapali) ve b (Acik); gurultu deterministik (-1, 0, +1 x noise).
std::vector<ProofRun> arms(double a, double b, double noise, int n, double low_a = 0, double low_b = 0) {
    std::vector<ProofRun> v;
    for (int i = 0; i < n; ++i) {
        const double e = ((i % 3) - 1) * noise;
        v.push_back(make_run(ProofArm::Off, a + e, low_a > 0 ? low_a + e : 0));
        v.push_back(make_run(ProofArm::On, b - e, low_b > 0 ? low_b - e : 0));
    }
    return v;
}

// Sahte yurutucu: oturumu sonuna kadar surer. fps(arm, index) kosu degerini uretir.
struct Driver {
    ProofSession s;
    std::size_t set_arm = 0, measure = 0, restore = 0;
    std::vector<ProofActionKind> log;
    Driver() : s() { s.begin(); s.consent(true); }
    template <typename FpsFn>
    void run(FpsFn fps, int cancel_after_runs = -1, bool invalid_at = false, int invalid_run = -1) {
        int done = 0;
        while (auto a = s.next()) {
            log.push_back(a->kind);
            if (a->kind == ProofActionKind::SetArm) {
                ++set_arm;
                s.on_arm_set(ProofArmOutcome::Applied);
            } else if (a->kind == ProofActionKind::MeasureRun) {
                ++measure;
                ProofRun r = make_run(a->arm, fps(a->arm, a->run_index));
                if (invalid_at && static_cast<int>(a->run_index) == invalid_run) r.valid = false, r.invalid_reason = "unstable";
                s.on_run(r);
                if (++done == cancel_after_runs) s.cancel();
            } else {
                ++restore;
                s.on_restore(ProofArmOutcome::Applied);
            }
        }
    }
};
double wobble(ProofArm arm, std::size_t i, double a, double b) {
    const double e = (static_cast<double>(i % 3) - 1.0) * 0.4;
    return (arm == ProofArm::Off ? a : b) + e;
}
}  // namespace

EF_TEST(proof_identical_arms_no_difference) {
    const auto r = evaluate_proof(arms(100, 100, 1.0, 5));
    EF_CHECK(r.verdict == ProofVerdict::NoDifference);
    EF_CHECK(has_verdict(r.verdict));
    EF_CHECK(r.valid_off == 5 && r.valid_on == 5);
    EF_CHECK(r.avg_fps.ci_low && r.avg_fps.ci_high && *r.avg_fps.ci_low <= 0 && *r.avg_fps.ci_high >= 0);
}

EF_TEST(proof_clear_difference_has_direction) {
    auto r = evaluate_proof(arms(100, 112, 0.5, 5, 80, 90));
    EF_CHECK(r.verdict == ProofVerdict::Better);
    EF_CHECK(r.avg_fps.diff && *r.avg_fps.diff > 11 && *r.avg_fps.ci_low > 0);
    EF_CHECK(r.avg_fps.sd_off && r.avg_fps.sd_on);  // kosudan kosuya yayilim raporlanir
    r = evaluate_proof(arms(112, 100, 0.5, 5, 90, 80));
    EF_CHECK(r.verdict == ProofVerdict::Worse && *r.avg_fps.diff < 0);
}

EF_TEST(proof_noise_floor_comes_from_measured_spread) {
    // Ayni kucuk ortalama fark: dar yayilimda anlamli, genis yayilimda "fark yok". Sabit esik yok.
    EF_CHECK(evaluate_proof(arms(100, 101, 0.05, 6)).verdict == ProofVerdict::Better);
    EF_CHECK(evaluate_proof(arms(100, 101, 3.0, 6)).verdict == ProofVerdict::NoDifference);
}

EF_TEST(proof_mixed_when_low1_opposes_average) {
    auto v = arms(100, 112, 0.5, 5, 90, 70);  // ortalama yukari, %1 dusuk asagi
    const auto r = evaluate_proof(v);
    EF_CHECK(r.verdict == ProofVerdict::Mixed);
    EF_CHECK(has_verdict(r.verdict));
}

EF_TEST(proof_low1_difference_without_average_difference_is_note_only) {
    const auto r = evaluate_proof(arms(100, 100, 1.0, 5, 90, 70));
    EF_CHECK(r.verdict == ProofVerdict::NoDifference && r.low1_differs);
}

EF_TEST(proof_mismatched_conditions_no_verdict) {
    auto v = arms(100, 120, 0.5, 5);
    v[7].conditions["power"] = "battery";  // kosu ortasinda guc kaynagi degisti
    v[4].conditions["driver"] = "2.0";
    const auto r = evaluate_proof(v);
    EF_CHECK(r.verdict == ProofVerdict::ConditionsDiffer && !has_verdict(r.verdict));
    EF_CHECK(r.reason == "driver,power");  // farkli alanlar adlandirilir
}

EF_TEST(proof_invalid_run_no_verdict) {
    auto v = arms(100, 120, 0.5, 5);
    v[6].valid = false;
    v[6].invalid_reason = "window_minimized";
    const auto r = evaluate_proof(v);
    EF_CHECK(r.verdict == ProofVerdict::InvalidRun && !has_verdict(r.verdict));
    EF_CHECK(r.reason == "window_minimized" && r.bad_run == 7);
}

EF_TEST(proof_fewer_than_five_valid_runs_is_insufficient) {
    const auto r = evaluate_proof(arms(100, 150, 0.5, 4));  // net fark olsa bile hukum yok
    EF_CHECK(r.verdict == ProofVerdict::InsufficientData && !has_verdict(r.verdict));
    EF_CHECK(r.valid_off == 4 && r.valid_on == 4);
    EF_CHECK(evaluate_proof({}).verdict == ProofVerdict::InsufficientData);
}

EF_TEST(proof_best_single_run_is_not_cherry_picked) {
    auto v = arms(100, 100, 0.5, 5);
    v[3].avg_fps = 160;  // Acik kolunda tek aykiri iyi kosu: hukum "Better" olmaz
    const auto r = evaluate_proof(v);
    EF_CHECK(r.verdict == ProofVerdict::NoDifference);
}

EF_TEST(proof_preconditions_refuse_in_order) {
    ProofPreconditions p;
    p.perf_off = true;
    p.plan_ready = true;
    p.plan_empty = false;
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::PlanBackgroundOnly);  // alt kume bos, yonetici/yeniden baslatma de yok
    p.scope_groups = 3;  // olculecek alt kume var
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::None);
    p.excluded_admin = true;  // karisik plan: yonetici gruplari alt kumeden cikar, deney yine yapilir (UAC acilmaz)
    p.excluded_reboot = true;
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::None);
    p.scope_groups = 0;  // alt kume bos: neden acik
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::PlanNeedsAdmin);
    p.excluded_admin = false;
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::PlanNeedsReboot);
    p.excluded_reboot = false;
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::PlanBackgroundOnly);
    p.scope_groups = 2;
    p.perf_off = false;
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::PerfNotOff);
    p.other_test_busy = true;
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::OtherTestBusy);
    p.game_blocked = true;  // oyun calisiyor: her seyden once reddedilir
    EF_CHECK(check_proof_preconditions(p) == ProofRefusal::GameRunning);
    ProofPreconditions q;
    q.perf_off = true;
    q.plan_ready = true;
    q.plan_empty = false;
    q.scope_groups = 1;
    q.hardware_adapter = false;
    EF_CHECK(check_proof_preconditions(q) == ProofRefusal::NoHardwareAdapter);
    q.plan_empty = true;
    EF_CHECK(check_proof_preconditions(q) == ProofRefusal::PlanEmpty);
}

EF_TEST(proof_duration_estimate_from_run_length) {
    const auto z = proof_plan_size();
    EF_CHECK(z.runs_total == 11 && z.runs_scored == 10);
    EF_CHECK(z.toggles == 6);  // 5 Acik gecisi + bitis restore'u (ABBA son kosusu Acik)
    const double q = estimate_proof_seconds(5.25), f = estimate_proof_seconds(10.25);  // Hizli / Tam kosu suresi
    EF_CHECK(std::fabs(q - (11 * (5.25 + kProofRunOverheadS) + 6 * kProofToggleBudgetS)) < 1e-9);
    EF_CHECK(f > q);
    // Gercek tur kalibrasyonu (0.8.3): Hizli sahne icin toplam ~85 sn (67-87 sn araliginda), dugme 2 dk gostermeli (3 dk degil).
    EF_CHECK(q >= 67.0 && q <= 100.0);
    EF_CHECK(estimate_proof_minutes(5.25) == 2);
    EF_CHECK(estimate_proof_minutes(5.25) == static_cast<int>(std::ceil(q / 60.0)));
    EF_CHECK(estimate_proof_minutes(0.0, 1, 0) >= 1);
}

EF_TEST(proof_session_happy_path_ends_off) {
    Driver d;
    d.run([](ProofArm a, std::size_t i) { return wobble(a, i, 100, 112); });
    EF_CHECK(d.s.phase() == ProofPhase::Done && d.s.terminal());
    EF_CHECK(d.measure == 11 && d.s.runs().size() == 10);
    EF_CHECK(d.set_arm == 5 && d.restore == 1);  // son kosu Acik: bitiste Kapali'ya donus
    EF_CHECK(d.s.arm() == ProofArm::Off);
    EF_CHECK(d.s.result() && d.s.result()->verdict == ProofVerdict::Better);
    // Sira: isinma Kapali, sonra A B B A A B B A A B (A = Kapali)
    const auto& r = d.s.runs();
    const ProofArm expect[10] = {ProofArm::Off, ProofArm::On,  ProofArm::On,  ProofArm::Off, ProofArm::Off,
                                 ProofArm::On,  ProofArm::On,  ProofArm::Off, ProofArm::Off, ProofArm::On};
    for (int i = 0; i < 10; ++i) EF_CHECK(r[static_cast<std::size_t>(i)].arm == expect[i]);
}

EF_TEST(proof_session_no_without_yes_touches_nothing) {
    ProofSession s;
    EF_CHECK(!s.next());  // onay oncesi eylem yok
    s.begin();
    EF_CHECK(s.phase() == ProofPhase::AwaitConsent && !s.next());
    s.consent(false);
    EF_CHECK(s.phase() == ProofPhase::Idle && !s.next() && !s.result());
}

EF_TEST(proof_session_cancel_while_on_requests_restore) {
    Driver d;
    d.run([](ProofArm a, std::size_t i) { return wobble(a, i, 100, 112); }, /*cancel_after_runs=*/3);  // isinma + A + B bitti, kol Acik
    EF_CHECK(d.s.terminal() && d.s.phase() == ProofPhase::Aborted);
    EF_CHECK(d.restore == 1 && d.s.arm() == ProofArm::Off);  // iptalde Acik kalmaz
    EF_CHECK(d.s.result() && d.s.result()->verdict == ProofVerdict::Aborted && !has_verdict(d.s.result()->verdict));
    EF_CHECK(d.s.result()->reason == "cancelled");
    EF_CHECK(d.measure == 3);  // kalan kosular atildi
}

EF_TEST(proof_session_cancel_request_is_seen_before_final_state) {
    // Iptal geldiginde kol Acik ise bekleyen eylem RestoreOriginal'dir (UI bunu bekler).
    ProofSession s;
    s.begin();
    s.consent(true);
    s.on_run(make_run(ProofArm::Off, 100));  // isinma
    EF_CHECK(s.next() && s.next()->kind == ProofActionKind::MeasureRun);  // A (Kapali)
    s.on_run(make_run(ProofArm::Off, 100));
    EF_CHECK(s.next() && s.next()->kind == ProofActionKind::SetArm && s.next()->arm == ProofArm::On);
    s.on_arm_set(ProofArmOutcome::Applied);
    s.cancel();
    EF_CHECK(s.phase() == ProofPhase::Restoring);
    EF_CHECK(s.next() && s.next()->kind == ProofActionKind::RestoreOriginal);
}

EF_TEST(proof_session_cancel_while_off_finishes_without_restore) {
    Driver d;
    d.run([](ProofArm a, std::size_t i) { return wobble(a, i, 100, 112); }, 2);  // isinma + A: kol hala Kapali
    EF_CHECK(d.s.terminal() && d.restore == 0 && d.s.arm() == ProofArm::Off);
    EF_CHECK(d.s.result()->verdict == ProofVerdict::Aborted);
}

EF_TEST(proof_session_invalid_run_stops_restores_no_verdict) {
    Driver d;
    d.run([](ProofArm a, std::size_t i) { return wobble(a, i, 100, 112); }, -1, true, 2);  // 2. kosu (Acik) gecersiz
    EF_CHECK(d.s.terminal() && d.s.phase() == ProofPhase::Aborted);
    EF_CHECK(d.restore == 1 && d.s.arm() == ProofArm::Off);
    const auto& res = *d.s.result();
    EF_CHECK(res.verdict == ProofVerdict::InvalidRun && res.reason == "unstable" && res.bad_run == 2);
    EF_CHECK(d.measure == 3);
}

EF_TEST(proof_session_arm_switch_failure_aborts_and_unverified_is_restore_failed) {
    ProofSession s;
    s.begin();
    s.consent(true);
    s.on_run(make_run(ProofArm::Off, 100));
    s.on_run(make_run(ProofArm::Off, 100));
    s.on_arm_set(ProofArmOutcome::NotApplied);  // controller uygulamadi / geri aldi: kol Kapali
    EF_CHECK(s.terminal() && s.arm() == ProofArm::Off);
    EF_CHECK(s.result()->verdict == ProofVerdict::Aborted && s.result()->reason == "arm_switch_failed");

    ProofSession u;
    u.begin();
    u.consent(true);
    u.on_run(make_run(ProofArm::Off, 100));
    u.on_run(make_run(ProofArm::Off, 100));
    u.on_arm_set(ProofArmOutcome::Unverified);  // geri alma dogrulanamadi: kurtarma
    EF_CHECK(u.terminal() && u.result()->verdict == ProofVerdict::RestoreFailed);
}

EF_TEST(proof_session_failed_final_restore_is_reported_not_hidden) {
    ProofSession s;
    s.begin();
    s.consent(true);
    s.on_run(make_run(ProofArm::Off, 100));
    s.on_run(make_run(ProofArm::Off, 100));
    s.on_arm_set(ProofArmOutcome::Applied);  // Acik
    s.cancel();
    EF_CHECK(s.phase() == ProofPhase::Restoring);
    s.on_restore(ProofArmOutcome::Unverified);
    EF_CHECK(s.terminal() && s.arm() == ProofArm::On);  // durum Acik kaldi; Kapali denmez
    EF_CHECK(s.result()->verdict == ProofVerdict::RestoreFailed);
}

EF_TEST(proof_session_progress_counts_scored_runs_only) {
    ProofSession s;
    s.begin();
    s.consent(true);
    EF_CHECK(s.progress().in_warmup && s.progress().scored_total == 10 && s.progress().scored_done == 0);
    s.on_run(make_run(ProofArm::Off, 100));
    EF_CHECK(!s.progress().in_warmup && s.progress().scored_done == 0 && s.progress().run_index == 1);
    s.on_run(make_run(ProofArm::Off, 100));
    EF_CHECK(s.progress().scored_done == 1);
}

EF_TEST(proof_session_abort_records_reason_and_restores) {
    ProofSession s;
    s.begin();
    s.consent(true);
    s.on_run(make_run(ProofArm::Off, 100));
    s.on_run(make_run(ProofArm::Off, 100));
    s.on_arm_set(ProofArmOutcome::Applied);  // Acik
    s.abort("plan_changed");                 // plan parmak izi degisti
    EF_CHECK(s.phase() == ProofPhase::Restoring && s.next()->kind == ProofActionKind::RestoreOriginal);
    s.on_restore(ProofArmOutcome::Applied);
    EF_CHECK(s.terminal() && s.arm() == ProofArm::Off);
    EF_CHECK(s.result()->verdict == ProofVerdict::Aborted && s.result()->reason == "plan_changed");
    s.abort("late");  // terminalden sonra etkisiz
    EF_CHECK(s.result()->reason == "plan_changed");
}

// P4: "Fark yok" kaydi grup bazinda; hukumsuz sonuc kaydi degistirmez, anlamli fark isareti kaldirir.
EF_TEST(proof_no_effect_groups_follow_verdict) {
    using V = std::vector<std::string>;
    const V scope{"plan.power_mode", "plan.vfx"};
    auto cur = update_no_effect_groups({}, scope, ProofVerdict::NoDifference);
    EF_CHECK(cur == scope);
    cur = update_no_effect_groups(cur, scope, ProofVerdict::NoDifference);
    EF_CHECK(cur == scope);  // tekrar yok
    for (auto v : {ProofVerdict::InsufficientData, ProofVerdict::ConditionsDiffer, ProofVerdict::InvalidRun, ProofVerdict::Aborted,
                   ProofVerdict::RestoreFailed})
        EF_CHECK(update_no_effect_groups(cur, {"plan.power_mode"}, v) == scope);  // hukum yok: degismez
    for (auto v : {ProofVerdict::Better, ProofVerdict::Worse, ProofVerdict::Mixed})
        EF_CHECK(update_no_effect_groups(cur, {"plan.power_mode"}, v) == V{"plan.vfx"});  // olculen fark: isaret kalkar
    EF_CHECK(update_no_effect_groups({"plan.background"}, scope, ProofVerdict::NoDifference) == (V{"plan.background", "plan.power_mode", "plan.vfx"}));
    EF_CHECK(update_no_effect_groups({}, {"", std::string(200, 'x')}, ProofVerdict::NoDifference).empty());  // bozuk anahtar yazilmaz
    V many;
    for (int i = 0; i < 70; ++i) many.push_back("g" + std::to_string(i));
    EF_CHECK(update_no_effect_groups({}, many, ProofVerdict::NoDifference).size() == kMaxNoEffectGroups);
}