#pragma once
// GAME-02: Counter-Strike 2 (Steam appid 730) adaptoru — SALT OKUMA.
// Bu PC'de gozlenen: <Steam>\userdata\<id>\730\local\cfg\cs2_video.txt (KeyValues "video.cfg", Version 16),
// Steam bulut listesinde (remotecache.vdf) yok. Valve alanlari resmi olarak belgelemiyor; oyunun yazilan
// degeri kabul ettigi bu PC'de kontrollu tur olmadan dogrulanmadi -> config YAZIMI KAPALI.
// CS2'nin resmi yerlesik benchmark komutu yok (topluluk haritasi resmi degil) -> olcum: gozlem (dis kare).

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/games/adapter.h"

namespace ef::core::games::cs2 {

inline constexpr std::string_view kAppId = "730";
inline constexpr std::string_view kExeRelative = "game\\bin\\win64\\cs2.exe";

// Duz KeyValues (tek seviye bolum) ayristirici; bozuk girdide nullopt (dosya asla yazilmaz).
std::optional<std::map<std::string, std::string>> parse_video_cfg(std::string_view text);

// Tipli gorunum (yalniz okunan, kaynakta gorulen alanlar). Bilinmeyen/eksik -> nullopt.
struct VideoSettings {
    std::optional<int> width, height;
    std::optional<double> refresh_hz;
    std::optional<bool> fullscreen, vsync;
    std::optional<int> msaa_samples;
    std::optional<int> fsr_detail;       // 0 = kapali (upscaling kalite etkisi GAME-04)
    std::optional<int> low_latency;      // r_low_latency (NVIDIA Reflex modu)
    std::optional<int> shader_quality, texture_detail, shadow_quality;
    std::optional<int> version;
};
VideoSettings typed(const std::map<std::string, std::string>& kv);

// Alan bazli metin duzenleme (yazim yolu hazir, kayitta KAPALI): yalniz verilen anahtarin deger tirnagi
// degisir; diger satirlar/bicim/sira bayt bayt korunur. Anahtar yoksa veya dosya bozuksa nullopt (yazma yok).
std::optional<std::string> set_field(std::string_view text, std::string_view key, std::string_view value);
// Geri alma: alanin su anki degeri bizim yazdigimiz degerse eski deger konur. Kullanici sonradan ayni
// alani degistirdiyse cakisma (dosyaya dokunulmaz); baska alanlardaki degisiklikler korunur.
struct RestoreResult {
    std::optional<std::string> text;  // nullopt -> yazma yok
    bool conflict = false;
};
RestoreResult restore_field(std::string_view text, std::string_view key, std::string_view old_value, std::string_view written_value);

// Kayit: yalniz gozlem + config okuma; dogrulanmis surum yok (yazim yok).
AdapterRecord adapter_record();

}  // namespace ef::core::games::cs2
