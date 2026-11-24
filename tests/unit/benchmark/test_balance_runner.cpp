// Denge testi kosucusu (spec 2026-11-12 A2): adim plani, pencereler ve zaman butcesi, plan kimligi, sahne
// sonucundan kural girdisi, girdilerin eslenmesi, baslamadan iptal ve ortak bekci (saf kisimlar; D3D yok).
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "benchmark/balance/balance_checks.h"
#include "benchmark/balance/balance_runner.h"
#include "benchmark/watchdog.h"
#include "ef_test.h"

using namespace ef::benchmark;
namespace bal = ef::benchmark::balance;
namespace dg = ef::core::diagnosis;

namespace {

bool close_to(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// `fps` hizinda `seconds` saniyelik sabit kare araligi serisi.
std::vector<double> steady(double fps, double seconds) {
    return std::vector<double>(static_cast<std::size_t>(fps * seconds), 1000.0 / fps);
}

// Olculmus bir sahne sonucu: GPU tamamlama araligi `fps`, kare basi GPU suresi `gpu_ms`.
SceneRunResult measured_scene(double fps, double seconds, double gpu_ms) {
    SceneRunResult r;
    r.ok = true;
    r.arena = true;
    r.gpu_timestamps = true;
    r.gpu_pacing_ms = steady(fps, seconds);
    r.frame_ms = r.gpu_pacing_ms;
    r.gpu_elapsed_ms.assign(r.frame_ms.size(), gpu_ms);
    r.frame_source = FrameSource::GpuCompletion;
    return r;
}

bal::StepResult measured_step(double fps, double busy) {
    bal::StepResult s;
    s.status = bal::StepStatus::Measured;
    s.stats.state = dg::StepState::Valid;
    s.stats.fps = fps;
    s.stats.frames = 1000;
    s.stats.seconds = 8;
    s.stats.gpu_busy_ratio = busy;
    return s;
}

}  // namespace

EF_TEST(balance_plan_matches_spec_table) {
    const bal::BalanceOptions full{};
    const auto plan = bal::balance_plan(full, 16);
    const bal::StepId order[bal::kStepCount] = {bal::StepId::S1, bal::StepId::S2, bal::StepId::S3, bal::StepId::S4, bal::StepId::K1,
                                                bal::StepId::K2, bal::StepId::K3, bal::StepId::T1, bal::StepId::T2};
    const char* keys[bal::kStepCount] = {"S1", "S2", "S3", "S4", "K1", "K2", "K3", "T1", "T2"};
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        EF_CHECK(plan[i].id == order[i] && static_cast<std::size_t>(plan[i].id) == i && bal::to_string(plan[i].id) == keys[i]);
        const SceneConfig& c = plan[i].config;
        EF_CHECK(c.arena && arena_config_valid(c) && duration_mode(c));
        EF_CHECK(c.warmup_s == 2.0 && close_to(c.measure_s, 8.0 + bal::kMeasurePadS));  // Tam: 2 sn + 8 sn (+ tam saniye payi)
        EF_CHECK(c.frame_tap == nullptr && c.preview == nullptr);
    }
    // S1 / T1: CPU kapasitesi, 320x180, C + Gmin.
    for (const std::size_t i : {std::size_t{0}, std::size_t{7}}) {
        const auto& p = plan[i];
        EF_CHECK(p.kind == (i == 0 ? bal::StepKind::CpuCapacity : bal::StepKind::CpuRepeat) && p.row == -1);
        EF_CHECK(p.cpu == &kArenaCpuC && p.gpu == &kArenaGpuGmin && p.config.width == 320 && p.config.height == 180);
        EF_CHECK(p.config.sim && p.config.sim_objects == 20000 && p.config.object_draws == 4000 && p.config.draw_mode == DrawMode::PerObject);
        EF_CHECK(p.config.sim_workers == 8 && p.config.arena_lights == 8 && !p.config.arena_bloom);  // min(16 - 1, 8)
    }
    // S2-S4 / T2: GPU kapasitesi, Cmin + G; K1-K3: birlikte, C + G; ayni uc cozunurluk.
    const std::uint32_t w[3] = {1920, 2560, 3840}, h[3] = {1080, 1440, 2160};
    for (int row = 0; row < 3; ++row) {
        const auto& s = plan[static_cast<std::size_t>(1 + row)];
        const auto& k = plan[static_cast<std::size_t>(4 + row)];
        EF_CHECK(s.kind == bal::StepKind::GpuCapacity && s.row == row && s.cpu == &kArenaCpuCmin && s.gpu == &kArenaGpuG);
        EF_CHECK(s.config.width == w[row] && s.config.height == h[row] && !s.config.sim && s.config.sim_workers == 0);
        EF_CHECK(s.config.draw_mode == DrawMode::Instanced && s.config.arena_lights == 64 && s.config.arena_bloom);
        EF_CHECK(k.kind == bal::StepKind::Coupled && k.row == row && k.cpu == &kArenaCpuC && k.gpu == &kArenaGpuG);
        EF_CHECK(k.config.width == w[row] && k.config.height == h[row] && k.config.sim && k.config.draw_mode == DrawMode::PerObject);
        EF_CHECK(k.config.arena_lights == 64 && k.config.arena_bloom && k.config.sim_workers == 8);
        EF_CHECK(bal::kRowResolutions[static_cast<std::size_t>(row)].width == w[row]);
    }
    EF_CHECK(plan[8].kind == bal::StepKind::GpuRepeat && plan[8].row == 0);
    // Tekrar adimlari ilk olcumle AYNI is yukudur (suruklenme yalniz zamana bagli olsun).
    EF_CHECK(arena_workload_hash(plan[7].config) == arena_workload_hash(plan[0].config));
    EF_CHECK(arena_workload_hash(plan[8].config) == arena_workload_hash(plan[1].config));
    EF_CHECK(arena_workload_hash(plan[4].config) != arena_workload_hash(plan[1].config));
    // Cekirdek sayisi isci sayisini belirler: min(mantiksal - 1, 8), en az 1; 0 -> 1 cekirdek sayilir.
    EF_CHECK(bal::balance_plan(full, 4)[0].config.sim_workers == 3);
    EF_CHECK(bal::balance_plan(full, 0)[0].config.sim_workers == 1 && bal::balance_plan(full, 1)[0].config.sim_workers == 1);
    // Hizli: 1 sn + 4 sn.
    bal::BalanceOptions quick;
    quick.quick = true;
    for (const auto& p : bal::balance_plan(quick, 16)) EF_CHECK(p.config.warmup_s == 1.0 && close_to(p.config.measure_s, 4.0 + bal::kMeasurePadS));
    // Tohum tum adimlara tasinir.
    bal::BalanceOptions seeded;
    seeded.seed = 42;
    for (const auto& p : bal::balance_plan(seeded, 16)) EF_CHECK(p.config.seed == 42);
}

