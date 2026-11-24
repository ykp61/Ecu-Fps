#include "core/perf/background_plan.h"

#include "core/adapters/windows/background_process.h"

namespace ef::core::perf {

std::vector<PlanItem> background_plan_items(const std::vector<BgCandidate>& candidates) {
    namespace cw = adapters::windows;
    std::vector<PlanItem> out;
    for (const auto& c : candidates) {
        FieldDetail d;
        d.name = c.display_name;
        d.cpu_pct = c.cpu_pct;
        d.ram_bytes = c.working_set_bytes;
        for (const auto& p : c.processes) {
            if (out.size() >= kBackgroundMaxProcesses) return out;
            if (p.pid == 0 || p.creation == 0 || p.path.empty()) continue;  // kimlik eksikse hedef kurulmaz
            PlanItem i;
            i.op = contracts::SettingOperation{contracts::OperationId::BackgroundProcessLower, contracts::OperationScope::Process,
                                               cw::format_bg_process_target({{p.pid, p.creation}, cw::bg_path_hash(p.path)}), "low",
                                               false, false};
            i.title_key = kBackgroundGroup;
            i.group_key = kBackgroundGroup;
            i.detail = d;
            out.push_back(std::move(i));
        }
    }
    return out;
}

}  // namespace ef::core::perf
