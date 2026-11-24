#include "benchmark/memory/memory_bench.h"
#include "benchmark/cpu/cpu_groups.h"
#include "core/measure/address_pattern.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <functional>
#include <new>
#include <optional>
#include <thread>

namespace ef::benchmark::memory {

namespace {

// Olcum sonucunu derleyicinin silmemesi icin.
volatile std::uint64_t g_sink = 0;

// RAII sanal bellek tamponu: her cikis yolunda (iptal, hata, istisna) VirtualFree.
class VirtualBuffer {
public:
    explicit VirtualBuffer(std::size_t bytes) : bytes_(bytes) {
        ptr_ = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    ~VirtualBuffer() {
        if (ptr_) VirtualFree(ptr_, 0, MEM_RELEASE);
    }
    VirtualBuffer(const VirtualBuffer&) = delete;
    VirtualBuffer& operator=(const VirtualBuffer&) = delete;
    bool ok() const noexcept { return ptr_ != nullptr; }
    std::uint64_t* words() const noexcept { return static_cast<std::uint64_t*>(ptr_); }
    std::size_t word_count() const noexcept { return bytes_ / sizeof(std::uint64_t); }

private:
    void* ptr_ = nullptr;
    std::size_t bytes_ = 0;
};

class Clock {
public:
    Clock() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq_ = static_cast<double>(f.QuadPart);
    }
    double now() const {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return static_cast<double>(c.QuadPart) / freq_;
    }

private:
    double freq_ = 1.0;
};

struct Measure {
    double seconds = 0;
    std::uint64_t units = 0;  // bayt veya erisim sayisi
    bool cancelled = false;
};

// Tam gecisler: sure en az min_s olana kadar tekrar; her geciste iptal kontrolu.
template <class Pass>
Measure repeat_passes(const Clock& clk, const MemoryConfig& cfg, std::atomic<bool>& cancel,
                      std::uint64_t units_per_pass, Pass&& pass) {
    Measure m;
    pass(0u);  // isinma (zamanlanmaz)
    const double t0 = clk.now();
    std::uint32_t n = 0;
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) {
            m.cancelled = true;
            return m;
        }
        pass(++n);
        m.units += units_per_pass;
        m.seconds = clk.now() - t0;
        if (m.seconds >= cfg.min_seconds_per_measure || m.seconds >= cfg.max_seconds_per_measure) break;
    }
    return m;
}

std::uint64_t seq_read_pass(const std::uint64_t* w, std::size_t n) {
    std::uint64_t a = 0, b = 0, c = 0, d = 0;
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        a += w[i];
        b += w[i + 1];
        c += w[i + 2];
        d += w[i + 3];
    }
    for (; i < n; ++i) a += w[i];
    return a + b + c + d;
}

void seq_write_pass(std::uint64_t* w, std::size_t n, std::uint64_t v) {
    for (std::size_t i = 0; i < n; ++i) w[i] = v;
}

// Yazma olcumunun sonucu dogrulanir: son gecisin deseni tamponun tamaminda okunmali (zamanlanmaz).
bool verify_write_pass(const std::uint64_t* w, std::size_t n, std::uint64_t v) {
    for (std::size_t i = 0; i < n; ++i)
        if (w[i] != v) return false;
    return true;
}

std::uint64_t strided_pass(const std::uint64_t* w, std::size_t n) {
    std::uint64_t s = 0;
    for (std::size_t i = 0; i < n; i += 8) s += w[i];  // 8 * 8 B = 64 B: satir basina bir erisim
    return s;
}

// Sattolo: tek dongulu rastgele permutasyon. Dugum = stride kelimelik aralik (en az 64 B satir);
// next indeksi dugumun ilk kelimesinde. Cok buyuk tamponda kurulum suresini sinirlamak icin dugum
// sayisi kMaxChaseNodes'u asmayacak sekilde adim ikiye katlanir (erisimler yine tum tampona yayilir).
// Ek bellek kullanmaz (butce disi tahsis yok).
constexpr std::size_t kMaxChaseNodes = std::size_t{1} << 22;

std::size_t chase_stride_words(std::size_t words) {
    std::size_t stride = 8;  // 8 * 8 B = 64 B
    while (words / stride > kMaxChaseNodes) stride *= 2;
    return stride;
}

