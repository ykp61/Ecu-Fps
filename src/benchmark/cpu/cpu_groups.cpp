#include "benchmark/cpu/cpu_groups.h"

#include <windows.h>

#include <memory>
#include <thread>
#include <vector>

namespace ef::benchmark {

namespace {

struct GroupInfo {
    std::uint32_t size = 0;
    KAFFINITY mask = 0;
};

std::vector<GroupInfo> active_groups() {
    std::vector<GroupInfo> out;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationGroup, nullptr, &len);
    if (len == 0) return out;
    auto buf = std::make_unique<unsigned char[]>(len);
    auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.get());
    if (!GetLogicalProcessorInformationEx(RelationGroup, info, &len) || info->Relationship != RelationGroup) return out;
    const WORD n = info->Group.ActiveGroupCount;
    for (WORD g = 0; g < n; ++g) {
        const auto& gi = info->Group.GroupInfo[g];
        out.push_back({gi.ActiveProcessorCount, gi.ActiveProcessorMask});
    }
    return out;
}

}  // namespace

std::uint32_t logical_processor_count() noexcept {
    const DWORD all = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (all > 0) return static_cast<std::uint32_t>(all);
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1u : hw;
}

std::uint16_t group_for_worker(std::uint32_t worker, std::uint32_t workers, std::span<const std::uint32_t> group_sizes) noexcept {
    std::uint64_t total = 0;
    for (const auto s : group_sizes) total += s;
    if (group_sizes.empty() || total == 0 || workers == 0) return 0;
    const std::uint64_t pos = static_cast<std::uint64_t>(worker % workers) * total / workers;
    std::uint64_t acc = 0;
    for (std::size_t g = 0; g < group_sizes.size(); ++g) {
        acc += group_sizes[g];
        if (pos < acc) return static_cast<std::uint16_t>(g);
    }
    return static_cast<std::uint16_t>(group_sizes.size() - 1);
}

void spread_current_thread(std::uint32_t worker, std::uint32_t workers) noexcept {
    try {
        if (GetActiveProcessorGroupCount() <= 1) return;
        const auto groups = active_groups();
        if (groups.size() <= 1) return;
        std::vector<std::uint32_t> sizes;
        sizes.reserve(groups.size());
        for (const auto& g : groups) sizes.push_back(g.size);
        const std::uint16_t g = group_for_worker(worker, workers, sizes);
        if (groups[g].mask == 0) return;
        GROUP_AFFINITY ga{};
        ga.Mask = groups[g].mask;
        ga.Group = g;
        (void)SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr);
    } catch (...) {
        // bad_alloc: is parcacigi kendi grubunda kalir
    }
}

}  // namespace ef::benchmark
