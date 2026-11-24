#pragma once
// OPT-09 (plan v3.5): kullanici kapsamli Windows oyun ayarlari — journal'li, geri okunur, birebir geri alinir.
// Hedefler SABIT izin listesidir (serbest kayit yolu yok; SEC-03 ilkesi):
//   "gaming:game_mode"       HKCU\Software\Microsoft\GameBar            AutoGameModeEnabled (DWORD) 1 = acik
//   "gaming:dvr_background"  HKCU\Software\Microsoft\Windows\CurrentVersion\GameDVR HistoricalCaptureEnabled (DWORD) 0 = kapali
//   "gaming:app_capture"     ayni anahtar, AppCaptureEnabled (DWORD) 0 = Game Bar yakalama kapali
//   "gaming:dvr_enabled"     HKCU\System\GameConfigStore               GameDVR_Enabled (DWORD) 0 = eski/aynalanan ana anahtar kapali
//   "gaming:gamebar_startup" HKCU\Software\Microsoft\GameBar           ShowStartupPanel (DWORD) 0 = oyun basinda ipucu paneli acilmaz
//   "gaming:windowed_opt"    HKCU\Software\Microsoft\DirectX\UserGpuPreferences DirectXUserGlobalSettings (SZ) SwapEffectUpgradeEnable
//   "gaming:windows_vrr"     ayni deger, VRROptimizeEnable
// Bu degerler Windows Ayarlar uygulamasinin kendi sakladigi yerlerdir (gozlem; resmi API degil -> ADR-022).
// Geri alma: anlik goruntudeki ham tur + bayt aynen yazilir; onceden yoksa deger silinir (varsayilan uydurulmaz).
// DirectXUserGlobalSettings ortak bir dizedir: yalniz ilgili anahtar degisir, digerleri (AutoHDR vb.) korunur.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

struct RegRaw {
    std::uint32_t type = 0;               // REG_DWORD=4, REG_SZ=1
    std::vector<std::uint8_t> bytes;
    bool operator==(const RegRaw&) const = default;
};

class UserRegistryApi {
public:
    virtual ~UserRegistryApi() = default;
    // HKCU altinda okuma; deger yoksa nullopt.
    virtual contracts::Result<std::optional<RegRaw>> read(std::wstring_view subkey, std::wstring_view name) = 0;
    virtual contracts::Status write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) = 0;
    virtual contracts::Status remove(std::wstring_view subkey, std::wstring_view name) = 0;
};

struct GamingTarget {
    std::string_view id;          // "gaming:game_mode"
    std::wstring_view subkey, name;
    bool is_dx_flag = false;      // DirectXUserGlobalSettings icindeki "Anahtar=deger;" ogesi
    std::wstring_view dx_key;     // SwapEffectUpgradeEnable / VRROptimizeEnable
};
const GamingTarget* find_gaming_target(std::string_view id) noexcept;

// "A=1;B=0;" icinde key degerini okur / degistirir (yoksa ekler); digerleri ve sira korunur.
std::optional<std::wstring> dx_flag_get(std::wstring_view s, std::wstring_view key);
std::wstring dx_flag_set(std::wstring_view s, std::wstring_view key, std::wstring_view value);

RegRaw dword_raw(std::uint32_t v);
std::optional<std::uint32_t> raw_dword(const RegRaw& r);
RegRaw sz_raw(std::wstring_view s);
std::optional<std::wstring> raw_sz(const RegRaw& r);

// Islem degeri "0"/"1". read_back: o anki etkin degeri "0"/"1" olarak dondurur (deger yoksa nullopt).
class GamingToggleAdapter final : public recovery::SettingAdapter {
public:
    explicit GamingToggleAdapter(UserRegistryApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    // Saha 2026-11-15 (yanlis "geri alinamadi"): DirectXUserGlobalSettings ortak dizesinde existed = DIZE vardi; ogenin
    // onceki yoklugu old_value "<absent>" ile tutulur. Oge kaldirilinca read_back nullopt doner; varsayilan kural
    // (existed -> deger esit) bunu "<absent>" ile karsilastirip basarili geri almayi dogrulanamadi sayiyordu.
    // Gercek uyusmazlik (oge baska degerde) yine uyusmazliktir.
    bool matches_snapshot(const contracts::SettingOperation& op, const recovery::Snapshot& snap,
                          const std::optional<std::string>& v) override;

private:
    UserRegistryApi& api_;
};

// Bellek ici sahte (birim testleri).
class FakeUserRegistry final : public UserRegistryApi {
public:
    std::map<std::wstring, RegRaw> values;  // "subkey|name"
    contracts::Result<std::optional<RegRaw>> read(std::wstring_view subkey, std::wstring_view name) override;
    contracts::Status write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) override;
    contracts::Status remove(std::wstring_view subkey, std::wstring_view name) override;
    static std::wstring key(std::wstring_view subkey, std::wstring_view name) { return std::wstring(subkey) + L"|" + std::wstring(name); }
};

}  // namespace ef::core::adapters::windows
