#include "benchmark/runner/runner.h"
#include "benchmark/scene/d3d_renderer.h"
#include "ef_test.h"

using namespace ef::benchmark;

EF_TEST(runner_tier_selection_and_locked_fingerprint) {
    EF_CHECK(select_tier(0.02, 4.0) == Tier::High);    // 0,02 x 40 = 0,8 ms
    EF_CHECK(select_tier(0.30, 4.0) == Tier::Medium);  // x40 = 12 > 4; x10 = 3 <= 4
    EF_CHECK(select_tier(1.00, 4.0) == Tier::Low);
    EF_CHECK(select_tier(0.0, 4.0) == Tier::Medium);   // olculemedi: varsayilan, tahmin yok
    LockedPlan a, b;
    EF_CHECK(a.fingerprint() == b.fingerprint());
    b.scene.tier = Tier::High;  // A/B arasinda is yuku degisirse iz degisir
    EF_CHECK(a.fingerprint() != b.fingerprint());
    b = a;
    b.storage_volume = L"D:\\";
    EF_CHECK(a.fingerprint() != b.fingerprint());
    EF_CHECK(to_string(SubtestStatus::Unavailable) == "unavailable");
}

EF_TEST(runner_scene_config_and_version_in_fingerprint) {
    LockedPlan p;  // kare kipi: isinma ayni kosuya eklenir (ayri kosu yok; 0/1 isinma olcumu bozmaz)
    p.scene.frames = 300;
    p.warmup_frames = 120;
    auto c = scene_run_config(p);
    EF_CHECK(c.warmup_frames == 120 && c.frames == 300 && !duration_mode(c));
    p.warmup_frames = 0;
    EF_CHECK(scene_run_config(p).warmup_frames == 0 && scene_run_config(p).frames == 300);
    p.warmup_frames = 1;
    EF_CHECK(scene_run_config(p).warmup_frames == 1 && scene_run_config(p).frames == 300);
    LockedPlan d;  // sure kipi: warmup_s kullanilir, kare isinmasi eklenmez
    d.scene.warmup_s = 3, d.scene.measure_s = 10, d.warmup_frames = 120;
    EF_CHECK(scene_run_config(d).warmup_frames == 0 && duration_mode(scene_run_config(d)));
    EF_CHECK(LockedPlan{}.fingerprint().rfind(std::string(kSceneVersion) + "|", 0) == 0);  // surum izde
}

EF_TEST(runner_scored_frames_do_not_mix_sources) {
    SceneRunResult g1, g2, s1;
    g1.frame_source = g2.frame_source = FrameSource::GpuCompletion;
    g1.frame_ms = {9, 9}, g1.gpu_pacing_ms = {10, 10};
    g2.frame_ms = {9}, g2.gpu_pacing_ms = {11};
    s1.frame_source = FrameSource::SubmitCadence;
    s1.frame_ms = {20, 20, 20}, s1.gpu_pacing_ms = {12}, s1.gpu_excluded = 2;
    ScoredFrames none;
    EF_CHECK(none.runs() == 0 && none.frame_ms().empty() && none.source() == FrameSource::SubmitCadence);
    ScoredFrames a;
    a.add(g1), a.add(g2);
    EF_CHECK(a.source() == FrameSource::GpuCompletion && a.frame_ms() == std::vector<double>({10, 10, 11}));
    EF_CHECK(&scored_frame_ms(g1) == &g1.gpu_pacing_ms && &scored_frame_ms(s1) == &s1.frame_ms);
    a.add(s1);  // tek gonderim-araligi kosusu tum seriyi gonderim araligina cevirir (onceki kosular dahil)
    EF_CHECK(a.source() == FrameSource::SubmitCadence && a.frame_ms() == std::vector<double>({9, 9, 9, 20, 20, 20}));
    EF_CHECK(a.gpu_excluded() == 2 && a.runs() == 3);
    a.add(g1);  // sonra gelen GPU kosusu da gonderim araligiyla eklenir
    EF_CHECK(a.source() == FrameSource::SubmitCadence && a.frame_ms().size() == 8);
}
