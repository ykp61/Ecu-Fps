#pragma once
// MEAS-02: dogrulanmis oyun sureci icin kare yakalama (ETW + PresentData). Yonetici/Performance Log Users
// gerekir; yetki yoksa AccessDenied acik hata (sessiz yukseltme yok).
//  - Hedef kimligi PID + olusturma zamani + yol ile yakalama oncesi VE sonrasi dogrulanir (PID tekrar kullanimi).
//  - Yalniz hedef PID'nin olaylari ozetlenir; overlay/diger surecler dislanir.
//  - Kayip ETW olayi/tampon veya halka tasmasi -> kosu gecersiz (iyimser FPS yok).

#include <atomic>
#include <optional>
#include <string>

#include "core/measure/frames.h"
#include "core/measure/run_manifest.h"
#include "core/measure/view.h"
#include "measurement/frame/frame_session.h"

namespace ef::measurement {

struct GameCaptureResult {
    FrameSessionStart start;
    core::measure::TargetError target = core::measure::TargetError::None;
    core::measure::FrameSummary summary;
    core::measure::FrameStats stats;
    core::measure::RunManifest manifest;
    // BM-01: ayni kosunun sonuc gorunumu; dusuk guven/gecersizlik burada da tasinir (uretim yolu).
    core::measure::ResultView view;
    std::uint64_t dropped_presents = 0;
    // Diag-08 hizasi: ETW oturumu baslangicinin QPC karsiligi (ms). Kare zaman damgalari bu eksende
    // goreli oldugundan, istemci PDH orneklerini ayni eksene tasimak icin kullanilir. Bilinmiyorsa bos;
    // bu durumda telemetri hizasiz sayilir ve kok neden olarak kullanilmaz.
    std::optional<double> capture_origin_ms;
    // EF-X18/T04: yakalama sonucu. Erken hatalarda NotStarted; oyun kapandiysa GameExited (tamamlandi degil).
    core::measure::CaptureOutcome outcome = core::measure::CaptureOutcome::NotStarted;
    std::optional<double> target_exited_after_s;  // erken cikis gozlendiyse, yakalama baslangicina gore saniye
    // Dogrulama bulgusu 2: hedef tam pencereden SONRA (durdurma/son bosaltma) kapandi; sonuc Complete kalir.
    bool target_exited_after_window = false;
    // Dogrulama bulgusu 3: ham sayaclar ayri (manifest.events_lost bunlarin toplamidir; kanit ayri yazar).
    FrameSessionStats session_stats;
    core::measure::CaptureLossCounters loss;
};

// Surec kimligini isletim sisteminden okur (PROCESS_QUERY_LIMITED_INFORMATION). Okunamazsa pid'li bos kimlik.
core::measure::ProcessIdentity query_identity(std::uint32_t pid);

// seconds kadar (veya cancel) yakalar. app: manifest uygulama kimligi (exe adi + surum).
GameCaptureResult capture_game(const core::measure::ProcessIdentity& target, double seconds, const std::atomic<bool>& cancel,
                               const std::string& app, double min_seconds);

}  // namespace ef::measurement
