#pragma once
// B1 (2026-11-09 incelemesi): App'in calisan oyun karari icin saf, test edilebilir yol.
//  - Guvenilir (Ok) taramanin TUM calisan oyun listesi TEK adimda yerlesir; olay sirasindan
//    gecici bos liste turetilmez.
//  - Yan etkiler (otomatik geri alma, bekleyen cikis/kurtarma) yalniz nihai durumdan uretilir:
//    son oyunun kapandigi gecis BIR kez bildirilir; oyun kaldigi surece hicbir mutasyon tetiklenmez.
//  - Failed/Incomplete/Unknown: liste ve etkin oyun KORUNUR, yan etki yok (fail-closed).

#include <optional>
#include <string>
#include <vector>

#include "core/games/watcher.h"

namespace ef::core::games {

// App'in uygulayacagi nihai durum + tik basina yan etkiler. Saf veri; Win32 yok.
struct WatchInstall {
    std::vector<std::string> running_dirs;  // kapi: calisan TUM kurulu oyun klasorleri
    std::optional<GameStart> active;        // izleyici sozlesmesi: tek etkin oyun (ilk calisan)
    DetectionState detection = DetectionState::Unknown;
    bool detection_ok = false;
    bool first_safe_scan = false;  // ilk guvenilir tarama tamamlandi
    bool last_game_closed = false; // once oyun vardi, nihai listede yok (gecis, tek sefer)
    bool auto_restore = false;     // ilk tarama bos VEYA son oyun kapandi
    bool discarded = false;        // eski gozlem: durum degismedi, yan etki yok
    std::int64_t observed_at_ms = -1;
};

class WatchState {
public:
    // Yeni izleyici oturumu: Unknown, liste bos, ilk guvenilir tarama bekleniyor.
    void reset();
    // Sonucu uygular ve nihai durumu doner. Non-Ok sonucta liste/etkin oyun degismez.
    WatchInstall apply(const WatchResult& result);

    const std::vector<std::string>& running() const { return running_; }
    const std::optional<GameStart>& active() const { return active_; }
    DetectionState detection() const { return detection_; }
    bool first_safe_scan_done() const { return first_safe_scan_done_; }

private:
    std::vector<std::string> running_;
    std::optional<GameStart> active_;
    DetectionState detection_ = DetectionState::Unknown;
    bool first_safe_scan_done_ = false;
    std::int64_t last_observed_ms_ = -1;
};

}  // namespace ef::core::games
