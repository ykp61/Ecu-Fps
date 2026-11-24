#pragma once
// F-48 (windows_write): HiberbootEnabled (REG_DWORD). Okuma normal kullanicida calisir; yazma/silme yalniz
// yukseltilmis broker'da basarili olur. Baska deger/anahtar dokunulmaz.

#include "core/adapters/windows/fast_startup.h"

namespace ef::platform::adapters {

class WinFastStartupApi final : public core::adapters::windows::FastStartupApi {
public:
    contracts::Result<std::optional<std::uint32_t>> read_pref() override;
    contracts::Status write_pref(std::uint32_t v) override;
    contracts::Status remove_pref() override;
};

}  // namespace ef::platform::adapters
