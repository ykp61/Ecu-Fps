#pragma once
// T13 platform: Store paketi kaldirma / yeniden kaydetme (Windows.Management.Deployment.PackageManager, WinRT ABI dogrudan; platform/windows/games
// ile ayni combase calisma-zamani yuklemesi) + makine icin "provisioned" okumasi + kurulu/varsayilan tarayici okumasi.
//  - installed_full_name: FindPackagesByUserSecurityIdPackageFamilyName (mevcut kullanici).
//  - remove_for_user: PackageManager.RemovePackageAsync (yalniz mevcut kullanici; tum kullanicilardan silme yok).
//  - register_by_family: IPackageManager5.RegisterPackageByFamilyNameAndOptionalPackagesAsync (provisioned paket icin).
//  - restorable: HKLM ...\Appx\AppxAllUserStore\InboxApplications icinde ailenin tam adi VAR ve ...\Deprovisioned icinde aile YOK
//    (salt okuma; yonetici gerekmez). Okunamazsa hata: paket plana girmez.
// DOGRULANMADI: gercek kaldirma ve yeniden kayit (derleme dogrulandi, cihazda calistirilmadi).
#include "core/adapters/windows/appx_remove.h"
#include "core/perf/debloat_plan.h"

namespace ef::platform::adapters {

class WinAppxApi final : public core::adapters::windows::AppxApi {
public:
    contracts::Result<std::optional<std::string>> installed_full_name(const std::string& family) override;
    contracts::Result<bool> restorable(const std::string& family) override;
    contracts::Status remove_for_user(const std::string& full_name) override;
    contracts::Status register_by_family(const std::string& family) override;
};

// Salt okuma: StartMenuInternet istemcileri + varsayilan https ProgId'si (HKCU UserChoice).
core::perf::BrowserInfo read_browser_info();

}  // namespace ef::platform::adapters
