#include "benchmark/cpu/cpu_workload.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include "benchmark/cpu/cpu_groups.h"
#include "benchmark/memory/memory_bench.h"

// FP sonuclari bit-ayni kalsin: a*b+c FMA'ya birlestirilmez (MSVC'de /fp:contract- yok; pragma kullanilir).
#pragma fp_contract(off)

namespace ef::benchmark::cpu {

namespace {

// Optimizer checksum hesaplarini olu kod diye atamasin: sonuclar atomik havuza yazilir.
std::atomic<std::uint64_t> g_checksum_sink{0};

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr std::int32_t kWorld = 1 << 20;     // parcacik dunyasi [0, kWorld)
constexpr std::int32_t kMaxSpeed = 4096;     // hiz siniri (yansima sonrasi dunyada kalir)
constexpr std::int32_t kGravity = 3;
constexpr std::uint32_t kObstaclePct = 28;   // izgara engel orani (%)

// Varsayilan tohum + varsayilan boyutlar icin altin parca checksum'lari (sim; cpu_v1 ile ayni).
// Birim test bu degerleri is yukunden bagimsiz basit bir uygulamayla yeniden uretir.
constexpr std::uint64_t kGolden[kGoldenCount] = {
    0x11007B8B17B31298ULL,
    0x0F03E0656E916CDFULL,
    0x48B1B636D96B57CEULL,
    0x14D338AB0593DF08ULL,
};

// Varsayilan tohum + boyutlar icin diger cekirdeklerin ilk birim checksum'lari (cpu_v2).
// Sira: nbody, matmul, ray, bigdata. Birim test bagimsiz referansla yeniden uretir.
constexpr std::uint64_t kGoldenUnits2[kKernelCount - 1][kGoldenUnits] = {
    {0x19A57596E4B2AFD1ULL, 0xB4B89EF45A63F719ULL},
    {0x6A487162CD0BD0B9ULL, 0x1E185C3699FFC19DULL},
    {0xA2BF85AC3FD75CB0ULL, 0x72FFF36A8E97DCB5ULL},
    {0x07FE0DDD5CFC2B06ULL, 0xB1B3E25C772C6327ULL},
};

std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// splitmix64 akisi: deterministik, platformdan bagimsiz.
struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        return mix64(state);
    }
    // [0, 1) araliginda 53 bitlik double.
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
};

std::uint64_t fnv_step(std::uint64_t h, std::uint32_t v) {
    return (h ^ v) * kFnvPrime;
}

std::uint64_t fnv_double(std::uint64_t h, double d) {
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof u);
    return (h ^ u) * kFnvPrime;
}

// Is parcacigina ozel tekrar kullanilan tamponlar (sim; her parcada heap tahsisi yapilmaz).
struct Scratch {
    std::vector<std::int32_t> x, y, vx, vy, fx, fy;
    std::vector<std::uint8_t> blocked;
    std::vector<std::int32_t> dist;
    std::vector<std::uint32_t> queue;
    std::vector<std::uint32_t> values;
};

Scratch& scratch() {
    thread_local Scratch s;
    return s;
}

struct Sphere {
    double cx, cy, cz, r, refl;
};

// FP ve buyuk calisma kumesi cekirdekleri icin isciye ait tampon. Isci bitince serbest kalir
// (thread_local degil: tek is parcacikli kosu cagiran is parcaciginda MiB'lerce bellek birakmaz).
struct Workspace {
    std::vector<double> fp;
    std::vector<std::uint32_t> keys, tmp;
    std::vector<std::uint64_t> table;
    std::vector<Sphere> spheres;
};

std::uint64_t nbody_impl(std::uint64_t seed, std::uint32_t bodies, std::uint32_t steps, Workspace& w) {
    const std::size_t n = bodies;
    w.fp.assign(10 * n, 0.0);
    double* px = w.fp.data();
    double* py = px + n;
    double* pz = py + n;
    double* vx = pz + n;
    double* vy = vx + n;
    double* vz = vy + n;
    double* m = vz + n;
    double* ax = m + n;
    double* ay = ax + n;
    double* az = ay + n;

    Rng rng{seed};
    for (std::size_t i = 0; i < n; ++i) {
        px[i] = rng.unit() * 2.0 - 1.0;
        py[i] = rng.unit() * 2.0 - 1.0;
        pz[i] = rng.unit() * 2.0 - 1.0;
        vx[i] = (rng.unit() - 0.5) * 0.1;
        vy[i] = (rng.unit() - 0.5) * 0.1;
        vz[i] = (rng.unit() - 0.5) * 0.1;
        m[i] = 0.5 + rng.unit();
    }
    constexpr double kEps2 = 0.01;
    constexpr double kDt = 0.001;
    for (std::uint32_t s = 0; s < steps; ++s) {
        // Ivmeler eski konumlardan (iki fazli guncelleme); toplama sirasi j artan.
        for (std::size_t i = 0; i < n; ++i) {
            const double xi = px[i], yi = py[i], zi = pz[i];
            double sx = 0.0, sy = 0.0, sz = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                if (j == i) continue;
                const double dx = px[j] - xi;
                const double dy = py[j] - yi;
                const double dz = pz[j] - zi;
                const double r2 = dx * dx + dy * dy + dz * dz + kEps2;
                const double inv = m[j] / (r2 * std::sqrt(r2));
                sx += dx * inv;
                sy += dy * inv;
                sz += dz * inv;
            }
            ax[i] = sx;
            ay[i] = sy;
            az[i] = sz;
        }
        for (std::size_t i = 0; i < n; ++i) {
            vx[i] += ax[i] * kDt;
            vy[i] += ay[i] * kDt;
            vz[i] += az[i] * kDt;
            px[i] += vx[i] * kDt;
            py[i] += vy[i] * kDt;
            pz[i] += vz[i] * kDt;
        }
    }
    std::uint64_t h = kFnvOffset;
    for (std::size_t i = 0; i < n; ++i) {
        h = fnv_double(h, px[i]);
        h = fnv_double(h, py[i]);
        h = fnv_double(h, pz[i]);
        h = fnv_double(h, vx[i]);
        h = fnv_double(h, vy[i]);
        h = fnv_double(h, vz[i]);
    }
    return h;
}

