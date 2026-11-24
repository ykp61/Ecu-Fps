#pragma once
// INV-05: guc ve guvenlik durumu tanisi (salt okuma). Hicbir ayar degistirilmez.
// Urun kurali X05: VBS/HVCI/Defender kapatma onerilmez; yalniz durum ve etkisi aciklanir.
// ADR-035/T11: Defender KAPATMA hala yok; tek istisna secmeli oyun klasoru tarama dislamasi (plan.defender, Gelismis, Evet/Hayir).

#include <string>
#include <string_view>
#include <vector>

#include "contracts/inventory.h"

namespace ef::core::inventory {

enum class PowerOverlay { BestEfficiency, Balanced, BetterPerformance, BestPerformance, Other };
std::string_view to_string(PowerOverlay o) noexcept;
PowerOverlay overlay_from_guid(std::string_view guid) noexcept;  // kucuk harf GUID

enum class PowerPlan { Balanced, HighPerformance, PowerSaver, Ultimate, Custom };
std::string_view to_string(PowerPlan p) noexcept;
PowerPlan plan_from_guid(std::string_view guid) noexcept;

enum class VbsState { Off, EnabledNotRunning, Running };
std::string_view to_string(VbsState s) noexcept;

struct PowerSecurity {
    contracts::Field<bool> on_ac, has_battery, battery_saver;
    contracts::Field<int> battery_percent;
    contracts::Field<std::string> active_plan_guid, active_plan_name;
    contracts::Field<std::string> overlay_guid;
    // OEM performans modu icin standart API yok: daima gerekceli Unsupported/unknown.
    contracts::Field<std::string> oem_mode;
    contracts::Field<std::string> vbs;   // VbsState metni
    contracts::Field<bool> hvci_running; // Memory Integrity
    contracts::Field<bool> credential_guard_running;
};

// Tanilama notlari (UI metni katalogdan anahtar ile gelir).
enum class PowerNote {
    OnBattery,              // pilde: performans sinirli olabilir
    BatterySaverOn,
    EfficiencyOverlay,      // guc modu "en iyi verimlilik"
    PowerSaverPlan,
    HvciRunning,            // Memory Integrity acik: bilgi, kapatma onerisi yok
    VbsRunning,
    BatteryStatusUnknown,
};
std::string_view note_key(PowerNote n) noexcept;  // i18n anahtari

std::vector<PowerNote> power_security_notes(const PowerSecurity& s);

}  // namespace ef::core::inventory
