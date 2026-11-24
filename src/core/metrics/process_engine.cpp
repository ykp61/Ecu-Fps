#include "core/metrics/process_engine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "core/metrics/live.h"

namespace ef::core::metrics {

namespace {
char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool is_hex(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// "0x" + 1..8 onaltilik hane; sifirla 8 haneye tamamlanir. pos ilerler. Basarisizsa false.
bool read_hex32(std::string_view s, std::size_t& pos, std::string& out) {
    if (pos + 2 > s.size() || s[pos] != '0' || lower(s[pos + 1]) != 'x') return false;
    std::size_t i = pos + 2;
    std::string digits;
    while (i < s.size() && is_hex(s[i])) digits.push_back(lower(s[i++]));
    if (digits.empty() || digits.size() > 8) return false;
    out.assign(8 - digits.size(), '0');
    out += digits;
    pos = i;
    return true;
}
}  // namespace

std::string engine_instance_luid(std::string_view name) {
    constexpr std::string_view kTag = "luid_";
    std::size_t at = std::string_view::npos;
    for (std::size_t i = 0; i + kTag.size() <= name.size(); ++i) {
        bool eq = true;
        for (std::size_t k = 0; k < kTag.size(); ++k)
            if (lower(name[i + k]) != kTag[k]) {
                eq = false;
                break;
            }
        if (eq) {
            at = i + kTag.size();
            break;
        }
    }
    if (at == std::string_view::npos) return {};
    std::string hi, lo;
    if (!read_hex32(name, at, hi)) return {};
    if (at >= name.size() || name[at] != '_') return {};
    ++at;
    if (!read_hex32(name, at, lo)) return {};
    if (at < name.size() && name[at] != '_') return {};  // "0x1234abcdX" gibi bozuk ad
    return hi + ":" + lo;
}

bool engine_instance_is_3d(std::string_view name) noexcept {
    constexpr std::string_view kSuffix = "engtype_3d";
    if (name.size() < kSuffix.size()) return false;
    const auto tail = name.substr(name.size() - kSuffix.size());
    for (std::size_t i = 0; i < kSuffix.size(); ++i)
        if (lower(tail[i]) != kSuffix[i]) return false;
    return true;
}

ProcessEnginePick pick_process_3d_engine(const std::vector<EngineInstance>& instances) {
    std::map<std::string, double> by_luid;  // sirali: esitlikte kucuk LUID
    for (const auto& i : instances) {
        if (!engine_instance_is_3d(i.name) || !std::isfinite(i.pct) || i.pct < 0.0) continue;
        const std::string luid = engine_instance_luid(i.name);
        if (luid.empty()) continue;
        by_luid[luid] += i.pct;
    }
    ProcessEnginePick out;
    out.adapters = by_luid.size();
    for (const auto& [luid, pct] : by_luid)
        if (!out.pct || pct > *out.pct) {
            out.pct = pct;
            out.luid = luid;
        }
    return out;
}

std::string engine_instance_type(std::string_view name) {
    constexpr std::string_view kTag = "engtype_";
    for (std::size_t i = 0; i + kTag.size() <= name.size(); ++i) {
        bool eq = true;
        for (std::size_t k = 0; k < kTag.size(); ++k)
            if (lower(name[i + k]) != kTag[k]) {
                eq = false;
                break;
            }
        if (!eq) continue;
        std::string out;
        for (std::size_t k = i + kTag.size(); k < name.size(); ++k) out.push_back(lower(name[k]));
        return out;
    }
    return {};
}

ProcessGpuPick pick_process_gpu_engine(const std::vector<EngineInstance>& instances) {
    std::map<std::string, std::map<std::string, double>> by_luid;  // luid -> tur -> toplam (sirali: belirlenimci)
    for (const auto& i : instances) {
        if (!std::isfinite(i.pct) || i.pct < 0.0) continue;
        const std::string luid = engine_instance_luid(i.name);
        const std::string type = engine_instance_type(i.name);
        if (luid.empty() || type.empty()) continue;
        by_luid[luid][type] += i.pct;
    }
    ProcessGpuPick out;
    out.adapters = by_luid.size();
    for (const auto& [luid, types] : by_luid) {
        const auto top = std::max_element(types.begin(), types.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        if (top == types.end()) continue;
        if (!out.pct || top->second > *out.pct) {
            out.pct = top->second;
            out.luid = luid;
            out.engine_type = top->first;
            const auto d3 = types.find("3d");
            out.pct_3d = d3 == types.end() ? std::nullopt : std::optional<double>(d3->second);
        }
    }
    return out;
}

bool process_gpu_reexpand_due(ProcessGpuReexpand& s, std::int64_t now_ms, bool instance_seen) noexcept {
    if (!s.late_done && now_ms - s.opened_ms >= kProcessGpuLateReexpandMs) {
        s.late_done = true;
        s.last_expand_ms = now_ms;
        return true;
    }
    if (!instance_seen && now_ms - s.last_expand_ms >= kProcessGpuRetryMs) {
        s.last_expand_ms = now_ms;
        return true;
    }
    return false;
}

std::optional<double> other_process_3d_pct(const std::vector<EngineInstance>& instances, std::string_view luid, std::uint32_t own_pid,
                                           const std::vector<std::uint32_t>& exempt_pids) {
    if (luid.empty()) return std::nullopt;
    std::map<std::pair<std::uint32_t, std::uint32_t>, double> by_engine;  // (phys, eng) -> diger sureclerin toplami
    bool any_3d = false;
    for (const auto& i : instances) {
        if (!engine_instance_is_3d(i.name) || !std::isfinite(i.pct) || i.pct < 0.0) continue;
        const auto id = parse_gpu_engine_instance(i.name);
        const std::string inst_luid = engine_instance_luid(i.name);
        if (!id || inst_luid.empty()) continue;
        any_3d = true;
        if (inst_luid != luid || id->pid == own_pid) continue;
        if (std::find(exempt_pids.begin(), exempt_pids.end(), id->pid) != exempt_pids.end()) continue;
        by_engine[{id->phys, id->eng}] += i.pct;
    }
    if (!any_3d) return std::nullopt;
    double top = 0.0;
    for (const auto& [engine, pct] : by_engine) top = std::max(top, pct);
    return top;
}

}  // namespace ef::core::metrics
