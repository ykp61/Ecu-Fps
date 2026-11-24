#include "core/perf/debloat_plan.h"

#include <algorithm>
#include <cctype>

#include "core/perf/tweak_plan.h"

namespace ef::core::perf {

namespace aw = adapters::windows;

namespace {
std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
bool has(std::string_view hay, std::string_view needle) { return lower(hay).find(needle) != std::string::npos; }
}  // namespace

bool other_browser_installed(const BrowserInfo& b) {
    if (!b.read_ok) return false;
    for (const auto& c : b.clients) {
        if (c.empty() || has(c, "edge") || has(c, "iexplore") || has(c, "internet explorer")) continue;
        return true;
    }
    return false;
}

bool default_browser_is_not_edge(const BrowserInfo& b) {
    if (!b.read_ok || b.default_progid.empty()) return false;  // bilinmiyor: Edge grubu sunulmaz
    return !has(b.default_progid, "msedge") && !has(b.default_progid, "edge");
}

bool edge_debloat_offered(const BrowserInfo& b) { return other_browser_installed(b) && default_browser_is_not_edge(b); }

std::vector<PlanItem> debloat_plan_items(aw::AppxApi& appx, const BrowserInfo& browsers, aw::TweakAdapter& tweak) {
    std::vector<PlanItem> out;
    if (edge_debloat_offered(browsers))
        for (auto& i : tweak_plan_items(kDebloatEdgeGroup, tweak)) out.push_back(std::move(i));
    for (auto& i : tweak_plan_items(kDebloatOneDriveGroup, tweak)) out.push_back(std::move(i));
    for (const auto& p : aw::appx_packages()) {
        const std::string family(p.family);
        const auto inst = appx.installed_full_name(family);
        if (!inst.ok() || !inst.value()) continue;  // okunamadi / kurulu degil: oge yok
        const auto rest = appx.restorable(family);
        if (!rest.ok() || !rest.value()) continue;  // geri alinabilirligi dogrulanamayan paket plana girmez (ADR-035/6)
        const auto target = aw::appx_target_for_family(family);
        if (target.empty()) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::AppxPackageRemove, contracts::OperationScope::CurrentUser, target,
                                           std::string(aw::kAppxRemovedValue), false, false};
        i.title_key = std::string(kDebloatAppsGroup);
        i.group_key = std::string(kDebloatAppsGroup);
        i.needs_admin = false;
        FieldDetail d;
        d.name = std::string(p.display_name);
        i.detail = std::move(d);
        out.push_back(std::move(i));
    }
    return out;
}

}  // namespace ef::core::perf
