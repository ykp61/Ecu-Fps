#pragma once
// Uyumluluk B8 (bosluk 8 / 12): destek paketi diag.json'un platform kismi (salt okuma; hicbir ayar degismez).
// Uygulamaya ozel alanlar (surum, dil zinciri, ortam bayraklari) cagiran tarafindan doldurulur.

#include <cstdint>
#include <optional>
#include <string>

#include "core/capabilities/device_verification.h"
#include "core/support/diag.h"

namespace ef::platform::inventory {

// OS build, CPU topolojisi, DXGI bagdastiricilari (+ yuksek performans tercihi), ekranlar (Hz/DPI/HDR), guc kaynagi,
// sistem diski doluluk orani, cihaz dogrulama ve destek matrisi.
void collect_platform_diag(core::support::DiagInput& in);

// Oyunlarin kullandigi (DXGI yuksek performans tercihindeki ilk donanim) GPU'nun PCI ureticisi; okunamazsa nullopt.
std::optional<std::uint32_t> game_gpu_vendor_id();
// Bu cihaz dogrulanmis bir kombinasyon mu (OS build + oyun GPU ureticisi). Arka plan is parcaciginda cagrilir.
core::capabilities::DeviceVerification current_device_verification();

}  // namespace ef::platform::inventory
