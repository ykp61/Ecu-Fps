#pragma once
// BRAND-01: tepsi ikonu ve ipucu secimi (saf). Kurallar (marka paketi asset-map state_policy):
//  - "active" ikon yalniz performans islemi dogrulanmis olarak Acik iken (Phase::On).
//  - Kapali: notr ikon. Belirsiz/uygulaniyor/geri aliniyor/onay bekliyor: notr ikon + gercegi
//    soyleyen ipucu ("Durum dogrulaniyor"); kurtarma gerekli: notr ikon + "Geri alma kontrolu gerekli".
//  - Acik/koyu ikon secimi uygulama temasina degil gorev cubugu zeminine gore.
//  - Yuksek kontrast: sistem on plan rengi (cizilir); saglanan siyah/beyaz ICO yedek.

#include <string_view>

#include "core/perf/controller.h"

namespace ef::core::brand {

enum class TaskbarTheme { Dark, Light };
enum class TrayVariant { DarkActive, DarkNeutral, LightActive, LightNeutral, HcWhite, HcBlack, HcSystem };
std::string_view to_string(TrayVariant v) noexcept;

struct TrayChoice {
    TrayVariant variant;
    std::string_view tip_key;  // katalog anahtari
    bool active;               // dogrulanmis acik
};

// known=false: baslangic durumu henuz journal'dan okunmadi.
// hc_system_drawable: yuksek kontrastta sistem rengiyle ikon cizilebiliyor mu.
// hc_dark_background: yuksek kontrast gorev cubugu zemini koyu mu (yedek ICO secimi).
TrayChoice choose_tray(bool known, perf::Phase phase, TaskbarTheme taskbar, bool high_contrast, bool hc_system_drawable,
                       bool hc_dark_background) noexcept;

}  // namespace ef::core::brand
