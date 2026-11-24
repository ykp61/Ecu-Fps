// Canli test ekrani (spec 2026-11-12 A4): genisletilmis FrameTap, kare basi besleme, GPU halkasinin canli
// toplami, onizleme boyutu / halkasi / kopyasi (saf kisimlar; D3D yok).
#include <cmath>
#include <cstdint>
#include <vector>

#include "benchmark/runner/runner.h"
#include "benchmark/scene/arena.h"
#include "benchmark/scene/scene.h"
#include "ef_test.h"

using namespace ef::benchmark;

namespace {
bool close_to(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }
}  // namespace

EF_TEST(live_tap_step_identity_and_reset) {
    FrameTap tap;
    tap.add(10.0);  // eski panel sayaci (yalniz olcum penceresi)
    FrameTapLive s = tap.snapshot();
    EF_CHECK(s.step_index == 0 && s.step_total == 0 && s.phase == TapPhase::Idle && !s.has_window && s.ring_count == 0);

    tap.begin_step(3, 9, 4, 2560, 1440);
    s = tap.snapshot();
    EF_CHECK(s.step_index == 3 && s.step_total == 9 && s.step_code == 4 && s.width == 2560 && s.height == 1440);
    EF_CHECK(s.phase == TapPhase::Prepare && !s.has_window && s.fps == 0 && !s.gpu_ms);
    // Canli alanlar eski sayaci etkilemez.
    EF_CHECK(tap.frames.load() == 1 && tap.sum_us.load() == 10000);

    tap.set_phase(TapPhase::Warmup);
    tap.publish(144.5, 3.25, 6.5, 0.4, 150.0);
    s = tap.snapshot();
    EF_CHECK(s.phase == TapPhase::Warmup && s.has_window && s.updates == 1 && s.ring_count == 1);
    EF_CHECK(close_to(s.fps, 144.5) && close_to(s.cpu_ms, 3.25) && s.gpu_ms && close_to(*s.gpu_ms, 6.5) && close_to(s.wait_share, 0.4));
    EF_CHECK(close_to(s.fps_ring[0], 150.0));
    // GPU suresi yoksa bos kalir (0 ms diye gosterilmez); bekleme payi [0,1]'e kirpilir; gecersiz deger 0 olur.
    tap.publish(60.0, 1.0, std::nullopt, 7.0, std::nan(""));
    s = tap.snapshot();
    EF_CHECK(!s.gpu_ms && close_to(s.wait_share, 1.0) && s.ring_count == 2 && s.fps_ring[1] == 0.f);
    EF_CHECK(tap.frames.load() == 1);  // publish eski sayaca yazmaz

    // Yeni adim: canli sayilar ve halka sifirlanir.
    tap.begin_step(4, 9, 5, 3840, 2160);
    s = tap.snapshot();
    EF_CHECK(s.step_index == 4 && !s.has_window && s.ring_count == 0 && s.updates == 0 && s.fps == 0 && s.phase == TapPhase::Prepare);
    // reset_live adim kimligini korur.
    tap.publish(30.0, 1.0, 2.0, 0.5, 30.0);
    tap.reset_live();
    s = tap.snapshot();
    EF_CHECK(s.step_index == 4 && s.width == 3840 && !s.has_window);
}

EF_TEST(live_tap_ring_keeps_newest_values_in_order) {
    FrameTap tap;
    const std::size_t pushes = kTapRingSize + 5;
    for (std::size_t i = 0; i < pushes; ++i) tap.publish(100.0, 1.0, 1.0, 0.0, static_cast<double>(i + 1));
    const FrameTapLive s = tap.snapshot();
    EF_CHECK(s.updates == pushes && s.ring_count == kTapRingSize);
    bool ordered = true;
    for (std::size_t i = 0; i < s.ring_count; ++i) ordered = ordered && close_to(s.fps_ring[i], static_cast<double>(i + 6));
    EF_CHECK(ordered);  // eskiden yeniye: 6 .. 37
}

