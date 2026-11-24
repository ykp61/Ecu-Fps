// Bellek ici font yukleyicisi (IDWriteFactory5) Windows 10 1709 (RS3) arayuzudur; proje alt siniri (NTDDI 10.0) DirectWrite
// basliklarinda bunu gizler. Yalniz bu cevirim biriminde acilir: calisma zamaninda arayuz yoksa fabrika sorgusu basarisiz
// olur ve sistem mono fontuna dusulur (mono_fonts); baska API bu birimde surume bagli kullanilmaz.
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000004  // NTDDI_WIN10_RS3
#include "ui/render/graphics.h"

#include <dwrite_3.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ef::ui::font_data {
extern const std::string_view mono_medium;
extern const std::string_view mono_semibold;
}  // namespace ef::ui::font_data

namespace ef::ui {

using Microsoft::WRL::ComPtr;

namespace {

D2D1_COLOR_F d2d(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
D2D1_RECT_F d2d(const RectF& r) { return D2D1::RectF(r.x, r.y, r.right(), r.bottom()); }

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool family_exists(IDWriteFactory* f, const wchar_t* name) {
    ComPtr<IDWriteFontCollection> sys;
    if (FAILED(f->GetSystemFontCollection(&sys))) return false;
    UINT32 idx = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(sys->FindFamilyName(name, &idx, &exists)) && exists;
}

// Gomulu JetBrains Mono (BOOST .mono): surec basina bir kez, paylasilan DirectWrite fabrikasinin bellek ici yukleyicisiyle
// koleksiyon kurulur (dosya yazilmaz). Veri statik dizidir: yukleyici kendini sahip olarak alir, kopya alinmaz.
// Basarisizsa bos (cagiran Cascadia Mono / Consolas'a duser).
struct MonoFonts {
    ComPtr<IDWriteFactory5> factory;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontCollection1> collection;
    std::wstring family;
};
const MonoFonts& mono_fonts() {
    static const MonoFonts fonts = [] {
        MonoFonts m;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), reinterpret_cast<IUnknown**>(m.factory.GetAddressOf()))))
            return MonoFonts{};
        if (FAILED(m.factory->CreateInMemoryFontFileLoader(&m.loader)) || FAILED(m.factory->RegisterFontFileLoader(m.loader.Get())))
            return MonoFonts{};
        ComPtr<IDWriteFontSetBuilder1> builder;
        if (FAILED(m.factory->CreateFontSetBuilder(&builder))) return MonoFonts{};
        for (const std::string_view blob : {font_data::mono_medium, font_data::mono_semibold}) {
            ComPtr<IDWriteFontFile> file;
            if (blob.empty() ||
                FAILED(m.loader->CreateInMemoryFontFileReference(m.factory.Get(), blob.data(), static_cast<UINT32>(blob.size()), m.loader.Get(), &file)) ||
                FAILED(builder->AddFontFile(file.Get())))
                return MonoFonts{};
        }
        ComPtr<IDWriteFontSet> set;
        if (FAILED(builder->CreateFontSet(&set)) || FAILED(m.factory->CreateFontCollectionFromFontSet(set.Get(), &m.collection))) return MonoFonts{};
        ComPtr<IDWriteFontFamily> fam;
        ComPtr<IDWriteLocalizedStrings> names;
        UINT32 len = 0;
        if (m.collection->GetFontFamilyCount() < 1 || FAILED(m.collection->GetFontFamily(0, &fam)) || FAILED(fam->GetFamilyNames(&names)) ||
            FAILED(names->GetStringLength(0, &len)))
            return MonoFonts{};
        m.family.resize(len + 1);
        if (FAILED(names->GetString(0, m.family.data(), len + 1))) return MonoFonts{};
        m.family.resize(len);
        return m;
    }();
    return fonts;
}

