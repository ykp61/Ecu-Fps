// P9/B5: "Performans etkisini olc" (ABBA kanit deneyi). app_measure.cpp'den ayrildi; davranis degismedi.
#include "app/measure_controller.h"

#include "app/endurance_report_map.h"
#include "app/measure_sampling.h"
#include "app/measure_support.h"
#include "app/worker.h"
#include "benchmark/balance/balance_checks.h"  // spec 2026-11-12 C1/C2: salt okuma uyum denetimleri
#include "benchmark/balance/balance_runner.h"  // spec 2026-11-12 A5: denge testi kosucusu
#include "benchmark/endurance/endurance_runner.h"
#include "core/adapters/windows/display_process.h"
#include "core/diagnosis/balance.h"
#include "core/diagnosis/calibration.h"
#include "core/diagnosis/components.h"
#include "core/diagnosis/game_limit.h"
#include "core/diagnosis/graphics_api.h"
#include "core/diagnosis/mismatch.h"
#include "core/diagnosis/platform.h"
#include "core/diagnostics/incident_engine.h"
#include "core/games/process_risk.h"
#include "core/inventory/power_security.h"
#include "core/insights/history_compare.h"
#include "core/measure/balance_log.h"
#include "core/measure/bench_text.h"
#include "core/measure/run_marker.h"
#include "core/metrics/sampling.h"
#include "core/metrics/sensor.h"
#include "core/perf/admin_plan.h"
#include "core/readiness/readiness.h"
#include "measurement/frame/capture_result.h"
#include "measurement/telemetry/adapter_gpu.h"
#include "measurement/telemetry/process_gpu.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/adapters/win_power.h"
#include "platform/windows/capture_channel.h"
#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/inventory/cpu_sets.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/inventory/power_security_win.h"
#include "platform/windows/inventory/windows_features.h"
#include "platform/windows/metrics/balance_check_sampler.h"
#include "platform/windows/metrics/pdh_sampler.h"
#include "platform/windows/net/speedtest_http.h"
#include "platform/windows/nvidia/nvapi_drs.h"
#include "platform/windows/readiness/background_reader.h"
#include "platform/windows/readiness/display_reader.h"
#include "platform/windows/readiness/storage_reader.h"
#include "platform/windows/run_key_store.h"

