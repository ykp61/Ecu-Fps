#include "benchmark/scene/scene.h"
#include "ef_test.h"

using namespace ef::benchmark;

EF_TEST(scene_same_config_same_workload) {
    SceneConfig a;
    const auto va = generate_instances(a), vb = generate_instances(a);
    EF_CHECK(va.size() == instance_count(Tier::Medium) && workload_hash(a, va) == workload_hash(a, vb));
    SceneConfig b = a;
    b.seed ^= 1;
    EF_CHECK(workload_hash(b, generate_instances(b)) != workload_hash(a, va));
    SceneConfig t = a;
    t.tier = Tier::High;  // farkli tier esdeger sayilmaz
    EF_CHECK(workload_hash(t, generate_instances(t)) != workload_hash(a, va));
    SceneConfig r = a;
    r.width = 1280, r.height = 720;  // cozunurluk is yukunun parcasi
    EF_CHECK(workload_hash(r, generate_instances(r)) != workload_hash(a, va));
    SceneConfig d = a;
    d.measure_s = 10;  // olcum plani da kimlige girer
    EF_CHECK(workload_hash(d, generate_instances(d)) != workload_hash(a, va));
    const auto c1 = camera_at(a, 123), c2 = camera_at(a, 123);
    EF_CHECK(c1.eye == c2.eye && c1.target == c2.target);  // kamera zaman degil kare indeksine bagli
    EF_CHECK(kSceneVersion == "scene_v3");  // v3: aydinlatma parcali cizim (High degisti)
    EF_CHECK(a.width == 1920 && a.height == 1080);
}

EF_TEST(scene_v2_tiers_and_lights) {
    // Maliyet tier ile kesin artar; ornek sayilari eski secim oranlarini korur.
    EF_CHECK(instance_count(Tier::Low) == 2000 && instance_count(Tier::Medium) == 20000 && instance_count(Tier::High) == 80000);
    EF_CHECK(light_count(Tier::Low) < light_count(Tier::Medium) && light_count(Tier::Medium) < light_count(Tier::High));
    EF_CHECK(tier_params(Tier::Low).blur_radius < tier_params(Tier::High).blur_radius);
    SceneConfig lo, hi;
    lo.tier = Tier::Low;
    hi.tier = Tier::High;
    const auto ll = generate_lights(lo), lh = generate_lights(hi);
    EF_CHECK(ll.size() == light_count(Tier::Low) && lh.size() == light_count(Tier::High));
    // Kalibrasyon tek cihazda High tamponlarinin ilk N ogesini cizer: alt tier listesine birebir esit olmali.
    bool prefix = true;
    for (std::size_t i = 0; i < ll.size(); ++i) prefix = prefix && ll[i].pos_inv_r2 == lh[i].pos_inv_r2 && ll[i].color == lh[i].color;
    EF_CHECK(prefix);
    const auto il = generate_instances(lo), ih = generate_instances(hi);
    bool iprefix = true;
    for (std::size_t i = 0; i < il.size(); ++i) iprefix = iprefix && il[i].pos == ih[i].pos && il[i].scale == ih[i].scale;
    EF_CHECK(iprefix);
    for (const auto& l : lh) EF_CHECK(l.pos_inv_r2[3] > 0);  // zayiflama tanimli
    // Sure kipinde kamera kOrbitFrames ile doner; kare kipinde `frames` ile (eski yol).
    SceneConfig dur;
    dur.measure_s = 5;
    dur.frames = 0;
    EF_CHECK(camera_at(dur, 0).eye == camera_at(dur, kOrbitFrames).eye);
    // Sure kipinde `frames` (varsayilan 600) periyodu belirlemez; kare kipinde frames=0 -> periyot 1.
    SceneConfig dur600;
    dur600.measure_s = 5;
    EF_CHECK(dur600.frames == 600 && orbit_period(dur600) == kOrbitFrames);
    EF_CHECK(!(camera_at(dur600, 0).eye == camera_at(dur600, 600).eye));
    SceneConfig fr0;
    fr0.frames = 0;
    EF_CHECK(orbit_period(fr0) == 1 && camera_at(fr0, 7).eye == camera_at(fr0, 0).eye);
    SceneConfig fr;
    fr.frames = 200;
    EF_CHECK(orbit_period(fr) == 200);
    // TDR guvenligi: aydinlatma cizim basina en cok 512 isik.
    static_assert(kLightsPerDraw == 512);
    EF_CHECK(light_draws(0) == 1 && light_draws(1) == 1 && light_draws(512) == 1 && light_draws(513) == 2);
    EF_CHECK(light_draws(light_count(Tier::Low)) == 1 && light_draws(light_count(Tier::High)) == 6);
    for (const Tier t : {Tier::Low, Tier::Medium, Tier::High})
        EF_CHECK(light_count(t) <= light_draws(light_count(t)) * kLightsPerDraw);
}

