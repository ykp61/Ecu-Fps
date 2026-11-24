#pragma once
// F-52: sistem tweak degerlerinin salt-okuma denetimi (kayit defteri). Yazma yok.

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

core::readiness::SystemTweaksInfo read_system_tweaks();

}  // namespace ef::platform::readiness
