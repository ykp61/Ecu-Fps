#pragma once
// Spec 2026-11-12 H (tespit; ADR-031): Windows Update Agent ile yukseltilmemis, salt okuma surucu aramasi.
// IUpdateSession -> IUpdateSearcher::BeginSearch(core::drivers::kWuaDriverCriteria); is parcacigi stop() ile
// RequestAbort eder. Indirme / kurulum arabirimleri bu klasorde YOKTUR (tests/windows/check_no_install.ps1 ve
// tests/unit/core/test_net_policy.cpp). Baglantiyi Windows Update hizmeti kurar; uygulamanin kendi ag listesi degismez.
#include <memory>

#include "core/drivers/driver_updates.h"

namespace ef::platform::wua {

// Cagiran is parcaciginda COM'u (MTA) acar ve kapatir.
std::unique_ptr<core::drivers::DriverSearchSource> make_driver_search();

}  // namespace ef::platform::wua