namespace ef::app {

using namespace detail;

// ---------------------------------------------------------------------------------------------------------------------
// P1: "Performans etkisini olc" (ABBA kanit deneyi). Tasarim: docs/superpowers/plans/2026-11-17-p1-abba-proof.md
// Saf mantik core::perf::ProofSession'dadir (birim testli, MOCK). Bu bolum gercek PerfController + Arena K1 adimina baglanir;
// gercek cihaz kosusu sahibin Evet/Hayir akisiyla yapilir (bu PC'de otomatik calistirilmaz).
// ---------------------------------------------------------------------------------------------------------------------
// Bir kosunun degerleri + kosul parmak izi. Performans'in kendisinin degistirebildigi alanlar (guc plani / overlay) parmak
// izine GIRMEZ: onlar etkinin parcasidir; sebeke/pil ve pil tasarrufu ise deney boyunca ayni olmali.
core::perf::ProofRun proof_run_from_report(const benchmark::balance::BalanceReport& rep, const BalanceEnvironment& e0,
                                           const BalanceEnvironment& e1, bool crashed, const std::string& app_version,
                                           const std::string& driver) {
    namespace bb = benchmark::balance;
    core::perf::ProofRun r;
    const auto& st = rep.step(bb::StepId::K1);
    auto& c = r.conditions;
    c["arena"] = rep.fingerprint;
    c["adapter"] = platform::to_utf8(rep.adapter.name).substr(0, 160);
    c["driver"] = driver;
    c["resolution"] = std::to_string(st.plan.config.width) + "x" + std::to_string(st.plan.config.height);
    c["power_source"] = e0.on_battery ? "battery" : "ac";
    c["battery_saver"] = e0.battery_saver ? "on" : "off";
    c["app"] = app_version;
    c["mode"] = rep.quick ? "quick" : "full";
    c["cores"] = std::to_string(rep.logical_cores);
    c["preview"] = rep.preview ? "on" : "off";
    if (crashed) {
        r.invalid_reason = "internal_error";
    } else if (!rep.error.empty()) {
        r.invalid_reason = rep.error;  // "no_hardware_adapter", "device_removed", "timeout", ...
    } else if (st.status != bb::StepStatus::Measured) {
        r.invalid_reason = st.status == bb::StepStatus::TimedOut ? "timeout" : "step_not_measured";
    } else if (st.stats.state != core::diagnosis::StepState::Valid) {
        r.invalid_reason = st.stats.reason.empty() ? std::string("invalid") : std::string(st.stats.reason);
    } else if (e0.on_battery != e1.on_battery || e0.battery_saver != e1.battery_saver) {
        r.invalid_reason = "power_source_changed";
    } else {
        const auto fs = core::measure::compute_frame_stats(st.scored_ms);
        if (!fs.avg_fps) {
            r.invalid_reason = "too_few_frames";
        } else {
            r.valid = true;
            r.avg_fps = *fs.avg_fps;
            r.low1_fps = fs.low1_fps;
            r.p99_ms = fs.p99_ms;
        }
    }
    return r;
}

void MeasureController::proof_refresh_estimate() {
    namespace bb = benchmark::balance;
    const bb::StepTiming t = vm_.measure.mode == core::measure::TestMode::Full ? bb::kFullTiming : bb::kQuickTiming;
    vm_.measure.proof.estimate_min = core::perf::estimate_proof_minutes(bb::planned_step_seconds(t));  // gercek isinma + olcum suresinden
}

core::perf::ProofRefusal MeasureController::proof_preconditions(bool* quality_tradeoff, bool gate, std::size_t* scope_groups, bool* scope_partial, bool* scope_admin,
                                                  std::vector<std::string>* scope_keys) {
    namespace cp = core::perf;
    cp::ProofPreconditions p;
    p.other_test_busy = env_.uninstall_running() || core::measure::busy(vm_.measure) ||
                        vm_.measure.endurance.running || vm_.speed.running || env_.admin_busy() || env_.update_holds_writes() || proof_ != nullptr;
    // gate=false: yalniz gorunur on kontrol (verify_or_block yeniden algilama tetikleyebilir); baslatmada her zaman true.
    p.game_blocked = gate && !env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark);  // oyun / algilama guvenilmez -> reddedilir
    std::set<std::string> groups;
    bool partial = false, admin_in = false;
    const bool admin_ok = vm_.admin_without_uac;  // hizmet yolu hazir: yonetici ayarlar UAC'siz olculur
    if (perf()) {
        p.perf_off = perf()->phase() == core::perf::Phase::Off && !perf()->external_conflict() && perf()->off_items().empty();
        p.plan_ready = perf()->preview_ready();
        for (const auto& it : perf()->preview().items) {
            if (!perf()->selected(it.group())) continue;  // yalniz secili gruplar uygulanir
            p.plan_empty = false;
            if (!cp::experiment_scope_item(it, admin_ok)) {  // yonetici / yeniden baslatma / arka plan: olculen alt kumeye girmez
                partial = true;
                p.excluded_admin = p.excluded_admin || it.needs_admin;
                p.excluded_reboot = p.excluded_reboot || it.op.needs_reboot;
                continue;
            }
            groups.insert(it.group());
            admin_in = admin_in || it.needs_admin;
            if (quality_tradeoff && it.op.affects_quality) *quality_tradeoff = true;
        }
    }
    p.scope_groups = groups.size();
    if (scope_groups) *scope_groups = groups.size();
    if (scope_partial) *scope_partial = partial;
    if (scope_admin) *scope_admin = admin_in;
    if (scope_keys) scope_keys->assign(groups.begin(), groups.end());
    return cp::check_proof_preconditions(p);
}

// Baslat dugmesinin yanindaki gorunur neden + olculecek alt kume (deney sirasinda dokunulmaz).
void MeasureController::proof_refresh_precheck() {
    if (proof_) return;
    auto& v = vm_.measure.proof;
    std::size_t g = 0;
    bool partial = false, adm = false;
    const auto r = proof_preconditions(nullptr, false, &g, &partial, &adm);
    if (r == v.precheck && g == v.pre_groups && partial == v.pre_partial && adm == v.pre_admin) return;
    v.precheck = r;
    v.pre_groups = g;
    v.pre_partial = partial;
    v.pre_admin = adm;
    publish_measure();
}

