#pragma once
// INV-05 platform okuyucu: guc plani, guc modu overlay, pil, VBS/HVCI (salt okuma).

#include <optional>
#include <string>

#include "core/inventory/power_security.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

core::inventory::PowerSecurity read_power_security();
// Etkin guc modu overlay GUID'i (kucuk harf; PowerGetEffectiveOverlayScheme, Windows 10 1809+). Okunamazsa nullopt.
std::optional<std::string> read_effective_overlay_guid();
core::json::Value to_json(const core::inventory::PowerSecurity& v);

}  // namespace ef::platform::inventory
