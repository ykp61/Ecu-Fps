#pragma once
// Gercek ortam parmak izi (OS build + GPU surucu surumleri + uygulama surumu).

#include "core/capabilities/registry.h"

namespace ef::platform::inventory {

core::capabilities::Environment current_environment();

}  // namespace ef::platform::inventory
