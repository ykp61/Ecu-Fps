// BENCH-01/BENCH-07: gercek D3D11 sahne kosusu (ekran disi; pencere acilmaz).
// Hafif kontroller (varsayilan, ctest):
//  1) Kare kipi determinizm (720p Low): ayni config iki kez -> ayni is yuku hash'i ve dogrulama pikseli.
//  2) Kisa sure kipi (720p Low, 0.2 + 0.5 sn): GPU tamamlama araligi ve FPS kaynagi.
//  3) Olcum karesi yok -> ok=false "no_measured_frames"; WARP ayri sinif; iptal.
// Agir kontroller (yalniz EF_HEAVY_PROBES=1; kullanici oyun oynarken ctest GPU'yu yuklemesin):
//  4) Kalibrasyon: agir boru hattinda tier secimi (Low -> High, butce 33 ms).
//  5) Gercek dogrulama: 1920x1080, isinma 3 sn + olcum 10 sn; olcum penceresinde KENDI surecimizin
//     GPU 3D motor kullanimi (PDH, ProcessGpuSampler) ve tum sureclerin 3D toplami (rakip yuk baglami).
//     Kendi 3D kullanim ortalamasi < %90 (BELOW_TARGET) veya olculemezse (UNMEASURED) cikis kodu 1.
//  6) VRAM adimlari (4 GiB'a kadar tahsis).
// Sonuclar uygulama sahnesi olcumudur; oyun FPS tahmini degildir.
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "benchmark/scene/d3d_renderer.h"
#include "core/measure/frame_stats.h"
#include "measurement/telemetry/process_gpu.h"

using namespace ef::benchmark;
using Clock = std::chrono::steady_clock;

namespace {

// Tum sureclerin 3D motor kullanim toplami (baglam: oyun/baska yuk GPU'yu paylasiyor mu).
struct AllGpu3d {
    PDH_HQUERY q = nullptr;
    PDH_HCOUNTER c = nullptr;
    bool open() {
        if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return false;
        if (PdhAddEnglishCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &c) != ERROR_SUCCESS) return false;
        PdhCollectQueryData(q);
        return true;
    }
    double sample() {
        if (!q || PdhCollectQueryData(q) != ERROR_SUCCESS) return -1;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr) != PDH_MORE_DATA) return -1;
        std::vector<unsigned char> buf(bytes);
        auto* it = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, it) != ERROR_SUCCESS) return -1;
        double s = 0;
        for (DWORD i = 0; i < count; ++i)
            if (it[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) s += it[i].FmtValue.doubleValue;
        return s;
    }
    ~AllGpu3d() {
        if (q) PdhCloseQuery(q);
    }
};

struct UtilSample {
    Clock::time_point t;
    double own = -1, all = -1;
};

bool heavy_enabled() {
    char buf[8]{};
    const DWORD n = GetEnvironmentVariableA("EF_HEAVY_PROBES", buf, sizeof buf);
    return n == 1 && buf[0] == '1';
}

void print_run(const char* tag, const SceneRunResult& r) {
    const auto sc = ef::core::measure::compute_frame_stats(scored_frame_ms(r));
    const auto sub = ef::core::measure::compute_frame_stats(r.frame_ms);
    std::printf("bench_scene: %s tier=%s %ux%u lights=%u light_draws=%u instances=%u in_flight=%u fence=%d ts=%d warmup=%.2fs/%u measure=%.2fs/%u\n",
                tag, std::string(to_string(r.tier)).c_str(), r.width, r.height, r.lights, r.light_draws, r.instances, r.frames_in_flight,
                r.fence_wait, r.gpu_timestamps, r.warmup_s, r.warmup_frames, r.measure_s, r.measured_frames);
    // Kaynak secimini dogrulamak icin kapsam ve dislanan kare sayisi sonuc satirinda (kapsam yoksa "unknown").
    char coverage[32] = "unknown";
    if (r.gpu_pacing_coverage) std::snprintf(coverage, sizeof coverage, "%.4f", *r.gpu_pacing_coverage);
    std::printf("bench_scene: %s result source=%s avg_fps=%.2f low1_fps=%.2f p99_ms=%.3f (n=%zu sufficient=%d) "
                "pacing_coverage=%s gpu_excluded=%u prepare_ms=%.1f\n",
                tag, std::string(to_string(r.frame_source)).c_str(), sc.avg_fps.value_or(0), sc.low1_fps.value_or(0),
                sc.p99_ms.value_or(0), sc.n, sc.sufficient, coverage, r.gpu_excluded, r.prepare_ms);
    std::printf("bench_scene: %s submit_cadence avg_fps=%.2f low1_fps=%.2f p99_ms=%.3f (n=%zu)\n", tag, sub.avg_fps.value_or(0),
                sub.low1_fps.value_or(0), sub.p99_ms.value_or(0), sub.n);
    std::printf("bench_scene: %s gpu_pacing n=%zu avg=%.3f p50=%.3f p99=%.3f max=%.3f | gpu_elapsed n=%zu avg=%.3f p99=%.3f excluded=%u | "
                "queue_saturation=%s%.3f | cpu_submit avg=%.3f p99=%.3f ms\n",
                tag, r.gpu_pacing.n, r.gpu_pacing.avg_ms, r.gpu_pacing.p50_ms, r.gpu_pacing.p99_ms, r.gpu_pacing.max_ms, r.gpu_elapsed.n,
                r.gpu_elapsed.avg_ms, r.gpu_elapsed.p99_ms, r.gpu_excluded, r.queue_saturation ? "" : "unknown/",
                r.queue_saturation.value_or(0), r.cpu_submit.avg_ms, r.cpu_submit.p99_ms);
}

