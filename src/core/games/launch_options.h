#pragma once
// F-59: Steam localconfig.vdf içinden uygulama başına "LaunchOptions" değerlerini çıkarır (saf, salt okuma).
//  - Toleranslı ayrıştırıcı: bozuk/eksik dosyada boş sonuç döner; istisna yok, dosya yazılmaz.
//  - Yalnız "apps" bloğu altındaki "<appid>" -> "LaunchOptions" eşlemesi toplanır.
//  - Dosya boyutu sınırı çağıran taraftadır (okuma katmanı).

#include <map>
#include <string>
#include <string_view>

namespace ef::core::games {

std::map<std::string, std::string> parse_launch_options(std::string_view vdf);

}  // namespace ef::core::games
