#include "core/measure/balance_view.h"

#include <algorithm>
#include <cmath>

#include "core/i18n/catalog.h"

namespace ef::core::measure {

namespace {
namespace dg = diagnosis;

constexpr std::string_view kGlobalPrefix = "balance.global.";
constexpr std::string_view kRowPrefix = "balance.row_";
constexpr std::string_view kNotePrefix = "balance.note.";
// denge_v4: CPU adiminin / tekrar adiminin kendi gecersizlik nedeni ("detail.cpu_step.<neden>", "detail.cpu_repeat.<neden>").
constexpr std::string_view kDetailCpuStepPrefix = "detail.cpu_step.";
constexpr std::string_view kDetailCpuRepeatPrefix = "detail.cpu_repeat.";

constexpr std::array<std::string_view, kBalanceStages> kStageKeys = {
    "measure.stage_balance_s1", "measure.stage_balance_s2", "measure.stage_balance_s3",
    "measure.stage_balance_s4", "measure.stage_balance_k1", "measure.stage_balance_k2",
    "measure.stage_balance_k3", "measure.stage_balance_t1", "measure.stage_balance_t2"};

bool code_chars(std::string_view s) noexcept {
    return !s.empty() && s.size() <= 48 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::optional<double> metric(const insights::HistoryEntry& e, const std::string& key) {
    const auto it = e.metrics.find(key);
    if (it == e.metrics.end() || !std::isfinite(it->second)) return std::nullopt;
    return it->second;
}

// Olculen FPS: sonlu ve pozitif (0 FPS olcum degildir).
std::optional<double> fps_metric(const insights::HistoryEntry& e, const std::string& key) {
    const auto v = metric(e, key);
    return v && *v > 0 ? v : std::nullopt;
}

std::optional<double> share_metric(const insights::HistoryEntry& e, const std::string& key) {
    const auto v = metric(e, key);
    return v && *v >= 0 && *v <= 1 ? v : std::nullopt;
}

// Kayittaki neden kodu: katalogda karsiligi olan kod aynen, digerleri "unknown" (ham metin gosterilmez).
std::string known_reason(std::string_view code) {
    if (code_chars(code) && i18n::id_of("balance.reason." + std::string(code)).has_value()) return std::string(code);
    return std::string(kBalanceUnknownReason);
}

bool needs_reason(dg::BalanceVerdict v) noexcept {
    return v == dg::BalanceVerdict::Undetermined || v == dg::BalanceVerdict::Unmeasurable;
}
}  // namespace

bool BalanceView::has(dg::BalanceVerdict v) const noexcept {
    return std::any_of(rows.begin(), rows.end(), [v](const BalanceRowView& r) { return r.verdict == v; });
}

bool balance_view_unmeasured(const BalanceView& v) noexcept {
    if (!v.global_reasons.empty()) return true;
    return std::any_of(v.rows.begin(), v.rows.end(), [](const BalanceRowView& r) {
        return r.verdict == dg::BalanceVerdict::Undetermined || r.verdict == dg::BalanceVerdict::Unmeasurable;
    });
}

std::map<std::string, double> balance_metrics(const dg::BalanceResult& r) {
    std::map<std::string, double> out;
    for (auto& [k, v] : dg::balance_history_metrics(r))
        if (std::isfinite(v)) out[k] = v;
    if (r.cpu_sim_share && r.cpu_draw_share && std::isfinite(*r.cpu_sim_share) && std::isfinite(*r.cpu_draw_share)) {
        out["balance.cpu_sim_share"] = *r.cpu_sim_share;
        out["balance.cpu_draw_share"] = *r.cpu_draw_share;
    }
    return out;
}

std::vector<std::string> balance_exclusions(const dg::BalanceResult& r) {
    std::vector<std::string> out;
    for (const auto reason : r.global_reasons)
        if (!reason.empty()) out.push_back(std::string(kGlobalPrefix) + std::string(reason));
    if (!r.cpu_step_reason.empty()) out.push_back(std::string(kDetailCpuStepPrefix) + std::string(r.cpu_step_reason));
    if (!r.cpu_repeat_reason.empty()) out.push_back(std::string(kDetailCpuRepeatPrefix) + std::string(r.cpu_repeat_reason));
    for (std::size_t i = 0; i < dg::kBalanceRows; ++i) {
        const auto& row = r.rows[i];
        if (!needs_reason(row.verdict) || row.reason.empty()) continue;
        // Genel neden satirlara da kopyalanir; kayitta bir kez (genel) durur.
        if (!r.global_reasons.empty() && row.reason == r.global_reason) continue;
        out.push_back(std::string(kRowPrefix) + std::string(dg::kBalanceRowKeys[i]) + "." + std::string(row.reason));
    }
    for (const auto note : r.notes)
        if (!note.empty()) out.push_back(std::string(kNotePrefix) + std::string(note));
    return out;
}

void add_balance_check_metrics(std::map<std::string, double>& m, const BalanceRunChecks& c) {
    const auto put = [&m](const char* key, const std::optional<double>& v) {
        if (v && std::isfinite(*v)) m[key] = *v;
    };
    const auto put_u = [&m](const char* key, const std::optional<std::uint32_t>& v) {
        if (v) m[key] = static_cast<double>(*v);
    };
    if (c.pcie && dg::evaluate_pcie_under_load(*c.pcie).measured_under_load) {
        put_u("balance.pcie_width", c.pcie->cur_width);
        put_u("balance.pcie_width_max", c.pcie->max_width);
        put_u("balance.pcie_gen", c.pcie->cur_gen);
        put_u("balance.pcie_gen_max", c.pcie->max_gen);
        put_u("balance.pcie_slot_width_max", c.pcie->slot_max_width);
        put_u("balance.pcie_slot_gen_max", c.pcie->slot_max_gen);
        put("balance.pcie_load_pct", c.pcie->gpu_load_pct);
    }
    put("balance.cpu_perf_first_pct", c.cpu_speed.first_pct);
    put("balance.cpu_perf_last_pct", c.cpu_speed.last_pct);
    put("balance.cpu_perf_limit_min_pct", c.cpu_speed.min_limit_pct);
    put_u("balance.cpu_fw_limit_events", c.cpu_firmware_limit_events);
    put("balance.other_gpu_pct", c.other_gpu_pct);
    put("balance.other_cpu_pct", c.other_cpu_pct);
    m["balance.retries"] = static_cast<double>(c.retries);
}

std::map<std::string, std::string> balance_check_conditions(const BalanceRunChecks& c) {
    std::map<std::string, std::string> out;
    out["retries"] = std::to_string(c.retries);
    out["other_gpu"] = !c.other_gpu_pct || !std::isfinite(*c.other_gpu_pct) ? "unknown"
                       : *c.other_gpu_pct < dg::kBalanceOtherGpuMaxPct     ? "low"
                                                                            : "high";
    return out;
}

std::optional<dg::PcieLoadReading> balance_pcie_from_metrics(const std::map<std::string, double>& metrics) {
    // Serit sayisi 1..32, hiz nesli 1..8: bunun disindaki (elle degistirilmis) deger okunmamis sayilir.
    const auto u = [&metrics](const char* key, double max) -> std::optional<std::uint32_t> {
        const auto it = metrics.find(key);
        if (it == metrics.end() || !std::isfinite(it->second) || it->second < 1.0 || it->second > max || it->second != std::floor(it->second))
            return std::nullopt;
        return static_cast<std::uint32_t>(it->second);
    };
    dg::PcieLoadReading r;
    r.cur_width = u("balance.pcie_width", 32.0);
    r.max_width = u("balance.pcie_width_max", 32.0);
    r.cur_gen = u("balance.pcie_gen", 8.0);
    r.max_gen = u("balance.pcie_gen_max", 8.0);
    r.slot_max_width = u("balance.pcie_slot_width_max", 32.0);
    r.slot_max_gen = u("balance.pcie_slot_gen_max", 8.0);
    if (const auto it = metrics.find("balance.pcie_load_pct"); it != metrics.end() && std::isfinite(it->second) && it->second >= 0.0 && it->second <= 100.0)
        r.gpu_load_pct = it->second;
    if (!r.cur_width && !r.cur_gen) return std::nullopt;
    return r;
}

BalancePcieRecord balance_pcie_record(const insights::HistoryEntry& e) {
    BalancePcieRecord rec;
    rec.reading = balance_pcie_from_metrics(e.metrics);
    if (const auto it = e.conditions.find("gpu"); it != e.conditions.end()) rec.gpu = it->second;
    return rec;
}

std::optional<dg::PcieLoadReading> balance_pcie_for_gpu(const BalancePcieRecord& rec, std::string_view current_gpu) {
    if (!rec.reading) return std::nullopt;
    if (rec.gpu.empty()) return rec.reading;
    if (current_gpu.empty() || rec.gpu != current_gpu.substr(0, kConditionCompareBytes)) return std::nullopt;
    return rec.reading;
}

bool balance_run_valid(const dg::BalanceResult& r) noexcept { return r.cpu_fps.has_value() && r.global_reasons.empty(); }

std::optional<BalanceView> balance_view_from_history(const insights::HistoryEntry& e) {
    if (e.source != kBalanceSource || e.protocol_version != insights::kBalanceProtocolVersion) return std::nullopt;
    BalanceView v;
    bool any_verdict = false;
    for (std::size_t i = 0; i < dg::kBalanceRows; ++i) {
        const std::string key(dg::kBalanceRowKeys[i]);
        auto& row = v.rows[i];
        if (const auto code = metric(e, "balance.verdict_" + key))
            if (const auto verdict = dg::verdict_from_code(*code)) {
                row.verdict = *verdict;
                any_verdict = true;
            }
        row.gpu_fps = fps_metric(e, "balance.gpu_fps_" + key);
        row.gpu_lower_bound = row.gpu_fps && metric(e, "balance.gpu_lower_bound_" + key).value_or(0.0) != 0.0;
        row.coupled_fps = fps_metric(e, "balance.coupled_fps_" + key);
    }
    if (!any_verdict) return std::nullopt;  // yarida kesilmis kosu: hukum kodu yok
    v.cpu_fps = fps_metric(e, "balance.cpu_fps");
    v.cpu_lower_bound = v.cpu_fps && metric(e, "balance.cpu_lower_bound").value_or(0.0) != 0.0;
    v.cpu_sim_share = share_metric(e, "balance.cpu_sim_share");
    v.cpu_draw_share = share_metric(e, "balance.cpu_draw_share");
    if (!v.cpu_sim_share || !v.cpu_draw_share) v.cpu_sim_share = v.cpu_draw_share = std::nullopt;
    for (const auto& x : e.exclusions) {
        const std::string_view s(x);
        if (s.starts_with(kGlobalPrefix)) {
            auto reason = known_reason(s.substr(kGlobalPrefix.size()));
            if (std::find(v.global_reasons.begin(), v.global_reasons.end(), reason) == v.global_reasons.end())
                v.global_reasons.push_back(std::move(reason));
        } else if (s.starts_with(kDetailCpuStepPrefix)) {
            if (v.cpu_step_reason.empty()) v.cpu_step_reason = known_reason(s.substr(kDetailCpuStepPrefix.size()));
        } else if (s.starts_with(kDetailCpuRepeatPrefix)) {
            if (v.cpu_repeat_reason.empty()) v.cpu_repeat_reason = known_reason(s.substr(kDetailCpuRepeatPrefix.size()));
        } else if (s.starts_with(kRowPrefix)) {
            const auto rest = s.substr(kRowPrefix.size());
            const auto dot = rest.find('.');
            if (dot == std::string_view::npos) continue;
            for (std::size_t i = 0; i < dg::kBalanceRows; ++i)
                if (rest.substr(0, dot) == dg::kBalanceRowKeys[i] && v.rows[i].reason.empty()) v.rows[i].reason = known_reason(rest.substr(dot + 1));
        } else if (s.starts_with(kNotePrefix)) {
            const auto code = s.substr(kNotePrefix.size());
            if (!balance_note_key(code).empty() && std::find(v.notes.begin(), v.notes.end(), code) == v.notes.end()) v.notes.emplace_back(code);
        }
    }
    // Genel neden varsa butun satirlar o nedenle cekimserdir (kural boyle uretir; kayit elle degismis olsa da
    // hukum gosterilmez). Nedeni olmayan cekimser satir "unknown" nedenini alir; hukmu olan satir neden tasimaz.
    for (auto& row : v.rows) {
        if (!v.global_reasons.empty()) {
            row.verdict = dg::BalanceVerdict::Undetermined;
            row.reason = v.global_reasons.front();
        } else if (!needs_reason(row.verdict)) {
            row.reason.clear();
        } else if (row.reason.empty()) {
            row.reason = std::string(kBalanceUnknownReason);
        }
    }
    if (const auto pcie = balance_pcie_from_metrics(e.metrics); pcie && dg::evaluate_pcie_under_load(*pcie).below_max())
        v.pcie_link = dg::pcie_link_text(*pcie);
    if (const auto it = e.conditions.find("adapter"); it != e.conditions.end()) v.adapter = it->second;
    if (const auto it = e.conditions.find("mode"); it != e.conditions.end()) v.quick = it->second != "full";
    if (const auto it = e.conditions.find("preview"); it != e.conditions.end()) v.preview = it->second == "on";
    v.when_unix_ms = e.when_unix_ms;
    return v;
}

std::string_view balance_verdict_key(dg::BalanceVerdict v) noexcept {
    switch (v) {
        case dg::BalanceVerdict::CpuLimited: return "balance.verdict.cpu";
        case dg::BalanceVerdict::Balanced: return "balance.verdict.balanced";
        case dg::BalanceVerdict::GpuLimited: return "balance.verdict.gpu";
        case dg::BalanceVerdict::Unmeasurable: return "balance.verdict.unmeasurable";
        case dg::BalanceVerdict::Undetermined: break;
    }
    return "balance.verdict.undetermined";
}

std::string_view balance_short_key(dg::BalanceVerdict v) noexcept {
    switch (v) {
        case dg::BalanceVerdict::CpuLimited: return "balance.short.cpu";
        case dg::BalanceVerdict::Balanced: return "balance.short.balanced";
        case dg::BalanceVerdict::GpuLimited: return "balance.short.gpu";
        case dg::BalanceVerdict::Unmeasurable: return "balance.short.unmeasurable";
        case dg::BalanceVerdict::Undetermined: break;
    }
    return "balance.short.undetermined";
}

std::string_view balance_row_key(std::size_t row) noexcept {
    static constexpr std::array<std::string_view, dg::kBalanceRows> keys = {"balance.card.row_1080", "balance.card.row_1440",
                                                                           "balance.card.row_2160"};
    return row < keys.size() ? keys[row] : std::string_view{};
}

std::string balance_reason_key(std::string_view code) { return "balance.reason." + known_reason(code); }

std::string balance_note_key(std::string_view code) {
    if (!code_chars(code)) return {};
    std::string key = "balance.note." + std::string(code);
    return i18n::id_of(key).has_value() ? key : std::string();
}

std::string balance_fail_reason(std::string_view runner_error) {
    if (runner_error.empty()) return "step_failed";
    if (runner_error == "timeout") return "step_timeout";
    return std::string(runner_error);
}

std::string_view balance_stage_key(std::uint32_t step_code) noexcept {
    return step_code >= 1 && step_code <= kStageKeys.size() ? kStageKeys[step_code - 1] : std::string_view{};
}

std::optional<int> balance_stage_index(std::string_view stage_key) noexcept {
    for (std::size_t i = 0; i < kStageKeys.size(); ++i)
        if (kStageKeys[i] == stage_key) return static_cast<int>(i) + 1;
    return std::nullopt;
}

}  // namespace ef::core::measure
