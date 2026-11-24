#include "platform/windows/metrics/cpu_speed_sampler.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <cmath>

namespace ef::platform::metrics {

namespace {
std::optional<double> read_double(void* counter) noexcept {
    if (!counter) return std::nullopt;
    PDH_FMT_COUNTERVALUE v{};
    if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) != ERROR_SUCCESS)
        return std::nullopt;
    if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) return std::nullopt;
    if (!std::isfinite(v.doubleValue) || v.doubleValue < 0.0) return std::nullopt;
    return v.doubleValue;
}
}  // namespace

CpuSpeedSampler::~CpuSpeedSampler() { close(); }

bool CpuSpeedSampler::open() {
    if (query_) return true;
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return false;
    PDH_HCOUNTER perf = nullptr, limit = nullptr;
    if (PdhAddEnglishCounterW(q, L"\\Processor Information(_Total)\\% Processor Performance", 0, &perf) != ERROR_SUCCESS) perf = nullptr;
    if (PdhAddEnglishCounterW(q, L"\\Processor Information(_Total)\\% Performance Limit", 0, &limit) != ERROR_SUCCESS) limit = nullptr;
    if (!perf && !limit) {
        PdhCloseQuery(q);
        return false;
    }
    query_ = q;
    perf_ = perf;
    limit_ = limit;
    PdhCollectQueryData(q);  // oran sayaci icin taban
    return true;
}

CpuSpeedReading CpuSpeedSampler::sample() {
    CpuSpeedReading r;
    if (!query_) return r;
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(query_)) != ERROR_SUCCESS) return r;
    r.performance_pct = read_double(perf_);
    r.limit_pct = read_double(limit_);
    return r;
}

void CpuSpeedSampler::close() {
    if (query_) PdhCloseQuery(static_cast<PDH_HQUERY>(query_));
    query_ = perf_ = limit_ = nullptr;
}

}  // namespace ef::platform::metrics
