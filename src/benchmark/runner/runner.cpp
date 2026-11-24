#include "benchmark/runner/runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "benchmark/cpu/cpu_verify.h"
#include "benchmark/cpu/cpu_workload.h"
#include "benchmark/memory/memory_bench.h"
#include "benchmark/scene/d3d_renderer.h"
#include "benchmark/scene/gpu_verify.h"
#include "benchmark/storage/storage_test.h"
#include "benchmark/watchdog.h"
#include "core/measure/bench_cards.h"
#include "core/measure/frame_stats.h"
#include "core/measure/verify_state.h"

namespace ef::benchmark {

std::string_view to_string(SubtestStatus s) noexcept {
    switch (s) {
        case SubtestStatus::Completed: return "completed";
        case SubtestStatus::Partial: return "partial";
        case SubtestStatus::Skipped: return "skipped";
        case SubtestStatus::Unavailable: return "unavailable";
        case SubtestStatus::Cancelled: return "cancelled";
        case SubtestStatus::Failed: return "failed";
    }
    return "failed";
}

Tier select_tier(double low_ms, double target_ms) noexcept {
    if (!(low_ms > 0) || !(target_ms > 0)) return Tier::Medium;
    // Kare suresi kabaca ornek sayisiyla olceklenir (secim icin yeterli; skor degil).
    const double k_med = static_cast<double>(instance_count(Tier::Medium)) / instance_count(Tier::Low);
    const double k_high = static_cast<double>(instance_count(Tier::High)) / instance_count(Tier::Low);
    if (low_ms * k_high <= target_ms) return Tier::High;
    if (low_ms * k_med <= target_ms) return Tier::Medium;
    return Tier::Low;
}

std::string LockedPlan::fingerprint() const {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s|%s|%llx|%ux%u|%u|w%u|cpu%.2f|%s|%s|%s|%s", std::string(kSceneVersion).c_str(),
                  std::string(to_string(scene.tier)).c_str(), static_cast<unsigned long long>(scene.seed), scene.width, scene.height,
                  scene.frames, warmup_frames, cpu_seconds_per_variant, std::string(cpu::kWorkloadVersion).c_str(),
                  memory::kMemBenchVersion, storage_volume.empty() ? "nostorage" : "storage", full ? "full" : "quick");
    return buf;
}

