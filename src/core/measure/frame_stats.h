#pragma once
// MEAS-04: kare istatistigi (saf; birim testlenir). Tanimlar docs/05_BENCHMARK_SPEC.md ile surumlenir:
//  - Ortalama render FPS = gecerli aralik sayisi / gecerli toplam sure (ms -> s).
//  - %1 dusuk FPS ("low1_v1") = en yavas ceil(N*0.01) kare suresinin ortalamasi -> 1000/ms.
//  - p95/p99 kare suresi: en yakin sira (nearest-rank) yuzdelik.
//  - p99,9 ve %0,1 dusuk ("low01_v1") yalniz N >= kMinIntervalsLow01 iken hesaplanir; daha az ornekte
//    deger yoktur (tek kareyi "0,1% dusuk" diye sunma).
//  - Desen etiketi ("frame_pattern_v1") medyan tabanlidir: esik = 2 x medyan. Nadir (<= %1) esik
//    asimi + p99 <= 2 x medyan -> micro_stutter; yaygin esik asimi -> spiky; aksi -> stable.
//  - NaN, sonsuz, <= 0 araliklar dislanir ve sayilir (exclusion reason). Asgari N altinda sonuc yok.

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ef::core::measure {

inline constexpr std::string_view kLowDefinition = "low1_v1";
inline constexpr std::string_view kLow01Definition = "low01_v1";
inline constexpr std::string_view kFramePatternRules = "frame_pattern_v1";
inline constexpr std::size_t kMinIntervals = 100;       // daha azinda %1 dusuk tek kareye dayanir
inline constexpr std::size_t kMinIntervalsLow01 = 1000;  // 0,1% diliminde en az ~1 kare anlamli

enum class FramePattern {
    Insufficient,  // yeterli kare yok; desen iddiasi yok
    Stable,        // esik asimi yok ve p99 medyanin 1,25 katinda veya altinda
    MicroStutter,  // nadir (<= %1) uzun kareler; p99 hala 2 x medyan altinda
    Spiky,         // esik asimlari yaygin veya p99 2 x medyanin ustunde
};
std::string_view to_string(FramePattern p) noexcept;
// "insufficient" | "stable" | "micro_stutter" | "spiky"; bilinmeyen -> Insufficient.
FramePattern frame_pattern_from_string(std::string_view s) noexcept;

struct FrameStats {
    std::size_t n = 0;              // gecerli aralik
    std::size_t excluded = 0;       // NaN/sonsuz/<=0
    double valid_seconds = 0;
    std::optional<double> avg_fps, low1_fps, low01_fps, p95_ms, p99_ms, p99_9_ms;
    double median_ms = 0;           // nearest-rank p50 (desen tabani)
    double max_ms = 0;              // en yavas gecerli kare
    FramePattern pattern = FramePattern::Insufficient;
    bool sufficient = false;        // n >= kMinIntervals
};

FrameStats compute_frame_stats(const std::vector<double>& intervals_ms, std::size_t min_n = kMinIntervals);

// Kare surelerinden saniye basina gercek FPS serisi. Eksik son saniye atilir; nokta sayisi en fazla max_points.
inline constexpr std::size_t kMaxFpsSeries = 120;
std::vector<double> fps_per_second(const std::vector<double>& frame_ms, std::size_t max_points = kMaxFpsSeries);
// EF-X19: yalniz cizim icin esit kovali ortalama. Dusuk yuzdelikler her zaman ham kare surelerinden hesaplanir.
std::vector<double> downsample_for_chart(const std::vector<double>& series, std::size_t max_points);

// EF-X19.02: grafik serisi ve istatistik AYNI ham kare suresi vektorunden uretilir. Seri (fps_per_second, en fazla
// max_points nokta; uzun kosuda bloklar ortalanir) yalniz cizim icindir; %1/%0,1 dusuk ve p95/p99 hicbir zaman seriden
// hesaplanmaz. Bu nedenle max_points degeri istatistigi degistiremez.
struct ChartedFrameStats {
    FrameStats stats;
    std::vector<double> fps_series;
};
ChartedFrameStats frame_stats_with_series(const std::vector<double>& frame_ms, std::size_t max_points = kMaxFpsSeries,
                                          std::size_t min_n = kMinIntervals);

}  // namespace ef::core::measure