// Agir kisim: kalibrasyon + 3 sn + 10 sn @1080p + VRAM. 0 = gecti.
int run_heavy(std::atomic<bool>& cancel) {
    SceneConfig base;  // 1920x1080
    const auto cal = calibrate_scene(base, CalibrationOptions{}, cancel);
    std::printf("bench_scene: calibration ok=%d err=%s seconds=%.2f submit_ms low=%.3f med=%.3f high=%.3f gpu_pacing_ms low=%.3f med=%.3f "
                "high=%.3f gpu_elapsed_ms low=%.3f med=%.3f high=%.3f -> tier=%s\n",
                cal.ok, cal.error.c_str(), cal.seconds, cal.avg_frame_ms[0], cal.avg_frame_ms[1], cal.avg_frame_ms[2], cal.avg_gpu_pacing_ms[0],
                cal.avg_gpu_pacing_ms[1], cal.avg_gpu_pacing_ms[2], cal.avg_gpu_elapsed_ms[0], cal.avg_gpu_elapsed_ms[1],
                cal.avg_gpu_elapsed_ms[2], std::string(to_string(cal.tier)).c_str());
    if (!cal.ok) return 1;

    SceneConfig v = base;
    v.tier = cal.tier;
    v.warmup_s = 3;
    v.measure_s = 10;
    std::vector<UtilSample> samples;
    std::mutex mu;
    std::atomic<bool> stop{false};
    ef::measurement::ProcessGpuSampler own(GetCurrentProcessId());
    const bool own_ok = own.open();
    AllGpu3d all;
    const bool all_ok = all.open();
    std::thread sampler([&] {
        while (!stop.load()) {
            for (int i = 0; i < 20 && !stop.load(); ++i) Sleep(50);  // ~1 sn aralik
            UtilSample s;
            s.t = Clock::now();
            if (own_ok) {
                const auto r = own.sample();
                if (r.engine_3d_pct) s.own = *r.engine_3d_pct;
            }
            if (all_ok) s.all = all.sample();
            std::lock_guard<std::mutex> lk(mu);
            samples.push_back(s);
        }
    });
    const auto r = run_scene(v, cancel);
    stop = true;
    sampler.join();
    if (!r.ok) {
        std::printf("bench_scene: heavy run failed error=%s\n", r.error.c_str());
        return 1;
    }
    print_run("heavy", r);
    // Yalniz tamamen olcum penceresinde kalan ornek araliklari (once ve sonraki ornek arasi).
    double own_sum = 0, own_min = 1e9, all_sum = 0;
    int own_n = 0, all_n = 0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i - 1].t < r.measure_begin || samples[i].t > r.measure_end) continue;
        if (samples[i].own >= 0) own_sum += samples[i].own, own_min = std::min(own_min, samples[i].own), ++own_n;
        if (samples[i].all >= 0) all_sum += samples[i].all, ++all_n;
    }
    const double own_avg = own_n ? own_sum / own_n : -1, all_avg = all_n ? all_sum / all_n : -1;
    const bool pass = own_n > 0 && own_avg >= 90.0;
    std::printf("bench_scene: util engine3d own_avg=%.1f%% own_min=%.1f%% (samples=%d) all_processes_3d_avg=%.1f%% (samples=%d) -> %s\n", own_avg,
                own_n ? own_min : -1.0, own_n, all_avg, all_n, own_n == 0 ? "UNMEASURED" : (pass ? "PASS(>=90%)" : "BELOW_TARGET(<90%)"));
    if (!pass) return 1;

    const auto vr = run_vram(256ull << 20, 0.8, 4ull << 30, cancel);
    std::printf("bench_scene: vram supported=%d budget=%llu steps=%zu stopped_at_budget=%d error=%s\n", vr.supported,
                static_cast<unsigned long long>(vr.budget_start), vr.steps.size(), vr.stopped_at_budget, vr.error.c_str());
    for (const auto& st : vr.steps)
        if (st.budget && st.usage > st.budget) return 1;  // butce asilmadi
    return 0;
}

}  // namespace