// CSS satir kutusu taban cizgisi: yarim satir payi + ascent (font tasarim olculeri; Windows'ta Chromium ile ayni win
// olculeri). Font bulunamazsa DirectWrite'in eski orani (0.78 x satir).
float css_baseline(IDWriteFontCollection* coll, const std::wstring& family, DWRITE_FONT_WEIGHT w, float size, float line) {
    UINT32 idx = 0;
    BOOL exists = FALSE;
    ComPtr<IDWriteFontFamily> fam;
    ComPtr<IDWriteFont> font;
    if (coll && SUCCEEDED(coll->FindFamilyName(family.c_str(), &idx, &exists)) && exists && SUCCEEDED(coll->GetFontFamily(idx, &fam)) &&
        SUCCEEDED(fam->GetFirstMatchingFont(w, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
        DWRITE_FONT_METRICS m{};
        font->GetMetrics(&m);
        if (m.designUnitsPerEm > 0) {
            const float a = static_cast<float>(m.ascent) / m.designUnitsPerEm * size, d = static_cast<float>(m.descent) / m.designUnitsPerEm * size;
            return (line - (a + d)) / 2 + a;
        }
    }
    return line * 0.78f;
}

}  // namespace

bool Graphics::mono_embedded() { return !mono_fonts().family.empty(); }

std::unique_ptr<Graphics> Graphics::create(const Theme& theme, std::wstring_view locale) {
    std::unique_ptr<Graphics> g(new Graphics());
    // Surec basina tek D2D fabrikasi (cok is parcacikli tur, surec omru boyunca tutulur): pencere her olusturuldugunda
    // (tepsiden ac/kapat) yeni fabrika ve onun yazilim tarayici onbellekleri kurulmaz.
    static ID2D1Factory1* const shared = [] {
        ID2D1Factory1* f = nullptr;
        D2D1_FACTORY_OPTIONS opts{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &opts, reinterpret_cast<void**>(&f)))) f = nullptr;
        return f;
    }();
    if (!shared) return nullptr;
    g->factory_ = shared;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(g->dwrite_.GetAddressOf()))))
        return nullptr;
    g->locale_ = locale;
    // Font onceligi tokens.font_families; sistemde olan ilki. Font dosyasi dagitilmaz.
    // "Segoe UI Variable" Windows 11'de optik boyut aileleriyle kayitlidir (Text / Display).
    std::wstring display_family;
    for (const auto& f : theme.font_families) {
        const auto w = widen(f);
        if (family_exists(g->dwrite_.Get(), w.c_str())) {
            g->family_ = w;
            break;
        }
        if (family_exists(g->dwrite_.Get(), (w + L" Text").c_str())) {
            g->family_ = w + L" Text";
            if (family_exists(g->dwrite_.Get(), (w + L" Display").c_str())) display_family = w + L" Display";
            break;
        }
    }
    if (g->family_.empty()) g->family_ = L"Segoe UI";
    // Mono roller: gomulu JetBrains Mono; yuklenemezse sistemdeki ilk esdeger (Cascadia Mono, Consolas).
    const MonoFonts& mono = mono_fonts();
    std::wstring mono_family = mono.family;
    IDWriteFontCollection* mono_coll = mono.collection.Get();
    if (mono_family.empty()) {
        mono_coll = nullptr;
        for (const wchar_t* f : {L"Cascadia Mono", L"Consolas", L"Courier New"})
            if (family_exists(g->dwrite_.Get(), f)) {
                mono_family = f;
                break;
            }
    }
    g->mono_family_ = mono_family;
    ComPtr<IDWriteFontCollection> sys;
    g->dwrite_->GetSystemFontCollection(&sys);
    for (std::size_t i = 0; i < g->formats_.size(); ++i) {
        const auto& r = theme.type[i];
        const bool mono_role = r.mono && !mono_family.empty();
        // Buyuk basliklar (>= 20 DIP) icin Display optik boyutu.
        const std::wstring& fam = mono_role ? mono_family : (r.size >= 20.f && !display_family.empty()) ? display_family : g->family_;
        IDWriteFontCollection* coll = mono_role ? mono_coll : nullptr;
        const auto weight = static_cast<DWRITE_FONT_WEIGHT>(r.weight);
        ComPtr<IDWriteTextFormat> fmt;
        if (FAILED(g->dwrite_->CreateTextFormat(fam.c_str(), coll, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, r.size,
                                                g->locale_.c_str(), &fmt)))
            return nullptr;
        const float base = r.css ? css_baseline(coll ? coll : sys.Get(), fam, weight, r.size, r.line) : r.line * 0.78f;
        fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, r.line, base);
        g->formats_[i] = fmt;
        g->baselines_[i] = base;
        g->tracking_[i] = r.tracking;
    }
    for (const wchar_t* icons : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        if (!family_exists(g->dwrite_.Get(), icons)) continue;
        if (SUCCEEDED(g->dwrite_->CreateTextFormat(icons, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                  DWRITE_FONT_STRETCH_NORMAL, 16.f, L"en-US", &g->icon_format_))) {
            g->icon_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g->icon_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            break;
        }
    }
    if (SUCCEEDED(g->dwrite_->CreateTypography(&g->tabular_)))
        g->tabular_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
    return g;
}

Graphics::~Graphics() = default;

bool Graphics::bind_dc(HDC dc, int w_px, int h_px, UINT dpi) {
    return bind_dc(dc, RECT{0, 0, std::max(w_px, 1), std::max(h_px, 1)}, dpi);
}

bool Graphics::bind_dc(HDC dc, const RECT& sub_px, UINT dpi) {
    if (sub_px.right <= sub_px.left || sub_px.bottom <= sub_px.top) return false;
    if (!dc_target_) {
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            static_cast<float>(dpi), static_cast<float>(dpi));
        if (FAILED(factory_->CreateDCRenderTarget(&props, &dc_target_))) return false;
    }
    dc_target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    if (FAILED(dc_target_->BindDC(dc, &sub_px))) return false;
    target_ = dc_target_;
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    apply_text_params();
    return true;
}

bool Graphics::bind_wic(IWICBitmap* bitmap, float dpi) {
    release_target();
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    dpi, dpi);
    if (FAILED(factory_->CreateWicBitmapRenderTarget(bitmap, props, &target_))) return false;
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    apply_text_params();
    return true;
}