void MeasureController::proof_publish(const char* stage) {
    if (!proof_) return;
    auto& v = vm_.measure.proof;
    v.phase = proof_->phase();
    v.progress = proof_->progress();
    v.stage = stage;
    publish_measure();
}

void MeasureController::start_proof_experiment() {
    namespace cp = core::perf;
    using core::measure::TestPhase;
    if (!perf()) return;
    env_.refresh_admin_path();  // hizmet durumu simdi okunur (salt okuma SCM sorgusu)
    bool quality = false, partial = false, admin = false;
    std::size_t groups = 0;
    auto refuse = [this](cp::ProofRefusal r) {
        vm_.measure.proof.refusal = r;
        publish_measure();
    };
    if (const auto r = proof_preconditions(&quality, true, &groups, &partial, &admin); r != cp::ProofRefusal::None) return refuse(r);
    proof_refresh_estimate();
    // Tek Evet/Hayir (deney baslamadan ONCE): Performans birkac kez Acik/Kapali yapilacak ve sonunda ilk duruma donecek.
    {
        ProofConfirmInfo info;
        info.toggles = cp::proof_plan_size().toggles;
        info.estimate_min = vm_.measure.proof.estimate_min;
        info.groups = groups;
        info.admin = admin;
        info.partial = partial;
        if (!env_.ask_proof_confirm(info)) return;  // pencere yok (tepside) / Hayir / kapatildi: hicbir sey degismedi
    }
    // Onay penceresi iç içe ileti dongusudur: durum bu arada degismis olabilir (oyun acildi, Performans acildi...).
    std::size_t groups2 = 0;
    bool partial2 = false, admin2 = false;
    std::vector<std::string> keys2;
    if (const auto r = proof_preconditions(nullptr, true, &groups2, &partial2, &admin2, &keys2); r != cp::ProofRefusal::None) return refuse(r);
    if (groups2 != groups || partial2 != partial || admin2 != admin) return refuse(cp::ProofRefusal::PlanNotReady);  // onay sirasinda plan degisti: yeniden onay
    if (bench_worker_.joinable()) bench_worker_.join();
    bench_cancel_ = false;
    proof_cancel_ = false;
    proof_wait_ = ProofWait::None;
    proof_plan_fp_.clear();
    proof_scope_keys_ = std::move(keys2);
    proof_ = std::make_unique<cp::ProofSession>(cp::kProofPairs, cp::kProofWarmup, quality);
    proof_->begin();
    proof_->consent(true);
    proof_prev_phase_ = vm_.measure.phase;
    vm_.measure.source = core::measure::TestSource::Builtin;
    vm_.measure.gate_stopped = false;
    vm_.measure.phase = TestPhase::Running;  // mevcut kapilar (tek aktif test, oyun algilaninca durdurma) deneye de uygulanir
    vm_.measure.stage_key = "proof.stage.prepare";
    vm_.measure.elapsed_s = 0;
    vm_.measure.total_s = 0;
    vm_.measure.started_ms = static_cast<std::int64_t>(GetTickCount64());
    vm_.measure.fail_reason.clear();
    vm_.measure.live_view.active = false;  // deney onizleme uretmez (parmak izi "preview=off")
    auto& v = vm_.measure.proof;
    v.refusal = cp::ProofRefusal::None;
    v.result.reset();
    v.scope_groups = groups;
    v.scope_partial = partial;
    v.scope_admin = admin;
    perf()->set_experiment_scope(true, admin);  // gecisler kapsam ogelerini uygular; yonetici ogeler yalniz hizmet yolundan (UAC acilmaz)
    env_.set_admin_service_only(true);           // hizmet dusarse UAC broker'ina DUSULMEZ: deney durur, geri yukleme denenir
    env_.begin_run_marker("proof");
    proof_publish("prepare");
    post([this] { proof_step(); });
}

