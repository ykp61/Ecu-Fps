// R15: benzer sistemlerle karsilastirma — yalniz gercek ornekler; yetersiz veri acik.
#include <cmath>

#include "core/insights/peer.h"
#include "ef_test.h"

using namespace ef::core::insights;

namespace {

PeerQuery peer_query() {
    PeerQuery q;
    q.fingerprint = "scene_v1|high|17a5";
    q.source = "builtin";
    q.conditions = {{"resolution", "1920x1080"}};
    q.metric = "avg_fps";
    q.value = 10.0;
    return q;
}

PeerSample peer_sample(double value, std::string fingerprint = "scene_v1|high|17a5") {
    PeerSample s;
    s.fingerprint = std::move(fingerprint);
    s.source = "builtin";
    s.conditions = {{"resolution", "1920x1080"}};
    s.metrics["avg_fps"] = value;
    return s;
}

}  // namespace

EF_TEST(peer_empty_pool_is_insufficient_with_zero_count) {
    const auto r = compare_peers(peer_query(), {});
    EF_CHECK(r.outcome == PeerOutcome::Insufficient);
    EF_CHECK(r.reason == PeerInsufficientReason::NoPool);  // veri havuzu kurulmadi
    EF_CHECK(r.matching_samples == 0);
    EF_CHECK(!r.percentile.has_value());  // yuzdelik uydurulmaz
    EF_CHECK(r.required_samples == kMinPeerSamples && kMinPeerSamples == 20);
}

EF_TEST(peer_below_minimum_is_insufficient_with_real_count) {
    std::vector<PeerSample> pool;
    for (int i = 1; i <= 19; ++i) pool.push_back(peer_sample(static_cast<double>(i)));
    const auto r = compare_peers(peer_query(), pool);
    EF_CHECK(r.outcome == PeerOutcome::Insufficient);
    EF_CHECK(r.reason == PeerInsufficientReason::BelowMinimum);
    EF_CHECK(r.matching_samples == 19);  // gercek sayi; esik tamamlanmis gibi gosterilmez
    EF_CHECK(!r.percentile.has_value());
}

EF_TEST(peer_minimum_samples_yield_deterministic_percentile) {
    std::vector<PeerSample> pool;
    for (int i = 1; i <= 20; ++i) pool.push_back(peer_sample(static_cast<double>(i)));
    auto r = compare_peers(peer_query(), pool);  // 9 altinda, 1 esit -> (9 + 0.5) / 20
    EF_CHECK(r.outcome == PeerOutcome::Comparable);
    EF_CHECK(r.matching_samples == 20);
    EF_CHECK(r.percentile && std::fabs(*r.percentile - 47.5) < 1e-9);

    auto low = peer_query();
    low.value = 0.0;  // tum gercek orneklerin altinda
    r = compare_peers(low, pool);
    EF_CHECK(r.percentile && std::fabs(*r.percentile) < 1e-9);

    auto high = peer_query();
    high.value = 21.0;  // tum gercek orneklerin ustunde
    r = compare_peers(high, pool);
    EF_CHECK(r.percentile && std::fabs(*r.percentile - 100.0) < 1e-9);
}

EF_TEST(peer_mismatched_fingerprint_source_or_conditions_excluded) {
    std::vector<PeerSample> pool;
    for (int i = 1; i <= 25; ++i) pool.push_back(peer_sample(static_cast<double>(i)));
    for (int i = 1; i <= 30; ++i) pool.push_back(peer_sample(static_cast<double>(i), "other_fingerprint"));
    for (int i = 1; i <= 10; ++i) {
        auto s = peer_sample(static_cast<double>(i));
        s.conditions = {{"resolution", "2560x1440"}};
        pool.push_back(std::move(s));
    }
    for (int i = 1; i <= 10; ++i) {
        auto s = peer_sample(static_cast<double>(i));
        s.source = "in_game";
        pool.push_back(std::move(s));
    }
    auto q = peer_query();
    q.value = 20.0;
    auto r = compare_peers(q, pool);
    EF_CHECK(r.matching_samples == 25);  // yalniz birebir eslesenler
    EF_CHECK(r.percentile && std::fabs(*r.percentile - 78.0) < 1e-9);  // (19 + 0.5) / 25

    std::vector<PeerSample> short_pool;
    for (int i = 1; i <= 19; ++i) short_pool.push_back(peer_sample(static_cast<double>(i)));
    for (int i = 1; i <= 30; ++i) short_pool.push_back(peer_sample(static_cast<double>(i), "other_fingerprint"));
    r = compare_peers(q, short_pool);
    EF_CHECK(r.outcome == PeerOutcome::Insufficient && r.reason == PeerInsufficientReason::BelowMinimum);
    EF_CHECK(r.matching_samples == 19);
}

EF_TEST(peer_metricless_or_invalid_query_is_insufficient) {
    PeerSample s = peer_sample(1.0);
    s.metrics.clear();  // eslesen ama metrigi olcmemis ornek yuzdelige girmez
    auto r = compare_peers(peer_query(), {s});
    EF_CHECK(r.outcome == PeerOutcome::Insufficient && r.reason == PeerInsufficientReason::NoMatchingSamples);
    EF_CHECK(r.matching_samples == 0);

    std::vector<PeerSample> pool;
    for (int i = 1; i <= 20; ++i) pool.push_back(peer_sample(static_cast<double>(i)));
    auto bad = peer_query();
    bad.value = std::nan("");
    r = compare_peers(bad, pool);
    EF_CHECK(r.outcome == PeerOutcome::Insufficient && r.reason == PeerInsufficientReason::InvalidQuery);
    EF_CHECK(!r.percentile.has_value());
}
