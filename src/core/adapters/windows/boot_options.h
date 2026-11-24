#pragma once
// ADR-040 m.4 (B0 iskeleti, B4 doldurdu): onyukleme yapilandirmasi (BCD) secenegi. Tek hedef "boot:dynamictick",
// tek deger "off" (= BCD "disabledynamictick yes"; oge 0x260000a5, winload, Boolean). Microsoft BCDEdit /set belgesi bu
// secenegi "yalniz hata ayiklama icin" diye tanimlar. Islem: OperationId::BootOptionSet, scope Machine, yalniz broker.
//
// Degismezler:
//  - IPC'den komut satiri, BCD kimligi, ogenin adi veya serbest deger gelmez; hedef bu tablodan cozulur.
//  - kBootOptionItemEnabled = false: izole VM'de uygula -> yeniden baslat -> geri oku -> Kapali -> yeniden baslat -> geri
//    oku turunun kaniti gelene kadar oge plana GIRMEZ ve broker yeni yazmayi (Apply/Fix) reddeder. Adaptor de ayni kapiyi
//    apply'da tasir (savunma derinligi); restore bu kapiya bakmaz (kayitli geri alma bayrak kapansa da calisir).
//  - Geri alma: anlik goruntude oge yoktuysa SILINIR, vardiysa eski deger ("on"/"off") yazilir (varsayilan uydurulmaz).
//  - Deger kodlamasi: "off" = disabledynamictick yes (dinamik tick kapali), "on" = disabledynamictick no, oge yok = nullopt.
// Bu dosya Win32 bagimsizdir; platform yolu platform/windows/adapters/win_boot_options.* (R-B4 adim 0: okuma yolu
// dogrulanmadi -> okuma Unsupported doner, oge hicbir zaman plana girmez).

#include <optional>
#include <string>
#include <string_view>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kBootDynamicTickTarget = "boot:dynamictick";
inline constexpr std::string_view kBootDynamicTickOff = "off";  // dinamik tick kapali (disabledynamictick = yes)
inline constexpr std::string_view kBootDynamicTickOn = "on";    // yalniz geri alma/okuma icin (istemci gondermez)

// ADR-040 m.4: VM tam tur kaniti yok -> KAPALI. Acmak yeni ADR notu + kanit dosyasi ister (R-B4 BLOCKER).
inline constexpr bool kBootOptionItemEnabled = false;

// Bilinen sabit BCD ogeleri (tablo). Yeni oge = yeni ADR.
enum class BootElement { DisableDynamicTick };

bool is_boot_option_target(std::string_view target) noexcept;
std::optional<BootElement> boot_element_of(std::string_view target) noexcept;
bool is_boot_option_value(std::string_view v) noexcept;  // "on" | "off"

// Hedef/deger cifti izin listesinde mi (bayraktan bagimsiz): yalniz ("boot:dynamictick", "off").
bool boot_option_pair_allowed(std::string_view target, std::string_view value) noexcept;
// Broker kurali: id BootOptionSet + scope Machine + izinli cift + item_enabled.
//  - admin_operation_allowed bunu item_enabled = true ile cagirir (yalniz bicim: journal'da kalmis kaydin geri alma
//    istegi bayrak sonradan kapansa da reddedilmez).
//  - admin_apply_allowed ayrica kBootOptionItemEnabled'a bakar: bayrak kapaliyken YENI yazma (Apply/Fix) reddedilir.
bool boot_option_admin_allowed(const contracts::SettingOperation& op, bool item_enabled = kBootOptionItemEnabled) noexcept;

// Platform arayuzu (yol/ad/komut almaz; yalniz tablodaki oge). Deger: "on" | "off"; oge yoksa nullopt.
class BootOptionApi {
public:
    virtual ~BootOptionApi() = default;
    virtual contracts::Result<std::optional<std::string>> read(BootElement e) = 0;
    virtual contracts::Status write(BootElement e, std::string_view value) = 0;
    virtual contracts::Status remove(BootElement e) = 0;
};

// Gercek yol yok: her cagri Unsupported doner (fail-closed; okunamayan deger plana girmez).
class UnavailableBootOptionApi final : public BootOptionApi {
public:
    contracts::Result<std::optional<std::string>> read(BootElement e) override;
    contracts::Status write(BootElement e, std::string_view value) override;
    contracts::Status remove(BootElement e) override;
};

// recovery::SettingAdapter sozlesmesi (capture -> apply -> read_back -> restore). Snapshot: existed + eski deger.
// apply_enabled yalniz apply'i kapatir (varsayilan bayrak); testler [MOCK] api ile true verir.
class BootOptionAdapter final : public recovery::SettingAdapter {
public:
    explicit BootOptionAdapter(BootOptionApi& api, bool apply_enabled = kBootOptionItemEnabled)
        : api_(api), apply_enabled_(apply_enabled) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    BootOptionApi& api() noexcept { return api_; }

private:
    BootOptionApi& api_;
    bool apply_enabled_;
};

}  // namespace ef::core::adapters::windows