EF_TEST(balance_timing_budget_and_test_overrides) {
    bal::BalanceOptions o;
    EF_CHECK(bal::effective_timing(o).warmup_s == 2.0 && bal::effective_timing(o).measure_s == 8.0);
    o.quick = true;
    EF_CHECK(bal::effective_timing(o).warmup_s == 1.0 && bal::effective_timing(o).measure_s == 4.0);
    static_assert(bal::kFullTiming.warmup_s == 2.0 && bal::kFullTiming.measure_s == 8.0 && bal::kQuickTiming.warmup_s == 1.0 &&
                  bal::kQuickTiming.measure_s == 4.0);
    // Toplam: Tam ~90 sn, Hizli ~45 sn (tam saniye payi dahil).
    EF_CHECK(close_to(bal::planned_seconds(bal::kFullTiming), 9 * 10.25) && close_to(bal::planned_seconds(bal::kQuickTiming), 9 * 5.25));
    EF_CHECK(bal::planned_seconds(bal::kFullTiming) < 95.0 && bal::planned_seconds(bal::kQuickTiming) < 50.0);
    // Her adimin kendi zaman butcesi vardir: pencere + hazirlik payi; mevcut 120 sn'lik alt test bekcisinden kisa.
    EF_CHECK(close_to(bal::step_budget_s(bal::kFullTiming), 10.25 + bal::kStepOverheadBudgetS) && bal::step_budget_s(bal::kFullTiming) < 120.0);
    EF_CHECK(bal::step_budget_s(bal::kQuickTiming) < bal::step_budget_s(bal::kFullTiming));
    EF_CHECK(bal::step_budget_s(bal::StepTiming{-5.0, -1.0}) == bal::kMeasurePadS + bal::kStepOverheadBudgetS);
    // Yalniz test icin gecersiz kilma: pencere ve cozunurlukler.
    o.timing_override = bal::StepTiming{0.1, 0.4};
    o.cpu_resolution_override = bal::Resolution{160, 90};
    o.row_resolution_override = std::array<bal::Resolution, dg::kBalanceRows>{{{320, 180}, {384, 216}, {480, 270}}};
    EF_CHECK(bal::effective_timing(o).warmup_s == 0.1 && bal::effective_timing(o).measure_s == 0.4);
    const auto plan = bal::balance_plan(o, 8);
    EF_CHECK(plan[0].config.width == 160 && plan[7].config.height == 90 && plan[0].config.warmup_s == 0.1);
    EF_CHECK(plan[2].config.width == 384 && plan[5].config.width == 384 && plan[3].config.height == 270 && plan[8].config.width == 320);
    // Urun varsayilanlari: gercek GPU, yazilim bagdastiricisi reddedilir, butce siniri yok.
    const bal::BalanceOptions d{};
    EF_CHECK(d.prefer_hardware && !d.allow_software && !d.timing_override && !d.row_resolution_override && !d.cpu_resolution_override);
    EF_CHECK(d.vram_budget_limit == 0 && d.vram_limit_row == -1 && d.frame_tap == nullptr && d.preview == nullptr && !d.quick);
    EF_CHECK(!d.step_budget_override_s && d.step_budget_row == -1);  // adim butcesi: step_budget_s(pencere)
}

