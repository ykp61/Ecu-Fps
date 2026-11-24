#include "core/perf/tweak_plan.h"

namespace ef::core::perf {

namespace aw = adapters::windows;

std::vector<PlanItem> tweak_plan_items_for(std::span<const aw::TweakEntry* const> entries, aw::TweakAdapter& adapter) {
    std::vector<PlanItem> out;
    const auto rt = adapter.runtime();  // hesaplanan hedefler (B3): okunamadiysa o girdi plana girmez
    for (const auto* e : entries) {
        if (!e) continue;
        const bool machine = e->hive == aw::TweakHive::Machine;
        if (machine && !adapter.machine_available()) continue;  // HKLM okunamiyorsa plana girmez
        const auto want = aw::tweak_on_raw(*e, rt);
        const auto want_text = aw::tweak_on_text(*e, rt);
        if (!want || !want_text) continue;
        const auto cur = adapter.read_raw(*e);
        if (!cur.ok()) continue;  // okunamayan girdi plana girmez
        if (cur.value() && *cur.value() == *want) continue;  // zaten hedef degerde
        if (aw::tweak_should_skip(*e, cur.value(), rt)) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::TweakValueSet,
                                           machine ? contracts::OperationScope::Machine : contracts::OperationScope::CurrentUser,
                                           aw::tweak_target(*e), *want_text, e->needs_reboot, false};
        i.title_key = std::string(e->group_key);
        i.group_key = std::string(e->group_key);
        i.needs_admin = machine;
        i.field_key = "tweak." + std::string(e->id);
        out.push_back(std::move(i));
    }
    return out;
}

std::vector<PlanItem> tweak_plan_items(std::string_view group, aw::TweakAdapter& adapter) {
    const auto entries = aw::tweaks_in_group(group);
    return tweak_plan_items_for(std::span<const aw::TweakEntry* const>(entries.data(), entries.size()), adapter);
}

}  // namespace ef::core::perf
