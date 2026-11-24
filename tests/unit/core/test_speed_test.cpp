// Internet hiz testi saf mantigi (Task 11). Gercek ag cagrisi yok; gercek test yalniz istege bagli probe'dadir.
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

#include "core/net/speed_test.h"
#include "ef_test.h"

using namespace ef::core::net;

namespace {
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
}  // namespace

EF_TEST(speed_chunk_mbps) {
    const auto v = chunk_mbps({1'250'000, 1.0});
    EF_CHECK(v.has_value() && near(*v, 10.0));
    EF_CHECK(!chunk_mbps({1'250'000, 0.0}));
    EF_CHECK(!chunk_mbps({1'250'000, -1.0}));
    EF_CHECK(!chunk_mbps({0, 1.0}));
    EF_CHECK(!chunk_mbps({1'000, std::numeric_limits<double>::quiet_NaN()}));
}

EF_TEST(speed_p90) {
    std::vector<ChunkSample> c;
    for (int i = 1; i <= 10; ++i) c.push_back({static_cast<std::uint64_t>(i) * 125'000, 1.0});  // i Mbps
    const auto p = percentile90_mbps(c);
    EF_CHECK(p.has_value() && near(*p, 9.0));
    EF_CHECK(!percentile90_mbps(std::span<const ChunkSample>{}));
    // Gecersiz parcalar sayilmaz; yalniz gecersiz varsa sonuc yok (0 uydurulmaz).
    const std::array<ChunkSample, 2> bad{ChunkSample{0, 1.0}, ChunkSample{100, 0.0}};
    EF_CHECK(!percentile90_mbps(bad));
}

EF_TEST(speed_median) {
    const std::array<double, 3> odd{5.0, 1.0, 3.0};
    EF_CHECK(median_ms(odd).value() == 3.0);
    const std::array<double, 4> even{4.0, 1.0, 3.0, 2.0};
    EF_CHECK(near(median_ms(even).value(), 2.5));
    EF_CHECK(!median_ms(std::span<const double>{}));
}

EF_TEST(speed_plan_escalates) {
    const SpeedPlan plan;
    const std::array<std::uint64_t, 5> want{100'000, 1'000'000, 10'000'000, 25'000'000, 25'000'000};
    for (std::size_t i = 0; i < want.size(); ++i) {
        EF_CHECK(plan.next_chunk(SpeedPhase::Download, i, 1.0, 0, 1.0) == want[i]);
        EF_CHECK(plan.next_chunk(SpeedPhase::Upload, i, 1.0, 0, 1.0) == want[i]);
    }
    EF_CHECK(plan.next_chunk(SpeedPhase::Latency, 0, 0.0, 0, 0.0) == 0);
    EF_CHECK(plan.next_chunk(SpeedPhase::Done, 0, 0.0, 0, 0.0) == 0);
}

EF_TEST(speed_plan_stops_on_time) {
    const SpeedPlan plan;
    EF_CHECK(plan.next_chunk(SpeedPhase::Download, 2, kPhaseSeconds, 0, 10.0) == 0);
    EF_CHECK(plan.next_chunk(SpeedPhase::Download, 2, 9.99, 0, 10.0) == 10'000'000);
}

EF_TEST(speed_plan_stops_on_budget) {
    const SpeedPlan plan;
    EF_CHECK(plan.next_chunk(SpeedPhase::Download, 0, 1.0, kSpeedMaxBytes - 50'000, 1.0) == 0);
    EF_CHECK(plan.next_chunk(SpeedPhase::Upload, 0, 1.0, kSpeedMaxBytes, 1.0) == 0);
    EF_CHECK(plan.next_chunk(SpeedPhase::Download, 0, 1.0, kSpeedMaxBytes - 100'000, 1.0) == 100'000);
}

EF_TEST(speed_plan_stops_on_total_time) {
    const SpeedPlan plan;
    EF_CHECK(plan.next_chunk(SpeedPhase::Upload, 0, 0.0, 0, kSpeedMaxSeconds) == 0);
    EF_CHECK(plan.next_chunk(SpeedPhase::Upload, 0, 0.0, 0, 61.0) == 0);
}

EF_TEST(gauge_fraction_points) {
    EF_CHECK(gauge_fraction(0) == 0.0);
    EF_CHECK(near(gauge_fraction(5), 0.125));
    EF_CHECK(near(gauge_fraction(100), 0.5));
    EF_CHECK(gauge_fraction(1000) == 1.0);
    EF_CHECK(gauge_fraction(5000) == 1.0);
    const double f75 = gauge_fraction(75);
    EF_CHECK(f75 > 0.375 && f75 < 0.5);
    EF_CHECK(gauge_fraction(-3) == 0.0);
    EF_CHECK(gauge_fraction(std::numeric_limits<double>::quiet_NaN()) == 0.0);
}

EF_TEST(speed_mbs) { EF_CHECK(near(mbps_to_mbs(100.0), 12.5)); }

EF_TEST(speed_host_constant) {
    static_assert(kSpeedHost == "speed.cloudflare.com");
    static_assert(kDownPath == "/__down");
    static_assert(kUpPath == "/__up");
    static_assert(kSpeedMaxBytes == 268'435'456ull);
    const std::string_view host = kSpeedHost;  // calisma zamani kontrolu da kayitta gorunsun
    EF_CHECK(host == "speed.cloudflare.com");
}

EF_TEST(speed_cancelled_result_has_no_numbers) {
    SpeedResult r;
    r.phase = SpeedPhase::Cancelled;
    r.down_mbps = 123.0;
    r.latency_ms = 9.0;
    r.bytes_used = 5'000'000;
    const auto s = sanitize_result(r);
    EF_CHECK(!s.down_mbps && !s.up_mbps && !s.latency_ms);
    EF_CHECK(s.bytes_used == 5'000'000);
    r.phase = SpeedPhase::Failed;
    r.fail = SpeedFail::Gated;
    EF_CHECK(!sanitize_result(r).down_mbps);
    r.phase = SpeedPhase::Done;
    r.fail = SpeedFail::None;
    EF_CHECK(sanitize_result(r).down_mbps.value() == 123.0);
}

EF_TEST(speed_fail_keys) {
    EF_CHECK(speed_fail_key(SpeedFail::Offline) == "speed.fail.offline");
    EF_CHECK(speed_fail_key(SpeedFail::Timeout) == "speed.fail.timeout");
    EF_CHECK(speed_fail_key(SpeedFail::Tls) == "speed.fail.tls");
    EF_CHECK(speed_fail_key(SpeedFail::Http) == "speed.fail.http");
    EF_CHECK(speed_fail_key(SpeedFail::Budget) == "speed.fail.budget");
    EF_CHECK(speed_fail_key(SpeedFail::Gated) == "speed.fail.gated");
    EF_CHECK(speed_fail_key(SpeedFail::None).empty());
}

// ---- R-C1: yuk altinda gecikme (bufferbloat) ----
EF_TEST(rc1_loaded_median_needs_min_samples_and_ignores_bad_values) {
    const std::vector<double> few{10, 12, 11, 13};  // 4 < kLoadedMinSamples
    EF_CHECK(!loaded_median_ms(few));
    const std::vector<double> ok{10, 50, 30, 20, 40};
    EF_CHECK(loaded_median_ms(ok).value() == 30.0);
    const std::vector<double> nan_mix{10, std::numeric_limits<double>::quiet_NaN(), 20, -5, 30, 40, 50};  // NaN ve negatif atilir
    EF_CHECK(loaded_median_ms(nan_mix).value() == 30.0);
    const std::vector<double> only_bad{std::numeric_limits<double>::infinity(), -1, std::numeric_limits<double>::quiet_NaN()};
    EF_CHECK(!loaded_median_ms(only_bad));
}

EF_TEST(rc1_bloat_grade_uses_worst_direction_increase) {
    EF_CHECK(bloat_grade(20.0, 35.0, 40.0) == BloatGrade::Low);      // artis 15 / 20
    EF_CHECK(bloat_grade(20.0, 49.9, std::nullopt) == BloatGrade::Low);  // artis 29,9
    EF_CHECK(bloat_grade(20.0, 50.0, std::nullopt) == BloatGrade::Medium);  // artis tam 30
    EF_CHECK(bloat_grade(20.0, 30.0, 119.9) == BloatGrade::Medium);  // en kotu yon yukleme: 99,9
    EF_CHECK(bloat_grade(20.0, 120.0, 30.0) == BloatGrade::High);    // artis tam 100
    EF_CHECK(bloat_grade(20.0, 10.0, 12.0) == BloatGrade::Low);      // yuklu < bos: artis 0 sayilir
}

EF_TEST(rc1_bloat_grade_insufficient_without_idle_or_loaded) {
    EF_CHECK(bloat_grade(std::nullopt, 50.0, 60.0) == BloatGrade::Insufficient);
    EF_CHECK(bloat_grade(20.0, std::nullopt, std::nullopt) == BloatGrade::Insufficient);
    EF_CHECK(bloat_grade(std::numeric_limits<double>::quiet_NaN(), 50.0, 60.0) == BloatGrade::Insufficient);
    EF_CHECK(bloat_key(BloatGrade::Low) == "speed.bloat.low" && bloat_key(BloatGrade::Medium) == "speed.bloat.medium");
    EF_CHECK(bloat_key(BloatGrade::High) == "speed.bloat.high" && bloat_key(BloatGrade::Insufficient) == "speed.bloat.none");
}

EF_TEST(rc1_sanitize_drops_loaded_latency_unless_done) {
    SpeedResult r;
    r.phase = SpeedPhase::Cancelled;
    r.down_loaded_ms = 80.0;
    r.up_loaded_ms = 120.0;
    const auto s = sanitize_result(r);
    EF_CHECK(!s.down_loaded_ms && !s.up_loaded_ms);
    r.phase = SpeedPhase::Done;
    const auto d = sanitize_result(r);
    EF_CHECK(d.down_loaded_ms.value() == 80.0 && d.up_loaded_ms.value() == 120.0);
}
