#pragma once
// INGAME-CAPTURE: oyun ici kare yakalama icin yukseltilmis yardimci surec (ayni EXE, Role::CaptureWorker)
// ve istemci kanali.
//  Yardimci : yalniz "--role=capture --parent=<pid> --nonce=<32hex>" ile baslar; yukseltilmis olmali, ust surec
//             ayni EXE olmali, SEC-04 trust politikasi broker ile ayni. Tek istemcili pipe (ACL: oturum kullanicisi),
//             Hello nonce + PID dogrulamasi, beyaz listeli tek JSON istek, hedef kimligi OS'tan + overlay reddi.
//             Sonuc: 1 JSON baslik + float32 LE aralik parcalari (<= 60 KB) olarak aktarilir.
//  Istemci  : ShellElevationLauncher ile baslatir, yardimci PID'ine baglanir, sonucu tipli yapiya cozer.
//             UAC reddi ayri sonuc; hicbir dosya yolu parametresi yok, diske yazilmaz.
//  Iptal    : Yardimci surec yalniz istemci onu sonlandirinca iptal olur (Cancel mesaji yok). Istemci cancel
//             bayragini gorunce TerminateProcess + wait uygular; yardimci sinirli sure (<=120 sn) icinde kendiliginden biter.

#include <atomic>
#include <string>

#include "core/measure/frames.h"
#include "core/roles.h"
#include "measurement/frame/game_capture.h"
#include "platform/windows/elevation.h"

namespace ef::platform {

enum class CaptureClientStatus {
    Ok,        // protokol tamamlandi; yakalama sonucu (start/target hatalari dahil) capture alaninda
    UacDenied, // kullanici UAC istemini reddetti; yardimci baslamadi, hicbir istek gonderilmedi
    Cancelled, // istemci iptal etti; yardimci sonlandirildi
    Error,     // baslatma/el sikisma/aktarim hatasi; detail teshis metni
};

struct CaptureClientResult {
    CaptureClientStatus status = CaptureClientStatus::Error;
    measurement::GameCaptureResult capture;
    std::string detail;

    bool ok() const noexcept { return status == CaptureClientStatus::Ok; }
    bool uac_denied() const noexcept { return status == CaptureClientStatus::UacDenied; }
    bool cancelled() const noexcept { return status == CaptureClientStatus::Cancelled; }
    // Protokol tamamlandi ama ETW yetkisi yok (yukseltilmemis/Performance Log Users degil): sessiz yukseltme yok.
    bool access_denied() const noexcept {
        return status == CaptureClientStatus::Ok && capture.start.error == measurement::FrameSessionError::AccessDenied;
    }
};

// Yardimci rolu: el sikisma + yakalama + sonuc aktarimi. Cikis kodu doner (kExit*).
// parent: main.cpp'de dogrulanan ust surec kimligi; pipe kabulunde PID yeniden kullanimi dahil birebir eslesir.
int run_capture_worker(const core::LaunchArgs& args, const ParentIdentity& parent);

// Istemci: hedef surec icin yukseltilmis yardimci baslatir ve sonucu toplar.
// target: onayli oyun kimligi (create_time/image doluysa yardimcinin okudugu kimlikle karsilastirilir).
// seconds: 1..120, min_seconds: 0..120 ve <= seconds. app: kisa uygulama kimligi (yol karakteri yok).
// cancel: true gorulurse yardimci surec sonlandirilir. Diske yazmaz.
CaptureClientResult capture_game_elevated(const core::measure::ProcessIdentity& target, double seconds, double min_seconds,
                                          std::string app, const std::atomic<bool>& cancel);

}  // namespace ef::platform