namespace {
using Clock = std::chrono::steady_clock;

// Alt test bekcisi: benchmark/watchdog.h (denge testi kosucusuyla ortak).

void add_stats_metrics(SubtestResult& r, const core::measure::FrameStats& st, const char* prefix, bool with_max = false) {
    if (!st.sufficient) return;
    r.metrics.push_back({std::string(prefix) + "avg_fps", *st.avg_fps});
    r.metrics.push_back({std::string(prefix) + "low1_fps", *st.low1_fps});
    r.metrics.push_back({std::string(prefix) + "p99_ms", *st.p99_ms});
    // Spec 2026-11-12 C (Sonuc karti "en kotu kare"): olculen en yavas gecerli kare; yalniz istenen seride.
    if (with_max && std::isfinite(st.max_ms) && st.max_ms > 0) r.metrics.push_back({std::string(prefix) + "max_ms", st.max_ms});
}

void add_frame_metrics(SubtestResult& r, const std::vector<double>& frame_ms, const char* prefix, bool with_max = false) {
    add_stats_metrics(r, core::measure::compute_frame_stats(frame_ms), prefix, with_max);
}

// FPS / %1 dusuk hangi seriden: 1 = GPU tamamlama araligi, 0 = gonderim araligi (yedek). Dislanan kare
// sayisi ve pacing kapsami da yazilir (kapsam bilinmiyorsa yazilmaz).
void add_source_metrics(SubtestResult& r, FrameSource src, std::uint32_t excluded, std::optional<double> coverage, const char* prefix) {
    r.metrics.push_back({std::string(prefix) + "frame_source_gpu", src == FrameSource::GpuCompletion ? 1.0 : 0.0});
    r.metrics.push_back({std::string(prefix) + "gpu_excluded", static_cast<double>(excluded)});
    if (coverage) r.metrics.push_back({std::string(prefix) + "gpu_pacing_coverage", *coverage});
}

double secs(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

}  // namespace

namespace {
using core::measure::VerifyCode;
void push_state(SubtestResult& r, std::string_view key, VerifyCode c) {
    r.metrics.push_back({std::string(key), static_cast<double>(static_cast<int>(c))});
}
std::optional<VerifyCode> gpu_code(gpu_verify::Status s) noexcept {
    using S = gpu_verify::Status;
    switch (s) {
        case S::NotRun: return std::nullopt;
        case S::Ok: return VerifyCode::Passed;
        case S::Mismatch: return VerifyCode::Mismatch;
        case S::Unsupported: return VerifyCode::Unsupported;
        case S::SetupFailed: return VerifyCode::SetupFailed;
        case S::DeviceRemoved: return VerifyCode::DeviceRemoved;
        case S::GpuTimeout: return VerifyCode::Timeout;
        case S::ReadbackFailed: return VerifyCode::ReadbackFailed;
        case S::Cancelled: return VerifyCode::Cancelled;
        case S::TimeLimit: return VerifyCode::Incomplete;
        case S::Invalid: return VerifyCode::Invalid;
    }
    return std::nullopt;
}
}  // namespace

void apply_gpu_verify(SubtestResult& r, BenchmarkReport& rep, const gpu_verify::Result& v, bool timed_out) {
    const auto vd = gpu_verify::verdict(v.status);
    const auto code = gpu_code(v.status);
    auto drop_score = [&] {
        r.metrics.clear();
        rep.scene_fps_series.clear();
        rep.scene_measured = false;
        rep.scene_measure_begin = rep.scene_measure_end = {};
    };
    switch (vd.effect) {
        case gpu_verify::Effect::Pass: break;
        case gpu_verify::Effect::NoClaim:
            // Skor kalir, sayac yazilmaz ("dogrulandi" denmez); ama "desteklenmiyor / kurulamadi" durumu gecmise
            // yazilir: dogrulamanin hic kosulmadigi kayittan (anahtar yok) ayrilir.
            if (code) push_state(r, core::measure::kGpuVerifyStateMetric, *code);
            return;
        case gpu_verify::Effect::Partial:
            r.status = SubtestStatus::Partial;
            r.reason = std::string(vd.reason);
            break;
        case gpu_verify::Effect::FailNoScore:
            drop_score();
            r.status = SubtestStatus::Failed;
            r.reason = std::string(vd.reason);
            break;
        case gpu_verify::Effect::Cancelled:
            drop_score();
            if (timed_out) r.status = SubtestStatus::Partial, r.reason = "timeout";
            else r.status = SubtestStatus::Cancelled, r.reason = "user_cancel";
            return;
    }
    // Ham sayilar gecmise (karta satir eklenmez). Pass, Partial ve uyusmazlikta yazilir.
    if (v.repeats_done || v.mismatched_words) {
        r.metrics.push_back({"scene_gpu_verify_repeats", static_cast<double>(v.repeats_done)});
        r.metrics.push_back({"scene_gpu_verify_words", static_cast<double>(v.words)});
        r.metrics.push_back({"scene_gpu_verify_mismatch_words", static_cast<double>(v.mismatched_words)});
    }
    if (code) push_state(r, core::measure::kGpuVerifyStateMetric, *code);
}

std::optional<core::measure::VerifyCode> storage_verify_code(storage::VerifyStatus s, std::string_view reason) noexcept {
    using S = storage::VerifyStatus;
    switch (s) {
        case S::NotRun: break;
        case S::Ok: return VerifyCode::Passed;
        case S::Mismatch: return VerifyCode::Mismatch;
        case S::IoError: return VerifyCode::IoError;
        case S::Cancelled: return VerifyCode::Cancelled;
        case S::TimeLimit: return VerifyCode::Incomplete;
        case S::Invalid: return VerifyCode::Invalid;
    }
    // Dogrulama tamponu/aygiti kurulamadi: faz baslamadi ama "kosulmadi" degil.
    if (reason == "storage_verify_setup_failed") return VerifyCode::SetupFailed;
    return std::nullopt;
}

void apply_cpu_verify(SubtestResult& r, const cpu_verify::Result& v, bool timed_out) {
    using S = cpu_verify::Status;
    if (v.status == S::NotRun) return;
    if (v.status == S::Cancelled) {
        // Iptal: kismi dogrulama "gecti" sayilmaz; CPU skoru da yarim kosu gibi silinir.
        r.metrics.clear();
        if (timed_out) r.status = SubtestStatus::Partial, r.reason = "timeout";
        else r.status = SubtestStatus::Cancelled, r.reason = "user_cancel";
        return;
    }
    VerifyCode code = VerifyCode::Passed;
    switch (v.status) {
        case S::Ok: break;
        case S::Mismatch:
            // T01: hesap tutarsizligi -> CPU skoru verilmez (kesin donanim karari degil).
            r.metrics.clear();
            r.status = SubtestStatus::Failed;
            r.reason = "cpu_compute_mismatch";
            code = VerifyCode::Mismatch;
            break;
        case S::Incomplete:
            r.status = SubtestStatus::Partial;
            r.reason = "cpu_verify_incomplete";
            code = VerifyCode::Incomplete;
            break;
        case S::Invalid:
            r.metrics.clear();
            r.status = SubtestStatus::Failed;
            r.reason = "cpu_verify_invalid";
            code = VerifyCode::Invalid;
            break;
        default: break;
    }
    // Ham sayilar gecmise (gercek sayaclar; desteklenmeyen varyantin blok sayisi yazilmaz).
    r.metrics.push_back({"cpu_verify_blocks", static_cast<double>(v.blocks)});
    r.metrics.push_back({"cpu_verify_mismatch_blocks", static_cast<double>(v.mismatched_blocks)});
    r.metrics.push_back({"cpu_verify_threads", static_cast<double>(v.threads)});
    for (const auto& i : v.isas) {
        const std::string pre = "cpu_verify_" + std::string(cpu_verify::isa_name(i.isa));
        r.metrics.push_back({pre + "_supported", i.supported ? 1.0 : 0.0});
        if (i.supported) {
            r.metrics.push_back({pre + "_blocks", static_cast<double>(i.blocks)});
            r.metrics.push_back({pre + "_mismatch_blocks", static_cast<double>(i.mismatched_blocks)});
        }
    }
    push_state(r, core::measure::kCpuVerifyStateMetric, code);
}

SceneConfig scene_run_config(const LockedPlan& plan) {
    SceneConfig c = plan.scene;
    if (!duration_mode(c)) c.warmup_frames = std::max(c.warmup_frames, plan.warmup_frames);
    return c;
}

LockedPlan make_plan(const RunnerOptions& o, const std::atomic<bool>& cancel) {
    LockedPlan p;
    p.storage_volume = o.storage_volume;
    p.full = o.full;
    // Plan v3.5: tier gercek agir sahnede kalibre edilir (Low -> Medium -> High, 33 ms butcesini asan ilk
    // tier'da durur) ve A/B boyunca kilitli kalir. Olcum sure tabanli: Hizli 3+10 sn, Tam 10+60 sn.
    p.scene.warmup_s = o.full ? 10.0 : 3.0;
    p.scene.measure_s = o.full ? 60.0 : 10.0;
    p.warmup_frames = 0;  // isinma ayni cihazda warmup_s ile (ayri kosu yok)
    p.cpu_seconds_per_variant = 0;  // on ayarlar (quick/full) kullanilir
    const auto cal = calibrate_scene(p.scene, CalibrationOptions{}, cancel);
    if (cal.ok) p.scene.tier = cal.tier;
    else p.scene.tier = Tier::Low;  // kalibrasyon yok: en hafif (zayif cihaz/TDR guvenligi)
    (void)o.target_frame_ms;
    return p;
}

BenchmarkReport run_benchmark(const LockedPlan& plan, const RunnerOptions& o, std::atomic<bool>& cancel) {
    BenchmarkReport rep;
    rep.plan = plan;
    rep.plan_fingerprint = plan.fingerprint();
    const int stages = 5;
    int stage = 0;
    auto progress = [&](const char* s) {
        if (o.progress) o.progress(s, static_cast<double>(stage) / stages);
        ++stage;
    };
    auto finish_cancel = [&](SubtestResult& r, const Watchdog& w) {
        if (w.timed_out) r.status = SubtestStatus::Partial, r.reason = "timeout";
        else r.status = SubtestStatus::Cancelled, r.reason = "user_cancel";
    };

    // 1) Sahne (GPU): isinma atilir, kilitli kosu skorlanir.
    {
        progress("scene");
        SubtestResult r{"scene"};
        const auto t0 = Clock::now();
        Watchdog w(cancel, o.subtest_timeout_s);
        // Isinma ayni cihazda ayni kosunun icinde (sure kipinde warmup_s, kare kipinde warmup_frames).
        auto scene_cfg = scene_run_config(plan);
        scene_cfg.frame_tap = o.frame_tap;  // EF-X15: nullptr ise davranis aynen
        scene_cfg.preview = o.preview;      // A4: nullptr ise davranis aynen
        const auto sr = run_scene(scene_cfg, w.local);
        if (sr.cancelled) finish_cancel(r, w);
        else if (!sr.ok)
            r.status = sr.adapter.kind == RendererKind::None ? SubtestStatus::Unavailable : SubtestStatus::Failed, r.reason = sr.error;
        else if (sr.adapter.kind != RendererKind::Hardware) r.status = SubtestStatus::Unavailable, r.reason = "software_renderer";
        else {
            r.status = SubtestStatus::Completed;
            const auto& scored = scored_frame_ms(sr);
            // EF-X19.02: metrikler ve grafik serisi ayni ham `scored` kare surelerinden (seri yalniz cizim).
            const auto charted = core::measure::frame_stats_with_series(scored);
            add_stats_metrics(r, charted.stats, "scene_", true);  // GPU tamamlanma temposu (gecerliyse)
            rep.scene_fps_series = charted.fps_series;
            add_source_metrics(r, sr.frame_source, sr.gpu_excluded, sr.gpu_pacing_coverage, "scene_");
            // DIAG-10: kalibrasyon ornegi icin gecerli aralik sayisi (uydurulmus kare sayisi degil).
            r.metrics.push_back({"scene_frames", static_cast<double>(scored.size())});
            // Kare basi GPU gecen sure (baska sureclerin isi dahil; kullanim degil).
            if (sr.gpu_elapsed.n) r.metrics.push_back({"scene_gpu_elapsed_avg_ms", sr.gpu_elapsed.avg_ms});
            // DIAG-10: gpu_busy_ms yalniz GPU zaman damgasi gercekten okunduysa ve yeterli gecerli kare
            // varsa yazilir. Deger kare basina ilk-son komut araligi: kendi isimizin UST SINIRI (araya
            // giren yabanci GPU isi dahil); kullanim yuzdesi degildir. Olcum yoksa alan yok (0 yazilmaz).
            if (sr.gpu_timestamps && sr.gpu_elapsed.n >= core::measure::kMinIntervals)
                r.metrics.push_back({"scene_gpu_busy_ms", sr.gpu_elapsed.avg_ms});
            r.metrics.push_back({"scene_prepare_ms", sr.prepare_ms});
            rep.scene_measure_begin = sr.measure_begin;
            rep.scene_measure_end = sr.measure_end;
            rep.scene_measured = true;
            // EF-X22: olcum penceresinden SONRA kucuk deterministik compute/geri okuma dogrulamasi (skor
            // penceresine girmez). Uyusmazlik/cihaz hatasi sahne skorunu siler; dogrulama yapilamazsa skor
            // kalir ama dogrulama metrigi yazilmaz ("dogrulandi" denmez).
            if (o.gpu_verify) {
                const auto vc = plan.full ? gpu_verify::full_config() : gpu_verify::quick_config();
                apply_gpu_verify(r, rep, gpu_verify::run(vc, w.local), w.timed_out.load());
            }
        }
        r.seconds = secs(t0);
        rep.subtests.push_back(r);
    }
    // 2) CPU
    if (!cancel.load()) {
        progress("cpu");
        SubtestResult r{"cpu"};
        const auto t0 = Clock::now();
        Watchdog w(cancel, o.subtest_timeout_s);
        cpu::CpuConfig cc = plan.full ? cpu::full_cpu_config() : cpu::quick_cpu_config();
        if (plan.cpu_seconds_per_variant > 0) cc.time_limit_s = plan.cpu_seconds_per_variant, cc.st_seconds = cc.mt_seconds = 0;
        const auto cr = cpu::run_cpu_benchmark(cc, w.local);
        switch (cr.status) {
            case cpu::CpuRunStatus::Ok:
                r.status = SubtestStatus::Completed;
                r.metrics = {{"cpu_st_units_per_s", cr.single_thread.work_units_per_second},
                             {"cpu_mt_units_per_s", cr.multi_thread.work_units_per_second},
                             {"cpu_threads", static_cast<double>(cr.hardware_threads)},
                             // Sayim yontemi kaydi: cpu_threads 0.6.0 sonrasindan beri tum islemci gruplarini sayar (> 64 CPU
                             // makinede eski kayitlardan farkli olabilir); surecin kendi grubu ayrica yazilir. Tek
                             // gruplu makinede iki deger esittir.
                             {"cpu_threads_process_group", static_cast<double>(std::max(1u, std::thread::hardware_concurrency()))}};
                // Cekirdek bazinda kirilim (birim/sn; FPS degil).
                for (const auto* v : {&cr.single_thread, &cr.multi_thread})
                    for (const auto& k : v->kernels)
                        r.metrics.push_back({std::string(v == &cr.single_thread ? "cpu_st_" : "cpu_mt_") +
                                                 std::string(cpu::kernel_name(k.kernel)) + "_units_per_s",
                                             k.units_per_second});
                // Cekisme uc durumlu: *_contention_known her zaman; contended/min pay yalniz biliniyorsa
                // (bilinmeyen pay "cekismesiz" yazilmaz). bigdata RAM butcesiyle kisildiysa skor varsayilanla
                // karsilastirilamaz (isaret + etkin eleman sayisi).
                for (const auto* v : {&cr.single_thread, &cr.multi_thread}) {
                    const std::string pre = v == &cr.single_thread ? "cpu_st_" : "cpu_mt_";
                    const auto c = cpu::contention(*v);
                    r.metrics.push_back({pre + "contention_known", c == cpu::Contention::Unknown ? 0.0 : 1.0});
                    if (c != cpu::Contention::Unknown) {
                        r.metrics.push_back({pre + "contended", c == cpu::Contention::Contended ? 1.0 : 0.0});
                        r.metrics.push_back({pre + "min_thread_cpu_share", *v->min_thread_cpu_share});
                    }
                }
                // Varyant bazinda: is yuku RAM butcesiyle varsayilandan farkliysa (isci sayisi veya eleman sayisi)
                // kart notu varyanta dusulur. cpu_bigdata_memory_limited eski kayitlarla uyum icin (ikisinin VEYA'si).
                r.metrics.push_back({"cpu_st_memory_limited", cr.single_thread.memory_limited ? 1.0 : 0.0});
                r.metrics.push_back({"cpu_mt_memory_limited", cr.multi_thread.memory_limited ? 1.0 : 0.0});
                r.metrics.push_back({"cpu_bigdata_memory_limited",
                                     cr.single_thread.memory_limited || cr.multi_thread.memory_limited ? 1.0 : 0.0});
                if (cr.multi_thread.bigdata_elements) {
                    // Eleman sayisi ST ve MT'de insa geregi aynidir. Butce olculemediyse bilinmiyor diye yazilir
                    // (kucultme yapilmadi; MT bigdata tek isciyle kostu).
                    r.metrics.push_back({"cpu_bigdata_elements", static_cast<double>(cr.multi_thread.bigdata_elements)});
                    r.metrics.push_back({"cpu_bigdata_elements_reduced", cr.multi_thread.bigdata_elements_reduced ? 1.0 : 0.0});
                    r.metrics.push_back({"cpu_bigdata_budget_known", cr.multi_thread.bigdata_budget_known ? 1.0 : 0.0});
                }
                break;
            case cpu::CpuRunStatus::Cancelled: finish_cancel(r, w); break;
            case cpu::CpuRunStatus::ChecksumMismatch: r.status = SubtestStatus::Failed, r.reason = "checksum_mismatch"; break;
            case cpu::CpuRunStatus::NoWork: r.status = SubtestStatus::Failed, r.reason = "no_work"; break;
        }
        // EF-X22: olcumden SONRA degisken yuk + SIMD varyant dogrulamasi (skor penceresine girmez; sahne coktan
        // bitti, FPS etkilenmez). Uyusmazlik CPU skorunu siler; desteklenmeyen varyant basarisizlik sayilmaz.
        if (o.cpu_verify && r.status == SubtestStatus::Completed && !w.local.load()) {
            const auto vc = plan.full ? cpu_verify::full_config() : cpu_verify::quick_config();
            apply_cpu_verify(r, cpu_verify::run(vc, w.local), w.timed_out.load());
        }
        r.seconds = secs(t0);
        rep.subtests.push_back(r);
    }
    // 3) RAM
    if (!cancel.load()) {
        progress("memory");
        SubtestResult r{"memory"};
        const auto t0 = Clock::now();
        Watchdog w(cancel, o.subtest_timeout_s);
        const auto mr = memory::run_memory_bench(plan.full ? memory::full_memory_config() : memory::quick_memory_config(), w.local);
        switch (mr.status) {
            case memory::MemStatus::Completed:
                r.status = SubtestStatus::Completed;
                for (const auto& b : mr.buffers) {
                    const auto kib = std::to_string(b.buffer_bytes >> 10) + "kib";
                    // Olculemeyen deger metrige yazilmaz (0 degil, yok).
                    if (b.seq_read_gbps) r.metrics.push_back({"mem_" + kib + "_read_gbps", *b.seq_read_gbps});
                    if (b.seq_write_gbps) r.metrics.push_back({"mem_" + kib + "_write_gbps", *b.seq_write_gbps});
                    if (b.chase_ns) r.metrics.push_back({"mem_" + kib + "_chase_ns", *b.chase_ns});
                }
                if (mr.mt.read_gbps) r.metrics.push_back({"mem_mt_read_gbps", *mr.mt.read_gbps});
                if (mr.mt.write_gbps) r.metrics.push_back({"mem_mt_write_gbps", *mr.mt.write_gbps});
                if (mr.mt.copy_gbps) r.metrics.push_back({"mem_mt_copy_gbps", *mr.mt.copy_gbps});
                // MT degeri hic yoksa is parcacigi sayisi da yazilmaz. Oran < 8 ise mem_mt_* "buyuk tampon"
                // olcumudur, DRAM degil (mem_mt_buffer_l3_ratio ile okunur; L3 bilinmiyorsa oran yazilmaz).
                if (mr.mt.read_gbps || mr.mt.write_gbps || mr.mt.copy_gbps) {
                    r.metrics.push_back({"mem_mt_threads", static_cast<double>(mr.mt.threads)});
                    if (mr.mt.buffer_to_l3) r.metrics.push_back({"mem_mt_buffer_l3_ratio", *mr.mt.buffer_to_l3});
                    // BM-03: MT cekismesi uc durumlu: bilinmeyen pay "cekismesiz" yazilmaz. Pay olculduyse
                    // isaret + en dusuk is parcacigi CPU payi sonuc guvenine ve karta tasinir.
                    r.metrics.push_back({"mem_mt_contention_known", mr.mt.min_thread_cpu_share ? 1.0 : 0.0});
                    if (mr.mt.min_thread_cpu_share) {
                        r.metrics.push_back({"mem_mt_contended", mr.mt.contended ? 1.0 : 0.0});
                        r.metrics.push_back({"mem_mt_min_thread_cpu_share", *mr.mt.min_thread_cpu_share});
                    }
                }
                if (mr.latency_ns) r.metrics.push_back({"mem_latency_ns", *mr.latency_ns});
                // mem_latency_ns ve mem_mt_* ayni buyuk tamponda olculur; MT olmasa da tampon turu acik yazilir:
                // mem_large_buffer_l3_known her zaman; L3 biliniyorsa mem_large_buffer_dram 1 = DRAM (>= 8*L3),
                // 0 = DRAM degil ve oran. L3 bilinmiyorsa DRAM/onbellek iddiasi yazilmaz (veri yetersiz).
                if (mr.latency_ns || mr.mt.read_gbps || mr.mt.write_gbps || mr.mt.copy_gbps) {
                    r.metrics.push_back({"mem_large_buffer_l3_known", mr.large_buffer_to_l3 ? 1.0 : 0.0});
                    if (mr.large_buffer_to_l3) {
                        r.metrics.push_back({"mem_large_buffer_dram", mr.large_buffer_is_dram ? 1.0 : 0.0});
                        r.metrics.push_back({"mem_large_buffer_l3_ratio", *mr.large_buffer_to_l3});
                    }
                }
                break;
            case memory::MemStatus::Cancelled: finish_cancel(r, w); break;
            case memory::MemStatus::Skipped: r.status = SubtestStatus::Skipped, r.reason = mr.reason; break;
            case memory::MemStatus::Failed: r.status = SubtestStatus::Failed, r.reason = mr.reason; break;
        }
        r.seconds = secs(t0);
        rep.subtests.push_back(r);
    }
    // 4) Depolama (yalniz kullanici birim sectiyse)
    if (!cancel.load()) {
        progress("storage");
        SubtestResult r{"storage"};
        const auto t0 = Clock::now();
        if (plan.storage_volume.empty()) {
            r.status = SubtestStatus::Skipped;
            r.reason = "no_volume_selected";
        } else {
            Watchdog w(cancel, o.subtest_timeout_s);
            storage::RunConfig sc;
            sc.volume_root = plan.storage_volume;
            sc.limits = plan.full ? storage::full_limits() : storage::quick_limits();
            const auto res = storage::run_storage_test(sc, w.local);
            switch (res.status) {
                case storage::Status::Ok: r.status = SubtestStatus::Completed; break;
                case storage::Status::Partial: r.status = SubtestStatus::Partial; break;
                case storage::Status::Skipped: r.status = SubtestStatus::Skipped; break;
                case storage::Status::Cancelled: finish_cancel(r, w); break;
                case storage::Status::Refused:
                case storage::Status::Failed: r.status = SubtestStatus::Failed; break;
            }
            if (r.reason.empty()) r.reason = res.reason;
            // Bosluk 7: bos alan yuzunden atlandiysa gereken ve mevcut bos alan (GetDiskFreeSpaceExW) kartta gosterilir.
            for (auto& kv : core::measure::storage_free_space_args(res.status == storage::Status::Skipped, res.reason, res.volume_total_bytes,
                                                                   res.required_free_bytes, res.volume_free_bytes))
                r.args.push_back(std::move(kv));
            // EF-X22: dogrulama fazi kosulduysa ham sayilar gecmise yazilir (karta satir eklenmez).
            // "storage_verify_blocks" = core::insights::kStorageVerifyMarkerMetric (karsilastirma isareti).
            if (res.verify.status != storage::VerifyStatus::NotRun) {
                r.metrics.push_back({"storage_verify_blocks", static_cast<double>(res.verify.blocks_verified)});
                r.metrics.push_back({"storage_verify_mismatch_blocks", static_cast<double>(res.verify.mismatched_blocks)});
            }
            if (const auto code = storage_verify_code(res.verify.status, res.reason))
                r.metrics.push_back({std::string(core::measure::kStorageVerifyStateMetric), static_cast<double>(static_cast<int>(*code))});
            for (const auto& pr : res.profiles)
                if (pr.status == storage::Status::Ok || pr.status == storage::Status::Partial) {
                    // Anahtar etiketten bagimsiz (gecmis karsilastirmasi kosudan kosuya ayni anahtari izler);
                    // burst etiketi ayri sayisal metriktir (1 = cihaz onbellegi karsilamis olabilir).
                    const std::string k = std::string("storage_") + pr.profile.id;
                    r.metrics.push_back({k + "_mbps", pr.mb_per_s()});
                    r.metrics.push_back({k + "_iops", pr.iops()});  // BENCH-09
                    if (pr.lat_samples > 0) {
                        r.metrics.push_back({k + "_p99_us", pr.lat_p99_us});
                        r.metrics.push_back({k + "_mean_us", pr.lat_mean_us});
                    }
                    r.metrics.push_back({k + "_burst", pr.label == storage::SpeedLabel::Burst ? 1.0 : 0.0});
                    // QD>1 sonucu is parcacigi sayisina bagli; slot adaleti (en az/ortalama) yalniz yeterli ornekte.
                    if (pr.io_threads) r.metrics.push_back({k + "_io_threads", static_cast<double>(pr.io_threads)});
                    if (pr.slot.known) r.metrics.push_back({k + "_slot_fairness", pr.slot.ratio});
                }
        }
        r.seconds = secs(t0);
        rep.subtests.push_back(r);
    }
    // 5) Birlesik: sahne + ayni anda depolama okumasi (akis takilmasi). Depolama yoksa atlanir.
    if (!cancel.load()) {
        progress("combined");
        SubtestResult r{"combined"};
        const auto t0 = Clock::now();
        if (plan.storage_volume.empty()) {
            r.status = SubtestStatus::Skipped;
            r.reason = "no_volume_selected";
        } else {
            Watchdog w(cancel, o.subtest_timeout_s);
            std::atomic<bool> io_stop{false}, io_done{false};
            storage::RunResult io;
            std::thread io_thread([&] {
                storage::RunConfig sc;
                sc.volume_root = plan.storage_volume;
                sc.profiles.clear();
                sc.verify = false;  // EF-X22: dogrulama ana depolama alt testinde; birlesik yuk yalniz okuma ortusmesi
                for (const auto& p : storage::default_profiles())
                    if (p.op == storage::Op::Read && p.pattern == storage::Pattern::Random) sc.profiles.push_back(p);
                io = storage::run_storage_test(sc, io_stop);
                io_done = true;
            });
            // Sahne, okuma bitene kadar (en cok 30 sn) tekrarlanir: iki yuk gercekten ortusur.
            ScoredFrames frames;  // kaynaklar karistirilmaz (bkz. d3d_renderer.h)
            bool cancelled = false, failed = false;
            std::string err;
            const auto tl = Clock::now();
            while (!io_done.load() && secs(tl) < 30) {
                const auto sr = run_scene(scene_run_config(plan), w.local);  // sahne alt testiyle ayni yapilandirma
                if (sr.cancelled) {
                    cancelled = true;
                    break;
                }
                if (!sr.ok) {
                    failed = true;
                    err = sr.error;
                    break;
                }
                frames.add(sr);
            }
            io_stop = true;
            io_thread.join();
            std::uint64_t io_ops = 0;
            for (const auto& pr : io.profiles) io_ops += pr.ops;
            if (cancelled) finish_cancel(r, w);
            else if (failed) r.status = SubtestStatus::Failed, r.reason = err;
            else {
                // Okuma sahne boyunca gercekten I/O yaptiysa birlesik sayilir; yapmadiysa kismi + neden.
                r.status = io_ops > 0 && !frames.frame_ms().empty() ? SubtestStatus::Completed : SubtestStatus::Partial;
                if (r.status == SubtestStatus::Partial) r.reason = "no_overlap_storage_" + io.reason;
                add_frame_metrics(r, frames.frame_ms(), "combined_");
                if (frames.runs()) add_source_metrics(r, frames.source(), frames.gpu_excluded(), std::nullopt, "combined_");
                r.metrics.push_back({"combined_io_ops", static_cast<double>(io_ops)});
            }
        }
        r.seconds = secs(t0);
        rep.subtests.push_back(r);
    }
    rep.cancelled = cancel.load();
    if (o.progress) o.progress("done", 1.0);
    return rep;
}

}  // namespace ef::benchmark
