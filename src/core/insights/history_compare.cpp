#include "core/insights/history_compare.h"

#include "core/perf/before_after.h"

#include <algorithm>

#include "core/insights/comparison_policy.h"
#include <cmath>
#include <iterator>
#include <set>

namespace ef::core::insights {

namespace {

std::string joined_exclusions(const std::vector<std::string>& v) {
    std::set<std::string> s(v.begin(), v.end());
    std::string out;
    for (const auto& x : s) {
        if (!out.empty()) out.push_back(';');
        out += x;
    }
    return out;
}

std::vector<std::string> sorted_unique(const std::vector<std::string>& v) {
    std::vector<std::string> out(v.begin(), v.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace

HistoryCompatibility history_compatibility(const HistoryEntry& base, const HistoryEntry& current) {
    // B3: olcum ekraniyla ayni ortak politika; burada ikinci bir kural kopyasi yok.
    const auto p = compare_compatibility(comparison_inputs(base), comparison_inputs(current));
    HistoryCompatibility c;
    c.available = p.available;
    c.compatible = p.comparable;
    c.differing = p.differing;
    c.note_key = p.note_key;
    c.conditions_match = p.conditions_match;
    return c;
}

Context history_context(const HistoryEntry& e) {
    Context c;
    if (!e.source.empty()) c.emplace("source", e.source);
    if (!e.fingerprint.empty()) c.emplace("fingerprint", e.fingerprint);
    if (!e.app_version.empty()) c.emplace("app_version", e.app_version);
    if (!e.scene_version.empty()) c.emplace("scene_version", e.scene_version);
    if (!e.protocol_version.empty()) c.emplace("protocol_version", e.protocol_version);
    c.emplace("run_valid", e.run_valid ? "true" : "false");
    c.emplace("exclusions", joined_exclusions(e.exclusions));  // gecerlilik farki baglamin parcasidir
    // Calisma kosullari (surucu, isletim sistemi...): degisirse fark bu degisiklige "denk gelmis" sayilir, neden sayilmaz.
    for (const auto& [k, v] : e.conditions) c.emplace("cond." + k, v);
    return c;
}

CohortComparison compare_cohort(const std::vector<HistoryEntry>& all, const HistoryEntry& base, const HistoryEntry& current) {
    CohortComparison out;
    const auto cond = [](const HistoryEntry& e, const char* k) {
        const auto it = e.conditions.find(k);
        return it == e.conditions.end() ? std::string() : it->second;
    };
    const auto is = [&](const HistoryEntry& e, const char* v) { return cond(e, "power") == v; };
    const HistoryEntry* off = is(base, "performance_off") ? &base : is(current, "performance_off") ? &current : nullptr;
    const HistoryEntry* on = is(base, "performance_on") ? &base : is(current, "performance_on") ? &current : nullptr;
    const char* key = base.source == "in_game" ? "avg_fps" : base.source == "endurance" ? "warm_fps" : base.source == "builtin" ? "scene_avg_fps" : nullptr;
    if (!off || !on || off == on || !key) return out;
    out.applicable = true;
    // Iki kol tek ortamda: surucu ve isletim sistemi de ayni olmali.
    const auto same_env = [&](const HistoryEntry& a, const HistoryEntry& b) {
        return !cond(a, "gpu_driver").empty() && cond(a, "gpu_driver") == cond(b, "gpu_driver") && !cond(a, "os").empty() && cond(a, "os") == cond(b, "os");
    };
    struct Run {
        std::int64_t when;
        bool is_on;
    };
    std::vector<Run> order;
    const auto collect = [&](const HistoryEntry& ref, bool arm_on) {
        std::vector<double> v;
        for (const auto& e : all) {
            if (cond(e, "power") != cond(ref, "power") || !same_env(ref, e)) continue;
            const auto c = compare_compatibility(comparison_inputs(ref), comparison_inputs(e));
            if (!c.comparable || !metric_comparable(c, ref, e, key).allowed) continue;
            const auto it = e.metrics.find(key);
            if (it == e.metrics.end() || !std::isfinite(it->second)) continue;
            v.push_back(it->second);
            order.push_back({e.when_unix_ms, arm_on});
        }
        return v;
    };
    if (!same_env(*off, *on)) return out;
    const auto a = collect(*off, false), b = collect(*on, true);
    out.n_off = a.size();
    out.n_on = b.size();
    if (a.size() < kMinCohortRuns || b.size() < kMinCohortRuns) return out;
    const auto m = perf::compare_metric(key, perf::Direction::HigherIsBetter, a, b);
    if (!m.diff || !m.ci_low || !m.ci_high) return out;
    out.enough = true;
    out.delta = m.diff;
    out.ci_low = m.ci_low;
    out.ci_high = m.ci_high;
    std::sort(order.begin(), order.end(), [](const Run& l, const Run& r) { return l.when < r.when; });
    bool off_to_on = false, on_to_off = false;
    for (std::size_t i = 1; i < order.size(); ++i) {
        off_to_on = off_to_on || (!order[i - 1].is_on && order[i].is_on);
        on_to_off = on_to_off || (order[i - 1].is_on && !order[i].is_on);
    }
    out.interleaved = off_to_on && on_to_off;
    out.uncertain = !out.interleaved || (*m.ci_low <= 0.0 && *m.ci_high >= 0.0);  // tek yonlu sira: etki denmez
    return out;
}

HistoryComparison compare_history(const HistoryEntry& base, const HistoryEntry& current) {
    HistoryComparison out;
    const auto p = compare_compatibility(comparison_inputs(base), comparison_inputs(current));
    out.compatibility = history_compatibility(base, current);
    const auto u = compare_update(history_context(base), history_context(current), {}, {});
    out.context = u.diff;
    out.attribution = u.attribution;
    // Surum bilinmiyorsa baglam eksiktir: degisiklik nedene atfedilemez (tek anahtar olsa bile).
    if (base.app_version.empty() || current.app_version.empty() || base.scene_version.empty() ||
        current.scene_version.empty() || base.protocol_version.empty() || current.protocol_version.empty())
        out.attribution = Attribution::ContextMissing;

    // Yalniz iki tarafta da/metrikte gercek ve sonlu degerler; eksik taraf nullopt kalir (0 degil).
    std::map<std::string, MetricChange> merged;
    for (const auto& [k, v] : base.metrics)
        if (safe_metric_key(k) && std::isfinite(v)) merged[k].base_value = v;
    for (const auto& [k, v] : current.metrics)
        if (safe_metric_key(k) && std::isfinite(v)) merged[k].current_value = v;
    for (auto& [k, m] : merged) {
        m.key = k;
        // B3: tekil olcum degerleri saklanir, fakat kosullar/surumler/gecerlilik uyusmuyorsa fark ve
        // yuzde URETILMEZ. Karar metrik basinadir: tamamlanmamis alt testin metrigi gecerli alt testleri bozmaz.
        const auto verdict = metric_comparable(p, base, current, k);
        if (verdict.allowed && m.base_value && m.current_value) {
            m.delta = *m.current_value - *m.base_value;
            if (*m.base_value != 0.0) m.percent = *m.delta / *m.base_value * 100.0;
        } else if (!verdict.allowed) {
            m.not_comparable_key = verdict.note_key;
        }
        out.metrics.push_back(std::move(m));
    }

    // Gecerlilik/dislama farklari acikca: iki yon de listelenir.
    const auto b = sorted_unique(base.exclusions);
    const auto c = sorted_unique(current.exclusions);
    std::set_difference(b.begin(), b.end(), c.begin(), c.end(), std::back_inserter(out.exclusions_only_base));
    std::set_difference(c.begin(), c.end(), b.begin(), b.end(), std::back_inserter(out.exclusions_only_current));
    return out;
}

}  // namespace ef::core::insights