std::uint64_t matmul_impl(std::uint64_t seed, std::uint32_t nn, Workspace& w) {
    const std::size_t n = nn;
    const std::size_t nsq = n * n;
    w.fp.assign(3 * nsq, 0.0);
    double* a = w.fp.data();
    double* b = a + nsq;
    double* c = b + nsq;
    Rng rng{seed};
    for (std::size_t i = 0; i < nsq; ++i) a[i] = rng.unit() * 2.0 - 1.0;
    for (std::size_t i = 0; i < nsq; ++i) b[i] = rng.unit() * 2.0 - 1.0;

    // Bloklu i-k-j: her C[i][j] icin katkilar k artan sirada eklenir (saf i-j-k ile bit-ayni).
    constexpr std::size_t kBs = 32;
    for (std::size_t ii = 0; ii < n; ii += kBs) {
        const std::size_t ie = std::min(ii + kBs, n);
        for (std::size_t kk = 0; kk < n; kk += kBs) {
            const std::size_t ke = std::min(kk + kBs, n);
            for (std::size_t jj = 0; jj < n; jj += kBs) {
                const std::size_t je = std::min(jj + kBs, n);
                for (std::size_t i = ii; i < ie; ++i) {
                    double* crow = c + i * n;
                    for (std::size_t k = kk; k < ke; ++k) {
                        const double av = a[i * n + k];
                        const double* brow = b + k * n;
                        for (std::size_t j = jj; j < je; ++j) crow[j] += av * brow[j];
                    }
                }
            }
        }
    }
    std::uint64_t h = kFnvOffset;
    for (std::size_t i = 0; i < nsq; ++i) h = fnv_double(h, c[i]);
    return h;
}


// Isin-kure kesisimi: en yakin pozitif t (> 1e-6); yoksa -1.
double hit_sphere(const Sphere& s, double ox, double oy, double oz, double dx, double dy, double dz) {
    const double lx = ox - s.cx;
    const double ly = oy - s.cy;
    const double lz = oz - s.cz;
    const double b = lx * dx + ly * dy + lz * dz;
    const double c = lx * lx + ly * ly + lz * lz - s.r * s.r;
    const double disc = b * b - c;
    if (disc <= 0.0) return -1.0;
    const double sq = std::sqrt(disc);
    double t = -b - sq;
    if (t <= 1e-6) t = -b + sq;
    return t > 1e-6 ? t : -1.0;
}

std::uint64_t ray_impl(std::uint64_t seed, std::uint32_t width, std::uint32_t height, std::uint32_t spheres,
                       Workspace& w) {
    const std::size_t ns = spheres;
    w.spheres.assign(ns, Sphere{});
    Sphere* sp = w.spheres.data();
    Rng rng{seed};
    for (std::size_t i = 0; i < ns; ++i) {
        sp[i].cx = rng.unit() * 8.0 - 4.0;
        sp[i].cy = rng.unit() * 6.0 - 3.0;
        sp[i].cz = 6.0 + rng.unit() * 10.0;
        sp[i].r = 0.3 + rng.unit() * 1.2;
        sp[i].refl = rng.unit() * 0.5;
    }
    const double linv = 1.0 / std::sqrt(0.4 * 0.4 + 0.8 * 0.8 + 0.45 * 0.45);
    const double lx = -0.4 * linv, ly = 0.8 * linv, lz = -0.45 * linv;
    const double fw = static_cast<double>(width);
    const double fh = static_cast<double>(height);
    const double aspect = height > 0 ? fw / fh : 1.0;

    std::uint64_t h = kFnvOffset;
    std::uint32_t hits = 0;
    for (std::uint32_t py = 0; py < height; ++py) {
        for (std::uint32_t px = 0; px < width; ++px) {
            double dx = (2.0 * (static_cast<double>(px) + 0.5) / fw - 1.0) * aspect;
            double dy = 1.0 - 2.0 * (static_cast<double>(py) + 0.5) / fh;
            double dz = 1.5;
            const double dinv = 1.0 / std::sqrt(dx * dx + dy * dy + dz * dz);
            dx *= dinv;
            dy *= dinv;
            dz *= dinv;
            double ox = 0.0, oy = 0.0, oz = 0.0;
            double color = 0.0, weight = 1.0;
            for (int bounce = 0; bounce < 2; ++bounce) {
                double tbest = 1e30;
                std::size_t hit = ns;
                for (std::size_t s = 0; s < ns; ++s) {
                    const double t = hit_sphere(sp[s], ox, oy, oz, dx, dy, dz);
                    if (t > 0.0 && t < tbest) {
                        tbest = t;
                        hit = s;
                    }
                }
                if (hit == ns) {
                    color += weight * (0.2 + 0.3 * dy);
                    break;
                }
                ++hits;
                const Sphere& s0 = sp[hit];
                const double hx = ox + dx * tbest;
                const double hy = oy + dy * tbest;
                const double hz = oz + dz * tbest;
                const double nx = (hx - s0.cx) / s0.r;
                const double ny = (hy - s0.cy) / s0.r;
                const double nz = (hz - s0.cz) / s0.r;
                double diff = nx * lx + ny * ly + nz * lz;
                if (diff < 0.0) diff = 0.0;
                if (diff > 0.0) {
                    for (std::size_t s = 0; s < ns; ++s) {
                        if (s == hit) continue;
                        if (hit_sphere(sp[s], hx, hy, hz, lx, ly, lz) > 0.0) {
                            diff = 0.0;
                            break;
                        }
                    }
                }
                const double local = 0.1 + 0.9 * diff;
                color += weight * (1.0 - s0.refl) * local;
                weight *= s0.refl;
                const double dn = dx * nx + dy * ny + dz * nz;
                dx = dx - nx * (2.0 * dn);
                dy = dy - ny * (2.0 * dn);
                dz = dz - nz * (2.0 * dn);
                ox = hx + nx * 1e-4;
                oy = hy + ny * 1e-4;
                oz = hz + nz * 1e-4;
            }
            double q = color * 255.0;
            if (q < 0.0) q = 0.0;
            if (q > 255.0) q = 255.0;
            h = fnv_step(h, static_cast<std::uint32_t>(q));
        }
    }
    return fold(h, hits);
}

