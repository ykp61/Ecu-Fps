#include <cmath>
#include <limits>

#include "core/perf/before_after.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace m = ef::core::metrics;
using ef::contracts::Measurement;
using ef::contracts::MissingReason;
using ef::contracts::Unit;

namespace {
const MetricComparison* find(const BeforeAfterReport& r, const std::string& key) {
    for (const auto& c : r.metrics)
        if (c.key == key) return &c;
    return nullptr;
}
std::vector<ResourceSample> cpu_series(const std::vector<double>& v) {
    std::vector<ResourceSample> out;
    for (double x : v) {
        ResourceSample s;
        s.cpu_pct = x;
        out.push_back(s);
    }
    return out;
}
}  // namespace

EF_TEST(before_after_t_table) {
    EF_CHECK(std::fabs(t_crit_95(1) - 12.706) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(9) - 2.262) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(9.8) - 2.262) < 1e-9);  // temkinli: alt df
    EF_CHECK(std::fabs(t_crit_95(18) - 2.101) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(35) - 2.042) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(1000) - 1.980) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(std::numeric_limits<double>::infinity()) - 1.960) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(0.2) - 12.706) < 1e-9);
    EF_CHECK(std::fabs(t_crit_95(std::nan("")) - 12.706) < 1e-9);
}

EF_TEST(before_after_noise_only_is_not_significant) {
    const std::vector<double> b = {40, 43, 38, 41, 44, 39, 42, 40, 37, 43};
    const std::vector<double> a = {41, 39, 44, 40, 38, 42, 43, 39, 41, 40};
    const auto c = compare_metric("cpu_pct", Direction::LowerIsBetter, b, a);
    EF_CHECK(c.verdict == Verdict::NoSignificantDifference);
    EF_CHECK(to_string(c.verdict) == "no_significant_difference");
    EF_CHECK(c.diff && c.ci_low && c.ci_high);
    EF_CHECK(*c.ci_low < 0 && *c.ci_high > 0);
    EF_CHECK(!c.improved);
    EF_CHECK(c.n_before == 10 && c.n_after == 10);
}

EF_TEST(before_after_clear_shift_lower_cpu_is_improvement) {
    const std::vector<double> b = {60, 62, 58, 61, 59, 63, 60, 61, 62, 59};
    const std::vector<double> a = {40, 42, 39, 41, 40, 38, 41, 42, 40, 39};
    const auto c = compare_metric("cpu_pct", Direction::LowerIsBetter, b, a);
    EF_CHECK(c.verdict == Verdict::Significant);
    EF_CHECK(c.diff && *c.diff < -15 && *c.diff > -25);
    EF_CHECK(*c.ci_high < 0);
    EF_CHECK(c.improved && *c.improved);
    // Ayni kayma, ters yon (yuksek iyi) -> kotulesme.
    const auto w = compare_metric("ram_free_bytes", Direction::HigherIsBetter, b, a);
    EF_CHECK(w.verdict == Verdict::Significant && w.improved && !*w.improved);
    // Kullanim artisi -> anlamli ama iyilesme degil.
    const auto up = compare_metric("gpu_pct", Direction::LowerIsBetter, a, b);
    EF_CHECK(up.verdict == Verdict::Significant && *up.diff > 0 && up.improved && !*up.improved);
}

EF_TEST(before_after_welch_interval_matches_hand_calc) {
    // once: ort 2, s^2 1 (n=3); sonra: ort 5, s^2 1 (n=3). se = sqrt(2/3), df = 4, t = 2.776.
    const auto c = compare_metric("x", Direction::HigherIsBetter, {1, 2, 3}, {4, 5, 6});
    const double half = 2.776 * std::sqrt(2.0 / 3.0);
    EF_CHECK(c.verdict == Verdict::Significant);
    EF_CHECK(std::fabs(*c.diff - 3.0) < 1e-9);
    EF_CHECK(std::fabs(*c.ci_low - (3.0 - half)) < 1e-9);
    EF_CHECK(std::fabs(*c.ci_high - (3.0 + half)) < 1e-9);
    EF_CHECK(*c.improved);
}

EF_TEST(before_after_fewer_than_three_samples_is_insufficient) {
    const auto c = compare_metric("cpu_pct", Direction::LowerIsBetter, {50, 51}, {10, 11, 12, 10});
    EF_CHECK(c.verdict == Verdict::InsufficientData);
    EF_CHECK(to_string(c.verdict) == "insufficient_data");
    EF_CHECK(!c.diff && !c.ci_low && !c.ci_high && !c.improved);
    EF_CHECK(c.mean_before && std::fabs(*c.mean_before - 50.5) < 1e-9);  // gozlenen ortalama korunur
    // NaN/sonsuz atlanir: 3 ham ornekten 2 gecerli kalir.
    const double nan = std::nan("");
    const auto d = compare_metric("cpu_pct", Direction::LowerIsBetter, {50, nan, 52}, {10, 11, 12});
    EF_CHECK(d.n_before == 2 && d.verdict == Verdict::InsufficientData);
    // Bir taraf hic yok -> unsupported degil, yetersiz veri.
    const auto e = compare_metric("gpu_pct", Direction::LowerIsBetter, {}, {10, 11, 12});
    EF_CHECK(e.verdict == Verdict::InsufficientData && !e.mean_before);
}

