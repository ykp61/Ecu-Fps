#pragma once
// F-68: ekran sinifi aygitlarinda sorun kodu ve DriverStore'daki uretici surucu paketi sayisi
// (salt okuma; surucu kurma/kaldirma/yazma yok).

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

core::readiness::DisplayDriverHealthInfo read_display_driver_health();

}  // namespace ef::platform::readiness