std::uint64_t bigdata_impl(std::uint64_t seed, std::uint32_t elements, Workspace& w) {
    const std::size_t e = std::min(elements, kMaxBigElements);  // savunma: 32 bit anahtar alani
    if (e == 0) return fold(0, 0);
    w.keys.resize(e);
    w.tmp.resize(e);
    Rng rng{seed};
    for (std::size_t i = 0; i < e; ++i) w.keys[i] = static_cast<std::uint32_t>(rng.next() >> 32);

    // LSD radix: 4 x 8 bit, kararli dagitma; cift sayida gecis -> sonuc keys'te.
    std::uint32_t* src = w.keys.data();
    std::uint32_t* dst = w.tmp.data();
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        std::size_t count[257] = {};
        for (std::size_t i = 0; i < e; ++i) ++count[((src[i] >> shift) & 0xFFu) + 1];
        for (std::size_t b = 0; b < 256; ++b) count[b + 1] += count[b];
        for (std::size_t i = 0; i < e; ++i) dst[count[(src[i] >> shift) & 0xFFu]++] = src[i];
        std::swap(src, dst);
    }
    std::uint64_t h = kFnvOffset;
    for (std::size_t i = 0; i < e; ++i) h = fnv_step(h, src[i]);

    // Hash-join: build = yari boy, anahtar alani 2E (eslesme olasiligi anlamli).
    const std::uint64_t domain = 2ull * e;
    const std::size_t build = std::max<std::size_t>(e / 2, 1);
    std::uint32_t bits = 1;
    while ((std::size_t{1} << bits) < 2 * build) ++bits;
    const std::size_t cap = std::size_t{1} << bits;
    const std::size_t mask = cap - 1;
    w.table.assign(cap, 0);
    auto slot_of = [bits](std::uint32_t key) {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(key * 0x9E3779B1u) >> (32 - bits));
    };
    Rng rb{seed ^ 0x5BD1E995ULL};
    for (std::size_t i = 0; i < build; ++i) {
        const auto key = static_cast<std::uint32_t>(rb.next() % domain);
        const auto payload = static_cast<std::uint32_t>(rb.next() >> 32);
        std::size_t s = slot_of(key);
        while (w.table[s] != 0) s = (s + 1) & mask;
        w.table[s] = (static_cast<std::uint64_t>(key + 1u) << 32) | payload;
    }
    std::uint64_t matches = 0, psum = 0;
    for (std::size_t i = 0; i < e; ++i) {
        const auto key = static_cast<std::uint32_t>(src[i] % domain);
        const std::uint64_t tag = static_cast<std::uint64_t>(key + 1u);
        std::size_t s = slot_of(key);
        while (w.table[s] != 0) {
            const std::uint64_t v = w.table[s];
            if ((v >> 32) == tag) {
                ++matches;
                psum += static_cast<std::uint32_t>(v);
            }
            s = (s + 1) & mask;
        }
    }
    return fold(fold(h, matches), psum);
}

std::uint64_t unit_checksum_ws(Kernel k, std::uint64_t seed, std::uint64_t index, const WorkloadSizes& z,
                               Workspace& w) {
    if (k == Kernel::Sim) return chunk_checksum(seed, index, z);
    const std::uint64_t us = chunk_seed(seed ^ kernel_salt(k), index);
    std::uint64_t v = 0;
    switch (k) {
        case Kernel::NBody: v = nbody_impl(us, z.nbody_bodies, z.nbody_steps, w); break;
        case Kernel::MatMul: v = matmul_impl(us, z.matrix_n, w); break;
        case Kernel::Ray: v = ray_impl(us, z.ray_width, z.ray_height, z.ray_spheres, w); break;
        case Kernel::BigData: v = bigdata_impl(us, z.big_elements, w); break;
        case Kernel::Sim: break;
    }
    return fold(fold(0, us), v);
}

