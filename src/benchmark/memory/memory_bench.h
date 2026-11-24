#pragma once
// BENCH-04: RAM alt testi (Uygulama Testi). Cache farkindalikli erisim olcumu:
//  - sirali okuma / sirali yazma bant genisligi (GB/s, 1 GB = 1e9 bayt)
//  - adimli (64 B, cache satiri basina bir erisim) okuma: ns/erisim
//  - rastgele isaretci kovalama gecikmesi: ns/erisim (bagimli yuk, prefetch etkisi az)
// Tampon boylari L1/L2/L3 ve buyuk tamponu (DRAM/LARGE) hedefler; boylar ve cache boylari sonucla saklanir.
// Kucuk tamponlarin yuksek hizi cache etkisidir; DRAM hizi diye sunulmaz (level_hint ayri).
// BENCH-08 (mem_v2): buyuk tamponda cok is parcacikli bant genisligi (okuma / yazma / kopya).
// Buyuk tampon hedefi 16*L3 (butceye kirpilir). Yalniz tampon >= 8*L3 ise (L3 algilandi) "DRAM"
// denir; aksi halde "buyuk tampon" (LARGE) olarak etiketlenir ve buffer_to_l3 orani sonucla verilir.
// N = mantiksal islemci sayisi; her is parcacigi tamponun kendi dilimine erisir (paylasim yok).
// Bayt sayimi (GB/s, 1 GB = 1e9 bayt):
//  - okuma: okunan bayt / sure.
//  - yazma: YALNIZ yazilan (store) bayt / sure. Normal (temporal) yazmada islemci hedef satiri once
//    okur (RFO, read-for-ownership); bu okuma trafigi SAYILMAZ. Bellek yolundaki gercek trafik
//    yazma GB/s degerinin yaklasik 2 kati olabilir.
//  - kopya: STREAM kurali, okunan + yazilan bayt / sure (hedefin RFO trafigi yine sayilmaz).
// Gecikme (latency_ns) = buyuk tamponda tek is parcacikli rastgele isaretci kovalama (TLB kacirmalari
// dahil); "DRAM gecikmesi" yalniz large_buffer_is_dram true ise denir.
// Cekisme: her MT is parcaciginin pencere boyunca aldigi CPU suresi (GetThreadTimes cekirdek +
// kullanici, ~15,6 ms tanecik) duvar suresine bolunur; en dusuk pay < kMtUncontendedShare ise
// kosu "contended" (rakip yuk; bant genisligi dusuk cikabilir) isaretlenir.
// Tahsis butcesi OS/kullanici payini korur (memory_budget.h); MT olcum ek tahsis yapmaz.
// Iptal/hata yolunda tum tahsisler RAII ile geri verilir. Bu bir performans olcumudur; RAM
// fiziksel ariza taramasi DEGILDIR.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "benchmark/memory/memory_budget.h"

