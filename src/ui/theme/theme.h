#pragma once
// UI-01: tasarim token'lari. Kaynak design/tokens.json (derleme zamaninda gomulur); kodda renk/olcu
// sabiti tekrar yazilmaz. Birim DIP. Platformdan bagimsiz (birim testlenir).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"

namespace ef::ui {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static std::optional<Color> from_hex(std::string_view hex);  // "#RRGGBB"
    Color with_alpha(float alpha) const { return {r, g, b, alpha}; }
    friend bool operator==(const Color&, const Color&) = default;
};
Color mix(const Color& a, const Color& b, float t);  // t=0 -> a, t=1 -> b

// WCAG 2.x goreli parlaklik ve kontrast orani (1..21).
double relative_luminance(const Color& c);
double contrast_ratio(const Color& a, const Color& b);
// M04: c'yi gerekirse 'toward' rengine dogru karistirarak bg uzerinde en az 'target' kontrasta getirir (WCAG 1.4.11
// metin disi ogeler icin 3:1). Ulasilamazsa 'toward' doner.
Color ensure_contrast(const Color& c, const Color& bg, const Color& toward, double target);

struct Palette {
    Color background, sidebar, surface, surface_raised, border, text_primary, text_secondary, accent, on_accent, violet,
        success, warning, error;
    // Klavye odak halkasi (tokens colors.focus_ring; yoksa accent). Secim cercevesinden ayri renktedir (TH-02) ve
    // tum yuzeylerde >= 3:1 kontrast tasir (WCAG 1.4.11; birim testte dogrulanir).
    Color focus_ring;
    // Zehir yesili: yogun kullanim rampasi (orange), yukleme/RGB serit (cyan), Performans karti zemini (hero).
    // Istege bagli token'lar; yoksa sirasiyla warning / accent / surface.
    Color orange, cyan, hero;
    // Veri rolleri (spec 2026-11-12 F): yesil yalniz marka, ana dugme ve "iyi" durumu icindir. CPU serileri kehribar,
    // GPU serileri / kare hizi camgobegi, ag indirme camgobegi / yukleme mor, notr bilgi gri. Istege bagli token'lar
    // (colors.data_*); yoksa sirasiyla warning / cyan / cyan / violet / text_secondary. Yuksek kontrastta sistem metin
    // rengine duser (anlam kelimeyle tasinir).
    Color data_cpu, data_gpu, data_net_down, data_net_up, data_info;
};

enum class Weight { Regular = 400, Medium = 500, Semibold = 600, Bold = 700 };
struct TypeRole {
    float size = 14, line = 20;
    Weight weight = Weight::Regular;
    // BOOST Panel rolleri (sahip onayli maket 2026-11-16): mono = gomulu JetBrains Mono (.mono; yedek Cascadia Mono /
    // Consolas); tracking = harf araligi DIP (tracking-wider 0.05em); css = taban cizgisi CSS satir kutusundaki gibi
    // (yarim satir payi + font ascent): maketteki kutu konumu aynen kullanilabilir.
    bool mono = false;
    float tracking = 0;
    bool css = false;
};
// Label: bolum etiketi (11 DIP, yari kalin; metni cagiran buyuk harfle verir). Token type.label istege baglidir.
// Hero: sonuc kartinin buyuk ortalama FPS sayisi (B15; token type.hero istege bagli, yoksa 38/44 yari kalin).
// Hero'dan sonrakiler token degildir: Panel'in maket olculeri (Tailwind text-[Npx], satir 1.5 x boyut; font-medium 500,
// font-semibold 600, font-bold 700). Mono* = .mono (JetBrains Mono); Caps11 = 11 yari kalin buyuk harf etiketi
// (tracking-wider); Num* = kalin sayi; Medium* / Small* / Semi13 / Title14 = Segoe UI Variable.
enum class Role {
    PageTitle, StateTitle, Section, Body, Secondary, Metric, Label, Hero,
    Mono9, Mono10, Mono11, Mono11Semi, Mono12Semi, Caps11, Small11, Small12, Medium10, Medium11, Medium12, Medium13, Semi12, Semi13,
    Title14, Num20, Num22, Num24,
    Small13, Semi18, Num18, Semi20, Mono12,  // Semi20: 20px/600 (Olcumler); Mono12: .mono 12px (Olcumler basligi)
    // Oyun hazirligi (BOOST maketi view-readiness): 13px aciklama, 18px/600 ozet, 18px/700 skor
    Count_
};
inline constexpr std::size_t kTokenRoleCount = static_cast<std::size_t>(Role::Hero) + 1;  // tokens.json'dan okunan roller