// Metin agirligi: Chromium (maket) acik renkli metni DirectWrite varsayilanindan daha dolgun cizer (olculen murekkep
// yogunlugu: varsayilan ~%92, gama 2,2 ~%95). Yalniz cizim; metin olcusu degismez. Kontrast gri tonlamada etkisizdir.
void Graphics::apply_text_params() {
    if (!dwrite_ || !target_) return;
    ComPtr<IDWriteRenderingParams> rp;
    if (SUCCEEDED(dwrite_->CreateCustomRenderingParams(2.2f, 1.0f, 0.f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &rp)))
        target_->SetTextRenderingParams(rp.Get());
}

void Graphics::release_target() {
    images_.clear();
    grads_.clear();
    dots_.clear();
    glows_.clear();
    brush_.Reset();
    dc_target_.Reset();
    target_.Reset();
}

bool Graphics::begin() {
    if (!target_) return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    dx_ = dy_ = 0;
    alpha_ = 1;
    clips_.clear();
    return true;
}

bool Graphics::end() {
    while (!clips_.empty()) pop_clip();
    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release_target();
        return false;
    }
    return SUCCEEDED(hr);
}

ID2D1SolidColorBrush* Graphics::brush(const Color& c0) {
    const Color c = c0.with_alpha(c0.a * alpha_);
    if (!brush_) target_->CreateSolidColorBrush(d2d(c), &brush_);
    else brush_->SetColor(d2d(c));
    return brush_.Get();
}

void Graphics::clear(const Color& c) { target_->Clear(d2d(c)); }
void Graphics::fill(const RectF& r, const Color& c) { target_->FillRectangle(d2d(r), brush(c)); }
void Graphics::fill_round(const RectF& r, float radius, const Color& c) {
    target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(r), radius, radius), brush(c));
}
void Graphics::stroke_round(const RectF& r, float radius, const Color& c, float width) {
    // Cizgi kutu icinde kalsin: yarim kalinlik iceri.
    const RectF in{r.x + width / 2, r.y + width / 2, r.w - width, r.h - width};
    target_->DrawRoundedRectangle(D2D1::RoundedRect(d2d(in), radius, radius), brush(c), width);
}
void Graphics::fill_circle(float cx, float cy, float radius, const Color& c) {
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush(c));
}
void Graphics::ring(float cx, float cy, float r, float th, float frac, const Color& track, const Color& fill) {
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush(track), th);
    if (!(frac > 0.002f)) return;  // bos (veya NaN): yalniz iz
    // Tepeden (-90 derece) saat yonunde; >= 0.999 tam halka.
    arc(cx, cy, r, th, -90.f, frac >= 0.999f ? 360.f : frac * 360.f, fill);
}

ID2D1StrokeStyle* Graphics::round_style() {
    if (!round_caps_) {
        const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                       D2D1_LINE_JOIN_ROUND);
        factory_->CreateStrokeStyle(props, nullptr, 0, &round_caps_);
    }
    return round_caps_.Get();
}

ComPtr<ID2D1Geometry> Graphics::arc_geometry(float cx, float cy, float r, float start_deg, float sweep_deg) {
    if (!(r > 0) || !std::isfinite(start_deg) || !std::isfinite(sweep_deg) || std::fabs(sweep_deg) < 0.01f) return nullptr;
    if (std::fabs(sweep_deg) >= 360.f) {
        ComPtr<ID2D1EllipseGeometry> e;
        if (FAILED(factory_->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), &e))) return nullptr;
        return e;
    }
    constexpr float kDeg = 3.14159265f / 180.f;
    const float a0 = start_deg * kDeg, a1 = (start_deg + sweep_deg) * kDeg;
    const D2D1_POINT_2F p0 = D2D1::Point2F(cx + r * std::cos(a0), cy + r * std::sin(a0));
    const D2D1_POINT_2F p1 = D2D1::Point2F(cx + r * std::cos(a1), cy + r * std::sin(a1));
    ComPtr<ID2D1PathGeometry> g;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return nullptr;
    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(p1, D2D1::SizeF(r, r), 0,
                                  sweep_deg > 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                  std::fabs(sweep_deg) > 180.f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return nullptr;
    return g;
}

void Graphics::arc(float cx, float cy, float r, float thick, float start_deg, float sweep_deg, const Color& c, bool round) {
    auto g = arc_geometry(cx, cy, r, start_deg, sweep_deg);
    if (!g) return;
    target_->DrawGeometry(g.Get(), brush(c), thick, round ? round_style() : nullptr);
}

