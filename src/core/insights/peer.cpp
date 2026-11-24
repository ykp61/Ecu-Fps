#include "core/insights/peer.h"

#include <cmath>

namespace ef::core::insights {

std::string_view to_string(PeerOutcome o) noexcept {
    switch (o) {
        case PeerOutcome::Insufficient: return "insufficient";
        case PeerOutcome::Comparable: return "comparable";
    }
    return "insufficient";
}

std::string_view to_string(PeerInsufficientReason r) noexcept {
    switch (r) {
        case PeerInsufficientReason::NoPool: return "no_pool";
        case PeerInsufficientReason::NoMatchingSamples: return "no_matching_samples";
        case PeerInsufficientReason::BelowMinimum: return "below_minimum";
        case PeerInsufficientReason::InvalidQuery: return "invalid_query";
    }
    return "no_pool";
}

PeerComparison compare_peers(const PeerQuery& q, const std::vector<PeerSample>& pool) {
    PeerComparison out;
    out.required_samples = kMinPeerSamples;
    if (pool.empty()) {
        out.reason = PeerInsufficientReason::NoPool;  // veri havuzu kurulmadi; N=0
        return out;
    }
    if (q.metric.empty() || !std::isfinite(q.value)) {
        out.reason = PeerInsufficientReason::InvalidQuery;
        return out;
    }
    // Eslesme: parmak izi + kaynak + olcum kosullari birebir ayni. Bos parmak izi/kosullu ornek eslesmez.
    std::vector<double> values;
    for (const auto& s : pool) {
        if (q.fingerprint.empty() || s.fingerprint != q.fingerprint) continue;
        if (q.source.empty() || s.source != q.source) continue;
        if (s.conditions != q.conditions) continue;
        const auto it = s.metrics.find(q.metric);
        if (it == s.metrics.end() || !std::isfinite(it->second)) continue;
        values.push_back(it->second);
    }
    out.matching_samples = values.size();
    if (values.empty()) {
        out.reason = PeerInsufficientReason::NoMatchingSamples;
        return out;
    }
    if (values.size() < kMinPeerSamples) {
        out.reason = PeerInsufficientReason::BelowMinimum;
        return out;
    }
    // Midrank yuzdelik: gercek degerlerin yarisi esit sayilir. Uydurma/enterpolasyon yok.
    std::size_t below = 0, equal = 0;
    for (const double v : values) {
        if (v < q.value) ++below;
        else if (v == q.value) ++equal;
    }
    out.outcome = PeerOutcome::Comparable;
    out.percentile = 100.0 * (static_cast<double>(below) + 0.5 * static_cast<double>(equal)) / static_cast<double>(values.size());
    return out;
}

}  // namespace ef::core::insights
