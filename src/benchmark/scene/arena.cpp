#include "benchmark/scene/arena.h"

#include <algorithm>
#include <cmath>

namespace ef::benchmark {

namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u64 kSaltSchoolStyle = 0x00A7E1A5C0107ull;
constexpr u64 kSaltObjectStyle = 0x057A1E0B7EC70ull;
constexpr u64 kSaltLight = 0x0116A7C0FFEE5ull;
constexpr float kTwoPi = 6.2831853f;

constexpr u64 mix64(u64 x) noexcept {  // splitmix64 sonlandiricisi
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// 16 bitlik alan -> [0, 1).
constexpr float unit16(u64 bits) noexcept { return static_cast<float>(bits & 0xFFFFu) * (1.0f / 65536.0f); }

// van der Corput (taban 2): her on ek [0, 1) araligina dengeli yayilir.
constexpr float radical_inverse(u32 i) noexcept {
    u32 r = 0;
    for (int b = 0; b < 32; ++b) r |= ((i >> b) & 1u) << (31 - b);
    return static_cast<float>(r >> 8) * (1.0f / 16777216.0f);
}

double signed_area2(std::span<const MarkPoint> p) noexcept {
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const MarkPoint& u = p[i];
        const MarkPoint& v = p[(i + 1) % p.size()];
        a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
    }
    return a;
}

}  // namespace

std::vector<std::uint16_t> triangulate_polygon(std::span<const MarkPoint> pts) {
    std::vector<std::uint16_t> out;
    const std::size_t n = pts.size();
    if (n < 3 || n > 0xFFFFu) return out;
    const bool ccw = signed_area2(pts) >= 0;
    std::vector<std::uint16_t> ring(n);
    for (std::size_t i = 0; i < n; ++i) ring[i] = static_cast<std::uint16_t>(ccw ? i : n - 1 - i);
    const auto cross = [&](std::uint16_t a, std::uint16_t b, std::uint16_t c) {
        return (static_cast<double>(pts[b].x) - pts[a].x) * (static_cast<double>(pts[c].y) - pts[a].y) -
               (static_cast<double>(pts[b].y) - pts[a].y) * (static_cast<double>(pts[c].x) - pts[a].x);
    };
    out.reserve((n - 2) * 3);
    while (ring.size() > 3) {
        bool clipped = false;
        const std::size_t m = ring.size();
        for (std::size_t i = 0; i < m && !clipped; ++i) {
            const std::uint16_t a = ring[(i + m - 1) % m], b = ring[i], c = ring[(i + 1) % m];
            if (!(cross(a, b, c) > 0)) continue;  // ic bukey ya da dogrusal kose kulak olamaz
            bool empty = true;
            for (const std::uint16_t p : ring) {
                if (p == a || p == b || p == c) continue;
                if (cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0) {
                    empty = false;
                    break;
                }
            }
            if (!empty) continue;
            out.insert(out.end(), {a, b, c});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
        }
        if (!clipped) return {};  // bozuk / kendini kesen cokgen: uydurma ucgen yok
    }
    out.insert(out.end(), {ring[0], ring[1], ring[2]});
    return out;
}

ArenaMesh arena_chevron_mesh() {
    ArenaMesh m;
    const float cx = kMarkWidth * 0.5f, cy = kMarkHeight * 0.5f, s = 2.0f / kMarkWidth, d = kChevronHalfDepth;
    const auto add_part = [&](std::span<const MarkPoint> src) {
        // Merkezle, olcekle, y'yi yukari cevir; saat yonunun tersine sirala.
        std::vector<MarkPoint> p(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) p[i] = {(src[i].x - cx) * s, (cy - src[i].y) * s};
        if (signed_area2(p) < 0) std::reverse(p.begin(), p.end());
        const auto tris = triangulate_polygon(p);
        const auto n = static_cast<std::uint16_t>(p.size());
        // On kapak (+z) ve arka kapak (-z; sarim ters).
        const auto front = static_cast<std::uint16_t>(m.vertices.size());
        for (const MarkPoint& q : p) m.vertices.push_back({{q.x, q.y, d}, {0.f, 0.f, 1.f}});
        for (const std::uint16_t i : tris) m.indices.push_back(static_cast<std::uint16_t>(front + i));
        const auto back = static_cast<std::uint16_t>(m.vertices.size());
        for (const MarkPoint& q : p) m.vertices.push_back({{q.x, q.y, -d}, {0.f, 0.f, -1.f}});
        for (std::size_t k = 0; k + 2 < tris.size(); k += 3) {
            m.indices.push_back(static_cast<std::uint16_t>(back + tris[k]));
            m.indices.push_back(static_cast<std::uint16_t>(back + tris[k + 2]));
            m.indices.push_back(static_cast<std::uint16_t>(back + tris[k + 1]));
        }
        // Yan yuzler: kenar a -> b, dis normal (dy, -dx).
        for (std::uint16_t i = 0; i < n; ++i) {
            const MarkPoint a = p[i], b = p[(i + 1) % n];
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (!(len > 0)) continue;
            const std::array<float, 3> nrm{dy / len, -dx / len, 0.f};
            const auto base = static_cast<std::uint16_t>(m.vertices.size());
            m.vertices.push_back({{a.x, a.y, -d}, nrm});
            m.vertices.push_back({{b.x, b.y, -d}, nrm});
            m.vertices.push_back({{a.x, a.y, d}, nrm});
            m.vertices.push_back({{b.x, b.y, d}, nrm});
            for (const int k : {0, 1, 2, 2, 1, 3}) m.indices.push_back(static_cast<std::uint16_t>(base + k));
        }
    };
    add_part(kMarkChevronA);
    add_part(kMarkChevronB);
    return m;
}