EF_TEST(balance_plan_fingerprint_identity) {
    bal::BalanceOptions o;
    const std::string fp = bal::plan_fingerprint(o, 16);
    EF_CHECK(fp == bal::plan_fingerprint(o, 16));
    EF_CHECK(fp.find(kArenaVersion) == 0 && fp.find(kFrameSimVersion) != std::string::npos && fp.find("|full|") != std::string::npos);
    // Kimlige girmeyenler: canli sayac, onizleme, bildirim, yalniz teste ozel butce siniri.
    FrameTap tap;
    const PreviewCallback cb = [](const PreviewFrame&) {};
    bal::BalanceOptions same = o;
    same.frame_tap = &tap;
    same.preview = &cb;
    same.progress = [](const bal::StepProgress&) {};
    same.vram_budget_limit = 1;
    same.vram_limit_row = 2;
    same.step_budget_override_s = 1.0;
    same.step_budget_row = 1;
    same.allow_software = true;
    EF_CHECK(bal::plan_fingerprint(same, 16) == fp);
    // Kimligi degistirenler: kip, tohum, isci sayisi (cekirdek), pencere, cozunurluk.
    bal::BalanceOptions v = o;
    v.quick = true;
    EF_CHECK(bal::plan_fingerprint(v, 16) != fp && bal::plan_fingerprint(v, 16).find("|quick|") != std::string::npos);
    v = o, v.seed ^= 1;
    EF_CHECK(bal::plan_fingerprint(v, 16) != fp);
    EF_CHECK(bal::plan_fingerprint(o, 4) != fp);
    EF_CHECK(bal::plan_fingerprint(o, 9) == bal::plan_fingerprint(o, 32));  // ikisi de 8 isci
    v = o, v.timing_override = bal::StepTiming{2.0, 8.5};
    EF_CHECK(bal::plan_fingerprint(v, 16) != fp && bal::plan_fingerprint(v, 16).find("|custom|") != std::string::npos);
    v = o, v.row_resolution_override = std::array<bal::Resolution, dg::kBalanceRows>{{{1920, 1080}, {2560, 1440}, {3200, 1800}}};
    EF_CHECK(bal::plan_fingerprint(v, 16) != fp);
    v = o, v.cpu_resolution_override = bal::Resolution{640, 360};
    EF_CHECK(bal::plan_fingerprint(v, 16) != fp);
}

