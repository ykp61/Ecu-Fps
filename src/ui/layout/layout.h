#pragma once
// UI-01: kabuk yerlesimi (saf hesap, DIP). Piksel donusumu cizim katmaninda olcekle yapilir.

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

#include "ui/theme/theme.h"

namespace ef::ui {

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    RectF inset(float d) const { return {x + d, y + d, w - 2 * d > 0 ? w - 2 * d : 0, h - 2 * d > 0 ? h - 2 * d : 0}; }
    RectF inset(float dx, float dy) const {
        return {x + dx, y + dy, w - 2 * dx > 0 ? w - 2 * dx : 0, h - 2 * dy > 0 ? h - 2 * dy : 0};
    }
};

// System: Sistem araclari sekmesinin "Sistem" alt sayfasi (spec 2026-11-12 B.4; envanter karti + Depolama). Sona
// eklendi: mevcut sayfalarin degerleri (nav kimlikleri) degismez. Updates: Guncellemeler (spec 2026-11-12 H; sona eklendi).
enum class Page { Overview, Readiness, Games, Measurements, Cleanup, Startup, History, Settings, System, Updates, Count_ };
inline constexpr std::size_t kPageCount = static_cast<std::size_t>(Page::Count_);

// Zehir yesili ust menu: gorunur sekme sirasi (Page degerleri degismez). Cleanup yuvasi "Sistem araclari" sekmesidir;
// Startup ve System ust seritte cizilmez (Sistem araclari icindeki alt sekmelerdir). Spec 2026-11-12 B5 / H: sekiz
// sekme, Guncellemeler Olcumler'den sonra.
inline constexpr std::size_t kNavTabCount = 8;
inline constexpr std::array<Page, kNavTabCount> kNavOrder{Page::Overview, Page::Games,   Page::Readiness, Page::Measurements,
                                                          Page::Updates,  Page::Cleanup, Page::History,   Page::Settings};

// Ekranin olctugu metin/logo olculeri (DIP). label_w kNavOrder sirasindadir. Logo olculeri MUREKKEP kutusudur
// (spec 2026-11-12 B5: SVG'nin kendi ic boslugu kirpilir): marka kutusu (ShellLayout::brand) cizilen seklin tam
// sinir kutusudur. Yukseklik 0 verilirse kutu seridin tamamidir.
struct TopbarMetrics {
    std::array<float, kNavTabCount> label_w{};
    float label_w_short_readiness = 0;  // "Hazirlik"
    float lockup_w = 0, lockup_h = 0;   // yatay logo (theme.logo_lockup_w genisligindeki SVG kutusunun murekkebi)
    float mark_w = 0, mark_h = 0;       // yalniz amblem (murekkep yuksekligi theme.logo_mark_h)
    float status_w = 0;                 // durum hapi: nokta + "Performans acik/kapali" (BOOST maketi)
    float status_compact_w = 0;         // sikisik bicim: hapsiz nokta + metin (0 = status_w)
    float status_dot_w = 0;             // yalniz nokta
    float nav_icon_w = 0;               // sekme basina ikon + ikon-etiket boslugu (0 = ikon yok)
};

// Ust menu daralma sirasi (spec 2026-11-12 B5; her adim oncekileri icerir): yatay logo -> yalniz amblem -> sekme ic
// boslugu kuculur (kTopbarTabPad -> kTopbarTabPadMin; mod MarkOnly kalir) -> kisa etiket ("Hazirlik") -> yalniz nokta.
enum class TopbarMode { Full, MarkOnly, ShortLabels, DotOnly };
inline constexpr bool topbar_mark_only(TopbarMode m) { return m != TopbarMode::Full; }
inline constexpr bool topbar_short_labels(TopbarMode m) { return m == TopbarMode::ShortLabels || m == TopbarMode::DotOnly; }

