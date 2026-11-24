#pragma once
// Spec 2026-11-12 G / ADR-030: Windows ile baslatma. Varsayilan acik: ilk calistirmada kullanici icin gorunur HKCU
// Run degeri "EcuFps" = "\"<exe>\" --tray" yazilir (yonetici gerekmez), ilk acilista bir kez engellemeyen bildirim
// gosterilir. EXE tasinmissa acilista kayit yeni yola guncellenir; anahtar kapaliysa kayit silinir ve bir daha
// yazilmaz; kaldirma akisi kaydi siler. Her yazma / silme geri okunarak dogrulanir; basarisizsa anahtar kapali gorunur
// ve neden yazilir. Kayit erisimi RunKeyStore arabirimindedir (saf kural; birim testleri sahte depo kullanir, gercek
// kayit defterine dokunmaz).

#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/settings/preferences.h"

namespace ef::core::settings {

inline constexpr std::wstring_view kAutostartValueName = L"EcuFps";
// SEC-05'in eski deger adi (kullanici acarsa yazilirdi). Acilista ve kaldirmada silinir; yerine yeni ad yazilir.
inline constexpr std::wstring_view kLegacyAutostartValueName = L"Ecu Fps";

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run degerleri (yalniz REG_SZ). read: deger yoksa nullopt.
class RunKeyStore {
public:
    virtual ~RunKeyStore() = default;
    virtual contracts::Result<std::optional<std::wstring>> read(std::wstring_view name) = 0;
    virtual contracts::Status write(std::wstring_view name, std::wstring_view command) = 0;
    virtual contracts::Status remove(std::wstring_view name) = 0;  // deger yoksa da basarili
};

// "\"<exe>\" --tray"
std::wstring autostart_command(std::wstring_view exe_path);

enum class AutostartFailure { None, ReadFailed, WriteFailed, VerifyFailed, RemoveFailed };

struct AutostartState {
    bool on = false;     // kayit var ve bu EXE'yi --tray ile gosteriyor (geri okunarak)
    bool known = true;   // kayit okunabildi (okunamazsa anahtar devre disi)
    AutostartFailure failure = AutostartFailure::None;  // son islemin nedeni (gorunur satir)
    friend bool operator==(const AutostartState&, const AutostartState&) = default;
};

// Acilis uzlastirmasinin hangi yoldan gectigi (tani kaydi; davranisi degistirmez).
enum class AutostartBranch {
    Matched,          // tercih acik, kayit bu EXE'yi gosteriyor
    FirstRunWritten,  // ilk calistirma: varsayilan acik yazildi
    Rewritten,        // tercih acik, kayit baska EXE'yi gosteriyordu (tasinmis EXE): bu yola yazildi
    RemovedOutside,   // tercih acik, kayit yok: disaridan silindi sayildi, tercih kapandi (yeniden yazilmaz)
    PrefOffRemoved,   // tercih kapali, kayit vardi: silindi
    PrefOffAbsent,    // tercih kapali, kayit yok
    ReadFailed,       // kayit okunamadi: hicbir sey yazilmadi
    WriteFailed,      // yazma / dogrulama basarisiz: tercih kapandi
    RemoveFailed,     // silme basarisiz
};
std::string_view to_string(AutostartBranch b) noexcept;

// Acilista gorulen kayit (yol icermez).
enum class RunValueSeen { Unreadable, Absent, ThisExe, OtherExe };
std::string_view to_string(RunValueSeen v) noexcept;

struct AutostartLaunch {
    AutostartState state;
    bool prefs_changed = false;  // cagiran tercihleri kaydeder
    AutostartBranch branch = AutostartBranch::Matched;
    RunValueSeen seen = RunValueSeen::Unreadable;
    bool first_run = false;
};

// Kullanicinin okuyabilecegi tani satiri (%LOCALAPPDATA%\EcuFps\logs\autostart.log). Yol / kullanici adi icermez.
// Sahip kurulumu 2026-11-13: Run degeri kurulumdan sonra kayboldu, neden kanitlanamadi; bu satirlar bir sonraki
// olayda hangi surecin ne gordugunu / ne yaptigini zamanla gosterir.
std::string autostart_launch_trace(const AutostartLaunch& r, const Preferences& after);
std::string autostart_set_trace(std::string_view who, bool requested, const AutostartState& s);

// Yalniz okuma: kaydin durumu (tercihlere ve kayda dokunmaz).
AutostartState autostart_read(RunKeyStore& store, std::wstring_view command);

// Acilis uzlastirmasi. Eski adli kayit silinir. Ilk calistirma (autostart_initialized yok): tercih acik + bildirim
// bekliyor. Tercih acikken kayit bu EXE'yi gostermiyorsa (tasinmis EXE) yeniden yazilir; kayit disaridan silinmisse
// tercih kapanir (yeniden yazilmaz). Tercih kapaliyken kayit varsa silinir. Yazma / dogrulama basarisizsa tercih
// kapanir ve neden doner (anahtar kapali gorunur). Okuma hatasinda hicbir sey yazilmaz.
AutostartLaunch autostart_on_launch(RunKeyStore& store, Preferences& prefs, std::wstring_view command);

// Ayarlar anahtari. on: yaz + geri oku; off: sil + geri oku. Tercih istenen degere ayarlanir (yazma basarisizsa
// kapali kalir). Bildirim artik gosterilmez (kullanici karar verdi).
AutostartState autostart_set(RunKeyStore& store, Preferences& prefs, bool on, std::wstring_view command);

// Kaldirma akisi: yeni ve eski adli kayitlarin ikisini de siler ve yoklugunu dogrular.
contracts::Status autostart_remove_all(RunKeyStore& store);

}  // namespace ef::core::settings
