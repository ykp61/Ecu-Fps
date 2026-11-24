// BENCH-02: CPU is yuklerinin dogrulugu. Referans uygulama bu dosyada bagimsiz ve kasitli
// olarak basit yazilmistir (yapi dizisi, Bellman-Ford gevsetme, eklemeli siralama); urun kodunu
// cagirmaz. Boylece yukun gercekten hesaplandigi ve checksum'un dogru oldugu kanitlanir.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "benchmark/cpu/cpu_workload.h"
#include "ef_test.h"

namespace cpu = ef::benchmark::cpu;

namespace {

std::uint64_t ref_mix(std::uint64_t z) {
    z ^= z >> 30;
    z *= 0xBF58476D1CE4E5B9ULL;
    z ^= z >> 27;
    z *= 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return z;
}

struct RefRng {
    std::uint64_t s;
    std::uint64_t operator()() {
        s = s + 0x9E3779B97F4A7C15ULL;
        return ref_mix(s);
    }
};

std::uint64_t ref_fold(std::uint64_t a, std::uint64_t v) {
    const std::uint64_t t = v + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2);
    return ref_mix(a ^ t);
}

std::uint64_t ref_fnv(std::uint64_t h, std::int64_t v) {
    h ^= static_cast<std::uint32_t>(v);
    return h * 0x100000001B3ULL;
}

struct P {
    std::int64_t x, y, vx, vy;
};

std::int64_t trunc_div(std::int64_t a, std::int64_t b) {
    // C++ tamsayi bolmesi sifira dogru keser; ayni anlami acikca yaz.
    const std::int64_t q = (a < 0 ? -a : a) / b;
    return a < 0 ? -q : q;
}

void ref_axis(std::int64_t& p, std::int64_t& v, std::int64_t f) {
    const std::int64_t world = 1 << 20;
    v = v + f;
    v = v - trunc_div(v, 64);
    if (v > 4096) v = 4096;
    if (v < -4096) v = -4096;
    p = p + v;
    if (p < 0) {
        p = -p;
        v = -v;
    } else if (p >= world) {
        p = 2 * (world - 1) - p;
        v = -v;
    }
}

std::uint64_t ref_particles(std::uint64_t seed, std::uint32_t n, std::uint32_t steps) {
    RefRng r{seed};
    std::vector<P> ps(n);
    for (auto& p : ps) {
        p.x = static_cast<std::int64_t>(r() % (1u << 20));
        p.y = static_cast<std::int64_t>(r() % (1u << 20));
        p.vx = static_cast<std::int64_t>(r() % 2049) - 1024;
        p.vy = static_cast<std::int64_t>(r() % 2049) - 1024;
    }
    for (std::uint32_t s = 0; s < steps && n > 0; ++s) {
        const std::vector<P> old = ps;
        for (std::uint32_t i = 0; i < n; ++i) {
            const P& a = old[(i + 1) % n];
            const P& b = old[(i + n - 1) % n];
            const P& c = old[i];
            const std::int64_t fx = trunc_div(a.x - c.x, 256) + trunc_div(b.x - c.x, 256);
            const std::int64_t fy = trunc_div(a.y - c.y, 256) + trunc_div(b.y - c.y, 256) - 3;
            ref_axis(ps[i].x, ps[i].vx, fx);
            ref_axis(ps[i].y, ps[i].vy, fy);
        }
    }
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const auto& p : ps) {
        h = ref_fnv(h, p.x);
        h = ref_fnv(h, p.y);
        h = ref_fnv(h, p.vx);
        h = ref_fnv(h, p.vy);
    }
    return h;
}

// Kuyruksuz Bellman-Ford tarzi gevsetme: degisiklik kalmayana kadar tum hucreleri tara.
std::uint64_t ref_path(std::uint64_t seed, std::uint32_t g) {
    const std::uint32_t cells = g * g;
    RefRng r{seed};
    std::vector<bool> wall(cells);
    for (std::uint32_t i = 0; i < cells; ++i) wall[i] = (r() % 100) < 28;
    wall[0] = false;
    wall[cells - 1] = false;
    const std::int64_t inf = 1LL << 40;
    std::vector<std::int64_t> d(cells, inf);
    d[0] = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t y = 0; y < g; ++y)
            for (std::uint32_t x = 0; x < g; ++x) {
                const std::uint32_t i = y * g + x;
                if (wall[i]) continue;
                std::int64_t best = d[i];
                if (x > 0 && d[i - 1] + 1 < best) best = d[i - 1] + 1;
                if (x + 1 < g && d[i + 1] + 1 < best) best = d[i + 1] + 1;
                if (y > 0 && d[i - g] + 1 < best) best = d[i - g] + 1;
                if (y + 1 < g && d[i + g] + 1 < best) best = d[i + g] + 1;
                if (best < d[i]) {
                    d[i] = best;
                    changed = true;
                }
            }
    }
    std::uint64_t acc = 0, reached = 0;
    for (std::uint32_t i = 0; i < cells; ++i)
        if (d[i] < inf) {
            acc += static_cast<std::uint64_t>(d[i] + 1) * (i + 1ULL);
            ++reached;
        }
    return ref_fold(acc, reached);
}

std::uint64_t ref_sort(std::uint64_t seed, std::uint32_t n) {
    RefRng r{seed};
    std::vector<std::uint32_t> v(n);
    for (auto& e : v) e = static_cast<std::uint32_t>(r() >> 32);
    for (std::size_t i = 1; i < v.size(); ++i) {  // eklemeli siralama
        const std::uint32_t k = v[i];
        std::size_t j = i;
        while (j > 0 && v[j - 1] > k) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = k;
    }
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (auto e : v) h = ref_fnv(h, e);
    return h;
}

