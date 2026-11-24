// BENCH-08: CPU + RAM is yukleri gercek Windows kosusu (pencere acmaz, sistem ayari degistirmez).
//  Kip: short (ctest, kisa) | quick (Hizli hazir ayar: CPU ST 5 + MT 10 sn, RAM ~8 sn) | full (Tam).
//  1) CPU: run_cpu_benchmark; durum Ok + checksum. Yuk dogrulamasi IS PARCACIGI BASINADIR: her MT
//     iscisinin GetThreadTimes (cekirdek + kullanici) CPU suresi kendi duvar suresine bolunur; her isci
//     >= %90 almali. quick/full kipinde kontrol edilir; short kipinde yalniz raporlanir. Esik altinda
//     kosu acikca "CEKISMELI" (contended) yazilir: rakip yuk (oyun, arka plan) isciyi CPU'dan etmistir.
//     Surec geneli GetProcessTimes / GetSystemTimes ornekleri yalniz bilgi icin basilir.
//  2) RAM: run_memory_bench; tampon tablosu, MT okuma/yazma/kopya GB/s, buyuk tampon gecikmesi.
//     Buyuk tampon / L3 orani basilir; oran < 8 ise "DRAM" degil "buyuk tampon" denir. MT pencerelerinde
//     de isci basina CPU payi ayni kuralla kontrol edilir.
// Yalniz kendi surecinin bellegini ayirir; oncelik/affinity degistirmez.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "benchmark/cpu/cpu_workload.h"
#include "benchmark/memory/memory_bench.h"

namespace cpu = ef::benchmark::cpu;
namespace mem = ef::benchmark::memory;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_fail;
}

double now_s() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}

double process_cpu_s() {
    FILETIME cr, ex, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &cr, &ex, &k, &u)) return 0.0;
    auto to_s = [](const FILETIME& ft) {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return static_cast<double>(v.QuadPart) * 1e-7;
    };
    return to_s(k) + to_s(u);
}

// Sistem geneli bos CPU suresi (tum islemcilerin toplami, saniye).
double system_idle_s() {
    FILETIME idle, k, u;
    if (!GetSystemTimes(&idle, &k, &u)) return 0.0;
    ULARGE_INTEGER v;
    v.LowPart = idle.dwLowDateTime;
    v.HighPart = idle.dwHighDateTime;
    return static_cast<double>(v.QuadPart) * 1e-7;
}

struct Sample {
    double wall;
    double cpu;
    double idle;
};

struct Util {
    double process = 0.0;    // bu surec CPU / (duvar * islemci)
    double idle = 0.0;       // sistem bos / (duvar * islemci)
    double others = 0.0;     // 1 - process - idle: diger sureclerin payi
};

