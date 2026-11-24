#include "core/perf/defender_plan.h"

#include <algorithm>

namespace ef::core::perf {

namespace aw = adapters::windows;

std::vector<PlanItem> defender_plan_items(aw::DefenderApi& api, const std::vector<DefenderFolder>& trusted_folders) {
    std::vector<PlanItem> out;
    if (api.usable() != std::optional<bool>(true)) return out;  // bilinmiyor/gecersiz: oge yok (uydurma yok)
    std::vector<std::string> roots;
    for (const auto& f : trusted_folders) roots.push_back(f.path);
    std::vector<std::string> already;  // okunabildiyse dislanmislar
    if (auto cur = api.exclusion_paths(); cur.ok())
        for (const auto& p : cur.value()) already.push_back(aw::defender_canonical_path(p));
    std::vector<std::string> seen;
    for (const auto& f : trusted_folders) {
        if (out.size() >= kDefenderMaxItems) break;
        if (!aw::defender_path_allowed(f.path, roots)) continue;
        const auto canon = aw::defender_canonical_path(f.path);
        if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
        seen.push_back(canon);
        if (std::find(already.begin(), already.end(), canon) != already.end()) continue;
        const auto target = aw::defender_target_for_path(f.path);
        if (target.empty()) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::DefenderExclusionSet, contracts::OperationScope::Machine, target, "1",
                                           false, false};
        i.title_key = kDefenderGroup;
        i.group_key = kDefenderGroup;
        i.needs_admin = true;
        FieldDetail d;
        d.name = f.name.empty() ? f.path : f.name;
        i.detail = std::move(d);
        out.push_back(std::move(i));
    }
    return out;
}

}  // namespace ef::core::perf
