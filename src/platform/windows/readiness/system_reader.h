#pragma once
// F-65/F-67: sistem sagligi okuyucusu (salt okuma; yonetici gerekmez, pencere acmaz).
// Okunamayan alan nullopt kalir; tahmin uretilmez. Hicbir ayar yazilmaz.

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

core::readiness::SystemHealthInfo read_system_health();

}  // namespace ef::platform::readiness
