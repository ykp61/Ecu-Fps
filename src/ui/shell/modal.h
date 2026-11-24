#pragma once
// Spec 2026-11-12 E: uygulama ici kip penceresi (onay / bildirim). Windows sinifi pencereler (TaskDialog / MessageBox)
// yerine uygulamanin kendi cizdigi kart: icerik karartilir, ortada baslik, en cok iki govde satiri, varsa madde
// listesi, varsa tek isaret kutusu, sag altta ikincil "Hayir" ve ana "Evet". Bu dosya durum makinesidir (saf; pencere
// ve cizim yok): odak sirasi, Esc = Hayir, Enter / Bosluk yalniz odaktaki ogeyi calistirir, ilk odak Hayir'dadir, Tab
// kartin icinde doner. Cizim Screen::modal_layer, ic ice ileti dongusu MainWindow::run_modal icindedir.

#include <cstddef>
#include <string>
#include <vector>

namespace ef::ui {

inline constexpr std::size_t kModalMaxBody = 2;      // govde satiri (paragraf) ust siniri
inline constexpr std::size_t kModalMaxList = 12;     // madde listesi ust siniri (UIA modal.list.N)
inline constexpr std::size_t kModalMaxChoices = 12;  // secenek ust siniri (UIA modal.choice.N)

struct ModalSpec {
    std::wstring title;                // soru / baslik (sarilir)
    std::vector<std::wstring> body;    // en cok kModalMaxBody satir; fazlasi son satira eklenir (metin kaybolmaz)
    std::vector<std::wstring> list;    // madde listesi (salt okunur)
    // Tek secimli liste (EF-X14 DNS hazir listesi). Bos degilse secim yapilmadan "Evet" devre disidir.
    std::vector<std::wstring> choices;
    int initial_choice = -1;          // acilista secili secenek (-1 = yok; Evet secim yapilinca etkin)
    std::wstring checkbox;             // bos = isaret kutusu yok
    bool checkbox_checked = false;     // baslangic degeri
    // R-C4: tek satir yazi alani (oyun sunucusu adresi). input_label bos = alan yok. Yalniz input_allowed icindeki
    // karakterler yazilir (en cok input_max); ekleme sondan, Geri sil = son karakter, Delete = tumunu sil, Ctrl+V = yapistir.
    // Evet, metin input_valid'den (yoksa: bos degil) gecince etkindir. Ilk odak alandadir (yazmaya hemen baslanir).
    std::wstring input_label;
    std::wstring input_hint;           // bos alanda soluk ornek
    std::wstring input_text;           // baslangic metni
    std::wstring input_allowed;        // izinli karakterler; bos = hepsi (yazdirilabilir)
    std::size_t input_max = 64;
    bool (*input_valid)(const std::wstring&) = nullptr;
    std::wstring yes, no;              // dugme etiketleri (ana / ikincil)
    bool destructive = false;          // yikici islem: ana dugme hata renginde
    // > 0: geri sayim. body icindeki "{s}" kalan saniyeyle degisir; sure dolunca yanit Hayir'dir (dismissed).
    int countdown_s = 0;
    // Ekranda gorunmeyen pencere basligi (eski TaskDialog basligi): kip penceresinin UIA HelpText'inde okunur.
    std::wstring caption;
};

enum class ModalAnswer { No, Yes };
struct ModalResult {
    ModalAnswer answer = ModalAnswer::No;
    // Hayir dugmesine basilmadan kapandi: Esc, pencere kapatildi, sure doldu ya da pencere gosterilemedi (ana
    // pencere yok / tepside). answer her zaman Hayir'dir; "karar verilmedi" ayrimi gereken cagiran buna bakar.
    bool dismissed = false;
    bool checked = false;  // isaret kutusunun son degeri
    int choice = -1;       // secilen secenek (yoksa -1)
    std::wstring text;     // yazi alaninin son metni (alan yoksa bos)
    friend bool operator==(const ModalResult&, const ModalResult&) = default;
};

enum class ModalItem { None, Input, Choice, Check, No, Yes };
struct ModalFocus {
    ModalItem item = ModalItem::None;
    std::size_t index = 0;  // yalniz Choice
    friend bool operator==(const ModalFocus&, const ModalFocus&) = default;
};

struct ModalState {
    ModalSpec spec;
    ModalFocus focus{ModalItem::No, 0};  // ilk odak Hayir
    bool checked = false;
    int choice = -1;
    int seconds_left = 0;
    std::wstring text;  // yazi alani metni
    bool done = false;
    ModalResult result;
};

enum class ModalKey { Tab, ShiftTab, Enter, Space, Escape, Up, Down, Left, Right };

// Spec'i sinirlara indirger (govde / liste / secenek ust sinirlari) ve acilis durumunu kurar: odak Hayir'da.
ModalState modal_open(ModalSpec spec);
// "Evet" etkin mi: secim listesi varsa secim yapilmis olmali.
bool modal_yes_enabled(const ModalState& m);
// Tab sirasi (kartin icinde doner): [secim listesi tek durak] [isaret kutusu] Hayir [Evet]. Devre disi Evet atlanir.
std::vector<ModalFocus> modal_focus_order(const ModalState& m);
// Klavye. Esc = Hayir (dismissed); Enter / Bosluk yalniz odaktaki ogeyi calistirir (ortuk varsayilan dugme yok);
// Tab / Shift+Tab kartin icinde doner; ok tuslari secim listesinde gezer ve secer. Bitmis pencerede etkisiz.
void modal_key(ModalState& m, ModalKey k);
// Fare / UIA: ogeyi calistirir (odak o ogeye gecer). Devre disi Evet ve liste disi indeks etkisizdir.
void modal_activate(ModalState& m, ModalFocus item);
// Geri sayim: 1 saniye gecti. Sure dolunca Hayir (dismissed). Geri sayimsiz pencerede etkisiz.
void modal_tick(ModalState& m);
// Pencere kapatildi / gosterilemedi: Hayir (dismissed).
void modal_dismiss(ModalState& m);
// R-C4: yazi alani. Alan yoksa hepsi etkisizdir. Karakter izinli degilse / sinir doluysa yok sayilir (true = degisti).
bool modal_has_input(const ModalState& m);
bool modal_input_char(ModalState& m, wchar_t c);
bool modal_input_backspace(ModalState& m);
bool modal_input_clear(ModalState& m);
// Yapistirma: izinli karakterler sirayla eklenir (bosluk/satir sonu gibi izinsizler atilir); en az bir karakter eklendiyse true.
bool modal_input_paste(ModalState& m, const std::wstring& text);
// Gosterilecek govde satiri ("{s}" kalan saniyeyle degistirilmis).
std::wstring modal_body_text(const ModalState& m, std::size_t line);
// Pencere gosterilemediginde (ana pencere yok / tepside) donen sessiz varsayilan: Hayir, dismissed.
inline ModalResult modal_not_shown() { return {ModalAnswer::No, true, false, -1}; }

// Kip penceresini kapatan tus (Enter / Bosluk / Esc) basili tutulursa tus tekrarlari sayfaya (ya da alttaki kip
// penceresine) ulasmaz: odak pencereyi acan ogeye dondugu icin tekrar ayni soruyu yeniden acardi, Esc tekrari da
// sayfadaki baska bir isi iptal ederdi. Koruma, tus birakilip yeniden basilinca ya da baska bir tusta biter.
struct ModalKeyGuard {
    unsigned vk = 0;  // kip penceresini kapatan sanal tus kodu; 0 = koruma yok
    void closed_by(unsigned key) { vk = key; }
    // Gelen tus basisi yutulsun mu (repeat: tus tekrari). Yutulmayan ilk basis korumayi kaldirir.
    bool swallow(unsigned key, bool repeat) {
        if (vk == 0) return false;
        if (key == vk && repeat) return true;
        vk = 0;
        return false;
    }
};

// Sayfada (kip penceresi disinda) Enter / Bosluk yalniz ilk basista odaktaki ogeyi calistirir; basili tutulan tusun
// tekrarlari ayni islemi yeniden baslatmaz (kip penceresiyle ayni kural).
inline bool page_activation_accepted(bool repeat) { return !repeat; }

// Fare tekerlegi: Windows "bir centikte kaydirilacak satir" ayari (SPI_GETWHEELSCROLLLINES) uygulanir. Varsayilan 3 satir
// eskisi gibi centik basina 48 DIP verir (satir = 16 DIP). WHEEL_PAGESCROLL (0xFFFFFFFF) = centik basina bir sayfa
// (PgDn ile ayni 400 DIP). 0 satir = kaydirma yok. Donus: icerigin asagi kaydirilacagi DIP (pozitif = asagi).
inline float wheel_scroll_dip(int wheel_delta, unsigned spi_lines) {
    constexpr float kLineDip = 16.f, kPageDip = 400.f;
    const float notches = static_cast<float>(wheel_delta) / 120.f;
    const float per = spi_lines == 0xFFFFFFFFu ? kPageDip : static_cast<float>(spi_lines > 100 ? 100 : spi_lines) * kLineDip;
    return -notches * per;
}

}  // namespace ef::ui
