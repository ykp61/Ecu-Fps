#pragma once
// INS-03 Windows kosucusu: sahneyi butce icinde surekli calistirir, her saniye ornek alir (FPS + GPU saati
// varsa), butce/iptal kontrolu her ornekte. Sonuc analyze_endurance ile yorumlanir. Saglik raporu degildir.
// B14: tek SceneSession (cihaz bir kez kurulur) uzerinde ~1 sn'lik dilimler art arda; telemetri yalniz dilimin
// gercek cizim penceresinde (measure_begin..measure_end) alinan okumalardan ortalanir.

#include <atomic>
#include <functional>

#include "benchmark/endurance/endurance.h"
#include "benchmark/scene/scene.h"

namespace ef::benchmark {

struct EnduranceRun {
    std::vector<EnduranceSample> samples;
    EnduranceStop stop = EnduranceStop::Completed;
    EnduranceReport report;
};

struct EnduranceTelemetry {
    std::optional<double> gpu_clock_mhz, gpu_load_pct;
    std::optional<bool> thermal, power;
};
struct EnduranceProbes {
    std::function<EnduranceTelemetry()> telemetry;  // yoksa sensorsuz izleme siniri raporlanir
    std::function<bool()> new_hardware_error;       // test sirasinda yeni olumcul donanim hatasi (seyrek cagrilir)
    std::uint32_t vendor_id = 0;                    // telemetri bu ureticinin karti icin; sahne baska kartta ise kullanilmaz
    // Uyumluluk B14: tek saat. on_start kosucu saati t=0 aninda (cihaz/sahne kurulduktan sonra, ilk dilimden hemen
    // once) is parcaciginda cagrilir; on_sample her dilim ornegi icin (t_s ayni saatten). UI ilerlemeyi buradan alir.
    // on_start argumani: GPU telemetrisi bu kosuda destekli mi (telemetri var, sahne donanim kartinda ve uretici uyuyor).
    std::function<void(bool telemetry_supported)> on_start;
    std::function<void(const EnduranceSample&)> on_sample;
};
EnduranceRun run_endurance(const SceneConfig& scene, const EnduranceBudget& budget, const std::atomic<bool>& cancel, const EnduranceProbes& probes = {});

}  // namespace ef::benchmark