struct Motion {
    int hover = 80, press = 60, toggle = 120, page = 140, details = 160, dialog_open = 160, dialog_close = 100,
        status = 120, logo_reveal = 160, live_value = 400;
    // Spec 2026-11-12 F: deger gecisi (LED parcalari, cubuklar), sayi sayaci, Performans halkasinin nefes dongusu.
    int value_ease = 250, count = 300, breathe_period = 3600;
    // Spec 2026-11-12 B.1: Performans kapaliyken yuvarlak dugmenin kalp atisi dongusu (cift vurus).
    int heartbeat_period = 1500;
    // Spec 2026-11-12 B2: oyun karti fareyle uzerine gelinince yukselir (card_lift ms); "Calisiyor" noktasi nabiz
    // atar (pulse_period: bir tam dongu).
    int card_lift = 200, pulse_period = 1600;
    // Spec 2026-11-12 B3: Oyun hazirligi halkasinin yayi ilk gosterimde oranina dolar.
    int ring_fill = 600;
    // Spec 2026-11-12 C: Olcumler Sonuc karti sayilari acilista sayac gibi dolar (<= 700 ms); darbogaz cubuklari dolar.
    int result_count = 700, bars_fill = 700;
    // Spec 2026-11-12 B4 / B6: Temizlik halkasi secim degisince doner; Gecmis satiri acilir; karsilastirma cubuklari dolar.
    int cleanup_ring = 500, row_expand = 250, compare_bars = 500;
};