EF_TEST(balance_step_stats_from_scene_result) {
    // Gecerli olcum: 250 FPS, 4 tam saniye, GPU suresi 3,9 / 4 ms -> mesgul 0,975.
    SceneRunResult r = measured_scene(250.0, 4.25, 3.9);
    dg::StepStats s = bal::step_stats_from(r, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Valid && s.reason.empty() && s.seconds == 4 && close_to(s.fps, 250.0, 1e-6));
    EF_CHECK(s.gpu_busy_ratio && close_to(*s.gpu_busy_ratio, 0.975, 1e-9) && s.disjoint_frames == 0);
    EF_CHECK(dg::bound_confirmed(dg::StepRole::GpuCapacity, s.gpu_busy_ratio) && !dg::bound_confirmed(dg::StepRole::CpuCapacity, s.gpu_busy_ratio));
    // Seri: zaman damgasi kaynagi gecerliyse GPU tamamlama araligi, degilse gonderim araligi.
    r.frame_ms = steady(100.0, 4.25);
    EF_CHECK(close_to(bal::step_stats_from(r, 4.0, true).fps, 250.0, 1e-6));
    r.frame_source = FrameSource::SubmitCadence;
    s = bal::step_stats_from(r, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Valid && close_to(s.fps, 100.0, 1e-6));
    // denge_v2: tek gecersiz zaman damgali kare adimi bozmaz; karelerin %1'inden fazlasi bozar (deger kullanilmaz).
    r.gpu_excluded = 1;
    s = bal::step_stats_from(r, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Valid && s.disjoint_frames == 1);
    r.gpu_excluded = static_cast<std::uint32_t>(r.frame_ms.size() / 50);  // %2
    s = bal::step_stats_from(r, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Invalid && s.reason == dg::balance_reason::kTimestampDisjoint);
    // denge_v4: S1/T1 (needs_gpu_time=false) %2 gecersiz kareyle gecerli kalir ve oran bos olur; S2-S4/K/T2 gecersiz.
    s = bal::step_stats_from(r, 4.0, false);
    EF_CHECK(s.state == dg::StepState::Valid && s.reason.empty() && !s.gpu_busy_ratio && s.disjoint_frames == r.gpu_excluded);
    EF_CHECK(bal::step_stats_from(r, 4.0, true).reason == dg::balance_reason::kTimestampDisjoint);
    EF_CHECK(!bal::needs_gpu_time(bal::StepKind::CpuCapacity) && !bal::needs_gpu_time(bal::StepKind::CpuRepeat));
    EF_CHECK(bal::needs_gpu_time(bal::StepKind::GpuCapacity) && bal::needs_gpu_time(bal::StepKind::Coupled) &&
              bal::needs_gpu_time(bal::StepKind::GpuRepeat));
    // Gecerlilik NOMINAL olcum suresiyle denetlenir: 4 sn icin >= 3, 8 sn icin >= 7 saniyelik ornek.
    r = measured_scene(250.0, 3.2, 1.0);
    EF_CHECK(bal::step_stats_from(r, 4.0, true).state == dg::StepState::Valid);
    EF_CHECK(bal::step_stats_from(r, 8.0, true).reason == dg::balance_reason::kTooFewSeconds);
    // GPU zaman damgasi yoksa mesgul orani bilinmez: sinir dogrulanmaz (deger "en az" kalir).
    r = measured_scene(250.0, 4.25, 1.0);
    r.gpu_timestamps = false;
    r.gpu_elapsed_ms.clear();
    r.gpu_pacing_ms.clear();
    r.frame_source = FrameSource::SubmitCadence;
    s = bal::step_stats_from(r, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Valid && !s.gpu_busy_ratio);
    EF_CHECK(!dg::bound_confirmed(dg::StepRole::CpuCapacity, s.gpu_busy_ratio) && !dg::bound_confirmed(dg::StepRole::GpuCapacity, s.gpu_busy_ratio));

    // Hedef / butce hatasi: olculemedi (GPU siniri hukmu degil).
    SceneRunResult bad;
    bad.error = "vram_budget";
    s = bal::step_stats_from(bad, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Unmeasurable && s.reason == dg::balance_reason::kVramBudget && s.fps == 0);
    bad.error = "target_create";
    s = bal::step_stats_from(bad, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Unmeasurable && s.reason == dg::balance_reason::kTargetFailed);
    // Olcum araligi olusmadi: gecersiz olcum (sahte 0 FPS degeri tasinmaz).
    bad.error = "no_measured_frames";
    s = bal::step_stats_from(bad, 4.0, true);
    EF_CHECK(s.state == dg::StepState::Invalid && s.reason == dg::balance_reason::kTooFewFrames && s.fps == 0);
    // Cihaz kaybi / zaman asimi / iptal: deger yok (kosu hata ile biter).
    for (const char* e : {"device_removed", "gpu_timeout", "sim_checksum", ""}) {
        bad.error = e;
        s = bal::step_stats_from(bad, 4.0, true);
        EF_CHECK(s.state == dg::StepState::NotRun && s.fps == 0 && !s.gpu_busy_ratio);
    }
}