void MeasureController::stop_proof_experiment() {
    if (!proof_) return;
    proof_cancel_ = true;
    bench_cancel_ = true;  // suren olcum kosusunu da durdurur
    // Olcum kosusu suruyorsa sonucu cancelled=true ile gelir; aksi halde bekleyen adim tamamlaninca proof_step iptali gorur.
    if (proof_wait_ == ProofWait::None) post([this] { proof_step(); });
}

void MeasureController::proof_step() {
    namespace cp = core::perf;
    if (!proof_) return;
    if (proof_wait_ != ProofWait::None) return;  // adim suruyor (controller gecisi veya olcum)
    if (proof_->phase() == cp::ProofPhase::Running && (proof_cancel_ || bench_cancel_)) proof_->cancel();
    const auto a = proof_->next();
    if (!a) return proof_finish();
    // Arm gecislerinde sonucu session'a ileten kucuk yardimci (kol degisimi ya da son geri yukleme).
    const auto deliver = [this](cp::ProofArmOutcome o) {
        const auto cur = proof_->next();
        if (cur && cur->kind == cp::ProofActionKind::RestoreOriginal) proof_->on_restore(o);
        else proof_->on_arm_set(o);
        post([this] { proof_step(); });
    };
    using core::perf::Phase;
    switch (a->kind) {
        case cp::ProofActionKind::MeasureRun: {
            proof_wait_ = ProofWait::Run;
            const char* stage = !a->scored ? "warmup" : (a->arm == cp::ProofArm::On ? "run_on" : "run_off");
            proof_publish(stage);
            proof_start_run(*a);
            return;
        }
        case cp::ProofActionKind::SetArm:
        case cp::ProofActionKind::RestoreOriginal: {
            if (!perf()) return deliver(cp::ProofArmOutcome::Unverified);
            const bool to_on = a->kind == cp::ProofActionKind::SetArm && a->arm == cp::ProofArm::On;
            const auto ph = perf()->phase();
            if (to_on) {
                if (ph != Phase::Off) return deliver(cp::ProofArmOutcome::NotApplied);  // beklenmedik durum: gecis yapilmaz
                proof_wait_ = ProofWait::Plan;
                proof_publish("on");
                perf()->toggle();  // Kapali -> plan -> (Evet vekaleten, ayni plan) -> uygula + geri oku
                return;
            }
            // Kapali'ya donus: yalniz mevcut geri yukleme akisi (journal'dan onceki degerler, dogrulama).
            if (ph == Phase::Off) return deliver(cp::ProofArmOutcome::Applied);  // zaten Kapali (dogrulanmis)
            if (ph != Phase::On) return deliver(cp::ProofArmOutcome::Unverified);
            proof_wait_ = ProofWait::RestoreConfirm;
            proof_publish(a->kind == cp::ProofActionKind::RestoreOriginal ? "restore" : "off");
            perf()->toggle();
            return;
        }
    }
}

