#include "core/perf/update_hold_plan.h"

#include "core/perf/tweak_plan.h"

namespace ef::core::perf {

namespace aw = adapters::windows;

std::vector<PlanItem> update_hold_plan_items(const aw::UpdateStatus& status, aw::TweakAdapter& adapter, bool own_values_active) {
    // Okunamayan politika durumu: kurumsal olabilir, dokunma (uydurma yok).
    if (!status.managed_by_policy.has()) return {};
    if (*status.managed_by_policy.value && !own_values_active) return {};  // kurum politikasiyla savasilmaz
    std::vector<PlanItem> out = tweak_plan_items(kUpdateHoldGroup, adapter);
    for (auto& i : tweak_plan_items(kUpdateOffGroup, adapter)) out.push_back(std::move(i));
    return out;
}

bool update_hold_own_active(std::span<const contracts::SettingOperation> active_ops) {
    for (const auto& op : active_ops) {
        if (op.id != contracts::OperationId::TweakValueSet) continue;
        const auto* e = aw::find_tweak_target(op.target);
        if (e && (e->group_key == kUpdateHoldGroup || e->group_key == kUpdateOffGroup)) return true;
    }
    return false;
}

}  // namespace ef::core::perf
