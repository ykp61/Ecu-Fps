// Denge testi "Arena v1" (spec 2026-11-12 A1): amblem agi, palet, profiller, isik parcasi olcegi, hedef butcesi,
// is yuku kimligi ve kalici isci havuzu (saf kisimlar; D3D yok).
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "benchmark/scene/arena.h"
#include "benchmark/scene/sim_pool.h"
#include "ef_test.h"

using namespace ef::benchmark;

namespace {

std::string read_source(const char* rel) {
    std::ifstream f(std::string(EF_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// SVG `d` degerindeki sayilari sirayla cikarir (M x y H x L x y ... Z).
std::vector<std::vector<float>> svg_path_numbers(const std::string& svg) {
    std::vector<std::vector<float>> paths;
    std::size_t pos = 0;
    while ((pos = svg.find(" d=\"", pos)) != std::string::npos) {
        pos += 4;
        const std::size_t end = svg.find('"', pos);
        if (end == std::string::npos) break;
        std::vector<float> nums;
        std::string cur;
        for (std::size_t i = pos; i <= end; ++i) {
            const char ch = svg[i];
            if ((ch >= '0' && ch <= '9') || ch == '.') {
                cur += ch;
            } else if (!cur.empty()) {
                nums.push_back(std::stof(cur));
                cur.clear();
            }
        }
        paths.push_back(nums);
        pos = end;
    }
    return paths;
}

double polygon_area(std::span<const MarkPoint> p) {
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const MarkPoint& u = p[i];
        const MarkPoint& v = p[(i + 1) % p.size()];
        a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
    }
    return std::fabs(a) * 0.5;
}

double triangles_area(std::span<const MarkPoint> p, const std::vector<std::uint16_t>& t, bool& all_ccw) {
    double a = 0;
    all_ccw = true;
    for (std::size_t k = 0; k + 2 < t.size(); k += 3) {
        const MarkPoint& u = p[t[k]];
        const MarkPoint& v = p[t[k + 1]];
        const MarkPoint& w = p[t[k + 2]];
        const double c = (static_cast<double>(v.x) - u.x) * (static_cast<double>(w.y) - u.y) -
                         (static_cast<double>(v.y) - u.y) * (static_cast<double>(w.x) - u.x);
        all_ccw = all_ccw && c > 0;
        a += c * 0.5;
    }
    return a;
}

SimConfig small_sim() {
    SimConfig c;
    c.objects = 3000;
    c.draw_budget = 500;
    return c;
}

std::uint64_t pooled_prefix(SimPool& pool, const SimConfig& c, std::uint32_t frames) {
    FrameSim sim(c);
    VisibleSet visible;
    std::uint64_t acc = 0;
    for (std::uint32_t f = 0; f < frames; ++f) {
        pool.step(sim);
        sim.cull_and_sort(visible);
        acc = sim_checksum_fold(acc, sim.frame_checksum(visible));
    }
    return acc;
}

}  // namespace

EF_TEST(arena_mark_table_matches_brand_svg) {
    // Geometri tablosu arena_v1 sahnesinin cift ok yol verisidir (harici model yok): dosya degisirse tablo da degismeli.
    // Marka 2.0.0'da (EF amblemi) degisti; olcum sahnesi karsilastirilabilir kalsin diye arena_v1 geometrisi ayri
    // dosyada dondurulmustur (assets/benchmark/arena_v1-chevrons.svg).
    const std::string svg = read_source("assets/benchmark/arena_v1-chevrons.svg");
    const auto paths = svg_path_numbers(svg);
    EF_CHECK(paths.size() == 2);
    if (paths.size() != 2) return;
    // Yol A: M52 22 H170 L270 124 L120 298 H0 L126 164 L217 154 L234 134 H139 Z (H: yalniz x; y onceki noktadan)
    const std::vector<float> a_expect{52, 22, 170, 270, 124, 120, 298, 0, 126, 164, 217, 154, 234, 134, 139};
    const std::vector<float> b_expect{240, 0, 369, 460, 124, 341, 261, 187, 321, 111};
    EF_CHECK(paths[0] == a_expect && paths[1] == b_expect);
    // Tablo ayni noktalar (yatay parcalarda y onceki noktayla ayni).
    EF_CHECK(kMarkChevronA.size() == 9 && kMarkChevronB.size() == 6);
    EF_CHECK(kMarkChevronA[0].x == 52 && kMarkChevronA[0].y == 22 && kMarkChevronA[1].x == 170 && kMarkChevronA[1].y == 22);
    EF_CHECK(kMarkChevronA[2].x == 270 && kMarkChevronA[2].y == 124 && kMarkChevronA[3].x == 120 && kMarkChevronA[3].y == 298);
    EF_CHECK(kMarkChevronA[4].x == 0 && kMarkChevronA[4].y == 298 && kMarkChevronA[5].x == 126 && kMarkChevronA[5].y == 164);
    EF_CHECK(kMarkChevronA[6].x == 217 && kMarkChevronA[6].y == 154 && kMarkChevronA[7].x == 234 && kMarkChevronA[7].y == 134);
    EF_CHECK(kMarkChevronA[8].x == 139 && kMarkChevronA[8].y == 134);
    EF_CHECK(kMarkChevronB[0].x == 240 && kMarkChevronB[0].y == 0 && kMarkChevronB[1].x == 369 && kMarkChevronB[1].y == 0);
    EF_CHECK(kMarkChevronB[2].x == 460 && kMarkChevronB[2].y == 124 && kMarkChevronB[3].x == 341 && kMarkChevronB[3].y == 261);
    EF_CHECK(kMarkChevronB[4].x == 187 && kMarkChevronB[4].y == 261 && kMarkChevronB[5].x == 321 && kMarkChevronB[5].y == 111);
    // Ortak sinir kutusu.
    float max_x = 0, max_y = 0;
    for (const auto& p : kMarkChevronA) max_x = std::max(max_x, p.x), max_y = std::max(max_y, p.y);
    for (const auto& p : kMarkChevronB) max_x = std::max(max_x, p.x), max_y = std::max(max_y, p.y);
    EF_CHECK(max_x == kMarkWidth && max_y == kMarkHeight);
}

EF_TEST(arena_palette_matches_design_tokens) {
    const std::string tokens = read_source("design/tokens.json");
    EF_CHECK(!tokens.empty());
    const auto has = [&](const ArenaColor& c) {
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%06X", c.rgb);
        return tokens.find("\"" + std::string(c.token) + "\": \"" + hex + "\"") != std::string::npos;
    };
    for (const auto& c : kArenaObjectColors) EF_CHECK(has(c));
    for (const auto& c : kArenaLightColors) EF_CHECK(has(c));
    EF_CHECK(has(kArenaWallColor) && has(kArenaLineColor) && has(kArenaFogColor));
    // sRGB -> dogrusal: uclar ve siralama.
    const auto black = srgb_to_linear(0x000000), white = srgb_to_linear(0xFFFFFF), accent = srgb_to_linear(0x3DFF7A);
    EF_CHECK(black[0] == 0 && black[1] == 0 && black[2] == 0);
    EF_CHECK(std::fabs(white[0] - 1.f) < 1e-6f && std::fabs(white[2] - 1.f) < 1e-6f);
    EF_CHECK(accent[1] > accent[2] && accent[2] > accent[0] && accent[0] > 0 && accent[0] < 0.1f);
}

EF_TEST(arena_triangulation_ear_clipping) {
    // Ucgen sayisi n - 2, alan korunur, hepsi saat yonunun tersine; ic bukey cokgen de dogru bolunur.
    for (const std::span<const MarkPoint> poly : {std::span<const MarkPoint>(kMarkChevronA), std::span<const MarkPoint>(kMarkChevronB)}) {
        const auto t = triangulate_polygon(poly);
        EF_CHECK(t.size() == (poly.size() - 2) * 3);
        bool ccw = false;
        const double a = triangles_area(poly, t, ccw);
        // Tablo SVG sirasindadir (y asagi): kulak kesme yonu kendisi duzeltir; ucgenler tablo koordinatinda
        // saat yonunun tersine cikar.
        EF_CHECK(ccw);
        EF_CHECK(std::fabs(a - polygon_area(poly)) < 1e-3 * polygon_area(poly));
        for (const auto i : t) EF_CHECK(i < poly.size());
    }
    // Yon fark etmez: ters sirali kare de iki ucgen verir.
    const std::vector<MarkPoint> cw{{0, 0}, {0, 1}, {1, 1}, {1, 0}}, ccw_sq{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    bool ok = false;
    EF_CHECK(triangulate_polygon(cw).size() == 6 && std::fabs(triangles_area(cw, triangulate_polygon(cw), ok) - 1.0) < 1e-9 && ok);
    EF_CHECK(triangulate_polygon(ccw_sq).size() == 6 && std::fabs(triangles_area(ccw_sq, triangulate_polygon(ccw_sq), ok) - 1.0) < 1e-9 && ok);
    // Ucgenden kucuk ya da bozuk girdi: uydurma ucgen yok.
    const std::vector<MarkPoint> two{{0, 0}, {1, 0}};
    EF_CHECK(triangulate_polygon(two).empty() && triangulate_polygon({}).empty());
    const std::vector<MarkPoint> line{{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    EF_CHECK(triangulate_polygon(line).empty());
}

EF_TEST(arena_chevron_mesh_is_closed_outward_prism) {
    const ArenaMesh m = arena_chevron_mesh();
    // Kapaklar 2 x (9 + 6) kose, yan yuzler 4 x (9 + 6) kose; kapaklar 2 x (7 + 4) ucgen, yanlar 2 x (9 + 6) ucgen.
    EF_CHECK(m.vertices.size() == 90 && m.indices.size() == 156);
    bool unit = true, bounds = true, facing = true, flat = true;
    for (const ArenaVertex& v : m.vertices) {
        const float len = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
        unit = unit && std::fabs(len - 1.f) < 1e-5f;
        bounds = bounds && std::fabs(v.pos[0]) <= 1.f + 1e-6f && std::fabs(v.pos[1]) <= kMarkHeight / kMarkWidth + 1e-6f &&
                 std::fabs(std::fabs(v.pos[2]) - kChevronHalfDepth) < 1e-6f;
    }
    double volume = 0;
    for (std::size_t k = 0; k + 2 < m.indices.size(); k += 3) {
        const ArenaVertex& a = m.vertices[m.indices[k]];
        const ArenaVertex& b = m.vertices[m.indices[k + 1]];
        const ArenaVertex& c = m.vertices[m.indices[k + 2]];
        const double e1[3] = {b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
        const double e2[3] = {c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]};
        const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        // On yuz kurali: cross(v1 - v0, v2 - v0) kose normali yonunde (renderer'in varsayilan sarimi).
        facing = facing && n[0] * a.normal[0] + n[1] * a.normal[1] + n[2] * a.normal[2] > 0;
        flat = flat && a.normal == b.normal && b.normal == c.normal;  // duz golgeleme: yuz basina tek normal
        const double bxc[3] = {b.pos[1] * c.pos[2] - b.pos[2] * c.pos[1], b.pos[2] * c.pos[0] - b.pos[0] * c.pos[2],
                               b.pos[0] * c.pos[1] - b.pos[1] * c.pos[0]};
        volume += (a.pos[0] * bxc[0] + a.pos[1] * bxc[1] + a.pos[2] * bxc[2]) / 6.0;
    }
    EF_CHECK(unit && bounds && facing && flat);
    // Kapali ve disa donuk yuzey: isaretli hacim = taban alani x kalinlik.
    const double s = 2.0 / kMarkWidth;
    const double base = (polygon_area(kMarkChevronA) + polygon_area(kMarkChevronB)) * s * s;
    EF_CHECK(std::fabs(volume - base * 2.0 * kChevronHalfDepth) < 1e-4);
    // Ok +x'i gosterir: en sag kose on okun ucu (460, 124) -> x = +1.
    float max_x = -9;
    for (const ArenaVertex& v : m.vertices) max_x = std::max(max_x, v.pos[0]);
    EF_CHECK(std::fabs(max_x - 1.f) < 1e-6f);
    // En buyuk olcekte de ayiklamanin sinir kuresi (kCullRadius) icinde kalir: gorunen nesne kirpilmaz.
    EF_CHECK(arena_mesh_radius(m) * kArenaScaleMax <= sim_to_float(kCullRadius));
}

EF_TEST(arena_object_style_and_draw_list) {
    const std::uint64_t seed = 0x17A5C0DEull;
    bool ranges = true, same_school = true;
    for (std::uint32_t i = 0; i < 4096; ++i) {
        const ArenaObjectStyle s = arena_object_style(seed, i), again = arena_object_style(seed, i);
        ranges = ranges && s.scale >= kArenaScaleMin && s.scale <= kArenaScaleMax && s.roll_phase >= 0 && s.roll_phase < 6.2832f &&
                 std::fabs(s.roll_rate) >= 0.012f && std::fabs(s.roll_rate) < 0.049f && s.color[1] > 0 && s.color[1] <= 1.f;
        ranges = ranges && s.scale == again.scale && s.color == again.color && s.roll_rate == again.roll_rate;
        // Ayni suru ayni palet rengini tasir (yalniz parlaklik degisir): kanal oranlari esit.
        const ArenaObjectStyle first = arena_object_style(seed, i - i % kSchoolSize);
        same_school = same_school && std::fabs(s.color[0] * first.color[1] - first.color[0] * s.color[1]) < 1e-5f &&
                      std::fabs(s.color[2] * first.color[1] - first.color[2] * s.color[1]) < 1e-5f;
    }
    EF_CHECK(ranges && same_school);
    EF_CHECK(arena_object_style(seed, 5).scale != arena_object_style(seed + 1, 5).scale);  // tohuma bagli

    SimConfig sc;
    sc.objects = 3000;
    sc.draw_budget = 500;
    FrameSim sim(sc);
    for (int f = 0; f < 3; ++f) sim.step(2);
    VisibleSet v;
    sim.cull_and_sort(v);
    std::vector<ArenaObjectStyle> styles;
    for (std::uint32_t i = 0; i < sc.objects; ++i) styles.push_back(arena_object_style(sc.seed, i));
    std::vector<ArenaObjectGpu> list;
    arena_fill_objects(sim, v, styles, false, list);
    EF_CHECK(list.size() == v.draws && v.draws == 500);
    bool ok = true, sorted = true;
    for (std::size_t k = 0; k < list.size(); ++k) {
        const std::uint32_t idx = visible_index(v.keys[k]);
        const SimObject& o = sim.objects()[idx];
        ok = ok && list[k].zq[0] == visible_depth(v.keys[k]) && list[k].zq[1] == 0 && list[k].zq[2] == 0 && list[k].zq[3] == 0;
        ok = ok && list[k].pos_scale[0] == sim_to_float(o.x) && list[k].pos_scale[1] == sim_to_float(o.y) && list[k].pos_scale[3] == styles[idx].scale;
        ok = ok && list[k].color_rate[0] == styles[idx].color[0] && list[k].color_rate[3] == styles[idx].roll_rate &&
             list[k].dir_roll[3] == styles[idx].roll_phase;
        ok = ok && (list[k].dir_roll[0] != 0 || list[k].dir_roll[2] != 0);  // yon tabani tanimli
        if (k) sorted = sorted && list[k].zq[0] >= list[k - 1].zq[0];      // onden arkaya
    }
    EF_CHECK(ok && sorted);
    // Akis kipi (sim kapali): hiz = nesne ileri hizi - kamera adimi (ikiye tumleyen); hepsi kameraya yaklasir.
    FrameSim still(sc);
    still.cull_and_sort(v);
    arena_fill_objects(still, v, styles, true, list);
    bool flow = !list.empty();
    for (std::size_t k = 0; k < list.size(); ++k) {
        const SimObject& o = still.objects()[visible_index(v.keys[k])];
        flow = flow && list[k].zq[1] == static_cast<std::uint32_t>(o.vz - kCameraStep) && static_cast<std::int32_t>(list[k].zq[1]) < 0;
    }
    EF_CHECK(flow);
    // Stil listesi kisa kalirsa tasma yok (duz stil).
    arena_fill_objects(still, v, {}, true, list);
    EF_CHECK(list.size() == v.draws && list[0].pos_scale[3] == 1.f);
    // Akis maskesi: derinligi kapsayan ikinin kuvveti - 1, en az 16 birim.
    EF_CHECK(arena_flow_mask(0) == (32u << 16) - 1);
    EF_CHECK(arena_flow_mask(16u << 16) == (32u << 16) - 1 && arena_flow_mask((16u << 16) - 1) == (32u << 16) - 1);
    EF_CHECK(arena_flow_mask(117u << 16) == (128u << 16) - 1 && arena_flow_mask(128u << 16) == (256u << 16) - 1);
    EF_CHECK(arena_flow_mask(static_cast<std::uint32_t>(kCameraFar)) == (512u << 16) - 1);
    static_assert(kArenaNoFlowMask == 0xFFFFFFFFu);  // sim acikken sarma yok
}

EF_TEST(arena_lights_prefix_and_chunk_scaling) {
    const auto all = arena_lights(7, kArenaMaxLights), g = arena_lights(7, kArenaGpuG.lights), gmin = arena_lights(7, kArenaGpuGmin.lights);
    EF_CHECK(all.size() == kArenaMaxLights && g.size() == 64 && gmin.size() == 8);
    EF_CHECK(arena_lights(7, kArenaMaxLights + 50).size() == kArenaMaxLights && arena_lights(7, 0).empty());
    bool prefix = true, valid = true;
    for (std::size_t i = 0; i < g.size(); ++i) prefix = prefix && g[i].pos_inv_r2 == all[i].pos_inv_r2 && g[i].color == all[i].color;
    for (std::size_t i = 0; i < gmin.size(); ++i) prefix = prefix && gmin[i].pos_inv_r2 == g[i].pos_inv_r2;
    for (const auto& l : all) {
        valid = valid && l.pos_inv_r2[2] >= 0 && l.pos_inv_r2[2] < kArenaLightPeriod && l.pos_inv_r2[3] > 0 &&
                std::fabs(l.pos_inv_r2[0]) <= 26.f && std::fabs(l.pos_inv_r2[1]) <= 26.f;
    }
    EF_CHECK(prefix && valid);
    // Her on ek z boyunca dengeli yayilir: 8 isik 8 ayri dilime duser (Gmin'de karanlik bolge kalmaz).
    std::vector<bool> slice(8, false);
    for (const auto& l : gmin) slice[static_cast<std::size_t>(l.pos_inv_r2[2] / (kArenaLightPeriod / 8))] = true;
    EF_CHECK(std::count(slice.begin(), slice.end(), true) == 8);
    EF_CHECK(arena_lights(8, 8)[3].pos_inv_r2 != gmin[3].pos_inv_r2);  // tohuma bagli
    // Isik periyodu tunel dongusunu tam boler (dongu sarmasinda desen kopmaz).
    EF_CHECK(sim_to_float(kTunnelLength) / kArenaLightPeriod == 2.f);

    // TDR: parca boyu 1080p'ye kadar 512, sonra piksel sayisiyla ters orantili.
    EF_CHECK(arena_light_chunk(320ull * 180) == kLightsPerDraw && arena_light_chunk(1920ull * 1080) == kLightsPerDraw);
    EF_CHECK(arena_light_chunk(2560ull * 1440) == 288 && arena_light_chunk(3840ull * 2160) == 128);
    EF_CHECK(arena_light_chunk(16384ull * 16384) == kArenaMinLightChunk);
    EF_CHECK(arena_light_draws(0, 1920ull * 1080) == 1 && arena_light_draws(64, 3840ull * 2160) == 1);
    EF_CHECK(arena_light_draws(8, 320ull * 180) == 1 && arena_light_draws(129, 3840ull * 2160) == 2);
    EF_CHECK(arena_light_draws(kArenaMaxLights, 3840ull * 2160) == 8 && arena_light_draws(kArenaMaxLights, 1920ull * 1080) == 2);
    for (const std::uint64_t px : {320ull * 180, 1920ull * 1080, 2560ull * 1440, 3840ull * 2160})
        for (const std::uint32_t n : {0u, 8u, 64u, 300u, kArenaMaxLights})
            EF_CHECK(n <= arena_light_draws(n, px) * arena_light_chunk(px));  // her isik bir parcaya duser
    // Bir cizimin isi (piksel x isik) 1080p x 512'yi asmaz.
    EF_CHECK(3840ull * 2160 * arena_light_chunk(3840ull * 2160) <= 1920ull * 1080 * kLightsPerDraw);
}

EF_TEST(arena_target_budget_rule) {
    // Hedef kumesi 60 bayt / piksel (spec A2).
    EF_CHECK(target_set_bytes(1920, 1080) == 124416000ull && target_set_bytes(2560, 1440) == 221184000ull);
    EF_CHECK(target_set_bytes(3840, 2160) == 497664000ull && target_set_bytes(320, 180) == 3456000ull);
    EF_CHECK(target_set_bytes(16384, 16384) == 16106127360ull);  // 32 bit tasmasi yok
    const std::uint64_t G = 1ull << 30, M = 1ull << 20;
    EF_CHECK(targets_fit_budget(0, 0, 100 * M) == BudgetCheck::Unknown);  // butce okunamadi: sahte karar yok
    EF_CHECK(targets_fit_budget(8 * G, 1 * G, target_set_bytes(3840, 2160)) == BudgetCheck::Fits);
    EF_CHECK(targets_fit_budget(1 * G, 600 * M, target_set_bytes(3840, 2160)) == BudgetCheck::Exceeds);  // 600 + 475 > 921
    EF_CHECK(targets_fit_budget(1 * G, 600 * M, target_set_bytes(1920, 1080)) == BudgetCheck::Fits);
    EF_CHECK(targets_fit_budget(1 * G, 950 * M, 1) == BudgetCheck::Exceeds);  // kullanim zaten payin ustunde
    // Sinir: kullanim + hedef tam butce payina esitse sigar; 1 bayt fazlasi sigmaz.
    EF_CHECK(targets_fit_budget(1000, 400, 500) == BudgetCheck::Fits && targets_fit_budget(1000, 400, 501) == BudgetCheck::Exceeds);
    EF_CHECK(targets_fit_budget(1 * M, 0, target_set_bytes(640, 360)) == BudgetCheck::Exceeds);
}

EF_TEST(arena_profiles_and_config) {
    // A1 profilleri (sabit).
    EF_CHECK(kArenaVersion == "arena_v1");
    EF_CHECK(kArenaCpuC.name == "C" && kArenaCpuC.sim && kArenaCpuC.objects == 20000 && kArenaCpuC.object_draws == 4000);
    EF_CHECK(kArenaCpuC.draw_mode == DrawMode::PerObject && kArenaCpuC.max_workers == 8);
    EF_CHECK(kArenaCpuCmin.name == "Cmin" && !kArenaCpuCmin.sim && kArenaCpuCmin.draw_mode == DrawMode::Instanced && kArenaCpuCmin.max_workers == 0);
    // GPU geometrisi iki CPU profilinde ayni: ayni nesne sayisi cizilir.
    EF_CHECK(kArenaCpuCmin.objects == kArenaCpuC.objects && kArenaCpuCmin.object_draws == kArenaCpuC.object_draws);
    EF_CHECK(kArenaGpuG.name == "G" && kArenaGpuG.lights == 64 && kArenaGpuG.bloom);
    EF_CHECK(kArenaGpuGmin.name == "Gmin" && kArenaGpuGmin.lights == 8 && !kArenaGpuGmin.bloom);
    // C profili kare simulasyonunun C profiliyle ayni: altin saglama gecerli.
    const SimConfig pc = sim_profile_c();
    EF_CHECK(kArenaCpuC.objects == pc.objects && kArenaCpuC.object_draws == pc.draw_budget);

    const SceneConfig s1 = arena_config(kArenaCpuC, kArenaGpuGmin, 320, 180, 16);
    EF_CHECK(s1.arena && s1.sim && s1.sim_objects == 20000 && s1.object_draws == 4000 && s1.draw_mode == DrawMode::PerObject);
    EF_CHECK(s1.sim_workers == 8 && s1.arena_lights == 8 && !s1.arena_bloom && s1.width == 320 && s1.height == 180);
    EF_CHECK(s1.measure_s == 0 && s1.warmup_s == 0);  // olcum penceresi cagirana kalir
    EF_CHECK(sim_golden_applies(arena_sim_config(s1)));
    // Isci = min(mantiksal cekirdek - 1, 8), en az 1.
    EF_CHECK(arena_config(kArenaCpuC, kArenaGpuG, 1920, 1080, 1).sim_workers == 1);
    EF_CHECK(arena_config(kArenaCpuC, kArenaGpuG, 1920, 1080, 4).sim_workers == 3);
    EF_CHECK(arena_config(kArenaCpuC, kArenaGpuG, 1920, 1080, 8).sim_workers == 7);
    EF_CHECK(arena_config(kArenaCpuC, kArenaGpuG, 1920, 1080, 64).sim_workers == 8);
    const SceneConfig s2 = arena_config(kArenaCpuCmin, kArenaGpuG, 3840, 2160, 16);
    EF_CHECK(s2.arena && !s2.sim && s2.sim_workers == 0 && s2.draw_mode == DrawMode::Instanced && s2.sim_objects == 20000);
    EF_CHECK(s2.object_draws == 4000 && s2.arena_lights == 64 && s2.arena_bloom && s2.width == 3840 && s2.height == 2160);

    // Gecerlilik.
    EF_CHECK(arena_config_valid(s1) && arena_config_valid(s2));
    SceneConfig bad = s1;
    bad.arena = false;
    EF_CHECK(!arena_config_valid(bad));
    bad = s1, bad.object_draws = bad.sim_objects + 1;
    EF_CHECK(!arena_config_valid(bad));
    bad = s1, bad.sim_objects = kArenaMaxObjects + 1;
    EF_CHECK(!arena_config_valid(bad));
    bad = s1, bad.arena_lights = kArenaMaxLights + 1;
    EF_CHECK(!arena_config_valid(bad));
    bad = s1, bad.sim_objects = 0, bad.object_draws = 0;
    EF_CHECK(!arena_config_valid(bad));  // simulasyon icin nesne gerekir
    bad.sim = false;
    EF_CHECK(arena_config_valid(bad));   // bos sahne (yalniz tunel) gecerli
}

EF_TEST(arena_workload_hash_identity) {
    const SceneConfig a = arena_config(kArenaCpuC, kArenaGpuG, 1920, 1080, 16);
    const std::uint64_t h = arena_workload_hash(a);
    EF_CHECK(h != 0 && h == arena_workload_hash(a) && h == workload_hash(a, {}));
    const auto differs = [&](SceneConfig c) { return arena_workload_hash(c) != h; };
    SceneConfig c = a;
    c.seed ^= 1;
    EF_CHECK(differs(c));
    c = a, c.width = 2560, c.height = 1440;
    EF_CHECK(differs(c));
    c = a, c.sim = false;
    EF_CHECK(differs(c));
    c = a, c.sim_objects = 19999;
    EF_CHECK(differs(c));
    c = a, c.sim_workers = 4;
    EF_CHECK(differs(c));
    c = a, c.draw_mode = DrawMode::Instanced;
    EF_CHECK(differs(c));
    c = a, c.object_draws = 3999;
    EF_CHECK(differs(c));
    c = a, c.arena_lights = 8;
    EF_CHECK(differs(c));
    c = a, c.arena_bloom = false;
    EF_CHECK(differs(c));
    c = a, c.measure_s = 8, c.warmup_s = 2;
    EF_CHECK(differs(c));
    c = a, c.frames = 601;
    EF_CHECK(differs(c));
    // Tier ve onizleme sayaci Arena kimligine girmez (tier kullanilmaz).
    c = a, c.tier = Tier::High;
    FrameTap tap;
    c.frame_tap = &tap;
    EF_CHECK(!differs(c));
    // Dort profil cifti ayri kimlik.
    const std::uint64_t h1 = arena_workload_hash(arena_config(kArenaCpuC, kArenaGpuGmin, 1920, 1080, 16));
    const std::uint64_t h2 = arena_workload_hash(arena_config(kArenaCpuCmin, kArenaGpuG, 1920, 1080, 16));
    const std::uint64_t h3 = arena_workload_hash(arena_config(kArenaCpuCmin, kArenaGpuGmin, 1920, 1080, 16));
    EF_CHECK(h != h1 && h != h2 && h != h3 && h1 != h2 && h1 != h3 && h2 != h3);
    // Sim kapaliyken isci sayisi kimlige girmez (kullanilmaz).
    SceneConfig m = arena_config(kArenaCpuCmin, kArenaGpuG, 1920, 1080, 16);
    m.sim_workers = 5;
    EF_CHECK(arena_workload_hash(m) == h2);
}

EF_TEST(sim_pool_fork_join_matches_serial) {
    // Kalici havuz: ayni is parcaciklari kareler boyunca; sonuc isci sayisindan ve zamanlamadan bagimsiz.
    const SimConfig c = small_sim();
    const std::uint64_t serial = sim_prefix_checksum(c, 12, 1);
    for (const std::uint32_t workers : {1u, 2u, 3u, 8u}) {
        SimPool pool(workers);
        EF_CHECK(pool.workers() == workers);
        EF_CHECK(pooled_prefix(pool, c, 12) == serial);
        EF_CHECK(pooled_prefix(pool, c, 12) == serial);  // ayni havuz yeni simulasyonla yeniden kullanilir
    }
    SimPool one(0);
    EF_CHECK(one.workers() == 1 && pooled_prefix(one, c, 5) == sim_prefix_checksum(c, 5, 1));
    // step() commit eder: kare sayaci her cagrida bir artar.
    FrameSim sim(c);
    SimPool pool(4);
    for (int f = 0; f < 3; ++f) pool.step(sim);
    EF_CHECK(sim.frame() == 3);
    // Isciden fazla nesne yoksa da (bos araliklar) dogru.
    SimConfig tiny = c;
    tiny.objects = 3;
    tiny.draw_budget = 3;
    SimPool wide(8);
    EF_CHECK(pooled_prefix(wide, tiny, 6) == sim_prefix_checksum(tiny, 6, 1));
    // Katlama sim_prefix_checksum ile ayni.
    EF_CHECK(sim_checksum_fold(0, 1) != 0 && sim_checksum_fold(0, 1) != sim_checksum_fold(0, 2));
}

EF_TEST(sim_pool_profile_c_golden) {
    // C profili (20 000 nesne): havuzla hesaplanan ilk kareler altin degere esit (renderer adim basinda bunu dener).
    const SimConfig c = sim_profile_c();
    SimPool pool(sim_worker_count(16));
    EF_CHECK(pool.workers() == 8);
    EF_CHECK(pooled_prefix(pool, c, kSimGoldenFrames) == sim_golden_checksum());
}
