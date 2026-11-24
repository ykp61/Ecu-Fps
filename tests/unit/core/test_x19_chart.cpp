#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "core/measure/frame_stats.h"
#include "ef_test.h"

namespace cm = ef::core::measure;

EF_TEST(x19_t04_downsample_never_changes_low_percentiles) {
    std::vector<double> ms;
    for (int i = 0; i < 5000; ++i) ms.push_back(i % 97 == 0 ? 40.0 : 6.0);
    const auto before = cm::compute_frame_stats(ms);
    const auto small = cm::downsample_for_chart(ms, 120);
    const auto after = cm::compute_frame_stats(ms);
    EF_CHECK(small.size() == 120 && before.low1_fps == after.low1_fps);
    EF_CHECK(cm::downsample_for_chart({1, 2, 3}, 120).size() == 3);  // kisa seri dokunulmaz
}

namespace {
// ~10 dk, 6 ms tabanli; her 97. kare 40 ms takilma (seyrek, ortalamada kaybolur).
std::vector<double> long_run() {
    std::vector<double> ms;
    for (int i = 0; i < 100000; ++i) ms.push_back(i % 97 == 0 ? 40.0 : 6.0);
    return ms;
}
bool same(const std::optional<double>& a, const std::optional<double>& b) { return a.has_value() == b.has_value() && (!a || *a == *b); }
}  // namespace

// EF-X19.02 kabul: "Grafik ornek azaltimi low percentile sonucunu degistirmez." Urundeki yol (benchmark sahnesi:
// runner.cpp frame_stats_with_series) seriyi ve istatistigi ayni ham vektorden uretir; azaltma miktari (max_points)
// istatistigi degistiremez ve istatistik ham vektorden dogrudan hesaplananla birebir aynidir.
EF_TEST(x19_02_chart_reduction_and_percentiles_share_raw_source) {
    const auto raw = long_run();
    const auto direct = cm::compute_frame_stats(raw);
    EF_CHECK(direct.sufficient && direct.low1_fps && direct.p99_ms && direct.low01_fps);
    std::size_t prev_points = 0;
    for (std::size_t pts : {std::size_t{1000}, std::size_t{120}, std::size_t{30}, std::size_t{5}}) {
        const auto c = cm::frame_stats_with_series(raw, pts);
        EF_CHECK(!c.fps_series.empty() && c.fps_series.size() <= pts);
        if (prev_points) EF_CHECK(c.fps_series.size() <= prev_points);  // gercekten azaltildi
        prev_points = c.fps_series.size();
        EF_CHECK(c.stats.n == direct.n);
        EF_CHECK(same(c.stats.low1_fps, direct.low1_fps));
        EF_CHECK(same(c.stats.low01_fps, direct.low01_fps));
        EF_CHECK(same(c.stats.p99_ms, direct.p99_ms));
        EF_CHECK(same(c.stats.p95_ms, direct.p95_ms));
        EF_CHECK(same(c.stats.avg_fps, direct.avg_fps));
    }
    // Varsayilan nokta sayisi urundeki seri ile ayni (fps_per_second, kMaxFpsSeries).
    const auto def = cm::frame_stats_with_series(raw);
    EF_CHECK(def.fps_series == cm::fps_per_second(raw));
    EF_CHECK(def.fps_series.size() == cm::kMaxFpsSeries);
}

// Karsi kanit: %1 dusuk azaltilmis seriden hesaplansaydi takilmalar ortalamada kaybolurdu. Ham kaynakla fark buyuk
// oldugu icin yukaridaki esitlik anlamlidir (test, yuzdeligin seriden HESAPLANMADIGINI gosterir).
EF_TEST(x19_02_low_from_reduced_series_would_hide_stutter) {
    const auto raw = long_run();
    const auto c = cm::frame_stats_with_series(raw, 120);
    EF_CHECK(c.stats.low1_fps.has_value());
    if (!c.stats.low1_fps) return;
    // Seri noktalari FPS; en yavas %1 noktanin FPS'i ~160 (ortalama), ham %1 dusuk ise 1000/40 = 25.
    double min_series_fps = 1e9;
    for (double f : c.fps_series) min_series_fps = std::min(min_series_fps, f);
    EF_CHECK(std::fabs(*c.stats.low1_fps - 25.0) < 0.5);
    EF_CHECK(min_series_fps > 4.0 * *c.stats.low1_fps);
}
