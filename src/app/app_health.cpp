// T16: saglik testi kosucusu. Yalniz kullanici baslatir. Ayar yazilmaz, sureclere/oyunlara dokunulmaz.
//  - CPU: mantiksal cekirdek basina bir yuk is parcacigi (dusuk oncelik) 60 sn; saniyede bir PDH hiz/sinir ornegi.
//    Sicaklik: mevcut hicbir CPU sicaklik okuyucusu yok -> temp_c hep bos (uydurulmaz).
//  - RAM: min(serbest/4, 2 GiB), test sonrasi >= 2 GiB serbest; dort desen yaz/oku, en cok 60 sn.
//  - GPU: yerlesik sahne pencere ister (yeni ust duzey pencere acilmaz) -> yuk uygulanmaz, Unavailable.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "app/readiness_collect.h"
#include "app/worker.h"
#include "platform/windows/metrics/cpu_speed_sampler.h"

namespace ef::app {

namespace {
namespace dg = core::diagnostics;
using Clock = std::chrono::steady_clock;

bool battery_saver_on() {
    SYSTEM_POWER_STATUS ps{};
    return GetSystemPowerStatus(&ps) && ps.SystemStatusFlag == 1;
}

// Bekler; iptal gelirse true doner.
bool sleep_cancellable(Clock::time_point until, const std::atomic<bool>& cancel) {
    while (Clock::now() < until) {
        if (cancel.load()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cancel.load();
}

void load_loop(const std::atomic<bool>& stop) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    volatile double x = 1.000001;
    std::uint64_t n = 0;
    while (!stop.load(std::memory_order_relaxed)) {
        for (int i = 0; i < 20000; ++i) x = x * 1.0000001 + 0.0000001;
        ++n;
    }
    (void)n;
}

bool run_cpu(const std::atomic<bool>& cancel, const std::function<void(int, int)>& progress, std::vector<dg::CpuSample>& out) {
    platform::metrics::CpuSpeedSampler sampler;
    const bool opened = sampler.open();
    std::atomic<bool> stop{false};
    std::vector<std::thread> workers;
    const unsigned n = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < n; ++i) workers.push_back(spawn_worker([&stop] { load_loop(stop); }));
    const auto start = Clock::now();
    bool cancelled = false;
    for (int s = 1; s <= dg::kHealthCpuSeconds; ++s) {
        if (sleep_cancellable(start + std::chrono::seconds(s), cancel)) {
            cancelled = true;
            break;
        }
        if (opened) {
            const auto r = sampler.sample();
            if (r.performance_pct) {
                dg::CpuSample cs;
                cs.pct_of_max_freq = *r.performance_pct;
                cs.perf_limit_pct = r.limit_pct;
                out.push_back(cs);  // temp_c: okuyucu yok, bos kalir
            }
        }
        progress(1, s);
    }
    stop = true;
    for (auto& t : workers) t.join();
    return !cancelled;
}

bool run_ram(const std::atomic<bool>& cancel, const std::function<void(int, int)>& progress, dg::RamResult& res) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms)) return true;  // okunamadi: NotRun (tested_bytes 0)
    const std::uint64_t bytes = dg::ram_test_bytes(ms.ullAvailPhys);
    if (bytes == 0) return true;
    void* mem = VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) return true;
    auto* buf = static_cast<std::uint64_t*>(mem);
    const std::size_t words = static_cast<std::size_t>(bytes / sizeof(std::uint64_t));
    res.tested_bytes = bytes;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::seconds(dg::kHealthRamSeconds);
    bool cancelled = false;
    bool all_passed = false;
    std::uint64_t seed = static_cast<std::uint64_t>(start.time_since_epoch().count()) | 1;
    for (int pass = 0; !cancelled && !all_passed && Clock::now() < deadline; ++pass) {
        int done = 0;
        for (int p = 0; p < dg::kRamPatternCount; ++p) {
            if (cancel.load()) {
                cancelled = true;
                break;
            }
            if (Clock::now() >= deadline) break;
            const auto pat = static_cast<dg::RamPattern>(p);
            dg::ram_fill(buf, words, pat, seed + static_cast<std::uint64_t>(p));
            res.errors += dg::ram_verify(buf, words, pat, seed + static_cast<std::uint64_t>(p));
            ++done;
            progress(2, static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count()));
        }
        if (done == dg::kRamPatternCount) all_passed = true;
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    }
    res.completed = all_passed;
    VirtualFree(mem, 0, MEM_RELEASE);
    return !cancelled;
}
}  // namespace

std::optional<dg::HealthReport> run_health_test(const std::atomic<bool>& cancel, const std::function<void(int, int)>& progress) {
    dg::HealthReport rep;
    if (battery_saver_on()) {
        rep.refusal = dg::HealthRefusal::BatterySaver;
        return rep;
    }
    if (!run_cpu(cancel, progress, rep.cpu)) return std::nullopt;
    rep.ram_run = true;
    if (!run_ram(cancel, progress, rep.ram)) return std::nullopt;
    rep.gpu = dg::gpu_verdict(false, false, false);  // GPU yuku uygulanmadi
    return rep;
}

}  // namespace ef::app
