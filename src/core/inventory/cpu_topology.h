#pragma once
// CPU topoloji ozeti (saf). Platform katmani GetLogicalProcessorInformationEx kayitlarini verir.

#include <cstdint>
#include <string>
#include <vector>

#include "contracts/inventory.h"

namespace ef::core::inventory {

struct CoreRecord {
    std::uint32_t logical_count = 1;  // bu cekirdekteki mantiksal islemci (SMT ile 2)
    std::uint8_t efficiency_class = 0;  // yuksek = performans cekirdegi (hibrit)
    std::uint16_t group = 0;
};

struct CacheRecord {
    std::uint8_t level = 0;
    std::uint32_t size_bytes = 0;
    bool unified_or_data = true;
};

struct CpuTopology {
    contracts::Field<std::uint32_t> packages, physical_cores, logical_processors;
    contracts::Field<std::uint32_t> performance_cores, efficiency_cores;
    contracts::Field<bool> hybrid, smt;
    contracts::Field<std::uint32_t> l3_total_kib, l3_slices, l3_max_slice_kib;
};

CpuTopology summarize_cpu(std::uint32_t packages, const std::vector<CoreRecord>& cores,
                          const std::vector<CacheRecord>& caches);

// F-41: bir surec/is parcaciginin calisabildigi CPU kumelerinin cekirdek sinifi kapsamasi.
// Windows CPU Set kayitlari logical processor + EfficiencyClass verir; "allowed" cagiran tarafindan
// secili/default CPU kumeleri veya benzesme maskesinden turetilir. Yalniz okuma; yazma yok.
struct CpuSetRecord {
    std::uint8_t efficiency_class = 0;  // yuksek = performans cekirdegi (hibrit)
    bool allowed = false;               // surec/is parcacigi bu kumede calisabilir
};

enum class CpuSetCoverage : std::uint8_t {
    Unknown,                   // kume yok veya izinli kume okunamadi
    NotHybrid,                 // tum kumeler ayni EfficiencyClass
    PerformanceAndEfficiency,  // izinli kumelerde hem P hem E var
    PerformanceOnly,           // yalniz P sinifi izinli
    EfficiencyOnly,            // yalniz E sinifi izinli
};

// En yuksek EfficiencyClass P, digerleri E kabul edilir. Izinli kume yoksa Unknown (iddia yok).
CpuSetCoverage classify_cpu_set_coverage(const std::vector<CpuSetRecord>& sets);

}  // namespace ef::core::inventory