void MeasureController::proof_on_perf_change() {
    namespace cp = core::perf;
    using core::perf::Phase;
    if (!proof_ || !perf() || proof_wait_ == ProofWait::None || proof_wait_ == ProofWait::Run) return;
    const auto ph = perf()->phase();
    const auto deliver = [this](cp::ProofArmOutcome o) {
        proof_wait_ = ProofWait::None;
        const auto cur = proof_->next();
        if (cur && cur->kind == cp::ProofActionKind::RestoreOriginal) proof_->on_restore(o);
        else proof_->on_arm_set(o);
        post([this] { proof_step(); });
    };
    switch (proof_wait_) {
        case ProofWait::Plan:
            if (ph == Phase::Planning) return;
            if (ph == Phase::Confirming) {
                const auto fp = cp::plan_fingerprint(perf()->plan());
                if (proof_plan_fp_.empty()) proof_plan_fp_ = fp;  // ilk onaylanan plan; sonrakiler ayni olmali
                if (fp != proof_plan_fp_ || proof_cancel_ || bench_cancel_) {
                    // Plan degisti ya da iptal: Hayir (hicbir sey yazilmaz); deney yarida kalir.
                    proof_wait_ = ProofWait::None;
                    post([this, changed = fp != proof_plan_fp_] {
                        if (changed) proof_->abort("plan_changed");
                        else proof_->cancel();  // kullanici iptali / oyun: onay verilmeden vazgecildi
                        perf()->answer(false);
                        const auto cur = proof_->next();
                        if (cur && cur->kind == core::perf::ProofActionKind::SetArm) proof_->on_arm_set(core::perf::ProofArmOutcome::NotApplied);
                        post([this] { proof_step(); });
                    });
                    return;
                }
                proof_wait_ = ProofWait::Apply;
                post([this] { perf()->answer(true); });  // tek onay vekaleten: ayni plan, ayni uygulama + geri okuma yolu
                return;
            }
            deliver(cp::ProofArmOutcome::NotApplied);  // plan bos / basarisiz / reddedildi: Kapali kaldi
            return;
        case ProofWait::Apply:
            if (ph == Phase::Applying || ph == Phase::Confirming || ph == Phase::Planning) return;
            if (ph == Phase::On) {
                const bool clean = perf()->notice() == core::perf::Notice::None;
                deliver(cp::ProofArmOutcome::Applied);
                if (!clean) proof_->abort("arm_partial");  // kismi uygulandi: Acik kolu tam degil; deney durur, geri yuklenir
                return;
            }
            deliver(ph == Phase::RecoveryNeeded ? cp::ProofArmOutcome::Unverified : cp::ProofArmOutcome::NotApplied);
            return;
        case ProofWait::RestoreConfirm:
            if (ph == Phase::ConfirmRestore) {
                proof_wait_ = ProofWait::Restore;
                post([this] { perf()->answer(true); });
                return;
            }
            if (ph == Phase::Planning) return;
            if (ph == Phase::Confirming) {  // bazi ayarlar gercekte kapali: toggle plan akisina girdi; geri yukleme degil
                proof_wait_ = ProofWait::None;
                post([this] {
                    perf()->answer(false);
                    const auto cur = proof_->next();
                    if (cur && cur->kind == core::perf::ProofActionKind::RestoreOriginal) proof_->on_restore(core::perf::ProofArmOutcome::NotApplied);
                    else proof_->on_arm_set(core::perf::ProofArmOutcome::NotApplied);
                    post([this] { proof_step(); });
                });
                return;
            }
            deliver(ph == Phase::Off ? cp::ProofArmOutcome::Applied : cp::ProofArmOutcome::Unverified);
            return;
        case ProofWait::Restore:
            if (ph == Phase::Restoring || ph == Phase::ConfirmRestore) return;
            if (ph == Phase::Off) return deliver(cp::ProofArmOutcome::Applied);
            deliver(ph == Phase::RecoveryNeeded ? cp::ProofArmOutcome::Unverified : cp::ProofArmOutcome::NotApplied);
            return;
        default: return;
    }
}

void MeasureController::proof_start_run(const core::perf::ProofAction&) {
    namespace bb = benchmark::balance;
    if (bench_worker_.joinable()) bench_worker_.join();
    bench_worker_ = spawn_worker([this, quick = vm_.measure.mode != core::measure::TestMode::Full,
                                  version = vm_.version, driver = vm_.gpu_driver, hwnd = env_.window_hwnd()] {
        // Uygulama/Denge Testi ile ayni: test suresince bu surec EcoQoS'tan cikar.
        platform::adapters::WinSelfThrottleApi self_throttle;
        core::adapters::windows::ScopedExecutionSpeedOptOut eco_opt_out(self_throttle);
        bb::BalanceOptions o;
        o.quick = quick;
        o.only_step = bb::StepId::K1;  // CPU + GPU birlikte, 1080p: tek adim; her kosu bagimsiz cihaz acar
        o.keep_scored_frames = true;
        std::atomic<std::int64_t> minimized_us{std::numeric_limits<std::int64_t>::min()};
        if (hwnd)
            o.window_minimized_during = [hwnd, &minimized_us](std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point) {
                return IsIconic(hwnd) != FALSE || minimized_us.load(std::memory_order_relaxed) >= steady_us(begin);
            };
        const BalanceEnvironment e0 = read_balance_environment();
        bb::BalanceReport rep;
        bool crashed = false;
        try {
            rep = bb::run_balance(o, bench_cancel_);
        } catch (...) {
            crashed = true;
        }
        const BalanceEnvironment e1 = read_balance_environment();
        auto run = proof_run_from_report(rep, e0, e1, crashed, version, driver);
        const bool cancelled = rep.cancelled;
        // Sert hata: cihaz yok / kayip / cokme. Isinma kosusunda "kararsiz" gibi yumusak nedenler deneyi bitirmez.
        const bool hard = crashed || !rep.error.empty();
        post([this, run = std::move(run), cancelled, hard]() mutable { proof_run_done(std::move(run), cancelled, hard); });
    });
}

