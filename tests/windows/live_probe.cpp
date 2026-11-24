// UI-09 Windows testi: gercek PDH ornekleyicisi (salt okuma, yonetici gerekmez).
//   ef_live_probe [ornek_sayisi=6]
// open() -> hemen bir ornek (oran sayaclari NotSampled olmali) -> N ornek 1 sn arayla.
// Kontroller: CPU ve RAM gercek makinede bilinmeli ve [0,100]; GPU/disk bilinmiyorsa neden
// NotSupported olmali (CI sanal makinesinde GPU Engine sayaci olmayabilir). Ornekleyicinin kendi
// CPU maliyeti (GetProcessTimes + QueryProcessCycleTime) ve duvar suresi olculup yazdirilir.
#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "core/metrics/live.h"
#include "platform/windows/metrics/pdh_sampler.h"

using namespace ef;
using contracts::Measurement;
using contracts::MissingReason;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("  FAIL %s\n", what);
    }
}

void fmt(const Measurement& m, char* out, std::size_t n) {
    if (m.has_value())
        std::snprintf(out, n, "%6.2f", *m.value());
    else
        std::snprintf(out, n, "missing(%s)", contracts::to_string(m.missing_reason()).data());
}

bool in_range(const Measurement& m) { return m.has_value() && *m.value() >= 0.0 && *m.value() <= 100.0; }

std::uint64_t cpu_100ns() {
    FILETIME c{}, e{}, k{}, u{};
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto q = [](FILETIME f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return q(k) + q(u);
}

std::uint64_t cycles() {
    ULONG64 c = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &c);
    return c;
}

