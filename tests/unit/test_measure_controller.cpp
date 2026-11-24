// P9/B5: MeasureController karakterizasyon testleri. App'ten ayrilan olcum denetleyicisinin MEVCUT davranisini sabitler
// (refactor davranisi degistirmez): baslatma kapilari, test durumu degisiklikleri, iptal bayraklari, canli test ekrani
// eslemesi, deney (ABBA) on kosullari ve saf kosu->deney donusumu. Gercek olcum kosusu / UAC / ag YOK: tum testler kapi
// reddi, onay reddi ya da saf islev yolunu kullanir (is parcacigi hic baslamaz). Sahte host = MOCK (gercek cihaz sayilmaz).
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/measure_controller.h"
#include "app/measure_support.h"
#include "core/perf/proof_experiment.h"
#include "ef_test.h"

namespace {

using ef::app::MeasureController;
using ef::app::MeasureGameTarget;
using ef::app::MeasureHost;
using ef::app::ProofConfirmInfo;
namespace cm = ef::core::measure;
namespace cg = ef::core::games;
namespace bb = ef::benchmark::balance;

// Sahte host (MOCK): her cagriyi sayar; kapilar ayarlanabilir. post() isi hemen calistirmaz, kuyruga koyar.
class FakeHost : public MeasureHost {
public:
    // ayarlar
    bool uninstall = false, admin_is_busy = false, holds_writes = false;
    bool verify_result = true, gate_or_block_result = true, consent = true, proof_consent = true;
    bool live_visible = false, win_visible = false;
    std::optional<MeasureGameTarget> game;
    // sayaclar / kayitlar
    int verify_calls = 0, gate_or_block_calls = 0, push_gate_calls = 0, consent_calls = 0, proof_confirm_calls = 0;
    int begin_marker_calls = 0, end_marker_calls = 0, measure_publishes = 0, speed_publishes = 0, history_saves = 0;
    std::vector<std::function<void()>> queued;
    std::vector<ef::core::measure::LiveSample> live_samples;
    ef::core::insights::History hist;
    ef::core::settings::Preferences pref;
    std::string os_build = "10.0.26100", commit = "abc123";

    void post(std::function<void()> fn) override { queued.push_back(std::move(fn)); }
    HWND host_hwnd() const override { return nullptr; }
    HWND window_hwnd() const override { return nullptr; }
    bool window_visible() const override { return win_visible; }
    bool live_view_visible() const override { return live_visible; }
    void publish_measure(const cm::MeasureState&) override { ++measure_publishes; }
    void publish_speed(const ef::ui::ViewModel::SpeedView&) override { ++speed_publishes; }
    void publish_live_sample(const cm::LiveSample& s) override { live_samples.push_back(s); }
    bool ask_speed_consent() override { ++consent_calls; return consent; }
    std::optional<std::string> address;  // R-C4: ask_server_address donusu (nullopt = Hayir)
    int address_calls = 0;
    std::string last_initial;
    std::optional<std::string> ask_server_address(const std::string& initial) override {
        ++address_calls;
        last_initial = initial;
        return address;
    }
    bool ask_proof_confirm(const ProofConfirmInfo&) override { ++proof_confirm_calls; return proof_consent; }

    bool uninstall_running() const override { return uninstall; }
    bool verify_or_block(cg::GuardedAction) override { ++verify_calls; return verify_result; }
    bool gate_or_block(cg::GuardedAction) override { ++gate_or_block_calls; return gate_or_block_result; }
    cg::GateDecision gate(cg::GuardedAction) const override { return {true, {}}; }
    void push_gate() override { ++push_gate_calls; }
    bool admin_busy() const override { return admin_is_busy; }
    bool update_holds_writes() const override { return holds_writes; }
    void refresh_admin_path() override {}
    void set_admin_service_only(bool) override {}
    ef::core::perf::PerfController* perf() override { return nullptr; }
    ef::core::settings::Preferences& prefs() override { return pref; }
    bool save_prefs() override { return true; }
    void start_findings_refresh() override {}
    void check_pending_exit() override {}

