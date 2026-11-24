#pragma once
// DIAG-08 (oyun ici): kare dususlerini tespit edip kaynagini kanita dayali siniflandirir (saf; birim testlenir).
// Kurallar "drops_v2" ile surumlenir. v2: yalitilmis, benzer buyuklukte tekrarlayan ve kaynak sinyali
// tasimayan takilmalar "pipeline_compile_suspected" olarak isaretlenir (F-66; kesin teshis degil, oneri).
//  - Tespit tabani medyan kare araligidir; esik = max(2*medyan, p95). Esigi ASAN kareler, en fazla
//    merge_gap_frames normal kare boslugu birlesitirilerek "drop penceresi" olur. Pencere en az
//    min_slow_frames yavas kare veya min_window_ms (varsayilan 50 ms) sure tasimalidir.
//  - Pencere: baslangic/bitis ms, olculen kare sayisi, en kotu kare ms, olculen araliklardan ortalama FPS
//    ve kayip kare tahmini (sum/medyan - olculen kare; yalniz olculen araliklar, uydurma yok).
//  - Siniflandirma compute::classify_window (GPU/CPU/cap) sonucunu yeniden kullanir; buna drop'a ozgu
//    disk/G-C, bellek basinci ve yenileme/cap aralik deseni kanitlari eklenir. Hizali metrik yoksa veya
//    kaynak kanitlari celisiyorsa Insufficient; "bilinmiyor" durust kalir. Yuzde/kesinlik iddiasi yok.
//  - Zaman hizasi dogrulanmamis kaynak (aligned()==false) kok neden olarak KULLANILMAZ; rapor
//    unaligned_sources altinda listelenir. Coznurlugu yetmeyen sayac (ContextOnly, MEAS-03) kok neden
//    sayilmaz.
//  - En fazla max_windows (varsayilan 1000) pencere siniflandirilir; fazlasi truncated_aggregate altinda
//    toplanir ve windows_truncated / aggregated_windows / "windows_aggregated" kodu ile isaretlenir
//    (sessiz kirpma yok). Bellek O(n).
//  - Cikti insan metni degil, UI katalog anahtari uretilebilen kanit KODLARI tasir.
//  - F-67 kosu duzeyi (pencere disi) sinyal: hedef oyunun ozel bellek kullanicisi kosunun basi ve sonu
//    arasinda hem mutlak hem oransal olarak belirgin artmissa "game_private_growth" destegi ve oyunu
//    yeniden baslatma onerisi eklenir. Bu bir sizinti teshisi degildir; yalniz olculen artis raporlanir.
//    Kural surumu kPrivateGrowthRules ile ayridir; pencere siniflandirmasini degistirmez.
//  - F-61 kosu duzeyi GPU limit sinyali: yalniz drop bulunan kosuda, menu/yukleme kapsami disinda ve
//    en az gpu_limit_min_windows yuk alti NVAPI penceresi varsa surucunun isi/guc bayragi ve/veya yuk
//    alti saat dususu kanit olarak eklenir. Referans ayni kosuda yuk altinda gozlenen en yuksek saattir;
//    surucu boost degeri uydurulmaz. Bayrak + saat ayni yonde degilse neden atfedilmez; donanim arizasi
//    (PSU/VRM/anakart) iddiasi uretilmez. Kural surumu kGpuLimitRules ile ayridir.
//
// Metrik adlari (measure::TelemetrySample::metric). compute.h adlari da kullanilir:
//   gpu_engine_3d_pct, cpu_max_core_pct, cpu_main_thread_pct  (compute.h)
//   gpu_busy_ms           GPU zaman damgasi / kare basi mesgul ms (RootCandidate ise classify_window'a verilir)
//   disk_queue_len        disk istek kuyrugu (adet)
//   disk_io_ms            ortalama disk G/C gecikmesi (ms)
//   other_process_cpu_pct oyun disi sureclerin toplam CPU yuzdesi
//   commit_used_pct       commit kullanim / limit yuzdesi
//   hard_fault_rate       saniyedeki hard page fault (sayfa/s)
//   game_private_mib      hedef oyunun ozel bellegi (MiB)
//   game_vram_mib         hedef oyunun ayrik VRAM kullanimi (MiB)
// Bu adlar mevcut telemetry sozlesmesinde yoksa bu modul icin sabittir; anlam degisirse kDropRules
// surumu de degisir.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/diagnosis/compute.h"
#include "core/diagnosis/evidence.h"
#include "core/measure/telemetry.h"
#include "core/metrics/live.h"

