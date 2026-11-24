// F-41: CPU kumesi P/E kapsama siniflandirmasi (saf). En yuksek EfficiencyClass P sayilir;
// izinli kume yoksa iddia uretilmez.
#include <cstdint>
#include <vector>

#include "core/inventory/cpu_topology.h"
#include "ef_test.h"

using ef::core::inventory::classify_cpu_set_coverage;
using ef::core::inventory::CpuSetCoverage;
using ef::core::inventory::CpuSetRecord;

namespace {

CpuSetRecord rec(std::uint8_t cls, bool allowed) { return CpuSetRecord{cls, allowed}; }

}  // namespace

EF_TEST(cpu_set_coverage_empty_is_unknown) {
    EF_CHECK(classify_cpu_set_coverage({}) == CpuSetCoverage::Unknown);
}

EF_TEST(cpu_set_coverage_single_class_is_not_hybrid) {
    EF_CHECK(classify_cpu_set_coverage({rec(0, true), rec(0, true)}) == CpuSetCoverage::NotHybrid);
    EF_CHECK(classify_cpu_set_coverage({rec(0, true), rec(0, false)}) == CpuSetCoverage::NotHybrid);
}

EF_TEST(cpu_set_coverage_both_classes_allowed) {
    const std::vector<CpuSetRecord> v{rec(1, true), rec(1, true), rec(0, true), rec(0, false)};
    EF_CHECK(classify_cpu_set_coverage(v) == CpuSetCoverage::PerformanceAndEfficiency);
}

EF_TEST(cpu_set_coverage_performance_only) {
    const std::vector<CpuSetRecord> v{rec(1, true), rec(0, false), rec(0, false)};
    EF_CHECK(classify_cpu_set_coverage(v) == CpuSetCoverage::PerformanceOnly);
}

EF_TEST(cpu_set_coverage_efficiency_only) {
    const std::vector<CpuSetRecord> v{rec(1, false), rec(0, true), rec(0, true)};
    EF_CHECK(classify_cpu_set_coverage(v) == CpuSetCoverage::EfficiencyOnly);
}

EF_TEST(cpu_set_coverage_without_allowed_sets_is_unknown) {
    EF_CHECK(classify_cpu_set_coverage({rec(1, false), rec(0, false)}) == CpuSetCoverage::Unknown);
}

EF_TEST(cpu_set_coverage_uses_max_class_as_performance) {
    // Degerler surucuden gelir; mutlak sayi degil en yuksek sinif P kabul edilir.
    EF_CHECK(classify_cpu_set_coverage({rec(2, true), rec(0, true)}) == CpuSetCoverage::PerformanceAndEfficiency);
    EF_CHECK(classify_cpu_set_coverage({rec(2, true), rec(1, false)}) == CpuSetCoverage::PerformanceOnly);
}
