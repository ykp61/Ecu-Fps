// Denge testi: kare simulasyonu belirlenimciligi, sabit isci araliklari, ayiklama + siralama, altin saglama.
#include <algorithm>
#include <thread>
#include <vector>

#include "benchmark/scene/frame_sim.h"
#include "ef_test.h"

using namespace ef::benchmark;

namespace {

SimConfig small_config() {
    SimConfig c;
    c.objects = 3000;
    c.draw_budget = 500;
    return c;
}

// Ayni kareleri gercek is parcaciklariyla isler (kalici havuzun yapacagi is: sabit aralik + join + commit).
std::uint64_t threaded_prefix(const SimConfig& c, std::uint32_t frames, std::uint32_t workers) {
    FrameSim sim(c);
    VisibleSet visible;
    std::vector<std::uint64_t> sums;
    for (std::uint32_t f = 0; f < frames; ++f) {
        std::vector<std::thread> pool;
        for (std::uint32_t w = 0; w < workers; ++w)
            pool.emplace_back([&sim, &c, workers, w] { sim.step_range(worker_range(c.objects, workers, w)); });
        for (auto& t : pool) t.join();
        sim.commit();
        sim.cull_and_sort(visible);
        sums.push_back(sim.frame_checksum(visible));
    }
    // sim_prefix_checksum ile ayni katlama: ayni degeri vermesi icin ayni kareler seri de islenir.
    FrameSim serial(c);
    bool same = true;
    for (std::uint32_t f = 0; f < frames; ++f) {
        serial.step(1);
        serial.cull_and_sort(visible);
        same = same && serial.frame_checksum(visible) == sums[f];
    }
    return same ? sim_prefix_checksum(c, frames, 1) : 0;
}

}  // namespace

EF_TEST(frame_sim_profile_and_worker_count) {
    const SimConfig c = sim_profile_c();
    EF_CHECK(c.objects == 20000 && c.draw_budget == 4000);  // spec A1 profil C
    EF_CHECK(kFrameSimVersion == "frame_sim_v1");
    // Isci = min(mantiksal cekirdek - 1, 8), en az 1.
    EF_CHECK(sim_worker_count(0) == 1 && sim_worker_count(1) == 1 && sim_worker_count(2) == 1);
    EF_CHECK(sim_worker_count(4) == 3 && sim_worker_count(8) == 7 && sim_worker_count(9) == 8);
    EF_CHECK(sim_worker_count(16) == 8 && sim_worker_count(64) == 8);
}

EF_TEST(frame_sim_worker_ranges_are_static_partition) {
    for (const std::uint32_t objects : {0u, 1u, 7u, 64u, 20000u, 20001u}) {
        for (const std::uint32_t workers : {1u, 2u, 3u, 7u, 8u, 13u}) {
            std::uint32_t next = 0;
            for (std::uint32_t w = 0; w < workers; ++w) {
                const IndexRange r = worker_range(objects, workers, w);
                EF_CHECK(r.begin == next && r.end >= r.begin);  // bosluksuz, ortusmesiz
                next = r.end;
            }
            EF_CHECK(next == objects);
            EF_CHECK(worker_range(objects, workers, workers).size() == 0);  // aralik disi isci: bos
        }
    }
    // Aralik yalniz (nesne, isci, sira) uclusune baglidir.
    const IndexRange a = worker_range(20000, 7, 3), b = worker_range(20000, 7, 3);
    EF_CHECK(a.begin == b.begin && a.end == b.end && a.begin == 8571 && a.end == 11428);
    // workers == 0 -> tek aralik.
    EF_CHECK(worker_range(100, 0, 0).begin == 0 && worker_range(100, 0, 0).end == 100);
}

EF_TEST(frame_sim_same_checksum_for_any_partition) {
    const SimConfig c = small_config();
    const std::uint64_t one = sim_prefix_checksum(c, 12, 1);
    EF_CHECK(one != 0);
    for (const std::uint32_t parts : {2u, 3u, 7u, 8u, 64u, 5000u})
        EF_CHECK(sim_prefix_checksum(c, 12, parts) == one);
    // Araliklarin islenme sirasi da sonucu degistirmez (ters sira).
    FrameSim fwd(c), rev(c);
    for (int f = 0; f < 12; ++f) {
        fwd.step(1);
        for (std::uint32_t w = 5; w-- > 0;) rev.step_range(worker_range(c.objects, 5, w));
        rev.commit();
        EF_CHECK(fwd.state_checksum() == rev.state_checksum() && fwd.frame() == rev.frame());
    }
    // Gercek is parcaciklariyla: 1 ve N isci ayni saglamayi verir.
    EF_CHECK(threaded_prefix(c, 12, 1) == one);
    EF_CHECK(threaded_prefix(c, 12, 4) == one);
    EF_CHECK(threaded_prefix(c, 12, 8) == one);
}