namespace ef::core::diagnosis {

// drops_v3 (Uyumluluk B4 bosluk 14): pencere siniflandiricisi yenileme hizindan turetilen VRR sinir adaylarini da
// (yalniz dusuk kare araligi CV'si ile) kare siniri sayar; kanit kodu "vrr_cap_candidate".
inline constexpr std::string_view kDropRules = "drops_v3";
inline constexpr std::string_view kPrivateGrowthRules = "private_growth_v1";
// F-61: kosu duzeyi GPU limit sinyali ayri surumlenir; pencere siniflandirmasini degistirmez.
inline constexpr std::string_view kGpuLimitRules = "gpu_limit_v1";

inline constexpr std::string_view kMetricGamePrivateMib = "game_private_mib";
inline constexpr std::string_view kMetricGameVramMib = "game_vram_mib";  // F-63: oyun PID ayrik VRAM
inline constexpr std::string_view kMetricGpuBusyMs = "gpu_busy_ms";
inline constexpr std::string_view kMetricDiskQueueLen = "disk_queue_len";
inline constexpr std::string_view kMetricDiskIoMs = "disk_io_ms";
inline constexpr std::string_view kMetricOtherProcessCpu = "other_process_cpu_pct";
inline constexpr std::string_view kMetricCommitUsedPct = "commit_used_pct";
inline constexpr std::string_view kMetricHardFaultRate = "hard_fault_rate";

// Kanit kodlari (kararli anahtarlar; UI katalogu bunlardan metin uretir). compute.h'un urettigi
// kodlar (ornek "gpu_util_high", "cpu_core_saturated", "fps_at_cap") oldugu gibi tasinir.
inline constexpr std::string_view kEvRefreshIntervalMatch = "refresh_interval_match";
inline constexpr std::string_view kEvFpsCapIntervalMatch = "fps_cap_interval_match";
inline constexpr std::string_view kEvPresentIntervalMultiple = "present_interval_multiple";
inline constexpr std::string_view kEvVsyncOn = "vsync_on";
inline constexpr std::string_view kEvFpsCapKnown = "fps_cap_known";
inline constexpr std::string_view kEvCapInferredFromRefresh = "cap_inferred_from_refresh";
inline constexpr std::string_view kEvDiskQueueHigh = "disk_queue_high";
inline constexpr std::string_view kEvDiskQueueLow = "disk_queue_low";
inline constexpr std::string_view kEvDiskIoLatencyHigh = "disk_io_latency_high";
inline constexpr std::string_view kEvDiskIoLatencyLow = "disk_io_latency_low";
inline constexpr std::string_view kEvOtherProcessCpuHigh = "other_process_cpu_high";
inline constexpr std::string_view kEvOtherProcessCpuLow = "other_process_cpu_low";
inline constexpr std::string_view kEvCommitPressureHigh = "commit_pressure_high";
inline constexpr std::string_view kEvCommitPressureLow = "commit_pressure_low";
inline constexpr std::string_view kEvHardFaultRateHigh = "hard_fault_rate_high";
inline constexpr std::string_view kEvHardFaultRateLow = "hard_fault_rate_low";
inline constexpr std::string_view kEvNoAlignedMetrics = "no_aligned_metrics";
inline constexpr std::string_view kEvMetricsMissing = "metrics_missing";
inline constexpr std::string_view kEvConflictingSources = "conflicting_sources";
inline constexpr std::string_view kEvNoLimitingSignalInDrop = "no_limiting_signal_in_drop";
inline constexpr std::string_view kEvPipelineCompileSuspected = "pipeline_compile_suspected";
inline constexpr std::string_view kEvPipelineCompileAdvice = "pipeline_compile_advice";
inline constexpr std::string_view kEvGamePrivateGrowth = "game_private_growth";
inline constexpr std::string_view kEvGameRestartAdvice = "game_restart_advice";
inline constexpr std::string_view kEvGameVramNearCapacity = "game_vram_near_capacity";
inline constexpr std::string_view kEvGameVramAdvice = "game_vram_advice";
// F-61: surucu bildirimli isi/guc limiti ve nedeni atfedilemeyen yuk alti saat dususu.
inline constexpr std::string_view kEvGpuThermalLimit = "gpu_thermal_limit";
inline constexpr std::string_view kEvGpuPowerLimit = "gpu_power_limit";
inline constexpr std::string_view kEvGpuClockDropUnattributed = "gpu_clock_drop_unattributed";
inline constexpr std::string_view kEvGpuThermalAdvice = "gpu_thermal_advice";
inline constexpr std::string_view kEvGpuPowerAdvice = "gpu_power_advice";
inline constexpr std::string_view kEvGpuLimitUnattributedAdvice = "gpu_limit_unattributed_advice";
inline constexpr std::string_view kEvNoDropWindows = "no_drop_windows";
inline constexpr std::string_view kEvWindowsBelowMinimum = "windows_below_minimum";
inline constexpr std::string_view kEvFramesInsufficient = "frames_insufficient";
inline constexpr std::string_view kEvWindowLimitAggregated = "windows_aggregated";
inline constexpr std::string_view kEvWindowLimitNotClassified = "window_limit_not_classified";
inline constexpr std::string_view kEvUnalignedSourcesIgnored = "unaligned_sources_ignored";
// Uyumluluk B8 (bosluk 17): oyunun karti AMD/Intel; surucu isi/guc/saat siniri bu ureticide okunamaz (uretici API'si
// baglanmadi). Yalniz drop bulunan kosuda not; "GPU siniri yok" anlamina gelmez.
inline constexpr std::string_view kEvGpuVendorApiMissing = "gpu_vendor_api_missing";
inline constexpr std::string_view kEvNoClassifiedDrops = "no_classified_drops";
inline constexpr std::string_view kEvWindowClassesSplit = "window_classes_split";
inline constexpr std::string_view kEvCapAndResourceSignal = "cap_and_resource_signal";
// Onekler: "context_only:<metrik>", "metric_missing:<metrik>", "compute_reason:<ComputeConfidence>".
inline constexpr std::string_view kEvContextOnlyPrefix = "context_only:";
inline constexpr std::string_view kEvMetricMissingPrefix = "metric_missing:";
inline constexpr std::string_view kEvComputeReasonPrefix = "compute_reason:";

enum class DropSource {
    NoDrops,               // drop penceresi yok (saglikli kosu)
    GpuBound,              // GPU %3D yuksek / GPU zaman damgasi kare suresine yakin
    CpuOrEngineBound,      // CPU cekirdegi/ana is parcacigi doygun, GPU bos
    FrameCapOrPresentWait, // araliklar yenileme/cap periyodunun tam kati
    BackgroundOrIo,        // disk kuyrugu/gecikmesi veya diger surec CPU yuku
    MemoryPressure,        // commit basinci / hard page fault
    PipelineCompileSuspected,  // F-66: kaynak sinyalsiz, yalitilmis ve benzer tekrarlayan takilmalar
    Insufficient,          // hizali kanit yok, coznurluk yetmiyor veya kanit celiskili
};
inline constexpr std::size_t kDropSourceCount = 8;
std::string_view to_string(DropSource s) noexcept;

enum class DropConfidence {
    Corroborated,  // bagimsiz en az iki kanit ayni yone (veya dogrudan GPU zamanlamasi/kanitli cap)
    SingleSignal,  // tek kanit; karsit yok ama dogrulama da yok
    Insufficient,  // kanit yok/celiskili; "bilinmiyor" durust kalir
};
std::string_view to_string(DropConfidence c) noexcept;

enum class DropOutcome {
    Insufficient,  // yeterli kare yok; drop yok denmez
    NoDrops,       // yeterli kare var, esigi asan pencere yok
    DropsFound,    // en az bir drop penceresi var
};
std::string_view to_string(DropOutcome o) noexcept;

// Girdi telemetry kaynagi: SourceInfo + ornekler. aligned()==false olanlar kok neden sayilmaz.
struct DropTelemetrySource {
    SourceInfo info;
    std::vector<measure::TelemetrySample> samples;
};

// F-61: oyun ici bir NVAPI okumasi (pencere basina bir ornek). Alanlar ayni birincil fiziksel GPU'ya
// aittir; desteklenmeyen/okunamayan alan nullopt kalir. Maskede surucunun bildirdigi dusus nedeni
// bitleri tasinir; tek basina ariza teshisi degildir.
struct GpuLimitWindow {
    double t_ms = 0;  // kare ekseni (capture origin cikarilmis)
    std::optional<std::uint32_t> perf_decrease_mask;
    std::optional<double> load_pct;   // ayni GPU'nun surucu kullanim yuzdesi
    std::optional<double> clock_mhz;  // ayni GPU'nun grafik motoru saati
};

// Kosu baglami. compute alanlari classify_window'a aynen verilir (refresh/cap/VSync bilgisi).
struct DropRunInputs {
    std::string run_id;
    ClockDomain frame_domain = ClockDomain::Qpc;
    RunComputeInputs compute;
    std::vector<ScopeRange> scopes;  // menu/yukleme/isima pencereleri drop tespitinden cikarilir
    // F-63: oyunun calistigi/en buyuk ayrik VRAM'i olan adaptorun toplam ayrik VRAM'i (MiB). Yalniz
    // DropFound durumunda kosu baglami olarak kullanilir; OS'nin surec basina butcesi DEGILDIR ve
    // bilinmiyorsa nullopt kalir (uydurma esik yok).
    std::optional<double> adapter_dedicated_vram_mib;
    // F-61: yalniz hizali (capture origin bilinen) NVAPI ornekleri. Drop bulunan kosuda, olculen
    // kapsam icindeki yuk alti pencereler surucu isi/guc bayragi ve saat dususu icin degerlendirilir.
    std::vector<GpuLimitWindow> gpu_limit_windows;
    // B3 inceleme (bosluk 2): oyun ici "baska programlar CPU" icin HAM birikimli CPU sureleri (own = oyun sureci),
    // kare ekseninde mikrosaniye (t_us = kare ekseni ms * 1000), zamana gore sirali, yalniz hizali kosuda. Doluysa
    // her drop penceresinin other_process_cpu_pct degeri telemetri orneginden degil, bu okumalardan PENCERENIN KENDI
    // zamani icin hesaplanir (other_cpu_time_percent_around; kayan ortalama yayilmasi / gecikmesi yok). Pencerede
    // yeterli okuma yoksa deger eksik kalir (0 uydurulmaz).
    std::vector<metrics::CpuTimeReading> other_cpu_times;
    // Uyumluluk B8 (bosluk 17): oyunun cizdigi kartin ureticisi icin surucu sinir API'si yok (AMD/Intel). Doluysa drop
    // bulunan kosuda kEvGpuVendorApiMissing notu eklenir (isi/guc kaniti yoklugu "sinir yok" diye okunmasin).
    bool gpu_vendor_limit_api_missing = false;
};

struct DropInput {
    DropRunInputs run;
    std::vector<FramePoint> frames;
    std::vector<DropTelemetrySource> sources;
    std::vector<std::string> metric_names;  // ek metrikler; bilinen adlar her zaman eklenir
    // Spec 2026-11-12 B: kare basi GPU mesgul suresi (ms), frames ile BIRE BIR hizali (i. deger = i. karenin GPU
    // suresi; ETW kare olayinin kendi alani, ayri saat / hiza yok). Bos = olculmedi; boyut frames ile esit degilse
    // yok sayilir. Doluysa her drop penceresinde kare basi ortalama (toplam / kare sayisi) classify_window'a
    // gpu_busy_ms olarak verilir (kGpuBusyShortRatio / kGpuBusyFullRatio) ve "gpu_busy_ms" telemetri sayacinin
    // yerini alir. Sonlu olmayan / negatif deger o pencerede GPU suresini "bilinmiyor" yapar (0 uydurulmaz).
    std::vector<double> frame_gpu_busy_ms;
};

// Esikler (surumlenir; testler bunlara dayanir).
struct DropThresholds {
    double spike_factor = 2.0;         // esik >= spike_factor * medyan
    double min_window_ms = 50.0;       // tek yavas kare bu sureyi asarsa pencere olur
    std::size_t min_slow_frames = 2;   // veya en az bu kadar yavas kare
    std::size_t merge_gap_frames = 1;  // arada en fazla bu kadar normal kare birlesir
    std::size_t max_windows = 1000;    // siniflandirilan pencere siniri (fazlasi toplulanir)
    std::size_t min_frames = 5;        // altinda tespit yok -> Insufficient
    double refresh_tolerance = 0.05;   // |oran - yakin tamsayi| <= %5 (cap/VSync aralik eslesmesi)
    double disk_queue_high = 2.0;
    double disk_io_ms_high = 50.0;
    double other_process_cpu_high = 25.0;
    double other_process_cpu_min_span_ms = 250.0;  // other_cpu_times: kisa pencere en az bu kadar genisletilir
    double commit_used_high = 95.0;
    double hard_fault_rate_high = 100.0;
    std::size_t worst_windows = 5;     // raporda en kotu pencere sayisi
    // F-66 pipeline/PSO derleme supheli deseni: en az bu kadar pencere, her biri en fazla
    // shader_max_slow_frames yavas kare / shader_max_window_ms sure, en kotu kareleri birbirine
    // gore en fazla shader_worst_tolerance oraninda farkli ve hicbirinde sinirlayici kaynak sinyali yok.
    std::size_t shader_min_windows = 2;
    std::size_t shader_max_slow_frames = 2;
    double shader_max_window_ms = 500.0;
    double shader_worst_tolerance = 0.5;
    // F-67 kosu duzeyi bellek buyumesi: en az bu kadar ornek, ilk/son uc ornegin medyani arasinda en az
    // bu sure, hem en az private_growth_min_mib artis hem de ilk degere gore private_growth_ratio oran.
    std::size_t private_growth_samples = 6;
    double private_growth_min_span_ms = 120000.0;
    double private_growth_min_mib = 64.0;
    double private_growth_ratio = 0.20;
    // F-63 kosu duzeyi VRAM basinci: yalniz drop penceresi varken, en az bu kadar gecerli ornekte
    // oyunun tepe ayrik VRAM'i adaptor toplaminin bu oranina ulasirsa baglam kaniti eklenir.
    // Adaptor toplami bu degerden kucukse (paylasimli/yazilim adaptor) sinyal uretilmez.
    std::size_t vram_pressure_min_samples = 3;
    double vram_pressure_ratio = 0.90;
    double vram_pressure_min_adapter_mib = 512.0;
    // F-61 kosu duzeyi GPU limit tanisi: en az bu kadar yuk alti NVAPI penceresi yoksa isi/guc/saat
    // kaniti uretilmez. Okuma 2 Hz'dir; 4 pencere ~2 s surerli gozlem demektir.
    std::size_t gpu_limit_min_windows = 4;
};

struct DropWindow {
    std::size_t first_frame = 0;  // filtrelenmis kare dizisindeki indeksler (izlenebilirlik)
    std::size_t last_frame = 0;
    double start_ms = 0;
    double end_ms = 0;  // start + olculen aralik toplami
    std::size_t frame_count = 0;
    std::size_t slow_frame_count = 0;
    double worst_ms = 0;
    double avg_fps = 0;  // olculen araliklardan
    std::size_t estimated_lost_frames = 0;
    DropSource source = DropSource::Insufficient;
    DropConfidence confidence = DropConfidence::Insufficient;
    std::vector<std::string> supporting, counter, notes;
};

struct DropReport {
    std::string run_id;
    DropOutcome outcome = DropOutcome::Insufficient;
    std::size_t valid_frames = 0;          // kapsam ici gecerli kare
    std::size_t excluded_frames = 0;       // NaN/sonsuz/<=0 aralik
    std::size_t scope_excluded_frames = 0; // menu/yukleme/isima
    double measured_ms = 0;                // gecerli araliklarin toplami
    double median_interval_ms = 0;
    double p95_interval_ms = 0;
    double threshold_ms = 0;
    std::size_t total_windows = 0;    // tespit edilen (min pencere suzgecinden gecen) pencere
    std::size_t analyzed_windows = 0; // siniflandirilan (<= max_windows)
    std::size_t aggregated_windows = 0;
    bool windows_truncated = false;
    std::optional<DropWindow> truncated_aggregate;  // sinir ustu pencerelerin tek toplu kaydi
    double drop_ms = 0;             // pencere sureleri toplami
    double drop_time_ratio = 0;     // drop_ms / measured_ms (0..1)
    std::vector<DropWindow> worst;  // en kotu en fazla DropThresholds::worst_windows
    std::array<std::size_t, kDropSourceCount> distribution{};
    // Baskin kaynak/guven yalniz drop penceresi varken ve siniflandirilabildiginde doludur.
    std::optional<DropSource> dominant;
    std::optional<DropConfidence> confidence;
    std::vector<std::string> supporting, counter, notes;  // kosu duzeyi kanit kodlari
    std::vector<std::string> unaligned_sources;           // kok neden olarak kullanilmadi
};

std::size_t source_count(const DropReport& r, DropSource s) noexcept;

// Rapor metin anahtarlari (yerellestirme UI katalogunda). Bilinmeyen kanit kodu ham gosterilmez;
// "drop.evidence.unknown" gibi durust genel anahtara duser.
std::string_view drop_source_key(DropSource s) noexcept;
std::string_view drop_confidence_key(DropConfidence c) noexcept;
std::string_view drop_outcome_key(DropOutcome o) noexcept;
std::string drop_evidence_key(std::string_view code);

// Saf tespit + siniflandirma. Girdi kopyalanmaz; bellek O(n) (n = gecerli kare).
DropReport detect_drops(const DropInput& in, const DropThresholds& th = {});

// Client tarafinda 1 Hz PDH ornegi + kare eksenine tasinmis zaman. aligned=false: QPC kokeni
// bilinmiyor/gecersiz; ornekler tasinir ama hizasiz kaynak olarak isaretlenir (kok neden sayilmaz).
struct LiveDropSample {
    double t_ms = 0;  // kare zaman ekseninde (origin cikarilmis); aligned=false ise anlamsiz
    bool aligned = false;
    metrics::LiveSnapshot snapshot;
};

// LiveSnapshot -> DropTelemetrySource. Yalniz gercekten okunan metrik adlari yazilir; eksik deger
// missing_reason ile tasinir. Olcum yoksa bos vektor (uydurma ornek yok).
std::vector<DropTelemetrySource> drop_sources_from_live(const std::vector<LiveDropSample>& in);

// F-61: LiveDropSample -> GpuLimitWindow. Yalniz hizali ornekler alinir; bu ornekte NVAPI alani
// okunmadiysa (missing) o alan nullopt kalir. Hicbir limit alani olmayan ornek pencere uretmez.
std::vector<GpuLimitWindow> gpu_limit_windows_from_live(const std::vector<LiveDropSample>& in);

}  // namespace ef::core::diagnosis