std::uint64_t ref_chunk(std::uint64_t seed, std::uint64_t index, const cpu::WorkloadSizes& s) {
    const std::uint64_t cs = ref_mix(seed ^ ref_mix(index + 1));
    std::uint64_t h = ref_fold(0, cs);
    h = ref_fold(h, ref_particles(cs ^ 1, s.particles, s.physics_steps));
    h = ref_fold(h, ref_path(cs ^ 2, s.grid_side));
    h = ref_fold(h, ref_sort(cs ^ 3, s.sort_elements));
    return h;
}

// --- cpu_v2 referanslari. FP referanslari ayni islem sirasini (IEEE, + - * / sqrt) yapi dizisiyle
// yeniden yazar; bigdata referansi std::sort + std::unordered_map ile tamamen farkli bir yoldur.

double ref_unit(RefRng& r) { return static_cast<double>(r() >> 11) * (1.0 / 9007199254740992.0); }

std::uint64_t ref_fnv_d(std::uint64_t h, double d) {
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof u);
    h ^= u;
    return h * 0x100000001B3ULL;
}

struct Body {
    double p[3], v[3], m;
};

std::uint64_t ref_nbody(std::uint64_t seed, std::uint32_t n, std::uint32_t steps) {
    RefRng r{seed};
    std::vector<Body> b(n);
    for (auto& x : b) {
        for (double& c : x.p) c = ref_unit(r) * 2.0 - 1.0;
        for (double& c : x.v) c = (ref_unit(r) - 0.5) * 0.1;
        x.m = 0.5 + ref_unit(r);
    }
    for (std::uint32_t s = 0; s < steps; ++s) {
        std::vector<std::array<double, 3>> acc(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            double a[3] = {0.0, 0.0, 0.0};
            for (std::uint32_t j = 0; j < n; ++j) {
                if (i == j) continue;
                const double d0 = b[j].p[0] - b[i].p[0];
                const double d1 = b[j].p[1] - b[i].p[1];
                const double d2 = b[j].p[2] - b[i].p[2];
                const double r2 = d0 * d0 + d1 * d1 + d2 * d2 + 0.01;
                const double inv = b[j].m / (r2 * std::sqrt(r2));
                a[0] += d0 * inv;
                a[1] += d1 * inv;
                a[2] += d2 * inv;
            }
            acc[i] = {a[0], a[1], a[2]};
        }
        for (std::uint32_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) b[i].v[c] += acc[i][static_cast<std::size_t>(c)] * 0.001;
        for (std::uint32_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) b[i].p[c] += b[i].v[c] * 0.001;
    }
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const auto& x : b) {
        for (double c : x.p) h = ref_fnv_d(h, c);
        for (double c : x.v) h = ref_fnv_d(h, c);
    }
    return h;
}

// Saf i-j-k carpim (k artan); urunun bloklu i-k-j surumuyle bit-ayni olmali.
std::uint64_t ref_matmul(std::uint64_t seed, std::uint32_t n) {
    RefRng r{seed};
    std::vector<double> a(static_cast<std::size_t>(n) * n), b(a.size());
    for (auto& x : a) x = ref_unit(r) * 2.0 - 1.0;
    for (auto& x : b) x = ref_unit(r) * 2.0 - 1.0;
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (std::uint32_t i = 0; i < n; ++i)
        for (std::uint32_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::uint32_t k = 0; k < n; ++k) s += a[static_cast<std::size_t>(i) * n + k] * b[static_cast<std::size_t>(k) * n + j];
            h = ref_fnv_d(h, s);
        }
    return h;
}