EF_TEST(balance_build_inputs_maps_steps_to_rule) {
    std::array<bal::StepResult, bal::kStepCount> steps;
    const double fps[bal::kStepCount] = {100, 300, 200, 60, 98, 97, 59, 101, 295};
    const double busy[bal::kStepCount] = {0.20, 0.99, 0.99, 0.99, 0.35, 0.50, 0.99, 0.21, 0.98};
    for (std::size_t i = 0; i < bal::kStepCount; ++i) steps[i] = measured_step(fps[i], busy[i]);
    steps[0].sim_ms = 2.0, steps[0].cull_ms = 1.0, steps[0].submit_ms = 5.0;
    dg::BalanceInputs in = bal::build_inputs(steps);
    EF_CHECK(in.cpu.fps == 100 && in.cpu_repeat.fps == 101 && in.gpu_repeat.fps == 295);
    EF_CHECK(in.gpu[0].fps == 300 && in.gpu[1].fps == 200 && in.gpu[2].fps == 60);
    EF_CHECK(in.coupled[0].fps == 98 && in.coupled[1].fps == 97 && in.coupled[2].fps == 59);
    EF_CHECK(in.cpu_split && in.cpu_split->sim_ms == 2.0 && in.cpu_split->cull_ms == 1.0 && in.cpu_split->submit_ms == 5.0);
    // Karistiricilar ve CPU hiz dususu kosucuda doldurulmaz (cagiran doldurur).
    EF_CHECK(!in.confounders.on_battery && !in.confounders.battery_saver && !in.confounders.power_saver_plan &&
              !in.confounders.vendor_throttle && !in.confounders.other_cpu_pct && !in.cpu_speed_drop);
    // Uctan uca kural: 1080p CPU (300 / 100, K mesgul 0,35), 1440p CPU (200 / 100), 2160p GPU (60 / 100, K mesgul 0,99).
    dg::BalanceResult v = dg::evaluate_balance(in);
    EF_CHECK(v.cpu_fps && *v.cpu_fps == 100 && !v.cpu_lower_bound && v.global_reason.empty());
    EF_CHECK(v.rows[0].verdict == dg::BalanceVerdict::CpuLimited && v.rows[1].verdict == dg::BalanceVerdict::CpuLimited &&
              v.rows[2].verdict == dg::BalanceVerdict::GpuLimited);
    EF_CHECK(v.cpu_sim_share && close_to(*v.cpu_sim_share, 0.25) && v.cpu_draw_share && close_to(*v.cpu_draw_share, 0.75));

    // Bir cozunurluk olculemedi: yalniz o satir "olculemedi"; digerleri hukum alir.
    auto partial = steps;
    partial[2] = bal::StepResult{};
    partial[2].status = bal::StepStatus::Unmeasurable;
    partial[2].stats = dg::unmeasurable_step(dg::balance_reason::kVramBudget);
    partial[5] = partial[2];
    v = dg::evaluate_balance(bal::build_inputs(partial));
    EF_CHECK(v.rows[1].verdict == dg::BalanceVerdict::Unmeasurable && v.rows[1].reason == dg::balance_reason::kVramBudget);
    EF_CHECK(v.rows[0].verdict == dg::BalanceVerdict::CpuLimited && v.rows[2].verdict == dg::BalanceVerdict::GpuLimited);
    // Zaman butcesini asan cozunurluk: "olculemedi (timeout)".
    partial[2].stats = dg::unmeasurable_step(dg::balance_reason::kTimeout);
    partial[2].status = bal::StepStatus::TimedOut;
    v = dg::evaluate_balance(bal::build_inputs(partial));
    EF_CHECK(v.rows[1].verdict == dg::BalanceVerdict::Unmeasurable && v.rows[1].reason == dg::balance_reason::kTimeout);

    // S1 olculmediyse / gecersizse CPU payi uydurulmaz ve tum satirlar cekimser kalir.
    auto no_cpu = steps;
    no_cpu[0].stats.state = dg::StepState::Invalid;
    no_cpu[0].stats.reason = dg::balance_reason::kUnstable;
    in = bal::build_inputs(no_cpu);
    EF_CHECK(!in.cpu_split);
    v = dg::evaluate_balance(in);
    EF_CHECK(!v.cpu_fps && v.global_reason == dg::balance_reason::kCpuStepInvalid);
    for (const auto& row : v.rows) EF_CHECK(row.verdict == dg::BalanceVerdict::Undetermined);
    // Hic kosulmamis dizi: girdilerin hepsi NotRun.
    in = bal::build_inputs(std::array<bal::StepResult, bal::kStepCount>{});
    EF_CHECK(in.cpu.state == dg::StepState::NotRun && in.gpu[2].state == dg::StepState::NotRun && in.gpu_repeat.state == dg::StepState::NotRun);
}