// Ust menude sekme ic boslugu (her yan; butun sekmelerde ayni; BOOST maketi px-3) ve dar pencerede inebilecegi en kucuk
// deger; sekmeler arasi bosluk (maket gap-0.5); logo ile ilk sekme arasindaki en kucuk bosluk. Sekme grubu maketteki
// gibi (justify-between) logo ile durum gostergesinin arasinda ortalanir.
inline constexpr float kTopbarTabPad = 12.f;
inline constexpr float kTopbarTabPadMin = 8.f;
inline constexpr float kTopbarTabGap = 2.f;
inline constexpr float kTopbarBrandGap = 24.f;
// Sekme ikonu (sahip secimi 2026-11-14): etiketin solunda, kenar kNavIconSize DIP; ikon ile etiket arasi kNavIconGap.
inline constexpr float kNavIconSize = 16.f;
inline constexpr float kNavIconGap = 6.f;

struct ShellLayout {
    RectF window, strip, topbar, brand, status, content, header, body;
    std::array<RectF, kPageCount> nav{};  // nav[Page::Startup].w == 0 (ust seritte yok)
    TopbarMode mode = TopbarMode::Full;
    float tab_pad = kTopbarTabPad;  // sekme ic boslugu (her yan); dar pencerede kuculur
    float tab_gap = kTopbarTabGap;  // sekmeler arasi bosluk (sikisik bicimde 0)
    bool status_pill = true;        // durum gostergesi hap icinde (sikisik bicimde nokta + metin)
    bool nav_icons = false;         // sekme ikonlari cizilir (sigmazsa ilk birakilan sey ikonlardir; bkz. fit_topbar)
};

// Ust menu sigdirma kurali (saf; sekme sayisindan bagimsiz: 8. sekme eklendiginde de ayni kural). Genislikler DIP.
struct TopbarWidths {
    float labels = 0;        // butun sekme etiketlerinin toplam genisligi (tam adlar)
    float short_saving = 0;  // kisa etiket kullanilinca kazanilan genislik
    std::size_t tabs = 0;    // sekme sayisi
    float lockup = 0, mark = 0, status = 0, status_dot = 0;
    float status_compact = 0;  // hapsiz durum genisligi (0 = status)
    float icons = 0;  // butun sekme ikonlarinin toplam genisligi (ikon + bosluk); 0 = ikon yok
};
struct TopbarFit {
    TopbarMode mode = TopbarMode::Full;
    float tab_pad = kTopbarTabPad;
    bool fits = true;  // false: en dar adim da sigmadi (asgari pencerenin alti)
    bool icons = false;  // sekme ikonlari cizilir
    bool roomy = true;   // maket bicimi: sekmeler arasi kTopbarTabGap + durum hapi (false: bitisik sekmeler, hapsiz durum)
};
// avail: icerik sutununun genisligi; margin: son sekme ile durum gostergesi arasindaki en kucuk bosluk.
TopbarFit fit_topbar(float avail, float margin, const TopbarWidths& w);

// w/h: istemci alani (DIP). En kucuk pencere boyutunun altinda bile negatif olcu uretmez.
// Siki yerlesim (spec 2026-11-11 §2): serit topbar_h (52); icerik sutunu en fazla content_max_w (1280) genisliginde ve
// pencerede ORTALI, yanlarda en az page_margin. Marka, sekmeler, durum gostergesi, baslik seridi (header, header_h) ve
// govde (body) ayni sutuna hizalanir. content pencere genisligindedir (kaydirma cubugu ve serit zeminleri icin).
ShellLayout compute_shell(float w, float h, const Theme& t, const TopbarMetrics& m);
// Panel govdesi (BOOST maketi: #header-strip gizli, main p-4): sayfa basligi seridi cizilmez; govde ust menunun (ve
// varsa seritlerin) page_margin altindan baslar, alt kenari ayni kalir.
inline RectF overview_body(const ShellLayout& l, const Theme& t) {
    const float top = l.header.y - t.space(1) + t.page_margin;
    return {l.body.x, top, l.body.w, l.body.bottom() > top ? l.body.bottom() - top : 0.f};
}