float arena_mesh_radius(const ArenaMesh& m) noexcept {
    float r2 = 0;
    for (const ArenaVertex& v : m.vertices) r2 = std::max(r2, v.pos[0] * v.pos[0] + v.pos[1] * v.pos[1] + v.pos[2] * v.pos[2]);
    return std::sqrt(r2);
}

std::array<float, 3> srgb_to_linear(std::uint32_t rgb) noexcept {
    const auto ch = [&](int shift) { return std::pow(static_cast<float>((rgb >> shift) & 0xFFu) / 255.0f, 2.2f); };
    return {ch(16), ch(8), ch(0)};
}

ArenaObjectStyle arena_object_style(std::uint64_t seed, std::uint32_t index) noexcept {
    const u64 hs = mix64(seed ^ mix64(u64{index / kSchoolSize} + 1) ^ kSaltSchoolStyle);
    const u64 ho = mix64(seed ^ mix64(u64{index} + 1) ^ kSaltObjectStyle);
    // Suru rengi: vurgu yesili agirlikli (10 kovadan 5), camgobegi 2, mor 2, turuncu 1.
    static constexpr std::array<std::uint8_t, 10> kBucket{0, 0, 0, 0, 0, 1, 1, 2, 2, 3};
    const std::array<float, 3> base = srgb_to_linear(kArenaObjectColors[kBucket[hs % kBucket.size()]].rgb);
    const float brightness = 0.75f + 0.25f * unit16(ho);
    ArenaObjectStyle s{};
    s.color = {base[0] * brightness, base[1] * brightness, base[2] * brightness};
    s.scale = kArenaScaleMin + (kArenaScaleMax - kArenaScaleMin) * unit16(ho >> 16);
    s.roll_phase = kTwoPi * unit16(ho >> 32);
    s.roll_rate = (0.012f + 0.036f * unit16(ho >> 48)) * (((hs >> 40) & 1u) != 0 ? 1.f : -1.f);
    return s;
}

void arena_fill_objects(const FrameSim& sim, const VisibleSet& visible, std::span<const ArenaObjectStyle> styles, bool flow,
                        std::vector<ArenaObjectGpu>& out) {
    const auto objects = sim.objects();
    const auto drawn = visible.drawn();
    out.resize(drawn.size());
    static constexpr ArenaObjectStyle kPlain{{1.f, 1.f, 1.f}, 1.f, 0.f, 0.f};
    for (std::size_t k = 0; k < drawn.size(); ++k) {
        const u32 index = visible_index(drawn[k]);
        const SimObject& o = objects[index];
        const ArenaObjectStyle& st = index < styles.size() ? styles[index] : kPlain;
        ArenaObjectGpu& g = out[k];
        g.pos_scale[0] = sim_to_float(o.x), g.pos_scale[1] = sim_to_float(o.y), g.pos_scale[2] = 0.f, g.pos_scale[3] = st.scale;
        // Yon: hiz vektoru. Yatay bileseni olmayan (tam dusey / sifir) hiz ileri yone duser: taban tanimli kalir.
        const bool degenerate = o.vx == 0 && o.vz == 0;
        g.dir_roll[0] = degenerate ? 0.f : sim_to_float(o.vx);
        g.dir_roll[1] = degenerate ? 0.f : sim_to_float(o.vy);
        g.dir_roll[2] = degenerate ? 1.f : sim_to_float(o.vz);
        g.dir_roll[3] = st.roll_phase;
        g.color_rate[0] = st.color[0], g.color_rate[1] = st.color[1], g.color_rate[2] = st.color[2], g.color_rate[3] = st.roll_rate;
        g.zq[0] = visible_depth(drawn[k]);
        g.zq[1] = flow ? static_cast<u32>(o.vz - kCameraStep) : 0u;
        g.zq[2] = 0, g.zq[3] = 0;
    }
}

