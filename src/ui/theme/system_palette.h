#pragma once
// UI-04: Windows yuksek kontrast etkinse uygulama kendi renklerini birakip sistem renklerini kullanir.

#include <optional>

#include "ui/theme/theme.h"

namespace ef::ui {

bool high_contrast_on();
// BRAND-01: gorev cubugu (sistem) modu: SystemUsesLightTheme. Belgelenmis API degil; okunamazsa koyu
// varsayilir (Windows varsayilani). Tepsi ikon secimi uygulama temasina degil buna gore yapilir.
bool taskbar_uses_light_theme();
// TH-08: Windows yuksek kontrast renk tablosunun uygulama paletine donusumu. Saf islev: calisma zamani
// GetSysColor degerlerini, snapshot/denetim araclari da temsili sistem tablosunu ayni kod yolundan gecirir.
struct SystemColorTable {
    Color window, button_face, window_text, highlight, highlight_text, hotlight, gray_text;
};
Palette high_contrast_palette_from(const SystemColorTable& c);
// Yuksek kontrast acikken GetSysColor tabanli palet; degilse nullopt.
std::optional<Palette> system_high_contrast_palette();

}  // namespace ef::ui