ID2D1LinearGradientBrush* Graphics::gradient(std::span<const GradientStop> stops, bool wrap) {
    if (stops.empty()) return nullptr;
    for (auto& e : grads_)
        if (e.wrap == wrap && e.alpha == alpha_ && std::equal(e.stops.begin(), e.stops.end(), stops.begin(), stops.end()))
            return e.brush.Get();
    std::vector<D2D1_GRADIENT_STOP> ds;
    ds.reserve(stops.size());
    for (const auto& st : stops) {
        const float pos = std::isfinite(st.pos) ? std::clamp(st.pos, 0.f, 1.f) : 0.f;
        ds.push_back({pos, d2d(st.c.with_alpha(st.c.a * alpha_))});
    }
    ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(target_->CreateGradientStopCollection(ds.data(), static_cast<UINT32>(ds.size()), D2D1_GAMMA_2_2,
                                                     wrap ? D2D1_EXTEND_MODE_WRAP : D2D1_EXTEND_MODE_CLAMP, &coll)))
        return nullptr;
    ComPtr<ID2D1LinearGradientBrush> b;
    if (FAILED(target_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 0)), coll.Get(), &b)))
        return nullptr;
    if (grads_.size() >= 16) grads_.erase(grads_.begin());  // kucuk onbellek: serit + rampa + kadran
    grads_.push_back({std::vector<GradientStop>(stops.begin(), stops.end()), alpha_, wrap, b});
    return grads_.back().brush.Get();
}

void Graphics::fill_gradient(const RectF& r, std::span<const GradientStop> stops, float shift01) {
    if (r.w <= 0 || r.h <= 0) return;
    auto* b = gradient(stops, true);
    if (!b) return;
    const float s = std::isfinite(shift01) ? shift01 - std::floor(shift01) : 0.f;  // [0,1) sarma
    // Taslak (.ef-strip: background-size 200%): gradyan genisligin 2 kati; ekranda yarisi gorunur, kayma 2 genislik.
    const float x0 = r.x + s * 2.f * r.w;
    b->SetStartPoint(D2D1::Point2F(x0, r.y));
    b->SetEndPoint(D2D1::Point2F(x0 + 2.f * r.w, r.y));
    target_->FillRectangle(d2d(r), b);
}

void Graphics::fill_gradient_round(const RectF& r, float radius, std::span<const GradientStop> stops, float visible01) {
    if (r.w <= 0 || r.h <= 0 || !(visible01 > 0)) return;  // NaN/0: cizilmez
    auto* b = gradient(stops, false);
    if (!b) return;
    const float v = std::min(visible01, 1.f);
    const RectF part{r.x, r.y, r.w * v, r.h};
    const float rad = std::max(0.f, std::min({radius, part.w / 2, part.h / 2}));
    b->SetStartPoint(D2D1::Point2F(r.x, r.y));
    b->SetEndPoint(D2D1::Point2F(r.right(), r.y));
    target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(part), rad, rad), b);
}

void Graphics::fill_linear_round(const RectF& r, float radius, Point from, Point to, std::span<const GradientStop> stops) {
    if (r.w <= 0 || r.h <= 0) return;
    auto* b = gradient(stops, false);
    if (!b) return;
    b->SetStartPoint(D2D1::Point2F(from.x, from.y));
    b->SetEndPoint(D2D1::Point2F(to.x, to.y));
    const float rad = std::max(0.f, std::min({radius, r.w / 2, r.h / 2}));
    if (rad > 0) target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(r), rad, rad), b);
    else target_->FillRectangle(d2d(r), b);
}

