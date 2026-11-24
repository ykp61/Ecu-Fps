#include "core/inventory/cpu_topology.h"

#include <algorithm>

namespace ef::core::inventory {

using contracts::Field;
using contracts::Provenance;

CpuTopology summarize_cpu(std::uint32_t packages, const std::vector<CoreRecord>& cores,
                          const std::vector<CacheRecord>& caches) {
    CpuTopology t;
    if (cores.empty()) {
        const auto u = Field<std::uint32_t>::unknown("no processor core records");
        t.packages = t.physical_cores = t.logical_processors = t.performance_cores = t.efficiency_cores = u;
        t.hybrid = t.smt = Field<bool>::unknown("no processor core records");
    } else {
        std::uint32_t logical = 0;
        std::uint8_t max_class = 0, min_class = 255;
        bool smt = false;
        for (const auto& c : cores) {
            logical += c.logical_count;
            smt = smt || c.logical_count > 1;
            max_class = std::max(max_class, c.efficiency_class);
            min_class = std::min(min_class, c.efficiency_class);
        }
        const bool hybrid = max_class != min_class;
        std::uint32_t perf = 0;
        for (const auto& c : cores)
            if (c.efficiency_class == max_class) ++perf;
        t.packages = packages ? Field<std::uint32_t>::known(packages, Provenance::Api)
                              : Field<std::uint32_t>::unknown("no package records");
        t.physical_cores = Field<std::uint32_t>::known(static_cast<std::uint32_t>(cores.size()), Provenance::Api);
        t.logical_processors = Field<std::uint32_t>::known(logical, Provenance::Api);
        t.hybrid = Field<bool>::known(hybrid, Provenance::Derived);
        t.smt = Field<bool>::known(smt, Provenance::Derived);
        t.performance_cores = Field<std::uint32_t>::known(perf, Provenance::Derived);
        t.efficiency_cores = Field<std::uint32_t>::known(static_cast<std::uint32_t>(cores.size()) - perf, Provenance::Derived);
    }
    std::uint64_t l3 = 0;
    std::uint32_t slices = 0, max_slice = 0;
    for (const auto& c : caches) {
        if (c.level != 3 || !c.unified_or_data) continue;
        l3 += c.size_bytes;
        ++slices;
        max_slice = std::max(max_slice, c.size_bytes);
    }
    if (slices) {
        t.l3_total_kib = Field<std::uint32_t>::known(static_cast<std::uint32_t>(l3 / 1024), Provenance::Api);
        t.l3_slices = Field<std::uint32_t>::known(slices, Provenance::Api);
        t.l3_max_slice_kib = Field<std::uint32_t>::known(max_slice / 1024, Provenance::Api);
    } else {
        t.l3_total_kib = t.l3_slices = t.l3_max_slice_kib = Field<std::uint32_t>::unknown("no L3 cache records");
    }
    return t;
}

CpuSetCoverage classify_cpu_set_coverage(const std::vector<CpuSetRecord>& sets) {
    if (sets.empty()) return CpuSetCoverage::Unknown;
    std::uint8_t max_class = 0;
    bool any_allowed = false;
    for (const auto& s : sets) {
        max_class = std::max(max_class, s.efficiency_class);
        any_allowed = any_allowed || s.allowed;
    }
    if (!any_allowed) return CpuSetCoverage::Unknown;  // izinli kume bilinmiyor: iddia yok
    bool hybrid = false;
    for (const auto& s : sets) hybrid = hybrid || s.efficiency_class != max_class;
    if (!hybrid) return CpuSetCoverage::NotHybrid;
    bool perf = false, eff = false;
    for (const auto& s : sets) {
        if (!s.allowed) continue;
        if (s.efficiency_class == max_class)
            perf = true;
        else
            eff = true;
    }
    if (perf && eff) return CpuSetCoverage::PerformanceAndEfficiency;
    return perf ? CpuSetCoverage::PerformanceOnly : CpuSetCoverage::EfficiencyOnly;
}

}  // namespace ef::core::inventory
