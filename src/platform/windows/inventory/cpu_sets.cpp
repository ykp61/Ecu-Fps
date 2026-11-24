#include "platform/windows/inventory/cpu_sets.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace ef::platform::inventory {

namespace {

struct SetEntry {
    ULONG id = 0;
    WORD group = 0;
    BYTE logical = 0;
    BYTE efficiency_class = 0;
};

// GetSystemCpuSetInformation ikinci parametresi bayt; her kayit degisken uzunlukta.
std::vector<SetEntry> read_system_sets() {
    ULONG bytes = 0;
    SetLastError(ERROR_SUCCESS);
    GetSystemCpuSetInformation(nullptr, 0, &bytes, GetCurrentProcess(), 0);
    if (bytes == 0 || bytes > (1u << 20)) return {};
    std::vector<unsigned char> buf(bytes);
    if (!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), bytes, &bytes,
                                    GetCurrentProcess(), 0))
        return {};
    std::vector<SetEntry> out;
    for (ULONG off = 0; off < bytes;) {
        const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buf.data() + off);
        if (info->Size == 0) break;
        if (info->Type == CpuSetInformation) {
            out.push_back({info->CpuSet.Id, info->CpuSet.Group, info->CpuSet.LogicalProcessorIndex,
                           info->CpuSet.EfficiencyClass});
        }
        off += info->Size;
    }
    return out;
}

// Secili/varsayilan kume kimlikleri. Bos liste "atama yok" demektir; okuma hatasi false.
bool read_selected_ids(HANDLE h, bool thread, std::vector<ULONG>& out) {
    ULONG count = 0;
    auto call = [&](PULONG ids, ULONG cap, PULONG req) {
        return thread ? GetThreadSelectedCpuSets(h, ids, cap, req) : GetProcessDefaultCpuSets(h, ids, cap, req);
    };
    SetLastError(ERROR_SUCCESS);
    if (!call(nullptr, 0, &count) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    if (count == 0) return true;
    if (count > 4096) return false;
    std::vector<ULONG> ids(count);
    if (!call(ids.data(), count, &count)) return false;
    ids.resize(std::min<std::size_t>(ids.size(), count));
    out = std::move(ids);
    return true;
}

bool contains(const std::vector<ULONG>& ids, ULONG id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

core::inventory::CpuSetCoverage read_game_cpu_coverage(std::uint32_t pid, std::uint32_t tid) {
    const auto sets = read_system_sets();
    if (sets.empty()) return core::inventory::CpuSetCoverage::Unknown;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return core::inventory::CpuSetCoverage::Unknown;

    // Thread secili kumesi varsa onceliklidir; yoksa surec varsayilan kumeleri gecerlidir.
    std::vector<ULONG> thread_ids;
    if (tid != 0) {
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(tid));
        if (!thread) {
            CloseHandle(process);
            return core::inventory::CpuSetCoverage::Unknown;  // ana thread secildi ama acilamadi
        }
        const bool ok = read_selected_ids(thread, true, thread_ids);
        CloseHandle(thread);
        if (!ok) {
            CloseHandle(process);
            return core::inventory::CpuSetCoverage::Unknown;
        }
    }
    std::vector<ULONG> process_ids;
    if (!read_selected_ids(process, false, process_ids)) {
        CloseHandle(process);
        return core::inventory::CpuSetCoverage::Unknown;
    }
    DWORD_PTR mask = 0, system_mask = 0;
    const bool have_mask = GetProcessAffinityMask(process, &mask, &system_mask) != FALSE;
    CloseHandle(process);

    const bool single_group = GetActiveProcessorGroupCount() == 1;
    const bool use_thread_ids = !thread_ids.empty();
    const bool use_process_ids = !use_thread_ids && !process_ids.empty();
    if (!use_thread_ids && !use_process_ids && !have_mask) return core::inventory::CpuSetCoverage::Unknown;
    // Tek grup disinda benzesme maskesini logical index'e guvenle esleyemeyiz.
    if (!use_thread_ids && !use_process_ids && !single_group) return core::inventory::CpuSetCoverage::Unknown;

    std::vector<core::inventory::CpuSetRecord> records;
    records.reserve(sets.size());
    for (const auto& s : sets) {
        bool allowed = true;
        if (use_thread_ids) allowed = contains(thread_ids, s.id);
        else if (use_process_ids) allowed = contains(process_ids, s.id);
        if (allowed && have_mask && single_group && s.group == 0) {
            if (s.logical < 64) allowed = ((mask >> s.logical) & 1) != 0;
            else allowed = false;
        }
        records.push_back({s.efficiency_class, allowed});
    }
    return core::inventory::classify_cpu_set_coverage(records);
}

}  // namespace ef::platform::inventory