void Graphics::fill_radial(const RectF& area, Point center, float rx, float ry, std::span<const GradientStop> stops) {
    if (area.w <= 0 || area.h <= 0 || !(rx > 0) || !(ry > 0) || stops.empty()) return;
    // Firca ya da katmanli buyuk dolgu yok: ikisi de yazilim rasterlestiricisinde her karede genis alan isi uretip
    // surecin ozel bellegini dalgalandiriyordu (ui_lifecycle). Profil, elips kutusunun (yaricap x gorunur uc) en cok
    // 160 x 160 tekselli on-carpilmis kopyasi olarak CPU'da bir kez kurulur ve tek DrawBitmap ile (dogrusal
    // aradegerleme) kutuya yayilir; isiltilar yumusak oldugu icin dusuk cozunurluk gorunmez.
    float tmax = 1.f;
    for (std::size_t i = stops.size(); i-- > 0;)
        if (stops[i].c.a > 0) {
            tmax = i + 1 < stops.size() ? std::clamp(stops[i + 1].pos, 0.f, 1.f) : 1.f;
            break;
        }
    if (!(tmax > 0)) return;
    const float ex = rx * tmax, ey = ry * tmax;
    ID2D1Bitmap* bmp = nullptr;
    for (auto& e : glows_)
        if (e.rx == rx && e.ry == ry && std::equal(e.stops.begin(), e.stops.end(), stops.begin(), stops.end())) {
            bmp = e.bitmap.Get();
            break;
        }
    if (!bmp) {
        const auto at = [&](float t) {
            if (t <= stops.front().pos) return stops.front().c;
            for (std::size_t i = 1; i < stops.size(); ++i)
                if (t <= stops[i].pos) {
                    const float span = stops[i].pos - stops[i - 1].pos;
                    return mix(stops[i - 1].c, stops[i].c, span > 0 ? (t - stops[i - 1].pos) / span : 1.f);
                }
            return stops.back().c;
        };
        constexpr float kTexelDip = 4.f;
        const UINT w = static_cast<UINT>(std::clamp(std::ceil(2 * ex / kTexelDip), 8.f, 160.f));
        const UINT h = static_cast<UINT>(std::clamp(std::ceil(2 * ey / kTexelDip), 8.f, 160.f));
        std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0u);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                const float u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(w) * 2.f - 1.f) * tmax;
                const float v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(h) * 2.f - 1.f) * tmax;
                const Color c = at(std::sqrt(u * u + v * v));
                const float a = std::clamp(c.a, 0.f, 1.f);
                const auto ch = [&](float k) { return static_cast<std::uint32_t>(std::clamp(k * a, 0.f, 1.f) * 255.f + 0.5f); };
                px[static_cast<std::size_t>(y) * w + x] = (ch(1.f) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
            }
        const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        ComPtr<ID2D1Bitmap> b;
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(w, h), px.data(), w * 4, props, &b))) return;
        if (glows_.size() >= 6) glows_.erase(glows_.begin());  // kucuk onbellek: iki zemin isiltisi + kahraman parlamasi
        glows_.push_back({std::vector<GradientStop>(stops.begin(), stops.end()), rx, ry, b});
        bmp = b.Get();
    }
    push_clip(area);
    target_->DrawBitmap(bmp, D2D1::RectF(center.x - ex, center.y - ey, center.x + ex, center.y + ey), alpha_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    pop_clip();
}
void Graphics::soft_shadow(const RectF& r, float radius, float dx, float dy, float blur, const Color& c) {
    if (r.w <= 0 || r.h <= 0 || !(blur > 0) || !(c.a > 0)) return;
    // Gauss kenar profili: kenardan disa d uzaklikta ortu = a * (1 - Phi(d / sigma)). Bes katman (disa yayilma
    // sigma x {2, 1.25, 0.5, -0.25, -1}); her katmanin alfasi, dista ice birikimli ortu kendi bandinin orta
    // noktasindaki hedefi verecek bicimde secilir (siyah golgede birlesim sirasi sonucu degistirmez).
    const float sigma = blur / 2;
    constexpr float kSpread[] = {2.f, 1.25f, 0.5f, -0.25f, -1.f};
    constexpr float kHalfStep = 0.375f;
    const auto cover = [&](float d_over_sigma) { return c.a * 0.5f * std::erfc(d_over_sigma / 1.41421356f); };
    float prev = 0;
    for (const float k : kSpread) {
        const float target = std::min(cover(k - kHalfStep), 0.999f);
        const float a = std::clamp(1.f - (1.f - target) / (1.f - prev), 0.f, 1.f);
        prev = target;
        const float sp = k * sigma;
        const RectF o{r.x + dx - sp, r.y + dy - sp, r.w + 2 * sp, r.h + 2 * sp};
        if (o.w <= 0 || o.h <= 0 || a <= 0) continue;
        const float rad = std::max(0.f, std::min({radius + sp, o.w / 2, o.h / 2}));
        target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(o), rad, rad), brush(c.with_alpha(a)));
    }
}

void Graphics::fill_dots(const RectF& r, float radius, float pitch, float dot_r, const Color& c) {
    if (r.w <= 0 || r.h <= 0 || !(pitch > 0) || !(dot_r > 0)) return;
    float dpi_x = 96.f, dpi_y = 96.f;
    target_->GetDpi(&dpi_x, &dpi_y);
    ID2D1BitmapBrush* b = nullptr;
    for (auto& e : dots_)
        if (e.pitch == pitch && e.dot_r == dot_r && e.dpi == dpi_x && e.c == c) {
            b = e.brush.Get();
            break;
        }
    // Karo piksel olarak (hedef DPI'sinda) CPU'da kurulur: ayri bir uyumlu cizim hedefi (ikinci rasterlestirici)
    // acilmaz. Nokta kapsami 4x4 alt ornekle; on-carpilmis BGRA. Karo boyu tam piksel; firca olcegi DIP adimini korur.
    const float px_per_dip = dpi_x / 96.f;
    const UINT n = static_cast<UINT>(std::clamp(std::lround(pitch * px_per_dip), 2L, 256L));
    const float tile_scale = pitch / (static_cast<float>(n) / px_per_dip);  // karonun DIP boyu -> pitch
    if (!b) {
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(n) * n, 0u);
        const float k = static_cast<float>(n) / pitch;  // karo pikseli / DIP
        const float cx = pitch / 2, rr = dot_r * dot_r;
        constexpr int kSub = 4;
        for (UINT y = 0; y < n; ++y)
            for (UINT x = 0; x < n; ++x) {
                int hit = 0;
                for (int sy = 0; sy < kSub; ++sy)
                    for (int sx = 0; sx < kSub; ++sx) {
                        const float dx = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kSub) / k - cx;
                        const float dy = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSub) / k - cx;
                        hit += dx * dx + dy * dy <= rr ? 1 : 0;
                    }
                if (!hit) continue;
                const float a = c.a * static_cast<float>(hit) / (kSub * kSub);
                const auto ch = [&](float v) { return static_cast<std::uint32_t>(std::clamp(v * a, 0.f, 1.f) * 255.f + 0.5f); };
                pixels[static_cast<std::size_t>(y) * n + x] = (ch(1.f) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
            }
        const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_x, dpi_y);
        ComPtr<ID2D1Bitmap> bmp;
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(n, n), pixels.data(), n * 4, props, &bmp))) return;
        ComPtr<ID2D1BitmapBrush> bb;
        if (FAILED(target_->CreateBitmapBrush(bmp.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), &bb)))
            return;
        if (dots_.size() >= 4) dots_.erase(dots_.begin());
        dots_.push_back({pitch, dot_r, dpi_x, c, bb});
        b = bb.Get();
    }
    b->SetTransform(D2D1::Matrix3x2F::Scale(tile_scale, tile_scale) * D2D1::Matrix3x2F::Translation(r.x, r.y));
    b->SetOpacity(alpha_);
    const float rad = std::max(0.f, std::min({radius, r.w / 2, r.h / 2}));
    target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(r), rad, rad), b);
}