    void begin_run_marker(const char*) override { ++begin_marker_calls; }
    void end_run_marker() override { ++end_marker_calls; }
    ef::core::insights::History& history() override { return hist; }
    void save_history() override { ++history_saves; }
    void sync_bench_evidence() override {}
    void refresh_history_compare() override {}

    std::optional<MeasureGameTarget> active_game() const override { return game; }
    std::string game_build(const std::string&) const override { return {}; }
    void note_game_renderer(const std::string&, cg::GuideRenderer) override {}
    const std::string& capture_os_build() const override { return os_build; }
    const std::string& capture_collector_commit() const override { return commit; }
};

struct Rig {
    FakeHost host;
    ef::ui::ViewModel vm;
    MeasureController mc{host, vm};
};

bool idle_untouched(const Rig& r) {
    return r.vm.measure.phase == cm::TestPhase::Idle && !r.vm.measure.endurance.running && !r.vm.speed.running &&
           r.host.begin_marker_calls == 0 && !r.host.queued.size();
}

}  // namespace

// ---- Uygulama Testi (Genel) baslatma kapilari ----
EF_TEST(measure_builtin_refused_while_data_removal_runs) {
    Rig r;
    r.host.uninstall = true;
    r.mc.start_builtin_test();
    EF_CHECK(r.host.verify_calls == 0);  // R20: kapi bile sorulmaz
    EF_CHECK(idle_untouched(r));
    EF_CHECK(!r.mc.bench_thread().joinable());
}

EF_TEST(measure_builtin_refused_while_another_test_is_active) {
    for (const auto ph : {cm::TestPhase::Preparing, cm::TestPhase::Running, cm::TestPhase::Stopping}) {
        Rig r;
        r.vm.measure.phase = ph;
        r.mc.start_builtin_test();
        EF_CHECK(r.host.verify_calls == 0);
        EF_CHECK(r.vm.measure.phase == ph);
        EF_CHECK(!r.mc.bench_thread().joinable());
    }
    Rig e;
    e.vm.measure.endurance.running = true;  // uzun test ile ayni anda ikinci test yok
    e.mc.start_builtin_test();
    EF_CHECK(e.host.verify_calls == 0 && e.vm.measure.phase == cm::TestPhase::Idle);
    Rig s;
    s.vm.speed.running = true;  // hiz testi suruyor
    s.mc.start_builtin_test();
    EF_CHECK(s.host.verify_calls == 0 && s.vm.measure.phase == cm::TestPhase::Idle);
}

EF_TEST(measure_builtin_refused_when_game_gate_blocks) {
    Rig r;
    r.host.verify_result = false;  // oyun calisiyor / algilama guvenilmez
    r.mc.start_builtin_test();
    EF_CHECK(r.host.verify_calls == 1);
    EF_CHECK(idle_untouched(r));  // kosu isareti yazilmaz, durum degismez, is parcacigi yok
    EF_CHECK(!r.mc.bench_thread().joinable());
}

// ---- Denge (Darbogaz) kapsami: ayni kapilar ----
EF_TEST(measure_balance_gates_match_builtin_gates) {
    Rig u;
    u.host.uninstall = true;
    u.mc.start_balance_test();
    EF_CHECK(u.host.verify_calls == 0 && idle_untouched(u));
    Rig busy;
    busy.vm.measure.phase = cm::TestPhase::Running;
    busy.mc.start_balance_test();
    EF_CHECK(busy.host.verify_calls == 0 && busy.vm.measure.phase == cm::TestPhase::Running);
    Rig g;
    g.host.verify_result = false;
    g.mc.start_balance_test();
    EF_CHECK(g.host.verify_calls == 1 && idle_untouched(g));
    EF_CHECK(!g.mc.bench_thread().joinable());
}

