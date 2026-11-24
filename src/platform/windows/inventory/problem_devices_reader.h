#pragma once
// T17: sorunlu aygit listesi (salt okuma). SetupAPI ile mevcut tum aygitlar taranir, CM_Get_DevNode_Status ile
// DN_HAS_PROBLEM olanlar toplanir. Surucu kurulmaz/kaldirilmaz, hicbir ayar yazilmaz; yonetici gerekmez.
// Liste alinamazsa enumerated=false (sorun yok denmez); en cok kMaxProblemDevices oge.

#include "core/inventory/problem_devices.h"

namespace ef::platform::inventory {

core::inventory::ProblemDeviceReport read_problem_devices();

}  // namespace ef::platform::inventory
