#include <cmath>
#include <cstdint>
#include <vector>

#include "core/measure/polling_rate.h"
#include "ef_test.h"

using namespace ef::core::measure;

namespace {
constexpr std::int64_t kFreq = 10'000'000;  // 10 MHz QPC

// Sentetik zaman damgalari: aralik = 1000/hz ms, +-jitter orani (deterministik desen).
std::vector<std::int64_t> synth(double hz, std::size_t n, double jitter = 0.0) {
    std::vector<std::int64_t> t{1'000'000};
    const double base = static_cast<double>(kFreq) / hz;
    for (std::size_t i = 0; i < n; ++i) {
        const double j = jitter * (static_cast<int>(i % 5) - 2) / 2.0;  // -1..+1 * jitter
        t.push_back(t.back() + static_cast<std::int64_t>(base * (1.0 + j)));
    }
    return t;
}
}  // namespace

EF_TEST(polling_1000hz) {
    const auto r = estimate_polling_rate(synth(1000, 600), kFreq);
    EF_CHECK(r.state == PollingResult::State::Ok && r.samples == 600);
    EF_CHECK(std::fabs(r.hz - 1000.0) < 1.0 && std::fabs(r.median_interval_ms - 1.0) < 0.01);
    EF_CHECK(!r.at_least);
}

EF_TEST(polling_125hz) {
    const auto r = estimate_polling_rate(synth(125, 400), kFreq);
    EF_CHECK(r.state == PollingResult::State::Ok && std::fabs(r.hz - 125.0) < 0.5 && !r.at_least);
}

EF_TEST(polling_8000hz_is_lower_bound) {
    const auto r = estimate_polling_rate(synth(8000, 2000), kFreq);
    EF_CHECK(r.state == PollingResult::State::Ok && std::fabs(r.hz - 8000.0) < 8.0);
    EF_CHECK(r.at_least);  // 2000 Hz ustu: alt sinir olarak sunulur
}

EF_TEST(polling_jitter_10_percent) {
    const auto r = estimate_polling_rate(synth(1000, 800, 0.10), kFreq);
    EF_CHECK(r.state == PollingResult::State::Ok);
    EF_CHECK(std::fabs(r.hz - 1000.0) < 100.0);  // medyan +-%10 icinde
    EF_CHECK(r.p95_interval_ms >= r.median_interval_ms);
}

EF_TEST(polling_long_gaps_filtered) {
    auto t = synth(1000, 500);
    // 3 uzun bosluk (50 ms) ekle: sonraki zaman damgalari kayar
    for (std::size_t at : {100u, 200u, 300u})
        for (std::size_t i = at; i < t.size(); ++i) t[i] += kFreq / 20;
    const auto r = estimate_polling_rate(t, kFreq);
    EF_CHECK(r.state == PollingResult::State::Ok && r.samples == 497);
    EF_CHECK(std::fabs(r.hz - 1000.0) < 1.0);
}

EF_TEST(polling_too_few_samples) {
    EF_CHECK(estimate_polling_rate(synth(1000, 299), kFreq).state == PollingResult::State::Insufficient);  // 299 aralik
    EF_CHECK(estimate_polling_rate(synth(1000, 300), kFreq).state == PollingResult::State::Ok);
    // Uzun bosluklar kalan sayiyi esigin altina indirirse yetersiz
    auto t = synth(1000, 301);
    for (std::size_t at : {150u, 200u})  // iki uzun bosluk: 301 - 2 = 299 gecerli aralik
        for (std::size_t i = at; i < t.size(); ++i) t[i] += kFreq / 10;
    EF_CHECK(estimate_polling_rate(t, kFreq).state == PollingResult::State::Insufficient);
    EF_CHECK(estimate_polling_rate({}, kFreq).state == PollingResult::State::Insufficient);
}

EF_TEST(polling_bad_frequency_is_insufficient) {
    const auto t = synth(1000, 600);
    EF_CHECK(estimate_polling_rate(t, 0).state == PollingResult::State::Insufficient);
    EF_CHECK(estimate_polling_rate(t, -5).state == PollingResult::State::Insufficient);
    EF_CHECK(estimate_polling_rate(t, 0).hz == 0.0);
}

EF_TEST(polling_shared_timestamps_mark_lower_bound) {
    // Her zaman damgasi iki kez (toplu kayit): gecerli araliklar var ama deger alt sinir
    std::vector<std::int64_t> t;
    for (const auto v : synth(500, 400)) {
        t.push_back(v);
        t.push_back(v);
    }
    const auto r = estimate_polling_rate(t, kFreq);
    EF_CHECK(r.state == PollingResult::State::Ok && r.at_least && std::fabs(r.hz - 500.0) < 1.0);
}