EF_TEST(balance_only_step_selects_one_step_and_keeps_identity) {
    // P1: tek adim kipi (mock test; cihaz acilmaz). Varsayilan: dokuz adim; kimlik only_step'ten etkilenmez.
    bal::BalanceOptions all;
    all.quick = true;
    bal::BalanceOptions one = all;
    one.only_step = bal::StepId::K1;
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        const auto id = static_cast<bal::StepId>(i);
        EF_CHECK(bal::step_selected(all, id));
        EF_CHECK(bal::step_selected(one, id) == (id == bal::StepId::K1));
    }
    EF_CHECK(bal::plan_fingerprint(all, 6) == bal::plan_fingerprint(one, 6));
    EF_CHECK(!all.keep_scored_frames);  // urun yolu kare listesi tutmaz
}

EF_TEST(balance_run_cancelled_before_start_runs_nothing) {
    std::atomic<bool> cancel{true};
    FrameTap tap;
    tap.begin_step(5, 9, 5, 1920, 1080);
    tap.set_phase(TapPhase::Measure);
    int events = 0;
    bal::BalanceOptions o;
    o.quick = true;
    o.logical_cores = 6;
    o.frame_tap = &tap;
    o.progress = [&](const bal::StepProgress&) { ++events; };
    int before = 0;
    o.before_steps = [&](const AdapterInfo&, const std::atomic<bool>&) { ++before; };
    const bal::BalanceReport r = bal::run_balance(o, cancel);  // cihaz acilmadan doner
    EF_CHECK(r.cancelled && !r.ok && r.error.empty() && events == 0);
    EF_CHECK(before == 0 && r.retries == 0);  // D6: cihaz acilmadan arka plan GPU ornegi alinmaz; tekrar yok
    EF_CHECK(r.quick && r.timing.measure_s == 4.0 && r.logical_cores == 6 && r.fingerprint == bal::plan_fingerprint(o, 6));
    EF_CHECK(r.adapter.kind == RendererKind::None && !r.software && !r.preview);
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        const bal::StepResult& s = r.steps[i];
        EF_CHECK(static_cast<std::size_t>(s.plan.id) == i && s.status == bal::StepStatus::NotRun && s.stats.state == dg::StepState::NotRun);
        EF_CHECK(s.stats.fps == 0 && !s.bound_confirmed && s.measured_frames == 0 && s.workload_hash == arena_workload_hash(s.plan.config));
        EF_CHECK(&r.step(s.plan.id) == &s);
        EF_CHECK(s.attempts == 0);
    }
    // Hukum uretilmez: S1 kosulmadi.
    const dg::BalanceResult v = dg::evaluate_balance(r.inputs);
    EF_CHECK(!v.cpu_fps && v.global_reason == dg::balance_reason::kCpuStepInvalid);
    // Canli sayac bosa doner.
    const FrameTapLive live = tap.snapshot();
    EF_CHECK(live.step_index == 0 && live.step_total == 0 && live.phase == TapPhase::Idle && !live.has_window);
}

