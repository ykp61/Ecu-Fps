#include <cmath>
#include <limits>

#include "core/measure/frame_stats.h"
#include "ef_test.h"

using namespace ef::core::measure;

EF_TEST(frame_stats_definitions) {
    // 198 kare 10 ms + 2 kare 50 ms: N=200, %1 = ceil(2) en yavas -> 50 ms -> 20 FPS
    std::vector<double> v(198, 10.0);
    v.push_back(50.0);
    v.push_back(50.0);
    const auto s = compute_frame_stats(v);
    EF_CHECK(s.sufficient && s.n == 200 && s.excluded == 0);
    EF_CHECK(std::fabs(*s.avg_fps - 200.0 / 2.08) < 1e-9);  // 200 aralik / 2,08 s
    EF_CHECK(std::fabs(*s.low1_fps - 20.0) < 1e-9);
    EF_CHECK(*s.p95_ms == 10.0 && *s.p99_ms == 10.0);  // nearest-rank: 198. ve 199. degil, ceil(0.99*200)=198 -> 10
    EF_CHECK(kLowDefinition == "low1_v1");
    // F-69: 0,1% dilimi icin 200 kare yetersiz; deger uydurulmaz.
    EF_CHECK(!s.low01_fps && !s.p99_9_ms);
    EF_CHECK(s.median_ms == 10.0 && s.max_ms == 50.0);
    EF_CHECK(s.pattern == FramePattern::MicroStutter);  // 2/200 = %1 nadir uzun kare
}

EF_TEST(frame_stats_low01_p999_and_pattern) {
    // N=1000: 995 x 10 ms + 5 x 50 ms. %0,1 = 1 kare, p99,9 = 999. sira.
    std::vector<double> v(995, 10.0);
    v.insert(v.end(), 5, 50.0);
    const auto s = compute_frame_stats(v);
    EF_CHECK(s.sufficient && s.n == 1000);
    EF_CHECK(s.p99_9_ms.has_value() && *s.p99_9_ms == 50.0);
    EF_CHECK(s.low01_fps.has_value() && std::fabs(*s.low01_fps - 20.0) < 1e-9);
    EF_CHECK(std::fabs(*s.low1_fps - (1000.0 / 30.0)) < 1e-9);  // son 10: 5x50 + 5x10
    EF_CHECK(s.pattern == FramePattern::MicroStutter);

    const auto stable = compute_frame_stats(std::vector<double>(1000, 16.0));
    EF_CHECK(stable.pattern == FramePattern::Stable && *stable.p99_9_ms == 16.0);
    EF_CHECK(std::fabs(*stable.low01_fps - 62.5) < 1e-9);

    std::vector<double> spiky(900, 10.0);
    spiky.insert(spiky.end(), 100, 30.0);  // %10 esik asimi -> yaygin
    EF_CHECK(compute_frame_stats(spiky).pattern == FramePattern::Spiky);

    for (const auto p : {FramePattern::Insufficient, FramePattern::Stable, FramePattern::MicroStutter, FramePattern::Spiky})
        EF_CHECK(frame_pattern_from_string(to_string(p)) == p);
    EF_CHECK(frame_pattern_from_string("bilinmeyen") == FramePattern::Insufficient);
}

EF_TEST(frame_stats_exclusions_and_min_n) {
    std::vector<double> v(150, 16.0);
    v.push_back(std::numeric_limits<double>::quiet_NaN());
    v.push_back(-3.0);
    v.push_back(0.0);
    v.push_back(std::numeric_limits<double>::infinity());
    const auto s = compute_frame_stats(v);
    EF_CHECK(s.n == 150 && s.excluded == 4 && s.sufficient);
    const auto few = compute_frame_stats(std::vector<double>(50, 16.0));
    EF_CHECK(!few.sufficient && !few.avg_fps && !few.low1_fps && !few.p99_ms);  // asgari N altinda sonuc yok
}
