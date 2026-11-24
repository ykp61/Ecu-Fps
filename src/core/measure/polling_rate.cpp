#include "core/measure/polling_rate.h"

#include <algorithm>
#include <cmath>

namespace ef::core::measure {

namespace {
double median_of(const std::vector<double>& sorted) {
    const std::size_t n = sorted.size();
    return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}
}  // namespace

PollingResult estimate_polling_rate(const std::vector<std::int64_t>& qpc_ticks, std::int64_t qpc_freq) {
    PollingResult r;
    if (qpc_freq <= 0 || qpc_ticks.size() < 2) return r;
    std::vector<double> ms;
    ms.reserve(qpc_ticks.size());
    bool shared_stamp = false;
    for (std::size_t i = 1; i < qpc_ticks.size(); ++i) {
        const std::int64_t d = qpc_ticks[i] - qpc_ticks[i - 1];
        if (d == 0) {
            shared_stamp = true;  // ayni zaman damgali toplu kayit: aralik yok
            continue;
        }
        if (d < 0) continue;  // saat geri gitti: gecersiz
        const double v = static_cast<double>(d) * 1000.0 / static_cast<double>(qpc_freq);
        if (!std::isfinite(v) || v > kMaxPollingIntervalMs) continue;  // fare durdu / sicrama
        ms.push_back(v);
    }
    if (ms.size() < kMinPollingIntervals) return r;
    std::sort(ms.begin(), ms.end());
    const double med = median_of(ms);
    if (!(med > 0.0)) return r;
    const std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(ms.size())));
    r.state = PollingResult::State::Ok;
    r.median_interval_ms = med;
    r.p95_interval_ms = ms[std::min(rank, ms.size()) - 1];
    r.hz = 1000.0 / med;
    r.samples = ms.size();
    r.at_least = shared_stamp || r.hz > kPollingAtLeastAboveHz;
    return r;
}

}  // namespace ef::core::measure
