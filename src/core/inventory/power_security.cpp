#include "core/inventory/power_security.h"

namespace ef::core::inventory {

std::string_view to_string(PowerOverlay o) noexcept {
    switch (o) {
        case PowerOverlay::BestEfficiency: return "best_efficiency";
        case PowerOverlay::Balanced: return "balanced";
        case PowerOverlay::BetterPerformance: return "better_performance";
        case PowerOverlay::BestPerformance: return "best_performance";
        case PowerOverlay::Other: return "other";
    }
    return "?";
}

PowerOverlay overlay_from_guid(std::string_view g) noexcept {
    if (g == "961cc777-2547-4f9d-8174-7d86181b8a7a") return PowerOverlay::BestEfficiency;
    if (g == "00000000-0000-0000-0000-000000000000") return PowerOverlay::Balanced;
    if (g == "3af9b8d9-7c97-431d-ad78-34a8bfea439f") return PowerOverlay::BetterPerformance;
    if (g == "ded574b5-45a0-4f42-8737-46345c09c238") return PowerOverlay::BestPerformance;
    return PowerOverlay::Other;
}

std::string_view to_string(PowerPlan p) noexcept {
    switch (p) {
        case PowerPlan::Balanced: return "balanced";
        case PowerPlan::HighPerformance: return "high_performance";
        case PowerPlan::PowerSaver: return "power_saver";
        case PowerPlan::Ultimate: return "ultimate";
        case PowerPlan::Custom: return "custom";
    }
    return "?";
}

PowerPlan plan_from_guid(std::string_view g) noexcept {
    if (g == "381b4222-f694-41f0-9685-ff5bb260df2e") return PowerPlan::Balanced;
    if (g == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") return PowerPlan::HighPerformance;
    if (g == "a1841308-3541-4fab-bc81-f71556f20b4a") return PowerPlan::PowerSaver;
    if (g == "e9a42b02-d5df-448d-aa00-03f14749eb61") return PowerPlan::Ultimate;
    return PowerPlan::Custom;
}

std::string_view to_string(VbsState s) noexcept {
    switch (s) {
        case VbsState::Off: return "off";
        case VbsState::EnabledNotRunning: return "enabled_not_running";
        case VbsState::Running: return "running";
    }
    return "?";
}

std::string_view note_key(PowerNote n) noexcept {
    switch (n) {
        case PowerNote::OnBattery: return "diag.on_battery";
        case PowerNote::BatterySaverOn: return "diag.battery_saver";
        case PowerNote::EfficiencyOverlay: return "diag.efficiency_overlay";
        case PowerNote::PowerSaverPlan: return "diag.power_saver_plan";
        case PowerNote::HvciRunning: return "diag.hvci_running";
        case PowerNote::VbsRunning: return "diag.vbs_running";
        case PowerNote::BatteryStatusUnknown: return "diag.battery_unknown";
    }
    return "?";
}

std::vector<PowerNote> power_security_notes(const PowerSecurity& s) {
    std::vector<PowerNote> n;
    if (s.has_battery.has() && *s.has_battery.value) {
        if (!s.on_ac.has()) n.push_back(PowerNote::BatteryStatusUnknown);
        else if (!*s.on_ac.value) n.push_back(PowerNote::OnBattery);
    }
    if (s.battery_saver.has() && *s.battery_saver.value) n.push_back(PowerNote::BatterySaverOn);
    if (s.overlay_guid.has() && overlay_from_guid(*s.overlay_guid.value) == PowerOverlay::BestEfficiency)
        n.push_back(PowerNote::EfficiencyOverlay);
    if (s.active_plan_guid.has() && plan_from_guid(*s.active_plan_guid.value) == PowerPlan::PowerSaver)
        n.push_back(PowerNote::PowerSaverPlan);
    if (s.hvci_running.has() && *s.hvci_running.value) n.push_back(PowerNote::HvciRunning);
    else if (s.vbs.has() && *s.vbs.value == "running") n.push_back(PowerNote::VbsRunning);
    return n;
}

}  // namespace ef::core::inventory