struct V3 {
    double x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct Ball {
    V3 c;
    double r, refl;
};

double ref_hit(const Ball& s, V3 o, V3 d) {
    const V3 l{o.x - s.c.x, o.y - s.c.y, o.z - s.c.z};
    const double bb = dot(l, d);
    const double cc = dot(l, l) - s.r * s.r;
    const double disc = bb * bb - cc;
    if (disc <= 0.0) return -1.0;
    double t = -bb - std::sqrt(disc);
    if (t <= 1e-6) t = -bb + std::sqrt(disc);
    return t > 1e-6 ? t : -1.0;
}

std::uint64_t ref_ray(std::uint64_t seed, std::uint32_t w, std::uint32_t hgt, std::uint32_t ns) {
    RefRng r{seed};
    std::vector<Ball> sp(ns);
    for (auto& s : sp) {
        s.c.x = ref_unit(r) * 8.0 - 4.0;
        s.c.y = ref_unit(r) * 6.0 - 3.0;
        s.c.z = 6.0 + ref_unit(r) * 10.0;
        s.r = 0.3 + ref_unit(r) * 1.2;
        s.refl = ref_unit(r) * 0.5;
    }
    const double li = 1.0 / std::sqrt(0.4 * 0.4 + 0.8 * 0.8 + 0.45 * 0.45);
    const V3 light{-0.4 * li, 0.8 * li, -0.45 * li};
    std::uint64_t h = 0xCBF29CE484222325ULL;
    std::uint32_t hits = 0;
    for (std::uint32_t py = 0; py < hgt; ++py)
        for (std::uint32_t px = 0; px < w; ++px) {
            V3 d{(2.0 * (px + 0.5) / w - 1.0) * (static_cast<double>(w) / hgt), 1.0 - 2.0 * (py + 0.5) / hgt, 1.5};
            d = d * (1.0 / std::sqrt(dot(d, d)));
            V3 o{0.0, 0.0, 0.0};
            double color = 0.0, weight = 1.0;
            for (int bounce = 0; bounce < 2; ++bounce) {
                double best = 1e30;
                int hit = -1;
                for (std::uint32_t s = 0; s < ns; ++s) {
                    const double t = ref_hit(sp[s], o, d);
                    if (t > 0.0 && t < best) best = t, hit = static_cast<int>(s);
                }
                if (hit < 0) {
                    color += weight * (0.2 + 0.3 * d.y);
                    break;
                }
                ++hits;
                const Ball& s0 = sp[static_cast<std::size_t>(hit)];
                const V3 hp = o + d * best;
                const V3 n{(hp.x - s0.c.x) / s0.r, (hp.y - s0.c.y) / s0.r, (hp.z - s0.c.z) / s0.r};
                double diff = dot(n, light);
                if (diff < 0.0) diff = 0.0;
                for (std::uint32_t s = 0; s < ns && diff > 0.0; ++s)
                    if (static_cast<int>(s) != hit && ref_hit(sp[s], hp, light) > 0.0) diff = 0.0;
                color += weight * (1.0 - s0.refl) * (0.1 + 0.9 * diff);
                weight *= s0.refl;
                const double dn2 = 2.0 * dot(d, n);
                d = V3{d.x - n.x * dn2, d.y - n.y * dn2, d.z - n.z * dn2};
                o = hp + n * 1e-4;
            }
            const double q = std::min(255.0, std::max(0.0, color * 255.0));
            h = ref_fnv(h, static_cast<std::uint32_t>(q));
        }
    return ref_fold(h, hits);
}

std::uint64_t ref_bigdata(std::uint64_t seed, std::uint32_t e) {
    if (e == 0) return ref_fold(0, 0);
    RefRng r{seed};
    std::vector<std::uint32_t> keys(e);
    for (auto& k : keys) k = static_cast<std::uint32_t>(r() >> 32);
    std::sort(keys.begin(), keys.end());
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (auto k : keys) h = ref_fnv(h, k);
    const std::uint64_t domain = 2ull * e;
    const std::uint32_t build = e / 2 > 0 ? e / 2 : 1;
    std::unordered_map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> tab;  // anahtar -> (adet, yuk toplami)
    RefRng rb{seed ^ 0x5BD1E995ULL};
    for (std::uint32_t i = 0; i < build; ++i) {
        const auto key = static_cast<std::uint32_t>(rb() % domain);
        const auto payload = static_cast<std::uint32_t>(rb() >> 32);
        auto& slot = tab[key];
        slot.first += 1;
        slot.second += payload;
    }
    std::uint64_t matches = 0, psum = 0;
    for (auto k : keys) {
        const auto it = tab.find(static_cast<std::uint32_t>(k % domain));
        if (it == tab.end()) continue;
        matches += it->second.first;
        psum += it->second.second;
    }
    return ref_fold(ref_fold(h, matches), psum);
}

std::uint64_t ref_salt(cpu::Kernel k) {
    switch (k) {
        case cpu::Kernel::NBody: return 0xA5A5000100000001ULL;
        case cpu::Kernel::MatMul: return 0xA5A5000200000002ULL;
        case cpu::Kernel::Ray: return 0xA5A5000300000003ULL;
        case cpu::Kernel::BigData: return 0xA5A5000400000004ULL;
        default: return 0;
    }
}

std::uint64_t ref_unit_checksum(cpu::Kernel k, std::uint64_t seed, std::uint64_t index, const cpu::WorkloadSizes& s) {
    if (k == cpu::Kernel::Sim) return ref_chunk(seed, index, s);
    const std::uint64_t us = ref_mix((seed ^ ref_salt(k)) ^ ref_mix(index + 1));
    std::uint64_t v = 0;
    switch (k) {
        case cpu::Kernel::NBody: v = ref_nbody(us, s.nbody_bodies, s.nbody_steps); break;
        case cpu::Kernel::MatMul: v = ref_matmul(us, s.matrix_n); break;
        case cpu::Kernel::Ray: v = ref_ray(us, s.ray_width, s.ray_height, s.ray_spheres); break;
        case cpu::Kernel::BigData: v = ref_bigdata(us, s.big_elements); break;
        default: break;
    }
    return ref_fold(ref_fold(0, us), v);
}

cpu::WorkloadSizes small_sizes() {
    cpu::WorkloadSizes s;
    s.particles = 37;
    s.physics_steps = 9;
    s.grid_side = 13;
    s.sort_elements = 301;
    s.nbody_bodies = 17;
    s.nbody_steps = 3;
    s.matrix_n = 45;  // 32'nin kati degil: blok kenarlari sinanir
    s.ray_width = 13;
    s.ray_height = 9;
    s.ray_spheres = 7;
    s.big_elements = 1000;
    return s;
}

}  // namespace

EF_TEST(cpu_workloads_match_independent_reference) {
    const std::uint64_t seeds[] = {1, 0xDEADBEEF, cpu::kDefaultSeed};
    for (auto sd : seeds) {
        EF_CHECK(cpu::particle_workload(sd, 37, 9) == ref_particles(sd, 37, 9));
        EF_CHECK(cpu::particle_workload(sd, 2, 50) == ref_particles(sd, 2, 50));
        EF_CHECK(cpu::path_workload(sd, 13) == ref_path(sd, 13));
        EF_CHECK(cpu::path_workload(sd, 1) == ref_path(sd, 1));
        EF_CHECK(cpu::sort_workload(sd, 301) == ref_sort(sd, 301));
        EF_CHECK(cpu::fold(sd, 7) == ref_fold(sd, 7));
    }
    const auto s = small_sizes();
    for (std::uint64_t i = 0; i < 5; ++i)
        EF_CHECK(cpu::chunk_checksum(42, i, s) == ref_chunk(42, i, s));
    // Farkli parcalar farkli sonuc verir (sabit/bos hesap degil).
    EF_CHECK(cpu::chunk_checksum(42, 0, s) != cpu::chunk_checksum(42, 1, s));
}

