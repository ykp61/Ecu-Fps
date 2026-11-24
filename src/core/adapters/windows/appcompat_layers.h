#pragma once
// F-47: oyun EXE'si basina tam ekran iyilestirmelerini kapatma — kullanici kapsami (HKCU), UAC yok.
// Hedef bicimi: "appcompat:<hex>" (hex = kanonik tam .exe yolunun utf-8 baytlari). Hex, islem hedefi
// sozlesmesinin guvenli karakter kumesine uyar (yol/shell karakteri tasimaz; codec yol kabul etmez).
// Kayit yolu SABITTIR:
//   HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
// Deger adi EXE yoludur; deger REG_SZ katman listesidir ("~ TOKEN1 TOKEN2").
// Uygulama YALNIZ kendi jetonunu (DISABLEDXMAXIMIZEDWINDOWEDMODE) ekler/kaldirir; kullanicinin diger
// katmanlari korunur. Geri alma anlik goruntudeki jeton durumuna doner; deger onceden yoksa ve baska
// katman da kalmadiysa deger silinir (varsayilan uydurulmaz).
// Guvenlik: serbest kayit yolu yok; hedef yol bicimi dogrulanir ve YAZMA yalniz kayitli kurulum
// klasorlerinin altindaki EXE'ler icin yapilir (SEC-03 ilkesi). Kurtarma/geri okuma icin klasor
// listesi gerekmez: onceki oturumun kaydi ayni adaptorle geri alinabilmelidir.

#include <cstdint>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/adapters/windows/gaming_toggles.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kAppCompatPrefix = "appcompat:";
inline constexpr std::wstring_view kAppCompatLayersSubkey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
inline constexpr std::wstring_view kDisableFsOptToken = L"DISABLEDXMAXIMIZEDWINDOWEDMODE";

// "appcompat:<hex>" -> genis yol. Bicim gecersizse (bozuk hex, goreli yol, "..", ".exe" disi, gecersiz utf-8) nullopt.
std::optional<std::wstring> appcompat_exe_from_target(std::string_view target);
// "appcompat:<hex>" -> kanonik hedef ("/" -> "\", ASCII kucuk harf). Gecersizse nullopt.
std::optional<std::string> appcompat_canonical_target(std::string_view target);
// Mutlak .exe yolundan kanonik hex hedef; gecersizse bos dize.
std::string appcompat_target_for_exe(std::string_view exe_utf8);

// T01: onek parametreli ortak hedef/kok yardimcilari (appcompat: ve gpupref: ayni kurallari kullanir; ayrisamaz).
std::optional<std::wstring> exe_from_prefixed_target(std::string_view prefix, std::string_view target);
std::optional<std::string> canonical_prefixed_target(std::string_view prefix, std::string_view target);
std::string prefixed_target_for_exe(std::string_view prefix, std::string_view exe_utf8);
// Yazma kokleri: kanonik (kucuk harf, "\\", sonda ayirici yok, mutlak surucu yolu) liste.
std::vector<std::string> canonicalize_write_roots(const std::vector<std::string>& roots);
bool canonical_target_under_roots(std::string_view prefix, std::string_view canonical_target, const std::vector<std::string>& roots);

// Katman listesi yardimcilari (jeton karsilastirmasi buyuk/kucuk harf duyarsiz).
bool layer_has_token(std::wstring_view layers, std::wstring_view token);
std::wstring layer_add_token(std::wstring_view layers, std::wstring_view token);
std::wstring layer_remove_token(std::wstring_view layers, std::wstring_view token);

class AppCompatLayerAdapter final : public recovery::SettingAdapter {
public:
    explicit AppCompatLayerAdapter(UserRegistryApi& api) : api_(api) {}
    // YAZMA icin izinli kurulum klasorleri (utf-8, mutlak). Bos liste: tum yazmalar reddedilir.
    void set_allowed_roots(std::vector<std::string> roots);
    // Salt okuma: true = jeton var, false = yok (deger baska katmanlar icerebilir), nullopt = deger yok.
    contracts::Result<std::optional<bool>> token_state(std::string_view target);

    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    bool under_allowed_root(std::string_view canonical_target) const;

    UserRegistryApi& api_;
    mutable std::mutex roots_mu_;  // kitaplik degisince UI, yazarken admin is parcacigi
    std::vector<std::string> roots_;
};

}  // namespace ef::core::adapters::windows
