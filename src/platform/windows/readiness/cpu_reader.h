#pragma once
// F-41: hibrit islemci okuyucusu (salt okuma; yonetici gerekmez, pencere acmaz).
// Topoloji mevcut INV-02 okuyucusundan, Windows build RtlGetVersion uzerinden gelir.
// Thread Director/APO/DTT etkinligi burada okunmaz (genel API yok); tahmin uretilmez.

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

core::readiness::CpuHybridInfo read_cpu_hybrid();

// F-34: CPUID ile HWP/Speed Shift ve EIST DESTEGINI okur (yazma yok). Gercek BIOS/MSR durumu
// ring0 olmadan okunamaz; bu okuyucu yalniz ozellik bitlerini dondurur.
core::readiness::CpuPowerInfo read_cpu_power();

// Spec 2026-11-12 C1: PDH "% Performance Limit" (anlik; ~0,25 sn bekler) ve son acilistan beri
// Kernel-Processor-Power 37 kayit sayisi. Okunamayan alan bos kalir. Sicaklik okunmaz.
core::readiness::CpuSpeedLimitInfo read_cpu_speed_limit();

}  // namespace ef::platform::readiness
