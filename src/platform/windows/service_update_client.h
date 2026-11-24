#pragma once
// ADR-033 / spec 7: kurulu UI'nin zorunlu guncellemeyi yardimci hizmete teslim etmesi. UI yalniz tasiyicidir: hizmet
// zarfi, hedefi, surumu, tekrar oynatma tabanini, boyutu ve SHA-256'yi kendisi yeniden dogrular ve EXE'yi UAC'siz degistirir.
//  - Unavailable: hizmet yok/calismiyor/yapilandirma veya PID uyusmuyor/Hello yok (hic veri gonderilmedi). UI
//    "Yardimci hizmeti onar" sunar; guncelleme zorunlu kalir.
//  - Error: hizmet reddetti veya baglanti aktarim sirasinda koptu (hizmet yarim parcayi atar, EXE degismez).
//  - Swapped: EXE degisti; UI yeni EXE'yi --updated-from ile baslatir, saglik sinyalini yeni UI verir.
// Ag kullanilmaz (indirme UI'da onceden yapildi).

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ef::platform {

enum class ServiceUpdateResult { Swapped, Error, Unavailable };

struct ServiceUpdateOutcome {
    ServiceUpdateResult result = ServiceUpdateResult::Unavailable;
    std::string error;  // hizmetin kisa nedeni (gunluk/hata ayiklama; kullaniciya ham gosterilmez)
};

// envelope_json: UI'nin indirdigi imzali zarf (ham). progress(sent, total).
ServiceUpdateOutcome service_update_send(std::string_view envelope_json, std::string_view exe_bytes,
                                         const std::function<void(std::uint64_t, std::uint64_t)>& progress = {});

// Yeni surum UI'si: pencere acildi -> saglik. Hizmet yalniz Swapped + ayni surumde kabul eder ve yeniden baslar.
bool service_update_healthy(std::string_view version);

}  // namespace ef::platform