std::size_t build_chase(std::uint64_t* w, std::size_t words, std::size_t stride) {
    const std::size_t nodes = words / stride;
    for (std::size_t i = 0; i < nodes; ++i) w[i * stride] = i;
    std::uint64_t x = 0x9E3779B97F4A7C15ull;
    for (std::size_t i = nodes - 1; i > 0; --i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        const std::size_t j = static_cast<std::size_t>(x % i);  // 0..i-1
        std::swap(w[i * stride], w[j * stride]);
    }
    return nodes;
}

Measure chase(const Clock& clk, const MemoryConfig& cfg, std::atomic<bool>& cancel, const std::uint64_t* w,
              std::size_t stride) {
    constexpr std::uint32_t kChunk = 1u << 14;
    Measure m;
    std::uint64_t idx = 0;
    for (std::uint32_t k = 0; k < kChunk; ++k) idx = w[idx * stride];  // isinma
    const double t0 = clk.now();
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) {
            m.cancelled = true;
            break;
        }
        for (std::uint32_t k = 0; k < kChunk; ++k) idx = w[idx * stride];
        m.units += kChunk;
        m.seconds = clk.now() - t0;
        if (m.seconds >= cfg.min_seconds_per_measure || m.seconds >= cfg.max_seconds_per_measure) break;
    }
    g_sink = g_sink + idx;
    return m;
}

// --- BENCH-08: cok is parcacikli bant genisligi ---
enum class MtOp { Read, Write, Copy };

constexpr std::size_t kMtBlockWords = (256 * 1024) / sizeof(std::uint64_t);  // 256 KiB blok

// Cagiran is parcaciginin CPU suresi (cekirdek + kullanici, saniye); okunamazsa bos.
std::optional<double> thread_cpu_seconds() {
    FILETIME cr, ex, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &cr, &ex, &k, &u)) return std::nullopt;
    auto to_s = [](const FILETIME& ft) {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return static_cast<double>(v.QuadPart) * 1e-7;
    };
    return to_s(k) + to_s(u);
}

struct MtThreadTime {
    double wall_s = 0;            // is parcaciginin olcum penceresi (go .. stop)
    std::optional<double> cpu_s;  // ayni aralikta GetThreadTimes farki
};

struct MtShared {
    std::atomic<std::uint32_t> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<std::uint64_t> sink{0};
    std::vector<MtThreadTime> times;  // slot basina; yalniz o slotun is parcacigi yazar, join sonrasi okunur
};

// Dilim uzerinde blok blok dongu; her bloktan sonra durdurma bayragi okunur (bitis kaymasi < 1 blok).
// Copy: dilimin ilk yarisi kaynak, ikinci yarisi hedef; bayt = okunan + yazilan.
void mt_worker(MtShared& sh, const Clock& clk, std::uint32_t slot, std::uint64_t* slice, std::size_t words,
               MtOp op) {
    // > 64 CPU: tum islemci gruplari. Tek is parcacikli kosu surecin birincil grubunda kalir (cpu_verify /
    // cpu_workload ile ayni kosul).
    if (sh.times.size() > 1) spread_current_thread(slot, static_cast<std::uint32_t>(sh.times.size()));
    const std::size_t span = op == MtOp::Copy ? words / 2 : words;
    const std::size_t block = std::min(kMtBlockWords, span);
    std::uint64_t acc = 0;
    std::uint64_t pass = 1;
    auto do_block = [&](std::size_t off) -> std::uint64_t {
        const std::size_t n = std::min(block, span - off);
        switch (op) {
            case MtOp::Read: acc += seq_read_pass(slice + off, n); return n * sizeof(std::uint64_t);
            case MtOp::Write: seq_write_pass(slice + off, n, pass); return n * sizeof(std::uint64_t);
            case MtOp::Copy: {
                const std::uint64_t* src = slice + off;
                std::uint64_t* dst = slice + span + off;
                for (std::size_t i = 0; i < n; ++i) dst[i] = src[i];
                return 2 * n * sizeof(std::uint64_t);
            }
        }
        return 0;
    };
    // Isinma: dilimin bir tam gecisi (zamanlanmaz).
    for (std::size_t off = 0; off < span; off += block) do_block(off);
    sh.ready.fetch_add(1, std::memory_order_acq_rel);
    while (!sh.go.load(std::memory_order_acquire)) {
        if (sh.stop.load(std::memory_order_relaxed)) return;
        std::this_thread::yield();
    }
    const double w0 = clk.now();
    const std::optional<double> c0 = thread_cpu_seconds();
    std::uint64_t local = 0;
    std::size_t off = 0;
    while (!sh.stop.load(std::memory_order_relaxed)) {
        local += do_block(off);
        off += block;
        if (off >= span) {
            off = 0;
            ++pass;
        }
    }
    const std::optional<double> c1 = thread_cpu_seconds();
    MtThreadTime& tt = sh.times[slot];
    tt.wall_s = clk.now() - w0;
    if (c0 && c1) tt.cpu_s = *c1 - *c0;
    if (op != MtOp::Read) acc += slice[span / 2] + slice[words - 1];
    sh.bytes.fetch_add(local, std::memory_order_relaxed);
    sh.sink.fetch_xor(acc, std::memory_order_relaxed);
}

