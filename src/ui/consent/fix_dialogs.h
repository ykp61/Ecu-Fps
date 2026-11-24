#pragma once
// CHK: Oyun Hazirligi "Duzelt" ve diger Evet/Hayir onaylari. Spec 2026-11-12 E: Windows TaskDialog yerine uygulamanin
// kendi cizdigi kip penceresi (MainWindow::run_modal; klavye / ekran okuyucu / yuksek kontrast uyumlu). Metin
// anahtarlari ve yanit anlamlari degismedi. Sahip HWND ana pencere degilse (ana pencere yok / tepside) soru sorulmaz:
// islem baslatilmaz, sessiz varsayilan Hayir'dir.
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ui/shell/modal.h"

namespace ef::ui {

// RDY-05: geri donussuz islemler (Mevcut degeri koru) Warning ile sorulur: kip penceresinin ana dugmesi hata rengindedir.
enum class FixDialogIcon { Info, Warning };

// Evet/Hayir. Ilk odak Hayir (yanlislikla Enter degisiklik yapmaz). Esc/kapat = Hayir.
bool ask_fix(HWND owner, const core::i18n::Catalog& cat, const std::wstring& question, const std::wstring& detail,
             FixDialogIcon icon = FixDialogIcon::Info);

// Spec 2026-11-12 B3: Oyun hazirligi "Duzelt" onayi. Baslik "Uygulansin mi?"; govdenin ilk satiri degisiklik
// ("ad: eski → yeni"; change bossa bulgu cumlesi), ikinci satiri "Geri alinabilir." + islemin mevcut ayrinti metni
// (UAC, yeniden baslatma, ekran kararmasi gibi zorunlu bilgi ayni dizgi anahtarlariyla kalir). Bulgu cumlesi kip
// penceresinin UIA HelpText'inde de okunur. Ilk odak Hayir; Esc/kapat = Hayir.
bool ask_fix_apply(HWND owner, const core::i18n::Catalog& cat, const std::wstring& change, const std::wstring& finding,
                   const std::wstring& detail);
// P12-B1: Panel "Duzelt" — otomatik duzeltilebilen TUM bulgular tek Evet/Hayir'da. Baslik "Uygulansin mi?"; madde listesi her
// ayarin "ad: eski → yeni" satiri; govde "Geri alinabilir." + (UAC, yeniden baslatma, ekran kararmasi gibi) zorunlu ayrinti.
// Ilk odak Hayir; Esc/kapat = Hayir. Ana pencere yoksa sorulmaz.
ModalSpec fix_all_modal_spec(const core::i18n::Catalog& cat, const std::vector<std::wstring>& changes, const std::wstring& detail);
bool ask_fix_all(HWND owner, const core::i18n::Catalog& cat, const std::vector<std::wstring>& changes, const std::wstring& detail);
// Otomatik duzeltmenin "ad: eski → yeni" satiri. Yalniz bulgunun kendisinin soyledigi durumdan uretilir (ornek:
// "Oyun Modu kapali" bulgusu -> "Oyun Modu: Kapali → Acik"; yenileme hizi bulgunun {hz} / {max} parametrelerinden).
// Eski ya da yeni deger bilinmiyorsa (DNS secimi, bilinmeyen duzeltme) bos doner: uydurma deger yazilmaz.
std::wstring fix_change_line(const core::i18n::Catalog& cat, const core::readiness::Check& c);

// EF-X14: DNS hazir listesi secimi ve Evet/Hayir tek pencerede. Varsayilan secim yok; secim yapilmadan Evet kapali.
// Ilk odak Hayir; Esc/kapat = Hayir. Donus: secilen kDnsPresets kimligi; Hayir/hata = nullopt. Baslik ve govde
// ask_fix_apply ile aynidir (yeni deger listeden secilir: ilk govde satiri bulgu cumlesidir).
// preselect (T10): bos degilse ve derlenmis listede varsa o liste secili acilir (Evet yine kullanicinin onayidir).
std::optional<std::string_view> ask_dns_preset(HWND owner, const core::i18n::Catalog& cat, const std::wstring& finding,
                                               const std::wstring& detail, std::string_view preselect = {});

// Ekran degisikligi sonrasi: "Goruntu duzgun mu?" Koru / Geri al. Sure dolarsa (ekran gorunmuyorsa bile)
// false doner ve cagiran eski ayara doner. true yalniz kullanici "Koru"ya basarsa.
bool confirm_display_keep(HWND owner, const core::i18n::Catalog& cat, int seconds);

// Yukaridaki sorularin kip penceresi tanimlari (saf; cizim denetimi ve birim testleri ayni tanimi kullanir).
ModalSpec fix_modal_spec(const core::i18n::Catalog& cat, const std::wstring& question, const std::wstring& detail,
                         FixDialogIcon icon = FixDialogIcon::Info);
ModalSpec fix_apply_modal_spec(const core::i18n::Catalog& cat, const std::wstring& change, const std::wstring& finding,
                               const std::wstring& detail);
ModalSpec dns_modal_spec(const core::i18n::Catalog& cat, const std::wstring& finding, const std::wstring& detail, std::string_view preselect = {});
ModalSpec display_keep_modal_spec(const core::i18n::Catalog& cat, int seconds);

// Spec 2026-11-12 B4 / E: Temizlik onayi — yikici islem (ana dugme hata renginde "Evet, temizle"), govdede
// "N kategori · X silinecek · surucu" ve zorunlu "Temizlik geri alinamaz". Ilk odak Hayir; Esc/kapat = Hayir.
ModalSpec cleanup_modal_spec(const core::i18n::Catalog& cat, std::size_t categories, const std::wstring& size, const std::wstring& root);
bool ask_cleanup(HWND owner, const core::i18n::Catalog& cat, std::size_t categories, const std::wstring& size, const std::wstring& root);
// Baslangic degisiklikleri onayi (mevcut soru ve aciklama dizgileri).
ModalSpec startup_modal_spec(const core::i18n::Catalog& cat);
bool ask_startup(HWND owner, const core::i18n::Catalog& cat);

// R-C4: Oyun sunucusu ping adresi — kip penceresi + tek satir yazi alani (yalniz rakam ve nokta; en cok 15 karakter).
// Evet gecerli bir IPv4 yazilinca etkindir; initial = kayitli adres (bos olabilir). Donus: Evet = kanonik "a.b.c.d";
// Hayir / kapat / ana pencere yok = nullopt (paket gonderilmez). Ilk odak yazi alanindadir.
ModalSpec server_ping_modal_spec(const core::i18n::Catalog& cat, const std::string& initial);
std::optional<std::string> ask_server_address(HWND owner, const core::i18n::Catalog& cat, const std::string& initial);

// Spec 2026-11-12 B7: Destek paketi "Olustur" — kip penceresi; "Cokme kayitlarini da ekle (bellek icerigi
// barindirabilir)" isaret kutusu varsayilan kapali, aciklamasi govdede. Donus: Hayir/kapat = nullopt; Evet = kutunun
// son degeri (cokme kayitlari eklensin mi). Ana pencere yoksa sorulmaz (nullopt).
ModalSpec support_modal_spec(const core::i18n::Catalog& cat);
std::optional<bool> ask_support_bundle(HWND owner, const core::i18n::Catalog& cat);
// Spec 2026-11-12 B7 / E: Uygulamayi kaldir — yikici onay (ana dugme hata renginde "Evet, kaldir"); govdede mevcut onay
// cumlesi ve zorunlu "Veri silme geri alinamaz". Ilk odak Hayir; Esc/kapat = Hayir.
ModalSpec uninstall_modal_spec(const core::i18n::Catalog& cat);
bool ask_uninstall(HWND owner, const core::i18n::Catalog& cat);
// Spec 2026-11-12 H2 / E: surucu kurulumu onayi — baslik "Surucu kurulsun mu?", govdede kurulacak oge (ad · tur · surum ·
// boyut) ve "Yonetici izni istenir..." notu; ana dugme "Evet, kur". Ilk odak Hayir; Esc/kapat = Hayir. UAC ayri OS iznidir.
// admin_without_uac: Windows Update kurulumu (DriverInstall) yardimci hizmetle yurur -> notun "UAC sorulmaz" esi.
ModalSpec driver_install_modal_spec(const core::i18n::Catalog& cat, const std::wstring& what, bool admin_without_uac = false);
bool ask_driver_install(HWND owner, const core::i18n::Catalog& cat, const std::wstring& what, bool admin_without_uac = false);
// ADR-032: resmi ekran karti paketi kurulumu. Govde: paket (ad · boyut · dogrulanan imzalayan) ve "Yonetici izni
// istenir..." notu; baslik "Ekran karti surucusu kurulsun mu?". Ilk odak Hayir; Esc/kapat = Hayir.
ModalSpec vendor_install_modal_spec(const core::i18n::Catalog& cat, const std::wstring& what);
bool ask_vendor_install(HWND owner, const core::i18n::Catalog& cat, const std::wstring& what);

}  // namespace ef::ui
