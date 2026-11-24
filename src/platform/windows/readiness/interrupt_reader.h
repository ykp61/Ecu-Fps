#pragma once
// B5 (ADR-040 m.5): kesme (MSI) durumu okuyucusu. YALNIZ OKUMA: kayit defterine/aygita yazmaz, yonetici istemez,
// pencere acmaz. Oyun GPU'su (DXGI yuksek performansli yazilim-olmayan adaptor -> SetupAPI ekran sinifi, PCI VEN/DEV
// eslesmesi) ve etkin ag bagdastiricisi (varsayilan yol; Network\{...}\Connection\PnpInstanceID) icin
//   Enum\<ornek>\Device Parameters\Interrupt Management\MessageSignaledInterruptProperties : MSISupported
//   Enum\<ornek>\Device Parameters\Interrupt Management\Affinity Policy : DevicePriority
// okunur. Ornek anahtari acilamazsa readable=false (veri yetersiz); acildi ama deger yoksa nullopt (desteklenmiyor).
// ENGELLEYICI (DXGI + SetupAPI): UI is parcaciginda cagirmayin.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/readiness/readiness_interrupts.h"

namespace ef::platform::readiness {

// Saf: bir aygit donanim kimligi (ornek "PCI\VEN_10DE&DEV_2B85&SUBSYS_...") verilen PCI uretici/aygit ile eslesir mi.
// Buyuk/kucuk harf duyarsiz; 4 haneli onaltilik tam eslesme (on ek eslesmesi yok).
bool hardware_id_matches_pci(std::string_view hardware_id, std::uint32_t vendor_id, std::uint32_t device_id);

std::vector<core::readiness::InterruptDeviceInfo> read_interrupt_devices();

}  // namespace ef::platform::readiness
