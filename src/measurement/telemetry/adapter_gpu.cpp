#include "measurement/telemetry/adapter_gpu.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <vector>

#include "core/metrics/process_engine.h"

namespace ef::measurement {

namespace {
struct QueryCloser {
    PDH_HQUERY q = nullptr;
    ~QueryCloser() {
        if (q) PdhCloseQuery(q);
    }
};
}  // namespace

std::optional<double> sample_other_process_gpu_pct(std::string_view adapter_luid, std::uint32_t own_pid, double seconds,
                                                   const std::atomic<bool>& cancel, const std::vector<std::uint32_t>& exempt_pids) {
    if (adapter_luid.empty()) return std::nullopt;
    QueryCloser query;
    if (PdhOpenQueryW(nullptr, 0, &query.q) != ERROR_SUCCESS) return std::nullopt;
    PDH_HCOUNTER engines = nullptr;
    // Ingilizce sayac adi: Windows dilinden bagimsiz. Joker, ekleme anindaki ornekleri kapsar.
    if (PdhAddEnglishCounterW(query.q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &engines) != ERROR_SUCCESS) return std::nullopt;
    if (PdhCollectQueryData(query.q) != ERROR_SUCCESS) return std::nullopt;  // taban
    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(std::clamp(seconds, 0.2, 5.0) * 1000.0);
    while (GetTickCount64() < until) {
        if (cancel.load(std::memory_order_relaxed)) return std::nullopt;
        Sleep(20);
    }
    if (PdhCollectQueryData(query.q) != ERROR_SUCCESS) return std::nullopt;
    DWORD bytes = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(engines, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr) != PDH_MORE_DATA || bytes == 0)
        return std::nullopt;
    std::vector<unsigned char> buf(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(engines, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return std::nullopt;
    std::vector<core::metrics::EngineInstance> instances;
    instances.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        const auto& v = items[i].FmtValue;
        if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        core::metrics::EngineInstance e;  // ornek adlari ASCII'dir (pid / luid / eng alanlari)
        for (const wchar_t* p = items[i].szName; p && *p; ++p) e.name.push_back(*p < 0x80 ? static_cast<char>(*p) : '?');
        e.pct = v.doubleValue;
        instances.push_back(std::move(e));
    }
    return core::metrics::other_process_3d_pct(instances, adapter_luid, own_pid, exempt_pids);
}

}  // namespace ef::measurement
