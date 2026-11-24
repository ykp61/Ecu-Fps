#include "core/perf/feature_plan.h"

#include <cctype>
#include <string>

#include "core/adapters/windows/service_start_type.h"
#include "core/perf/tweak_plan.h"

namespace ef::core::perf {

namespace aw = adapters::windows;
namespace st = adapters::startup;

namespace {
std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

// Bu grubun hizmetlerinden biri ayni planda oturumluk durdurulacak mi.
bool session_stop_conflict(std::string_view group, std::span<const PlanItem> planned) {
    for (const auto& p : planned) {
        if (p.op.id != contracts::OperationId::ServiceSessionStop) continue;
        auto n = aw::parse_session_stop_target(p.op.target);
        if (!n.ok()) continue;
        const auto* f = aw::find_feature_service(n.value());
        if (f && f->group == group) return true;
    }
    return false;
}
}  // namespace

std::optional<bool> combine_usage(std::initializer_list<std::optional<bool>> parts) noexcept {
    bool unknown = false;
    for (const auto& p : parts) {
        if (!p) unknown = true;
        else if (*p) return true;
    }
    if (unknown) return std::nullopt;
    return false;
}

bool is_virtual_printer(std::string_view name, std::string_view port) noexcept {
    const std::string n = lower(name), p = lower(port);
    for (std::string_view k : {"microsoft print to pdf", "microsoft xps document writer", "onenote", "fax", "pdf", "xps"})
        if (has(n, k)) return true;
    if (p == "nul:" || p == "portprompt:" || p == "file:" || p.rfind("xpsport", 0) == 0 || p.rfind("microsoft.office.onenote", 0) == 0 ||
        p.rfind("shrfax", 0) == 0)
        return true;
    return false;
}

bool feature_group_offered(std::string_view group, const FeatureUsage& usage) noexcept {
    if (group == kFeatureBluetoothGroup) return usage.bluetooth_devices == std::optional<bool>(false);
    if (group == kFeaturePrintGroup) return usage.printers == std::optional<bool>(false);
    if (group == kFeatureXboxGroup) return usage.xbox_games == std::optional<bool>(false);
    return group == kFeatureSearchGroup || group == kFeatureStoreGroup;
}

std::vector<PlanItem> feature_plan_items(const FeatureUsage& usage, st::ServiceApi& services, aw::ServiceBusyProbe& probe, aw::TweakAdapter& tweak,
                                         std::span<const PlanItem> already_planned) {
    std::vector<PlanItem> out;
    for (const auto group : {kFeatureBluetoothGroup, kFeaturePrintGroup, kFeatureXboxGroup, kFeatureSearchGroup}) {
        if (!feature_group_offered(group, usage)) continue;
        if (session_stop_conflict(group, already_planned)) continue;
        for (const auto& f : aw::feature_services()) {
            if (f.group != group) continue;
            auto s = services.get(std::string(f.name));
            if (!s.ok()) continue;  // bu Windows'ta yok / okunamadi: sessizce atla (uydurma yok)
            if (s.value().start == st::StartType::Disabled) continue;  // zaten kapali
            if (!aw::check_feature_disable(services, probe, s.value(), false).ok()) continue;
            PlanItem i;
            i.op = contracts::SettingOperation{contracts::OperationId::ServiceStartTypeSet, contracts::OperationScope::Machine,
                                               aw::format_service_start_target(f.name), "disabled", false, false};
            i.title_key = std::string(group);
            i.group_key = std::string(group);
            i.needs_admin = true;
            i.field_key = "feature.svc." + lower(f.name);
            out.push_back(std::move(i));
        }
    }
    for (auto& i : tweak_plan_items(kFeatureStoreGroup, tweak)) out.push_back(std::move(i));
    return out;
}

}  // namespace ef::core::perf