// Cagiran is parcaciginin CPU suresi (cekirdek + kullanici, saniye); okunamazsa bos.
std::optional<double> thread_cpu_seconds() {
#ifdef _WIN32
    FILETIME cr, ex, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &cr, &ex, &k, &u)) return std::nullopt;
    auto to_s = [](const FILETIME& ft) {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return static_cast<double>(v.QuadPart) * 1e-7;
    };
    return to_s(k) + to_s(u);
#else
    return std::nullopt;
#endif
}

struct ThreadTime {
    double wall_s = 0.0;
    std::optional<double> cpu_s;
};

struct ChunkRecord {
    std::uint64_t index;
    std::uint64_t checksum;
};

struct PhaseRun {
    KernelResult result;
    std::vector<std::uint64_t> unit_checksums;  // indeks sirasinda
    bool failed = false;
    bool cancelled = false;
};

PhaseRun run_phase(const CpuConfig& config, Kernel kernel, std::uint32_t threads, double seconds,
                   std::atomic<bool>& cancel) {
    using clock = std::chrono::steady_clock;
    if (threads == 0) threads = 1;

    PhaseRun run;
    run.result.kernel = kernel;

    std::atomic<std::uint64_t> next_index{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> failed{false};
    std::vector<std::vector<ChunkRecord>> per_thread(threads);
    std::vector<ThreadTime> times(threads);  // slot basina; yalniz o isci yazar, join sonrasi okunur

    const auto start = clock::now();
    const auto deadline = start + std::chrono::duration_cast<clock::duration>(
                                      std::chrono::duration<double>(std::max(seconds, 0.0)));

    auto worker = [&](std::uint32_t slot) {
        if (threads > 1) spread_current_thread(slot, threads);  // > 64 CPU: tum islemci gruplari
        const auto w0 = clock::now();
        const std::optional<double> c0 = thread_cpu_seconds();
        try {
            Workspace ws;
            auto& out = per_thread[slot];
            for (;;) {
                if (cancel.load(std::memory_order_relaxed)) {
                    cancelled.store(true, std::memory_order_relaxed);
                    break;
                }
                // Sure siniri biten fazda bile ilk birim alinir (sure > 0 iken): is parcacigi baslatma / zamanlayici gecikmesi
                // (yuk altinda, paralel derleme, debug) tum fazi sure disinda birakip sifir birimle NoWork uretmesin.
                // Sure siniri sifir ise sozlesme "hic is yok"tur (cpu_zero_time_limit_is_no_work).
                if (clock::now() >= deadline && (seconds <= 0.0 || next_index.load(std::memory_order_relaxed) != 0)) break;
                const std::uint64_t idx = next_index.fetch_add(1, std::memory_order_relaxed);
                if (config.max_chunks != 0 && idx >= config.max_chunks) break;
                // Talep edilen birim her zaman tamamlanir: indeksler 0..n-1 bitisik kalir.
                out.push_back({idx, unit_checksum_ws(kernel, config.seed, idx, config.sizes, ws)});
            }
        } catch (...) {
            failed.store(true, std::memory_order_relaxed);
        }
        const std::optional<double> c1 = thread_cpu_seconds();
        times[slot].wall_s = std::chrono::duration<double>(clock::now() - w0).count();
        if (c0 && c1) times[slot].cpu_s = *c1 - *c0;
    };

    if (threads == 1) {
        worker(0);
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        try {
            for (std::uint32_t i = 0; i < threads; ++i) pool.emplace_back(worker, i);
        } catch (...) {
            failed.store(true, std::memory_order_relaxed);
        }
        for (auto& t : pool) t.join();
    }
    const auto end = clock::now();

    std::vector<ChunkRecord> all;
    for (auto& v : per_thread) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end(), [](const ChunkRecord& a, const ChunkRecord& b) { return a.index < b.index; });
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].index != i) {  // bitisiklik bozulduysa sonuc guvenilmez
            failed.store(true);
            break;
        }
    }

    run.failed = failed.load();
    run.cancelled = cancelled.load();
    run.result.threads = threads;
    run.result.elapsed_s = std::chrono::duration<double>(end - start).count();
    // Isci CPU payi: tum isciler okunabildi ve yeterince uzun calistiysa (tanecik hatasi < ~%8).
    {
        std::vector<double> shares;
        shares.reserve(times.size());
        for (const auto& tt : times) {
            if (!tt.cpu_s || tt.wall_s < kMinShareWallSeconds) {
                shares.clear();
                break;
            }
            shares.push_back(*tt.cpu_s / tt.wall_s);
        }
        if (!shares.empty()) {
            run.result.min_thread_cpu_share = *std::min_element(shares.begin(), shares.end());
            run.result.thread_cpu_share = std::move(shares);
        }
    }
    if (run.failed) return run;

    run.unit_checksums.reserve(all.size());
    std::uint64_t acc = 0;
    for (const auto& r : all) {
        run.unit_checksums.push_back(r.checksum);
        acc = fold(acc, r.checksum);
    }
    run.result.units = all.size();
    run.result.checksum = acc;
    if (run.result.elapsed_s > 0.0)
        run.result.units_per_second = static_cast<double>(run.result.units) / run.result.elapsed_s;
    g_checksum_sink.fetch_xor(acc, std::memory_order_relaxed);
    return run;
}

struct VariantRun {
    CpuVariantResult result;
    std::vector<std::vector<std::uint64_t>> unit_checksums;  // result.kernels ile ayni sira
    bool failed = false;
};

std::uint32_t enabled_count(std::uint32_t mask) {
    std::uint32_t n = 0;
    for (std::uint32_t k = 0; k < kKernelCount; ++k) n += (mask >> k) & 1u;
    return n;
}