// ---- Oyunda Test ----
EF_TEST(measure_ingame_refusals) {
    Rig busy;
    busy.vm.measure.phase = cm::TestPhase::Preparing;
    busy.mc.start_ingame_test();
    EF_CHECK(busy.host.gate_or_block_calls == 0 && busy.vm.measure.phase == cm::TestPhase::Preparing);

    Rig e;
    e.vm.measure.endurance.running = true;
    e.mc.start_ingame_test();
    EF_CHECK(e.host.gate_or_block_calls == 0);

    Rig blocked;
    blocked.host.gate_or_block_result = false;  // kapi reddetti: neden App'te gosterilir
    blocked.mc.start_ingame_test();
    EF_CHECK(blocked.host.gate_or_block_calls == 1 && blocked.host.push_gate_calls == 0 && idle_untouched(blocked));

    Rig none;  // kapi acik ama izleyici oyun gormuyor: neden satiri yenilenir, test baslamaz
    none.mc.start_ingame_test();
    EF_CHECK(none.host.gate_or_block_calls == 1 && none.host.push_gate_calls == 1 && idle_untouched(none));
    EF_CHECK(!none.mc.bench_thread().joinable());
}

// ---- Uzun test ----
EF_TEST(measure_endurance_refusals_and_stop) {
    Rig u;
    u.host.uninstall = true;
    u.vm.measure.builtin_available = true;
    u.mc.start_endurance_test();
    EF_CHECK(!u.vm.measure.endurance.running && u.host.verify_calls == 0);

    Rig off;  // Uygulama Testi bagli degil
    off.mc.start_endurance_test();
    EF_CHECK(!off.vm.measure.endurance.running && off.host.verify_calls == 0);

    Rig busy;
    busy.vm.measure.builtin_available = true;
    busy.vm.measure.phase = cm::TestPhase::Running;
    busy.mc.start_endurance_test();
    EF_CHECK(!busy.vm.measure.endurance.running && busy.host.verify_calls == 0);

    Rig gate;
    gate.vm.measure.builtin_available = true;
    gate.host.verify_result = false;
    gate.mc.start_endurance_test();
    EF_CHECK(gate.host.verify_calls == 1 && !gate.vm.measure.endurance.running && gate.host.begin_marker_calls == 0);
    EF_CHECK(!gate.mc.endurance_thread().joinable());

    // durdurma: iptal bayragi yalniz kosu suruyorsa kalkar
    Rig st;
    st.mc.stop_endurance_test();
    EF_CHECK(!st.mc.endurance_cancel_requested());
    st.vm.measure.endurance.running = true;
    st.mc.stop_endurance_test();
    EF_CHECK(st.mc.endurance_cancel_requested());
}