struct MtMeasure {
    double seconds = 0;
    std::uint64_t bytes = 0;
    bool cancelled = false;
    bool failed = false;
    std::optional<double> min_thread_cpu_share;  // is parcaciklari arasinda en dusuk CPU / duvar
};

MtMeasure run_mt(const Clock& clk, std::uint64_t* base, const MtSlicePlan& plan, MtOp op, double window_s,
                 std::atomic<bool>& cancel) {
    MtMeasure m;
    MtShared sh;
    sh.times.resize(plan.threads);
    const std::size_t slice_words = static_cast<std::size_t>(plan.slice_bytes / sizeof(std::uint64_t));
    std::vector<std::thread> pool;
    pool.reserve(plan.threads);
    auto stop_all = [&] {
        sh.stop.store(true);
        for (auto& t : pool) t.join();
    };
    try {
        for (std::uint32_t t = 0; t < plan.threads; ++t)
            pool.emplace_back(mt_worker, std::ref(sh), std::cref(clk), t,
                              base + static_cast<std::size_t>(t) * slice_words, slice_words, op);
    } catch (...) {
        stop_all();
        m.failed = true;
        return m;
    }
    while (sh.ready.load(std::memory_order_acquire) < plan.threads) {
        if (cancel.load(std::memory_order_relaxed)) {
            stop_all();
            m.cancelled = true;
            return m;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double t0 = clk.now();
    sh.go.store(true, std::memory_order_release);
    while (clk.now() - t0 < window_s) {
        if (cancel.load(std::memory_order_relaxed)) {
            stop_all();
            m.cancelled = true;
            return m;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stop_all();
    m.seconds = clk.now() - t0;
    m.bytes = sh.bytes.load();
    g_sink = g_sink + sh.sink.load();
    // Tum is parcaciklarinin payi okunabildiyse en dusugu; biri bile okunamadiysa bos (tahmin yok).
    bool all = !sh.times.empty();
    double lo = 0;
    for (std::size_t i = 0; i < sh.times.size() && all; ++i) {
        const auto& tt = sh.times[i];
        if (!tt.cpu_s || tt.wall_s <= 0.0) {
            all = false;
            break;
        }
        const double share = *tt.cpu_s / tt.wall_s;
        lo = i == 0 ? share : std::min(lo, share);
    }
    if (all) m.min_thread_cpu_share = lo;
    return m;
}

double gbps(const Measure& m) { return static_cast<double>(m.units) / m.seconds / 1e9; }
double mt_gbps(const MtMeasure& m) { return static_cast<double>(m.bytes) / m.seconds / 1e9; }
double ns_per(const Measure& m) { return m.seconds * 1e9 / static_cast<double>(m.units); }

MemoryResult finish(MemoryResult r, MemStatus s, std::string reason) {
    r.status = s;
    r.reason = std::move(reason);
    return r;
}

}  // namespace

const char* to_string(MemStatus s) noexcept {
    switch (s) {
        case MemStatus::Completed: return "completed";
        case MemStatus::Cancelled: return "cancelled";
        case MemStatus::Skipped: return "skipped";
        case MemStatus::Failed: return "failed";
    }
    return "failed";
}

MemoryConfig quick_memory_config() {
    MemoryConfig c;
    c.min_seconds_per_measure = kQuickMinSecondsPerMeasure;
    c.max_seconds_per_measure = kQuickMaxSecondsPerMeasure;
    c.mt_seconds_per_measure = kQuickMtSecondsPerMeasure;
    return c;
}

MemoryConfig full_memory_config() {
    MemoryConfig c;
    c.min_seconds_per_measure = kFullMinSecondsPerMeasure;
    c.max_seconds_per_measure = kFullMaxSecondsPerMeasure;
    c.mt_seconds_per_measure = kFullMtSecondsPerMeasure;
    return c;
}

MemorySnapshot read_memory_snapshot() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    MemorySnapshot s;
    if (GlobalMemoryStatusEx(&ms)) {
        s.total_phys = ms.ullTotalPhys;
        s.avail_phys = ms.ullAvailPhys;
    }
    return s;
}

CacheSizes detect_cache_sizes() {
    CacheSizes c;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return c;
    std::vector<unsigned char> buf(len);
    auto* base = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationCache, base, &len)) return c;
    for (DWORD off = 0; off < len;) {
        auto* e = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
        if (e->Size == 0) break;
        if (e->Relationship == RelationCache) {
            const CACHE_RELATIONSHIP& cr = e->Cache;
            const bool data = cr.Type == CacheData || cr.Type == CacheUnified;
            const std::uint64_t sz = cr.CacheSize;
            // Hibrit/cok CCD islemcide ornek basina en buyuk boy alinir.
            if (data && cr.Level == 1) c.l1d = std::max(c.l1d, sz);
            if (data && cr.Level == 2) c.l2 = std::max(c.l2, sz);
            if (data && cr.Level == 3) c.l3 = std::max(c.l3, sz);
        }
        off += e->Size;
    }
    return c;
}

