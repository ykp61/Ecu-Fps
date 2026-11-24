#include "core/perf/before_after.h"

#include <cmath>
#include <utility>

namespace ef::core::perf {

namespace {

constexpr std::size_t kMinSamples = 3;

std::optional<double> opt_value(const contracts::Measurement& m) {
    const auto v = m.value();
    if (!v || !std::isfinite(*v)) return std::nullopt;
    return v;
}

std::vector<double> finite_only(const std::vector<double>& in) {
    std::vector<double> out;
    out.reserve(in.size());
    for (double v : in)
        if (std::isfinite(v)) out.push_back(v);
    return out;
}

double mean_of(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

// Orneklem varyansi (n-1).
double var_of(const std::vector<double>& v, double m) {
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return s / static_cast<double>(v.size() - 1);
}

template <typename Get>
std::vector<double> collect(const std::vector<ResourceSample>& samples, Get get) {
    std::vector<double> out;
    for (const auto& s : samples) {
        const std::optional<double> v = get(s);
        if (v) out.push_back(*v);
    }
    return out;
}

}  // namespace

ResourceSample sample_from_live(const metrics::LiveSnapshot& s) {
    ResourceSample r;
    r.cpu_pct = opt_value(s.cpu_pct);
    r.gpu_pct = opt_value(s.gpu_pct);
    r.ram_used_pct = opt_value(s.ram_pct);
    if (r.ram_used_pct && s.ram_total_mib > 0 && s.ram_used_mib <= s.ram_total_mib)
        r.ram_free_bytes = (s.ram_total_mib - s.ram_used_mib) * (std::uint64_t{1} << 20);
    for (const auto& v : s.volumes) {
        VolumeFree f;
        f.root = v.root;
        if (v.free_gib) f.free_bytes = *v.free_gib * (std::uint64_t{1} << 30);
        r.volumes.push_back(std::move(f));
    }
    return r;
}

std::string_view to_string(Verdict v) noexcept {
    switch (v) {
        case Verdict::Significant: return "significant";
        case Verdict::NoSignificantDifference: return "no_significant_difference";
        case Verdict::InsufficientData: return "insufficient_data";
        case Verdict::Unsupported: return "unsupported";
    }
    return "unsupported";
}

double t_crit_95(double df) noexcept {
    // Iki yonlu %95 (alpha = 0.05) Student t kritik degerleri, df = 1..30.
    static constexpr double kTable[30] = {12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306,
                                          2.262,  2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120,
                                          2.110,  2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064,
                                          2.060,  2.056, 2.052, 2.048, 2.045, 2.042};
    if (!(df >= 1.0)) return kTable[0];  // NaN dahil
    if (df < 31.0) return kTable[static_cast<int>(std::floor(df)) - 1];
    if (df < 40.0) return 2.042;
    if (df < 60.0) return 2.021;
    if (df < 120.0) return 2.000;
    if (std::isinf(df)) return 1.960;
    return 1.980;
}

MetricComparison compare_metric(std::string key, Direction dir, const std::vector<double>& before_in,
                                const std::vector<double>& after_in) {
    MetricComparison c;
    c.key = std::move(key);
    c.direction = dir;
    const auto before = finite_only(before_in);
    const auto after = finite_only(after_in);
    c.n_before = before.size();
    c.n_after = after.size();
    if (!before.empty()) c.mean_before = mean_of(before);
    if (!after.empty()) c.mean_after = mean_of(after);

    if (before.empty() && after.empty()) {
        c.verdict = Verdict::Unsupported;
        return c;
    }
    if (before.size() < kMinSamples || after.size() < kMinSamples) {
        c.verdict = Verdict::InsufficientData;
        return c;
    }

    const double mb = *c.mean_before;
    const double ma = *c.mean_after;
    const double d = ma - mb;
    c.diff = d;
    const double qb = var_of(before, mb) / static_cast<double>(before.size());
    const double qa = var_of(after, ma) / static_cast<double>(after.size());
    const double se2 = qb + qa;

    if (!(se2 > 0.0)) {
        // Iki taraf da sabit: fark kesindir, aralik tek noktadir.
        c.ci_low = d;
        c.ci_high = d;
    } else {
        const double nb1 = static_cast<double>(before.size() - 1);
        const double na1 = static_cast<double>(after.size() - 1);
        const double df = (se2 * se2) / ((qb * qb) / nb1 + (qa * qa) / na1);
        const double half = t_crit_95(df) * std::sqrt(se2);
        c.ci_low = d - half;
        c.ci_high = d + half;
    }

    if (*c.ci_low <= 0.0 && *c.ci_high >= 0.0) {
        c.verdict = Verdict::NoSignificantDifference;
        return c;
    }
    c.verdict = Verdict::Significant;
    c.improved = dir == Direction::LowerIsBetter ? d < 0.0 : d > 0.0;
    return c;
}

BeforeAfterReport compare_before_after(const std::vector<ResourceSample>& before,
                                       const std::vector<ResourceSample>& after,
                                       std::optional<std::uint64_t> cleanup_freed_bytes) {
    BeforeAfterReport r;
    r.cleanup_freed_bytes = cleanup_freed_bytes;

    auto add = [&](const char* key, Direction dir, auto get) {
        r.metrics.push_back(compare_metric(key, dir, collect(before, get), collect(after, get)));
    };
    add("cpu_pct", Direction::LowerIsBetter, [](const ResourceSample& s) { return s.cpu_pct; });
    add("gpu_pct", Direction::LowerIsBetter, [](const ResourceSample& s) { return s.gpu_pct; });
    add("ram_used_pct", Direction::LowerIsBetter, [](const ResourceSample& s) { return s.ram_used_pct; });
    add("ram_free_bytes", Direction::HigherIsBetter, [](const ResourceSample& s) -> std::optional<double> {
        if (!s.ram_free_bytes) return std::nullopt;
        return static_cast<double>(*s.ram_free_bytes);
    });

    std::vector<std::string> roots;
    auto note_roots = [&](const std::vector<ResourceSample>& v) {
        for (const auto& s : v)
            for (const auto& vol : s.volumes) {
                bool seen = false;
                for (const auto& r0 : roots) seen = seen || r0 == vol.root;
                if (!seen) roots.push_back(vol.root);
            }
    };
    note_roots(before);
    note_roots(after);
    for (const auto& root : roots) {
        auto get = [&root](const ResourceSample& s) -> std::optional<double> {
            for (const auto& vol : s.volumes)
                if (vol.root == root && vol.free_bytes) return static_cast<double>(*vol.free_bytes);
            return std::nullopt;
        };
        r.metrics.push_back(compare_metric("disk_free_bytes:" + root, Direction::HigherIsBetter,
                                           collect(before, get), collect(after, get)));
    }
    for (auto& m : r.metrics) apply_min_effect(m, min_effect_for(m.key));
    return r;
}

double min_effect_for(std::string_view key) noexcept {
    if (key == "cpu_pct" || key == "gpu_pct" || key == "ram_used_pct") return 1.0;  // yuzde puani
    if (key == "ram_free_bytes") return 100.0 * 1024 * 1024;                          // 100 MiB
    if (key.rfind("disk_free_bytes:", 0) == 0) return 1024.0 * 1024 * 1024;           // 1 GiB (olcum cozunurlugu)
    return 0.0;
}

void apply_min_effect(MetricComparison& m, double min_effect) noexcept {
    if (m.verdict != Verdict::Significant || !m.diff || min_effect <= 0) return;
    if (std::fabs(*m.diff) < min_effect) {
        m.verdict = Verdict::NoSignificantDifference;  // istatistiksel olarak secilebilir ama pratikte onemsiz
        m.improved.reset();
    }
}

}  // namespace ef::core::perf
