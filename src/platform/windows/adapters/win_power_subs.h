#pragma once
// F-32/F-33/F-42 platform: etkin guc planinin AC alt ayarlari (powrprof).
// Okuma normal kullanicida calisir; PowerWriteACValueIndex yonetici ister ve yalniz broker'da cagrilir.
// Yalniz ACSettingIndex okunur/yazilir; DCSettingIndex'e DOKUNULMAZ.

#include "core/adapters/windows/power_subs.h"

namespace ef::platform::adapters {

class WinPowerSubApi final : public core::adapters::windows::PowerSubApi {
public:
    contracts::Result<core::adapters::windows::PowerSubValue> read_active_ac(std::string_view target) override;
    contracts::Status write_active_ac(std::string_view target, std::uint32_t v) override;
    contracts::Status write_scheme_ac(const std::string& scheme_guid, std::string_view target, std::uint32_t v) override;
    contracts::Result<std::uint32_t> read_scheme_ac(const std::string& scheme_guid, std::string_view target) override;
};

}  // namespace ef::platform::adapters