void Graphics::arc_gradient(float cx, float cy, float r, float thick, float start_deg, float sweep_deg,
                            std::span<const GradientStop> stops) {
    auto g = arc_geometry(cx, cy, r, start_deg, sweep_deg);
    if (!g) return;
    auto* b = gradient(stops, false);
    if (!b) return;
    // Yayin x araligi: uc noktalar + 0/180 derece gecisleri.
    float lo = cx - r, hi = cx + r;
    if (std::fabs(sweep_deg) < 360.f) {
        constexpr float kDeg = 3.14159265f / 180.f;
        const float a = std::min(start_deg, start_deg + sweep_deg), z = std::max(start_deg, start_deg + sweep_deg);
        const float xa = cx + r * std::cos(a * kDeg), xz = cx + r * std::cos(z * kDeg);
        lo = std::min(xa, xz);
        hi = std::max(xa, xz);
        auto crosses = [&](float k) {  // a..z araliginda k + 360n var mi
            const float n = std::ceil((a - k) / 360.f);
            return k + n * 360.f <= z;
        };
        if (crosses(0.f)) hi = cx + r;
        if (crosses(180.f)) lo = cx - r;
    }
    if (hi - lo < 0.5f) hi = lo + 0.5f;
    b->SetStartPoint(D2D1::Point2F(lo, cy));
    b->SetEndPoint(D2D1::Point2F(hi, cy));
    target_->DrawGeometry(g.Get(), b, thick, round_style());
}

void Graphics::glow_round(const RectF& r, float radius, const Color& c, float strength) {
    if (!(strength > 0) || r.w <= 0 || r.h <= 0) return;
    const float k = std::min(strength, 1.f);
    struct Pass {
        float d, a;
    };
    for (const Pass p : {Pass{9.f, 0.05f}, Pass{5.f, 0.10f}, Pass{2.f, 0.18f}}) {  // distan ice
        const RectF o{r.x - p.d, r.y - p.d, r.w + 2 * p.d, r.h + 2 * p.d};
        const float rad = radius + p.d;
        target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(o), rad, rad), brush(c.with_alpha(c.a * p.a * k)));
    }
}

void Graphics::polyline(std::span<const Point> pts, float w, const Color& c) {
    if (pts.size() < 2 || !(w > 0)) return;
    ComPtr<ID2D1PathGeometry> g;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t i = 1; i < pts.size(); ++i) sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    target_->DrawGeometry(g.Get(), brush(c), w, round_style());
}

void Graphics::fill_polygon(std::span<const Point> pts, const Color& c) {
    if (pts.size() < 3) return;
    ComPtr<ID2D1PathGeometry> g;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t i = 1; i < pts.size(); ++i) sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    target_->FillGeometry(g.Get(), brush(c));
}

void Graphics::draw_icon(Icon icon, const RectF& box, const Color& c) {
    const float side = std::min(box.w, box.h);
    if (!(side > 0)) return;
    const float k = side / kIconGrid;  // izgara birimi -> DIP
    const float ox = box.x + (box.w - side) / 2, oy = box.y + (box.h - side) / 2;
    const float sw = kIconStroke * k;
    auto* b = brush(c);
    auto* caps = round_style();
    const auto pt = [&](float x, float y) { return D2D1::Point2F(ox + x * k, oy + y * k); };
    for (const IconPrim& p : icon_prims(icon)) {
        switch (p.op) {
            case IconOp::Line:
                target_->DrawLine(pt(p.x, p.y), pt(p.u, p.v), b, sw, caps);
                break;
            case IconOp::Rect: {
                const auto rr = D2D1::RoundedRect(
                    D2D1::RectF(ox + p.x * k, oy + p.y * k, ox + (p.x + p.u) * k, oy + (p.y + p.v) * k), p.p * k, p.p * k);
                if (p.fill) target_->FillRoundedRectangle(rr, b);
                else target_->DrawRoundedRectangle(rr, b, sw, caps);
                break;
            }
            case IconOp::Circle: {
                const auto e = D2D1::Ellipse(pt(p.x, p.y), p.u * k, p.u * k);
                if (p.fill) target_->FillEllipse(e, b);
                else target_->DrawEllipse(e, b, sw);
                break;
            }
            case IconOp::Arc:
                if (auto g = arc_geometry(ox + p.x * k, oy + p.y * k, p.u * k, p.p, p.q))
                    target_->DrawGeometry(g.Get(), b, sw, caps);
                break;
        }
    }
}

