#pragma once
// UI-08/UI-11: kullanici tercihleri (kalici, %LOCALAPPDATA%\EcuFps\preferences.json). Saf + JSON.
// Bilinmeyen alanlar yok sayilir; bozuk dosyada varsayilanlarla devam edilir (dosya silinmez).

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"

namespace ef::core::settings {

struct Preferences {
    bool welcome_seen = false;    // ilk acilis karsilamasi gosterildi ve kapatildi
    bool app_animations = true;   // UI-11: uygulama animasyonlari (varsayilan acik; Windows tercihiyle birlikte uygulanir)
    bool strip_enabled = true;    // Zehir yesili: RGB isik seridi hareketi (oyun/test/pil/HC'de yine durur)
    // R-C2: oyun kapaninca salt okuma temizlik taramasi + tepsi bildirimi (silme YOK; silme ayri Evet/Hayir). Varsayilan acik.
    bool cleanup_hint_after_game = true;
    // R-C4: kullanicinin yazdigi oyun sunucusu IPv4 adresi (a.b.c.d; bos = yok). Yalniz bu bilgisayarda saklanir;
    // okunurken core::net::normalize_server_ipv4 ile dogrulanir, gecersizse atilir.
    std::string game_server_ipv4;
    // Spec 2026-11-12 A4: Test ayarlari -> "Canli test ekrani" (varsayilan acik). Istege bagli alan: eski dosyalar okunur.
    bool live_test_view = true;
    bool auto_active = false;     // OPT-06: Performans onayli oyun icin otomatik acildi (cokme sonrasi geri alma icin)
    // UI-10: plan ogesi bazli secim (anahtar: title_key, deger: secili mi). Yoksa varsayilan secili.
    std::map<std::string, bool> plan_item_selected;
    // WIN-10: kullanicinin Baslangic sayfasinda secmis oldugu oturumluk durdurulacak hizmetler
    // (derlenmis izin listesi disi yine plana girmez; Apply aninda yeniden dogrulanir). En fazla 8.
    std::vector<std::string> session_stop_services;
    // Izleyicinin gercekten calisirken gordugu oyun EXE yollari (Steam manifesti vermez; tahmin yok). En fazla 64.
    std::vector<std::string> observed_game_exes;
    // EF-X11: kullanicinin Oyunlar sayfasinda elle ekledigi oyun EXE yollari (en fazla 32, dogrulanmis).
    std::vector<std::string> manual_game_exes;
    // EF-X01: olay gunlugu tarama penceresi (gun). Yalniz 7 veya 30; baska deger okunmaz/yazilmaz (7).
    int incident_days = 7;
    // Denetim 5.2: uygulama dili ("system" | "tr-TR" | "en-US"; core/i18n/catalog.h). Istege bagli alan; eski dosya "system".
    std::string ui_language = "system";
    // Zorunlu guncelleme tekrar oynatma korumasi (spec §2): dogrulanmis en yuksek manifest_version (0 = hic gorulmedi).
    // Yalniz imzasi ve guvenlik denetimleri gecen manifestten sonra artirilir; asla geriye yazilmaz.
    std::uint64_t release_manifest_seen = 0;
    // Spec 2026-11-12 G / ADR-030: Windows ile baslatma (core/settings/autostart.h). autostart_initialized yoksa
    // (ilk calistirma ya da bu ozellikten onceki dosya) acilista varsayilan acik uygulanir. Istege bagli alanlar.
    bool autostart_initialized = false;
    bool start_with_windows = true;
    bool autostart_notice_pending = false;  // ilk acilis bildirimi henuz bir pencerede gosterilmedi
    // EF-X25 T07 (Koruma Modu): son acilista gorulen Windows ana yapi numarasi (0 = hic kaydedilmedi). Degisirse sapma
    // bildirimi "guncelleme sonrasi" der; okunamayan yapi (0) kaydi ezmez. Istege bagli alan: eski dosyalar okunur.
    std::uint32_t last_os_build = 0;
    // P4: bu PC'deki kanit deneyinde (Arena) "Fark yok" hukmune giren ayar gruplari (grup anahtari; en fazla 64, tekrarsiz).
    // Yalniz gorunum vurgusu: onay ekraninda "Ayrintilar" altinda. Secim / plan / uygulama degismez. Istege bagli alan.
    std::vector<std::string> proof_no_effect_groups;

    bool item_selected(const std::string& key) const {
        const auto it = plan_item_selected.find(key);
        return it == plan_item_selected.end() ? true : it->second;
    }

    std::string serialize() const;
    static contracts::Result<Preferences> parse(std::string_view json);
};

// Uyumluluk #26: kurulum penceresi tercih dosyasina yalniz kendi alanlarini yazar. Kayit oncesi diskteki guncel dosya
// okunur; dil, oyunlar, plan secimleri gibi tepsi uygulamasinin alanlari oldugu gibi kalir. Kurulumun alanlari:
// Windows ile baslatma (autostart_*/start_with_windows) ve guncelleme tekrar oynatma korumasi (release_manifest_seen;
// asla geriye yazilmaz: ikisinin buyugu). Baslatma alanlari yalniz kurulum onlari bu kayitta degistirdiyse
// (with_autostart) yazilir; aksi halde tepside o arada degisen secim korunur.
Preferences merge_setup_fields(Preferences on_disk, const Preferences& setup, bool with_autostart);

// Uyumluluk #26 tepsi tarafi: tepsi uygulamasi tum tercihlerini yazar, ama kurulumun alanlarini bayat kopyasiyla geri
// almaz. release_manifest_seen diskteki ile bellekteki degerin buyugudur. Baslatma alanlari (autostart_*,
// start_with_windows) tepsi bunlari son yukleme/kayittan (baseline) beri degistirmediyse diskteki degerden alinir.
Preferences merge_tray_fields(Preferences tray, const Preferences& on_disk, const Preferences& baseline);

}  // namespace ef::core::settings
