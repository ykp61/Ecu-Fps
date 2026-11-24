#pragma once
// F-59 platform okuyucu: Steam localconfig.vdf başlatma seçenekleri ve CS2 video ayarları.
//  - Salt okuma; boyut sınırı 8 MiB (yerel yapılandırma), 1 MiB (video ayarı).
//  - Dosya bulunamaz/bozuksa boş/nullopt döner; hiçbir dosya yazılmaz.

#include <map>
#include <optional>
#include <string>

#include "core/games/cs2.h"

namespace ef::platform::games {

// F-59: localconfig.vdf okunabildiyse scanned=true; options appid -> LaunchOptions eşlemesi.
// "Kayıt yok" ile "dosya okunamadı" ayrımı UI'da risk yok/taranamadı farkını korur.
struct SteamLaunchOptions {
    bool scanned = false;
    std::map<std::string, std::string> options;
};

SteamLaunchOptions steam_launch_options();

// En yeni Steam kullanıcısının CS2 video ayarları; dosya yoksa/bozuksa nullopt.
std::optional<core::games::cs2::VideoSettings> steam_cs2_video();

}  // namespace ef::platform::games
