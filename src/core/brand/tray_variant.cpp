#include "core/brand/tray_variant.h"

namespace ef::core::brand {

std::string_view to_string(TrayVariant v) noexcept {
    switch (v) {
        case TrayVariant::DarkActive: return "dark-active";
        case TrayVariant::DarkNeutral: return "dark-neutral";
        case TrayVariant::LightActive: return "light-active";
        case TrayVariant::LightNeutral: return "light-neutral";
        case TrayVariant::HcWhite: return "hc-white";
        case TrayVariant::HcBlack: return "hc-black";
        case TrayVariant::HcSystem: return "hc-system";
    }
    return "dark-neutral";
}

TrayChoice choose_tray(bool known, perf::Phase phase, TaskbarTheme taskbar, bool high_contrast, bool hc_system_drawable,
                       bool hc_dark_background) noexcept {
    using perf::Phase;
    TrayChoice c{TrayVariant::DarkNeutral, "tray.tip_checking", false};
    if (known) {
        switch (phase) {
            case Phase::On:
                c.active = true;
                c.tip_key = "tray.tip_on";
                break;
            case Phase::Off: c.tip_key = "tray.tip_off"; break;
            case Phase::RecoveryNeeded: c.tip_key = "tray.tip_recovery"; break;
            // Plan hazirlaniyor / onay bekliyor / uygulaniyor / geri aliniyor: henuz dogrulanmis durum yok.
            case Phase::Planning:
            case Phase::Confirming:
            case Phase::Applying:
            case Phase::ConfirmRestore:
            case Phase::Restoring: c.tip_key = "tray.tip_checking"; break;
        }
        // Onay bekleyen geri yukleme: degisiklikler hala etkin ve dogrulanmis, ama islem suruyor.
        if (phase == Phase::ConfirmRestore) c.tip_key = "tray.tip_on";
    }
    if (high_contrast) {
        // Yuksek kontrastta renk durumu tasimaz; durum ipucu/menu metniyle verilir.
        c.variant = hc_system_drawable ? TrayVariant::HcSystem : (hc_dark_background ? TrayVariant::HcWhite : TrayVariant::HcBlack);
        return c;
    }
    const bool light = taskbar == TaskbarTheme::Light;
    c.variant = c.active ? (light ? TrayVariant::LightActive : TrayVariant::DarkActive)
                         : (light ? TrayVariant::LightNeutral : TrayVariant::DarkNeutral);
    return c;
}

}  // namespace ef::core::brand