// bigdata butcesi (bayt): yapilandirma kancasi veya OS bellek durumu. Okunamazsa / politika atladiysa bos
// (bilinmiyor; "0 bayt yer var" diye okunmaz).
std::optional<std::uint64_t> resolve_bigdata_budget(const CpuConfig& config) {
    if (config.bigdata_budget_unknown) return std::nullopt;
    if (config.bigdata_budget_bytes != 0) return config.bigdata_budget_bytes;
    const memory::Budget b = memory::compute_budget(memory::read_memory_snapshot(), memory::BudgetPolicy{});
    if (b.skipped) return std::nullopt;
    return b.bytes;
}

// Dogrulanmis yapilandirma + bir kez okunan bigdata butcesi. Butce biliniyor ve tek isci sigmiyorsa big_elements
// kucultulur; ayni yapilandirma ST ve MT'ye verilir (ortak onek checksum'lari karsilastirilabilir kalir).
// Butce bilinmiyorsa boyut degismez.
struct PreparedConfig {
    CpuConfig config;
    std::optional<std::uint64_t> bigdata_budget;
    bool elements_reduced = false;
};

PreparedConfig prepare_config(const CpuConfig& raw) {
    PreparedConfig p;
    p.config = validated_config(raw);
    if ((p.config.kernel_mask & kernel_bit(Kernel::BigData)) == 0) return p;
    p.bigdata_budget = resolve_bigdata_budget(p.config);
    if (!p.bigdata_budget) return p;
    const std::uint32_t e = bigdata_elements_for_budget(p.config.sizes.big_elements, *p.bigdata_budget);
    if (e != p.config.sizes.big_elements) {
        p.config.sizes.big_elements = e;
        p.elements_reduced = true;
    }
    return p;
}

VariantRun run_variant_impl(const CpuConfig& config, std::uint32_t threads, double seconds,
                            std::optional<std::uint64_t> bigdata_budget, bool elements_reduced, std::atomic<bool>& cancel) {
    if (threads == 0) threads = 1;
    VariantRun run;
    run.result.threads = threads;
    const std::uint32_t mask = config.kernel_mask & kAllKernels;
    const std::uint32_t enabled = enabled_count(mask);
    if (enabled == 0) return run;
    const double per_kernel = std::max(seconds, 0.0) / static_cast<double>(enabled);

    double log_sum = 0.0;
    bool all_positive = true;
    bool share_known = true;
    double share_lo = 0.0;
    std::uint64_t acc = 0;
    for (std::uint32_t k = 0; k < kKernelCount; ++k) {
        if (((mask >> k) & 1u) == 0) continue;
        if (cancel.load(std::memory_order_relaxed)) {
            run.result.cancelled = true;
            break;
        }
        std::uint32_t phase_threads = threads;
        if (static_cast<Kernel>(k) == Kernel::BigData) {
            run.result.bigdata_elements = config.sizes.big_elements;
            run.result.bigdata_elements_reduced = elements_reduced;
            run.result.bigdata_budget_known = bigdata_budget.has_value();
            if (elements_reduced) run.result.memory_limited = true;
            if (threads > 1) {
                // Isci basina ~8 MiB: toplam RAM butcesini asmasin (gerekirse isci sayisi azaltilir). Butce
                // bilinmiyorsa guvenli taraf: tek isci (ST ile ayni bellek), memory_limited isaretlenir.
                phase_threads =
                    bigdata_budget ? bigdata_thread_cap(threads, config.sizes.big_elements, *bigdata_budget) : 1u;
                if (phase_threads < threads) run.result.memory_limited = true;
            }
        }
        PhaseRun ph = run_phase(config, static_cast<Kernel>(k), phase_threads, per_kernel, cancel);
        if (ph.result.min_thread_cpu_share) {
            share_lo = run.result.kernels.empty() ? *ph.result.min_thread_cpu_share
                                                  : std::min(share_lo, *ph.result.min_thread_cpu_share);
        } else {
            share_known = false;
        }
        run.result.elapsed_s += ph.result.elapsed_s;
        if (ph.failed) run.failed = true;
        run.result.chunks += ph.result.units;
        acc = fold(acc, ph.result.checksum);
        if (ph.result.units_per_second > 0.0) log_sum += std::log(ph.result.units_per_second);
        else all_positive = false;
        run.result.kernels.push_back(ph.result);
        run.unit_checksums.push_back(std::move(ph.unit_checksums));
        if (ph.cancelled) {
            run.result.cancelled = true;
            break;
        }
    }
    run.result.checksum = acc;
    if (share_known && !run.result.kernels.empty()) {
        run.result.min_thread_cpu_share = share_lo;
        run.result.contended = share_lo < kUncontendedThreadCpuShare;
    }
    if (!run.failed && !run.result.cancelled && all_positive && run.result.kernels.size() == enabled)
        run.result.work_units_per_second = std::exp(log_sum / static_cast<double>(enabled));
    return run;
}

std::uint64_t fold_prefix(const std::vector<std::uint64_t>& v, std::size_t n) {
    std::uint64_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) acc = fold(acc, v[i]);
    return acc;
}

std::uint32_t golden_count(Kernel k) { return k == Kernel::Sim ? kGoldenCount : kGoldenUnits; }

}  // namespace