EF_TEST(cpu_v2_kernels_match_independent_reference) {
    const std::uint64_t seeds[] = {1, 0xDEADBEEF, cpu::kDefaultSeed};
    for (auto sd : seeds) {
        EF_CHECK(cpu::nbody_workload(sd, 17, 3) == ref_nbody(sd, 17, 3));
        EF_CHECK(cpu::nbody_workload(sd, 1, 2) == ref_nbody(sd, 1, 2));
        EF_CHECK(cpu::matmul_workload(sd, 45) == ref_matmul(sd, 45));
        EF_CHECK(cpu::matmul_workload(sd, 64) == ref_matmul(sd, 64));
        EF_CHECK(cpu::ray_workload(sd, 13, 9, 7) == ref_ray(sd, 13, 9, 7));
        EF_CHECK(cpu::ray_workload(sd, 32, 20, 12) == ref_ray(sd, 32, 20, 12));
        EF_CHECK(cpu::bigdata_workload(sd, 1000) == ref_bigdata(sd, 1000));
        EF_CHECK(cpu::bigdata_workload(sd, 1) == ref_bigdata(sd, 1));
        EF_CHECK(cpu::bigdata_workload(sd, 4096) == ref_bigdata(sd, 4096));
    }
    const auto s = small_sizes();
    for (std::uint32_t k = 0; k < cpu::kKernelCount; ++k) {
        const auto kern = static_cast<cpu::Kernel>(k);
        for (std::uint64_t i = 0; i < 3; ++i)
            EF_CHECK(cpu::unit_checksum(kern, 42, i, s) == ref_unit_checksum(kern, 42, i, s));
        EF_CHECK(cpu::unit_checksum(kern, 42, 0, s) != cpu::unit_checksum(kern, 42, 1, s));
    }
    EF_CHECK(cpu::unit_checksum(cpu::Kernel::Sim, 42, 2, s) == cpu::chunk_checksum(42, 2, s));
    EF_CHECK(cpu::bigdata_workload(7, 1000) != cpu::bigdata_workload(8, 1000));
}

EF_TEST(cpu_golden_values_match_reference_default_sizes) {
    const cpu::WorkloadSizes def{};
    for (std::uint32_t i = 0; i < cpu::kGoldenCount; ++i) {
        const std::uint64_t ref = ref_chunk(cpu::kDefaultSeed, i, def);
        if (ref != cpu::golden_chunk_checksum(i))
            std::fprintf(stderr, "  golden[%u] beklenen 0x%016" PRIX64 "\n", i, ref);
        EF_CHECK(ref == cpu::golden_chunk_checksum(i));
        EF_CHECK(cpu::chunk_checksum(cpu::kDefaultSeed, i, def) == ref);
        EF_CHECK(cpu::golden_unit_checksum(cpu::Kernel::Sim, i) == ref);
    }
    for (std::uint32_t k = 1; k < cpu::kKernelCount; ++k) {
        const auto kern = static_cast<cpu::Kernel>(k);
        for (std::uint32_t i = 0; i < cpu::kGoldenUnits; ++i) {
            const std::uint64_t ref = ref_unit_checksum(kern, cpu::kDefaultSeed, i, def);
            if (ref != cpu::golden_unit_checksum(kern, i))
                std::fprintf(stderr, "  golden %s[%u] beklenen 0x%016" PRIX64 "ULL\n",
                             std::string(cpu::kernel_name(kern)).c_str(), i, ref);
            EF_CHECK(ref == cpu::golden_unit_checksum(kern, i));
            EF_CHECK(cpu::unit_checksum(kern, cpu::kDefaultSeed, i, def) == ref);
        }
    }
}

EF_TEST(cpu_sim_only_mask_matches_v1_reference) {
    cpu::CpuConfig c;
    c.sizes = small_sizes();
    c.max_chunks = 64;
    c.time_limit_s = 30.0;  // sinir parca sayisidir; sure siniri devreye girmez
    c.kernel_mask = cpu::kernel_bit(cpu::Kernel::Sim);
    std::atomic<bool> cancel{false};
    const auto st = cpu::run_variant(c, 1, cancel);
    const auto mt = cpu::run_variant(c, 4, cancel);
    EF_CHECK(st.chunks == 64);
    EF_CHECK(mt.chunks == 64);
    EF_CHECK(st.threads == 1);
    EF_CHECK(mt.threads == 4);
    EF_CHECK(st.kernels.size() == 1 && mt.kernels.size() == 1);
    std::uint64_t acc = 0;
    for (std::uint64_t i = 0; i < 64; ++i) acc = ref_fold(acc, ref_chunk(c.seed, i, c.sizes));
    if (st.kernels.size() == 1 && mt.kernels.size() == 1) {
        EF_CHECK(st.kernels[0].kernel == cpu::Kernel::Sim);
        EF_CHECK(st.kernels[0].checksum == acc);
        EF_CHECK(mt.kernels[0].checksum == acc);
        EF_CHECK(st.kernels[0].units_per_second > 0.0);
        // Tek cekirdekte geometrik ortalama = cekirdek hizi.
        EF_CHECK(std::fabs(st.work_units_per_second - st.kernels[0].units_per_second) <= 1e-9 * st.work_units_per_second);
    }
    EF_CHECK(st.checksum == mt.checksum);
    EF_CHECK(st.checksum == ref_fold(0, acc));
    EF_CHECK(!st.cancelled && !mt.cancelled);
}