EF_TEST(live_tap_feeder_buckets_and_one_second_window) {
    FrameTap tap;
    TapFeeder f(&tap);
    // Ilk kare (aralik yok) ve gecersiz araliklar sayilmaz.
    f.frame(0.0, 4.0, 1.0);
    f.frame(-1.0, 4.0, 1.0);
    f.frame(std::nan(""), 4.0, 1.0);
    f.frame(70000.0, 4.0, 1.0);
    EF_CHECK(!tap.snapshot().has_window);
    // 10 ms'lik kareler: kova 250 ms dolunca (25. karede) kapanir.
    f.gpu(48.0, 24);  // 24 karenin GPU suresi toplami: kare basi 2 ms
    for (int i = 0; i < 24; ++i) f.frame(10.0, 4.0, 1.0);
    EF_CHECK(!tap.snapshot().has_window);
    f.frame(10.0, 4.0, 1.0);
    FrameTapLive s = tap.snapshot();
    EF_CHECK(s.has_window && s.updates == 1 && s.ring_count == 1);
    EF_CHECK(close_to(s.fps, 100.0) && close_to(s.cpu_ms, 4.0) && close_to(s.wait_share, 0.1) && s.gpu_ms && close_to(*s.gpu_ms, 2.0));
    EF_CHECK(close_to(s.fps_ring[0], 100.0));
    // Uc kova daha 5 ms'lik karelerle (200 FPS): pencere 4 kova = 100 + 3 x 200 FPS'lik kovalar.
    for (int b = 0; b < 3; ++b)
        for (int i = 0; i < 50; ++i) f.frame(5.0, 1.0, 0.0);
    s = tap.snapshot();
    EF_CHECK(s.updates == 4 && s.ring_count == 4);
    // 25 + 150 kare / (250 + 750) ms
    EF_CHECK(close_to(s.fps, 175.0) && close_to(s.fps_ring[0], 100.0) && close_to(s.fps_ring[3], 200.0));
    EF_CHECK(close_to(s.cpu_ms, (25 * 4.0 + 150 * 1.0) / 175.0) && close_to(s.wait_share, 25.0 / 1000.0));
    // Bir kova daha: en eski (100 FPS) kova pencereden cikar -> "1 sn FPS" 200 olur.
    for (int i = 0; i < 50; ++i) f.frame(5.0, 1.0, 0.0);
    s = tap.snapshot();
    EF_CHECK(s.updates == 5 && close_to(s.fps, 200.0) && close_to(s.cpu_ms, 1.0) && close_to(s.wait_share, 0.0));
    EF_CHECK(!s.gpu_ms);  // pencerede GPU verisi kalmadi: bos (eski deger tasinmaz)
    // Yavas kare (kova suresinden uzun): her kare bir kova kapatir.
    f.frame(1000.0, 900.0, 50.0);
    s = tap.snapshot();
    EF_CHECK(s.updates == 6 && close_to(s.fps_ring[s.ring_count - 1], 1.0));
    // tap yoksa besleme hicbir sey yapmaz.
    TapFeeder none(nullptr);
    none.frame(10.0, 1.0, 1.0);
    none.gpu(1.0, 1);
}

EF_TEST(gpu_ring_live_total_includes_warmup_without_touching_scores) {
    GpuQueryRing ring(4);
    std::uint32_t slot = 0;
    double sum = -1;
    std::uint32_t n = 99;
    ring.take_live(sum, n);
    EF_CHECK(sum == 0 && n == 0);
    EF_CHECK(ring.issue(false, slot));  // isinma karesi
    ring.resolve_oldest(false, 1000, true, 100, 103);  // 3 ms
    EF_CHECK(ring.elapsed_ms().empty() && ring.pacing_ms().empty() && ring.excluded() == 0);
    EF_CHECK(ring.issue(true, slot));
    ring.resolve_oldest(false, 1000, true, 103, 108);  // 5 ms, olculen
    ring.take_live(sum, n);
    EF_CHECK(n == 2 && close_to(sum, 8.0));
    EF_CHECK(ring.elapsed_ms().size() == 1 && close_to(ring.elapsed_ms()[0], 5.0) && ring.pacing_ms().size() == 1);
    // Okununca sifirlanir; gecersiz (disjoint) kare canli toplama girmez ama dislanan sayilir.
    ring.take_live(sum, n);
    EF_CHECK(n == 0 && sum == 0);
    EF_CHECK(ring.issue(true, slot));
    ring.resolve_oldest(true, 1000, true, 108, 111);
    ring.take_live(sum, n);
    EF_CHECK(n == 0 && ring.excluded() == 1 && ring.elapsed_ms().size() == 1);
}

EF_TEST(preview_size_fits_box_and_never_upscales) {
    const auto eq = [](PreviewSize p, std::uint32_t w, std::uint32_t h) { return p.width == w && p.height == h; };
    EF_CHECK(eq(preview_size(320, 180), 320, 180));  // S1: buyutulmez
    EF_CHECK(eq(preview_size(960, 540), 960, 540));
    EF_CHECK(eq(preview_size(1920, 1080), 960, 540));
    EF_CHECK(eq(preview_size(2560, 1440), 960, 540));
    EF_CHECK(eq(preview_size(3840, 2160), 960, 540));
    EF_CHECK(eq(preview_size(1920, 1200), 864, 540));  // 16:10 -> yukseklik sinirlar
    EF_CHECK(eq(preview_size(3440, 1440), 960, 401));  // 21:9 -> genislik sinirlar
    EF_CHECK(eq(preview_size(961, 540), 960, 539));
    EF_CHECK(eq(preview_size(960, 541), 958, 540));
    EF_CHECK(eq(preview_size(16384, 16), 960, 1));     // en az 1 piksel
    EF_CHECK(eq(preview_size(0, 1080), 0, 0) && eq(preview_size(1920, 0), 0, 0));
    static_assert(kPreviewMaxWidth == 960 && kPreviewMaxHeight == 540);
    static_assert(1000.0 / kPreviewMinIntervalMs <= 20.0);  // en fazla 20 Hz
}