MemoryResult run_memory_bench(const MemoryConfig& cfg, std::atomic<bool>& cancel) {
    MemoryResult r;
    try {
        r.snapshot = cfg.snapshot_override ? *cfg.snapshot_override : read_memory_snapshot();
        r.budget = compute_budget(r.snapshot, cfg.policy);
        r.caches = detect_cache_sizes();
        if (r.budget.skipped) {
            std::string why = r.budget.reason;  // tasima oncesi kopya (arguman sirasi belirsiz)
            return finish(std::move(r), MemStatus::Skipped, std::move(why));
        }
        if (cancel.load()) return finish(std::move(r), MemStatus::Cancelled, "user_cancel");

        const auto plan = plan_buffers(r.caches, r.budget.bytes);
        if (plan.empty()) return finish(std::move(r), MemStatus::Skipped, "no_buffer_fits_budget");

        const Clock clk;
        for (std::size_t bi = 0; bi < plan.size(); ++bi) {
            const auto& pb = plan[bi];
            if (pb.bytes > r.budget.bytes) continue;  // savunma: butce asla asilmaz
            VirtualBuffer vb(static_cast<std::size_t>(pb.bytes));
            if (!vb.ok()) return finish(std::move(r), MemStatus::Failed, "allocation_failed");
            r.peak_alloc_bytes = std::max(r.peak_alloc_bytes, pb.bytes);
            std::uint64_t* w = vb.words();
            const std::size_t n = vb.word_count();
            std::memset(w, 0x5A, static_cast<std::size_t>(pb.bytes));  // sayfalari onceden dokun (zamanlanmaz)
            if (static_cast<int>(bi) == cfg.inject_failure_at_buffer)
                return finish(std::move(r), MemStatus::Failed, "injected_failure");

            BufferResult br;
            br.buffer_bytes = pb.bytes;
            br.level_hint = pb.level_hint;

            const auto cancelled = [&] {
                r.buffers.push_back(br);  // kismi sonuc korunur; tampon RAII ile serbest
                return finish(std::move(r), MemStatus::Cancelled, "user_cancel");
            };

            Measure m = repeat_passes(clk, cfg, cancel, pb.bytes, [&](std::uint32_t) { g_sink = g_sink + seq_read_pass(w, n); });
            if (m.cancelled) return cancelled();
            br.seq_read_gbps = gbps(m);

            std::uint64_t last_pattern = 0;
            m = repeat_passes(clk, cfg, cancel, pb.bytes, [&](std::uint32_t p) {
                last_pattern = p;
                seq_write_pass(w, n, p);
            });
            if (m.cancelled) return cancelled();
            if (cfg.inject_corruption == MemoryConfig::Corruption::WritePass && static_cast<int>(bi) == cfg.inject_corruption_at_buffer)
                w[n / 2] ^= 1ull;  // test kancasi: dogrulama oncesi tek bit hatasi
            if (!verify_write_pass(w, n, last_pattern)) return finish(std::move(r), MemStatus::Failed, "memory_verify_mismatch");
            // EF-X22: adrese bagli desen (zamanlanmaz): tekduze desenin goremedigi adres/bit hatasi.
            core::measure::fill_address_pattern(w, n, 0xA5A5ull + pb.bytes);
            if (cfg.inject_corruption == MemoryConfig::Corruption::AddressPattern && static_cast<int>(bi) == cfg.inject_corruption_at_buffer)
                w[n / 3] ^= 1ull;  // test kancasi: adres deseninde tek bit hatasi
            if (core::measure::verify_address_pattern(w, n, 0xA5A5ull + pb.bytes))
                return finish(std::move(r), MemStatus::Failed, "memory_address_pattern_mismatch");
            g_sink = g_sink + w[n / 2];
            br.seq_write_gbps = gbps(m);

            m = repeat_passes(clk, cfg, cancel, n / 8, [&](std::uint32_t) { g_sink = g_sink + strided_pass(w, n); });
            if (m.cancelled) return cancelled();
            br.strided_ns = ns_per(m);

            // Buyuk tampon (DRAM veya LARGE): MT bant genisligi + gecikme. Cache seviyesi tamponlarinda
            // MT olculmez (cache etkisi bellek bant genisligi diye sunulmaz).
            const bool large = pb.level_hint == "DRAM" || pb.level_hint == "LARGE";
            if (large) {
                r.large_buffer_bytes = pb.bytes;
                r.large_buffer_to_l3 = buffer_l3_ratio(pb.bytes, r.caches);
                r.large_buffer_is_dram = is_dram_ratio(r.large_buffer_to_l3);  // < 8*L3 icin asla true degil
            }
            if (large && cfg.multi_thread) {
                const std::uint32_t want = cfg.threads != 0 ? cfg.threads : logical_processor_count();
                const MtSlicePlan sp = plan_mt_slices(pb.bytes, want);
                if (sp.threads > 0) {
                    r.mt.threads = sp.threads;
                    r.mt.buffer_bytes = pb.bytes;
                    r.mt.slice_bytes = sp.slice_bytes;
                    r.mt.buffer_to_l3 = r.large_buffer_to_l3;
                    r.mt.dram = r.large_buffer_is_dram;
                    const MtOp ops[] = {MtOp::Read, MtOp::Write, MtOp::Copy};
                    bool share_known = true;
                    bool any_window = false;
                    double share_lo = 0;
                    for (const MtOp op : ops) {
                        const MtMeasure mm = run_mt(clk, w, sp, op, cfg.mt_seconds_per_measure, cancel);
                        if (mm.cancelled) return cancelled();
                        if (mm.failed) return finish(std::move(r), MemStatus::Failed, "thread_create_failed");
                        if (mm.seconds <= 0.0 || mm.bytes == 0) continue;  // olculemedi: bos kalir (0 yazilmaz)
                        const double v = mt_gbps(mm);
                        if (op == MtOp::Read) r.mt.read_gbps = v;
                        else if (op == MtOp::Write) r.mt.write_gbps = v;
                        else r.mt.copy_gbps = v;
                        if (mm.min_thread_cpu_share) {
                            share_lo = any_window ? std::min(share_lo, *mm.min_thread_cpu_share) : *mm.min_thread_cpu_share;
                            any_window = true;
                        } else {
                            share_known = false;
                        }
                    }
                    if (share_known && any_window) {
                        r.mt.min_thread_cpu_share = share_lo;
                        r.mt.contended = share_lo < kMtUncontendedShare;
                    }
                }
            }

            if (n >= 16) {
                const std::size_t stride = chase_stride_words(n);
                build_chase(w, n, stride);
                m = chase(clk, cfg, cancel, w, stride);
                if (m.cancelled) return cancelled();
                br.chase_ns = ns_per(m);
                if (large) r.latency_ns = br.chase_ns;
            }
            r.buffers.push_back(std::move(br));
        }
        return finish(std::move(r), MemStatus::Completed, {});
    } catch (const std::bad_alloc&) {
        return finish(std::move(r), MemStatus::Failed, "out_of_memory");
    } catch (const std::exception&) {
        return finish(std::move(r), MemStatus::Failed, "internal_error");
    }
}

}  // namespace ef::benchmark::memory
