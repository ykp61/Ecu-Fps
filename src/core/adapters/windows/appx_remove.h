#pragma once
// ADR-035 / T13: yeniden kaydedilebilir medya uygulamalari. Derlenmis listedeki Store paketi YALNIZ bu kullanici icin kaldirilir
// (PackageManager.RemovePackageAsync; tum kullanicilardan silme, Edge/OneDrive kaldirma, sistem paketi yok). Hedef "appx:<PackageFamilyName>",
// deger yalniz "removed". UAC yok, broker yok (kullanici kapsami).
// Degismez (ADR-035/6): geri alinabilirligi dogrulanamayan adim plana girmez. restorable() = paket makine icin PROVISIONED
// (inbox gorsellerinde kayitli) ve "deprovisioned" degil; yalniz "staged" yetmez (Windows son kullanici kaldirinca stage'i temizler).
//  - capture: kurulu tam ad + restorable()==true; aksi halde reddedilir (yakalanmayan adim uygulanmaz).
//  - apply  : restorable() yeniden dogrulanir, sonra yalniz bu kullanici icin kaldirilir.
//  - read_back: "removed" (kurulu degil) / "installed".
//  - restore: aile adiyla yeniden kaydet (register_by_family) ve kurulu oldugunu dogrula; basarisizsa hata (motor RecoveryRequired yoluna alir).
// Gercek kaldirma/yeniden kayit bu PC'de DOGRULANMADI; birim testleri sahte AppxApi kullanir.
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kAppxTargetPrefix = "appx:";
inline constexpr std::string_view kAppxRemovedValue = "removed";
inline constexpr std::string_view kAppxInstalledValue = "installed";

struct AppxPackage {
    std::string_view family;        // PackageFamilyName (Ad_YayinciKimligi)
    std::string_view display_name;  // onay satirinda gorunen urun adi (yerellestirilmez)
};
// Derlenmis liste (Clipchamp, Groove/Muzik, Filmler ve TV, Solitaire, Haberler, Hava Durumu).
std::span<const AppxPackage> appx_packages() noexcept;
const AppxPackage* find_appx_package(std::string_view family) noexcept;

std::string appx_target_for_family(std::string_view family);                  // listede degilse bos
std::optional<std::string> appx_family_from_target(std::string_view target);  // liste + bicim gecersizse nullopt

class AppxApi {
public:
    virtual ~AppxApi() = default;
    // Bu kullanici icin kurulu paketin tam adi; kurulu degilse nullopt (hata: okunamadi).
    virtual contracts::Result<std::optional<std::string>> installed_full_name(const std::string& family) = 0;
    // Makine icin provisioned (yeniden kaydedilebilir) mi. Okunamazsa hata: plana girmez.
    virtual contracts::Result<bool> restorable(const std::string& family) = 0;
    virtual contracts::Status remove_for_user(const std::string& full_name) = 0;
    virtual contracts::Status register_by_family(const std::string& family) = 0;
};

class AppxRemoveAdapter final : public recovery::SettingAdapter {
public:
    explicit AppxRemoveAdapter(AppxApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    AppxApi& api_;
};

class FakeAppxApi final : public AppxApi {
public:
    contracts::Result<std::optional<std::string>> installed_full_name(const std::string& family) override;
    contracts::Result<bool> restorable(const std::string& family) override;
    contracts::Status remove_for_user(const std::string& full_name) override;
    contracts::Status register_by_family(const std::string& family) override;
    // family -> tam ad (kurulu); provisioned kumesi.
    std::vector<std::pair<std::string, std::string>> installed;
    std::vector<std::string> provisioned;
    bool fail_read = false, fail_restorable = false, fail_remove = false, fail_register = false;
    bool register_is_noop = false;  // kayit "basarili" doner ama paket kurulmaz (dogrulama basarisiz)
    int removes = 0, registers = 0;
};

}  // namespace ef::core::adapters::windows
