#include "benchmark/scene/frame_sim.h"

#include <algorithm>

namespace ef::benchmark {

namespace {

using i32 = std::int32_t;
using i64 = std::int64_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 kTunnelMask = static_cast<u32>(kTunnelLength) - 1u;
static_assert((static_cast<u32>(kTunnelLength) & kTunnelMask) == 0, "tunel uzunlugu ikinin kuvveti olmali");

// Suru kurali sabitleri (frame_sim_v1). Kaydirmalar isaretli tamsayida aritmetiktir (C++20).
constexpr int kCohesionShift = 9;                  // komsu ortalamasina cekim
constexpr int kAlignShift = 4;                     // komsu hiz ortalamasina yaklasma
constexpr i64 kSeparation = 3 * i64{kSimOne};      // Manhattan uzakligi bundan kucukse itme
constexpr int kSeparationShift = 5;
constexpr int kSwirlShift = 13;                    // eksen cevresinde donus (tegetsel)
constexpr int kRadialShift = 10;                   // baslangic yaricapina yay (yorunge kararli kalir)
constexpr int kLateralDragShift = 6;
constexpr int kWallShift = 4;
constexpr int kDriveShift = 4;                     // ileri hiz hedefine yaklasma
constexpr i64 kWander = 512;                       // kare tohumlu sapma: [-512, 511]
constexpr i32 kMaxLateral = kSimOne / 4;
constexpr i32 kMaxForward = kSimOne / 2;

constexpr u64 kSaltSchool = 0x5C4001A5C4001Aull;
constexpr u64 kSaltObject = 0x0B7EC70B7EC70Bull;
constexpr u64 kLcgMul = 6364136223846793005ull;
constexpr u64 kLcgAdd = 1442695040888963407ull;

// Altin deger: C profili, ilk kSimGoldenFrames kare (frame_sim_v1). Algoritma degisirse surumle birlikte degisir.
constexpr u64 kSimGolden = 0xACC0F5F683CA63AAull;

constexpr u64 mix64(u64 x) noexcept {  // splitmix64 sonlandiricisi
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

constexpr u64 fold(u64 acc, u64 value) noexcept {
    return mix64(acc ^ (value + 0x9E3779B97F4A7C15ull + (acc << 6) + (acc >> 2)));
}

constexpr u64 frame_seed(u64 seed, u64 frame) noexcept { return mix64(seed ^ mix64(frame + 1)); }

constexpr u64 pack(i32 a, i32 b) noexcept { return (static_cast<u64>(static_cast<u32>(a)) << 32) | static_cast<u32>(b); }

constexpr i32 wrap_delta(i32 d) noexcept {  // tunel dongusunde en kisa isaretli fark
    if (d > kTunnelLength / 2) return d - kTunnelLength;
    if (d < -kTunnelLength / 2) return d + kTunnelLength;
    return d;
}

constexpr i32 clamp_i32(i64 v, i32 limit) noexcept {
    return static_cast<i32>(v > limit ? limit : (v < -limit ? -limit : v));
}

// Eksene uzaklik (yaklasik oklid normu, tamsayi): max(a, 7a/8 + b/2); hata yaklasik %3.
constexpr i64 radial_norm(i32 x, i32 y) noexcept {
    const i64 ax = x < 0 ? -i64{x} : i64{x};
    const i64 ay = y < 0 ? -i64{y} : i64{y};
    const i64 a = ax > ay ? ax : ay, b = ax > ay ? ay : ax;
    const i64 blend = a - (a >> 3) + (b >> 1);
    return a > blend ? a : blend;
}

// 16 bitlik alan -> [-32768, 32767].
constexpr i32 centered16(u64 bits) noexcept { return static_cast<i32>(bits & 0xFFFFu) - 32768; }

}  // namespace

SimConfig sim_profile_c() noexcept { return SimConfig{}; }

std::uint32_t sim_worker_count(std::uint32_t logical_cores) noexcept {
    const u32 n = logical_cores > 1 ? logical_cores - 1 : 1;
    return n < 8 ? n : 8;
}

IndexRange worker_range(std::uint32_t objects, std::uint32_t workers, std::uint32_t worker) noexcept {
    const u64 w = workers ? workers : 1;
    if (worker >= w) return IndexRange{objects, objects};
    return IndexRange{static_cast<u32>(u64{objects} * worker / w), static_cast<u32>(u64{objects} * (u64{worker} + 1) / w)};
}

std::int32_t sim_camera_z(std::uint64_t frame) noexcept {
    return static_cast<i32>(static_cast<u32>(frame * static_cast<u64>(kCameraStep)) & kTunnelMask);
}

bool sim_visible(const SimObject& o, std::int32_t camera_z, std::uint32_t& depth) noexcept {
    const u32 dz = (static_cast<u32>(o.z) - static_cast<u32>(camera_z)) & kTunnelMask;
    if (dz < static_cast<u32>(kCameraNear) || dz > static_cast<u32>(kCameraFar)) return false;
    const i64 half_x = ((i64{dz} * kCameraTanXQ8) >> 8) + kCullRadius;
    const i64 half_y = ((i64{dz} * kCameraTanYQ8) >> 8) + kCullRadius;
    const i64 ax = o.x < 0 ? -i64{o.x} : i64{o.x};
    const i64 ay = o.y < 0 ? -i64{o.y} : i64{o.y};
    if (ax > half_x || ay > half_y) return false;
    depth = dz;
    return true;
}

std::uint64_t sim_object_hash(std::uint64_t seed, std::uint32_t index) noexcept {
    return mix64(seed ^ mix64(u64{index} + 1) ^ kSaltObject);
}

FrameSim::FrameSim(const SimConfig& config) : config_(config) {
    const u32 n = config_.objects;
    cur_.resize(n);
    next_.resize(n);
    target_vz_.resize(n);
    home_radius_.resize(n);
    for (u32 i = 0; i < n; ++i) {
        // Grup: ortak merkez, yon ve hiz.
        const u64 hs = mix64(config_.seed ^ mix64(u64{i / kSchoolSize} + 1) ^ kSaltSchool);
        const i32 cz = static_cast<i32>(static_cast<u32>(hs) & kTunnelMask);
        const i32 cx = centered16(hs >> 25) * 24;  // +-12 birim
        const i32 cy = centered16(hs >> 41) * 24;
        const i32 dir = ((hs >> 57) & 1u) != 0 ? 1 : -1;
        const i32 speed = 5243 + static_cast<i32>((hs >> 58) & 63u) * 125;  // 0,08 .. 0,20 birim/kare
        // Uye: merkez cevresinde dagilim ve kucuk kisisel hiz farki.
        const u64 ho = sim_object_hash(config_.seed, i);
        SimObject o{};
        o.x = clamp_i32(i64{cx} + centered16(ho) * 8, kTunnelHardRadius);  // +-4 birim
        o.y = clamp_i32(i64{cy} + centered16(ho >> 16) * 8, kTunnelHardRadius);
        o.z = static_cast<i32>(static_cast<u32>(cz + centered16(ho >> 32) * 20) & kTunnelMask);  // +-10 birim
        target_vz_[i] = dir * (speed + (static_cast<i32>((ho >> 48) & 0xFFu) - 128) * 4);
        o.vz = target_vz_[i];
        home_radius_[i] = static_cast<i32>(radial_norm(o.x, o.y));
        cur_[i] = o;
    }
}

void FrameSim::step_range(IndexRange range) noexcept {
    const u32 n = config_.objects;
    const u32 end = range.end < n ? range.end : n;
    const u32 neighbors = config_.neighbors;
    const u64 fs = frame_seed(config_.seed, frame_);
    const SimObject* cur = cur_.data();
    for (u32 i = range.begin; i < end; ++i) {
        const SimObject o = cur[i];
        u64 s = fs ^ (u64{i} * 0x9E3779B97F4A7C15ull);
        // Grup: ardisik kSchoolSize indeks (son grup eksik olabilir). Tek uyeli grubun komsusu yoktur.
        const u32 base = i - i % kSchoolSize;
        const u32 size = n - base < kSchoolSize ? n - base : kSchoolSize;
        const u32 others = size - 1;
        const u32 k = others ? neighbors : 0;
        i64 cx = 0, cy = 0, cz = 0, sx = 0, sy = 0, sz = 0, px = 0, py = 0, pz = 0;
        for (u32 j = 0; j < k; ++j) {
            s = s * kLcgMul + kLcgAdd;
            // Kare tohumlu komsu: ayni gruptan, kendisi haric (carpma-yuksek ile [0, others) araligi).
            u32 local = i - base + 1 + static_cast<u32>((u64{static_cast<u32>(s >> 32)} * others) >> 32);
            if (local >= size) local -= size;
            const u32 idx = base + local;
            const SimObject& b = cur[idx];
            const i32 dx = b.x - o.x, dy = b.y - o.y, dz = wrap_delta(b.z - o.z);
            cx += dx;
            cy += dy;
            cz += dz;
            sx += b.vx;
            sy += b.vy;
            sz += b.vz;
            const i64 dist = (dx < 0 ? -i64{dx} : i64{dx}) + (dy < 0 ? -i64{dy} : i64{dy}) + (dz < 0 ? -i64{dz} : i64{dz});
            if (dist < kSeparation) {
                px -= dx;
                py -= dy;
                pz -= dz;
            }
        }
        i64 ax = 0, ay = 0, az = 0;
        if (k) {
            const i64 kk = k;
            ax = ((cx / kk) >> kCohesionShift) + (((sx / kk) - o.vx) >> kAlignShift) + (px >> kSeparationShift);
            ay = ((cy / kk) >> kCohesionShift) + (((sy / kk) - o.vy) >> kAlignShift) + (py >> kSeparationShift);
            az = ((cz / kk) >> kCohesionShift) + (((sz / kk) - o.vz) >> kAlignShift) + (pz >> kSeparationShift);
        }
        // Eksen cevresinde donus + yanal surtunme; yaricap baslangic degerine yayla baglidir (suru zamanla
        // eksene cokmez ya da duvara yigilmaz: is yuku duragan kalir).
        ax += ((-i64{o.y}) >> kSwirlShift) - (i64{o.vx} >> kLateralDragShift);
        ay += (i64{o.x} >> kSwirlShift) - (i64{o.vy} >> kLateralDragShift);
        if (const i64 radius = radial_norm(o.x, o.y); radius > 0) {
            const i64 error = i64{home_radius_[i]} - radius;  // > 0: eksene fazla yakin
            ax += ((error * o.x) / radius) >> kRadialShift;
            ay += ((error * o.y) / radius) >> kRadialShift;
        }
        // Tunel duvari (yumusak).
        if (o.x > kTunnelRadius)
            ax -= (i64{o.x} - kTunnelRadius) >> kWallShift;
        else if (o.x < -kTunnelRadius)
            ax -= (i64{o.x} + kTunnelRadius) >> kWallShift;
        if (o.y > kTunnelRadius)
            ay -= (i64{o.y} - kTunnelRadius) >> kWallShift;
        else if (o.y < -kTunnelRadius)
            ay -= (i64{o.y} + kTunnelRadius) >> kWallShift;
        // Ileri hiz hedefi ve kare tohumlu sapma.
        az += (i64{target_vz_[i]} - o.vz) >> kDriveShift;
        s = s * kLcgMul + kLcgAdd;
        ax += static_cast<i64>((s >> 34) & 0x3FFu) - kWander;
        ay += static_cast<i64>((s >> 44) & 0x3FFu) - kWander;
        az += static_cast<i64>((s >> 54) & 0x3FFu) - kWander;

        SimObject r;
        r.vx = clamp_i32(i64{o.vx} + ax, kMaxLateral);
        r.vy = clamp_i32(i64{o.vy} + ay, kMaxLateral);
        r.vz = clamp_i32(i64{o.vz} + az, kMaxForward);
        r.x = clamp_i32(i64{o.x} + r.vx, kTunnelHardRadius);
        r.y = clamp_i32(i64{o.y} + r.vy, kTunnelHardRadius);
        r.z = static_cast<i32>(static_cast<u32>(o.z + r.vz) & kTunnelMask);
        next_[i] = r;
    }
}

void FrameSim::commit() noexcept {
    cur_.swap(next_);
    ++frame_;
}

void FrameSim::step(std::uint32_t partitions) noexcept {
    const u32 p = partitions ? partitions : 1;
    for (u32 w = 0; w < p; ++w) step_range(worker_range(config_.objects, p, w));
    commit();
}

void FrameSim::cull_and_sort(VisibleSet& out) const {
    out.keys.clear();
    const i32 camera_z = sim_camera_z(frame_);
    const u32 n = config_.objects;
    for (u32 i = 0; i < n; ++i) {
        u32 depth = 0;
        if (sim_visible(cur_[i], camera_z, depth)) out.keys.push_back((u64{depth} << 32) | i);
    }
    std::sort(out.keys.begin(), out.keys.end());  // anahtarlar tekil: siralama belirlenimci
    out.in_frustum = static_cast<u32>(out.keys.size());
    out.draws = out.in_frustum < config_.draw_budget ? out.in_frustum : config_.draw_budget;
}

std::uint64_t FrameSim::state_checksum() const noexcept {
    u64 acc = fold(0, cur_.size());
    for (const SimObject& o : cur_) {
        acc = fold(acc, pack(o.x, o.y));
        acc = fold(acc, pack(o.z, o.vx));
        acc = fold(acc, pack(o.vy, o.vz));
    }
    return acc;
}

std::uint64_t FrameSim::frame_checksum(const VisibleSet& visible) const noexcept {
    u64 acc = fold(fold(0, frame_), state_checksum());
    acc = fold(acc, visible.in_frustum);
    for (const u64 key : visible.drawn()) acc = fold(acc, key);
    return acc;
}

std::uint64_t sim_checksum_fold(std::uint64_t acc, std::uint64_t frame_checksum) noexcept { return fold(acc, frame_checksum); }

std::uint64_t sim_prefix_checksum(const SimConfig& config, std::uint32_t frames, std::uint32_t partitions) {
    FrameSim sim(config);
    VisibleSet visible;
    u64 acc = 0;
    for (u32 f = 0; f < frames; ++f) {
        sim.step(partitions);
        sim.cull_and_sort(visible);
        acc = fold(acc, sim.frame_checksum(visible));
    }
    return acc;
}

bool sim_golden_applies(const SimConfig& c) noexcept {
    const SimConfig p = sim_profile_c();
    return c.seed == p.seed && c.objects == p.objects && c.draw_budget == p.draw_budget && c.neighbors == p.neighbors;
}

std::uint64_t sim_golden_checksum() noexcept { return kSimGolden; }

}  // namespace ef::benchmark