void Graphics::push_clip(const RectF& r) {
    target_->PushAxisAlignedClip(d2d(r), D2D1_ANTIALIAS_MODE_ALIASED);
    clips_.push_back(r);
}
void Graphics::pop_clip() {
    if (clips_.empty()) return;
    target_->PopAxisAlignedClip();
    clips_.pop_back();
}
void Graphics::set_offset(float dx, float dy) {
    dx_ = dx;
    dy_ = dy;
    target_->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy));
}

ComPtr<IDWriteTextLayout> Graphics::layout(Role role, std::wstring_view s, float w, float h, const TextOpts& o) {
    ComPtr<IDWriteTextLayout> l;
    auto* fmt = formats_[static_cast<std::size_t>(role)].Get();
    if (FAILED(dwrite_->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), fmt, std::max(w, 0.f),
                                         std::max(h, 0.f), &l)))
        return nullptr;
    l->SetWordWrapping(o.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    l->SetTextAlignment(o.align == Align::Center     ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : o.align == Align::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                     : DWRITE_TEXT_ALIGNMENT_LEADING);
    l->SetParagraphAlignment(o.vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if ((role == Role::Metric || role == Role::Hero || o.tabular) && tabular_) l->SetTypography(tabular_.Get(), {0, static_cast<UINT32>(s.size())});
    // CSS letter-spacing: her karakterin ardina (sondaki de dahil; olcum ve cizim ayni yerlesimi kullanir).
    if (const float tr = tracking_[static_cast<std::size_t>(role)]; tr != 0.f) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(l.As(&l1))) l1->SetCharacterSpacing(0.f, tr, 0.f, {0, static_cast<UINT32>(s.size())});
    }
    return l;
}

SizeF Graphics::measure(Role role, std::wstring_view s, float max_w, bool wrap, bool tabular) {
    TextOpts o;
    o.wrap = wrap;
    o.tabular = tabular;
    auto l = layout(role, s, max_w, 100000.f, o);
    if (!l) return {};
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return {m.widthIncludingTrailingWhitespace, m.height};
}

Point Graphics::text_end(Role role, std::wstring_view s, float max_w) {
    if (s.empty()) return {};
    auto l = layout(role, s, max_w, 100000.f, TextOpts{});
    if (!l) return {};
    FLOAT x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS m{};
    if (FAILED(l->HitTestTextPosition(static_cast<UINT32>(s.size() - 1), TRUE, &x, &y, &m))) return {};
    return {x, y};
}

// Metin kutusunun ust kenari (DIP). Ilk satirin taban cizgisi cihaz pikselinde tam sayiya (+1/250 piksel) oturtulur.
// Neden: DirectWrite/Direct2D taban cizgisini yalniz bazi islemlerde tam piksele yuvarlar (kesirli olcekte, ornek %125'te
// kutu 425,8 DIP = 532,25 px). Yuvarlanmadiginda glif ceyrek piksel asagi, yuvarlandiginda net cizilir; hangisinin
// secildigi surece gore degisir ve canli bolgenin alt dikdortgen cizimi (ofset -69 px) ile tam cizim ayni metni farkli
// cizerdi (paint_probe: "region-only paint differs", 7099 piksel). Taban cizgisi onceden oturtulunca iki durum ayni
// bitmap'i uretir. Yalniz yalin oteleme donusumunde uygulanir (olcekli/dondurulmus cizimde dokunulmaz).
// D2D1_DRAW_TEXT_OPTIONS_NO_SNAP: D2D'nin kendi (surece bagli) yuvarlamasi kapatilir; konum yukaridaki gibi bizde.
float Graphics::text_y(const ComPtr<IDWriteTextLayout>& l, const DWRITE_TEXT_METRICS& m, float y) const {
    if (!target_ || !l) return y;
    float dpi_x = 96.f, dpi_y = 96.f;
    target_->GetDpi(&dpi_x, &dpi_y);
    D2D1_MATRIX_3X2_F tm;
    target_->GetTransform(&tm);
    if (tm._11 != 1.f || tm._22 != 1.f || tm._12 != 0.f || tm._21 != 0.f || !(dpi_y > 0.f)) return y;
    DWRITE_LINE_METRICS lm{};
    UINT32 n = 0;
    l->GetLineMetrics(&lm, 1, &n);
    if (n == 0) return y;
    const float k = dpi_y / 96.f;
    const float base_px = (y + tm._32 + m.top + lm.baseline) * k;
    const float snapped = std::round(base_px) + 0.004f;
    return y + (snapped - base_px) / k;
}

