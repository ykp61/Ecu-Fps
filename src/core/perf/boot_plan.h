#pragma once
// ADR-040 m.4 (B0 iskeleti, B4 doldurdu): "plan.boot_tick" grubu. Tek oge: dinamik tick kapali (BootOptionSet,
// "boot:dynamictick" = "off", needs_reboot, needs_admin). Kapilar (hepsi plana GIRMEME + PlanOmission gerekcesi):
//  - item_enabled = false (varsayilan kBootOptionItemEnabled; bu surumde her zaman) -> Unsupported "gate.not_verified"
//  - pilli cihaz -> Unsupported "gate.battery"; guc kaynagi okunamadi -> Insufficient "gate.power_unread"
//  - sistem surucusunde BitLocker acik -> Unsupported "gate.bitlocker_on"; okunamadi -> Insufficient "gate.bitlocker_unread"
//  - BCD okunamiyor (girdi ya da adaptor okumasi ya da tanimsiz deger) -> Insufficient "gate.bcd_unread"
//  - mevcut deger zaten "off" -> oge yok (degisiklik yok, gerekce de yok)
// Anahtarlar "plan.boot_tick.gate.*" (katalogda TR/EN). Bu dosya saftir; okumayi cagiran yapar.

#include <optional>
#include <vector>

#include "core/adapters/windows/boot_options.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr std::string_view kBootTickGroup = "plan.boot_tick";

struct BootPlanInputs {
    bool bcd_readable = false;                 // BCD deposu okunabildi mi (okunamazsa oge yok)
    std::optional<bool> bitlocker_system_on;   // sistem surucusunde BitLocker; nullopt = okunamadi (oge yok)
    std::optional<bool> on_battery_device;     // pilli cihaz mi; nullopt = okunamadi (oge yok)
};

// Plan ogesinin tipli islemi (needs_reboot = true; onay ekrani yeniden baslatma uyarisini buradan alir).
contracts::SettingOperation boot_tick_off_operation();

std::vector<PlanItem> boot_plan_items(adapters::windows::BootOptionApi& api, const BootPlanInputs& in, std::vector<PlanOmission>* omitted,
                                      bool item_enabled = adapters::windows::kBootOptionItemEnabled);

}  // namespace ef::core::perf