EF_TEST(cpu_mt_checksum_equals_st_for_same_work_all_kernels) {
    cpu::CpuConfig c;
    c.sizes = small_sizes();
    c.max_chunks = 24;
    c.time_limit_s = 30.0;
    std::atomic<bool> cancel{false};
    const auto st = cpu::run_variant(c, 1, cancel);
    const auto mt = cpu::run_variant(c, 4, cancel);
    EF_CHECK(st.kernels.size() == cpu::kKernelCount);
    EF_CHECK(mt.kernels.size() == cpu::kKernelCount);
    EF_CHECK(st.chunks == 24ull * cpu::kKernelCount);
    EF_CHECK(mt.chunks == 24ull * cpu::kKernelCount);
    EF_CHECK(st.checksum == mt.checksum);
    std::uint64_t total = 0;
    double log_sum = 0.0;
    for (std::size_t k = 0; k < st.kernels.size() && k < mt.kernels.size(); ++k) {
        const auto kern = static_cast<cpu::Kernel>(k);
        std::uint64_t acc = 0;
        for (std::uint64_t i = 0; i < 24; ++i) acc = ref_fold(acc, ref_unit_checksum(kern, c.seed, i, c.sizes));
        EF_CHECK(st.kernels[k].kernel == kern);
        EF_CHECK(st.kernels[k].units == 24 && mt.kernels[k].units == 24);
        EF_CHECK(st.kernels[k].checksum == acc);
        EF_CHECK(mt.kernels[k].checksum == acc);
        EF_CHECK(st.kernels[k].units_per_second > 0.0 && mt.kernels[k].units_per_second > 0.0);
        total = ref_fold(total, acc);
        log_sum += std::log(st.kernels[k].units_per_second);
    }
    EF_CHECK(st.checksum == total);
    const double geo = std::exp(log_sum / cpu::kKernelCount);
    EF_CHECK(std::fabs(st.work_units_per_second - geo) <= 1e-9 * geo);
    EF_CHECK(!st.cancelled && !mt.cancelled);
}

EF_TEST(cpu_presets_and_variant_seconds) {
    const auto q = cpu::quick_cpu_config();
    const auto f = cpu::full_cpu_config();
    EF_CHECK(q.st_seconds == 5.0 && q.mt_seconds == 10.0);
    EF_CHECK(f.st_seconds == 20.0 && f.mt_seconds == 60.0);
    EF_CHECK(cpu::variant_seconds(q, false) == 5.0 && cpu::variant_seconds(q, true) == 10.0);
    EF_CHECK(cpu::variant_seconds(f, false) == 20.0 && cpu::variant_seconds(f, true) == 60.0);
    EF_CHECK(q.kernel_mask == cpu::kAllKernels && f.kernel_mask == cpu::kAllKernels);
    EF_CHECK(q.threads == 0 && f.threads == 0);  // tum mantiksal islemciler
    EF_CHECK(q.seed == cpu::kDefaultSeed && q.sizes == cpu::WorkloadSizes{});
    // Eski alan: st/mt 0 ise time_limit_s gecerli.
    cpu::CpuConfig legacy;
    legacy.time_limit_s = 3.5;
    EF_CHECK(cpu::variant_seconds(legacy, false) == 3.5 && cpu::variant_seconds(legacy, true) == 3.5);
    // Varsayilan bigdata calisma kumesi L2'yi asar: anahtar + gecici + tablo >= 4 MiB.
    const cpu::WorkloadSizes d{};
    EF_CHECK(static_cast<std::uint64_t>(d.big_elements) * 4 * 2 + static_cast<std::uint64_t>(d.big_elements) * 8 >= (4ull << 20));
    EF_CHECK(cpu::kernel_name(cpu::Kernel::Sim) == "sim");
    EF_CHECK(cpu::kernel_name(cpu::Kernel::NBody) == "nbody");
    EF_CHECK(cpu::kernel_name(cpu::Kernel::MatMul) == "matmul");
    EF_CHECK(cpu::kernel_name(cpu::Kernel::Ray) == "ray");
    EF_CHECK(cpu::kernel_name(cpu::Kernel::BigData) == "bigdata");
}