EF_TEST(balance_shared_watchdog_cancel_and_timeout) {
    const auto wait_for = [](const std::atomic<bool>& flag) {
        const auto t0 = std::chrono::steady_clock::now();
        while (!flag.load() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5)) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return flag.load();
    };
    {  // Ust iptal yerel bayraga tasinir; zaman asimi sayilmaz.
        std::atomic<bool> parent{false};
        Watchdog w(parent, 3600.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        EF_CHECK(!w.local.load() && !w.timed_out.load());
        parent = true;
        EF_CHECK(wait_for(w.local) && !w.timed_out.load());
    }
    {  // Zaman butcesi asilinca yerel iptal + timed_out.
        std::atomic<bool> parent{false};
        Watchdog w(parent, 0.05);
        EF_CHECK(wait_for(w.local) && w.timed_out.load() && !parent.load());
    }
}

// ---- D6: salt okuma uyum denetimlerinin kosuya baglanmasi (balance_checks.h; sahte ornekleyiciyle) ----

namespace {
struct FakeWindow {
    std::chrono::steady_clock::time_point begin{}, end{};
};
struct FakeSampler {
    int armed = 0, disarmed = 0;
    double last_delay = -1;
    mutable std::vector<FakeWindow> cpu_windows;
    mutable std::optional<FakeWindow> pcie_window;
    mutable std::optional<double> pcie_busy;
    std::optional<dg::PcieLoadReading> link;
    dg::CpuSpeedFinding speed;
    void arm_pcie(double delay_s) { ++armed, last_delay = delay_s; }
    void disarm_pcie() { ++disarmed; }
    dg::CpuSpeedFinding cpu_speed(std::span<const FakeWindow> w) const {
        cpu_windows.assign(w.begin(), w.end());
        return speed;
    }
    std::optional<dg::PcieLoadReading> pcie(const FakeWindow& w, std::optional<double> busy) const {
        pcie_window = w;
        pcie_busy = busy;
        if (!link) return std::nullopt;
        dg::PcieLoadReading r = *link;
        if (busy) r.gpu_load_pct = *busy * 100.0;
        return r;
    }
};
}  // namespace

