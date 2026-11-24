#pragma once
// SEC-05 (ADR-013): tek ornek kilidi ve gorunur, istege bagli otomatik baslatma (HKCU Run).

#include <memory>
#include <string>

#include "contracts/result.h"
#include "core/instance_policy.h"

struct HWND__;  // windows.h STRICT HWND (baslik windows.h icermez)

namespace ef::platform {

inline constexpr wchar_t kMainWindowClass[] = L"EcuFps.MainWindow";
// UI-06: tepsi simgesinin sahibi, surec boyunca yasayan gizli pencere (aktivasyon buraya gider).
inline constexpr wchar_t kHostWindowClass[] = L"EcuFps.Host";
inline constexpr wchar_t kActivateMessageName[] = L"EcuFps.Activate.v1";

class SingleInstance {
public:
    // Oturum basina tek ornek. Zaten varsa nullptr doner (already_running=true). name bos = kullanici SID'li urun adi
    // (testler kendi adini verir).
    static std::unique_ptr<SingleInstance> acquire(bool& already_running, const std::wstring& name = {});
    // B2 / bosluk 4: cikmakta olan onceki ornegin kilidi birakmasini en cok timeout_ms bekler. Birakilir (veya surec
    // sonlanip kilit terk edilirse) kilit bu ornegin olur; sure dolarsa nullptr.
    static std::unique_ptr<SingleInstance> wait_acquire(unsigned timeout_ms, const std::wstring& name = {});
    ~SingleInstance();

private:
    explicit SingleInstance(void* h) : h_(h) {}
    void* h_;
};

// ADR-033: bu kullanicinin Ui tek ornek kilidi su an tutuluyor mu (kurulum penceresi "Ecu Fps acik" der). Salt
// okuma: kilit acilmaya calisilir, alinmaz. Varligi dogrulanamazsa (erisim reddi) tutuluyor sayilir.
bool ui_instance_running();
// Calisan ornegin penceresini one getirmeyi bir kez dener (ikinci ornekten). SendMessageTimeout: mesaj dongusu bitmis
// (cikmakta olan) ornek NotResponding doner; pencere yoksa NoWindow. Bekleme yapilmaz (dongu core/instance_policy.h).
core::ActivateTry try_activate_existing_instance(unsigned timeout_ms = 2000);
// Verilen pencereye aktivasyon mesajini bir kez gonderir; yalniz core::kActivateReply yaniti Activated sayilir. UIPI
// reddi Blocked doner (bekleme anlamsiz).
core::ActivateTry activate_window(::HWND__* w, unsigned timeout_ms);
// B2: ikinci baslatma akisi (core::resolve_second_launch). Acquired ise kilit 'out'a yazilir ve normal baslanir.
core::RelaunchOutcome resolve_second_launch(std::unique_ptr<SingleInstance>& out);
// Ana pencerenin aktivasyon mesaji kimligi (RegisterWindowMessage).
unsigned int activate_message_id();

// Otomatik baslatma (ADR-030; ADR-013'un "yalniz kullanici acarsa" kismini supersede eder): varsayilan acik, gorunur
// HKCU\...\Run degeri "EcuFps". Kural core/settings/autostart.h'de, kayit erisimi WinRunKeyStore'da. run_key testte
// degistirilir.
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValueName[] = L"EcuFps";
contracts::Status set_autostart(bool enabled, const std::wstring& run_key = kRunKey);
contracts::Result<bool> autostart_enabled(const std::wstring& run_key = kRunKey);
std::wstring autostart_command();  // "\"<exe>\" --tray"

}  // namespace ef::platform