EF_TEST(scene_duration_windows) {
    SceneConfig f;  // kare kipi
    f.warmup_frames = 10;
    f.frames = 100;
    EF_CHECK(!duration_mode(f));
    EF_CHECK(scene_phase(f, 0, 0.0) == ScenePhase::Warmup);
    EF_CHECK(scene_phase(f, 9, 99.0) == ScenePhase::Warmup);  // kare kipinde sure onemsiz
    EF_CHECK(scene_phase(f, 10, 0.0) == ScenePhase::Measure);
    EF_CHECK(scene_phase(f, 109, 0.0) == ScenePhase::Measure);
    EF_CHECK(scene_phase(f, 110, 0.0) == ScenePhase::Done);
    SceneConfig d;  // sure kipi
    d.warmup_s = 3;
    d.measure_s = 10;
    d.max_frames = 5000;
    EF_CHECK(duration_mode(d));
    EF_CHECK(scene_phase(d, 0, 0.0) == ScenePhase::Warmup);
    EF_CHECK(scene_phase(d, 400, 2.999) == ScenePhase::Warmup);
    EF_CHECK(scene_phase(d, 401, 3.0) == ScenePhase::Measure);
    EF_CHECK(scene_phase(d, 900, 12.99) == ScenePhase::Measure);
    EF_CHECK(scene_phase(d, 901, 13.0) == ScenePhase::Done);
    EF_CHECK(scene_phase(d, 5000, 4.0) == ScenePhase::Done);  // bellek siniri
    SceneConfig n = d;
    n.warmup_s = 0;  // isinmasiz sure kipi dogrudan olcer
    EF_CHECK(scene_phase(n, 0, 0.0) == ScenePhase::Measure);
}

EF_TEST(scene_tier_pick_from_calibration) {
    // Butceyi asmayan en yuksek tier; olculmeyen (0) atlanir; hicbiri sigmazsa Low.
    EF_CHECK(pick_scene_tier({0.5, 2.0, 9.0}, 33.0) == Tier::High);    // hizli GPU: High ~9 ms
    EF_CHECK(pick_scene_tier({4.0, 20.0, 80.0}, 33.0) == Tier::Medium);
    EF_CHECK(pick_scene_tier({25.0, 0.0, 0.0}, 33.0) == Tier::Low);    // ust tier'lar denenmedi
    EF_CHECK(pick_scene_tier({60.0, 0.0, 0.0}, 33.0) == Tier::Low);    // zayif iGPU: yine Low kosar
    EF_CHECK(pick_scene_tier({0.0, 0.0, 0.0}, 33.0) == Tier::Low);
    EF_CHECK(pick_scene_tier({1.0, 2.0, 3.0}, 0.0) == Tier::Low);      // gecersiz butce
}