EF_TEST(frame_sim_is_deterministic_and_seeded_by_frame) {
    const SimConfig c = small_config();
    EF_CHECK(sim_prefix_checksum(c, 6) == sim_prefix_checksum(c, 6));  // ayni config -> ayni deger
    EF_CHECK(sim_prefix_checksum(c, 6) != sim_prefix_checksum(c, 7));  // kare sayisi sonuca girer
    SimConfig other = c;
    other.seed ^= 1;
    EF_CHECK(sim_prefix_checksum(other, 6) != sim_prefix_checksum(c, 6));
    other = c;
    other.neighbors = 8;
    EF_CHECK(sim_prefix_checksum(other, 6) != sim_prefix_checksum(c, 6));
    other = c;
    other.draw_budget = 400;
    EF_CHECK(sim_prefix_checksum(other, 6) != sim_prefix_checksum(c, 6));
    // Durum her karede degisir; kare sayaci yalniz commit ile artar.
    FrameSim sim(c);
    EF_CHECK(sim.frame() == 0 && sim.objects().size() == c.objects);
    const std::uint64_t s0 = sim.state_checksum();
    sim.step_range(worker_range(c.objects, 1, 0));
    EF_CHECK(sim.frame() == 0 && sim.state_checksum() == s0);  // commit oncesi guncel durum degismez
    sim.commit();
    EF_CHECK(sim.frame() == 1 && sim.state_checksum() != s0);
    // Kamera yalniz kare indeksine baglidir ve tunel boyunca sarilir.
    EF_CHECK(sim_camera_z(0) == 0 && sim_camera_z(1) == kCameraStep && sim_camera_z(40) == 40 * kCameraStep);
    const std::uint64_t lap = static_cast<std::uint64_t>(kTunnelLength / kCameraStep);
    EF_CHECK(sim_camera_z(lap) == 0 && sim_camera_z(lap + 3) == sim_camera_z(3));
    EF_CHECK(sim_object_hash(c.seed, 5) == sim_object_hash(c.seed, 5) && sim_object_hash(c.seed, 5) != sim_object_hash(c.seed, 6));
    // Bos sahne (Cmin: simulasyon yok) guvenlidir.
    SimConfig none = c;
    none.objects = 0;
    FrameSim empty(none);
    VisibleSet v;
    empty.step(4);
    empty.cull_and_sort(v);
    EF_CHECK(empty.frame() == 1 && v.in_frustum == 0 && v.draws == 0 && v.drawn().empty());
    // Eksik son grup ve tek uyeli grup (komsusuz) da tanimlidir.
    SimConfig tiny = c;
    tiny.objects = 5;
    EF_CHECK(sim_prefix_checksum(tiny, 20, 1) == sim_prefix_checksum(tiny, 20, 3));
    tiny.objects = kSchoolSize + 1;
    EF_CHECK(sim_prefix_checksum(tiny, 20, 1) == sim_prefix_checksum(tiny, 20, 4));
    tiny.objects = 1;
    EF_CHECK(sim_prefix_checksum(tiny, 20, 1) == sim_prefix_checksum(tiny, 20, 2));
}

