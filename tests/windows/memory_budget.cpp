// BENCH-04: RAM alt testi gercek Windows kosusu.
//  1) Tam kosu: butce OS payini korur, tepe tahsis <= butce, sonuc mem_v2 + "ariza testi degil" notu.
//  2) Iptal: baska is parcacigi iptal eder; donuste surec commit/working set tabana yakin.
//  3) Enjekte hata: en buyuk tampon ayrildiktan sonra hata; tahsis geri verilir.
//  4) Dusuk bellek girdisi: atlandi + neden, hic tahsis yok.
// Yalniz kendi surecinin bellegini ayirir; sistem ayari degistirmez.
#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "benchmark/memory/memory_bench.h"

using namespace ef::benchmark::memory;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_fail;
}

struct Usage {
    std::uint64_t commit = 0;  // PrivateUsage
    std::uint64_t ws = 0;      // WorkingSetSize
    std::uint64_t peak_commit = 0;
};

Usage usage() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    Usage u;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        u.commit = pmc.PrivateUsage;
        u.ws = pmc.WorkingSetSize;
        u.peak_commit = pmc.PeakPagefileUsage;
    }
    return u;
}

double mib(std::uint64_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); }

// Tabana donus toleransi: CRT/heap/thread yigini gurultusu icin 16 MiB.
constexpr std::uint64_t kTolerance = 16 * kMiB;

bool near_base(const Usage& base, const Usage& now) {
    return now.commit <= base.commit + kTolerance && now.ws <= base.ws + kTolerance;
}

void print_usage(const char* tag, const Usage& base, const Usage& now) {
    std::printf("  %s: commit %.1f MiB (taban %.1f), ws %.1f MiB (taban %.1f)\n", tag, mib(now.commit), mib(base.commit),
                mib(now.ws), mib(base.ws));
}

void print_result(const MemoryResult& r) {
    std::printf("  status=%s version=%s is_fault_test=%d reason='%s'\n", to_string(r.status), r.version.c_str(),
                r.is_fault_test ? 1 : 0, r.reason.c_str());
    std::printf("  note: %s\n", r.note.c_str());
    std::printf("  RAM toplam %.0f MiB, bos %.0f MiB, pay %.0f MiB, butce %.0f MiB, tepe tahsis %.0f MiB\n",
                mib(r.snapshot.total_phys), mib(r.snapshot.avail_phys), mib(r.budget.reserve_bytes),
                mib(r.budget.bytes), mib(r.peak_alloc_bytes));
    std::printf("  cache L1d %llu KiB, L2 %llu KiB, L3 %llu KiB\n",
                static_cast<unsigned long long>(r.caches.l1d / kKiB), static_cast<unsigned long long>(r.caches.l2 / kKiB),
                static_cast<unsigned long long>(r.caches.l3 / kKiB));
    std::printf("  %-6s %12s %10s %10s %12s %10s\n", "hedef", "tampon", "oku GB/s", "yaz GB/s", "adim ns", "rast ns");
    for (const auto& b : r.buffers) {
        std::printf("  %-6s %9.0f KiB %10.2f %10.2f %12.2f %10.2f\n", b.level_hint.c_str(),
                    static_cast<double>(b.buffer_bytes) / 1024.0, b.seq_read_gbps.value_or(-1),
                    b.seq_write_gbps.value_or(-1), b.strided_ns.value_or(-1), b.chase_ns.value_or(-1));
    }
}

}  // namespace