EF_TEST(scene_gpu_query_ring_validity) {
    GpuQueryRing r(4);
    std::uint32_t s = 99;
    // 2 isinma + 2 olcum karesi: halka dolar, yeni kare reddedilir (cozulmeden yeniden kullanim yok).
    EF_CHECK(r.issue(false, s) && s == 0);
    EF_CHECK(r.issue(false, s) && s == 1);
    EF_CHECK(r.issue(true, s) && s == 2);
    EF_CHECK(r.issue(true, s) && s == 3);
    EF_CHECK(r.full() && !r.issue(true, s));
    // Isinma kareleri: gecerli olsalar da sayilmaz, dislanma da sayilmaz.
    EF_CHECK(r.oldest_slot() == 0);
    r.resolve_oldest(false, 1000000, true, 0, 5000);
    r.resolve_oldest(true, 1000000, true, 0, 5000);
    EF_CHECK(r.elapsed_ms().empty() && r.excluded() == 0 && r.pending() == 2);
    // Olcum: gecerli aralik ms'ye cevrilir; disjoint dislanir.
    r.resolve_oldest(false, 1000000, true, 1000, 9000);  // 8000 tik @ 1 MHz = 8 ms
    r.resolve_oldest(true, 1000000, true, 1000, 9000);
    EF_CHECK(r.elapsed_ms().size() == 1 && r.elapsed_ms()[0] > 7.999 && r.elapsed_ms()[0] < 8.001);
    EF_CHECK(r.excluded() == 1 && r.pending() == 0);
    // Sarma: yuva indeksi dongusel; frekans 0, okunamayan ve ters sira dislanir.
    EF_CHECK(r.issue(true, s) && s == 0);
    EF_CHECK(r.issue(true, s) && s == 1);
    EF_CHECK(r.issue(true, s) && s == 2);
    r.resolve_oldest(false, 0, true, 1, 2);
    r.resolve_oldest(false, 1000, false, 1, 2);
    r.resolve_oldest(false, 1000, true, 5, 2);
    EF_CHECK(r.excluded() == 4 && r.elapsed_ms().size() == 1);
    r.resolve_oldest(false, 1000, true, 1, 2);  // bos halkada etkisiz
    EF_CHECK(r.excluded() == 4 && r.pending() == 0);
}

EF_TEST(scene_gpu_completion_pacing) {
    // Tamamlama araligi = ardisik gecerli karelerin bitis (ts1) farki; @1 MHz tik = 1 us.
    GpuQueryRing r(8);
    std::uint32_t s = 0;
    auto frame = [&](bool measured, bool disjoint, std::uint64_t a, std::uint64_t b) {
        EF_CHECK(r.issue(measured, s));
        r.resolve_oldest(disjoint, 1000000, true, a, b);
    };
    frame(false, false, 0, 8000);       // isinma: sayilmaz ama ilk olculen karenin capasi
    frame(true, false, 8000, 18000);    // pacing 10 ms, elapsed 10 ms
    frame(true, false, 19000, 30000);   // pacing 12 ms, elapsed 11 ms (arada 1 ms bosluk)
    EF_CHECK(r.pacing_ms().size() == 2 && r.elapsed_ms().size() == 2);
    EF_CHECK(r.pacing_ms()[0] > 9.999 && r.pacing_ms()[0] < 10.001 && r.pacing_ms()[1] > 11.999 && r.pacing_ms()[1] < 12.001);
    frame(true, true, 30000, 40000);    // disjoint: dislanir, zincir kopar
    frame(true, false, 40000, 50000);   // onceki gecersiz -> pacing yok, elapsed var
    frame(true, false, 50000, 60000);   // zincir yeniden: 10 ms
    EF_CHECK(r.excluded() == 1 && r.elapsed_ms().size() == 4 && r.pacing_ms().size() == 3);
    // Yuva alamayan kare: dislanir ve sonraki karenin zinciri kopar (aradaki kare atlanmis sayilmaz).
    r.note_unissued(true);
    frame(true, false, 70000, 80000);
    EF_CHECK(r.excluded() == 2 && r.elapsed_ms().size() == 5 && r.pacing_ms().size() == 3);
    frame(true, false, 80000, 90000);
    EF_CHECK(r.pacing_ms().size() == 4);
    // Frekans degisimi zinciri koparir.
    EF_CHECK(r.issue(true, s));
    r.resolve_oldest(false, 2000000, true, 180000, 200000);
    EF_CHECK(r.pacing_ms().size() == 4 && r.elapsed_ms().size() == 7);
    // Isinmada yuva alamayan kare olculen kare dislanmasi sayilmaz.
    r.note_unissued(false);
    EF_CHECK(r.excluded() == 2);
}