std::string_view kernel_name(Kernel k) noexcept {
    switch (k) {
        case Kernel::Sim: return "sim";
        case Kernel::NBody: return "nbody";
        case Kernel::MatMul: return "matmul";
        case Kernel::Ray: return "ray";
        case Kernel::BigData: return "bigdata";
    }
    return "unknown";
}

CpuConfig quick_cpu_config() {
    CpuConfig c;
    c.st_seconds = kQuickStSeconds;
    c.mt_seconds = kQuickMtSeconds;
    c.time_limit_s = kQuickMtSeconds;
    return c;
}

CpuConfig full_cpu_config() {
    CpuConfig c;
    c.st_seconds = kFullStSeconds;
    c.mt_seconds = kFullMtSeconds;
    c.time_limit_s = kFullMtSeconds;
    return c;
}

CpuConfig validated_config(const CpuConfig& config) {
    CpuConfig c = config;
    c.sizes.big_elements = std::min(c.sizes.big_elements, kMaxBigElements);
    return c;
}

std::uint64_t bigdata_bytes_per_worker(std::uint32_t elements) noexcept {
    const std::uint64_t e = std::min(elements, kMaxBigElements);
    if (e == 0) return 0;
    const std::uint64_t build = std::max<std::uint64_t>(e / 2, 1);
    std::uint64_t cap = 2;  // bigdata_impl ile ayni: bits >= 1, 2^bits >= 2 * build
    while (cap < 2 * build) cap <<= 1;
    return 2 * sizeof(std::uint32_t) * e + sizeof(std::uint64_t) * cap;
}

std::uint32_t bigdata_thread_cap(std::uint32_t threads, std::uint32_t elements, std::uint64_t budget_bytes) noexcept {
    if (threads == 0) threads = 1;
    const std::uint64_t per = bigdata_bytes_per_worker(elements);
    if (per == 0) return threads;
    const std::uint64_t fit = budget_bytes / per;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, std::min<std::uint64_t>(threads, fit)));
}

std::uint32_t bigdata_elements_for_budget(std::uint32_t elements, std::uint64_t budget_bytes) noexcept {
    std::uint32_t e = std::min(elements, kMaxBigElements);
    while (e > kMinBigElements && bigdata_bytes_per_worker(e) > budget_bytes) e = std::max(e / 2, kMinBigElements);
    return e;
}

bool golden_applies(Kernel k, const CpuConfig& config) noexcept {
    if (config.seed != kDefaultSeed) return false;
    const WorkloadSizes& z = config.sizes;
    const WorkloadSizes d{};
    switch (k) {
        case Kernel::Sim:
            return z.particles == d.particles && z.physics_steps == d.physics_steps && z.grid_side == d.grid_side &&
                   z.sort_elements == d.sort_elements;
        case Kernel::NBody: return z.nbody_bodies == d.nbody_bodies && z.nbody_steps == d.nbody_steps;
        case Kernel::MatMul: return z.matrix_n == d.matrix_n;
        case Kernel::Ray: return z.ray_width == d.ray_width && z.ray_height == d.ray_height && z.ray_spheres == d.ray_spheres;
        case Kernel::BigData: return z.big_elements == d.big_elements;
    }
    return false;
}

Contention contention(const CpuVariantResult& v) noexcept {
    if (!v.min_thread_cpu_share) return Contention::Unknown;
    return *v.min_thread_cpu_share < kUncontendedThreadCpuShare ? Contention::Contended : Contention::Uncontended;
}

const char* to_string(Contention c) noexcept {
    switch (c) {
        case Contention::Uncontended: return "uncontended";
        case Contention::Contended: return "contended";
        case Contention::Unknown: break;
    }
    return "unknown";
}

double variant_seconds(const CpuConfig& config, bool multi_thread) noexcept {
    const double v = multi_thread ? config.mt_seconds : config.st_seconds;
    return v > 0.0 ? v : config.time_limit_s;
}

std::uint64_t fold(std::uint64_t acc, std::uint64_t value) {
    return mix64(acc ^ (value + 0x9E3779B97F4A7C15ULL + (acc << 6) + (acc >> 2)));
}

std::uint64_t chunk_seed(std::uint64_t seed, std::uint64_t index) {
    return mix64(seed ^ mix64(index + 1));
}

std::uint64_t kernel_salt(Kernel k) noexcept {
    // Sim tuzsuzdur (cpu_v1 uyumu); digerleri sabit, ayirt edici sabitler.
    switch (k) {
        case Kernel::Sim: return 0;
        case Kernel::NBody: return 0xA5A5'0001'0000'0001ULL;
        case Kernel::MatMul: return 0xA5A5'0002'0000'0002ULL;
        case Kernel::Ray: return 0xA5A5'0003'0000'0003ULL;
        case Kernel::BigData: return 0xA5A5'0004'0000'0004ULL;
    }
    return 0;
}