void MeasureController::proof_run_done(core::perf::ProofRun run, bool cancelled, bool hard_fail) {
    if (!proof_) return;
    proof_wait_ = ProofWait::None;
    const auto a = proof_->next();
    if (cancelled || proof_cancel_ || bench_cancel_) {
        proof_->cancel();  // yarim kosu sonuc degildir; Acik ise geri yukleme istenir
    } else if (a && a->kind == core::perf::ProofActionKind::MeasureRun) {
        // Isinma kosusunun degeri yok sayilir (session yine ilerler); ama isinma bile kosamadiysa (cihaz yok / kayip / cokme)
        // deney devam etmez. Puanlanan kosu gecersizse session hukumsuz bitirir.
        if (!a->scored && hard_fail) proof_->abort(run.invalid_reason.empty() ? "warmup_failed" : run.invalid_reason);
        else proof_->on_run(std::move(run));
    }
    vm_.measure.elapsed_s = static_cast<double>(GetTickCount64() - static_cast<std::uint64_t>(vm_.measure.started_ms)) / 1000.0;
    proof_step();
}

void MeasureController::proof_finish() {
    namespace cp = core::perf;
    using core::measure::TestPhase;
    if (!proof_) return;
    auto& v = vm_.measure.proof;
    v.phase = proof_->phase();
    v.progress = proof_->progress();
    v.stage.clear();
    v.result = proof_->result();
    if (vm_.measure.gate_stopped && v.result && !cp::has_verdict(v.result->verdict)) v.refusal = cp::ProofRefusal::GameRunning;  // oyun acilinca durdu
    // P4: hukum (Fark yok / anlamli fark) olculen gruplar icin kalici kaydedilir; hukumsuz sonuc kaydi degistirmez.
    // Yalniz gorunum vurgusu (onay ekraninda "Ayrintilar"); secim, plan ve uygulama ayni kalir.
    if (v.result && cp::has_verdict(v.result->verdict)) {
        auto next = cp::update_no_effect_groups(env_.prefs().proof_no_effect_groups, proof_scope_keys_, v.result->verdict);
        if (next != env_.prefs().proof_no_effect_groups) {
            env_.prefs().proof_no_effect_groups = std::move(next);
            env_.save_prefs();
            if (perf()) perf()->set_no_effect_groups(env_.prefs().proof_no_effect_groups);
        }
    }
    proof_scope_keys_.clear();
    proof_.reset();
    if (perf()) perf()->set_experiment_scope(false);
    env_.set_admin_service_only(false);
    proof_wait_ = ProofWait::None;
    proof_cancel_ = false;
    bench_cancel_ = false;
    env_.end_run_marker();
    vm_.measure.stage_key.clear();
    vm_.measure.gate_stopped = false;
    vm_.measure.phase = proof_prev_phase_ == TestPhase::Stopping || proof_prev_phase_ == TestPhase::Running || proof_prev_phase_ == TestPhase::Preparing
                            ? TestPhase::Idle
                            : proof_prev_phase_;  // onceki test sonucu ("Onceki sonuc") korunur
    if (perf()) perf()->refresh_preview();
    proof_refresh_precheck();
    env_.start_findings_refresh();  // deney boyunca ertelenen sistem kontrolu bulgulari
    publish_measure();
    env_.check_pending_exit();  // Cik deney yuzunden bekliyordu: Performans artik dondu (ya da donemedi) -> normal cikis kurali
}

}  // namespace ef::app