// 50 ms aralikli (duvar, surec CPU) ornekleri.
class Sampler {
public:
    Sampler() {
        t_ = std::thread([this] {
            while (!stop_.load()) {
                {
                    std::lock_guard<std::mutex> g(mu_);
                    samples_.push_back({now_s(), process_cpu_s(), system_idle_s()});
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }
    ~Sampler() { finish(); }
    void finish() {
        if (t_.joinable()) {
            stop_ = true;
            t_.join();
        }
    }
    // [t0, t1] araligina en yakin ornekler arasindaki kullanim (0..1).
    Util utilization(double t0, double t1, double cores) const {
        Util out;
        if (samples_.size() < 2 || cores <= 0) return out;
        const Sample* a = nullptr;
        const Sample* b = nullptr;
        for (const auto& s : samples_) {
            if (!a && s.wall >= t0) a = &s;  // pencere icindeki ilk ornek
            if (s.wall <= t1) b = &s;        // pencere icindeki son ornek
        }
        if (!a || !b || b->wall - a->wall < 0.05) return out;
        const double cap = (b->wall - a->wall) * cores;
        out.process = (b->cpu - a->cpu) / cap;
        out.idle = (b->idle - a->idle) / cap;
        out.others = 1.0 - out.process - out.idle;
        return out;
    }

private:
    std::thread t_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mu_;
    std::vector<Sample> samples_;
};

const char* status_name(cpu::CpuRunStatus s) {
    switch (s) {
        case cpu::CpuRunStatus::Ok: return "ok";
        case cpu::CpuRunStatus::Cancelled: return "cancelled";
        case cpu::CpuRunStatus::ChecksumMismatch: return "checksum_mismatch";
        case cpu::CpuRunStatus::NoWork: return "no_work";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "short";
    const bool enforce_util = mode == "quick" || mode == "full";
    cpu::CpuConfig cc;
    mem::MemoryConfig mc;
    if (mode == "quick") {
        cc = cpu::quick_cpu_config();
        mc = mem::quick_memory_config();
    } else if (mode == "full") {
        cc = cpu::full_cpu_config();
        mc = mem::full_memory_config();
    } else {
        cc.st_seconds = 1.0;
        cc.mt_seconds = 2.0;
        mc.min_seconds_per_measure = 0.05;
        mc.max_seconds_per_measure = 0.10;
        mc.mt_seconds_per_measure = 0.4;
    }
    const double cores = static_cast<double>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    std::printf("cpu_mem_probe kip=%s mantiksal_islemci=%.0f hardware_concurrency=%u\n", mode.c_str(), cores,
                std::thread::hardware_concurrency());

    // 1) CPU
    std::printf("[1] CPU (%s) ST %.1f sn + MT %.1f sn\n", std::string(cpu::kWorkloadVersion).c_str(),
                cpu::variant_seconds(cc, false), cpu::variant_seconds(cc, true));
    Sampler sampler;
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    std::atomic<bool> cancel{false};
    const double c0 = now_s();
    const auto r = cpu::run_cpu_benchmark(cc, cancel);
    const double c1 = now_s();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));  // son ornek pencere sonrasina dussun
    sampler.finish();
    std::printf("  durum=%s checksum_ok=%d is_fps_prediction=%d toplam_sure=%.2f sn\n", status_name(r.status),
                r.checksum_ok ? 1 : 0, r.is_fps_prediction ? 1 : 0, c1 - c0);
    std::printf("  ST: %.3f birim/sn (geo ort.), %llu birim, %.2f sn\n", r.single_thread.work_units_per_second,
                static_cast<unsigned long long>(r.single_thread.chunks), r.single_thread.elapsed_s);
    std::printf("  MT: %.3f birim/sn (geo ort.), %u is parcacigi, %llu birim, %.2f sn, MT/ST %.2fx\n",
                r.multi_thread.work_units_per_second, r.multi_thread.threads,
                static_cast<unsigned long long>(r.multi_thread.chunks), r.multi_thread.elapsed_s,
                r.single_thread.work_units_per_second > 0 ? r.multi_thread.work_units_per_second / r.single_thread.work_units_per_second : 0.0);
    // MT fazlari kosunun sonundadir: [c1 - mt_elapsed, c1]; cekirdek fazlari sirayla.
    const double mt_start = c1 - r.multi_thread.elapsed_s;
    const Util um = sampler.utilization(mt_start, c1, cores);
    auto pct = [](const std::optional<double>& v) { return v ? *v * 100.0 : -1.0; };
    std::printf("  %-8s %10s %12s %9s %4s %9s %9s %9s %8s\n", "cekirdek", "ST b/sn", "MT b/sn", "MT/ST", "isci",
                "isci min%", "isci ort%", "isci max%", "surec%");
    double t = mt_start;
    for (std::size_t i = 0; i < r.single_thread.kernels.size() && i < r.multi_thread.kernels.size(); ++i) {
        const auto& s = r.single_thread.kernels[i];
        const auto& m = r.multi_thread.kernels[i];
        const Util u = sampler.utilization(t, t + m.elapsed_s, cores);
        t += m.elapsed_s;
        double avg = -1.0, mx = -1.0;
        if (!m.thread_cpu_share.empty()) {
            double sum = 0.0;
            mx = 0.0;
            for (const double v : m.thread_cpu_share) {
                sum += v;
                mx = std::max(mx, v);
            }
            avg = sum / static_cast<double>(m.thread_cpu_share.size()) * 100.0;
            mx *= 100.0;
        }
        std::printf("  %-8s %10.2f %12.2f %8.2fx %4u %8.1f%% %8.1f%% %8.1f%% %7.1f%%\n",
                    std::string(cpu::kernel_name(s.kernel)).c_str(), s.units_per_second, m.units_per_second,
                    s.units_per_second > 0 ? m.units_per_second / s.units_per_second : 0.0, m.threads,
                    pct(m.min_thread_cpu_share), avg, mx, u.process * 100.0);
    }
    std::printf("  ST isci CPU payi: %.1f%%; MT en dusuk isci CPU payi: %.1f%% (GetThreadTimes / isci duvar suresi)\n",
                pct(r.single_thread.min_thread_cpu_share), pct(r.multi_thread.min_thread_cpu_share));
    std::printf("  [bilgi] MT faz surec CPU %.1f%%, sistem bos %.1f%%, diger surecler %.1f%% (GetProcessTimes/GetSystemTimes)\n",
                um.process * 100.0, um.idle * 100.0, um.others * 100.0);
    if (r.multi_thread.memory_limited || r.single_thread.memory_limited)
        std::printf("  [NOT] bigdata isci/eleman sayisi RAM butcesiyle azaltildi (ST=%d MT=%d, eleman=%u, kucultuldu=%d, "
                    "butce olculdu=%d; skor varsayilanla karsilastirilamaz)\n",
                    r.single_thread.memory_limited ? 1 : 0, r.multi_thread.memory_limited ? 1 : 0,
                    r.multi_thread.bigdata_elements, r.multi_thread.bigdata_elements_reduced ? 1 : 0,
                    r.multi_thread.bigdata_budget_known ? 1 : 0);
    if (r.multi_thread.contended || r.single_thread.contended)
        std::printf("  [CEKISMELI] en az bir isci pencerenin %%90'indan az CPU aldi: bu kosunun skorlari rakip yuk altinda\n");
    check(r.status == cpu::CpuRunStatus::Ok && r.checksum_ok && r.scored, "CPU durum ok + checksum dogru");
    check(!r.is_fps_prediction, "skor FPS degil");
    check(r.single_thread.kernels.size() == cpu::kKernelCount && r.multi_thread.kernels.size() == cpu::kKernelCount,
          "5 cekirdek ST ve MT olculdu");
    check(r.multi_thread.threads == static_cast<std::uint32_t>(std::thread::hardware_concurrency()), "MT = tum mantiksal islemciler");
    check(r.single_thread.elapsed_s >= cpu::variant_seconds(cc, false) * 0.95, "ST suresi hazir ayara uygun");
    check(r.multi_thread.elapsed_s >= cpu::variant_seconds(cc, true) * 0.95, "MT suresi hazir ayara uygun");
    std::printf("  cekisme: ST=%s MT=%s; bigdata eleman ST=%u MT=%u\n", cpu::to_string(cpu::contention(r.single_thread)),
                cpu::to_string(cpu::contention(r.multi_thread)), r.single_thread.bigdata_elements,
                r.multi_thread.bigdata_elements);
    if (enforce_util) {
        // Pay bilinmiyorsa (bos) cekismesiz SAYILMAZ: Unknown da basarisizliktir.
        check(cpu::contention(r.single_thread) == cpu::Contention::Uncontended,
              "ST: isci pencerenin >= %90'inda CPU aldi (pay olculdu, cekismesiz)");
        check(cpu::contention(r.multi_thread) == cpu::Contention::Uncontended,
              "MT: her isci pencerenin >= %90'inda CPU aldi (pay olculdu, cekismesiz)");
    } else if (cpu::contention(r.multi_thread) != cpu::Contention::Uncontended ||
               cpu::contention(r.single_thread) != cpu::Contention::Uncontended) {
        std::printf("  [UYARI] kisa kosu cekismeli veya pay bilinmiyor; quick/full kipinde bu kontrol zorunludur\n");
    }

    // 2) RAM
    std::printf("[2] RAM (%s) olcum %.2f-%.2f sn, MT pencere %.2f sn\n", mem::kMemBenchVersion, mc.min_seconds_per_measure,
                mc.max_seconds_per_measure, mc.mt_seconds_per_measure);
    std::atomic<bool> cancel2{false};
    const double m0 = now_s();
    const auto mr = mem::run_memory_bench(mc, cancel2);
    const double m1 = now_s();
    std::printf("  durum=%s neden='%s' toplam_sure=%.2f sn\n", mem::to_string(mr.status), mr.reason.c_str(), m1 - m0);
    std::printf("  RAM toplam %.0f MiB, bos %.0f MiB, butce %.0f MiB, tepe tahsis %.0f MiB\n",
                static_cast<double>(mr.snapshot.total_phys) / 1048576.0, static_cast<double>(mr.snapshot.avail_phys) / 1048576.0,
                static_cast<double>(mr.budget.bytes) / 1048576.0, static_cast<double>(mr.peak_alloc_bytes) / 1048576.0);
    std::printf("  %-6s %12s %10s %10s %10s %10s\n", "hedef", "tampon", "oku GB/s", "yaz GB/s", "adim ns", "rast ns");
    for (const auto& b : mr.buffers)
        std::printf("  %-6s %9.0f KiB %10.2f %10.2f %10.2f %10.2f\n", b.level_hint.c_str(),
                    static_cast<double>(b.buffer_bytes) / 1024.0, b.seq_read_gbps.value_or(-1), b.seq_write_gbps.value_or(-1),
                    b.strided_ns.value_or(-1), b.chase_ns.value_or(-1));
    // L3 bilinmiyorsa ne DRAM ne onbellek denir (veri yetersiz).
    const char* big_label = !mr.large_buffer_to_l3 ? "buyuk tampon (L3 bilinmiyor)"
                            : mr.large_buffer_is_dram ? "DRAM" : "buyuk tampon (DRAM degil)";
    std::printf("  buyuk tampon %.0f MiB, L3 %.0f MiB, oran %.2fx -> %s\n",
                static_cast<double>(mr.large_buffer_bytes) / 1048576.0, static_cast<double>(mr.caches.l3) / 1048576.0,
                mr.large_buffer_to_l3.value_or(-1), big_label);
    std::printf("  MT (%s): %u is parcacigi, tampon %.0f MiB, dilim %.1f MiB: oku %.2f GB/s, yaz %.2f GB/s (yalniz store), kopya %.2f GB/s\n",
                big_label, mr.mt.threads, static_cast<double>(mr.mt.buffer_bytes) / 1048576.0,
                static_cast<double>(mr.mt.slice_bytes) / 1048576.0, mr.mt.read_gbps.value_or(-1),
                mr.mt.write_gbps.value_or(-1), mr.mt.copy_gbps.value_or(-1));
    std::printf("  MT en dusuk isci CPU payi: %.1f%%%s\n", mr.mt.min_thread_cpu_share ? *mr.mt.min_thread_cpu_share * 100.0 : -1.0,
                mr.mt.contended ? "  [CEKISMELI]" : "");
    std::printf("  %s gecikme (tek is parcacigi isaretci kovalama): %.1f ns\n", big_label, mr.latency_ns.value_or(-1));
    if (mr.status == mem::MemStatus::Completed) {
        const bool has_large = mr.large_buffer_bytes != 0;
        if (has_large) {
            check(mr.mt.read_gbps && mr.mt.write_gbps && mr.mt.copy_gbps, "MT okuma/yazma/kopya olculdu");
            check(mr.mt.read_gbps.value_or(0) > 0 && mr.mt.write_gbps.value_or(0) > 0 && mr.mt.copy_gbps.value_or(0) > 0,
                  "MT GB/s > 0");
            check(mr.latency_ns.has_value() && *mr.latency_ns > 0, "buyuk tampon gecikmesi olculdu");
            check(static_cast<std::uint64_t>(mr.mt.threads) * mr.mt.slice_bytes <= mr.mt.buffer_bytes, "dilimler tamponu asmaz");
            check(mr.large_buffer_is_dram == (mr.large_buffer_to_l3.value_or(0) >= mem::kDramMinL3Ratio),
                  "DRAM etiketi yalniz tampon >= 8*L3 ise");
            check(mr.mt.dram == mr.large_buffer_is_dram && mr.mt.buffer_bytes == mr.large_buffer_bytes,
                  "MT ve gecikme ayni buyuk tamponda");
            if (enforce_util) {
                check(mr.mt.min_thread_cpu_share.has_value(), "RAM MT isci CPU payi olculdu");
                check(!mr.mt.contended, "RAM MT: her isci pencerenin >= %90'inda CPU aldi");
            }
        } else {
            std::printf("  buyuk tampon butceye sigmadi: MT olcum yok (bos, 0 degil)\n");
            check(!mr.mt.read_gbps && !mr.latency_ns, "buyuk tampon yoksa MT/gecikme bos");
        }
        check(mr.peak_alloc_bytes <= mr.budget.bytes, "tepe tahsis <= butce");
    } else {
        check(mr.status == mem::MemStatus::Skipped && !mr.reason.empty(), "tamamlanmadiysa atlandi + neden");
    }

    std::printf("cpu_mem_probe: %s (%d hata)\n", g_fail ? "BASARISIZ" : "GECTI", g_fail);
    return g_fail ? 1 : 0;
}
