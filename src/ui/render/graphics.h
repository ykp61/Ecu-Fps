#pragma once
// UI-01: Direct2D + DirectWrite cizim katmani. Koordinatlar DIP; hedefin DPI'si olcegi verir.
// Ayni kod hem pencereye (HWND) hem ekran goruntusu/denetim icin WIC bitmap'e cizer.
// Denetim modu: her metin kutusu olculur; kutuya sigmayan metin "tasma" olarak kaydedilir.

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <map>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/games/game_icon.h"
#include "ui/brand/svg_path.h"
#include "ui/layout/layout.h"
#include "ui/render/icons.h"
#include "ui/theme/theme.h"

namespace ef::ui {

enum class Align { Leading, Center, Trailing };

// Hazir pikselli resim (on-carpilmis BGRA). Graphics dosya/kabuk kodu icermez; pikselleri platform katmani yukler.
using core::games::ImagePixels;
using core::games::ImageRef;

struct TextOpts {
    Align align = Align::Leading;
    bool wrap = true;      // false: tek satir (kesilmez; sigmazsa denetimde tasma)
    bool vcenter = false;  // kutu icinde dikey ortala
    bool tabular = false;  // M13: degisen sayilar icin esit genislikli rakam (tnum); Metric rolunde her zaman acik
};

struct Overflow {
    std::wstring text;
    RectF box;
    float need_w = 0, need_h = 0;
};

struct SizeF {
    float w = 0, h = 0;
};

struct Point {
    float x = 0, y = 0;
};

// Gradyan duragi: pos 0..1 (kutu/yay genisligi boyunca), renk Palette'ten gelir.
struct GradientStop {
    float pos = 0;
    Color c;
    friend bool operator==(const GradientStop&, const GradientStop&) = default;
};

class Graphics {
public:
    // locale: "tr-TR" / "en-US" (DirectWrite sekillendirme ve buyuk/kucuk harf).
    static std::unique_ptr<Graphics> create(const Theme& theme, std::wstring_view locale);
    ~Graphics();

    // Hedefler
    // software=true: CPU rasterlestirici (GPU surucusu surece yuklenmez); false: donanim (D3D).
    void set_software(bool on) { software_ = on; }
    bool software() const { return software_; }
    // GDI DC hedefi: WM_PAINT'in verdigi HDC'ye her karede baglanir. HwndRenderTarget'in
    // DXGI present yolu bazi Windows surumlerinde gorunmez kare urettigi icin (OS 26300'te
    // dogrulandi; DXGI ve DC yollari calisiyor) cizim dogrudan DC'ye yapilir.
    bool bind_dc(HDC dc, int w_px, int h_px, UINT dpi);
    // Ayni hedef, DC'nin alt dikdortgenine (piksel) baglanir: cizim koordinatlarinin orijini alt dikdortgenin sol
    // ustudur. Yalniz bir bolgeyi yeniden cizen hizli yollar icin (canli test gorunumu); cagiran set_offset ile pencere
    // koordinatina geri tasir.
    bool bind_dc(HDC dc, const RECT& sub_px, UINT dpi);
    bool bind_wic(IWICBitmap* bitmap, float dpi);
    void release_target();

    bool begin();
    // false: cihaz kayboldu, hedef serbest birakildi (sonraki karede yeniden bind edilir).
    bool end();