// ---- Internet hiz testi ----
EF_TEST(measure_speed_refusals_consent_and_stop) {
    Rig running;
    running.vm.speed.running = true;
    running.mc.start_speed_test();
    EF_CHECK(running.host.verify_calls == 0 && running.host.consent_calls == 0);

    Rig busy;
    busy.vm.measure.phase = cm::TestPhase::Running;
    busy.mc.start_speed_test();
    EF_CHECK(busy.host.verify_calls == 0 && !busy.vm.speed.running);

    Rig u;
    u.host.uninstall = true;
    u.mc.start_speed_test();
    EF_CHECK(u.host.verify_calls == 0 && !u.vm.speed.running);

    Rig gate;  // oyun acik: onay bile sorulmaz
    gate.host.verify_result = false;
    gate.mc.start_speed_test();
    EF_CHECK(gate.host.verify_calls == 1 && gate.host.consent_calls == 0 && !gate.vm.speed.running);

    Rig no;  // Hayir: ag baglantisi kurulmaz, durum degismez
    no.host.consent = false;
    no.mc.start_speed_test();
    EF_CHECK(no.host.verify_calls == 1 && no.host.consent_calls == 1);
    EF_CHECK(!no.vm.speed.running && no.host.speed_publishes == 0 && !no.mc.speed_thread().joinable());

    // Evet sonrasi kapi yeniden dogrulanir (onay penceresi acikken oyun baslamis olabilir)
    int calls = 0;
    struct SecondGate : FakeHost {
        int* n = nullptr;
        bool verify_or_block(cg::GuardedAction) override { return ++*n < 2; }
    };
    ef::ui::ViewModel vm;
    SecondGate host;
    host.n = &calls;
    MeasureController mc(host, vm);
    mc.start_speed_test();
    EF_CHECK(calls == 2 && host.consent_calls == 1);
    EF_CHECK(!vm.speed.running && !mc.speed_thread().joinable());

    // durdurma yalniz calisirken iptal ister
    Rig st;
    st.mc.stop_speed_test();
    EF_CHECK(!st.mc.speed_cancel_requested());
    st.vm.speed.running = true;
    st.mc.stop_speed_test();
    EF_CHECK(st.mc.speed_cancel_requested());
}

// R-C4: oyun sunucusu ping. Gercek ICMP paketi GONDERILMEZ: yalniz reddedilen / Hayir yollari sinanir.
EF_TEST(measure_server_ping_refusals_and_no) {
    Rig running;  // hiz testi ya da baska sunucu ping'i suruyor: hicbir sey sorulmaz
    running.vm.speed.running = true;
    running.mc.start_server_ping();
    EF_CHECK(running.host.verify_calls == 0 && running.host.address_calls == 0);
    Rig srv;
    srv.vm.speed.server_running = true;
    srv.mc.start_server_ping();
    EF_CHECK(srv.host.verify_calls == 0 && srv.host.address_calls == 0);

    Rig busy;  // olcum suruyor
    busy.vm.measure.phase = cm::TestPhase::Running;
    busy.mc.start_server_ping();
    EF_CHECK(busy.host.verify_calls == 0 && busy.host.address_calls == 0);

    Rig u;
    u.host.uninstall = true;
    u.mc.start_server_ping();
    EF_CHECK(u.host.verify_calls == 0 && u.host.address_calls == 0);

    Rig gate;  // oyun acik: adres bile sorulmaz
    gate.host.verify_result = false;
    gate.mc.start_server_ping();
    EF_CHECK(gate.host.verify_calls == 1 && gate.host.address_calls == 0 && !gate.vm.speed.server_running);

    Rig no;  // Hayir: paket gonderilmez, kayitli adres ve durum degismez, hicbir sey yayinlanmaz
    no.vm.speed.server_ip = "203.0.113.10";
    no.host.pref.game_server_ipv4 = "203.0.113.10";
    no.mc.start_server_ping();
    EF_CHECK(no.host.address_calls == 1 && no.host.last_initial == "203.0.113.10");  // kayitli adres pencerede hazir gelir
    EF_CHECK(!no.vm.speed.server_running && no.vm.speed.server_state == ef::core::net::ServerPingState::None);
    EF_CHECK(no.vm.speed.server_ip == "203.0.113.10" && no.host.pref.game_server_ipv4 == "203.0.113.10");
    EF_CHECK(no.host.speed_publishes == 0 && !no.mc.speed_thread().joinable());

    // Evet sonrasi kapi yeniden dogrulanir: pencere acikken oyun baslamis olabilir -> paket yok, adres saklanmaz
    int calls = 0;
    struct SecondGate : FakeHost {
        int* n = nullptr;
        bool verify_or_block(cg::GuardedAction) override { return ++*n < 2; }
    };
    ef::ui::ViewModel vm;
    SecondGate host;
    host.n = &calls;
    host.address = "203.0.113.10";
    MeasureController mc(host, vm);
    mc.start_server_ping();
    EF_CHECK(calls == 2 && host.address_calls == 1);
    EF_CHECK(!vm.speed.server_running && vm.speed.server_ip.empty() && host.pref.game_server_ipv4.empty() && !mc.speed_thread().joinable());
}

