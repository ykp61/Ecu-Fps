#pragma once
// ADR-035/T02: TweakAdapter icin HKLM (64-bit gorunum) kayit defteri erisimi. Yazma yalniz yukseltilmis broker'da cagrilir;
// bu sinif yol uretmez, TweakAdapter yalniz derlenmis katalogdaki yollari verir.
#include "core/adapters/windows/tweak_adapter.h"

namespace ef::platform::adapters {

class WinMachineRegistryApi final : public core::adapters::windows::MachineRegistryApi {
public:
    contracts::Result<std::optional<core::adapters::windows::RegRaw>> read(std::wstring_view subkey, std::wstring_view name) override;
    contracts::Status write(std::wstring_view subkey, std::wstring_view name, const core::adapters::windows::RegRaw& v) override;
    contracts::Status remove(std::wstring_view subkey, std::wstring_view name) override;
    contracts::Result<bool> key_exists(std::wstring_view subkey) override;
    contracts::Status remove_key_if_empty(std::wstring_view subkey) override;
};

}  // namespace ef::platform::adapters