EF_TEST(cpu_bigdata_memory_cap_and_validation) {
    // Varsayilan: 2^19 eleman -> 4 MiB anahtar+gecici + 4 MiB tablo = 8 MiB isci basina.
    EF_CHECK(cpu::bigdata_bytes_per_worker(1u << 19) == 8ull * 1024 * 1024);
    EF_CHECK(cpu::bigdata_bytes_per_worker(1000) == 8000 + 8 * 1024);
    EF_CHECK(cpu::bigdata_bytes_per_worker(0) == 0);
    EF_CHECK(cpu::bigdata_thread_cap(8, 1u << 19, 20ull << 20) == 2);
    EF_CHECK(cpu::bigdata_thread_cap(8, 1u << 19, 0) == 1);  // butce yok: en az 1 isci
    EF_CHECK(cpu::bigdata_thread_cap(8, 1u << 19, 1ull << 30) == 8);
    EF_CHECK(cpu::bigdata_thread_cap(0, 1u << 19, 1ull << 30) == 1);
    // Dogrulama: big_elements < 2^31.
    cpu::CpuConfig big;
    big.sizes.big_elements = 0xFFFFFFFFu;
    EF_CHECK(cpu::validated_config(big).sizes.big_elements == cpu::kMaxBigElements);
    static_assert(cpu::kMaxBigElements < 0x80000000u);
    const cpu::CpuConfig def;
    EF_CHECK(cpu::validated_config(def).sizes == def.sizes);

    // Butce 2 isciye yetiyor: 4 is parcacikli varyantta bigdata 2 isciyle calisir, checksum ayni kalir.
    cpu::CpuConfig c;
    c.sizes = small_sizes();
    c.max_chunks = 16;
    c.time_limit_s = 30.0;
    c.kernel_mask = cpu::kernel_bit(cpu::Kernel::BigData) | cpu::kernel_bit(cpu::Kernel::Sim);
    c.bigdata_budget_bytes = 2 * cpu::bigdata_bytes_per_worker(c.sizes.big_elements) + 100;
    std::atomic<bool> cancel{false};
    const auto st = cpu::run_variant(c, 1, cancel);
    const auto mt = cpu::run_variant(c, 4, cancel);
    EF_CHECK(st.kernels.size() == 2 && mt.kernels.size() == 2);
    if (st.kernels.size() == 2 && mt.kernels.size() == 2) {
        EF_CHECK(mt.kernels[0].kernel == cpu::Kernel::Sim && mt.kernels[0].threads == 4);
        EF_CHECK(mt.kernels[1].kernel == cpu::Kernel::BigData && mt.kernels[1].threads == 2);
        EF_CHECK(st.kernels[1].threads == 1);
        EF_CHECK(mt.kernels[1].units == 16 && mt.kernels[1].checksum == st.kernels[1].checksum);
    }
    EF_CHECK(mt.memory_limited && !st.memory_limited);
    EF_CHECK(mt.threads == 4);  // istenen varyant is parcacigi sayisi degismez
    EF_CHECK(st.checksum == mt.checksum);
    // Cok kisa fazlarda (< kMinShareWallSeconds) CPU payi bilinmez: bos, cekisme iddiasi yok.
    for (const auto& k : mt.kernels) EF_CHECK(k.thread_cpu_share.empty() && !k.min_thread_cpu_share);
    EF_CHECK(!mt.min_thread_cpu_share && !mt.contended);
}

EF_TEST(cpu_bigdata_elements_shrink_when_one_worker_over_budget) {
    // Saf kural: sigiyorsa aynen; sigmiyorsa yarilanir, kMinBigElements altina inmez.
    EF_CHECK(cpu::bigdata_elements_for_budget(1u << 19, 1ull << 30) == (1u << 19));
    EF_CHECK(cpu::bigdata_elements_for_budget(1u << 19, 8ull << 20) == (1u << 19));       // tam 8 MiB sigar
    EF_CHECK(cpu::bigdata_elements_for_budget(1u << 19, (8ull << 20) - 1) == (1u << 18));
    EF_CHECK(cpu::bigdata_elements_for_budget(1u << 19, 0) == cpu::kMinBigElements);     // butce yok: en kucuk
    EF_CHECK(cpu::bigdata_elements_for_budget(1000, 0) == 1000);                          // zaten alt sinirin altinda
    EF_CHECK(cpu::bigdata_elements_for_budget(0xFFFFFFFFu, 1ull << 40) == cpu::kMaxBigElements);
    EF_CHECK(cpu::bigdata_bytes_per_worker(cpu::kMinBigElements) == 64ull * 1024);

    // Tek isci (ST dahil) butceyi asiyor: her iki varyant ayni kucultulmus eleman sayisiyla kosar, checksum esit.
    cpu::CpuConfig c;
    c.sizes = small_sizes();
    c.sizes.big_elements = 1u << 14;  // 256 KiB/isci
    c.max_chunks = 6;
    c.time_limit_s = 30.0;
    c.kernel_mask = cpu::kernel_bit(cpu::Kernel::BigData);
    c.bigdata_budget_bytes = 100 * 1024;  // 8192 -> 128 KiB > 100 KiB; 4096 -> 64 KiB sigar
    std::atomic<bool> cancel{false};
    const auto st = cpu::run_variant(c, 1, cancel);
    const auto mt = cpu::run_variant(c, 4, cancel);
    EF_CHECK(st.bigdata_elements == cpu::kMinBigElements && mt.bigdata_elements == cpu::kMinBigElements);
    EF_CHECK(st.memory_limited && mt.memory_limited);
    EF_CHECK(st.kernels.size() == 1 && mt.kernels.size() == 1);
    if (st.kernels.size() == 1 && mt.kernels.size() == 1) {
        EF_CHECK(st.kernels[0].units == 6 && mt.kernels[0].units == 6);
        EF_CHECK(mt.kernels[0].threads == 1);  // 100 KiB / 64 KiB = 1 isci
        EF_CHECK(st.kernels[0].checksum == mt.kernels[0].checksum);
        // Kucultulmus is yuku gercekten kucuk eleman sayisiyla hesaplandi.
        cpu::WorkloadSizes z = c.sizes;
        z.big_elements = cpu::kMinBigElements;
        EF_CHECK(cpu::unit_checksum(cpu::Kernel::BigData, c.seed, 0, z) !=
                  cpu::unit_checksum(cpu::Kernel::BigData, c.seed, 0, c.sizes));
    }
    // Tam benchmark: kucultme ST ve MT'de ayni; durum Ok (ortak onek tutarli), altin denetim atlanir.
    c.max_chunks = 3;
    const auto r = cpu::run_cpu_benchmark(c, cancel);
    EF_CHECK(r.status == cpu::CpuRunStatus::Ok && r.checksum_ok);
    EF_CHECK(r.single_thread.memory_limited && r.multi_thread.memory_limited);
    EF_CHECK(r.single_thread.bigdata_elements == cpu::kMinBigElements);
    // bigdata calismiyorsa butce okunmaz, kucultme/isaret yok.
    c.kernel_mask = cpu::kernel_bit(cpu::Kernel::Sim);
    const auto sim = cpu::run_variant(c, 1, cancel);
    EF_CHECK(!sim.memory_limited && sim.bigdata_elements == 0);
}

