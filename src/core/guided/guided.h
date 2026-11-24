#pragma once
// Rehberli eylemler: urunun yazmadigi (belgesiz yazma yolu, BIOS, surucu) ayarlar icin kullaniciyi
// dogru Windows Ayarlar sayfasina veya resmi kaynaga yonlendirir. Hicbir ayar degistirilmez.

#include <string_view>

namespace ef::core::guided {

enum class Action {
    GameModeSettings,      // W03
    GraphicsSettings,      // W04 / W05 / W06 (uygulama basina GPU, pencereli optimizasyon, HAGS)
    CaptureSettings,       // W07
    WindowsUpdate,         // WIN-07 / R28
    StartupApps,           // B01-B06
    PowerSettings,         // W01/W02 (laptop)
    DisplaySettings,       // W08
    StorageSettings,       // EXT-CLN: Windows Depolama ayarlari (yalniz acar; temizlik sayilmaz)
};

std::string_view uri(Action a) noexcept;       // ms-settings: URI (yalniz izin listesi)
std::string_view text_key(Action a) noexcept;  // i18n anahtari (aciklama)
bool is_allowed_uri(std::string_view u) noexcept;

}  // namespace ef::core::guided
