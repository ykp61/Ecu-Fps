// Uyumluluk B14: uzun test kosucusu gercek cihazda kisa butceyle (6 sn). Tek SceneSession, kesintisiz dilimler,
// tek saat (on_start ornekten once), iptal. Ayar yazmaz; yalniz ekran disi cizim. 10 dakikalik gercek kosu degildir.
// B14 incelemesi: kalici ornekleyici yolu sahte telemetriyle (gercek sensor degil) denetlenir: her ornek dilimin cizim
// penceresindeki okumayi tasir (saat tabani / pencere donusumu hatasi bos ornek verir), sensor kaybi SensorLost biter,
// baska ureticinin karti icin telemetri kullanilmaz ve "desteklenmiyor" bildirilir.
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "benchmark/endurance/endurance_runner.h"

using namespace ef::benchmark;

namespace {
constexpr double kFakeClock = 1500.0, kFakeLoad = 77.0;
}  // namespace

int main() {
    SceneConfig scene;
    scene.tier = Tier::Medium;
    EnduranceBudget budget;
    budget.planned_seconds = 6;
    budget.max_seconds = 12;
    std::atomic<bool> cancel{false};
    int starts = 0, samples_seen = 0;
    bool sample_before_start = false, supported = false;
    std::atomic<int> calls{0};
    int calls_at_start = -1;
    EnduranceProbes probes;
    probes.vendor_id = 0;  // her donanim karti
    probes.telemetry = [&] {
        ++calls;
        EnduranceTelemetry t;
        t.gpu_clock_mhz = kFakeClock;
        t.gpu_load_pct = kFakeLoad;
        return t;
    };
    probes.on_start = [&](bool sup) {
        ++starts;
        supported = sup;
        calls_at_start = calls.load();
    };
    probes.on_sample = [&](const EnduranceSample&) {
        if (starts == 0) sample_before_start = true;
        ++samples_seen;
    };
    const auto t0 = GetTickCount64();
    const auto run = run_endurance(scene, budget, cancel, probes);
    std::printf("endurance_probe: stop=%s samples=%zu wall=%.1fs telemetry_supported=%d probe_calls=%d\n", std::string(to_string(run.stop)).c_str(),
                run.samples.size(), static_cast<double>(GetTickCount64() - t0) / 1000.0, supported ? 1 : 0, calls.load());
    if (run.stop == EnduranceStop::DeviceLost && run.samples.empty()) {
        std::printf("endurance_probe: no D3D11 scene on this machine\n");
        return 77;
    }
    if (run.stop != EnduranceStop::Completed) return 1;
    if (starts != 1 || sample_before_start || samples_seen != static_cast<int>(run.samples.size())) return 2;
    if (run.samples.size() < 4) return 3;
    if (calls_at_start != 0) return 9;  // ornekleyici hazirlik sirasinda okumamali (t=0'dan sonra baslar)
    double prev = 0;
    for (const auto& s : run.samples) {
        std::printf("  t=%.2f fps=%.1f clock=%.0f load=%.0f\n", s.t_s, s.fps.value_or(-1), s.gpu_clock_mhz.value_or(-1), s.gpu_load_pct.value_or(-1));
        if (!s.fps || !(*s.fps > 0)) return 4;
        if (s.t_s <= prev) return 5;
        if (s.t_s - prev > 1.6) return 6;  // dilimler arasinda bosluk yok: ~1 sn arayla
        prev = s.t_s;
        if (supported) {
            // Her dilimin cizim penceresinde ~6 okuma vardir; saat tabani ya da pencere yanlissa ornek bos kalir.
            if (!s.gpu_clock_mhz || std::abs(*s.gpu_clock_mhz - kFakeClock) > 1e-9) return 10;
            if (!s.gpu_load_pct || std::abs(*s.gpu_load_pct - kFakeLoad) > 1e-9) return 11;
        } else if (s.gpu_clock_mhz || s.gpu_load_pct) {
            return 12;  // yazilim karti: telemetri kullanilmaz
        }
    }
    if (std::abs(run.report.duration_s - run.samples.back().t_s) > 1e-9) return 7;
    if (run.report.telemetry_unsupported == supported) return 13;

    // Sensor kaybi: 10 okumadan sonra saat gelmez -> SensorWatch ust uste 5 bos ornekte SensorLost.
    if (supported) {
        std::atomic<int> n{0};
        EnduranceProbes lost;
        lost.telemetry = [&] {
            EnduranceTelemetry t;
            if (++n <= 10) t.gpu_clock_mhz = kFakeClock, t.gpu_load_pct = kFakeLoad;
            return t;
        };
        budget.planned_seconds = 30;
        budget.max_seconds = 40;
        const auto l = run_endurance(scene, budget, cancel, lost);
        std::printf("endurance_probe: sensor-lost stop=%s samples=%zu\n", std::string(to_string(l.stop)).c_str(), l.samples.size());
        if (l.stop != EnduranceStop::SensorLost) return 14;
        if (l.samples.empty() || !l.samples.front().gpu_clock_mhz) return 15;
        if (l.samples.size() < static_cast<std::size_t>(SensorWatch::kMaxMissing) + 1) return 16;
        for (std::size_t i = l.samples.size() - SensorWatch::kMaxMissing; i < l.samples.size(); ++i)
            if (l.samples[i].gpu_clock_mhz) return 17;
        if (l.report.telemetry_unsupported) return 18;
    }

    // Baska ureticinin karti: telemetri hic cagrilmaz, kosu "desteklenmiyor" bildirir (okunamadi degil).
    {
        std::atomic<int> other_calls{0};
        bool other_supported = true;
        EnduranceProbes other;
        other.vendor_id = 0xFFFF;  // gecersiz PCI uretici kimligi: hicbir kart eslesmez
        other.telemetry = [&] {
            ++other_calls;
            EnduranceTelemetry t;
            t.gpu_clock_mhz = kFakeClock;
            return t;
        };
        other.on_start = [&](bool sup) { other_supported = sup; };
        budget.planned_seconds = 3;
        budget.max_seconds = 8;
        const auto o = run_endurance(scene, budget, cancel, other);
        std::printf("endurance_probe: other-vendor stop=%s samples=%zu calls=%d\n", std::string(to_string(o.stop)).c_str(), o.samples.size(),
                    other_calls.load());
        if (o.stop != EnduranceStop::Completed || other_supported || other_calls.load() != 0 || !o.report.telemetry_unsupported) return 19;
        for (const auto& s : o.samples)
            if (s.gpu_clock_mhz) return 20;
    }

    // Iptal: ~2 sn sonra; kosu Cancelled, karsilastirma yok (2 dakika dolmadi).
    std::atomic<bool> cancel2{false};
    std::thread stopper([&] {
        Sleep(2000);
        cancel2 = true;
    });
    budget.planned_seconds = 30;
    budget.max_seconds = 40;
    const auto c = run_endurance(scene, budget, cancel2, {});
    stopper.join();
    std::printf("endurance_probe: cancel stop=%s samples=%zu\n", std::string(to_string(c.stop)).c_str(), c.samples.size());
    if (c.stop != EnduranceStop::Cancelled || c.report.cold_fps) return 8;
    std::printf("endurance_probe: ok\n");
    return 0;
}