int main() {
    const Usage base = usage();
    print_usage("baslangic", base, base);

    // 1) Tam kosu
    std::printf("[1] tam kosu\n");
    {
        std::atomic<bool> cancel{false};
        const MemoryConfig cfg;
        const MemoryResult r = run_memory_bench(cfg, cancel);
        print_result(r);
        const Usage after = usage();
        print_usage("sonra", base, after);
        check(r.version == "mem_v2", "surum mem_v2");
        check(!r.is_fault_test && r.note.find("RAM ar") != std::string::npos, "ariza testi degil notu");
        check(r.budget.skipped || r.budget.bytes + r.budget.reserve_bytes <= r.snapshot.avail_phys,
              "butce + OS payi <= bos bellek");
        check(r.peak_alloc_bytes <= r.budget.bytes, "tepe tahsis <= butce");
        if (r.status == MemStatus::Completed) {
            check(!r.buffers.empty(), "tampon sonuclari var");
            bool all = true;
            for (const auto& b : r.buffers)
                all = all && b.seq_read_gbps && b.seq_write_gbps && b.strided_ns && b.chase_ns && *b.seq_read_gbps > 0;
            check(all, "her tamponda olcumler dolu");
            check(after.peak_commit >= base.commit + r.peak_alloc_bytes * 9 / 10, "tahsis gercekten yapildi (tepe commit)");
        } else {
            check(r.status == MemStatus::Skipped && !r.reason.empty(), "tamamlanmadiysa atlandi + neden");
        }
        check(near_base(base, after), "tam kosu sonrasi commit/ws tabana dondu");
    }

    // 2) Iptal (en buyuk tamponun icindeyken olasi): 300 ms sonra iptal
    std::printf("[2] iptal\n");
    {
        std::atomic<bool> cancel{false};
        MemoryConfig cfg;
        cfg.min_seconds_per_measure = 0.2;
        std::thread t([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            cancel.store(true);
        });
        const auto t0 = std::chrono::steady_clock::now();
        const MemoryResult r = run_memory_bench(cfg, cancel);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        t.join();
        const Usage after = usage();
        std::printf("  status=%s reason='%s' tamamlanan tampon=%zu sure=%.0f ms\n", to_string(r.status), r.reason.c_str(),
                    r.buffers.size(), ms);
        print_usage("iptal sonrasi", base, after);
        if (r.status != MemStatus::Skipped) {
            check(r.status == MemStatus::Cancelled, "durum cancelled");
            check(ms < 300 + 1500, "iptal 1.5 sn icinde etkili");
        }
        check(near_base(base, after), "iptal sonrasi commit/ws tabana dondu");
    }

    // 3) Enjekte hata: son (en buyuk) tampon ayrildiktan sonra
    std::printf("[3] enjekte hata\n");
    {
        std::atomic<bool> cancel{false};
        MemoryConfig cfg;
        const auto plan = plan_buffers(detect_cache_sizes(), compute_budget(read_memory_snapshot(), cfg.policy).bytes);
        cfg.inject_failure_at_buffer = plan.empty() ? 0 : static_cast<int>(plan.size() - 1);
        const MemoryResult r = run_memory_bench(cfg, cancel);
        const Usage after = usage();
        std::printf("  status=%s reason='%s' tepe tahsis %.0f MiB\n", to_string(r.status), r.reason.c_str(),
                    mib(r.peak_alloc_bytes));
        print_usage("hata sonrasi", base, after);
        if (r.status != MemStatus::Skipped) check(r.status == MemStatus::Failed && !r.reason.empty(), "durum failed + neden");
        check(near_base(base, after), "hata sonrasi commit/ws tabana dondu");
    }

    // 4) Dusuk bos bellek girdisi -> atlandi, tahsis yok
    std::printf("[4] dusuk bellek\n");
    {
        std::atomic<bool> cancel{false};
        MemoryConfig cfg;
        cfg.snapshot_override = MemorySnapshot{8 * kGiB, 1536 * kMiB};
        const MemoryResult r = run_memory_bench(cfg, cancel);
        std::printf("  status=%s reason='%s'\n", to_string(r.status), r.reason.c_str());
        check(r.status == MemStatus::Skipped && !r.reason.empty(), "atlandi + neden");
        check(r.peak_alloc_bytes == 0 && r.buffers.empty(), "hic tahsis yok");
    }

    std::printf("memory_budget: %s (%d hata)\n", g_fail ? "BASARISIZ" : "GECTI", g_fail);
    return g_fail ? 1 : 0;
}
