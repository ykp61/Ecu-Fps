#include "core/perf/boot_plan.h"

namespace ef::core::perf {

namespace cw = adapters::windows;

contracts::SettingOperation boot_tick_off_operation() {
    return contracts::SettingOperation{contracts::OperationId::BootOptionSet, contracts::OperationScope::Machine,
                                       std::string(cw::kBootDynamicTickTarget), std::string(cw::kBootDynamicTickOff),
                                       true, false};
}

std::vector<PlanItem> boot_plan_items(cw::BootOptionApi& api, const BootPlanInputs& in, std::vector<PlanOmission>* omitted,
                                      bool item_enabled) {
    const auto skip = [omitted](OmissionKind kind, const char* key) {
        if (omitted) omitted->push_back(PlanOmission{std::string(kBootTickGroup), kind, key});
        return std::vector<PlanItem>{};
    };
    using K = OmissionKind;
    // ADR-040 m.4: VM kaniti gelene kadar oge yok. Sira: once girdilerle kesinlesen kapilar, en son BCD okumasi.
    if (!item_enabled) return skip(K::Unsupported, "plan.boot_tick.gate.not_verified");
    if (!in.on_battery_device) return skip(K::Insufficient, "plan.boot_tick.gate.power_unread");
    if (*in.on_battery_device) return skip(K::Unsupported, "plan.boot_tick.gate.battery");
    if (!in.bitlocker_system_on) return skip(K::Insufficient, "plan.boot_tick.gate.bitlocker_unread");
    if (*in.bitlocker_system_on) return skip(K::Unsupported, "plan.boot_tick.gate.bitlocker_on");
    if (!in.bcd_readable) return skip(K::Insufficient, "plan.boot_tick.gate.bcd_unread");
    const auto cur = api.read(cw::BootElement::DisableDynamicTick);
    if (!cur.ok() || (cur.value() && !cw::is_boot_option_value(*cur.value()))) return skip(K::Insufficient, "plan.boot_tick.gate.bcd_unread");
    if (cur.value() && *cur.value() == cw::kBootDynamicTickOff) return {};  // zaten kapali: degisiklik yok
    PlanItem i;
    i.op = boot_tick_off_operation();
    i.title_key = std::string(kBootTickGroup);
    i.group_key = std::string(kBootTickGroup);
    i.needs_admin = true;
    std::vector<PlanItem> out;
    out.push_back(std::move(i));
    return out;
}

}  // namespace ef::core::perf
