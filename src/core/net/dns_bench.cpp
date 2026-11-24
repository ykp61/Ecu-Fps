#include "core/net/dns_bench.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ef::core::net {

namespace {
double nearest_rank(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0.0;
    const auto n = static_cast<double>(sorted.size());
    auto rank = static_cast<std::size_t>(std::ceil(q * n));
    if (rank < 1) rank = 1;
    return sorted[std::min(rank, sorted.size()) - 1];
}
}  // namespace

std::vector<ResolverStat> summarize(const std::vector<ResolverSamples>& samples) {
    std::vector<ResolverStat> out;
    out.reserve(samples.size());
    for (const auto& s : samples) {
        ResolverStat st;
        st.id = s.id;
        st.total = static_cast<int>(s.ms.size());
        std::vector<double> got;
        got.reserve(s.ms.size());
        for (const auto& v : s.ms)
            if (v && std::isfinite(*v) && *v >= 0.0) got.push_back(*v);
        st.ok_count = static_cast<int>(got.size());
        st.fail_pct = st.total > 0 ? 100.0 * (st.total - st.ok_count) / st.total : 0.0;
        st.ok = st.ok_count >= kDnsBenchMinSamples;
        if (!got.empty()) {
            std::sort(got.begin(), got.end());
            const std::size_t n = got.size();
            st.median_ms = n % 2 ? got[n / 2] : (got[n / 2 - 1] + got[n / 2]) / 2.0;
            st.p95_ms = nearest_rank(got, 0.95);
        }
        out.push_back(std::move(st));
    }
    return out;
}

DnsBenchVerdict verdict(const std::vector<ResolverStat>& stats, std::string_view current_id) {
    DnsBenchVerdict v;
    const ResolverStat* best = nullptr;
    const ResolverStat* cur = nullptr;
    for (const auto& s : stats) {
        if (s.id == current_id) cur = &s;
        if (s.ok && (!best || s.median_ms < best->median_ms)) best = &s;  // esitlikte ilk (mevcut siradadir)
    }
    if (!best) return v;
    v.best_id = best->id;
    if (!cur || !cur->ok || best->id == cur->id) return v;  // taban yok / zaten en iyi: degisiklik onerilmez
    const double gain = cur->median_ms - best->median_ms;
    v.meaningful = best->fail_pct == 0.0 && gain >= kDnsBenchMinGainMs && gain >= cur->median_ms * (kDnsBenchMinGainPct / 100.0);
    return v;
}

bool is_private_ipv4(std::string_view ip) noexcept {
    int part[4] = {0, 0, 0, 0};
    int idx = 0, digits = 0;
    for (const char ch : ip) {
        if (ch == '.') {
            if (digits == 0 || ++idx > 3) return false;
            digits = 0;
        } else if (ch >= '0' && ch <= '9') {
            part[idx] = part[idx] * 10 + (ch - '0');
            if (++digits > 3 || part[idx] > 255) return false;
        } else {
            return false;
        }
    }
    if (idx != 3 || digits == 0) return false;
    return part[0] == 10 || (part[0] == 172 && part[1] >= 16 && part[1] <= 31) || (part[0] == 192 && part[1] == 168) ||
           (part[0] == 169 && part[1] == 254);
}

}  // namespace ef::core::net