EF_TEST(scene_frame_source_and_saturation) {
    // Zaman damgasi yok, dislanan kare var ya da pacing gonderim araliklarinin %99,5'inden azini kapsiyorsa
    // gonderim araligi (gecersiz kareler %1 dusugu iyilestirip takilmayi gizleyemez).
    EF_CHECK(choose_frame_source(false, 1000, 1000, 0) == FrameSource::SubmitCadence);
    EF_CHECK(choose_frame_source(true, 0, 1000, 0) == FrameSource::SubmitCadence);
    EF_CHECK(choose_frame_source(true, 0, 0, 0) == FrameSource::SubmitCadence);
    EF_CHECK(choose_frame_source(true, 950, 1000, 0) == FrameSource::SubmitCadence);   // eski %95 esigi artik yetmez
    EF_CHECK(choose_frame_source(true, 994, 1000, 0) == FrameSource::SubmitCadence);
    EF_CHECK(choose_frame_source(true, 995, 1000, 0) == FrameSource::GpuCompletion);
    EF_CHECK(choose_frame_source(true, 1000, 1000, 0) == FrameSource::GpuCompletion);
    EF_CHECK(choose_frame_source(true, 998, 1000, 1) == FrameSource::SubmitCadence);   // tek dislanan kare yeter
    EF_CHECK(!pacing_coverage(0, 0).has_value());
    EF_CHECK(*pacing_coverage(0, 10) == 0.0 && *pacing_coverage(995, 1000) > 0.9949 && *pacing_coverage(995, 1000) < 0.9951);
    EF_CHECK(*pacing_coverage(12, 10) == 1.0);  // kirpilir
    // Ring + secim birlikte: ortadaki gecersiz kare (takilma ani) iki araligi dusurur -> gonderim araligi.
    {
        GpuQueryRing r(8);
        std::uint32_t s = 0;
        std::uint64_t t = 0;
        auto frame = [&](bool disjoint, std::uint64_t dur) {
            EF_CHECK(r.issue(true, s));
            r.resolve_oldest(disjoint, 1000000, true, t, t + dur);
            t += dur;
        };
        frame(false, 10000);  // capa (olculen ilk kare; pacing yok)
        for (int i = 0; i < 200; ++i) frame(i == 100, i == 100 ? 50000 : 10000);
        // 200 gonderim araligi; pacing 198 (kare 100'e giren ve cikan dustu), 1 dislanan
        EF_CHECK(r.excluded() == 1 && r.pacing_ms().size() == 198);
        EF_CHECK(choose_frame_source(true, r.pacing_ms().size(), 200, r.excluded()) == FrameSource::SubmitCadence);
    }
    EF_CHECK(to_string(FrameSource::GpuCompletion) == "gpu_completion" && to_string(FrameSource::SubmitCadence) == "submit_cadence");
    // Doygunluk: bilinmiyorsa nullopt; [0,1] kirpilir.
    DurationStats e, p;
    EF_CHECK(!queue_saturation(e, p).has_value());
    e.n = 10, e.avg_ms = 9.0;
    EF_CHECK(!queue_saturation(e, p).has_value());
    p.n = 10, p.avg_ms = 10.0;
    EF_CHECK(queue_saturation(e, p).has_value() && *queue_saturation(e, p) > 0.899 && *queue_saturation(e, p) < 0.901);
    e.avg_ms = 12.0;  // ust uste binen kareler: elapsed > aralik -> 1'e kirpilir
    EF_CHECK(*queue_saturation(e, p) == 1.0);
    p.avg_ms = 0.0;
    EF_CHECK(!queue_saturation(e, p).has_value());
}