int main() {
    std::atomic<bool> cancel{false};

    // 1) Kare kipi determinizm (kisa, 720p Low)
    SceneConfig c;
    c.tier = Tier::Low;
    c.width = 1280, c.height = 720;
    c.warmup_frames = 30;
    c.frames = 200;
    const auto a = run_scene(c, cancel);
    if (!a.ok) {
        std::printf("bench_scene: skipped error=%s\n", a.error.c_str());
        return a.adapter.kind == RendererKind::None ? 0 : 1;  // GPU'suz CI: cihaz yok -> atla
    }
    const auto b = run_scene(c, cancel);
    std::wprintf(L"bench_scene: adapter=\"%ls\" vendor=0x%04x fl=0x%x kind=%hs\n", a.adapter.name.c_str(), a.adapter.vendor_id,
                 a.adapter.feature_level, std::string(to_string(a.adapter.kind)).c_str());
    std::printf("bench_scene: determinism hash=%016llx/%016llx pix=%016llx/%016llx frames=%zu measured=%u warmup=%u\n",
                static_cast<unsigned long long>(a.workload_hash), static_cast<unsigned long long>(b.workload_hash),
                static_cast<unsigned long long>(a.pixel_checksum), static_cast<unsigned long long>(b.pixel_checksum), a.frame_ms.size(),
                a.measured_frames, a.warmup_frames);
    if (a.workload_hash != b.workload_hash || a.pixel_checksum != b.pixel_checksum) {
        std::printf("bench_scene: nondeterministic\n");
        return 1;
    }
    if (a.measured_frames != c.frames || a.warmup_frames != c.warmup_frames || a.frame_ms.size() != c.frames) return 1;
    if (a.gpu_timestamps && a.gpu_elapsed_ms.size() + a.gpu_excluded != c.frames) return 1;  // her olculen kare ya gecerli ya dislandi
    if (a.gpu_pacing_ms.size() > a.gpu_elapsed_ms.size()) return 1;
    print_run("det", a);

    // 2) Kisa sure kipi (hafif): tamamlama araligi ve FPS kaynagi
    SceneConfig d = c;
    d.warmup_s = 0.2;
    d.measure_s = 0.5;
    const auto dr = run_scene(d, cancel);
    if (!dr.ok) {
        std::printf("bench_scene: duration run failed error=%s\n", dr.error.c_str());
        return 1;
    }
    print_run("short", dr);
    if (dr.measured_frames == 0 || dr.frame_ms.empty()) return 1;
    if (dr.frame_source != choose_frame_source(dr.gpu_timestamps, dr.gpu_pacing_ms.size(), dr.frame_ms.size(), dr.gpu_excluded)) return 1;
    if (dr.frame_source == FrameSource::GpuCompletion && (dr.gpu_excluded != 0 || !dr.gpu_pacing_coverage || *dr.gpu_pacing_coverage < 0.995))
        return 1;
    if (dr.queue_saturation && (*dr.queue_saturation < 0 || *dr.queue_saturation > 1)) return 1;

    // 3) Olcum karesi yok -> sonuc yok (sahte 0 FPS degil)
    SceneConfig z = c;
    z.warmup_frames = 0;
    z.frames = 0;
    const auto zr = run_scene(z, cancel);
    SceneConfig one = c;
    one.warmup_frames = 0;
    one.frames = 1;  // tek kare: aralik yok
    const auto onr = run_scene(one, cancel);
    std::printf("bench_scene: empty frames=0 ok=%d err=%s | frames=1 ok=%d err=%s\n", zr.ok, zr.error.c_str(), onr.ok, onr.error.c_str());
    if (zr.ok || zr.error != "no_measured_frames" || onr.ok || onr.error != "no_measured_frames") return 1;

    // WARP ayri sinif (kucuk cozunurluk: yazilim render'i yavas)
    SceneConfig s;
    s.tier = Tier::Low;
    s.width = 320, s.height = 180;
    s.frames = 10;
    const auto w = run_scene(s, cancel, false);
    std::printf("bench_scene: warp ok=%d kind=%s frames=%zu avg_frame_ms=%.3f\n", w.ok, std::string(to_string(w.adapter.kind)).c_str(),
                w.frame_ms.size(), w.frame_ms.empty() ? 0.0 : std::accumulate(w.frame_ms.begin(), w.frame_ms.end(), 0.0) / w.frame_ms.size());
    if (w.ok && w.adapter.kind != RendererKind::Software) return 1;
    // iptal: kismi sonuc puanlanmaz
    std::atomic<bool> stopped{true};
    const auto x = run_scene(c, stopped);
    if (!x.cancelled || x.ok) return 1;

    if (!heavy_enabled()) {
        std::printf("bench_scene: heavy=skipped (EF_HEAVY_PROBES=1 ile kalibrasyon + 3 sn + 10 sn @1080p + VRAM)\n");
        std::printf("bench_scene: ok\n");
        return 0;
    }
    if (run_heavy(cancel) != 0) {
        std::printf("bench_scene: heavy FAILED\n");
        return 1;
    }
    std::printf("bench_scene: ok\n");
    return 0;
}