EF_TEST(measure_cancel_flags_are_independent_atomics) {
    Rig r;
    EF_CHECK(!r.mc.bench_cancel_requested() && !r.mc.endurance_cancel_requested() && !r.mc.speed_cancel_requested());
    r.mc.request_bench_cancel();
    EF_CHECK(r.mc.bench_cancel_requested() && !r.mc.endurance_cancel_requested() && !r.mc.speed_cancel_requested());
    Rig all;
    all.mc.request_shutdown_cancel();  // kapanis: uc iptal bayragi birlikte
    EF_CHECK(all.mc.bench_cancel_requested() && all.mc.endurance_cancel_requested() && all.mc.speed_cancel_requested());
}

// ---- Canli test ekrani ----
EF_TEST(measure_live_view_push_maps_frame_tap_to_sample) {
    Rig r;
    r.mc.live_view_push();  // canli ekran kapali: hicbir sey iletilmez
    EF_CHECK(r.host.live_samples.empty());

    r.vm.measure.live_view.active = true;
    r.host.live_visible = true;
    r.mc.frame_tap().begin_step(2, 9, 3, 1920, 1080);
    r.mc.frame_tap().set_phase(ef::benchmark::TapPhase::Measure);
    r.mc.live_view_push();
    EF_CHECK(r.host.live_samples.size() == 1);
    const auto& s = r.host.live_samples.back();
    EF_CHECK(s.phase == cm::LivePhase::Measure);
    EF_CHECK(s.step_index == 2 && s.step_total == 9 && s.step_code == 3);
    EF_CHECK(s.width == 1920 && s.height == 1080);
    EF_CHECK(!s.has_numbers);  // kova kapanmadi: sayilar olculmedi

    r.mc.frame_tap().set_phase(ef::benchmark::TapPhase::Warmup);
    r.mc.live_view_push();
    EF_CHECK(r.host.live_samples.back().phase == cm::LivePhase::Warmup);

    r.mc.stop_live_view();  // kosu bitti: canli ekran kapanir
    EF_CHECK(!r.vm.measure.live_view.active);
    const auto n = r.host.live_samples.size();
    r.mc.live_view_push();
    EF_CHECK(r.host.live_samples.size() == n);
}

// ---- Gecmis kosul kayitlari ----
EF_TEST(measure_run_conditions_only_report_read_values) {
    Rig r;
    EF_CHECK(r.mc.run_conditions().empty());  // hicbir alan okunmadi: hicbir sey uydurulmaz
    r.vm.os = "Windows 11";
    r.vm.gpu = std::string(300, 'g');
    r.vm.gpu_driver = "";
    r.vm.build_id = "b1";
    const auto c = r.mc.run_conditions();
    EF_CHECK(c.at("os") == "Windows 11");
    EF_CHECK(c.at("gpu").size() == 160);  // history.cpp siniri
    EF_CHECK(c.at("build") == "b1");
    EF_CHECK(c.count("gpu_driver") == 0 && c.count("cpu") == 0);
    EF_CHECK(c.count("power") == 0);  // Performans anahtari yok: durum yazilmaz
}

EF_TEST(measure_history_cap_keeps_three_newest) {
    std::vector<cm::ResultView> h(5);
    for (std::size_t i = 0; i < h.size(); ++i) h[i].when_unix_ms = static_cast<std::int64_t>(100 - i);
    ef::app::detail::cap_measure_history(h);
    EF_CHECK(h.size() == ef::core::insights::kMaxEntries);
    EF_CHECK(h.front().when_unix_ms == 100 && h.back().when_unix_ms == 98);
}

