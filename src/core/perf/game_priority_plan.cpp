#include "core/perf/game_priority_plan.h"

#include <algorithm>

#include "core/games/protection_gate.h"

namespace ef::core::perf {

namespace aw = adapters::windows;

bool game_kernel_protected(const std::string& app_id, FaceitState faceit) {
    if (app_id.empty()) return false;
    for (const auto& e : games::builtin_matrix()) {
        if (e.game_id != app_id) continue;
        if (e.protection == "VAC / Trusted Mode") continue;  // kullanici modu
        if (e.protection == "FACEIT Anti-Cheat") {
            if (faceit != FaceitState::Absent) return true;  // kurulu veya bilinmiyor: fail-closed
            continue;
        }
        return true;  // EAC / BattlEye / Vanguard / Ricochet / bilinmeyen: cekirdek varsay
    }
    return false;
}

std::string game_priority_exe(const PriorityLibraryGame& g) {
    if (!g.exe_hint.empty()) {
        const auto cut = g.exe_hint.find_last_of("\\/");
        const auto base = cut == std::string::npos ? g.exe_hint : g.exe_hint.substr(cut + 1);
        if (const auto* p = aw::find_priority_game(base)) return std::string(p->exe);
    }
    if (g.launcher == "steam" && !g.app_id.empty())
        for (const auto& p : aw::priority_games())
            if (!p.steam_app_id.empty() && p.steam_app_id == g.app_id) return std::string(p.exe);
    return {};
}

std::vector<PlanItem> game_priority_plan_items(aw::IfeoPriorityAdapter& adapter, const GamePriorityInput& in) {
    std::vector<PlanItem> out;
    if (!in.scan_trusted) return out;
    std::vector<std::string> seen;
    for (const auto& g : in.games) {
        const auto exe = game_priority_exe(g);
        if (exe.empty()) continue;
        if (game_kernel_protected(g.app_id, in.faceit)) continue;
        if (std::find(seen.begin(), seen.end(), exe) != seen.end()) continue;
        const auto target = aw::ifeo_priority_target(exe);
        if (target.empty()) continue;
        const auto cur = adapter.read_value(exe);
        if (!cur.ok() || cur.value()) continue;  // okunamadi veya deger zaten var: ezilmez
        seen.push_back(exe);
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::GamePriorityHint, contracts::OperationScope::Machine, target,
                                           std::string(aw::kIfeoPriorityValue), false, false};
        i.title_key = kGamePriorityGroup;
        i.group_key = kGamePriorityGroup;
        i.needs_admin = true;
        FieldDetail d;
        d.name = g.name.empty() ? exe : g.name;
        i.detail = std::move(d);
        out.push_back(std::move(i));
    }
    return out;
}

}  // namespace ef::core::perf