double qpc_ms() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return 1000.0 * static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
}

}  // namespace

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 6;
    platform::metrics::PdhSampler s;
    const double t_open0 = qpc_ms();
    const std::uint64_t c_open0 = cpu_100ns();
    if (!s.open()) {
        std::printf("FAIL open (CPU sayaci eklenemedi)\n");
        return 1;
    }
    std::printf("open: %.2f ms wall, %.2f ms cpu; gpu_counter=%d disk_counter=%d\n", qpc_ms() - t_open0,
                static_cast<double>(cpu_100ns() - c_open0) / 1e4, s.gpu_supported() ? 1 : 0,
                s.disk_supported() ? 1 : 0);

    const auto first = s.sample();
    char a[48], b[48], c[48], d[48];
    fmt(first.cpu_pct, a, sizeof a);
    std::printf("sample 0 (open sonrasi): cpu=%s\n", a);
    check(!first.cpu_pct.has_value() && first.cpu_pct.missing_reason() == MissingReason::NotSampled,
          "ilk ornek CPU NotSampled olmali");
    check(in_range(first.ram_pct), "RAM ilk ornekte de bilinmeli");

    std::uint64_t cpu_total = 0, cyc_total = 0;
    double wall_total = 0, wall_max = 0;
    for (int i = 1; i <= n; ++i) {
        Sleep(1000);
        const std::uint64_t c0 = cpu_100ns(), y0 = cycles();
        const double w0 = qpc_ms();
        const auto x = s.sample();
        const double w = qpc_ms() - w0;
        cpu_total += cpu_100ns() - c0;
        cyc_total += cycles() - y0;
        wall_total += w;
        if (w > wall_max) wall_max = w;

        fmt(x.cpu_pct, a, sizeof a);
        fmt(x.gpu_pct, b, sizeof b);
        fmt(x.ram_pct, c, sizeof c);
        fmt(x.disk_pct, d, sizeof d);
        std::printf("sample %d t=%lld cpu=%s gpu=%s ram=%s (%llu/%llu MiB) disk=%s vols=%zu wall=%.2fms\n", i,
                    static_cast<long long>(x.mono_ms), a, b, c, static_cast<unsigned long long>(x.ram_used_mib),
                    static_cast<unsigned long long>(x.ram_total_mib), d, x.volumes.size(), w);

        check(in_range(x.cpu_pct), "CPU bilinmeli ve [0,100]");
        check(in_range(x.ram_pct), "RAM bilinmeli ve [0,100]");
        check(x.ram_total_mib > 0 && x.ram_used_mib <= x.ram_total_mib, "RAM MiB tutarli");
        check(in_range(x.gpu_pct) || x.gpu_pct.missing_reason() == MissingReason::NotSupported,
              "GPU bilinmeli veya NotSupported");
        check(in_range(x.disk_pct) || x.disk_pct.missing_reason() == MissingReason::NotSupported,
              "Disk bilinmeli veya NotSupported");
        check(core::metrics::ring_fraction(x.cpu_pct) >= 0.0f && core::metrics::ring_fraction(x.cpu_pct) <= 1.0f,
              "halka orani 0..1");
        bool any_vol = false;
        for (const auto& v : x.volumes) {
            if (v.total_gib && v.free_gib) {
                any_vol = true;
                check(*v.free_gib <= *v.total_gib, "bos alan <= toplam");
            }
        }
        check(any_vol, "en az bir sabit birimin alani okunmali");
        if (i == n) {
            for (const auto& v : x.volumes) {
                if (v.total_gib && v.free_gib)
                    std::printf("  volume %s total=%llu GiB free=%llu GiB\n", v.root.c_str(),
                                static_cast<unsigned long long>(*v.total_gib),
                                static_cast<unsigned long long>(*v.free_gib));
                else
                    std::printf("  volume %s Olculemedi\n", v.root.c_str());
            }
        }
    }
    if (n > 0) {
        std::printf("sampler cost per sample: cpu=%.3f ms (GetProcessTimes), cycles=%llu, wall avg=%.3f ms max=%.3f ms\n",
                    static_cast<double>(cpu_total) / 1e4 / n, static_cast<unsigned long long>(cyc_total / n),
                    wall_total / n, wall_max);
        std::printf("gpu re-expansions: %u\n", s.gpu_reexpansions());
        // GetProcessTimes ~15.6 ms tik cozunurlugundedir; tek ornek maliyeti icin ardisik ornek
        // patlamasi olculur (1 Hz kullanimdan daha agir; ust sinir verir).
        constexpr int kBurst = 100;
        const std::uint64_t bc0 = cpu_100ns(), by0 = cycles();
        const double bw0 = qpc_ms();
        for (int i = 0; i < kBurst; ++i) (void)s.sample();
        const double burst_cpu_ms = static_cast<double>(cpu_100ns() - bc0) / 1e4 / kBurst;
        std::printf("burst %d samples: cpu=%.3f ms/sample (GetProcessTimes), cycles=%llu/sample, wall=%.3f ms/sample\n",
                    kBurst, burst_cpu_ms, static_cast<unsigned long long>((cycles() - by0) / kBurst),
                    (qpc_ms() - bw0) / kBurst);
        // 1 Hz'de ornek basina 50 ms CPU = %5 tek cekirdek; bunun ustu hafiflik hedefini bozar.
        check(burst_cpu_ms < 50.0, "ornek basina CPU < 50 ms");
    }
    s.close();
    check(!s.is_open(), "close sonrasi kapali");
    const auto after = s.sample();
    check(!after.cpu_pct.has_value(), "kapali ornekleyici deger uydurmaz");
    // Uygulama pencere gorunmezken kapatir, gorunur olunca yeniden acar: kapanis tamponlari biraksa da yeniden acilis
    // ve ornekleme ilk acilistaki gibi calisir (ilk ornek NotSampled, ikinci ornekte CPU degeri).
    check(s.open(), "kapatildiktan sonra yeniden acilir");
    const auto re0 = s.sample();
    check(!re0.cpu_pct.has_value() && re0.cpu_pct.missing_reason() == MissingReason::NotSampled,
          "yeniden acilista ilk ornek CPU NotSampled olmali");
    Sleep(1100);
    const auto re1 = s.sample();
    check(in_range(re1.cpu_pct), "yeniden acilista ikinci ornekte CPU bilinmeli");
    check(in_range(re1.ram_pct), "yeniden acilista RAM bilinmeli");
    s.close();
    check(!s.is_open(), "ikinci close sonrasi kapali");
    std::printf("%s (%d hata)\n", g_fail ? "FAIL" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