// DS-08 notu: colors.tray_*/high_contrast_* token'lari tepsi ikonu ve EXE kaynak hattinin (assets/branding)
// girdisidir; calisma zamaninda Palette'e okunmaz. radii.dialog ve motion.dialog_open/close uygulamanin kendi
// cizdigi kip penceresi icindir (spec 2026-11-12 E; onay pencereleri artik OS TaskDialog degil; bkz. theme.cpp).
struct Theme {
    Palette dark;  // UI-14: uygulama tek tema kullanir (yuksek kontrast ayri sistem paleti)
    std::array<TypeRole, static_cast<std::size_t>(Role::Count_)> type{};
    std::vector<std::string> font_families;
    std::vector<float> spacing;
    float radius_control = 6, radius_card = 6, radius_dialog = 12;
    // Siki yerlesim (spec 2026-11-11 §2): dugme 32 (satir ici 28), asgari genislik 72; anahtar 40x22, Performans
    // seridinde 52x28. touch_min = WCAG 2.5.8 asgari dokunma hedefi (24); control_max_h = tek bir denetimin en buyuk
    // yuksekligi (satirin tamami dugme olmasin; cizim denetimi bununla olcer).
    float toggle_w = 40, toggle_h = 22, hero_toggle_w = 52, hero_toggle_h = 28;
    float button_h = 32, button_row_h = 28, button_min_w = 72, touch_min = 24, control_max_h = 44;
    // Yogunluk (design/tokens.json density.*): sayfa kenar boslugu, kartlar arasi bosluk, kart ic boslugu, tek satir
    // baslik seridi, tablo satiri (tek / iki satir), icerik azami genisligi (govde bu genislikte ORTALANIR).
    float page_margin = 16, card_gap = 10, card_pad = 12, header_h = 36, row_h = 34, row2_h = 48, content_max_w = 1280;
    // DS-13: bilesen geometrisi token'lari (design/tokens.json geometry.*).
    float live_ring_d = 88, live_ring_thick = 8, power_ring_thick = 4, scrollbar_thumb_w = 4, scrollbar_inset = 8;
    // Panel yuvarlak Performans dugmesi (spec 2026-11-12 B.1; geometry.power_button_diameter). control_max_h'nin
    // bilincli istisnasidir: tek buyuk ana eylem.
    float power_button_d = 92;
    // Olcumler Test karti yuvarlak baslat dugmesi (spec 2026-11-12 C; geometry.test_button_diameter). control_max_h'nin
    // bilincli istisnasi (tek buyuk ana eylem).
    float test_button_d = 70;
    // Spec 2026-11-12 B2 (geometry.card_lift / geometry.status_dot): oyun kartinin hover yukselmesi (DIP) ve durum
    // cipindeki "Calisiyor" noktasinin capi.
    float card_lift = 2, status_dot = 6;
    float focus_ring_w = 2;  // controls.keyboard_focus_ring_width (DS-09)
    float sidebar_min = 192, sidebar_max = 208, min_window_w = 960, min_window_h = 540;
    Motion motion;
    std::array<float, 4> easing{0.16f, 1.f, 0.3f, 1.f};
    bool continuous_idle_animation = false;
    // Zehir yesili ust menu: serit yuksekligi (geometry.topbar_h), RGB serit kalinligi (geometry.strip_h), serit
    // dongu suresi (motion_ms.strip_period) ve serit hareketi (controls.strip_animation). Serit hareketi
    // continuous_idle_animation'dan ayri yonetilir; azaltilmis hareket/HC/oyun durumunda durur.
    float topbar_h = 52, strip_h = 3;
    // Ust menu sekmesi yuksekligi (seritte dikey ortali) ve logo: amblemin gorunen yuksekligi (dar modda yalniz
    // amblem) ile yatay logonun genisligi.
    float topbar_tab_h = 36, logo_mark_h = 30, logo_lockup_w = 160;
    int strip_period_ms = 8000;
    bool strip_animation = true;
    // ADR-033 / spec 3: kurulum penceresi (design/tokens.json setup.*): sabit pencere boyu, kenar boslugu, lockup kutusu,
    // satir yuksekligi, indirme cubugu kalinligi ve adim noktasi capi.
    struct SetupTokens {
        float window_w = 480, window_h = 560, margin = 24, logo_w = 200, logo_h = 40, row_h = 44, progress_h = 6, step_dot = 10;
    } setup;

    const TypeRole& role(Role r) const { return type[static_cast<std::size_t>(r)]; }
    float space(std::size_t i) const { return i < spacing.size() ? spacing[i] : 4.f * static_cast<float>(i + 1); }
};

contracts::Result<Theme> parse_theme(std::string_view json_text);
// Gomulu design/tokens.json (gecersizse derleme/test yakalar; burada varsayilanlar doner).
const Theme& default_theme();

// Denetim 5.1: Windows Erisilebilirlik > Metin boyutu (TextScaleFactor, %100-%225). Tip rolleri (boyut + satir) ve metni
// tasiyan denetim yukseklikleri ayni oranla buyur; bosluklar, ikonlar ve pencere asgarisi degismez. Oran
// kMaxTextScale ile sinirlidir: asgari pencerede (960x540) ve dar govdede yerlesimin dogrulandigi en buyuk oran budur
// (ui_snapshot EF_TEXT_SCALE=1.25: 0 tasma). %150 denendi: sabit olculu yerlesimlerde yuzlerce tasma; Windows %125
// ustundeyse uygulama %125'te kalir. 1.0 = degisiklik yok.
inline constexpr float kMaxTextScale = 1.25f;
float clamp_text_scale(float factor) noexcept;
Theme with_text_scale(const Theme& base, float factor);
std::string_view embedded_tokens_json() noexcept;

}  // namespace ef::ui