// ---- ABBA deneyi: sure tahmini ve on kosullar ----
EF_TEST(measure_proof_estimate_follows_selected_mode) {
    Rig r;
    r.vm.measure.mode = cm::TestMode::Quick;
    r.mc.proof_refresh_estimate();
    const int quick = r.vm.measure.proof.estimate_min;
    EF_CHECK(quick == ef::core::perf::estimate_proof_minutes(bb::planned_step_seconds(bb::kQuickTiming)));
    r.vm.measure.mode = cm::TestMode::Full;
    r.mc.proof_refresh_estimate();
    EF_CHECK(r.vm.measure.proof.estimate_min == ef::core::perf::estimate_proof_minutes(bb::planned_step_seconds(bb::kFullTiming)));
    EF_CHECK(r.vm.measure.proof.estimate_min >= quick);
}

EF_TEST(measure_proof_precheck_reports_first_failing_reason) {
    using ef::core::perf::ProofRefusal;
    Rig idle;  // Performans anahtari yok: ilk tutmayan on kosul "Kapali degil"
    idle.mc.proof_refresh_precheck();
    EF_CHECK(idle.vm.measure.proof.precheck == ProofRefusal::PerfNotOff);
    EF_CHECK(idle.host.measure_publishes == 1);
    idle.mc.proof_refresh_precheck();  // degismedi: yeniden yayinlanmaz
    EF_CHECK(idle.host.measure_publishes == 1);

    Rig u;
    u.host.uninstall = true;
    u.mc.proof_refresh_precheck();
    EF_CHECK(u.vm.measure.proof.precheck == ProofRefusal::OtherTestBusy);
    Rig sp;
    sp.vm.speed.running = true;
    sp.mc.proof_refresh_precheck();
    EF_CHECK(sp.vm.measure.proof.precheck == ProofRefusal::OtherTestBusy);
    Rig ad;
    ad.host.admin_is_busy = true;
    ad.mc.proof_refresh_precheck();
    EF_CHECK(ad.vm.measure.proof.precheck == ProofRefusal::OtherTestBusy);
    Rig up;
    up.host.holds_writes = true;
    up.mc.proof_refresh_precheck();
    EF_CHECK(up.vm.measure.proof.precheck == ProofRefusal::OtherTestBusy);
    // on kontrol oyun kapisini CANLI sormaz (verify_or_block yeniden algilama tetikleyebilir)
    EF_CHECK(idle.host.verify_calls == 0);
}

EF_TEST(measure_proof_start_refused_without_perf_controller) {
    Rig r;  // controller yok: hicbir sey baslamaz, onay sorulmaz
    r.mc.start_proof_experiment();
    EF_CHECK(r.host.proof_confirm_calls == 0 && !r.mc.proof_running());
    EF_CHECK(r.vm.measure.phase == cm::TestPhase::Idle && r.host.begin_marker_calls == 0);
    r.mc.stop_proof_experiment();  // deney yokken durdurma no-op
    EF_CHECK(!r.mc.proof_running() && !r.mc.bench_cancel_requested());
}

// ---- ABBA kosusu -> deney girdisi (saf) ----
namespace {
bb::BalanceReport measured_report(std::size_t frames, double interval_ms) {
    bb::BalanceReport rep;
    rep.quick = true;
    rep.logical_cores = 8;
    rep.fingerprint = "arena_fp";
    auto& st = rep.steps[static_cast<std::size_t>(bb::StepId::K1)];
    st.plan.id = bb::StepId::K1;
    st.plan.config.width = 1920;
    st.plan.config.height = 1080;
    st.status = bb::StepStatus::Measured;
    st.stats.state = ef::core::diagnosis::StepState::Valid;
    st.scored_ms.assign(frames, interval_ms);
    return rep;
}
}  // namespace