std::uint64_t particle_workload(std::uint64_t seed, std::uint32_t particles, std::uint32_t steps) {
    Scratch& s = scratch();
    const std::size_t n = particles;
    s.x.resize(n);
    s.y.resize(n);
    s.vx.resize(n);
    s.vy.resize(n);
    s.fx.resize(n);
    s.fy.resize(n);

    Rng rng{seed};
    for (std::size_t i = 0; i < n; ++i) {
        s.x[i] = static_cast<std::int32_t>(rng.next() % kWorld);
        s.y[i] = static_cast<std::int32_t>(rng.next() % kWorld);
        s.vx[i] = static_cast<std::int32_t>(rng.next() % 2049) - 1024;
        s.vy[i] = static_cast<std::int32_t>(rng.next() % 2049) - 1024;
    }

    auto integrate = [](std::int32_t& pos, std::int32_t& vel, std::int32_t force) {
        vel += force;
        vel -= vel / 64;
        vel = std::clamp(vel, -kMaxSpeed, kMaxSpeed);
        pos += vel;
        if (pos < 0) {
            pos = -pos;
            vel = -vel;
        } else if (pos >= kWorld) {
            pos = 2 * (kWorld - 1) - pos;
            vel = -vel;
        }
    };

    for (std::uint32_t step = 0; step < steps && n > 0; ++step) {
        // Kuvvetler eski konumlardan hesaplanir (iki fazli guncelleme).
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t nx = (i + 1) % n;
            const std::size_t pv = (i + n - 1) % n;
            s.fx[i] = (s.x[nx] - s.x[i]) / 256 + (s.x[pv] - s.x[i]) / 256;
            s.fy[i] = (s.y[nx] - s.y[i]) / 256 + (s.y[pv] - s.y[i]) / 256 - kGravity;
        }
        for (std::size_t i = 0; i < n; ++i) {
            integrate(s.x[i], s.vx[i], s.fx[i]);
            integrate(s.y[i], s.vy[i], s.fy[i]);
        }
    }

    std::uint64_t h = kFnvOffset;
    for (std::size_t i = 0; i < n; ++i) {
        h = fnv_step(h, static_cast<std::uint32_t>(s.x[i]));
        h = fnv_step(h, static_cast<std::uint32_t>(s.y[i]));
        h = fnv_step(h, static_cast<std::uint32_t>(s.vx[i]));
        h = fnv_step(h, static_cast<std::uint32_t>(s.vy[i]));
    }
    return h;
}

std::uint64_t path_workload(std::uint64_t seed, std::uint32_t grid_side) {
    Scratch& s = scratch();
    const std::size_t g = grid_side;
    const std::size_t cells = g * g;
    if (cells == 0) return fold(0, 0);
    s.blocked.resize(cells);
    s.dist.assign(cells, -1);
    s.queue.resize(cells);

    Rng rng{seed};
    for (std::size_t i = 0; i < cells; ++i) s.blocked[i] = (rng.next() % 100) < kObstaclePct ? 1 : 0;
    s.blocked[0] = 0;
    s.blocked[cells - 1] = 0;

    // 4-komsulu BFS, kaynak (0,0).
    std::size_t head = 0, tail = 0;
    s.dist[0] = 0;
    s.queue[tail++] = 0;
    while (head < tail) {
        const std::uint32_t c = s.queue[head++];
        const std::size_t cx = c % g, cy = c / g;
        const std::int32_t d = s.dist[c] + 1;
        auto visit = [&](std::size_t nidx) {
            if (!s.blocked[nidx] && s.dist[nidx] < 0) {
                s.dist[nidx] = d;
                s.queue[tail++] = static_cast<std::uint32_t>(nidx);
            }
        };
        if (cx > 0) visit(c - 1);
        if (cx + 1 < g) visit(c + 1);
        if (cy > 0) visit(c - g);
        if (cy + 1 < g) visit(c + g);
    }

    std::uint64_t acc = 0;
    std::uint64_t reached = 0;
    for (std::size_t i = 0; i < cells; ++i) {
        if (s.dist[i] >= 0) {
            acc += static_cast<std::uint64_t>(s.dist[i] + 1) * static_cast<std::uint64_t>(i + 1);
            ++reached;
        }
    }
    return fold(acc, reached);
}

std::uint64_t sort_workload(std::uint64_t seed, std::uint32_t elements) {
    Scratch& s = scratch();
    s.values.resize(elements);
    Rng rng{seed};
    for (auto& v : s.values) v = static_cast<std::uint32_t>(rng.next() >> 32);
    std::sort(s.values.begin(), s.values.end());
    std::uint64_t h = kFnvOffset;
    for (const auto v : s.values) h = fnv_step(h, v);
    return h;
}

std::uint64_t nbody_workload(std::uint64_t seed, std::uint32_t bodies, std::uint32_t steps) {
    Workspace w;
    return nbody_impl(seed, bodies, steps, w);
}

std::uint64_t matmul_workload(std::uint64_t seed, std::uint32_t n) {
    Workspace w;
    return matmul_impl(seed, n, w);
}

std::uint64_t ray_workload(std::uint64_t seed, std::uint32_t width, std::uint32_t height, std::uint32_t spheres) {
    Workspace w;
    return ray_impl(seed, width, height, spheres, w);
}

std::uint64_t bigdata_workload(std::uint64_t seed, std::uint32_t elements) {
    Workspace w;
    return bigdata_impl(seed, elements, w);
}

std::uint64_t chunk_checksum(std::uint64_t seed, std::uint64_t index, const WorkloadSizes& sizes) {
    const std::uint64_t cs = chunk_seed(seed, index);
    std::uint64_t h = fold(0, cs);
    h = fold(h, particle_workload(cs ^ 1ULL, sizes.particles, sizes.physics_steps));
    h = fold(h, path_workload(cs ^ 2ULL, sizes.grid_side));
    h = fold(h, sort_workload(cs ^ 3ULL, sizes.sort_elements));
    return h;
}

std::uint64_t unit_checksum(Kernel k, std::uint64_t seed, std::uint64_t index, const WorkloadSizes& sizes) {
    Workspace w;
    return unit_checksum_ws(k, seed, index, sizes, w);
}