    // Cizim (DIP)
    void clear(const Color& c);
    void fill(const RectF& r, const Color& c);
    void fill_round(const RectF& r, float radius, const Color& c);
    void stroke_round(const RectF& r, float radius, const Color& c, float width = 1.f);
    void fill_circle(float cx, float cy, float radius, const Color& c);
    // UI-09/10: halka gosterge. frac 0..1 saat yonunde tepeden; uclar yuvarlak. Iz (track) tam halka.
    void ring(float cx, float cy, float radius, float thickness, float frac, const Color& track, const Color& fill);
    // Zehir yesili ilkelleri (yazilim DC hedefi; D2D efekti yok).
    // Yatay dogrusal gradyan. shift01 gradyani kutu genisligi oraninda kaydirir ve sarar (RGB serit hareketi).
    void fill_gradient(const RectF& r, std::span<const GradientStop> stops, float shift01 = 0);
    // Gradyan tum kutuya yayilir; yalniz visible01 kadari (soldan) yuvarlak uclu cizilir (rampa cubugu).
    void fill_gradient_round(const RectF& r, float radius, std::span<const GradientStop> stops, float visible01);
    // Iki nokta arasinda dogrusal gradyan (CSS linear-gradient acisi cagiranda noktalara cevrilir) ile yuvarlak dolgu.
    void fill_linear_round(const RectF& r, float radius, Point from, Point to, std::span<const GradientStop> stops);
    // Eliptik radyal gradyan (CSS radial-gradient(ellipse rx ry at center)), area'ya kirpilir. Firca yok: profil kucuk bir
    // bitmap'e (en cok 160 x 160) bir kez kurulur ve kutuya yayilir. Cagiran area'yi gorunur elips kutusuyla sinirlar (ucuz).
    void fill_radial(const RectF& area, Point center, float rx, float ry, std::span<const GradientStop> stops);
    // CSS box-shadow taklidi (efekt yok): sekil (dx, dy) kadar kaydirilip bes katmanli yuvarlak dolguyla Gauss
    // bulaniklik (sigma = blur / 2) yaklasiklanir. Sekil opak bir yuzeyle ortulecegi icin ic katmanlar gorunmez.
    void soft_shadow(const RectF& r, float radius, float dx, float dy, float blur, const Color& c);
    // Noktali doku (CSS radial-gradient(c dot_r, transparent 0) / background-size pitch): kutunun sol ustune hizali
    // pitch x pitch karolarin ortasinda dot_r yaricapli nokta. Karo hedef basina bir kez bitmap olarak kurulur.
    void fill_dots(const RectF& r, float radius, float pitch, float dot_r, const Color& c);
    // Yay: 0 derece = saat 3 yonu, pozitif = saat yonu; uclar yuvarlak (round = false: duz). |sweep| >= 360 tam halka.
    void arc(float cx, float cy, float r, float thick, float start_deg, float sweep_deg, const Color& c, bool round = true);
    // Duraklar yayin sinir kutusunun x araligina yayilir (kadran yayi).
    void arc_gradient(float cx, float cy, float r, float thick, float start_deg, float sweep_deg,
                      std::span<const GradientStop> stops);
    // Parlama taklidi: 3 es merkezli yuvarlak dolgu (2/5/9 DIP buyutme; alfa 0.18/0.10/0.05 x strength).
    void glow_round(const RectF& r, float radius, const Color& c, float strength);
    // Acik cizgi dizisi; uclar ve birlesimler yuvarlak.
    void polyline(std::span<const Point> pts, float w, const Color& c);
    // Kapali cokgen dolgusu (ince grafik alani).
    void fill_polygon(std::span<const Point> pts, const Color& c);
    // Vektor ikon (spec 2026-11-11 §3): 24 birimlik izgara kutuya oranli ve ortali sigdirilir; cizgi 1,6 birim,
    // uclar yuvarlak. Renk cagirandan (yuksek kontrastta sistem metin rengi). Bos kutu: cizilmez.
    void draw_icon(Icon icon, const RectF& box, const Color& c);
    void push_clip(const RectF& r);
    void pop_clip();
    void set_offset(float dx, float dy);  // kaydirma icin oteleme
    void set_alpha(float a) { alpha_ = a < 0 ? 0 : (a > 1 ? 1 : a); }  // sayfa gecisi opakligi

    // Metin: kullanilan yuksekligi dondurur.
    float text(Role role, std::wstring_view s, const RectF& box, const Color& c, TextOpts o = {});
    // Simge fontundan tek glif (Segoe Fluent Icons / MDL2). Font yoksa cizmez.
    void icon(wchar_t glyph, const RectF& box, const Color& c, float size = 16.f);
    SizeF measure(Role role, std::wstring_view s, float max_w, bool wrap = true, bool tabular = false);
    // max_w genisliginde sarilan metnin bittigi nokta (metin kutusunun sol ustune gore): x = son satirda metnin sag
    // kenari, y = son satirin ust kenari. Metnin hemen ardina satir ici oge (baglanti) koymak icin. Bos metin -> {0, 0}.
    Point text_end(Role role, std::wstring_view s, float max_w);
    // BRAND-01: marka SVG'si kutuya oranli sigdirilir (sola dayali, dikey ortali). mono verilirse tum
    // sekiller o renkle (yuksek kontrast); aksi halde SVG renkleri (currentColor icin text yedek).
    // Geometri belge basina bir kez kurulur ve onbellekte tutulur. Cizilen genislik dondurulur.
    float svg(const SvgDoc& doc, const RectF& box, std::optional<Color> mono = std::nullopt, const Color& current = {1, 1, 1, 1});
    // Resmi kutuya yayarak (yuvarlak koseli, dogrusal aradegerleme) cizer. false: cizilmedi (resim yok/gecersiz ya da
    // hedef bitmap'i olusturulamadi) -> cagiran yedegini cizer. Hedefe bagli ID2D1Bitmap onbellekte tutulur ve
    // release_target() ile birakilir; sahibi kalmayan resimlerin girdileri sonraki cizimde dusurulur.
    // max_side: kabul edilen en buyuk kenar (piksel). Varsayilan ikon siniri; canli test onizlemesi kendi sinirini verir.
    bool image(const ImageRef& img, const RectF& box, float radius = 0, std::uint32_t max_side = core::games::kImagePixelsMaxSide);
    std::size_t image_cache_size() const { return images_.size(); }  // test/denetim
    // Sahibi kalmayan resimlerin hedef bitmap'lerini hemen birakir (canli test onizlemesi kosu bitince: 960x540'lik
    // bitmap bir sonraki resim cizimine kadar onbellekte beklemez). Birakilan girdi sayisi doner.
    std::size_t drop_released_images();

