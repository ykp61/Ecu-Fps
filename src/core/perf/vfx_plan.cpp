#include "core/perf/vfx_plan.h"

namespace ef::core::perf {

namespace cw = adapters::windows;

std::vector<PlanItem> vfx_plan_items(cw::VisualEffectsApi& api) {
    std::vector<PlanItem> out;
    for (auto e : {cw::VisualEffect::ClientAreaAnimation, cw::VisualEffect::MenuAnimation, cw::VisualEffect::ComboBoxAnimation,
                   cw::VisualEffect::ListBoxSmoothScrolling, cw::VisualEffect::CursorShadow, cw::VisualEffect::DropShadow,
                   cw::VisualEffect::Transparency}) {
        const auto cur = api.get(e);
        if (!cur.ok() || !cur.value()) continue;  // okunamayan veya zaten kapali: plana girmez
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::VisualEffectsSet, contracts::OperationScope::CurrentUser,
                                           "vfx:" + std::string(cw::to_string(e)), "0", false, false};
        i.title_key = "plan.vfx";
        i.group_key = "plan.vfx";
        out.push_back(std::move(i));
    }
    return out;
}

}  // namespace ef::core::perf
