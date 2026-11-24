#include "core/perf/report_collector.h"
#include "ef_test.h"

using namespace ef::core::perf;

namespace {
ResourceSample cpu(double v) {
    ResourceSample s;
    s.cpu_pct = v;
    return s;
}
}  // namespace

EF_TEST(report_collector_before_after_flow) {
    ReportCollector c(5);
    for (int i = 0; i < 8; ++i) c.push(cpu(i % 2 ? 41.0 : 39.0));  // once ~40
    c.mark_before();
    EF_CHECK(c.stage() == ReportCollector::Stage::Applying && c.before_count() == 5);
    c.push(cpu(99));  // uygulama sirasinda: hicbir tarafa girmez
    c.begin_after();
    for (int i = 0; i < 4; ++i) c.push(cpu(i % 2 ? 21.0 : 19.0));
    EF_CHECK(c.stage() == ReportCollector::Stage::CollectingAfter && c.after_count() == 4 && !c.report());
    c.push(cpu(20));
    EF_CHECK(c.stage() == ReportCollector::Stage::Done && c.report());
    const auto& m = c.report()->metrics.front();
    EF_CHECK(m.key == "cpu_pct" && m.verdict == Verdict::Significant && m.improved && *m.improved);
    EF_CHECK(m.n_before == 5 && m.n_after == 5);
}

EF_TEST(report_collector_cancel_and_no_before) {
    ReportCollector c(5);
    c.mark_before();  // hic ornek yok
    c.begin_after();
    for (int i = 0; i < 5; ++i) c.push(cpu(20));
    EF_CHECK(c.report() && c.report()->metrics.front().verdict == Verdict::InsufficientData);  // tahmin yok
    ReportCollector d(3);
    d.push(cpu(1));
    d.mark_before();
    d.cancel();
    EF_CHECK(d.stage() == ReportCollector::Stage::Idle && !d.report());
    d.begin_after();  // iptal sonrasi baslamaz
    EF_CHECK(d.stage() == ReportCollector::Stage::Idle);
}

EF_TEST(before_after_tiny_but_consistent_shift_is_not_meaningful) {
    // Gercek cihazda gorulen durum: bos RAM 14 MB tutarli dusus -> istatistiksel ama pratikte onemsiz.
    std::vector<ResourceSample> b, a;
    for (int i = 0; i < 10; ++i) {
        ResourceSample x, y;
        x.ram_free_bytes = (46ull << 30) + static_cast<std::uint64_t>(i % 2) * (1ull << 20);
        y.ram_free_bytes = (46ull << 30) - (14ull << 20) + static_cast<std::uint64_t>(i % 2) * (1ull << 20);
        x.cpu_pct = 40.0 + (i % 2) * 0.1, y.cpu_pct = 39.6 + (i % 2) * 0.1;  // 0,4 puan
        b.push_back(x), a.push_back(y);
    }
    const auto r = compare_before_after(b, a);
    for (const auto& m : r.metrics)
        if (m.key == "ram_free_bytes" || m.key == "cpu_pct") EF_CHECK(m.verdict == Verdict::NoSignificantDifference && !m.improved);
}