    // Denetim
    void set_audit(bool on) { audit_ = on; }
    const std::vector<Overflow>& overflows() const { return overflows_; }
    void clear_overflows() { overflows_.clear(); }

    const std::wstring& font_family() const { return family_; }
    // Mono rollerin font ailesi (gomulu JetBrains Mono; yuklenemezse sistem yedegi).
    const std::wstring& mono_family() const { return mono_family_; }
    // Gomulu JetBrains Mono bellek ici koleksiyonu kuruldu mu (surec basina bir kez).
    static bool mono_embedded();
    // Rolun satir kutusu icindeki taban cizgisi (kutunun ustunden DIP): taban cizgisine hizalamak icin.
    float baseline(Role role) const { return baselines_[static_cast<std::size_t>(role)]; }
    bool has_icon_font() const { return icon_format_ != nullptr; }
    ID2D1Factory1* factory() const { return factory_.Get(); }

private:
    Graphics() = default;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout(Role role, std::wstring_view s, float w, float h, const TextOpts& o);
    ID2D1SolidColorBrush* brush(const Color& c);
    float text_y(const Microsoft::WRL::ComPtr<IDWriteTextLayout>& l, const DWRITE_TEXT_METRICS& m, float y) const;
    ID2D1LinearGradientBrush* gradient(std::span<const GradientStop> stops, bool wrap);
    ID2D1StrokeStyle* round_style();
    void apply_text_params();
    Microsoft::WRL::ComPtr<ID2D1Geometry> arc_geometry(float cx, float cy, float r, float start_deg, float sweep_deg);

    Microsoft::WRL::ComPtr<ID2D1Factory1> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, static_cast<std::size_t>(Role::Count_)> formats_{};
    Microsoft::WRL::ComPtr<IDWriteTextFormat> icon_format_;
    Microsoft::WRL::ComPtr<IDWriteTypography> tabular_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dc_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::array<float, static_cast<std::size_t>(Role::Count_)> baselines_{};
    std::array<float, static_cast<std::size_t>(Role::Count_)> tracking_{};
    std::wstring family_;
    std::wstring mono_family_;
    std::wstring locale_;
    bool audit_ = false;
    std::vector<Overflow> overflows_;
    std::vector<RectF> clips_;
    float dx_ = 0, dy_ = 0;
    float alpha_ = 1;
    bool software_ = true;
    std::map<const SvgDoc*, std::vector<Microsoft::WRL::ComPtr<ID2D1PathGeometry>>> svg_cache_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_caps_;
    // Gradyan fircalari hedefe baglidir: release_target() ile birlikte birakilir.
    struct GradEntry {
        std::vector<GradientStop> stops;
        float alpha = 1;
        bool wrap = false;
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brush;
    };
    std::vector<GradEntry> grads_;
    // Isilti bitmap'leri ve nokta dokusu karosu da hedefe baglidir (ayni kural; kucuk onbellek).
    struct GlowEntry {
        std::vector<GradientStop> stops;
        float rx = 0, ry = 0;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    std::vector<GlowEntry> glows_;

    struct DotsEntry {
        float pitch = 0, dot_r = 0, dpi = 0;
        Color c;
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
    };
    std::vector<DotsEntry> dots_;
    // Resim bitmap'leri de hedefe baglidir (ayni kural). Anahtar piksel nesnesinin adresi; owner adresin yeniden
    // kullanilmasini (eski resim silinip yenisi ayni adrese gelirse) ayirt eder.
    struct ImageEntry {
        std::weak_ptr<const ImagePixels> owner;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
    };
    std::map<const ImagePixels*, ImageEntry> images_;
};

}  // namespace ef::ui
