#pragma once
// F-50: Hyper-V/WHP/WSL2/Sanal Makine Platformu ISARETLERINI salt okuma (hizmet + dosya + CPUID).
// Istege bagli ozelligin kesin durumu bu API'lerle kanitlanmaz; hicbir ayar/ozellik yazilmaz.

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

core::readiness::VirtualizationInfo read_virtualization();

}  // namespace ef::platform::readiness