std::uint64_t golden_chunk_checksum(std::uint32_t index) {
    return index < kGoldenCount ? kGolden[index] : 0;
}

std::uint64_t golden_unit_checksum(Kernel k, std::uint32_t index) {
    if (k == Kernel::Sim) return golden_chunk_checksum(index);
    const auto ki = static_cast<std::uint32_t>(k);
    if (ki == 0 || ki >= kKernelCount || index >= kGoldenUnits) return 0;
    return kGoldenUnits2[ki - 1][index];
}

CpuVariantResult run_variant(const CpuConfig& raw_config, std::uint32_t threads, std::atomic<bool>& cancel) {
    const PreparedConfig pc = prepare_config(raw_config);
    const CpuConfig& config = pc.config;
    VariantRun run = run_variant_impl(config, threads, variant_seconds(config, threads > 1), pc.bigdata_budget,
                                      pc.elements_reduced, cancel);
    if (run.failed) run.result.chunks = 0;
    return run.result;
}

CpuBenchmarkResult run_cpu_benchmark(const CpuConfig& raw_config, std::atomic<bool>& cancel) {
    const PreparedConfig pc = prepare_config(raw_config);
    const CpuConfig& config = pc.config;  // bigdata kucultulduyse yalniz bigdata altin denetimi atlanir
    CpuBenchmarkResult out;
    out.hardware_threads = logical_processor_count();  // tum islemci gruplari (hardware_concurrency tek grup sayar)
    const std::uint32_t mt_threads = config.threads != 0 ? config.threads : out.hardware_threads;
    const std::uint32_t mask = config.kernel_mask & kAllKernels;
    out.single_thread.threads = 1;
    out.multi_thread.threads = mt_threads;

    if (cancel.load()) {
        out.cancelled = true;
        out.status = CpuRunStatus::Cancelled;
        return out;
    }

    // On kontrol: altin birimler olcumden once dogrulanir (bozuk derleme/donanim hatasi). Cekirdek bazinda:
    // yalniz is yuku varsayilan olan cekirdekler (golden_applies).
    {
        Workspace ws;
        for (std::uint32_t k = 0; k < kKernelCount; ++k) {
            if (((mask >> k) & 1u) == 0) continue;
            const auto kern = static_cast<Kernel>(k);
            if (!golden_applies(kern, config)) continue;
            for (std::uint32_t i = 0; i < golden_count(kern); ++i) {
                if (cancel.load()) {
                    out.cancelled = true;
                    out.status = CpuRunStatus::Cancelled;
                    return out;
                }
                if (unit_checksum_ws(kern, config.seed, i, config.sizes, ws) != golden_unit_checksum(kern, i)) {
                    out.status = CpuRunStatus::ChecksumMismatch;
                    return out;
                }
            }
        }
    }

    VariantRun st = run_variant_impl(config, 1, variant_seconds(config, false), pc.bigdata_budget, pc.elements_reduced, cancel);
    out.single_thread = st.result;
    if (st.result.cancelled || cancel.load()) {
        out.cancelled = true;
        out.status = CpuRunStatus::Cancelled;
        return out;
    }
    VariantRun mt =
        run_variant_impl(config, mt_threads, variant_seconds(config, true), pc.bigdata_budget, pc.elements_reduced, cancel);
    out.multi_thread = mt.result;
    if (mt.result.cancelled || cancel.load()) {
        out.cancelled = true;
        out.status = CpuRunStatus::Cancelled;
        return out;
    }
    bool any_empty = st.result.kernels.empty() || mt.result.kernels.empty();
    for (const auto* v : {&st.result, &mt.result})
        for (const auto& k : v->kernels) any_empty = any_empty || k.units == 0;
    if (st.failed || mt.failed || any_empty || st.result.kernels.size() != mt.result.kernels.size()) {
        out.single_thread.chunks = st.failed ? 0 : st.result.chunks;
        out.multi_thread.chunks = mt.failed ? 0 : mt.result.chunks;
        out.single_thread.work_units_per_second = 0;
        out.multi_thread.work_units_per_second = 0;
        out.status = CpuRunStatus::NoWork;
        return out;
    }

    // Dogruluk: her cekirdekte ST ve MT'nin ortak birim oneki ayni checksum'u vermeli; varsayilan
    // yapilandirmada kosu sirasinda uretilen altin indeksler de esit olmali.
    bool ok = true;
    for (std::size_t ki = 0; ki < st.unit_checksums.size(); ++ki) {
        const auto& a = st.unit_checksums[ki];
        const auto& b = mt.unit_checksums[ki];
        const std::size_t common = std::min(a.size(), b.size());
        if (fold_prefix(a, common) != fold_prefix(b, common)) ok = false;
        const Kernel kern = st.result.kernels[ki].kernel;
        if (golden_applies(kern, config)) {
            for (const auto* v : {&a, &b})
                for (std::size_t i = 0; i < golden_count(kern) && i < v->size(); ++i)
                    if ((*v)[i] != golden_unit_checksum(kern, static_cast<std::uint32_t>(i))) ok = false;
        }
    }
    g_checksum_sink.fetch_xor(st.result.checksum ^ mt.result.checksum, std::memory_order_relaxed);

    out.checksum_ok = ok;
    out.status = ok ? CpuRunStatus::Ok : CpuRunStatus::ChecksumMismatch;
    out.scored = ok;
    return out;
}

std::uint64_t checksum_sink_value() noexcept {
    return g_checksum_sink.load(std::memory_order_relaxed);
}

}  // namespace ef::benchmark::cpu
