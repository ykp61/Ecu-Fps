#include "benchmark/balance/balance_runner.h"

#include <algorithm>
#include <cstdio>
#include <thread>

#include "benchmark/watchdog.h"

namespace ef::benchmark::balance {

namespace {
using Clock = std::chrono::steady_clock;
namespace dg = core::diagnosis;

struct StepShape {
    StepId id;
    StepKind kind;
    const ArenaCpuProfile* cpu;
    const ArenaGpuProfile* gpu;
    int row;
};
// Sabit sira (A2): S1, S2-S4, K1-K3, T1, T2.
constexpr std::array<StepShape, kStepCount> kShapes{{
    {StepId::S1, StepKind::CpuCapacity, &kArenaCpuC, &kArenaGpuGmin, -1},
    {StepId::S2, StepKind::GpuCapacity, &kArenaCpuCmin, &kArenaGpuG, 0},
    {StepId::S3, StepKind::GpuCapacity, &kArenaCpuCmin, &kArenaGpuG, 1},
    {StepId::S4, StepKind::GpuCapacity, &kArenaCpuCmin, &kArenaGpuG, 2},
    {StepId::K1, StepKind::Coupled, &kArenaCpuC, &kArenaGpuG, 0},
    {StepId::K2, StepKind::Coupled, &kArenaCpuC, &kArenaGpuG, 1},
    {StepId::K3, StepKind::Coupled, &kArenaCpuC, &kArenaGpuG, 2},
    {StepId::T1, StepKind::CpuRepeat, &kArenaCpuC, &kArenaGpuGmin, -1},
    {StepId::T2, StepKind::GpuRepeat, &kArenaCpuCmin, &kArenaGpuG, 0},
}};

double secs_since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

// Renderer hatasi -> adim "olculemedi" nedeni (bos: bu hata olculemedi sayilmaz).
std::string_view unmeasurable_reason(std::string_view error) noexcept {
    if (error == "vram_budget") return dg::balance_reason::kVramBudget;
    if (error == "target_create") return dg::balance_reason::kTargetFailed;
    return {};
}

std::optional<dg::StepRole> role_of(StepKind k) noexcept {
    switch (k) {
        case StepKind::CpuCapacity:
        case StepKind::CpuRepeat: return dg::StepRole::CpuCapacity;
        case StepKind::GpuCapacity:
        case StepKind::GpuRepeat: return dg::StepRole::GpuCapacity;
        case StepKind::Coupled: break;
    }
    return std::nullopt;
}

void fill_measured(StepResult& out, const SceneRunResult& r) {
    out.frame_source = r.frame_source;
    out.warmup_frames = r.warmup_frames;
    out.measured_frames = r.measured_frames;
    out.warmup_s = r.warmup_s;
    out.measure_s = r.measure_s;
    const DurationStats frame = summarize_ms(r.frame_ms);
    out.frame_ms = frame.avg_ms;
    out.gpu_ms = r.gpu_elapsed.avg_ms;
    out.wait_ms = r.wait.avg_ms;
    out.sim_ms = r.sim_step.avg_ms;
    out.cull_ms = r.cull.avg_ms;
    out.submit_ms = r.cpu_submit.avg_ms;
    if (frame.n && frame.avg_ms > 0 && r.wait.n) out.wait_share = std::clamp(r.wait.avg_ms / frame.avg_ms, 0.0, 1.0);
    out.sim_workers = r.sim_workers;
    out.object_draws = r.object_draws_max;
    out.pixel_checksum = r.pixel_checksum;
    out.gpu_timestamps = r.gpu_timestamps;
    out.preview = r.preview;
    out.preview_frames = r.preview_frames;
    out.preview_ms = r.preview_ms;
    out.second_fps = dg::per_second_fps(scored_frame_ms(r));
    out.measure_begin = r.measure_begin;
    out.measure_end = r.measure_end;
}

}  // namespace

std::string_view to_string(StepId id) noexcept {
    switch (id) {
        case StepId::S1: return "S1";
        case StepId::S2: return "S2";
        case StepId::S3: return "S3";
        case StepId::S4: return "S4";
        case StepId::K1: return "K1";
        case StepId::K2: return "K2";
        case StepId::K3: return "K3";
        case StepId::T1: return "T1";
        case StepId::T2: return "T2";
    }
    return "S1";
}

std::string_view to_string(StepKind kind) noexcept {
    switch (kind) {
        case StepKind::CpuCapacity: return "cpu_capacity";
        case StepKind::GpuCapacity: return "gpu_capacity";
        case StepKind::Coupled: return "coupled";
        case StepKind::CpuRepeat: return "cpu_repeat";
        case StepKind::GpuRepeat: return "gpu_repeat";
    }
    return "cpu_capacity";
}

std::string_view to_string(StepStatus s) noexcept {
    switch (s) {
        case StepStatus::NotRun: return "not_run";
        case StepStatus::Measured: return "measured";
        case StepStatus::Unmeasurable: return "unmeasurable";
        case StepStatus::TimedOut: return "timed_out";
        case StepStatus::Cancelled: return "cancelled";
        case StepStatus::Failed: return "failed";
    }
    return "not_run";
}

double planned_step_seconds(const StepTiming& t) noexcept {
    return std::max(0.0, t.warmup_s) + std::max(0.0, t.measure_s) + kMeasurePadS;
}

double step_budget_s(const StepTiming& t) noexcept { return planned_step_seconds(t) + kStepOverheadBudgetS; }

double planned_seconds(const StepTiming& t) noexcept { return static_cast<double>(kStepCount) * planned_step_seconds(t); }

bool step_selected(const BalanceOptions& o, StepId id) noexcept { return !o.only_step || *o.only_step == id; }

StepTiming effective_timing(const BalanceOptions& o) noexcept {
    if (o.timing_override) return *o.timing_override;
    return o.quick ? kQuickTiming : kFullTiming;
}

std::array<StepPlan, kStepCount> balance_plan(const BalanceOptions& o, std::uint32_t logical_cores) noexcept {
    const StepTiming t = effective_timing(o);
    const Resolution cpu_res = o.cpu_resolution_override ? *o.cpu_resolution_override : kCpuStepResolution;
    const auto& rows = o.row_resolution_override ? *o.row_resolution_override : kRowResolutions;
    std::array<StepPlan, kStepCount> plan;
    for (std::size_t i = 0; i < kStepCount; ++i) {
        const StepShape& s = kShapes[i];
        const Resolution res = s.row < 0 ? cpu_res : rows[static_cast<std::size_t>(s.row)];
        StepPlan& p = plan[i];
        p.id = s.id;
        p.kind = s.kind;
        p.cpu = s.cpu;
        p.gpu = s.gpu;
        p.row = s.row;
        p.config = arena_config(*s.cpu, *s.gpu, res.width, res.height, logical_cores ? logical_cores : 1u);
        p.config.seed = o.seed;
        p.config.warmup_s = t.warmup_s;
        p.config.measure_s = t.measure_s + kMeasurePadS;  // yalniz tam saniyeler kullanilir (bkz. baslik)
    }
    return plan;
}

std::string plan_fingerprint(const BalanceOptions& o, std::uint32_t logical_cores) {
    const StepTiming t = effective_timing(o);
    std::uint64_t h = 1469598103934665603ull;
    for (const StepPlan& p : balance_plan(o, logical_cores)) {
        const std::uint64_t w = arena_workload_hash(p.config);
        for (int k = 0; k < 8; ++k) h = (h ^ ((w >> (8 * k)) & 0xFFu)) * 1099511628211ull;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s|%s|%s|w%.2f+m%.2f|%016llx", std::string(kArenaVersion).c_str(), std::string(kFrameSimVersion).c_str(),
                  o.timing_override ? "custom" : (o.quick ? "quick" : "full"), t.warmup_s, t.measure_s, static_cast<unsigned long long>(h));
    return buf;
}

bool needs_gpu_time(StepKind kind) noexcept { return kind != StepKind::CpuCapacity && kind != StepKind::CpuRepeat; }

dg::StepStats step_stats_from(const SceneRunResult& r, double measure_s, bool gpu_time_needed) {
    if (r.ok) return dg::summarize_step(scored_frame_ms(r), r.gpu_elapsed_ms, measure_s, r.gpu_excluded, gpu_time_needed);
    if (const auto reason = unmeasurable_reason(r.error); !reason.empty()) return dg::unmeasurable_step(reason);
    dg::StepStats s;
    if (r.error == "no_measured_frames") {  // pencere acildi ama aralik olusmadi: gecersiz olcum (sahte 0 FPS degil)
        s.state = dg::StepState::Invalid;
        s.reason = dg::balance_reason::kTooFewFrames;
    }
    return s;  // diger durumlar: NotRun (kosu zaten hata ile biter)
}

dg::BalanceInputs build_inputs(const std::array<StepResult, kStepCount>& steps) {
    dg::BalanceInputs in;
    const auto at = [&](StepId id) -> const StepResult& { return steps[static_cast<std::size_t>(id)]; };
    in.cpu = at(StepId::S1).stats;
    in.gpu = {at(StepId::S2).stats, at(StepId::S3).stats, at(StepId::S4).stats};
    in.coupled = {at(StepId::K1).stats, at(StepId::K2).stats, at(StepId::K3).stats};
    in.cpu_repeat = at(StepId::T1).stats;
    in.gpu_repeat = at(StepId::T2).stats;
    const StepResult& s1 = at(StepId::S1);
    if (s1.status == StepStatus::Measured && s1.stats.state == dg::StepState::Valid)
        in.cpu_split = dg::CpuFrameSplit{s1.sim_ms, s1.cull_ms, s1.submit_ms};
    return in;
}

BalanceReport run_balance(const BalanceOptions& o, const std::atomic<bool>& cancel) {
    BalanceReport rep;
    const auto t_run = Clock::now();
    const std::uint32_t cores = o.logical_cores ? o.logical_cores : std::max(1u, std::thread::hardware_concurrency());
    rep.quick = o.quick;
    rep.timing = effective_timing(o);
    rep.logical_cores = cores;
    rep.fingerprint = plan_fingerprint(o, cores);
    const auto plan = balance_plan(o, cores);
    for (std::size_t i = 0; i < kStepCount; ++i) {
        rep.steps[i].plan = plan[i];
        rep.steps[i].workload_hash = arena_workload_hash(plan[i].config);
    }
    const auto finish = [&]() -> BalanceReport& {
        if (o.frame_tap) {
            o.frame_tap->begin_step(0, 0, 0, 0, 0);
            o.frame_tap->set_phase(TapPhase::Idle);
        }
        rep.inputs = build_inputs(rep.steps);
        rep.seconds = secs_since(t_run);
        return rep;
    };
    if (cancel.load(std::memory_order_relaxed)) {
        rep.cancelled = true;
        return finish();
    }

    ArenaSession session;
    if (!session.open(plan[0].config, o.prefer_hardware, o.hardware_adapter_index)) {
        rep.adapter = session.adapter();
        rep.error = session.error().empty() ? "open_failed" : session.error();
        return finish();
    }
    rep.adapter = session.adapter();
    rep.software = rep.adapter.kind != RendererKind::Hardware;
    if (rep.software && !o.allow_software) {  // WARP puanlanmaz: denge sonucu uretilmez
        rep.error = "no_hardware_adapter";
        return finish();
    }

    if (o.before_steps) o.before_steps(rep.adapter, cancel);
    const double budget_s = step_budget_s(rep.timing);
    std::array<bool, dg::kBalanceRows> row_timed_out{};
    std::size_t i = 0;
    for (; i < kStepCount; ++i) {
        StepResult& sr = rep.steps[i];
        const StepPlan& p = sr.plan;
        const Resolution res{p.config.width, p.config.height};
        const auto index = static_cast<std::uint32_t>(i + 1), total = static_cast<std::uint32_t>(kStepCount);
        if (cancel.load(std::memory_order_relaxed)) {
            rep.cancelled = true;
            break;
        }
        if (!step_selected(o, p.id)) continue;  // P1: tek adim kipi; digerleri NotRun
        // Ayni cozunurlugun onceki adimi butceyi astiysa bu adim da asar: kosulmaz, satir "olculemedi" kalir.
        if (p.row >= 0 && row_timed_out[static_cast<std::size_t>(p.row)]) {
            sr.status = StepStatus::NotRun;
            sr.error = "timeout";
            sr.stats = dg::unmeasurable_step(dg::balance_reason::kTimeout);
            continue;
        }
        if (o.frame_tap) o.frame_tap->begin_step(index, total, static_cast<std::uint32_t>(p.id) + 1u, res.width, res.height);
        if (o.progress) o.progress(StepProgress{p.id, index, total, res, false, nullptr});

        SceneConfig c = p.config;
        c.frame_tap = o.frame_tap;
        c.preview = o.preview;
        session.limit_vram_budget(o.vram_limit_row < 0 || o.vram_limit_row == p.row ? o.vram_budget_limit : 0);
        const auto t_step = Clock::now();
        SceneRunResult r;
        bool timed_out = false;
        bool r_minimized = false;
        const bool budget_override = o.step_budget_override_s && (o.step_budget_row < 0 || o.step_budget_row == p.row);
        for (;;) {
            {
                Watchdog w(cancel, budget_override ? *o.step_budget_override_s : budget_s);  // DENEME basina zaman butcesi; ust iptali de tasir
                r = session.run_step(c, w.local);
                timed_out = w.timed_out.load();
            }
            ++sr.attempts;
            // Uyumluluk B3: olcum penceresinde pencere simge durumundaysa deneme gecersizdir (sonuc uydurulmaz).
            r_minimized = r.ok && o.window_minimized_during && r.measure_end > r.measure_begin &&
                          o.window_minimized_during(r.measure_begin, r.measure_end);
            // Kararsiz adim icin sinirli tekrar: yalniz olculmus (ok) ve kural geregi "unstable" cikan adim. Son deneme
            // oldugu gibi kullanilir; iptal / zaman asimi / hata olan deneme tekrar edilmez.
            if (!r.ok || r.cancelled || cancel.load(std::memory_order_relaxed)) break;
            const dg::StepStats attempt = dg::flag_window_minimized(step_stats_from(r, rep.timing.measure_s, needs_gpu_time(p.kind)), r_minimized);
            const bool retry = dg::should_retry_step(attempt, sr.attempts, rep.retries) ||
                               (o.retry_any_invalid && attempt.state == dg::StepState::Invalid && sr.attempts < dg::kBalanceStepMaxAttempts &&
                                rep.retries < dg::kBalanceRunMaxRetries);
            if (!retry) break;
            ++rep.retries;
            if (o.frame_tap) o.frame_tap->begin_step(index, total, static_cast<std::uint32_t>(p.id) + 1u, res.width, res.height);
        }
        sr.seconds = secs_since(t_step);
        sr.error = r.error;
        bool stop = false;
        if (r.cancelled) {
            // Kullanici iptali zaman asimindan onceliklidir (ikisi birden olabilir): kismi adim atilir.
            if (cancel.load(std::memory_order_relaxed) || !timed_out) {
                sr.status = StepStatus::Cancelled;
                rep.cancelled = true;
                stop = true;
            } else {
                sr.status = StepStatus::TimedOut;
                sr.error = "timeout";
                sr.stats = dg::unmeasurable_step(dg::balance_reason::kTimeout);
                if (p.row >= 0) {
                    row_timed_out[static_cast<std::size_t>(p.row)] = true;
                } else if (p.kind == StepKind::CpuCapacity) {
                    // S1 en hafif GPU yukudur; o bile butceye sigmadiysa hicbir satir hukum alamaz ve kalan adimlar
                    // da asar: kullaniciyi dakikalarca bekletmek yerine kosu burada biter.
                    rep.error = "timeout";
                    stop = true;
                }
            }
        } else if (r.ok) {
            sr.status = StepStatus::Measured;
            sr.stats = dg::flag_window_minimized(step_stats_from(r, rep.timing.measure_s, needs_gpu_time(p.kind)), r_minimized);
            fill_measured(sr, r);
            if (o.keep_scored_frames) sr.scored_ms = scored_frame_ms(r);
            if (const auto role = role_of(p.kind)) sr.bound_confirmed = dg::bound_confirmed(*role, sr.stats.gpu_busy_ratio);
            rep.preview = rep.preview || r.preview;
        } else if (!unmeasurable_reason(r.error).empty()) {
            sr.status = StepStatus::Unmeasurable;  // butce / hedef: diger adimlar surer
            sr.stats = step_stats_from(r, rep.timing.measure_s, needs_gpu_time(p.kind));
        } else if (r.error == "no_measured_frames") {
            sr.status = StepStatus::Measured;  // kosuldu ama aralik olusmadi: gecersiz olcum
            sr.stats = step_stats_from(r, rep.timing.measure_s, needs_gpu_time(p.kind));
            sr.warmup_frames = r.warmup_frames;
            sr.measured_frames = r.measured_frames;
        } else {
            // Cihaz kaybi, GPU zaman asimi, simulasyon saglamasi, kaynak hatasi: yarim adim atilir, kosu biter.
            sr.status = StepStatus::Failed;
            rep.error = r.error.empty() ? "step_failed" : r.error;
            stop = true;
        }
        if (o.progress) o.progress(StepProgress{p.id, index, total, res, true, &sr});
        if (stop) break;
    }
    rep.ok = i == kStepCount;  // dongu iptal / hata ile kesilmedi
    return finish();
}

}  // namespace ef::benchmark::balance