EF_TEST(frame_sim_cull_and_depth_sort) {
    const SimConfig c = small_config();
    FrameSim sim(c);
    VisibleSet v;
    for (int f = 0; f < 30; ++f) {
        sim.step(3);
        sim.cull_and_sort(v);
        const auto objs = sim.objects();
        const std::int32_t cam = sim_camera_z(sim.frame());
        // Kaba kuvvetle ayni sayi: listede yalniz ve tam olarak gorunur nesneler var.
        std::uint32_t brute = 0;
        for (const SimObject& o : objs) {
            std::uint32_t depth = 0;
            if (sim_visible(o, cam, depth)) ++brute;
        }
        EF_CHECK(v.in_frustum == brute && v.keys.size() == brute);
        EF_CHECK(v.draws == std::min(brute, c.draw_budget) && v.drawn().size() == v.draws);
        std::vector<bool> seen(c.objects, false);
        bool ordered = true, valid = true;
        for (std::size_t k = 0; k < v.keys.size(); ++k) {
            const std::uint32_t idx = visible_index(v.keys[k]);
            if (k > 0 && !(v.keys[k - 1] < v.keys[k])) ordered = false;  // onden arkaya, esitlikte indeks; tekil
            if (idx >= c.objects || seen[idx]) {
                valid = false;
                break;
            }
            seen[idx] = true;
            std::uint32_t depth = 0;
            if (!sim_visible(objs[idx], cam, depth) || depth != visible_depth(v.keys[k])) valid = false;
            if (depth < static_cast<std::uint32_t>(kCameraNear) || depth > static_cast<std::uint32_t>(kCameraFar)) valid = false;
        }
        EF_CHECK(ordered && valid);
    }
    // Kesik piramit: eksendeki nesne yakin/uzak duzlem arasinda gorunur, disinda gorunmez.
    std::uint32_t depth = 0;
    SimObject o{0, 0, 100 * kSimOne, 0, 0, 0};
    EF_CHECK(sim_visible(o, 0, depth) && depth == static_cast<std::uint32_t>(100 * kSimOne));
    o.z = kCameraNear - 1;
    EF_CHECK(!sim_visible(o, 0, depth));
    o.z = kCameraNear;
    EF_CHECK(sim_visible(o, 0, depth));
    o.z = kCameraFar;
    EF_CHECK(sim_visible(o, 0, depth));
    o.z = kCameraFar + 1;
    EF_CHECK(!sim_visible(o, 0, depth));
    // Kamera arkasindaki nesne tunel dongusunde cok uzaktadir (gorunmez); sarma dogru calisir.
    o.z = 10 * kSimOne;
    EF_CHECK(!sim_visible(o, 20 * kSimOne, depth));
    o.z = 5 * kSimOne;
    EF_CHECK(sim_visible(o, kTunnelLength - 5 * kSimOne, depth) && depth == static_cast<std::uint32_t>(10 * kSimOne));
    // Yanal sinir: yarim genislik = derinlik * tan + nesne yaricapi.
    o = SimObject{0, 0, 16 * kSimOne, 0, 0, 0};
    const std::int32_t half_y = ((16 * kSimOne) / 256) * kCameraTanYQ8 + kCullRadius;
    o.y = half_y;
    EF_CHECK(sim_visible(o, 0, depth));
    o.y = half_y + 1;
    EF_CHECK(!sim_visible(o, 0, depth));
    o.y = -half_y;
    EF_CHECK(sim_visible(o, 0, depth));
    o.y = 0;
    const std::int32_t half_x = ((16 * kSimOne) / 256) * kCameraTanXQ8 + kCullRadius;
    o.x = -half_x;
    EF_CHECK(sim_visible(o, 0, depth));
    o.x = -half_x - 1;
    EF_CHECK(!sim_visible(o, 0, depth));
}

EF_TEST(frame_sim_profile_c_is_stationary_and_bounded) {
    // C profili: cizim cagrisi sayisi her karede butceye esit kalmali (is yuku duragan), durum sinirlar icinde.
    const SimConfig c = sim_profile_c();
    FrameSim sim(c);
    VisibleSet v;
    std::uint32_t min_in = c.objects, max_in = 0;
    bool draws_ok = true, bounded = true;
    for (int f = 0; f < 360; ++f) {
        sim.step(4);
        if (f % 4 != 3) continue;
        sim.cull_and_sort(v);
        min_in = std::min(min_in, v.in_frustum);
        max_in = std::max(max_in, v.in_frustum);
        draws_ok = draws_ok && v.draws == c.draw_budget;
        for (const SimObject& o : sim.objects()) {
            if (o.x < -kTunnelHardRadius || o.x > kTunnelHardRadius || o.y < -kTunnelHardRadius || o.y > kTunnelHardRadius ||
                o.z < 0 || o.z >= kTunnelLength || o.vx < -kSimOne || o.vx > kSimOne || o.vy < -kSimOne || o.vy > kSimOne ||
                o.vz < -kSimOne || o.vz > kSimOne) {
                bounded = false;
                break;
            }
        }
    }
    EF_CHECK(draws_ok && bounded);
    EF_CHECK(min_in >= c.draw_budget + c.draw_budget / 2);  // butcenin en az 1,5 kati gorunur: pay var
    EF_CHECK(max_in <= c.objects);
}

EF_TEST(frame_sim_golden_prefix_checksum) {
    const SimConfig c = sim_profile_c();
    EF_CHECK(sim_golden_applies(c));
    EF_CHECK(sim_prefix_checksum(c, kSimGoldenFrames, 1) == sim_golden_checksum());
    EF_CHECK(sim_prefix_checksum(c, kSimGoldenFrames, sim_worker_count(16)) == sim_golden_checksum());
    EF_CHECK(sim_golden_checksum() != 0);
    // Altin deger yalniz C profili icindir.
    SimConfig other = c;
    other.objects = 19999;
    EF_CHECK(!sim_golden_applies(other) && sim_prefix_checksum(other, kSimGoldenFrames) != sim_golden_checksum());
    other = c;
    other.seed += 1;
    EF_CHECK(!sim_golden_applies(other));
    other = c;
    other.draw_budget = 3999;
    EF_CHECK(!sim_golden_applies(other));
    other = c;
    other.neighbors = 11;
    EF_CHECK(!sim_golden_applies(other));
}
