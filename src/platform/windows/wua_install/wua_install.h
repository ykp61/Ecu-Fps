#pragma once
// Spec 2026-11-12 H2 (ADR-031): yukseltilmis yardimcinin (broker) Windows Update Agent surucu kurulumu. Bu klasor WUA
// indirme / kurulum arabirimlerinin izinli oldugu TEK yerdir (tests/windows/check_no_install.ps1,
// tests/unit/core/test_net_policy.cpp). Arama hizmet secimi Windows Update'e sabitlenir (WSUS / baska hizmet degil);
// kabul kurali core::drivers::check_candidates'tadir. Lisans onayi kullanici adina verilmez. Baglantiyi Windows Update
// hizmeti kurar; uygulamanin kendi ag listesi degismez. Yalniz broker cagirir (UI sureci degil).
#include <functional>
#include <memory>

#include "core/drivers/driver_install.h"

namespace ef::platform::wua {

// restore_point: geri yukleme noktasi istegi (broker'in sabit aciklamali isi). Nesne cagiran is parcaciginda COM'u
// (MTA) acar; ayni is parcaciginda kullanilir.
std::unique_ptr<core::drivers::DriverInstallApi> make_driver_install(std::function<core::drivers::RestorePointState()> restore_point);

}  // namespace ef::platform::wua