EF_TEST(before_after_never_available_is_unsupported) {
    // GPU hic olculmedi; CPU her ornekte var.
    const auto b = cpu_series({60, 61, 62, 60});
    const auto a = cpu_series({30, 31, 32, 30});
    const auto r = compare_before_after(b, a);
    const auto* gpu = find(r, "gpu_pct");
    EF_CHECK(gpu && gpu->verdict == Verdict::Unsupported);
    EF_CHECK(to_string(gpu->verdict) == "unsupported");
    EF_CHECK(!gpu->mean_before && !gpu->mean_after && !gpu->diff && !gpu->improved);
    EF_CHECK(gpu->n_before == 0 && gpu->n_after == 0);
    const auto* cpu = find(r, "cpu_pct");
    EF_CHECK(cpu && cpu->verdict == Verdict::Significant && *cpu->improved);
    EF_CHECK(r.metrics.size() == 4);  // disk koku yok
    EF_CHECK(!r.cleanup_freed_bytes);
}

EF_TEST(before_after_cleanup_bytes_pass_through_exact) {
    const std::uint64_t freed = 123456789012345ULL;
    const auto r = compare_before_after({}, {}, freed);
    EF_CHECK(r.cleanup_freed_bytes && *r.cleanup_freed_bytes == freed);
    for (const auto& c : r.metrics) EF_CHECK(c.verdict == Verdict::Unsupported);
}

EF_TEST(before_after_missing_samples_are_skipped_not_zero) {
    auto b = cpu_series({50, 50, 50});
    auto a = cpu_series({50, 50, 50});
    b.push_back(ResourceSample{});  // eksik ornek 0 sayilsaydi ortalama duserdi
    a.push_back(ResourceSample{});
    const auto r = compare_before_after(b, a);
    const auto* cpu = find(r, "cpu_pct");
    EF_CHECK(cpu->n_before == 3 && std::fabs(*cpu->mean_before - 50.0) < 1e-9);
    EF_CHECK(cpu->verdict == Verdict::NoSignificantDifference);  // sabit ve esit -> aralik [0,0]
    EF_CHECK(*cpu->ci_low == 0 && *cpu->ci_high == 0);
}

EF_TEST(before_after_disk_and_ram_free_higher_is_better) {
    std::vector<ResourceSample> b, a;
    for (int i = 0; i < 10; ++i) {
        ResourceSample s;
        s.ram_free_bytes = 4000000000ULL + static_cast<std::uint64_t>(i % 3) * 1000000ULL;
        s.volumes = {{"C:\\", 50000000000ULL}, {"D:\\", std::nullopt}};
        b.push_back(s);
        s.ram_free_bytes = 6000000000ULL + static_cast<std::uint64_t>(i % 2) * 1000000ULL;
        s.volumes = {{"C:\\", 55000000000ULL}, {"D:\\", std::nullopt}};
        a.push_back(s);
    }
    const auto r = compare_before_after(b, a, 5000000000ULL);
    const auto* ram = find(r, "ram_free_bytes");
    EF_CHECK(ram && ram->verdict == Verdict::Significant && *ram->improved);
    const auto* c = find(r, "disk_free_bytes:C:\\");
    EF_CHECK(c && c->verdict == Verdict::Significant && *c->improved);  // sabit kayma -> kesin fark
    EF_CHECK(*c->ci_low == 5000000000.0 && *c->ci_high == 5000000000.0);
    const auto* d = find(r, "disk_free_bytes:D:\\");
    EF_CHECK(d && d->verdict == Verdict::Unsupported);
    EF_CHECK(r.metrics.size() == 6);
    EF_CHECK(*r.cleanup_freed_bytes == 5000000000ULL);
}

EF_TEST(before_after_sample_from_live) {
    m::LiveSnapshot s;
    s.cpu_pct = Measurement::of(25, Unit::Percent);
    s.ram_pct = Measurement::of(50, Unit::Percent);
    s.ram_used_mib = 8192;
    s.ram_total_mib = 16384;
    s.volumes = {{"C:\\", 500, 100}, {"E:\\", std::nullopt, std::nullopt}};
    const auto r = sample_from_live(s);
    EF_CHECK(r.cpu_pct && *r.cpu_pct == 25);
    EF_CHECK(!r.gpu_pct);  // NotSampled -> nullopt, 0 degil
    EF_CHECK(r.ram_used_pct && *r.ram_used_pct == 50);
    EF_CHECK(r.ram_free_bytes && *r.ram_free_bytes == 8192ULL * 1048576ULL);
    EF_CHECK(r.volumes.size() == 2 && *r.volumes[0].free_bytes == 100ULL * 1073741824ULL);
    EF_CHECK(!r.volumes[1].free_bytes);
    // RAM yuzdesi eksikse bos RAM de uretilmez.
    m::LiveSnapshot g;
    g.ram_used_mib = 1;
    g.ram_total_mib = 2;
    g.ram_pct = Measurement::missing(Unit::Percent, MissingReason::SourceError);
    EF_CHECK(!sample_from_live(g).ram_free_bytes);
}
