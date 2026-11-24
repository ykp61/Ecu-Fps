#pragma once
// OPT-09: GamingToggleAdapter icin HKCU kayit defteri erisimi (yalniz cagiran kullanicinin kovani).
#include "core/adapters/windows/gaming_toggles.h"

namespace ef::platform::adapters {

class WinUserRegistryApi final : public core::adapters::windows::UserRegistryApi {
public:
    contracts::Result<std::optional<core::adapters::windows::RegRaw>> read(std::wstring_view subkey, std::wstring_view name) override;
    contracts::Status write(std::wstring_view subkey, std::wstring_view name, const core::adapters::windows::RegRaw& v) override;
    contracts::Status remove(std::wstring_view subkey, std::wstring_view name) override;
};

}  // namespace ef::platform::adapters
