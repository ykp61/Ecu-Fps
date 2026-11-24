#pragma once
// F-32/F-33/F-42 (windows_write dilim 3): etkin guc planinin AC alt ayarlari.
//   power:boost:ac    Islemci performans artirma modu (PERFBOOSTMODE)      hedef 1 = Enabled
//   power:parking:ac  Cekirdek parki alt siniri (CPMINCORES)               hedef 100 = park kapali
//   power:aspm:ac     PCI Express baglanti durumu guc yonetimi (ASPM)       hedef 0 = Off
//   power:usbss:ac    USB secici askiya alma (USB selective suspend)         hedef 0 = Disabled (B1; varsayilan Hayir)
// Kurallar (katalog): yalniz AC degerine yazilir, DC (pil) degerine DOKUNULMAZ. Deger etkin plana yazilir;
// geri alma, anlik goruntude saklanan PLAN GUID'ine yazilir (kullanici sonradan baska plana gecse bile
// dogru plan geri yuklenir). Okuma yukseltme istemez; YAZMA yalniz yukseltilmis broker'da (makine kapsami).
// Deger planda yoksa/okunamiyorsa plana giremez ve uygulama reddedilir (varsayilan uydurulmaz).
// Boost modu yalniz "Efficient*" (3/4) degerinden "Enabled" (1) yonunde degistirilir; kullanici/OEM
// tarafindan bilerek kapatilmis (0) veya zaten performans odakli (1/2/5/6) deger ezilmez.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kPowerBoostAcTarget = "power:boost:ac";
inline constexpr std::string_view kPowerParkingAcTarget = "power:parking:ac";
inline constexpr std::string_view kPowerAspmAcTarget = "power:aspm:ac";
// B1 (rakip eksik analizi): USB secici askiya alma, yalniz AC. Alt grup 2a737441-..., ayar 48e6b7a6-...; 0 = devre disi.
inline constexpr std::string_view kPowerUsbSelectiveSuspendAcTarget = "power:usbss:ac";

inline constexpr std::uint32_t kPowerBoostEnabled = 1;    // PERFBOOSTMODE: Disabled=0 ... Enabled=1
inline constexpr std::uint32_t kPowerParkingDisabled = 100;  // CPMINCORES %100 = park algoritmasi kapali
inline constexpr std::uint32_t kPowerAspmOff = 0;         // PCIe link state: Off=0, Moderate=1, Max=2
inline constexpr std::uint32_t kPowerUsbSuspendDisabled = 0;  // USB selective suspend: Disabled=0, Enabled=1

bool is_power_sub_target(std::string_view target) noexcept;
// Hedefin katalog araligindaki gecerli indeksi mi (0-6 / 0-100 / 0-2)?
bool valid_power_sub_index(std::string_view target, std::uint32_t v) noexcept;
bool valid_power_sub_value(std::string_view target, std::string_view v) noexcept;
// Broker izin listesi: yalniz katalogun istedigi hedef deger istemciden kabul edilir.
bool admin_power_sub_value_allowed(std::string_view target, std::string_view new_value) noexcept;

struct PowerSubValue {
    std::string scheme_guid;      // degerin okundugu/yazildigi plan
    std::uint32_t ac_index = 0;   // REG_DWORD ACSettingIndex
};

class PowerSubApi {
public:
    virtual ~PowerSubApi() = default;
    // Etkin planin AC degeri; ayar planda yoksa/okunamiyorsa hata (dokunulmaz).
    virtual contracts::Result<PowerSubValue> read_active_ac(std::string_view target) = 0;
    // Etkin plana AC degeri yaz (yalniz broker'da basarili olur).
    virtual contracts::Status write_active_ac(std::string_view target, std::uint32_t v) = 0;
    // Yakalanan plan GUID'ine AC degeri yaz (geri alma; plan artik etkin olmasa bile).
    virtual contracts::Status write_scheme_ac(const std::string& scheme_guid, std::string_view target, std::uint32_t v) = 0;
    // Saha 2026-11-15: yakalanan plan GUID'indeki AC degeri (geri alma oncesi ve sonrasi dogrulama ayni plani okur).
    virtual contracts::Result<std::uint32_t> read_scheme_ac(const std::string& scheme_guid, std::string_view target) = 0;
};

// Islem: OperationId::PowerSettingSet, scope Machine, hedef yukaridaki uc sabit deger, value kodlu indeks.
// needs_reboot=false: guc plani degeri etkin planda hemen gecerlidir.
class PowerSubAdapter final : public recovery::SettingAdapter {
public:
    explicit PowerSubAdapter(PowerSubApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    // Saha 2026-11-15: geri alma anlik goruntudeki PLANA yazar; okuma da o plandan yapilir. Kullanici arada baska plana
    // gectiyse etkin planin degeri okunup basarili geri alma "dogrulanamadi" (veya dis degisiklik) sayilmaz.
    contracts::Result<std::optional<std::string>> read_for_restore(const contracts::SettingOperation& op,
                                                                   const recovery::Snapshot& snap) override;

    PowerSubApi& api() noexcept { return api_; }

private:
    PowerSubApi& api_;
};

// Testler ve plan kurali icin bellek ici uygulama: plan basina hedef -> indeks.
class FakePowerSubApi final : public PowerSubApi {
public:
    contracts::Result<PowerSubValue> read_active_ac(std::string_view target) override;
    contracts::Status write_active_ac(std::string_view target, std::uint32_t v) override;
    contracts::Status write_scheme_ac(const std::string& scheme_guid, std::string_view target, std::uint32_t v) override;
    contracts::Result<std::uint32_t> read_scheme_ac(const std::string& scheme_guid, std::string_view target) override;

    std::string active = "381b4222-f694-41f0-9685-ff5bb260df2e";
    std::map<std::string, std::map<std::string, std::uint32_t, std::less<>>, std::less<>> schemes;
    bool fail_read = false;
    bool fail_write = false;
    int writes = 0;
};

}  // namespace ef::core::adapters::windows
