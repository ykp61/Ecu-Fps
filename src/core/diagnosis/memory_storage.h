#pragma once
// DIAG-03: RAM, VRAM ve G/C (disk) ayrimi (saf; birim testlenir). Kurallar "memio_v1" ile surumlenir.
//  - Sistem RAM: kapasite baskisi (bos RAM dusuk / commit sinira yakin) VE ayni pencerelerde kare
//    sivrileriyle birlikte suren hard page fault -> MemoryPressure. Hard fault SAYFALAMADIR (diskten sayfa
//    okuma); fiziksel RAM arizasi DEGILDIR. Sonuc is_hardware_fault = false tasir ve hicbir sinif bunu iddia etmez.
//  - VRAM: yalniz oyun PID'ine bagli surec kullanimi (measure::vram_valid_for_game) adaptor butcesiyle
//    karsilastirilir. Bu uygulamanin kendi DXGI butcesi asla kullanilmaz; adaptor toplami yalniz baglamdir.
//    Tumlesik (iGPU) / paylasimli bellekte "VRAM" sistem RAM'inin parcasidir: iki kez (hem RAM hem VRAM)
//    sayilmaz; baski yalniz RAM alaninda degerlendirilir.
//  - G/C: oyun disk okumasi kare sivrileriyle ayni pencerelerde -> IoStutter (takilma). Menu/yukleme
//    pencereleri (WindowScope::MenuOrLoading) surekli FPS sinirlarindan dislanir; uzun yukleme ayrica
//    "yukleme suresi" olarak raporlanir, surekli FPS sinirlayicisi olarak asla.
//  - Eksik metrik -> Insufficient. Guven rastgele yuzde degil, neden enum'udur.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/diagnosis/evidence.h"
#include "core/measure/frame_stats.h"
#include "core/measure/telemetry.h"

