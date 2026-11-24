#pragma once
// T16: Saglik testi (saf mantik; Win32 yok, birim testlenir). Kullanici baslatir: 60 sn CPU yuku altinda frekans/sinir
// ornekleri, RAM desen testi, GPU. Puan YOKTUR; olculmeyen deger uretilmez.
//  - CPU: sicaklik yalniz okunabilen bir sensor yolu varsa gosterilir; yoksa satir "sicaklik sensoru okunamadi" der.
//  - RAM: yazilan desen geri okunur; fark sayisi hata sayisidir. Tum desenler bitmeden sure dolarsa "kismi".
//  - GPU: bu testte yuk uygulanmaz (yerlesik sahne pencere ister; yeni ust duzey pencere acilmaz) -> Unavailable.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::core::diagnostics {

inline constexpr int kHealthCpuSeconds = 60;
inline constexpr int kHealthRamSeconds = 60;
inline constexpr std::uint64_t kHealthRamMaxBytes = 2ull << 30;       // en cok 2 GiB
inline constexpr std::uint64_t kHealthRamKeepFreeBytes = 2ull << 30;  // test sonrasi en az 2 GiB bos kalir
inline constexpr std::uint64_t kHealthRamMinBytes = 64ull << 20;      // altinda anlamli test sayilmaz
inline constexpr std::size_t kHealthMinCpuSamples = 30;

struct CpuSample {
    double pct_of_max_freq = 0.0;          // % Processor Performance (anma frekansina gore ortalama, 1 sn)
    std::optional<double> perf_limit_pct;  // % Performance Limit; okunamadiysa bos
    std::optional<double> temp_c;          // yalniz mevcut bir sensor yolu deger dondururse
};
enum class CpuVerdict { Stable, Throttled, Insufficient };

// Ornekler ~1 Hz. Insufficient: 30'dan az ornek veya ilk 10 sn ortancasi olculemedi (<= 0).
// Throttled: son 20 sn ortancasi ilk 10 sn ortancasinin %85'inin altinda VEYA sinir sayaci olan orneklerin >%10'unda
// perf_limit_pct < 100.
CpuVerdict cpu_verdict(const std::vector<CpuSample>& samples);

// Ilk 10 sn ve son 20 sn ortancalari (en az 30 ornek varsa); gosterim icin.
struct CpuSummary {
    double first_median = 0.0, last_median = 0.0;
    std::optional<double> max_temp_c;
};
std::optional<CpuSummary> cpu_summary(const std::vector<CpuSample>& samples);

// --- RAM desen testi: cagiranin verdigi tampon uzerinde (birim testlenir) ---
enum class RamPattern { WalkingOnes, WalkingOnesInverse, Prng, PrngInverse };
inline constexpr int kRamPatternCount = 4;
void ram_fill(std::uint64_t* buf, std::size_t words, RamPattern p, std::uint64_t seed);
// Beklenen desenden farkli kelime sayisi (temiz tamponda 0).
std::uint64_t ram_verify(const std::uint64_t* buf, std::size_t words, RamPattern p, std::uint64_t seed);

struct RamResult {
    std::uint64_t tested_bytes = 0;
    std::uint64_t errors = 0;
    bool completed = false;  // dort desenin hepsi en az bir kez tam gecti
};
enum class RamVerdict { Ok, Errors, Partial, NotRun };
RamVerdict ram_verdict(const RamResult& r);
// Test icin ayrilacak bayt: min(serbest/4, 2 GiB) ve test sonrasi en az 2 GiB serbest; 64 MiB'den azsa 0 (calistirma).
std::uint64_t ram_test_bytes(std::uint64_t free_bytes);

enum class GpuVerdict { Stable, Faulted, Unavailable };
// available: GPU yuku uygulanabildi mi. Faulted: calisma sirasinda aygit kaldirma/sifirlama veya yeni goruntu sifirlama olayi.
GpuVerdict gpu_verdict(bool available, bool device_removed_or_reset, bool new_display_incident);

enum class HealthRefusal { None, BatterySaver };
struct HealthReport {
    HealthRefusal refusal = HealthRefusal::None;  // None disindaysa olcum yapilmadi
    std::vector<CpuSample> cpu;
    RamResult ram;
    bool ram_run = false;  // RAM asamasi baslatildi mi
    GpuVerdict gpu = GpuVerdict::Unavailable;
};

// Oyun Hazirligi > Donanim satirlari. phase: 1 CPU, 2 RAM; elapsed_s: asamada gecen sn. Rapor yoksa sayi uretilmez.
std::vector<readiness::Check> health_test_checks(bool running, int phase, int elapsed_s, const std::optional<HealthReport>& report);

}  // namespace ef::core::diagnostics
