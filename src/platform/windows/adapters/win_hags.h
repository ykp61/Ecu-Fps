#pragma once
// OPT-10 platform: HAGS tercihi. Yalniz tek sabit deger: HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers
// HwSchMode (REG_DWORD). Okuma normal kullanicida calisir; yazma/silme yalniz yukseltilmis broker'da basarili olur.
// Destek: D3DKMT WDDM 2.7 caps (inventory::read_hags); en az bir donanim bagdastiricisi "destekli" ise true.
#include "core/adapters/windows/hags.h"

namespace ef::platform::adapters {

class WinHagsApi final : public core::adapters::windows::HagsApi {
public:
    contracts::Result<std::optional<std::uint32_t>> read_pref() override;
    contracts::Status write_pref(std::uint32_t v) override;
    contracts::Status remove_pref() override;
    std::optional<bool> supported() override;
};

}  // namespace ef::platform::adapters
