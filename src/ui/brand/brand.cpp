#include "ui/brand/brand.h"

#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <cstring>
#include <vector>

namespace ef::ui::brand::embedded_data {
extern const std::string_view lockup_dark, lockup_light, lockup_white, mark_color, mark_current, mark_optical16, mark_optical20,
    mark_optical24;
}

namespace ef::ui::brand {

using Microsoft::WRL::ComPtr;

std::string_view embedded_svg(Logo l) noexcept {
    switch (l) {
        case Logo::LockupDark: return embedded_data::lockup_dark;
        case Logo::LockupLight: return embedded_data::lockup_light;
        case Logo::LockupWhite: return embedded_data::lockup_white;
        case Logo::MarkColor: return embedded_data::mark_color;
        case Logo::MarkCurrent: return embedded_data::mark_current;
        case Logo::MarkOptical16: return embedded_data::mark_optical16;
        case Logo::MarkOptical20: return embedded_data::mark_optical20;
        case Logo::MarkOptical24: return embedded_data::mark_optical24;
    }
    return {};
}

const SvgDoc& logo(Logo l) {
    static const auto docs = [] {
        std::array<SvgDoc, std::size(kAllLogos)> d{};
        for (std::size_t i = 0; i < d.size(); ++i) d[i] = parse_svg(embedded_svg(kAllLogos[i])).value_or(SvgDoc{});
        return d;
    }();
    return docs[static_cast<std::size_t>(l)];
}

Logo mark_for_px(int px) noexcept {
    if (px <= 16) return Logo::MarkOptical16;
    if (px <= 20) return Logo::MarkOptical20;
    if (px <= 24) return Logo::MarkOptical24;
    return Logo::MarkCurrent;
}

namespace {
// LoadIconWithScaleDown yalniz Common Controls v6'da (uygulama manifesti). Statik baglanirsa manifestsiz
// araclar hic yuklenmez; bu yuzden calisma aninda cozulur, yoksa LoadImageW (en yakin boyut).
HICON load_icon_px(HINSTANCE inst, int id, int px) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, PCWSTR, int, int, HICON*);
    static const Fn fn = [] {
        HMODULE m = GetModuleHandleW(L"comctl32.dll");
        if (!m) m = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "LoadIconWithScaleDown"))) : nullptr;
    }();
    HICON h = nullptr;
    if (fn && SUCCEEDED(fn(inst, MAKEINTRESOURCEW(id), px, px, &h))) return h;
    return static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(id), IMAGE_ICON, px, px, LR_DEFAULTCOLOR));
}
}  // namespace

HICON load_app_icon(HINSTANCE inst, int px) { return load_icon_px(inst, IDI_ECUFPS_APP, px); }

HICON load_tray_icon(HINSTANCE inst, int id, int px) { return load_icon_px(inst, id, px); }

namespace {
ComPtr<ID2D1PathGeometry> geometry(ID2D1Factory* f, const SvgShape& s, float scale, float ox, float oy) {
    ComPtr<ID2D1PathGeometry> g;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(f->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return nullptr;
    sink->SetFillMode(s.evenodd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);  // SVG fill-rule
    bool open = false;
    auto pt = [&](float x, float y) { return D2D1::Point2F(ox + x * scale, oy + y * scale); };
    for (const auto& c : s.cmds) {
        switch (c.op) {
            case 'M':
                if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->BeginFigure(pt(c.x, c.y), D2D1_FIGURE_BEGIN_FILLED);
                open = true;
                break;
            case 'L': sink->AddLine(pt(c.x, c.y)); break;
            case 'Q': sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(c.cx, c.cy), pt(c.x, c.y))); break;
            case 'Z':
                if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                open = false;
                break;
            default: break;
        }
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return nullptr;
    return g;
}
}  // namespace

HICON render_mark_icon(int px, const Color& fg) {
    if (px <= 0 || px > 256) return nullptr;
    // Kucuk boyutta boyuta ozel optik cizim (paketin tepsi icin hazirladigi, ok ucu dahil tam amblem), buyukte ana
    // amblem. Yuksek kontrast tepsi ikonu tam amblemi kullanir (asset-map tray.state_shape).
    const SvgDoc& doc = logo(mark_for_px(px));
    if (doc.shapes.empty()) return nullptr;
    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return nullptr;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return nullptr;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(static_cast<UINT>(px), static_cast<UINT>(px), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
        return nullptr;
    ComPtr<ID2D1RenderTarget> rt;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(factory->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return nullptr;
    // Paketin kare viewBox'u ikon tuvalidir (ic bosluk ve optik cizimlerde piksel izgarasi tuvalin parcasidir):
    // viewBox ikon kenarina olceklenir; optik cizim kendi boyutunda 1:1 piksele oturur.
    if (doc.view_w <= 0 || doc.view_h <= 0) return nullptr;
    const float scale = static_cast<float>(px) / std::max(doc.view_w, doc.view_h);
    const float ox = (static_cast<float>(px) - doc.view_w * scale) / 2;
    const float oy = (static_cast<float>(px) - doc.view_h * scale) / 2;
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    rt->CreateSolidColorBrush(D2D1::ColorF(fg.r, fg.g, fg.b, 1.f), &brush);
    for (const auto& s : doc.shapes)
        if (auto g = geometry(factory.Get(), s, scale, ox, oy)) rt->FillGeometry(g.Get(), brush.Get());
    if (FAILED(rt->EndDraw())) return nullptr;
    // Premultiplied BGRA -> 32 bit DIB + AND maskesi -> HICON
    std::vector<BYTE> pixels(static_cast<std::size_t>(px) * px * 4);
    if (FAILED(bmp->CopyPixels(nullptr, static_cast<UINT>(px) * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return nullptr;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = px;
    bi.bV5Height = -px;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    std::memcpy(bits, pixels.data(), pixels.size());
    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace ef::ui::brand