float Graphics::text(Role role, std::wstring_view s, const RectF& box, const Color& c, TextOpts o) {
    if (s.empty()) return 0;
    auto l = layout(role, s, box.w, box.h, o);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    if (audit_) {
        // Sondaki bosluk haric genislik; yukseklik kutuyu asmamali (yarim DIP tolerans).
        if (m.width > box.w + 0.5f || m.height > box.h + 0.5f)
            overflows_.push_back({std::wstring(s), box, m.width, m.height});
    }
    target_->DrawTextLayout(D2D1::Point2F(box.x, text_y(l, m, box.y)), l.Get(), brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
    return m.height;
}

float Graphics::svg(const SvgDoc& doc, const RectF& box, std::optional<Color> mono, const Color& current) {
    if (doc.shapes.empty() || doc.view_w <= 0 || doc.view_h <= 0 || box.w <= 0 || box.h <= 0) return 0;
    auto& geos = svg_cache_[&doc];
    if (geos.empty()) {
        for (const auto& s : doc.shapes) {
            ComPtr<ID2D1PathGeometry> g;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(factory_->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return 0;
            sink->SetFillMode(s.evenodd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);  // SVG fill-rule
            bool open = false;
            for (const auto& c : s.cmds) {
                if (c.op == 'M') {
                    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->BeginFigure(D2D1::Point2F(c.x, c.y), D2D1_FIGURE_BEGIN_FILLED);
                    open = true;
                } else if (c.op == 'L') {
                    sink->AddLine(D2D1::Point2F(c.x, c.y));
                } else if (c.op == 'Q') {
                    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(c.cx, c.cy), D2D1::Point2F(c.x, c.y)));
                } else if (c.op == 'Z' && open) {
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    open = false;
                }
            }
            if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (FAILED(sink->Close())) return 0;
            geos.push_back(g);
        }
    }
    // Oran korunur (logo uzatilmaz/sikistirilmaz).
    const float scale = std::min(box.w / doc.view_w, box.h / doc.view_h);
    const float dx = box.x, dy = box.y + (box.h - doc.view_h * scale) / 2;
    D2D1_MATRIX_3X2_F prev;
    target_->GetTransform(&prev);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(dx, dy) * prev);
    for (std::size_t i = 0; i < geos.size() && i < doc.shapes.size(); ++i) {
        const Color c = mono ? *mono : (doc.shapes[i].fill ? *doc.shapes[i].fill : current);
        target_->FillGeometry(geos[i].Get(), brush(c));
    }
    target_->SetTransform(prev);
    return doc.view_w * scale;
}

bool Graphics::image(const ImageRef& img, const RectF& box, float radius, std::uint32_t max_side) {
    if (!target_ || !img || !core::games::image_pixels_valid(*img, max_side) || !(box.w > 0) || !(box.h > 0)) return false;
    constexpr std::size_t kMaxImages = 256;  // kMaxGameRows'un ustu; asilirsa onbellek bosaltilir (yeniden kurulur)
    auto it = images_.find(img.get());
    if (it != images_.end() && it->second.owner.lock() != img) {  // adres baska bir resme gecmis
        images_.erase(it);
        it = images_.end();
    }
    if (it == images_.end()) {
        std::erase_if(images_, [](const auto& kv) { return kv.second.owner.expired(); });
        if (images_.size() >= kMaxImages) images_.clear();
        ImageEntry e;
        e.owner = img;
        const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(img->w, img->h), img->bgra.data(), img->w * 4, props, &e.bitmap))) return false;
        if (FAILED(target_->CreateBitmapBrush(
                e.bitmap.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),
                &e.brush)))
            return false;
        it = images_.emplace(img.get(), std::move(e)).first;
    }
    auto* b = it->second.brush.Get();
    // Firca uzayi: bitmap pikseli -> kutu (DIP). Dunya donusumu (kaydirma) firca icin de gecerlidir.
    b->SetTransform(D2D1::Matrix3x2F::Scale(box.w / static_cast<float>(img->w), box.h / static_cast<float>(img->h)) *
                    D2D1::Matrix3x2F::Translation(box.x, box.y));
    b->SetOpacity(alpha_);
    const float rad = std::max(0.f, std::min({radius, box.w / 2, box.h / 2}));
    if (rad > 0) target_->FillRoundedRectangle(D2D1::RoundedRect(d2d(box), rad, rad), b);
    else target_->FillRectangle(d2d(box), b);
    return true;
}

std::size_t Graphics::drop_released_images() {
    return std::erase_if(images_, [](const auto& kv) { return kv.second.owner.expired(); });
}

void Graphics::icon(wchar_t glyph, const RectF& box, const Color& c, float size) {
    if (!icon_format_) return;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dwrite_->CreateTextLayout(&glyph, 1, icon_format_.Get(), box.w, box.h, &l))) return;
    l->SetFontSize(size, {0, 1});
    target_->DrawTextLayout(D2D1::Point2F(box.x, box.y), l.Get(), brush(c));
}

}  // namespace ef::ui