std::uint32_t arena_flow_mask(std::uint32_t max_depth_q16) noexcept {
    const u32 need = std::max<u32>(max_depth_q16, 16u << kSimFixShift);
    u32 p = 1u << 20;
    while (p <= need && p < 0x80000000u) p <<= 1;
    return p - 1;
}

std::vector<PointLight> arena_lights(std::uint64_t seed, std::uint32_t count) {
    const u32 n = std::min(count, kArenaMaxLights);
    std::vector<PointLight> v;
    v.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        const u64 h = mix64(seed ^ mix64(u64{i} + 1) ^ kSaltLight);
        const float angle = kTwoPi * unit16(h), ring = 10.f + 16.f * unit16(h >> 16), reach = 34.f + 22.f * unit16(h >> 32);
        const auto col = srgb_to_linear(kArenaLightColors[i % kArenaLightColors.size()].rgb);
        PointLight l{};
        l.pos_inv_r2 = {std::cos(angle) * ring, std::sin(angle) * ring, kArenaLightPeriod * radical_inverse(i), 1.f / (reach * reach)};
        l.color = {col[0], col[1], col[2], 1.f};
        v.push_back(l);
    }
    return v;
}

std::uint32_t arena_light_chunk(std::uint64_t pixels) noexcept {
    if (pixels <= kArenaChunkBasePixels) return kLightsPerDraw;
    const u64 c = u64{kLightsPerDraw} * kArenaChunkBasePixels / pixels;
    return static_cast<u32>(std::max<u64>(c, kArenaMinLightChunk));
}

std::uint32_t arena_light_draws(std::uint32_t lights, std::uint64_t pixels) noexcept {
    const u32 chunk = arena_light_chunk(pixels);
    const u32 n = lights / chunk + (lights % chunk ? 1u : 0u);
    return n ? n : 1u;
}

std::uint64_t target_set_bytes(std::uint32_t width, std::uint32_t height) noexcept {
    return u64{width} * height * kTargetBytesPerPixel;
}

BudgetCheck targets_fit_budget(std::uint64_t budget, std::uint64_t usage, std::uint64_t bytes) noexcept {
    if (budget == 0) return BudgetCheck::Unknown;
    const auto limit = static_cast<u64>(static_cast<double>(budget) * kTargetBudgetFraction);
    if (usage > limit || bytes > limit - usage) return BudgetCheck::Exceeds;
    return BudgetCheck::Fits;
}

SceneConfig arena_config(const ArenaCpuProfile& cpu, const ArenaGpuProfile& gpu, std::uint32_t width, std::uint32_t height,
                         std::uint32_t logical_cores) noexcept {
    SceneConfig c;
    c.arena = true;
    c.width = width;
    c.height = height;
    c.sim = cpu.sim;
    c.sim_objects = cpu.objects;
    c.sim_workers = cpu.sim && cpu.max_workers ? std::min(sim_worker_count(logical_cores), cpu.max_workers) : 0u;
    c.draw_mode = cpu.draw_mode;
    c.object_draws = cpu.object_draws;
    c.arena_lights = gpu.lights;
    c.arena_bloom = gpu.bloom;
    return c;
}

bool arena_config_valid(const SceneConfig& c) noexcept {
    if (!c.arena) return false;
    if (c.sim_objects > kArenaMaxObjects || c.object_draws > c.sim_objects) return false;
    if (c.arena_lights > kArenaMaxLights || c.sim_workers > 64) return false;
    if (c.sim && c.sim_objects == 0) return false;
    return true;
}

SimConfig arena_sim_config(const SceneConfig& c) noexcept {
    SimConfig s;
    s.seed = c.seed;
    s.objects = c.sim_objects;
    s.draw_budget = c.object_draws;
    return s;
}

std::uint64_t arena_workload_hash(const SceneConfig& c) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };
    const auto mix_u32 = [&](u32 v) { mix(&v, sizeof v); };
    mix(kArenaVersion.data(), kArenaVersion.size());
    mix(kFrameSimVersion.data(), kFrameSimVersion.size());
    mix(&c.seed, sizeof c.seed);
    mix_u32(c.width);
    mix_u32(c.height);
    mix_u32(c.frames);
    mix_u32(c.warmup_frames);
    mix(&c.warmup_s, sizeof c.warmup_s);
    mix(&c.measure_s, sizeof c.measure_s);
    mix_u32(c.sim ? 1u : 0u);
    mix_u32(c.sim_objects);
    mix_u32(c.sim ? (c.sim_workers ? c.sim_workers : 1u) : 0u);
    mix_u32(c.draw_mode == DrawMode::PerObject ? 1u : 0u);
    mix_u32(c.object_draws);
    mix_u32(c.arena_lights);
    mix_u32(c.arena_bloom ? 1u : 0u);
    mix_u32(c.arena_bloom ? kArenaBlurRadius : 0u);
    mix_u32(arena_sim_config(c).neighbors);
    return h;
}

}  // namespace ef::benchmark
