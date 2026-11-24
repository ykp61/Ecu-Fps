#include "core/measure/bench_text.h"

#include <algorithm>

#include "core/i18n/catalog.h"

namespace ef::core::measure {

namespace {
bool ascii_code(std::string_view s) noexcept {
    return !s.empty() && s.size() <= 96 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
bool digits(std::string_view s) noexcept {
    return !s.empty() && s.size() <= 12 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool has_key(std::string_view key) { return i18n::id_of(key).has_value(); }
}  // namespace

const std::vector<ReasonFamily>& reason_families() {
    // Uzun önek önce (ör. "preparation_io_error_" "io_error_" ile başlamaz ama sıra yine de belirleyicidir).
    static const std::vector<ReasonFamily> f{
        {"preparation_io_error_", "bench.reason.preparation_io_error", true},
        {"io_port_failed_", "bench.reason.io_port_failed", true},
        {"io_error_", "bench.reason.io_error", true},
        {"no_overlap_storage_", "bench.reason.no_overlap_storage", false},
        {"cache_file_", "bench.reason.cache_file_failed", false},
        {"marker_", "bench.reason.marker_failed", false},
    };
    return f;
}

ReasonText reason_text(std::string_view code) {
    if (ascii_code(code)) {
        std::string exact = "bench.reason." + std::string(code);
        if (has_key(exact)) return {std::move(exact), {}};
        for (const auto& fam : reason_families()) {
            if (!code.starts_with(fam.prefix)) continue;
            const auto rest = code.substr(fam.prefix.size());
            if (!fam.numeric) return {std::string(fam.key), {}};
            if (digits(rest)) return {std::string(fam.key), std::string(rest)};
            break;  // sayısal aile ama sonek sayı değil: ham değer gösterilmez, genel metin
        }
    }
    return {"bench.reason.unknown", {}};
}

StatusTone status_tone(std::string_view s) noexcept {
    if (s == "completed") return StatusTone::Success;
    if (s == "partial" || s == "skipped") return StatusTone::Warning;
    if (s == "failed" || s == "unavailable") return StatusTone::Error;
    return StatusTone::Secondary;  // cancelled ve bilinmeyen
}

std::string_view evidence_status(std::string_view key) noexcept {
    if (!key.starts_with("bench.") || key.starts_with("bench.run.")) return {};  // kosu dislamalari uyari tonunda
    const auto dot = key.rfind('.');
    if (dot == std::string_view::npos || dot <= 6) return {};
    return key.substr(dot + 1);
}

BuiltinSummary builtin_summary(const std::vector<SubtestOutcome>& subtests) {
    BuiltinSummary s;
    for (const auto& t : subtests) {
        if (t.status == "skipped" && t.reason == "no_volume_selected") continue;  // kullanıcı seçimi: eksik sayılmaz
        ++s.total;
        if (t.status == "completed") ++s.done;
        if (t.status == "completed" || t.status == "partial") s.any_data = true;
    }
    if (!s.any_data)
        for (const auto& t : subtests)
            if (!t.reason.empty() && !(t.status == "skipped" && t.reason == "no_volume_selected")) {
                s.fail_reason = t.reason;
                break;
            }
    if (s.total > 0 && s.done == s.total) s.headline_key = "bench.result.completed";
    else if (s.any_data) {
        s.headline_key = "bench.result.partial";
        s.args = {{"done", std::to_string(s.done)}, {"total", std::to_string(s.total)}};
    } else s.headline_key = "bench.result.none";
    return s;
}

std::string builtin_history_headline(const std::vector<std::string>& exclusions) {
    for (const auto& e : exclusions)
        if (e != "storage.skipped" && e != "combined.skipped") return "bench.result.incomplete";
    return "bench.result.completed";
}

std::string tier_key(std::string_view tier) {
    if (tier == "low" || tier == "medium" || tier == "high") return "bench.tier." + std::string(tier);
    return {};
}

std::string scene_tier_from_title(std::string_view title) {
    if (!title.starts_with("scene_v")) return {};
    const auto sp = title.find(' ');
    if (sp == std::string_view::npos) return {};
    const auto tier = title.substr(sp + 1);
    return tier_key(tier).empty() ? std::string() : std::string(tier);
}

contracts::MissingReason scene_missing_reason(std::string_view status) noexcept {
    using contracts::MissingReason;
    if (status == "unavailable") return MissingReason::NotSupported;  // bu sistemde çalışmıyor
    if (status == "failed" || status == "cancelled") return MissingReason::SourceError;  // kaynak hatası
    return MissingReason::NotSampled;  // skipped/partial/bilinmeyen: örneklenmedi
}

contracts::Confidence result_confidence(const std::vector<SubtestOutcome>& subtests,
                                        const std::map<std::string, double>& metrics) {
    using contracts::Confidence;
    bool scene_ok = false, cpu_ok = false, mem_ok = false, degraded = false;
    for (const auto& t : subtests) {
        const bool user_skip = t.status == "skipped" && t.reason == "no_volume_selected";
        if (t.id == "scene") {
            scene_ok = t.status == "completed";
            if (!scene_ok) degraded = true;
        } else if (t.id == "cpu" || t.id == "memory") {
            const bool ok = t.status == "completed";
            (t.id == "cpu" ? cpu_ok : mem_ok) = ok;
            if (!ok) degraded = true;
        } else if (!user_skip && (t.status == "partial" || t.status == "failed" || t.status == "unavailable")) {
            degraded = true;  // depolama/birleşik kısmi veya başarısız: başlık güveni düşer
        }
    }
    // MT çekişmesi yalnız ölçüldüyse dikkate alınır (bilinmiyorsa "çekişme yok" iddiası yok).
    auto contended = [&](const char* known, const char* flag) {
        const auto k = metrics.find(known), f = metrics.find(flag);
        return k != metrics.end() && k->second > 0.5 && f != metrics.end() && f->second > 0.5;
    };
    const bool contention = contended("cpu_mt_contention_known", "cpu_mt_contended") ||
                            contended("mem_mt_contention_known", "mem_mt_contended");
    if (!scene_ok || degraded || contention) return Confidence::Low;
    return cpu_ok && mem_ok ? Confidence::Medium : Confidence::Low;
}

}  // namespace ef::core::measure
