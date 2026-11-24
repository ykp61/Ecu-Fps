// DIAG-01: kanit zaman cizgisi (timeline).
#include "core/diagnosis/evidence.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::measure;

namespace {
std::vector<FramePoint> frames(double until_ms, double step) {
    std::vector<FramePoint> v;
    for (double t = 0; t <= until_ms; t += step) v.push_back({t, step});
    return v;
}
std::vector<TelemetrySample> cpu_1hz(double until) {
    std::vector<TelemetrySample> v;
    for (double t = 1000; t <= until; t += 1000) v.push_back({"cpu_pct", MetricSource::Pdh, t, 1000, 55.0, {}});
    return v;
}
}  // namespace

EF_TEST(timeline_unaligned_source_blocks_definitive_diagnosis) {
    SourceInfo pdh{"pdh", ClockDomain::Qpc, 0, false};
    SourceInfo other{"vendor", ClockDomain::SteadyClock, 0, false};  // farkli saat, ofset dogrulanmadi
    auto tl = build_timeline("run-1", ClockDomain::Qpc, frames(10000, 10), {{pdh, cpu_1hz(10000)}}, {"cpu_pct"}, {}, 2000);
    EF_CHECK(tl.windows.size() == 5 && tl.usable().size() == 5);
    EF_CHECK(tl.windows[0].run_id == "run-1" && !tl.windows[0].metrics.empty());  // hangi kosu/pencere acik
    EF_CHECK(tl.windows[0].metrics[0].attribution == Attribution::RootCandidate);  // 2 s pencere, 1 Hz: yeterli
    tl = build_timeline("run-1", ClockDomain::Qpc, frames(10000, 10), {{pdh, cpu_1hz(10000)}, {other, cpu_1hz(10000)}}, {"cpu_pct"}, {}, 2000);
    EF_CHECK(tl.usable().empty() && tl.windows[0].unaligned_sources[0] == "vendor");  // kesinlesmez
    other.offset_verified = true;
    tl = build_timeline("run-1", ClockDomain::Qpc, frames(10000, 10), {{other, cpu_1hz(10000)}}, {"cpu_pct"}, {}, 2000);
    EF_CHECK(tl.usable().size() == 5);
}

EF_TEST(timeline_scope_filter_and_missing) {
    SourceInfo pdh{"pdh", ClockDomain::Qpc, 0, false};
    std::vector<TelemetrySample> gpu{{"gpu_pct", MetricSource::Pdh, 3000, 1000, std::nullopt, "counter_unavailable"}};
    const auto tl = build_timeline("run-2", ClockDomain::Qpc, frames(8000, 10), {{pdh, gpu}}, {"gpu_pct"},
                                   {{0, 2000, WindowScope::Warmup}, {6000, 8000, WindowScope::MenuOrLoading}}, 2000);
    EF_CHECK(tl.windows.size() == 4);
    EF_CHECK(!tl.windows[0].valid && tl.windows[1].valid && tl.windows[2].valid && !tl.windows[3].valid);
    EF_CHECK(tl.windows[1].metrics.empty());  // eksik deger baglam olarak eklenmez (0 degil)
    const auto none = build_timeline("run-3", ClockDomain::Qpc, {}, {}, {}, {}, 2000);
    EF_CHECK(none.windows.empty() && none.usable().empty());
}
