#pragma once
// WIN-01 platform: gercek Windows guc API'leri (powrprof). Windows 11 guc modu fonksiyonlari calisma
// aninda yuklenir; Windows 10'da kontrollu Unsupported.

#include "core/adapters/windows/power.h"

namespace ef::platform::adapters {

class WinPowerApi final : public core::adapters::windows::PowerApi {
public:
    contracts::Result<std::string> get_ac_mode() override;
    contracts::Status set_ac_mode(const std::string& guid) override;
    contracts::Result<std::string> get_dc_mode() override;
    contracts::Status set_dc_mode(const std::string& guid) override;
    contracts::Result<bool> has_battery() override;
    contracts::Result<std::string> get_active_scheme() override;
    contracts::Status set_active_scheme(const std::string& guid) override;
    contracts::Result<std::vector<std::string>> list_schemes() override;
};

}  // namespace ef::platform::adapters