EF_TEST(scene_duration_stats) {
    EF_CHECK(summarize_ms({}).n == 0);  // sahte deger yok
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(static_cast<double>(i));
    v.push_back(-1.0);  // gecersiz dislanir
    const auto s = summarize_ms(v);
    EF_CHECK(s.n == 100 && s.avg_ms > 50.49 && s.avg_ms < 50.51);
    EF_CHECK(s.p50_ms == 50.0 && s.p99_ms == 99.0 && s.max_ms == 100.0 && s.sum_ms == 5050.0);
}

#include "benchmark/scene/d3d_renderer.h"

EF_TEST(gpu_timestamp_and_vram_rules) {
    EF_CHECK(gpu_timestamp_valid(false, 1000000000, true, 10, 20));
    EF_CHECK(!gpu_timestamp_valid(true, 1000000000, true, 10, 20));   // disjoint skora girmez
    EF_CHECK(!gpu_timestamp_valid(false, 0, true, 10, 20));           // frekans yok
    EF_CHECK(!gpu_timestamp_valid(false, 1000000000, false, 10, 20));  // okunamadi
    EF_CHECK(!gpu_timestamp_valid(false, 1000000000, true, 20, 10));   // ters sira
    const std::uint64_t G = 1ull << 30, M = 1ull << 20;
    EF_CHECK(vram_steps_allowed(15 * G, 1 * G, 256 * M, 0.8, 4 * G) == 16);  // cap 4 GiB
    EF_CHECK(vram_steps_allowed(4 * G, 3 * G, 256 * M, 0.8, 64 * G) == 0);   // kullanim zaten %80 ustunde -> hic
    EF_CHECK(vram_steps_allowed(0, 0, 256 * M, 0.8, 4 * G) == 0);            // butce yok -> sahte deger yok
}

// Arena v1 alanlari (spec 2026-11-12 A1) varsayilan kapalidir: scene_v3 is yuku kimligi degismemelidir.
// Altin degerler Arena alanlari eklenmeden ONCEKI koddan (6ac32c5) alinmistir.
EF_TEST(scene_v3_workload_hash_unchanged_by_arena_knobs) {
    SceneConfig a;
    SceneConfig lo;
    lo.tier = Tier::Low, lo.width = 1280, lo.height = 720, lo.frames = 120;
    SceneConfig hi;
    hi.tier = Tier::High, hi.warmup_s = 3, hi.measure_s = 10;
    EF_CHECK(workload_hash(a, generate_instances(a)) == 0x95AD0EFA63445E15ull);
    EF_CHECK(workload_hash(lo, generate_instances(lo)) == 0xD63DD3CCA7C19A6Cull);
    EF_CHECK(workload_hash(hi, generate_instances(hi)) == 0xC9F66CB3D736B552ull);
    // Varsayilan: hepsi kapali.
    EF_CHECK(!a.arena && !a.sim && a.sim_objects == 0 && a.sim_workers == 0 && a.draw_mode == DrawMode::Instanced);
    EF_CHECK(a.object_draws == 0 && a.arena_lights == 0 && !a.arena_bloom);
    // `arena` kapaliyken diger Arena alanlari okunmaz: kimlik ayni kalir.
    SceneConfig k = a;
    k.sim = true, k.sim_objects = 20000, k.sim_workers = 8, k.draw_mode = DrawMode::PerObject, k.object_draws = 4000;
    k.arena_lights = 64, k.arena_bloom = true;
    EF_CHECK(workload_hash(k, generate_instances(k)) == 0x95AD0EFA63445E15ull);
    // `arena` acilinca ayri kimlik (scene_v3 ile karismaz).
    k.arena = true;
    EF_CHECK(workload_hash(k, generate_instances(k)) != 0x95AD0EFA63445E15ull);
    EF_CHECK(to_string(DrawMode::Instanced) == "instanced" && to_string(DrawMode::PerObject) == "per_object");
}