EF_TEST(measure_proof_run_valid_path_carries_conditions_and_fps) {
    const auto rep = measured_report(200, 10.0);
    ef::app::BalanceEnvironment ac;
    const auto run = ef::app::proof_run_from_report(rep, ac, ac, false, "0.9.9", "drv-1");
    EF_CHECK(run.valid && run.invalid_reason.empty());
    EF_CHECK(run.avg_fps > 99.0 && run.avg_fps < 101.0);  // 10 ms kare = 100 FPS
    EF_CHECK(run.conditions.at("arena") == "arena_fp");
    EF_CHECK(run.conditions.at("resolution") == "1920x1080");
    EF_CHECK(run.conditions.at("power_source") == "ac");
    EF_CHECK(run.conditions.at("battery_saver") == "off");
    EF_CHECK(run.conditions.at("app") == "0.9.9" && run.conditions.at("driver") == "drv-1");
    EF_CHECK(run.conditions.at("mode") == "quick" && run.conditions.at("cores") == "8" && run.conditions.at("preview") == "off");
}

EF_TEST(measure_proof_run_invalid_reasons_are_ordered) {
    ef::app::BalanceEnvironment ac;
    ef::app::BalanceEnvironment battery;
    battery.on_battery = true;
    {  // cokme her seyden once
        const auto run = ef::app::proof_run_from_report(measured_report(200, 10.0), ac, ac, true, "v", "d");
        EF_CHECK(!run.valid && run.invalid_reason == "internal_error");
    }
    {  // kosucu hata kodu
        auto rep = measured_report(200, 10.0);
        rep.error = "device_removed";
        const auto run = ef::app::proof_run_from_report(rep, ac, ac, false, "v", "d");
        EF_CHECK(!run.valid && run.invalid_reason == "device_removed");
    }
    {  // adim olculmedi / zaman asimi
        auto rep = measured_report(200, 10.0);
        auto& st = rep.steps[static_cast<std::size_t>(bb::StepId::K1)];
        st.status = bb::StepStatus::NotRun;
        EF_CHECK(ef::app::proof_run_from_report(rep, ac, ac, false, "v", "d").invalid_reason == "step_not_measured");
        st.status = bb::StepStatus::TimedOut;
        EF_CHECK(ef::app::proof_run_from_report(rep, ac, ac, false, "v", "d").invalid_reason == "timeout");
    }
    {  // adim gecersiz: neden anahtari korunur, bossa "invalid"
        auto rep = measured_report(200, 10.0);
        auto& st = rep.steps[static_cast<std::size_t>(bb::StepId::K1)];
        st.stats.state = ef::core::diagnosis::StepState::Invalid;
        EF_CHECK(ef::app::proof_run_from_report(rep, ac, ac, false, "v", "d").invalid_reason == "invalid");
        st.stats.reason = "unstable";
        EF_CHECK(ef::app::proof_run_from_report(rep, ac, ac, false, "v", "d").invalid_reason == "unstable");
    }
    {  // kosu sirasinda fis cekildi
        const auto run = ef::app::proof_run_from_report(measured_report(200, 10.0), ac, battery, false, "v", "d");
        EF_CHECK(!run.valid && run.invalid_reason == "power_source_changed");
    }
    {  // kare yok
        const auto run = ef::app::proof_run_from_report(measured_report(0, 10.0), ac, ac, false, "v", "d");
        EF_CHECK(!run.valid && run.invalid_reason == "too_few_frames");
    }
    {  // pil durumu parmak izine kosu BASINDAKI okumadan girer
        const auto run = ef::app::proof_run_from_report(measured_report(200, 10.0), battery, battery, false, "v", "d");
        EF_CHECK(run.valid && run.conditions.at("power_source") == "battery");
    }
}

