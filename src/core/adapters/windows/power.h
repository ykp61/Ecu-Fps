#pragma once
// WIN-01: W01 AC guc modu (Windows 11 kullanici guc modu) ve W02 etkin guc plani adaptorleri.
// Kurallar (katalog): yalniz bilinen guc modu GUID'leri; plan yalniz sistemde VAR olan plana gecer,
// plan olusturulmaz/duzenlenmez; geri alma onceki modu/plani etkinlestirir ve dogrular.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "contracts/support.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

// Bilinen guc modu GUID'leri (kucuk harf)
inline constexpr std::string_view kModeBestEfficiency = "961cc777-2547-4f9d-8174-7d86181b8a7a";
inline constexpr std::string_view kModeBalanced = "00000000-0000-0000-0000-000000000000";
inline constexpr std::string_view kModeBetterPerformance = "3af9b8d9-7c97-431d-ad78-34a8bfea439f";
inline constexpr std::string_view kModeBestPerformance = "ded574b5-45a0-4f42-8737-46345c09c238";
inline constexpr std::string_view kSchemeBalanced = "381b4222-f694-41f0-9685-ff5bb260df2e";

bool is_known_power_mode(std::string_view guid) noexcept;
bool is_guid_text(std::string_view s) noexcept;  // 8-4-4-4-12 kucuk harf hex

class PowerApi {
public:
    virtual ~PowerApi() = default;
    // Windows 11 kullanici yapilandirmali AC guc modu (yoksa Unsupported).
    virtual contracts::Result<std::string> get_ac_mode() = 0;
    virtual contracts::Status set_ac_mode(const std::string& guid) = 0;
    // Gap 11: pil (DC) guc modu; AC ile ayni GUID kumesi. Yoksa Unsupported (cihaz yalniz AC).
    virtual contracts::Result<std::string> get_dc_mode() = 0;
    virtual contracts::Status set_dc_mode(const std::string& guid) = 0;
    // Sistem pili var mi (GetSystemPowerStatus BatteryFlag). Okunamazsa hata: DC ogesi plana girmez.
    virtual contracts::Result<bool> has_battery() = 0;
    virtual contracts::Result<std::string> get_active_scheme() = 0;
    virtual contracts::Status set_active_scheme(const std::string& guid) = 0;
    virtual contracts::Result<std::vector<std::string>> list_schemes() = 0;
};

// Hedefler: "power:mode:ac", "power:mode:dc" (pil), "power:scheme:active". AC ve DC ayri journal
// adimlaridir: her biri kendi baslangic kaydi, uygulama, geri okuma ve geri yuklemesine sahiptir.
inline constexpr std::string_view kTargetPowerModeAc = "power:mode:ac";
inline constexpr std::string_view kTargetPowerModeDc = "power:mode:dc";
class PowerModeAdapter final : public recovery::SettingAdapter {
public:
    explicit PowerModeAdapter(PowerApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    // Guc modu kaydiricisi Windows 11'de yalniz Dengeli plan etkinken anlamlidir.
    contracts::Capability capability();

private:
    contracts::Result<std::string> get_mode(std::string_view target);
    contracts::Status set_mode(std::string_view target, const std::string& guid);
    PowerApi& api_;
};

class PowerSchemeAdapter final : public recovery::SettingAdapter {
public:
    explicit PowerSchemeAdapter(PowerApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    contracts::Capability capability();

private:
    PowerApi& api_;
};

class FakePowerApi final : public PowerApi {
public:
    contracts::Result<std::string> get_ac_mode() override;
    contracts::Status set_ac_mode(const std::string& guid) override;
    contracts::Result<std::string> get_dc_mode() override;
    contracts::Status set_dc_mode(const std::string& guid) override;
    contracts::Result<bool> has_battery() override;
    contracts::Result<std::string> get_active_scheme() override { return scheme; }
    contracts::Status set_active_scheme(const std::string& guid) override;
    contracts::Result<std::vector<std::string>> list_schemes() override { return schemes; }
    bool modes_supported = true;
    std::string mode = std::string(kModeBalanced);
    // Varsayilan masaustu: pil yok. Pilli cihaz testleri battery=true ayarlar.
    bool battery = false;
    bool battery_readable = true;
    bool dc_supported = true;
    // Gecici DC okuma hatasi (IoError/Denied vb.); bos = hata yok.
    std::optional<contracts::ErrorCode> dc_read_error;
    std::string dc_mode = std::string(kModeBalanced);
    std::string scheme = std::string(kSchemeBalanced);
    std::vector<std::string> schemes{std::string(kSchemeBalanced), "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"};
};

}  // namespace ef::core::adapters::windows