EF_TEST(cpu_bigdata_unknown_budget_keeps_default_size_and_reports) {
    // Butce bilinmiyor (compute_budget atladi): eleman sayisi KUCULTULMEZ; ST ayni is yukuyle kosar ve
    // memory_limited degildir. MT bigdata guvenli tarafta tek isciyle kosar ve memory_limited isaretlenir.
    cpu::CpuConfig c;
    c.sizes = small_sizes();
    c.sizes.big_elements = 1u << 14;
    c.max_chunks = 6;
    c.time_limit_s = 30.0;
    c.kernel_mask = cpu::kernel_bit(cpu::Kernel::BigData) | cpu::kernel_bit(cpu::Kernel::Sim);
    c.bigdata_budget_unknown = true;
    std::atomic<bool> cancel{false};
    const auto st = cpu::run_variant(c, 1, cancel);
    const auto mt = cpu::run_variant(c, 4, cancel);
    EF_CHECK(st.bigdata_elements == (1u << 14) && mt.bigdata_elements == (1u << 14));
    EF_CHECK(!st.bigdata_elements_reduced && !mt.bigdata_elements_reduced);
    EF_CHECK(!st.bigdata_budget_known && !mt.bigdata_budget_known);
    EF_CHECK(!st.memory_limited && mt.memory_limited);
    EF_CHECK(st.kernels.size() == 2 && mt.kernels.size() == 2);
    if (mt.kernels.size() == 2 && st.kernels.size() == 2) {
        EF_CHECK(mt.kernels[0].threads == 4);  // sim etkilenmez
        EF_CHECK(mt.kernels[1].kernel == cpu::Kernel::BigData && mt.kernels[1].threads == 1);
        EF_CHECK(mt.kernels[1].checksum == st.kernels[1].checksum);
    }
    // Butce biliniyor ve yeterli: isaret yok, butce olculdu.
    c.bigdata_budget_unknown = false;
    c.bigdata_budget_bytes = 1ull << 30;
    const auto ok = cpu::run_variant(c, 4, cancel);
    EF_CHECK(ok.bigdata_budget_known && !ok.memory_limited && !ok.bigdata_elements_reduced);
}

EF_TEST(cpu_golden_applies_per_kernel) {
    // bigdata kucultulmesi yalniz bigdata altin denetimini kapatir; diger cekirdekler denetlenir.
    const cpu::CpuConfig def;
    for (std::uint32_t k = 0; k < cpu::kKernelCount; ++k) EF_CHECK(cpu::golden_applies(static_cast<cpu::Kernel>(k), def));
    cpu::CpuConfig shrunk = def;
    shrunk.sizes.big_elements = cpu::kMinBigElements;
    EF_CHECK(!cpu::golden_applies(cpu::Kernel::BigData, shrunk));
    for (const auto k : {cpu::Kernel::Sim, cpu::Kernel::NBody, cpu::Kernel::MatMul, cpu::Kernel::Ray})
        EF_CHECK(cpu::golden_applies(k, shrunk));
    // Cekirdegin kendi boyutu degisirse yalniz o cekirdek; tohum degisirse hicbiri.
    cpu::CpuConfig mm = def;
    mm.sizes.matrix_n = 64;
    EF_CHECK(!cpu::golden_applies(cpu::Kernel::MatMul, mm) && cpu::golden_applies(cpu::Kernel::Ray, mm));
    cpu::CpuConfig seed = def;
    seed.seed ^= 1;
    for (std::uint32_t k = 0; k < cpu::kKernelCount; ++k) EF_CHECK(!cpu::golden_applies(static_cast<cpu::Kernel>(k), seed));

    // Varsayilan boyutlarla, bigdata butcesi tek isciye yetmiyor: bigdata kucultulur, diger dort cekirdegin altin
    // denetimi (on kontrol + kosu ici) yine calisir ve gecer.
    cpu::CpuConfig c;
    c.time_limit_s = 0.2;
    c.max_chunks = 2;
    c.bigdata_budget_bytes = 100 * 1024;
    std::atomic<bool> cancel{false};
    const auto r = cpu::run_cpu_benchmark(c, cancel);
    EF_CHECK(r.status == cpu::CpuRunStatus::Ok && r.checksum_ok);
    EF_CHECK(r.single_thread.bigdata_elements_reduced && r.single_thread.memory_limited && r.single_thread.bigdata_budget_known);
}

EF_TEST(cpu_contention_is_tristate) {
    cpu::CpuVariantResult v;
    EF_CHECK(cpu::contention(v) == cpu::Contention::Unknown);  // pay yok: cekismesiz DEGIL
    EF_CHECK(std::string(cpu::to_string(cpu::contention(v))) == "unknown");
    v.min_thread_cpu_share = 0.95;
    EF_CHECK(cpu::contention(v) == cpu::Contention::Uncontended);
    v.min_thread_cpu_share = 0.5;
    EF_CHECK(cpu::contention(v) == cpu::Contention::Contended);
    EF_CHECK(std::string(cpu::to_string(cpu::Contention::Contended)) == "contended");
    EF_CHECK(std::string(cpu::to_string(cpu::Contention::Uncontended)) == "uncontended");
}