// ---- Katman yonu (kaynak tarama; salt okuma) ----
// ui katmani platform / benchmark / olcum / uygulama basliklarini icermez; core hicbir ust katmani icermez; olcum denetleyicisi
// App'i ve ana pencereyi gormez (yalniz MeasureHost arayuzu). Sistem basliklari (<windows.h>) bu kurala girmez.
namespace {
std::vector<std::string> project_includes(const std::filesystem::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos || line[i] != '#') continue;  // yorum satirlari ('//') da elenir
        i = line.find_first_not_of(" \t", i + 1);
        if (i == std::string::npos || line.compare(i, 7, "include") != 0) continue;
        const auto q1 = line.find('"', i + 7);
        if (q1 == std::string::npos) continue;
        const auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        out.push_back(line.substr(q1 + 1, q2 - q1 - 1));
    }
    return out;
}

// Dizin altindaki kaynak dosyalarda yasak oneklerle baslayan her "..." include'u listeler ("dosya -> include").
std::vector<std::string> violations(const std::string& rel_dir, const std::vector<std::string>& forbidden_prefixes) {
    std::vector<std::string> bad;
    const std::filesystem::path root = std::filesystem::path(EF_SOURCE_ROOT) / rel_dir;
    EF_CHECK(std::filesystem::exists(root));
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        const auto ext = e.path().extension().string();
        if (!e.is_regular_file() || (ext != ".h" && ext != ".cpp")) continue;
        for (const auto& inc : project_includes(e.path()))
            for (const auto& pre : forbidden_prefixes)
                if (inc.rfind(pre, 0) == 0) bad.push_back(e.path().filename().string() + " -> " + inc);
    }
    return bad;
}
}  // namespace

EF_TEST(layer_direction_ui_has_no_platform_benchmark_or_app_headers) {
    const auto bad = violations("src/ui", {"platform/", "benchmark/", "measurement/", "app/"});
    for (const auto& b : bad) std::fprintf(stderr, "  katman ihlali: %s\n", b.c_str());
    EF_CHECK(bad.empty());
}

EF_TEST(layer_direction_core_has_no_upper_layer_headers) {
    const auto bad = violations("src/core", {"platform/", "ui/", "app/", "benchmark/", "measurement/"});
    for (const auto& b : bad) std::fprintf(stderr, "  katman ihlali: %s\n", b.c_str());
    EF_CHECK(bad.empty());
}

EF_TEST(layer_direction_scan_catches_a_violation) {
    // Tarayicinin kendisi: gecici dosyada yasak include bulunur, yorum satiri ve sistem basligi sayilmaz.
    const auto dir = std::filesystem::temp_directory_path() / "ef_layer_scan_probe";
    std::filesystem::create_directories(dir);
    const auto f = dir / "probe.cpp";
    {
        std::ofstream o(f);
        o << "#include <windows.h>\n// #include \"platform/x.h\"\n  #  include \"platform/windows/y.h\"\n#include \"core/z.h\"\n";
    }
    const auto inc = project_includes(f);
    EF_CHECK(inc.size() == 2);
    if (inc.size() == 2) EF_CHECK(inc[0] == "platform/windows/y.h" && inc[1] == "core/z.h");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

EF_TEST(layer_direction_measure_controller_does_not_see_app_or_main_window) {
    // Denetleyici + olcum kaynaklari yalniz MeasureHost'u gorur (app.h / app_internal.h / ana pencere yok).
    const std::filesystem::path app = std::filesystem::path(EF_SOURCE_ROOT) / "src/app";
    int scanned = 0;
    for (const auto& e : std::filesystem::directory_iterator(app)) {
        const auto name = e.path().filename().string();
        if (name.rfind("measure_", 0) != 0) continue;
        ++scanned;
        for (const auto& inc : project_includes(e.path())) {
            EF_CHECK(inc != "app/app.h");
            EF_CHECK(inc != "app/app_internal.h");
            EF_CHECK(inc != "ui/shell/main_window.h");
        }
    }
    EF_CHECK(scanned >= 8);  // controller.{h,cpp}, builtin, balance, ingame, proof, sampling.{h,cpp}, support.h
}