// Baslik seridi (tek satir, header_h): solda sayfa basligi, sagda sayfanin doldurdugu yuva (chip / eylem dugmesi).
// Serit kaydirilmaz; kutular pencere koordinatindadir. Kutular dikey ortalanir; sigmayan istek bos kutu (w == 0)
// dondurur ve yer ayirmaz (cagiran ogeyi cizmez ya da govdeye tasir).
struct HeaderBar {
    RectF strip;     // seridin tamami
    float lead = 0;  // basligin (ve basliga bagli ogelerin) sag kenari: bos alanin sol siniri
    float trail = 0; // sag yuvanin bos kalan sag kenari (take sagdan sola ilerler)
    float gap = 8;   // ogeler arasi bosluk

    static HeaderBar make(const RectF& strip, float title_w, float gap) {
        HeaderBar b;
        b.strip = strip;
        b.gap = gap;
        b.lead = strip.x + (title_w > 0 ? title_w + 2 * gap : 0.f);
        b.trail = strip.right();
        if (b.lead > b.trail) b.lead = b.trail;
        return b;
    }
    float remaining() const { return trail > lead ? trail - lead : 0.f; }
    bool fits(float w) const { return w > 0 && w <= remaining(); }
    // Sag yuvadan (sagdan sola) w x h kutu.
    RectF take(float w, float h) {
        if (!fits(w)) return {trail, strip.y, 0, 0};
        const RectF r{trail - w, center_y(h), w, clamp_h(h)};
        trail = r.x - gap;
        if (trail < lead) trail = lead;
        return r;
    }
    // Basligin hemen sagindan (soldan saga) w x h kutu: basliga bagli ogeler (alt sekmeler).
    RectF take_leading(float w, float h) {
        if (!fits(w)) return {lead, strip.y, 0, 0};
        const RectF r{lead, center_y(h), w, clamp_h(h)};
        lead = r.right() + gap;
        if (lead > trail) lead = trail;
        return r;
    }

private:
    float clamp_h(float h) const { return h < strip.h ? (h > 0 ? h : 0.f) : strip.h; }
    float center_y(float h) const { return strip.y + (strip.h - clamp_h(h)) / 2; }
};

// DIP <-> piksel (scale = dpi / 96).
inline float to_px(float dip, float scale) { return dip * scale; }
inline float to_dip(float px, float scale) { return scale > 0 ? px / scale : px; }

// UI-15: sayfa govdesi icin kontrollu iki sutun. Alan daralinca tek sutuna duser (yatay kaydirma yok).
// Esik: iki sutun en az min_column genisliginde ve arada gap kadar bosluk kalabiliyorsa ayrilir.
struct ColumnSet {
    RectF left, right;
    bool stacked = true;
};
ColumnSet content_columns(const RectF& area, const Theme& t, float min_column = 400.f);

// Kartlari govdede dikey dizer: her kartin yuksekligi verilir, araya gap konur.
struct Stack {
    RectF area;
    float gap = 10;
    float cursor = 0;
    RectF next(float height) {
        RectF r{area.x, area.y + cursor, area.w, height};
        cursor += height + gap;
        return r;
    }
};

// ---- Esit boylu kart satirlari (spec 2026-11-12 §1-§2) ----
// Yan yana duran kartlar ayni ust kenarda ve ayni yukseklikte cizilir: satir yuksekligi = en uzun hucrenin dogal
// yuksekligi. Hucre = tek kart ya da dikey alt yigin (kartlar arasinda gap); alt yiginda artan bosluk SON karta verilir.
// Dar pencerede (stacked) satir tek sutuna duser: kartlar okuma sirasiyla, dogal boylarinda alt alta durur.

// Saf yerlesim sonucu. Koordinatlar satirin sol ust kosesine goredir (x = 0, y = 0).
struct RowFrames {
    float height = 0;                       // satirin kullandigi toplam yukseklik
    std::vector<RectF> cells;               // hucre dikdortgenleri (kart icermeyen hucre: h == 0)
    std::vector<std::vector<RectF>> cards;  // hucre -> kart cerceveleri (dogal yuksekligi <= 0 olan kart yoktur: h == 0)
};
// natural_heights[c][k]: c. hucrenin k. kartinin dogal yuksekligi (<= 0: kart yok; yer ve bosluk ayrilmaz).
// widths[c]: hucre genisligi (stacked modda cagiran her hucreye tam genisligi verir).
RowFrames equal_row_frames(const std::vector<std::vector<float>>& natural_heights, const std::vector<float>& widths, float gap,
                           bool stacked = false);
