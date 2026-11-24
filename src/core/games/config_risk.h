#pragma once
// F-59: oyun yapılandırma ve başlatma seçeneği risk tarayıcısı — SALT OKUMA, saf (birim testlenir).
//  - Yalnız belgelenmiş/toplulukça iyi bilinen risk kalıpları işaretlenir; "kesin FPS düşürür" iddiası yok.
//  - Hiçbir dosya/seçenek yazılmaz; çıktı yalnız kod + eşleşen değer + önem derecesidir.
//  - Oyun desteği: CS2 video ayarları (doğrulanmış okuyucu) ve Steam başlatma seçenekleri.
//    Engine.ini taraması, doğrulanmış oyun-yolu eşlemesi olmadığı için bu dilimde yoktur.

#include <string>
#include <string_view>
#include <vector>

#include "core/games/cs2.h"

namespace ef::core::games {

enum class RiskSeverity { Info, Caution, Warning };
std::string_view to_string(RiskSeverity s) noexcept;

struct ConfigRisk {
    std::string code;      // katalog anahtarı (risk.*)
    RiskSeverity severity = RiskSeverity::Info;
    std::string detail;    // eşleşen belirteç (ör. "-high"); kullanıcıya ham gösterilir
};

// Steam başlatma seçenekleri metnini risk kalıpları için tarar. Çift tırnaklı parçalar tek belirteç sayılır.
std::vector<ConfigRisk> scan_launch_options(std::string_view options);

// CS2 video ayarlarında yalnız okunan alanlara dayalı riskler. Bilinmeyen alan risk üretmez.
std::vector<ConfigRisk> scan_cs2_video(const cs2::VideoSettings& v);

// İki taramayı birleştirir; aynı kod iki kez eklenmez (ilk sıra korunur).
std::vector<ConfigRisk> merge_risks(std::vector<ConfigRisk> a, const std::vector<ConfigRisk>& b);

}  // namespace ef::core::games
