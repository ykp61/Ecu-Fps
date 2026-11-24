#pragma once
// F-62: RAM yuva/kanal yerlesimi (salt okuma). SMBIOS tip 16/17 + VM imzasi.
// Hicbir ayar yazilmaz; fiziksel kanal sayisi iddia edilmez.

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

core::readiness::MemoryLayoutInfo read_memory_layout();

// B5 (ADR-040 m.6): toplam fiziksel bellek (GlobalMemoryStatusEx), bekleme listesi toplami ve bos+sifirlanmis liste
// (PDH \Memory sayaclari). Salt okuma; yonetici gerekmez. Okunamayan alan nullopt kalir (bulgu uretilmez).
core::readiness::MemoryPressureInfo read_memory_pressure();

}  // namespace ef::platform::readiness
