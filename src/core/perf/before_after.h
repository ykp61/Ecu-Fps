#pragma once
// OPT-08: Performans modu oncesi/sonrasi kaynak raporu (saf cekirdek; Win32 yok, birim testlenir).
// Kurallar:
//  - Girdi 1 Hz ornekleridir (tipik 10 sn); eksik ornek (nullopt / NaN / sonsuz) atlanir, 0 sayilmaz.
//  - Fark = sonra - once. %95 guven araligi Welch t-araligi ile hesaplanir (kucuk t-tablosu).
//  - Bir tarafta < 3 gecerli ornek -> InsufficientData. Metrik hic gelmediyse -> Unsupported.
//  - Aralik 0'i iceriyorsa -> NoSignificantDifference ("anlamli fark yok"); improved yalniz
//    Significant iken doludur.
//  - Kazanc yonu: CPU/GPU/RAM kullanimi icin dusuk, bos RAM/disk icin yuksek deger iyilesmedir.
//  - Temizlik ile bosaltilan alan olculmus kesin degerdir (bayt); aralik hesaplanmaz.
//  - Varsayimsal deger uretilmez.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/metrics/live.h"

namespace ef::core::perf {

struct VolumeFree {
    std::string root;                        // "C:\" (utf-8)
    std::optional<std::uint64_t> free_bytes;  // okunamazsa nullopt
};

// Tek 1 Hz ornegi. Olculemeyen alan nullopt kalir.
struct ResourceSample {
    std::optional<double> cpu_pct;
    std::optional<double> gpu_pct;
    std::optional<double> ram_used_pct;
    std::optional<std::uint64_t> ram_free_bytes;
    std::vector<VolumeFree> volumes;
};

// Canli anlik goruntuden ornek. Eksik Measurement -> nullopt. Bos RAM yalniz ram_pct mevcut ve
// used <= total > 0 iken (total - used) MiB * 2^20 olur. Disk bos alani free_gib * 2^30 (GiB
// cozunurlugu).
ResourceSample sample_from_live(const metrics::LiveSnapshot& s);

enum class Direction { LowerIsBetter, HigherIsBetter };

enum class Verdict {
    Significant,
    NoSignificantDifference,  // UI: "anlamli fark yok"
    InsufficientData,         // metrik var ama bir tarafta < 3 gecerli ornek
    Unsupported,              // metrik iki tarafta da hic yok
};
std::string_view to_string(Verdict v) noexcept;

struct MetricComparison {
    std::string key;  // "cpu_pct", "gpu_pct", "ram_used_pct", "ram_free_bytes", "disk_free_bytes:<kok>"
    Direction direction = Direction::LowerIsBetter;
    std::size_t n_before = 0;
    std::size_t n_after = 0;
    std::optional<double> mean_before;  // ornek yoksa nullopt
    std::optional<double> mean_after;
    std::optional<double> diff;     // mean_after - mean_before; iki taraf >= 3 ornekte doludur
    std::optional<double> ci_low;   // %95 aralik alt siniri (fark icin)
    std::optional<double> ci_high;
    Verdict verdict = Verdict::Unsupported;
    std::optional<bool> improved;   // yalniz Significant iken
};

struct BeforeAfterReport {
    std::vector<MetricComparison> metrics;
    std::optional<std::uint64_t> cleanup_freed_bytes;  // kesin deger; temizlik yoksa nullopt
};

// Iki yonlu %95 Welch t kritik degeri. df kesirli olabilir; tablo arasinda kucuk df secilir
// (temkinli). df < 1 -> df=1 degeri.
double t_crit_95(double df) noexcept;

// Tek metrigi karsilastirir. NaN/sonsuz ornekler atlanir.
MetricComparison compare_metric(std::string key, Direction dir, const std::vector<double>& before,
                                const std::vector<double>& after);

// Asgari anlamli etki: istatistiksel olarak secilebilen ama pratikte onemsiz fark "anlamli fark yok"
// sayilir (yuzde metrikleri 1 puan, bos RAM 100 MiB, bos disk 1 GiB). compare_before_after uygular.
double min_effect_for(std::string_view key) noexcept;
void apply_min_effect(MetricComparison& m, double min_effect) noexcept;

// Tum metrikleri karsilastirir. Sira: cpu_pct, gpu_pct, ram_used_pct, ram_free_bytes, sonra
// disk_free_bytes:<kok> (koklerin ilk gorulme sirasi; once, sonra).
BeforeAfterReport compare_before_after(const std::vector<ResourceSample>& before,
                                       const std::vector<ResourceSample>& after,
                                       std::optional<std::uint64_t> cleanup_freed_bytes = std::nullopt);

}  // namespace ef::core::perf
