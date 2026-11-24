#pragma once
// Siki yerlesim (spec 2026-11-11 §3): vektor ikon seti. Her ikon 24 birimlik izgarada ilkel sekillerin (cizgi,
// dikdortgen, daire, yay) saf constexpr tablosudur; font ya da bitmap degildir. Cizgi kalinligi 1,6 birim, uclar
// yuvarlak. Cizim Graphics::draw_icon ile yapilir; renk cagirandan gelir (yuksek kontrastta sistem metin rengi).
// Platformdan bagimsiz (birim testlenir): her ilkel, cizgi kalinligi dahil 0..24 kutusunda kalir.

#include <cstddef>
#include <cstdint>
#include <span>

namespace ef::ui {

// Nav*: ust menu sekme ikonlari (kNavOrder sirasi; sahip secimi 2026-11-14 "Secenek 1 - ince cizgi").
enum class Icon {
    Cpu, Ram, Gpu, Disk, Net, Display, Windows,
    NavOverview, NavGames, NavReadiness, NavMeasurements, NavUpdates, NavSystem, NavHistory, NavSettings
};
inline constexpr std::size_t kIconCount = 15;

inline constexpr float kIconGrid = 24.f;    // izgara kenari (birim)
inline constexpr float kIconStroke = 1.6f;  // cizgi kalinligi (birim)

enum class IconOp : std::uint8_t { Line, Rect, Circle, Arc };

// Ilkel (izgara birimi). Alanlarin anlami op'a baglidir; kurucu islevleri (icon_prim::*) kullanin.
struct IconPrim {
    IconOp op = IconOp::Line;
    float x = 0, y = 0;  // Line: baslangic; Rect: sol ust; Circle/Arc: merkez
    float u = 0, v = 0;  // Line: bitis; Rect: genislik, yukseklik; Circle/Arc: yaricap (u)
    float p = 0, q = 0;  // Rect: kose yaricapi (p); Arc: baslangic acisi (p) ve tarama (q), derece
    bool fill = false;   // Rect/Circle: dolu (cizgi yok)
};

namespace icon_prim {
constexpr IconPrim line(float x1, float y1, float x2, float y2) { return {IconOp::Line, x1, y1, x2, y2, 0, 0, false}; }
constexpr IconPrim rect(float x, float y, float w, float h, float radius = 0, bool fill = false) {
    return {IconOp::Rect, x, y, w, h, radius, 0, fill};
}
constexpr IconPrim circle(float cx, float cy, float r, bool fill = false) { return {IconOp::Circle, cx, cy, r, 0, 0, 0, fill}; }
// Aci: 0 derece = saat 3 yonu, pozitif = saat yonu (Graphics::arc ile ayni).
constexpr IconPrim arc(float cx, float cy, float r, float start_deg, float sweep_deg) {
    return {IconOp::Arc, cx, cy, r, 0, start_deg, sweep_deg, false};
}
}  // namespace icon_prim

// Ikonun ilkelleri (cizim sirasi). Gecersiz deger -> bos.
std::span<const IconPrim> icon_prims(Icon i) noexcept;

// Ilkelin sinir kutusu (izgara birimi). with_stroke: cizgili ilkelde yarim kalinlik eklenir (dolu ilkelde eklenmez).
struct IconBounds {
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
};
IconBounds icon_prim_bounds(const IconPrim& p, bool with_stroke) noexcept;

}  // namespace ef::ui