EF_TEST(cpu_thread_cpu_share_measured_on_long_phase) {
    // Tek is parcacikli sim fazi ~0,3 sn: isci CPU payi olculur (0 < pay; tanecik nedeniyle ~1'i biraz asabilir).
    cpu::CpuConfig c;
    c.sizes = small_sizes();
    c.time_limit_s = 0.3;
    c.kernel_mask = cpu::kernel_bit(cpu::Kernel::Sim);
    std::atomic<bool> cancel{false};
    const auto st = cpu::run_variant(c, 1, cancel);
    EF_CHECK(st.kernels.size() == 1);
    if (st.kernels.size() == 1) {
        EF_CHECK(st.kernels[0].threads == 1);
        EF_CHECK(st.kernels[0].thread_cpu_share.size() == 1);
        EF_CHECK(st.kernels[0].min_thread_cpu_share.has_value());
        if (st.kernels[0].min_thread_cpu_share) {
            EF_CHECK(*st.kernels[0].min_thread_cpu_share > 0.0 && *st.kernels[0].min_thread_cpu_share < 1.5);
        }
    }
    EF_CHECK(st.min_thread_cpu_share.has_value());
    if (st.min_thread_cpu_share) EF_CHECK(st.contended == (*st.min_thread_cpu_share < cpu::kUncontendedThreadCpuShare));
}

EF_TEST(cpu_cancel_marks_cancelled_not_scored) {
    cpu::CpuConfig c;
    c.time_limit_s = 30.0;
    std::atomic<bool> cancel{true};
    const auto r = cpu::run_cpu_benchmark(c, cancel);
    EF_CHECK(r.cancelled);
    EF_CHECK(r.status == cpu::CpuRunStatus::Cancelled);
    EF_CHECK(!r.scored);
    EF_CHECK(!r.is_fps_prediction);

    // Kosu sirasinda iptal: ayri is parcacigi bayragi kaldirir.
    std::atomic<bool> cancel2{false};
    std::atomic<bool> started{false};
    cpu::CpuBenchmarkResult r2;
    const auto t0 = std::chrono::steady_clock::now();
    std::thread t([&] {
        started = true;
        r2 = cpu::run_cpu_benchmark(c, cancel2);
    });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel2 = true;
    t.join();
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EF_CHECK(r2.cancelled);
    EF_CHECK(!r2.scored);
    EF_CHECK(r2.single_thread.elapsed_s < 10.0);  // 30 sn siniri beklenmeden durdu
    EF_CHECK(s < 10.0);
}

EF_TEST(cpu_full_run_short_time_limit_scored) {
    cpu::CpuConfig c;
    c.time_limit_s = 0.5;
    std::atomic<bool> cancel{false};
    const auto r = cpu::run_cpu_benchmark(c, cancel);
    EF_CHECK(r.status == cpu::CpuRunStatus::Ok);
    EF_CHECK(r.checksum_ok && r.scored && !r.cancelled);
    EF_CHECK(r.workload_version == "cpu_v2");
    EF_CHECK(r.score_unit == "cpu_work_units_per_second");
    EF_CHECK(!r.is_fps_prediction);
    EF_CHECK(r.hardware_threads >= 1);
    EF_CHECK(r.single_thread.threads == 1);
    EF_CHECK(r.multi_thread.threads == r.hardware_threads);
    EF_CHECK(r.single_thread.chunks > 0 && r.multi_thread.chunks > 0);
    EF_CHECK(r.single_thread.elapsed_s > 0.0 && r.multi_thread.elapsed_s > 0.0);
    EF_CHECK(r.single_thread.work_units_per_second > 0.0);
    EF_CHECK(r.multi_thread.work_units_per_second > 0.0);
    EF_CHECK(r.single_thread.kernels.size() == cpu::kKernelCount);
    EF_CHECK(r.multi_thread.kernels.size() == cpu::kKernelCount);
    EF_CHECK(cpu::checksum_sink_value() != 0);
    std::printf("  cpu_v2: ST %.2f is/sn (%llu birim, %.3f sn), MT %.2f is/sn (%u is parcacigi, %llu birim, %.3f sn)\n",
                r.single_thread.work_units_per_second,
                static_cast<unsigned long long>(r.single_thread.chunks), r.single_thread.elapsed_s,
                r.multi_thread.work_units_per_second, r.multi_thread.threads,
                static_cast<unsigned long long>(r.multi_thread.chunks), r.multi_thread.elapsed_s);
}

EF_TEST(cpu_zero_time_limit_is_no_work) {
    // Sifir sure siniri -> hic parca tamamlanmaz; NoWork, puanlanmaz.
    cpu::CpuConfig c;
    c.time_limit_s = 0.0;
    std::atomic<bool> cancel{false};
    const auto r = cpu::run_cpu_benchmark(c, cancel);
    EF_CHECK(r.status == cpu::CpuRunStatus::NoWork);
    EF_CHECK(!r.scored && !r.checksum_ok);
    // Bos cekirdek maskesi de puanlanmaz.
    cpu::CpuConfig e;
    e.time_limit_s = 0.2;
    e.kernel_mask = 0;
    const auto r2 = cpu::run_cpu_benchmark(e, cancel);
    EF_CHECK(r2.status == cpu::CpuRunStatus::NoWork && !r2.scored);
}

