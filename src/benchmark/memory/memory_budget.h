#pragma once
// BENCH-04: RAM alt testi icin saf (platformdan bagimsiz) butce ve tampon boyu plani.
// Butce: once OS ve kullanici isleri icin sabit/oransal pay ayrilir, kalanin bir orani alinir,
// sonra ust sinirla kirpilir. Kalan cok azsa test "atlandi" olur; zorla tahsis edilmez.
// Bu modul hicbir bellek ayirmaz; yalniz hesap yapar (birim test edilebilir).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ef::benchmark::memory {

inline constexpr std::uint64_t kKiB = 1024ull;
inline constexpr std::uint64_t kMiB = 1024ull * kKiB;
inline constexpr std::uint64_t kGiB = 1024ull * kMiB;

// GlobalMemoryStatusEx'ten gelen ham degerler (bayt).
struct MemorySnapshot {
    std::uint64_t total_phys = 0;
    std::uint64_t avail_phys = 0;
};

struct BudgetPolicy {
    std::uint64_t reserve_min_bytes = 2 * kGiB;   // OS + kullanici icin en az birakilacak pay
    double reserve_total_fraction = 0.10;         // ayrica toplam RAM'in bu orani da birakilir (buyugu gecerli)
    double usable_fraction = 0.25;                // payin ustundeki bos bellegin en fazla bu orani
    // Tek test icin mutlak ust sinir. 2 GiB: 128 MiB L3'te bile 16*L3 tampon sigar (BENCH-08);
    // asil sinir yine usable_fraction ve OS/kullanici payidir (kucuk RAM'de butce daha kucuk kalir).
    std::uint64_t cap_bytes = 2 * kGiB;
    std::uint64_t min_bytes = 32 * kMiB;          // bunun altinda test anlamsiz -> atlandi
};

struct Budget {
    bool skipped = true;
    std::uint64_t bytes = 0;          // izin verilen en buyuk toplam tahsis
    std::uint64_t reserve_bytes = 0;  // OS/kullanici icin dokunulmayan pay
    std::string reason;               // atlandiysa neden
};

// Saf fonksiyon: avail <= reserve veya sonuc < min ise skipped=true, bytes=0.
// Garanti: skipped degilse bytes <= cap ve bytes + reserve <= avail_phys.
Budget compute_budget(const MemorySnapshot& snap, const BudgetPolicy& policy);

// Algilanan cache boylari (bayt); 0 = bilinmiyor.
struct CacheSizes {
    std::uint64_t l1d = 0;
    std::uint64_t l2 = 0;
    std::uint64_t l3 = 0;
};

struct PlannedBuffer {
    std::uint64_t bytes = 0;
    // "L1" / "L2" / "L3": hedeflenen cache seviyesi (olculen degil).
    // "DRAM": tampon >= kDramMinL3Ratio * L3 (L3 boyu algilandi). "LARGE": L3'ten buyuk ama DRAM
    // iddiasi icin yetersiz (oran < 8 veya L3 bilinmiyor); "buyuk tampon" diye etiketlenir.
    std::string level_hint;
};

// BENCH-08: buyuk tampon L3'un en az 8 kati ise DRAM sayilir; tercih edilen boy 16 kat.
inline constexpr double kDramMinL3Ratio = 8.0;
inline constexpr std::uint64_t kDramPreferredL3Multiple = 16;
// tampon / algilanan L3; L3 bilinmiyorsa (0) bos.
std::optional<double> buffer_l3_ratio(std::uint64_t buffer_bytes, const CacheSizes& caches);
// Oran biliniyor ve >= kDramMinL3Ratio ise true (yalniz o zaman "DRAM" denir).
bool is_dram_ratio(const std::optional<double>& ratio);

// Cache boylarina gore L1/L2/L3 ve buyuk tampon (DRAM/LARGE) boylari. Her boy butceyi asmaz.
// Buyuk tampon: max(16*L3, 64 MiB), butceye kirpilir; L3'ten buyuk degilse eklenmez. Kirpilan boy
// 8*L3'un altinda kalirsa (veya L3 bilinmiyorsa) "LARGE" etiketlenir, asla "DRAM" denmez.
// Cache bilinmiyorsa sabit varsayilan boylar kullanilir (16K/256K/4M/128M "LARGE").
std::vector<PlannedBuffer> plan_buffers(const CacheSizes& caches, std::uint64_t budget_bytes);

// BENCH-08: cok is parcacikli bant genisligi icin tamponun dilimlere bolunmesi.
// Her is parcacigi kendi dilimine erisir. Dilim 64 KiB'e asagi yuvarlanir ve en az kMinMtSlice olur
// (gerekirse is parcacigi sayisi azaltilir). Garanti: threads * slice_bytes <= buffer_bytes.
inline constexpr std::uint64_t kMinMtSlice = 1 * kMiB;
struct MtSlicePlan {
    std::uint32_t threads = 0;     // 0 = olculemez (tampon cok kucuk)
    std::uint64_t slice_bytes = 0;
};
MtSlicePlan plan_mt_slices(std::uint64_t buffer_bytes, std::uint32_t threads);

}  // namespace ef::benchmark::memory
