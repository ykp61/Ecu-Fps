#pragma once
// T11 (ADR-035): secmeli Microsoft Defender tarama dislamasi (oyun klasoru). Defender KAPATILMAZ; yalniz secilen oyun klasorleri
// taranmaz (guvenlik odunu). Hedef "defx:<hex>" (hex = kanonik klasor yolunun utf-8 baytlari), deger yalniz "1".
//  - Yakalama: yol zaten dislanmis miydi (Defender 2021'den beri dislama listesini yonetici olmayana gizler; bu yuzden yakalama,
//    geri okuma ve yazma BROKER'da yapilir). Onceden dislanmis yol Kapali'da KALDIRILMAZ; yalniz bizim ekledigimiz kaldirilir.
//  - Yazma/geri okuma yalniz Defender etkin antivirus ise ve yerel dislamalar gecerliyse (usable()==true) yapilir; ucuncu taraf
//    antivirus (Defender pasif) veya ilke ile yonetilen dislamalarda yerel dislama yok sayilir, geri okuma yalan soylerdi.
//  - Yol bicimi dogrulanir: surucu kokunden en az 2 seviye asagida, kok/Windows/profil kokleri/steamapps\common koku yok.
//  - Uygulama tarafinda ayrica yol, launcher kaynakli oyun kurulum klasorlerinden biri olmali (defender_path_allowed).
// Gercek WMI cagrisi ve Tamper Protection davranisi DOGRULANMADI; birim testleri sahte DefenderApi kullanir.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kDefenderTargetPrefix = "defx:";

class DefenderApi {
public:
    virtual ~DefenderApi() = default;
    // Dislanmis yollar. Yonetici degilse Defender listeyi gizler: bu durumda hata/Denied dondurulur, bos liste UYDURULMAZ.
    virtual contracts::Result<std::vector<std::string>> exclusion_paths() = 0;
    virtual contracts::Status add_path(const std::string& path) = 0;
    virtual contracts::Status remove_path(const std::string& path) = 0;
    // true: Defender etkin antivirus ve yerel dislamalar gecerli; false: gecerli degil; nullopt: bilinmiyor.
    virtual std::optional<bool> usable() = 0;
};

// Kanonik yol: "/" -> "\", ASCII kucuk harf, sonda ayirici yok.
std::string defender_canonical_path(std::string_view path);
// Yol bicimi (broker ile paylasilir): mutlak surucu yolu, guvenli bilesenler, kok/sistem/profil/steamapps\common kokleri degil.
bool defender_path_shape_ok(std::string_view path);
// Uygulama kurali: bicim + yol launcher kaynakli kurulum klasorlerinden birine TAM esit.
bool defender_path_allowed(std::string_view path, const std::vector<std::string>& trusted_game_roots);

// "defx:<hex>" hedefi; bicim gecersizse bos dize / nullopt. parse kanonik yolu verir.
std::string defender_target_for_path(std::string_view path);
std::optional<std::string> defender_path_from_target(std::string_view target);

class DefenderExclusionAdapter final : public recovery::SettingAdapter {
public:
    explicit DefenderExclusionAdapter(DefenderApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    DefenderApi& api_;
};

// Broker izni: Machine kapsam, "defx:<hex>" bicimi + gecerli yol bicimi, deger "1".
bool defender_admin_allowed(const contracts::SettingOperation& op) noexcept;

class FakeDefenderApi final : public DefenderApi {
public:
    contracts::Result<std::vector<std::string>> exclusion_paths() override;
    contracts::Status add_path(const std::string& path) override;
    contracts::Status remove_path(const std::string& path) override;
    std::optional<bool> usable() override { return usable_value; }
    std::vector<std::string> paths;  // sahte dislama listesi (oldugu gibi saklanir)
    std::optional<bool> usable_value = true;
    bool deny_list = false, deny_write = false;
    int adds = 0, removes = 0;
};

}  // namespace ef::core::adapters::windows