// n es sutun: tam DIP'e yuvarlanir, artan pikseller son sutuna verilir (sutunlar + bosluklar = total_w).
std::vector<float> equal_widths(float total_w, std::size_t n, float gap);

// Kart yuvasi: kart islevi iki gecisli calisir. Olcum gecisinde (measuring) place() yalniz dogal yuksekligi kaydeder ve
// false doner: kart HICBIR SEY cizmeden / kaydetmeden (isabet, StatusSeq, sayac) geri doner. Cizim gecisinde satir
// yuksekligindeki cerceveyi verir; icerik uste, eylem (dugme) satiri cercevenin altina hizalanir. place() cagirmayan
// kart "yok" sayilir (natural == 0): yer ayrilmaz.
struct CardSlot {
    RectF area;              // x, y, w: kartin yeri (h kullanilmaz)
    float row_h = 0;         // cizim gecisi: satir yuksekligi (0 = dogal boy)
    bool measuring = false;  // olcum gecisi
    float natural = 0;       // kartin bildirdigi dogal yukseklik
    RectF frame;             // cizim gecisinde verilen cerceve
    bool place(float natural_h, RectF& out) {
        natural = natural_h > 0 ? natural_h : 0.f;
        if (measuring) return false;
        frame = {area.x, area.y, area.w, natural > row_h ? natural : row_h};
        out = frame;
        return true;
    }
};
using CardFn = std::function<void(CardSlot&)>;
// Denetim kaydi: cizilen her esit satirin hucre dikdortgenleri (yalniz yan yana modda, >= 2 hucre).
using RowLog = std::vector<std::vector<RectF>>;

// Esit boylu tek satir. cells[c]: hucrenin kartlari (ustten alta). Hic karti olmayan hucre satirdan duser; kalan
// hucreler genisligi esit paylasir (tek hucre kalirsa tam genislik, dogal boy).
struct EqualRow {
    RectF area;  // x, y, w
    float gap = 10;
    bool stacked = false;
    RowLog* log = nullptr;
    std::vector<std::vector<CardFn>> cells;
    // Olcer (cizimsiz gecis), yerlestirir, cizer. Kullanilan yuksekligi dondurur (kart yoksa 0).
    float run() const;
};

// Sayfa akisi: tam genislik kartlar ve esit satirlar ust uste dizilir (aralarinda gap).
struct CardFlow {
    RectF area;  // x, y, w
    float gap = 10;
    bool stacked = false;  // dar pencere: satirlar tek sutuna duser
    RowLog* log = nullptr;
    float cursor = 0;
    // pairs(): iki kart yalniz benzer boydaysa eslenir (spec §1a) — uzun olan, kisa olanin bu katini asiyorsa
    // eslenmezler (kisa kart bos uzatilmaz): ilki tam genislik alir, ikincisi siradaki kartla eslenmeyi bekler (esi
    // cikmazsa o da tam genislik). 0 = her zaman esle.
    float pair_max_ratio = 0;

    float y() const { return area.y + cursor; }
    // Flow disinda cizilen blok (serit, izgara, etiket): yuksekligi kadar ilerler (h <= 0: ilerlemez).
    void advance(float h) {
        if (h > 0) cursor += h + gap;
    }
    // Tek kart, tam genislik, dogal boy. Kullanilan yuksekligi dondurur (kart yoksa 0; yer ayrilmaz).
    float full(const CardFn& fn);
    // Esit boylu satir (EqualRow). Kullanilan yuksekligi dondurur.
    float row(std::vector<std::vector<CardFn>> cells);
    // Okuma sirasiyla ikiser eslenen kartlar: var olan (dogal yuksekligi > 0) kartlar ikiser esit satir olur; full
    // isaretli kart (liste / tablo) ve esi olmayan kart tek basina tam genislik alir.
    struct Item {
        CardFn fn;
        bool full = false;
    };
    void pairs(const std::vector<Item>& items);
    float used() const { return cursor > 0 ? cursor - gap : 0.f; }
};

}  // namespace ef::ui
