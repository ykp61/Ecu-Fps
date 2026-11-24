#pragma once
// WIN-05: yazilmayan Windows ozelliklerinin belgeli yollarla durum okumasi + rehberli eylem acma.
//  - Oyun Modu: HKCU\Software\Microsoft\GameBar\AutoGameModeEnabled (Microsoft Learn: Windows 11 settings reference)
//  - HAGS: D3DKMTQueryAdapterInfo(KMTQAITYPE_WDDM_2_7_CAPS) — HwSchSupported/HwSchEnabled (WDK, d3dkmdt.h)

#include <string>
#include <vector>

#include "contracts/inventory.h"
#include "core/guided/guided.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

struct HagsStatus {
    std::string luid;  // "hi:lo"
    contracts::Field<bool> supported, enabled, enabled_by_default;
};

contracts::Field<bool> read_game_mode();
std::vector<HagsStatus> read_hags();
core::json::Value windows_features_json();

// Yalniz izinli ms-settings URI'lerini acar (ShellExecute). UI kullanir.
bool open_guided(core::guided::Action a);

}  // namespace ef::platform::inventory
