#pragma once
// ADR-032 platform: resmi ekran karti paketi. Bu klasor, kurulum paketini SUREC OLARAK calistiran tek yerdir
// (tests/windows/check_no_install.ps1 baska yerde CreateProcess + paket yolu birlesimini kabul etmez).
//  - UI (yukseltilmemis): kullanicinin sectigi dosya %LOCALAPPDATA%\EcuFps\drivers\<id>.part'a kopyalanir, tamamlaninca
//    <id>.exe olur; imza + imzalayan on denetlenir (yalniz geri bildirim; karar yardimcidadir).
//  - Yardimci (yukseltilmis): <kullanici klasoru>\<id>.exe yalniz-okuma paylasimiyla acilip makine journal kokunun
//    "vendor_pkg" alt klasorune (yonetici ACL'i) kopyalanir; imza ve imzalayan KOPYA uzerinde yeniden dogrulanir; paket
//    resmi sessiz anahtarlarla calistirilir (pencere yok, kabuk yok, ortam degiskeni / arguman kullanicidan gelmez).
// Ag yok: imza zinciri cevrimdisi dogrulanir (iptal denetimi yalniz onbellek).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/drivers/vendor_install.h"

namespace ef::platform::vendor {

// %LOCALAPPDATA%\EcuFps\drivers (yoksa nullopt). create: klasoru olusturur.
std::optional<std::filesystem::path> user_package_dir(bool create);
// 16 kucuk onaltilik rakam (BCryptGenRandom).
std::string new_package_id();
// Yarim kalmis kopyalar (*.part) ve onceki oturumdan kalan paketler silinir (yalniz bu klasorde, yalniz <16 hex> adlar).
void clear_package_dir();
void remove_package(std::string_view id);

// Dosyanin Authenticode imzasi (WinVerifyTrust, cevrimdisi) ve imzalayan sertifikanin konu adi.
core::drivers::SignatureCheck verify_signer(const std::filesystem::path& file);

struct ImportRun {
    core::drivers::CopyOutcome outcome = core::drivers::CopyOutcome::ReadFailed;
    std::uint64_t bytes = 0;
};
// Kullanicinin sectigi dosyayi <dir>\<id>.part'a kopyalar (boyut siniri, iptal), tamamlaninca <id>.exe yapar. Basarisiz /
// iptal edilen kopya silinir.
ImportRun import_package(const std::filesystem::path& source, std::string_view id, const std::function<bool()>& stop,
                         const std::function<void(std::uint64_t, std::uint64_t)>& progress);

// Yardimci arabirimi. machine_dir: dogrulanmis makine journal koku. restore_point: geri yukleme noktasi istegi.
std::unique_ptr<core::drivers::VendorInstallApi> make_vendor_install(std::filesystem::path machine_dir,
                                                                     std::function<core::drivers::RestorePointState()> restore_point);

}  // namespace ef::platform::vendor
