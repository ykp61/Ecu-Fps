#include "core/perf/gpu_route_plan.h"

#include "core/adapters/windows/gpu_preference.h"

namespace ef::core::perf {

std::vector<PlanItem> gpu_route_plan_items(const readiness::GpuPrefInfo& info, const std::function<bool(std::string_view)>& writable) {
    using readiness::GpuPrefKind;
    std::vector<PlanItem> out;
    if (!info.laptop.value_or(false) || !info.hybrid.value_or(false)) return out;
    for (const auto& g : info.games) {
        if (out.size() >= kGpuRouteMaxItems) break;
        if (g.pref != GpuPrefKind::SystemDefault && g.pref != GpuPrefKind::PowerSaving) continue;
        if (g.exe.empty()) continue;
        if (writable && !writable(g.exe)) continue;
        const auto target = adapters::windows::gpu_pref_target_for_exe(g.exe);
        if (target.empty()) continue;  // yol bicimi gecersiz: hedef kurulmaz
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::GpuPreferenceSet, contracts::OperationScope::CurrentUser, target, "2",
                                           false, false};
        i.title_key = kGpuRouteGroup;
        i.group_key = kGpuRouteGroup;
        FieldDetail d;
        d.name = g.game;
        i.detail = std::move(d);
        out.push_back(std::move(i));
    }
    return out;
}

}  // namespace ef::core::perf
