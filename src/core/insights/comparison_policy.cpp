#include "core/insights/comparison_policy.h"

#include <algorithm>
#include <cmath>

namespace ef::core::insights {

static std::string condition_of(const HistoryEntry& e, const char* key) {
    const auto it = e.conditions.find(key);
    return it == e.conditions.end() ? std::string() : it->second;
}

ComparisonInputs comparison_inputs(const HistoryEntry& e) {
    ComparisonInputs in;
    in.source = e.source;
    in.fingerprint = e.fingerprint;
    in.app_version = e.app_version;
    in.scene_version = e.scene_version;
    in.protocol_version = e.protocol_version;
    in.run_valid = e.run_valid;
    in.game_build = condition_of(e, "game_build");
    in.cpu = condition_of(e, "cpu");
    in.gpu = condition_of(e, "gpu");
    in.live_panel = condition_of(e, "live_panel");
    in.preview = condition_of(e, "preview");
    return in;
}

std::optional<i18n::StringId> comparison_field_label(std::string_view field) noexcept {
    using i18n::StringId;
    if (field == "source") return StringId::HistoryCompareCondSource;
    if (field == "fingerprint") return StringId::HistoryCompareCondFingerprint;
    if (field == "app_version") return StringId::HistoryCompareCondAppVersion;
    if (field == "scene_version") return StringId::HistoryCompareCondSceneVersion;
    if (field == "protocol_version") return StringId::HistoryCompareCondProtocolVersion;
    if (field == "run_valid") return StringId::HistoryCompareCondRunValid;
    if (field == "game_build") return StringId::HistoryCompareCondGameBuild;
    if (field == "hardware") return StringId::HistoryCompareCondHardware;
    if (field == "live_panel") return StringId::HistoryCompareCondLivePanel;
    if (field == "preview") return StringId::HistoryCompareCondPreview;
    if (field == "exclusions") return StringId::HistoryCompareCondExclusions;
    return std::nullopt;
}

ComparisonCompatibility compare_compatibility(const ComparisonInputs& base, const ComparisonInputs& current) {
    ComparisonCompatibility out;
    // Bos kaynak/parmak izi "bilinmiyor"dur; iki bos deger eslesme sayilmaz (kanitlanmis kosul yok).
    if (base.source.empty() || base.source != current.source) out.differing.emplace_back("source");
    if (base.fingerprint.empty() || base.fingerprint != current.fingerprint) out.differing.emplace_back("fingerprint");
    out.available = out.differing.empty();
    if (!out.available) return out;

    auto require_same_version = [&out](const std::string& b, const std::string& c, const char* field, const char* note) {
        if (b.empty() || c.empty() || b != c) {
            out.differing.emplace_back(field);
            if (out.note_key.empty()) out.note_key = note;
        }
    };
    require_same_version(base.app_version, current.app_version, "app_version", "comparison.version_mismatch");
    require_same_version(base.scene_version, current.scene_version, "scene_version", "comparison.scene_mismatch");
    require_same_version(base.protocol_version, current.protocol_version, "protocol_version",
                         "comparison.protocol_mismatch");
    if (base.source == "in_game" && (base.game_build.empty() || current.game_build.empty() || base.game_build != current.game_build)) {
        out.differing.emplace_back("game_build");
        if (out.note_key.empty()) out.note_key = "comparison.game_build_mismatch";
    }
    // Tek tarafta eksik donanim kanit degildir: fark uretilmez.
    const auto differs = [](const std::string& a, const std::string& b) { return a != b; };
    if (differs(base.cpu, current.cpu) || differs(base.gpu, current.gpu)) {
        out.differing.emplace_back("hardware");
        if (out.note_key.empty()) out.note_key = "comparison.hardware_mismatch";
    }
    if (base.live_panel != current.live_panel) {
        out.differing.emplace_back("live_panel");
        if (out.note_key.empty()) out.note_key = "comparison.live_panel_mismatch";
    }
    // Denge testi: canli test ekrani olcum yukunu degistirebilir; acik ve kapali kosular karsilastirilmaz.
    if (base.preview != current.preview) {
        out.differing.emplace_back("preview");
        if (out.note_key.empty()) out.note_key = "comparison.preview_mismatch";
    }
    if (!base.run_valid || !current.run_valid) {
        out.differing.emplace_back("run_valid");
        if (out.note_key.empty()) out.note_key = "comparison.invalid_run";
    }
    out.comparable = out.differing.empty();
    out.conditions_match = out.comparable || (out.differing.size() == 1 && out.differing.front() == "run_valid");
    return out;
}

std::string_view metric_validity_key(MetricValidity v) noexcept {
    switch (v) {
        case MetricValidity::Valid: return "valid";
        case MetricValidity::SubtestNotCompleted: return "subtest_not_completed";
        case MetricValidity::RunInvalid: return "run_invalid";
        case MetricValidity::NotMeasured: return "not_measured";
    }
    return "run_invalid";
}

std::string_view metric_subtest(std::string_view key) noexcept {
    if (key.rfind("scene_", 0) == 0) return "scene";
    if (key.rfind("cpu_", 0) == 0) return "cpu";
    if (key.rfind("mem_", 0) == 0) return "memory";
    if (key.rfind("storage_", 0) == 0) return "storage";
    if (key.rfind("combined_", 0) == 0) return "combined";
    return {};
}

bool storage_verify_ran(const HistoryEntry& e) {
    return e.metrics.find(std::string(kStorageVerifyMarkerMetric)) != e.metrics.end();
}

MetricValidity metric_validity(const HistoryEntry& e, std::string_view key) {
    const auto it = e.metrics.find(std::string(key));
    if (it == e.metrics.end() || !std::isfinite(it->second)) return MetricValidity::NotMeasured;
    const auto excluded = [&e](std::string_view subtest) {
        const std::string prefix = std::string(subtest) + ".";
        return std::any_of(e.exclusions.begin(), e.exclusions.end(), [&](const std::string& x) { return x.rfind(prefix, 0) == 0; });
    };
    const auto sub = metric_subtest(key);
    if (e.source != "builtin" || sub.empty() || e.protocol_version.empty()) return e.run_valid ? MetricValidity::Valid : MetricValidity::RunInvalid;
    if (excluded(sub)) return MetricValidity::SubtestNotCompleted;
    // run_valid=false yalniz sahne alt testinden gelir; sahne disi tamamlanmis alt testleri etkilemez. Baska nedenle
    // gecersiz (ornek eski kayit) ise tamami gecersizdir.
    if (!e.run_valid && !excluded("scene")) return MetricValidity::RunInvalid;
    return MetricValidity::Valid;
}

MetricComparison metric_comparable(const ComparisonCompatibility& compat, const HistoryEntry& base, const HistoryEntry& current,
                                   std::string_view key) {
    MetricComparison out;
    if (!compat.conditions_match) {
        out.note_key = compat.note_key;
        return out;
    }
    const auto a = metric_validity(base, key), b = metric_validity(current, key);
    if (a == MetricValidity::NotMeasured || b == MetricValidity::NotMeasured) {
        out.note_key = "comparison.metric_not_measured";
        return out;
    }
    if (a == MetricValidity::SubtestNotCompleted || b == MetricValidity::SubtestNotCompleted) {
        out.note_key = "comparison.metric_subtest";
        return out;
    }
    if (a != MetricValidity::Valid || b != MetricValidity::Valid) {
        out.note_key = "comparison.invalid_run";
        return out;
    }
    // EF-X22 dogrulama bulgusu: dogrulama fazi yazma butcesinin bir kismini kullanir; yazma profillerinin payi
    // kuculur. Fazin bir tarafta kosup digerinde kosmadigi depolama metrikleri ayni is yukunu olcmez.
    if (metric_subtest(key) == "storage" && storage_verify_ran(base) != storage_verify_ran(current)) {
        out.note_key = "comparison.protocol_mismatch";
        return out;
    }
    out.allowed = true;
    return out;
}

}  // namespace ef::core::insights
