#pragma once
// DIAG-02: CPU/GPU, kare siniri (cap) ve sunum beklemesi ayrimi (saf; birim testlenir).
// Kurallar coklu kanita dayanir:
//  - Dusuk GPU kullanimi TEK BASINA CPU/motor karari degildir; tek cekirdek/ana is parcacigi yuksekligi
//    veya GPU zaman damgasiyla "GPU mesgul << kare suresi" dogrulamasi ve cap'in dislanmasi gerekir.
//  - Yuksek kullanim normal performansta ariza degildir: GpuBound bir tanimdir; is_fault yalniz
//    beklenen FPS biliniyor ve altinda kaliniyorsa true olur.
//  - Eksik metrik "darbogaz yok" demek degildir -> Insufficient.
//  - MEAS-03: ContextOnly (ornek 1 Hz) baglam, ms olcekli kok neden olarak kullanilmaz.
// Cikti rastgele yuzde degil; destekleyen/karsit kanit kodlari ve guven nedeni (enum) tasir.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/diagnosis/evidence.h"
#include "core/diagnosis/release_gate.h"

namespace ef::core::diagnosis {

// Pencere sayaclarinda aranan metrik adlari.
inline constexpr std::string_view kMetricGpu3d = "gpu_engine_3d_pct";
inline constexpr std::string_view kMetricCpuMaxCore = "cpu_max_core_pct";
inline constexpr std::string_view kMetricCpuMainThread = "cpu_main_thread_pct";

enum class ComputeClass { GpuBound, CpuOrEngineBound, FrameCapOrPresentWait, Mixed, Healthy, Insufficient };

enum class ComputeConfidence {
    MultipleCorroborated,    // birbirinden bagimsiz en az iki kanit ayni yone
    DirectGpuTiming,         // GPU zaman damgasi (kare olcekli) dogrudan kanit
    SingleSignal,            // tek kanit; karsit kanit yok ama dogrulama da yok
    CapKnown,                // bilinen FPS siniri/VSync ile eslesme
    CapInferredFromRefresh,  // VSync durumu bilinmiyor; ekran yenilemesine sabitlenme
    CapStateUnknown,         // cap/VSync durumu bilinmiyor; CPU karari zayif
    ConflictingSignals,      // kanitlar celisiyor
    NoCorroboration,         // dusuk GPU var ama CPU tarafi dogrulanmadi
    ContextOnlyMetrics,      // sayaclar yalniz baglam (cozunurluk yetmez)
    MissingMetrics,          // gerekli sayac yok
    MissingFrames,           // gecerli/yeterli kare istatistigi yok
    NoLimitingSignal,        // sinirlayici isaret yok, gerekli veriler mevcut
    CalibrationUnverified    // BM-02: yayin kapisi kapali (gercek donanim holdout dogrulanmadi)
};

enum class PresentSync { Unknown, VsyncOn, VsyncOff };

// Kosu duzeyi girdiler (tum pencereler icin ortak).
struct RunComputeInputs {
    std::optional<double> display_refresh_hz;
    std::optional<double> fps_cap;  // bilinen oyun/surucu FPS siniri
    bool fps_cap_known_none = false;  // siniri olmadigi dogrulandi (fps_cap yoksa)
    PresentSync vsync = PresentSync::Unknown;
    std::optional<double> expected_fps;  // referans (ornek baseline); yoksa ariza karari verilmez
};

// Pencereye ozel istege bagli girdiler.
struct WindowComputeExtras {
    std::optional<double> gpu_busy_ms;        // GPU zaman damgasi: kare basi mesgul sure (ortalama)
    std::optional<double> frame_interval_cv;  // kare araligi degiskenlik katsayisi (stddev/ortalama)
};

struct ComputeVerdict {
    ComputeClass cls = ComputeClass::Insufficient;
    bool is_fault = false;  // yalniz beklenen performansin altinda + sinirlayici sinif
    ComputeConfidence reason = ComputeConfidence::MissingFrames;
    std::vector<std::string> supporting;  // kanit kodlari, ornek "gpu_util_high"
    std::vector<std::string> counter;     // karsit/eksik kanit kodlari, ornek "cpu_metric_missing"
};

// Esikler (surumlenir; testler bunlara dayanir).
inline constexpr double kGpuHighPct = 90.0;
inline constexpr double kGpuLowPct = 70.0;
inline constexpr double kCpuCoreHighPct = 90.0;
inline constexpr double kCapTolerance = 0.03;          // |fps - cap| / cap
inline constexpr double kLowVarianceCv = 0.10;         // sabit kare akisi
inline constexpr double kGpuBusyShortRatio = 0.60;     // gpu_busy / kare suresi
inline constexpr double kGpuBusyFullRatio = 0.90;
inline constexpr double kFaultBelowExpected = 0.90;    // beklenen FPS'in %90'i alti

ComputeVerdict classify_window(const EvidenceWindow& w, const RunComputeInputs& run, const WindowComputeExtras& extras = {});

struct RunComputeVerdict {
    ComputeVerdict overall;
    std::vector<ComputeVerdict> windows;  // timeline.windows ile ayni sira
};

// extras bos olabilir; dolu ise timeline.windows ile ayni boyutta olmalidir (degilse yok sayilir).
// Kullanilabilir pencere yoksa Insufficient; baskin sinif pencerelerin >= %60'i degilse Mixed.
RunComputeVerdict classify_run(const Timeline& tl, const RunComputeInputs& run,
                               const std::vector<WindowComputeExtras>& extras = {});

// BM-02: yayin kapisi. Kapi kapaliysa siniflandirici yayinlanabilir sinif uretmez: Insufficient +
// CalibrationUnverified + "diagnosis_calibration_unverified" karsi kaniti. Kalibrasyon degerlendirmesi
// (evaluate) kapiyi KULLANMAZ; aksi halde kapi hic acilamazdi.
ComputeVerdict classify_window_gated(const EvidenceWindow& w, const RunComputeInputs& run, const WindowComputeExtras& extras,
                                     const ReleaseStatus& gate);
RunComputeVerdict classify_run_gated(const Timeline& tl, const RunComputeInputs& run,
                                     const std::vector<WindowComputeExtras>& extras, const ReleaseStatus& gate);

std::string_view to_string(ComputeClass c) noexcept;
std::string_view to_string(ComputeConfidence r) noexcept;

}  // namespace ef::core::diagnosis