EF_TEST(preview_ring_rate_limit_delay_and_slot_reuse) {
    PreviewRing r;
    EF_CHECK(r.pending() == 0 && !r.ready(100) && r.should_issue(0.0));
    EF_CHECK(r.issue(0.0, 0, false) == 0u);
    // Hiz siniri: 50 ms dolmadan yeni kopya istenmez.
    EF_CHECK(!r.should_issue(49.9) && r.should_issue(50.0));
    // Gecikme: kopya en erken iki kare sonra okunmaya calisilir.
    EF_CHECK(!r.ready(0) && !r.ready(1));
    auto p = r.ready(2);
    EF_CHECK(p && p->slot == 0 && p->frame == 0 && !p->measuring);
    // GPU henuz bitirmediyse (pop edilmez) ayni kopya sonraki karede yeniden denenir.
    p = r.ready(3);
    EF_CHECK(p && p->slot == 0 && r.pending() == 1);
    EF_CHECK(r.issue(50.0, 5, true) == 1u);
    EF_CHECK(r.issue(100.0, 9, true) == 2u);
    // Halka dolu: yeni kopya istenmez (olcum dongusu beklemez, kare atlanir).
    EF_CHECK(r.pending() == kPreviewSlots && !r.should_issue(100000.0) && !r.issue(100000.0, 20, true));
    // En eski okununca yuvasi yeniden kullanilir; siradaki en eski kopya doner.
    r.pop();
    p = r.ready(7);
    EF_CHECK(p && p->slot == 1 && p->frame == 5 && p->measuring);
    EF_CHECK(r.should_issue(150.0) && r.issue(150.0, 21, false) == 0u);
    r.pop();
    r.pop();
    p = r.ready(23);
    EF_CHECK(p && p->slot == 0 && p->frame == 21 && r.pending() == 1);
    r.pop();
    r.pop();  // bosken zararsiz
    EF_CHECK(r.pending() == 0 && !r.ready(1000));
    // Hiz siniri son kopya ISTEGINE goredir (okuma zamanina degil).
    EF_CHECK(!r.should_issue(199.0) && r.should_issue(200.0));
    r.reset();
    EF_CHECK(r.should_issue(0.0) && r.issue(0.0, 0, false) == 0u);
}

EF_TEST(preview_copy_strips_row_padding) {
    // 3x2 piksel, satir basi 16 bayt (4 bayt dolgu).
    std::vector<std::uint8_t> src(32, 0xEE);
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 12; ++x) src[y * 16 + x] = static_cast<std::uint8_t>(y * 100 + x);
    PreviewFrame f;
    f.pixels = src.data();
    f.width = 3, f.height = 2, f.stride = 16;
    std::vector<std::uint8_t> out{1, 2, 3};
    EF_CHECK(copy_preview(f, out) && out.size() == 24);
    bool same = true;
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 12; ++x) same = same && static_cast<std::uint32_t>(out[y * 12 + x]) == y * 100 + x;
    EF_CHECK(same);
    // Tutarsiz kare kopyalanmaz.
    PreviewFrame bad = f;
    bad.stride = 11;
    EF_CHECK(!copy_preview(bad, out));
    bad = f, bad.pixels = nullptr;
    EF_CHECK(!copy_preview(bad, out));
    bad = f, bad.height = 0;
    EF_CHECK(!copy_preview(bad, out));
}

EF_TEST(preview_and_live_tap_stay_outside_workload_identity) {
    // Varsayilan kapali; acilmasi scene_v3 ve Arena kimligini / plan parmak izini degistirmez.
    EF_CHECK(SceneConfig{}.preview == nullptr && SceneConfig{}.frame_tap == nullptr);
    EF_CHECK(RunnerOptions{}.preview == nullptr && RunnerOptions{}.frame_tap == nullptr);
    const PreviewCallback cb = [](const PreviewFrame&) {};
    FrameTap tap;
    SceneConfig c;
    const auto inst = generate_instances(c);
    const std::uint64_t h = workload_hash(c, inst);
    c.preview = &cb;
    c.frame_tap = &tap;
    EF_CHECK(workload_hash(c, inst) == h);
    SceneConfig a = arena_config(kArenaCpuC, kArenaGpuG, 1920, 1080, 16);
    const std::uint64_t ha = arena_workload_hash(a);
    a.preview = &cb;
    a.frame_tap = &tap;
    EF_CHECK(arena_workload_hash(a) == ha);
    LockedPlan p;
    const std::string fp = p.fingerprint();
    p.scene.preview = &cb;
    EF_CHECK(p.fingerprint() == fp);
    EF_CHECK(scene_run_config(p).preview == &cb);
}
