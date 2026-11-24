#include "core/perf/drift.h"

#include "core/adapters/nvidia/drs.h"
#include "core/adapters/windows/power_subs.h"

namespace ef::core::perf {

using contracts::OperationId;
using contracts::OperationScope;

bool drift_ignored(const contracts::SettingOperation& op) noexcept {
    if (op.scope == OperationScope::Process) return true;
    if (op.needs_reboot) return true;  // yeniden baslatma bekleyen ayar (HAGS): okunan deger henuz etkin degil
    switch (op.id) {
        case OperationId::ServiceSessionStop:
        case OperationId::UpdatePauseSession:
        case OperationId::BackgroundProcessLower:
        case OperationId::RestorePointCreate:
        case OperationId::MaintenanceJob:
            return true;  // oturum/surec sonunda kendiliginden doner veya ayar degildir
        default: return false;
    }
}

std::string drift_group_key(const contracts::SettingOperation& op) {
    switch (op.id) {
        case OperationId::PowerModeSet: return "plan.power_mode";
        case OperationId::PowerSchemeSet: return "plan.power_scheme";
        case OperationId::VisualEffectsSet: return "plan.vfx";
        case OperationId::GamingToggleSet:
            return op.target == "gaming:windowed_opt" || op.target == "gaming:windows_vrr" ? "plan.windowed" : "plan.gaming";
        case OperationId::PowerSettingSet:
            if (op.target == adapters::windows::kPowerAspmAcTarget) return "plan.power_pcie";
            if (op.target == adapters::windows::kPowerUsbSelectiveSuspendAcTarget) return "plan.power_usb";
            return "plan.power_cpu";
        case OperationId::HagsSet: return "plan.hags";
        case OperationId::VendorProfileValue: {
            // G13/G14 (ADR-040 m.8) kendi grubunda; G01/G02/G05/G06 mevcut "plan.nvidia" grubunda kalir.
            const auto t = adapters::nvidia::parse_drs_target(op.target);
            if (t.ok() && (t.value().setting == static_cast<std::uint32_t>(adapters::nvidia::DrsSetting::ThreadedOptimization) ||
                           t.value().setting == static_cast<std::uint32_t>(adapters::nvidia::DrsSetting::FxaaEnable)))
                return "plan.nvidia_extra";
            return "plan.nvidia";
        }
        default: return {};
    }
}

std::vector<DriftItem> find_drift(const std::vector<ActiveStep>& steps, const DriftReadFn& read_back) {
    std::vector<DriftItem> out;
    if (!read_back) return out;
    for (const auto& s : steps) {
        if (drift_ignored(s.op) || s.group_key.empty()) continue;
        const auto cur = read_back(s.op);
        if (!cur.ok()) continue;  // okunamayan adim sapmis SAYILMAZ (bilinmeyen != degisti)
        const auto& v = cur.value();
        const bool ours = v && (*v == s.op.new_value || (!s.written.empty() && *v == s.written));
        if (ours) continue;
        out.push_back({s.op, s.group_key, v});
    }
    return out;
}

}  // namespace ef::core::perf