EF_TEST(balance_checks_arm_pcie_only_during_s3_and_use_cpu_loaded_windows) {
    // C1 pencereleri: CPU yuklu adimlar (S1, K1-K3, T1). GPU kapasite adimlari (S2-S4, T2) CPU'yu yuklemez.
    static_assert(bal::cpu_loaded_step(bal::StepKind::CpuCapacity) && bal::cpu_loaded_step(bal::StepKind::Coupled) &&
                  bal::cpu_loaded_step(bal::StepKind::CpuRepeat));
    static_assert(!bal::cpu_loaded_step(bal::StepKind::GpuCapacity) && !bal::cpu_loaded_step(bal::StepKind::GpuRepeat));
    static_assert(bal::kPcieStep == bal::StepId::S3);
    // PCIe okumasi yalniz S3 baslarken kurulur (isinma + pay sonra), S3 biterken kapatilir.
    FakeSampler fs;
    const bal::StepTiming timing{2.0, 8.0};
    for (const auto id : {bal::StepId::S1, bal::StepId::S2, bal::StepId::S4, bal::StepId::K2, bal::StepId::T2}) {
        bal::notify_check_sampler(fs, bal::StepProgress{id, 1, 9, {}, false, nullptr}, timing);
        bal::notify_check_sampler(fs, bal::StepProgress{id, 1, 9, {}, true, nullptr}, timing);
    }
    EF_CHECK(fs.armed == 0 && fs.disarmed == 0);
    bal::notify_check_sampler(fs, bal::StepProgress{bal::StepId::S3, 3, 9, {}, false, nullptr}, timing);
    EF_CHECK(fs.armed == 1 && fs.disarmed == 0 && close_to(fs.last_delay, 2.0 + bal::kPcieReadDelayPadS));
    bal::notify_check_sampler(fs, bal::StepProgress{bal::StepId::S3, 3, 9, {}, true, nullptr}, timing);
    EF_CHECK(fs.armed == 1 && fs.disarmed == 1);

    // Kosu raporu: butun adimlar olculdu; her adimin kendi olcum penceresi var.
    bal::BalanceReport rep;
    const auto plan = bal::balance_plan(bal::BalanceOptions{}, 8);
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        rep.steps[i] = measured_step(100.0, 0.97);
        rep.steps[i].plan = plan[i];
        rep.steps[i].measure_begin = t0 + std::chrono::seconds(10 * static_cast<int>(i) + 2);
        rep.steps[i].measure_end = t0 + std::chrono::seconds(10 * static_cast<int>(i) + 10);
    }
    rep.retries = 1;
    fs.link = dg::PcieLoadReading{};
    fs.link->cur_width = 8, fs.link->max_width = 16, fs.link->cur_gen = 4, fs.link->max_gen = 4;
    fs.speed.measured = true;
    fs.speed.drop = true;
    auto c = bal::collect_run_checks<FakeWindow>(rep, fs, 3.0, 5u);
    EF_CHECK(fs.cpu_windows.size() == 5);  // S1, K1, K2, K3, T1
    EF_CHECK(fs.cpu_windows.front().begin == rep.step(bal::StepId::S1).measure_begin && fs.cpu_windows.back().end == rep.step(bal::StepId::T1).measure_end);
    EF_CHECK(fs.pcie_window && fs.pcie_window->begin == rep.step(bal::StepId::S3).measure_begin && fs.pcie_window->end == rep.step(bal::StepId::S3).measure_end);
    EF_CHECK(fs.pcie_busy && close_to(*fs.pcie_busy, 0.97));  // yuk kaniti: S3'un GPU mesgul orani
    EF_CHECK(c.pcie && c.pcie->cur_width == 8u && c.pcie->gpu_load_pct && close_to(*c.pcie->gpu_load_pct, 97.0));
    EF_CHECK(c.cpu_speed.drop && c.other_gpu_pct == 3.0 && c.cpu_firmware_limit_events == 5u && c.retries == 1);
    // Kural girdileri: karistirici + notlar; hukmu kural verir.
    dg::BalanceInputs in;
    bal::apply_run_checks(in, c);
    EF_CHECK(in.confounders.other_gpu_pct == 3.0 && in.cpu_speed_drop && in.cpu_firmware_limit_logged);
    c.cpu_firmware_limit_events = 0u;
    c.cpu_speed = dg::CpuSpeedFinding{};
    c.other_gpu_pct.reset();
    bal::apply_run_checks(in, c);
    EF_CHECK(!in.confounders.other_gpu_pct && !in.cpu_speed_drop && !in.cpu_firmware_limit_logged);
    c.cpu_firmware_limit_events.reset();  // gunluk okunamadi: "kayit var" denmez
    bal::apply_run_checks(in, c);
    EF_CHECK(!in.cpu_firmware_limit_logged);

    // S3 olculemediyse (butce / zaman asimi) link "yuk altinda okundu" sayilmaz; olculmeyen CPU adimi pencere vermez.
    rep.steps[static_cast<std::size_t>(bal::StepId::S3)].status = bal::StepStatus::Unmeasurable;
    rep.steps[static_cast<std::size_t>(bal::StepId::K1)].status = bal::StepStatus::TimedOut;
    fs.pcie_window.reset();
    c = bal::collect_run_checks<FakeWindow>(rep, fs, std::nullopt, std::nullopt);
    EF_CHECK(!c.pcie && !fs.pcie_window && fs.cpu_windows.size() == 4 && !c.other_gpu_pct && !c.cpu_firmware_limit_events);
}
