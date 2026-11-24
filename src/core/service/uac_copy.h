#pragma once
// Sahip raporu 2026-11-13: kurulu 0.3.0'da Performans UAC sormadan acildi ama metin "UAC istenecek" diyordu.
// Yonetici metni gercek yolu soylemeli. ServiceFirstChannel'dan gecen ve hizmetin yurutebildigi (service_handles)
// islerin metinlerinin ".svc" esi vardir: "Yonetici yetkisi gerekir; yardimci hizmet uygular, UAC sorulmaz." Ayni olgular,
// yalniz UAC sozu yok. Her zaman UAC rolu kullanan isler (temizlik yardimcisi, oyunda yakalama, kurulum/kaldirma,
// "Dosyadan kur" = VendorInstall) ve "izin verilmedi" bildirimleri hic degismez. Davranis degismez; yalniz metin secimi.

#include <cstddef>
#include <string>
#include <string_view>

#include "core/i18n/catalog.h"
#include "core/ipc/protocol.h"

namespace ef::core::service {

// Eslenen metin -> hizmet esi ve metnin anlattigi kanal istegi (hepsi service_handles(action) == true olmali).
struct AdminCopy {
    i18n::StringId uac;
    i18n::StringId svc;
    ipc::RequestAction action;
};
const AdminCopy* admin_copy_table(std::size_t& count) noexcept;

// admin_without_uac: kurulu kopya + yonetici hesabi + hizmet SCM'de calisiyor ve dogru yapilandirilmis (UI yoklamasi).
// Esi olmayan (UAC-yalniz) metin her iki durumda aynen doner.
i18n::StringId admin_copy(i18n::StringId id, bool admin_without_uac) noexcept;
// Anahtar bicimi (plan grubu aciklamalari anahtarla gelir). Bilinmeyen / esi olmayan anahtar aynen doner.
std::string admin_copy_key(std::string_view key, bool admin_without_uac);

}  // namespace ef::core::service