namespace ef::core::diagnosis {

inline constexpr std::string_view kMemoryStorageRules = "memio_v1";

// Pencere metrigi: deger + coznurluk yeterli mi (measure::Attribution::RootCandidate).
// Coznurlugu yetmeyen sayac kapasite baglami verir ama sivri ile korelasyonda kok neden sayilmaz.
struct MetricValue {
    std::optional<double> value;
    bool attributable = false;
};

enum class AdapterKind { Discrete, Integrated, Unknown };

struct AdapterMemory {
    AdapterKind kind = AdapterKind::Unknown;
    bool shares_system_memory = false;               // UMA / paylasimli bellek kullanan adaptor
    std::optional<std::uint64_t> dedicated_budget_bytes;  // adaptor yerel bellek butcesi (oyunun degil, adaptorun)
};

struct MemoryWindow {
    double start_ms = 0, end_ms = 0;
    WindowScope scope = WindowScope::Measured;
    measure::FrameStats frames;
    // Sistem RAM
    MetricValue ram_available_bytes, ram_total_bytes, commit_bytes, commit_limit_bytes, hard_faults_per_s;
    // Oyun sureci disk okumasi (bayt/s)
    MetricValue game_disk_read_bps;
    // Oyun VRAM: tipli okuma (kapsam + PID). Paylasimli kullanim ayri tutulur, RAM'e eklenmez.
    std::optional<measure::VramReading> game_vram_dedicated;
    std::optional<std::uint64_t> game_vram_shared_bytes;
};

// EvidenceWindow -> MemoryWindow. Metrik adlari: "ram_available_bytes", "ram_total_bytes", "commit_bytes",
// "commit_limit_bytes", "hard_faults_per_s", "game_disk_read_bps". VRAM tipli oldugu icin ayri verilir.
MemoryWindow memory_window_from(const EvidenceWindow& w);

struct MemoryThresholds {
    double spike_ratio = 2.0;               // p99 >= oran * ortalama kare suresi -> sivri
    double ram_available_low_ratio = 0.10;  // bos / toplam
    double commit_near_ratio = 0.95;        // commit / limit
    double hard_faults_high_per_s = 100.0;
    double vram_near_budget_ratio = 0.95;   // oyun ozel kullanim / adaptor butcesi
    double disk_read_high_bps = 20.0 * 1024 * 1024;
    std::size_t min_correlated_windows = 2;
};

enum class MemStorageClass { Healthy, MemoryPressure, VramPressure, IoStutter, Mixed, Insufficient };
enum class DomainVerdict { NotLimiting, Limiting, Insufficient, NotApplicable };

enum class EvidenceCode {
    RamAvailableLow,
    CommitNearLimit,
    HardFaultsWithSpikes,
    HardFaultsWithoutSpikes,           // karsit: fault var, takilma yok
    HardFaultsWithoutCapacityPressure, // karsit: fault var ama kapasite baskisi yok (dosya sayfalama olabilir)
    SpikesWithoutHardFaults,           // karsit
    RamMetricsMissing,
    VramNearBudgetWithSpikes,
    VramWithinBudget,                  // karsit
    VramOwnProcessBudgetIgnored,       // kendi DXGI butcesi oyun icin kullanilmadi
    VramAdapterTotalContextOnly,       // adaptor toplami oyuna ait degil
    VramPidMismatch,
    VramMetricsMissing,
    VramBudgetMissing,
    SharedMemoryCountedOnceAsRam,      // iGPU paylasimli bellek: RAM alaninda bir kez
    DiskReadWithSpikes,
    DiskReadWithoutSpikes,             // karsit
    SpikesWithoutDiskRead,             // karsit
    DiskMetricsMissing,
    LoadingReportedSeparately,         // menu/yukleme pencereleri surekli FPS sinirindan dislandi
    NoFrameSpikes,
    NoMeasuredWindows,
};

enum class MemoryConfidence {
    CorrelatedAcrossManyWindows,  // >= 3 iliskili pencere, karsit yok
    FewCorrelatedWindows,         // esik kadar iliskili pencere
    CounterEvidencePresent,       // destekleyen ve karsit birlikte
    NoLimitingSignal,             // olculdu, sinir bulunamadi
    MissingMetrics,
    CoarseResolution,             // sayac araligi sivriyi aciklamaya yetmiyor
    NoMeasuredWindows,
};

struct DomainFinding {
    DomainVerdict verdict = DomainVerdict::Insufficient;
    std::vector<EvidenceCode> supporting, counter;
    std::vector<EvidenceCode> notes;  // ne destek ne karsit: kaynak dislama / kapsam bilgisi
    MemoryConfidence reason = MemoryConfidence::MissingMetrics;
    std::size_t correlated_windows = 0;
};

struct LoadingSegment {
    double start_ms = 0, end_ms = 0;
    double duration_ms() const noexcept { return end_ms - start_ms; }
};

struct MemoryStorageDiagnosis {
    MemStorageClass cls = MemStorageClass::Insufficient;
    DomainFinding ram, vram, io;
    std::vector<EvidenceCode> supporting, counter, notes;  // alanlarin birlesimi
    MemoryConfidence reason = MemoryConfidence::NoMeasuredWindows;
    // Hard fault sayfalamadir; bu modul fiziksel RAM arizasi iddia etmez (sabit false, testle korunur).
    bool is_hardware_fault = false;
    // "Yukleme suresi": surekli FPS sinirindan ayri rapor. Toplam ve ardisik bolumler.
    std::vector<LoadingSegment> loading;
    double loading_total_ms = 0;
};

bool is_frame_spike(const measure::FrameStats& f, double spike_ratio) noexcept;

MemoryStorageDiagnosis diagnose_memory_storage(const std::vector<MemoryWindow>& windows, const AdapterMemory& adapter,
                                               std::uint32_t game_pid, const MemoryThresholds& th = {});

}  // namespace ef::core::diagnosis
