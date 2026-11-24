// R16: iki yerel gecmis kaydinin durust karsilastirmasi (kosul eslesmesi, eksik metrik, taban 0).
#include <algorithm>
#include <cmath>
#include <limits>

#include "core/insights/history_compare.h"
#include "ef_test.h"

using namespace ef::core::insights;

namespace {

HistoryEntry entry(std::string fingerprint = "scene_v1|high|17a5", std::string source = "builtin") {
    HistoryEntry e;
    e.source = std::move(source);
    e.fingerprint = std::move(fingerprint);
    e.app_version = "0.1.0";
    e.scene_version = "scene_v3";
    e.protocol_version = "bench_v1";
    e.run_valid = true;
    e.title = "scene_v1 high";
    return e;
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

const MetricChange* metric(const HistoryComparison& c, const std::string& key) {
    for (const auto& m : c.metrics)
        if (m.key == key) return &m;
    return nullptr;
}

}  // namespace

EF_TEST(history_compare_compatible_metrics_missing_and_zero_baseline) {
    auto base = entry();
    base.metrics = {{"avg_fps", 100.0}, {"low1_fps", 80.0}, {"zero_metric", 0.0}};
    auto current = base;
    current.metrics = {{"avg_fps", 110.0}, {"zero_metric", 5.0}, {"extra_metric", 7.0}};
    const auto c = compare_history(base, current);
    EF_CHECK(c.compatibility.compatible && c.compatibility.differing.empty());
    EF_CHECK(c.context.changed.empty() && c.attribution == Attribution::NoChangeInContext);

    EF_CHECK(c.metrics.size() == 4);  // avg_fps, extra_metric, low1_fps, zero_metric (sirali)
    const auto* avg = metric(c, "avg_fps");
    EF_CHECK(avg && avg->base_value && avg->current_value && avg->delta && std::fabs(*avg->delta - 10.0) < 1e-9);
    EF_CHECK(avg->percent && std::fabs(*avg->percent - 10.0) < 1e-9);
    const auto* low = metric(c, "low1_fps");
    EF_CHECK(low && low->base_value && !low->current_value && !low->delta && !low->percent);  // eksik, 0 degil
    const auto* extra = metric(c, "extra_metric");
    EF_CHECK(extra && !extra->base_value && extra->current_value && !extra->delta && !extra->percent);
    const auto* zero = metric(c, "zero_metric");
    EF_CHECK(zero && zero->base_value && *zero->base_value == 0.0 && zero->delta && std::fabs(*zero->delta - 5.0) < 1e-9);
    EF_CHECK(!zero->percent);  // taban 0: yuzde hesaplanmaz
}

EF_TEST(history_compare_incompatible_conditions) {
    auto base = entry();
    base.metrics = {{"avg_fps", 100.0}};
    auto other_fp = entry("scene_v1|medium|9999");
    other_fp.metrics = base.metrics;
    auto c = compare_history(base, other_fp);
    EF_CHECK(!c.compatibility.compatible);
    EF_CHECK(has(c.compatibility.differing, "fingerprint") && !has(c.compatibility.differing, "source"));

    auto other_src = entry(base.fingerprint, "in_game");
    other_src.metrics = base.metrics;
    c = compare_history(base, other_src);
    EF_CHECK(!c.compatibility.compatible && has(c.compatibility.differing, "source"));

    auto unknown_a = entry(""), unknown_b = entry("");  // iki bos iz kanitlanmis kosul degildir
    c = compare_history(unknown_a, unknown_b);
    EF_CHECK(!c.compatibility.compatible && has(c.compatibility.differing, "fingerprint"));
    // Kosullar uyusmuyorsa metrik farki gosterilse de "neden" atfi yapilmaz (UI uyumsuzlugu ayri gosterir).
}

EF_TEST(history_compare_context_attribution_is_honest) {
    auto base = entry();
    base.metrics = {{"avg_fps", 100.0}};
    auto current = base;
    current.metrics = {{"avg_fps", 110.0}};
    current.app_version = "0.2.0";
    auto c = compare_history(base, current);
    EF_CHECK(c.attribution == Attribution::SingleChangeCoincides);  // tek degisiklik: denk geldi, neden degil
    EF_CHECK(c.context.changed.size() == 1 && c.context.changed[0] == "app_version");

    current.exclusions = {"run.too_short"};  // ikinci kosul degisikligi -> karisik
    c = compare_history(base, current);
    EF_CHECK(c.attribution == Attribution::ConfoundedMultipleChanges);
    EF_CHECK(c.context.changed.size() == 2 && has(c.context.changed, "exclusions"));

    current.app_version.clear();  // surum bilinmiyor -> baglam eksik, atif yok
    c = compare_history(base, current);
    EF_CHECK(c.attribution == Attribution::ContextMissing);
}

EF_TEST(history_compare_exclusions_listed_per_side) {
    auto base = entry();
    base.exclusions = {"run.too_short", "drop.insufficient"};
    auto current = base;
    current.exclusions = {"drop.insufficient", "run.overlay"};
    const auto c = compare_history(base, current);
    EF_CHECK(c.exclusions_only_base.size() == 1 && c.exclusions_only_base[0] == "run.too_short");
    EF_CHECK(c.exclusions_only_current.size() == 1 && c.exclusions_only_current[0] == "run.overlay");
    EF_CHECK(has(c.context.changed, "exclusions"));
}

EF_TEST(history_compare_versions_and_run_validity_block_delta) {
    auto base = entry();
    base.metrics = {{"avg_fps", 100.0}};
    auto current = base;
    current.metrics = {{"avg_fps", 110.0}};

    // B3: sahne surumu farkli -> tekil degerler saklanir, fark/yuzde uretilmez.
    current.scene_version = "scene_v4";
    auto c = compare_history(base, current);
    EF_CHECK(c.compatibility.available && !c.compatibility.compatible);
    EF_CHECK(has(c.compatibility.differing, "scene_version"));
    EF_CHECK(c.compatibility.note_key == "comparison.scene_mismatch");
    EF_CHECK(c.metrics.size() == 1 && c.metrics[0].base_value && c.metrics[0].current_value);
    EF_CHECK(!c.metrics[0].delta && !c.metrics[0].percent);

    // Olcum protokolu farkli.
    current = base;
    current.metrics = {{"avg_fps", 110.0}};
    current.protocol_version = "bench_v2";
    c = compare_history(base, current);
    EF_CHECK(!c.compatibility.compatible && c.compatibility.note_key == "comparison.protocol_mismatch");
    EF_CHECK(c.metrics.size() == 1 && c.metrics[0].base_value && c.metrics[0].current_value);
    EF_CHECK(!c.metrics[0].delta && !c.metrics[0].percent);

    // Gecersiz/yarim kosu: fark iddia edilmez.
    current = base;
    current.metrics = {{"avg_fps", 110.0}};
    current.run_valid = false;
    c = compare_history(base, current);
    EF_CHECK(!c.compatibility.compatible && c.compatibility.note_key == "comparison.invalid_run");
    EF_CHECK(c.metrics.size() == 1 && !c.metrics[0].delta && !c.metrics[0].percent);

    // Eski kayit surum alanlarini tasimiyor: esit sayilmaz, fail-closed.
    auto old_base = base;
    old_base.scene_version.clear();
    c = compare_history(old_base, current);
    EF_CHECK(c.compatibility.available && !c.compatibility.compatible);
    EF_CHECK(has(c.compatibility.differing, "scene_version"));
    EF_CHECK(!c.metrics[0].delta && !c.metrics[0].percent);
}

EF_TEST(history_compare_unsafe_and_nonfinite_metrics_ignored) {
    auto base = entry();
    base.metrics = {{"avg_fps", 100.0}, {"C:\\Users\\name\\secret", 1.0}, {"nan_metric", std::numeric_limits<double>::quiet_NaN()}};
    auto current = base;
    current.metrics = {{"avg_fps", 120.0}, {"C:\\Users\\name\\secret", 2.0}};
    const auto c = compare_history(base, current);
    EF_CHECK(c.metrics.size() == 1 && c.metrics[0].key == "avg_fps");  // izin listesi disi anahtar yok
    EF_CHECK(c.metrics[0].percent && std::fabs(*c.metrics[0].percent - 20.0) < 1e-9);
}

static HistoryEntry x19_run(const char* power, double fps) {
    HistoryEntry e;
    e.source = "builtin";
    e.fingerprint = "fp";
    e.app_version = "1";
    e.scene_version = "s";
    e.protocol_version = "p";
    e.run_valid = true;
    e.conditions["power"] = power;
    e.conditions["gpu_driver"] = "1";
    e.conditions["os"] = "w";
    e.metrics["scene_avg_fps"] = fps;
    return e;
}

EF_TEST(x19_t03_cohort_needs_three_runs_each_side) {
    const auto off = x19_run("performance_off", 100), on = x19_run("performance_on", 120);
    auto c = compare_cohort({off, on}, off, on);
    EF_CHECK(!c.enough && !c.delta && c.uncertain && c.n_off == 1 && c.n_on == 1);  // tek cift: etki kaniti degil
    c = compare_cohort({x19_run("performance_off", 100), x19_run("performance_off", 101), x19_run("performance_off", 99), x19_run("performance_on", 120)}, off, on);
    EF_CHECK(!c.enough);
}

EF_TEST(x19_cohort_reports_interval_and_withholds_on_overlap) {
    const auto off = x19_run("performance_off", 100), on = x19_run("performance_on", 120);
    std::vector<HistoryEntry> clear{x19_run("performance_off", 100), x19_run("performance_on", 120), x19_run("performance_off", 101),
                                    x19_run("performance_on", 121), x19_run("performance_on", 119), x19_run("performance_off", 99)};
    for (std::size_t i = 0; i < clear.size(); ++i) clear[i].when_unix_ms = static_cast<std::int64_t>(i);  // kollar karisik
    auto c = compare_cohort(clear, off, on);
    EF_CHECK(c.enough && c.delta && *c.delta > 15 && !c.uncertain);
    std::vector<HistoryEntry> noisy{x19_run("performance_off", 90), x19_run("performance_off", 130), x19_run("performance_off", 100),
                                    x19_run("performance_on", 95), x19_run("performance_on", 135), x19_run("performance_on", 110)};
    c = compare_cohort(noisy, off, on);
    EF_CHECK(c.enough && c.uncertain);  // fark gosterilir, iyilesti denmez
    {
        auto serial = clear;  // tum Kapali once, tum Acik sonra: sira etkisi dislanamaz
        for (std::size_t i = 0; i < serial.size(); ++i) serial[i].when_unix_ms = serial[i].conditions["power"] == "performance_on" ? 100 + static_cast<std::int64_t>(i) : static_cast<std::int64_t>(i);
        EF_CHECK(compare_cohort(serial, off, on).enough && compare_cohort(serial, off, on).uncertain);
        EF_CHECK(*compare_cohort(clear, on, off).delta > 15);  // kayit sirasi yon degistirmez: delta her zaman Acik - Kapali
        auto driver = clear;
        driver[1].conditions["gpu_driver"] = "2";
        EF_CHECK(compare_cohort(driver, off, on).n_on == 2);
    }
    auto invalid = clear;
    invalid[0].run_valid = false;
    EF_CHECK(!compare_cohort(invalid, off, on).enough);  // gecersiz kosu sayilmaz
}

EF_TEST(x19_game_build_survives_history_roundtrip) {
    History h;
    auto e = x19_run("performance_off", 100);
    e.source = "in_game";
    e.conditions["game_build"] = "100";
    (void)h.add(e);
    const auto parsed = History::parse(h.serialize());
    EF_CHECK(parsed.ok() && parsed.value().entries().front().conditions.at("game_build") == "100");
}