namespace ef::benchmark::memory {

inline constexpr const char* kMemBenchVersion = "mem_v2";
inline constexpr const char* kMemBenchNote = "performans ölçümü, RAM arıza testi değildir";

enum class MemStatus { Completed, Cancelled, Skipped, Failed };
const char* to_string(MemStatus s) noexcept;

struct MemoryConfig {
    BudgetPolicy policy{};
    double min_seconds_per_measure = 0.05;  // tek is parcacikli her olcum en az bu sure (tekrarlarla)
    double max_seconds_per_measure = 0.40;  // ust sinir (DRAM kovalamasi uzun surmesin)
    bool multi_thread = true;               // buyuk tamponda MT okuma/yazma/kopya olcumu
    std::uint32_t threads = 0;              // MT is parcacigi sayisi; 0 = tum islemci gruplarindaki mantiksal CPU
    double mt_seconds_per_measure = 0.5;    // her MT olcum penceresi (okuma, yazma, kopya ayri ayri)
    // Test kancalari: gercek kullanimda bos/0 kalir.
    std::optional<MemorySnapshot> snapshot_override;  // butceyi sabit girdiyle zorla
    int inject_failure_at_buffer = -1;               // bu sira no'lu tampon ayrildiktan sonra hata
    // EF-X22.01: dogrulama hata enjeksiyonu. inject_corruption_at_buffer sira no'lu tamponda ilgili dogrulama oncesi
    // tek bir bit cevrilir; sonuc skor degil Failed olmali. Gercek kullanimda None / -1 kalir.
    enum class Corruption { None, WritePass, AddressPattern };
    Corruption inject_corruption = Corruption::None;
    int inject_corruption_at_buffer = -1;
};

// Hazir pencereler (duz yapilandirma degerleri).
// Hizli (~8 sn + buyuk tampon hazirligi): cache seviyesi olcumleri 0.12 sn, MT buyuk tampon olcumleri 1.5 sn.
// Tam (~30 sn + hazirlik): cache seviyesi olcumleri 0.5 sn, MT buyuk tampon olcumleri 5 sn.
inline constexpr double kQuickMinSecondsPerMeasure = 0.12;
inline constexpr double kQuickMaxSecondsPerMeasure = 0.25;
inline constexpr double kQuickMtSecondsPerMeasure = 1.5;
inline constexpr double kFullMinSecondsPerMeasure = 0.5;
inline constexpr double kFullMaxSecondsPerMeasure = 1.0;
inline constexpr double kFullMtSecondsPerMeasure = 5.0;
MemoryConfig quick_memory_config();
MemoryConfig full_memory_config();

// MT is parcacigi CPU payi bu degerin altindaysa olcum cekismeli (contended) sayilir.
inline constexpr double kMtUncontendedShare = 0.90;

// Cok is parcacikli bant genisligi (yalniz buyuk tampon - DRAM veya LARGE - varsa olculur).
struct MtBandwidth {
    std::uint32_t threads = 0;
    std::uint64_t buffer_bytes = 0;      // dilimlere bolunen buyuk tampon
    std::uint64_t slice_bytes = 0;       // is parcacigi basina dilim
    std::optional<double> buffer_to_l3;  // buffer_bytes / algilanan L3; L3 bilinmiyorsa bos
    bool dram = false;                   // yalniz buffer_to_l3 >= 8 ise true; aksi halde "buyuk tampon"
    std::optional<double> read_gbps;
    std::optional<double> write_gbps;    // yalniz yazilan bayt (RFO sayilmaz)
    std::optional<double> copy_gbps;     // STREAM kurali: okunan + yazilan bayt
    // Uc pencerede is parcacigi basina CPU suresi / duvar suresinin en dusugu (okunamazsa bos).
    std::optional<double> min_thread_cpu_share;
    bool contended = false;              // min_thread_cpu_share < kMtUncontendedShare
};

struct BufferResult {
    std::uint64_t buffer_bytes = 0;
    std::string level_hint;  // hedeflenen seviye ("L1"...); olculen seviye iddiasi degil
    std::optional<double> seq_read_gbps;
    std::optional<double> seq_write_gbps;
    std::optional<double> strided_ns;  // 64 B adim, erisim basina
    std::optional<double> chase_ns;    // rastgele bagimli erisim basina
};

struct MemoryResult {
    MemStatus status = MemStatus::Failed;
    std::string version = kMemBenchVersion;
    std::string note = kMemBenchNote;
    bool is_fault_test = false;  // her zaman false: ariza testi degil
    std::string reason;          // Skipped/Failed/Cancelled nedeni
    MemorySnapshot snapshot{};
    Budget budget{};
    CacheSizes caches{};
    std::uint64_t peak_alloc_bytes = 0;
    std::vector<BufferResult> buffers;  // tamamlanan tamponlar (iptalde kismi)
    MtBandwidth mt;                     // buyuk tampon yoksa / multi_thread=false ise bos
    // Buyuk tampon (MT ve gecikme olcumunun tamponu); yoksa 0 / bos.
    std::uint64_t large_buffer_bytes = 0;
    std::optional<double> large_buffer_to_l3;  // L3 bilinmiyorsa bos
    bool large_buffer_is_dram = false;         // oran >= 8: yalniz o zaman "DRAM" denir
    std::optional<double> latency_ns;          // buyuk tamponun chase_ns degeri (yoksa bos)
};

// Windows: GlobalMemoryStatusEx; okunamazsa sifirlar.
MemorySnapshot read_memory_snapshot();
// Windows: GetLogicalProcessorInformationEx ile veri/birlesik cache boylari (ilk cekirdek).
CacheSizes detect_cache_sizes();

// Engelleyen calisma; cancel herhangi bir anda true olabilir (olcum dongulerinde kontrol edilir).
MemoryResult run_memory_bench(const MemoryConfig& cfg, std::atomic<bool>& cancel);

}  // namespace ef::benchmark::memory
