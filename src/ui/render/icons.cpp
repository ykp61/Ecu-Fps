#include "ui/render/icons.h"

#include <algorithm>
#include <cmath>

namespace ef::ui {

namespace {

using namespace icon_prim;

// Cpu: cip govdesi + cekirdek + her kenarda iki bacak.
constexpr IconPrim kCpu[] = {
    rect(5, 5, 14, 14, 2), rect(9, 9, 6, 6, 1),
    line(9, 2, 9, 5),      line(15, 2, 15, 5),    // ust bacaklar
    line(9, 19, 9, 22),    line(15, 19, 15, 22),  // alt
    line(2, 9, 5, 9),      line(2, 15, 5, 15),    // sol
    line(19, 9, 22, 9),    line(19, 15, 22, 15),  // sag
};

// Ram: bellek cubugu + uc yonga + alt kenarda temas uclari (ortada centik boslugu).
constexpr IconPrim kRam[] = {
    rect(2, 6, 20, 10, 1.5f),
    line(7, 9.5f, 7, 12.5f), line(12, 9.5f, 12, 12.5f), line(17, 9.5f, 17, 12.5f),
    line(5, 16, 5, 19),      line(9, 16, 9, 19),        line(15, 16, 15, 19),      line(19, 16, 19, 19),
};

// Gpu: solda braket + kart govdesi + fan (gobekli) + alt kenarda yuva uclari.
constexpr IconPrim kGpu[] = {
    line(3, 3.5f, 3, 20.5f), line(3, 8, 6, 8),  // braket ve karta baglantisi
    rect(6, 6, 16, 11, 1.5f),
    circle(15, 11.5f, 3.2f), circle(15, 11.5f, 0.9f, true),
    line(9, 17, 9, 19.5f),   line(12.5f, 17, 12.5f, 19.5f),
};

// Disk: surucu govdesi + plak + mil + durum cizgisi ve isigi.
constexpr IconPrim kDisk[] = {
    rect(4, 2.5f, 16, 19, 2),
    circle(12, 9.5f, 4),          circle(12, 9.5f, 0.9f, true),
    line(7.5f, 17.5f, 11, 17.5f), circle(16.2f, 17.5f, 0.9f, true),
};

// Net: kure + ekvator + iki boylam yayi (kutuplardan gecen buyuk daire yaylari).
constexpr IconPrim kNet[] = {
    circle(12, 12, 9),
    line(3, 12, 21, 12),
    arc(20.125f, 12, 12.125f, 132.07f, 95.86f),  // bati boylami: (12,21) -> (8,12) -> (12,3)
    arc(3.875f, 12, 12.125f, -47.93f, 95.86f),   // dogu boylami: (12,3) -> (16,12) -> (12,21)
};

// Display: ekran + ayak + taban.
constexpr IconPrim kDisplay[] = {
    rect(2.5f, 4, 19, 12.5f, 1.5f),
    line(12, 16.5f, 12, 20), line(8, 20, 16, 20),
};

// Windows: dort kare pencere.
constexpr IconPrim kWindows[] = {
    rect(3.5f, 3.5f, 7.5f, 7.5f, 1), rect(13, 3.5f, 7.5f, 7.5f, 1),
    rect(3.5f, 13, 7.5f, 7.5f, 1),   rect(13, 13, 7.5f, 7.5f, 1),
};

// --- Ust menu sekme ikonlari (ince cizgi, yuvarlak kose) ---
// Panel: 2x2 kutucuk.
constexpr IconPrim kNavOverview[] = {
    rect(3, 3, 8, 8, 2), rect(13, 3, 8, 8, 2), rect(3, 13, 8, 8, 2), rect(13, 13, 8, 8, 2),
};
// Oyunlar: oyun kolu (govde + yon tusu + iki dugme).
constexpr IconPrim kNavGames[] = {
    rect(2, 7, 20, 11, 5), line(7, 10, 7, 15), line(4.5f, 12.5f, 9.5f, 12.5f),
    circle(16, 11, 1.3f, true), circle(18.5f, 14, 1.3f, true),
};
// Oyun hazirligi: daire icinde onay isareti.
constexpr IconPrim kNavReadiness[] = {
    circle(12, 12, 9), line(8, 12.5f, 11, 15.5f), line(11, 15.5f, 16.5f, 9),
};
// Olcumler: yarim gosterge + ibre + mil.
constexpr IconPrim kNavMeasurements[] = {
    arc(12, 16, 9, 180, 180), line(12, 16, 16.5f, 10.5f), circle(12, 16, 1.6f, true),
};
// Guncellemeler: asagi ok + tepsi.
constexpr IconPrim kNavUpdates[] = {
    line(12, 3, 12, 15), line(7.5f, 10.5f, 12, 15), line(16.5f, 10.5f, 12, 15), line(4, 20, 20, 20),
};
// Sistem araclari: yonga (govde + cekirdek + bacaklar).
constexpr IconPrim kNavSystem[] = {
    rect(6, 6, 12, 12, 2),  rect(9.5f, 9.5f, 5, 5, 1),
    line(9, 2.5f, 9, 6),    line(15, 2.5f, 15, 6),    line(9, 18, 9, 21.5f),  line(15, 18, 15, 21.5f),
    line(2.5f, 9, 6, 9),    line(2.5f, 15, 6, 15),    line(18, 9, 21.5f, 9),  line(18, 15, 21.5f, 15),
};
// Gecmis: saat.
constexpr IconPrim kNavHistory[] = {
    circle(12, 12, 9), line(12, 7, 12, 12), line(12, 12, 15.5f, 14),
};
// Ayarlar: iki surgu (cizgi topuzun cevresinde kesilir).
constexpr IconPrim kNavSettings[] = {
    line(4, 7, 6.4f, 7),   circle(9, 7, 2.6f),  line(11.6f, 7, 20, 7),
    line(4, 17, 12.4f, 17), circle(15, 17, 2.6f), line(17.6f, 17, 20, 17),
};

}  // namespace

std::span<const IconPrim> icon_prims(Icon i) noexcept {
    switch (i) {
        case Icon::Cpu: return kCpu;
        case Icon::Ram: return kRam;
        case Icon::Gpu: return kGpu;
        case Icon::Disk: return kDisk;
        case Icon::Net: return kNet;
        case Icon::Display: return kDisplay;
        case Icon::Windows: return kWindows;
        case Icon::NavOverview: return kNavOverview;
        case Icon::NavGames: return kNavGames;
        case Icon::NavReadiness: return kNavReadiness;
        case Icon::NavMeasurements: return kNavMeasurements;
        case Icon::NavUpdates: return kNavUpdates;
        case Icon::NavSystem: return kNavSystem;
        case Icon::NavHistory: return kNavHistory;
        case Icon::NavSettings: return kNavSettings;
    }
    return {};
}

IconBounds icon_prim_bounds(const IconPrim& p, bool with_stroke) noexcept {
    IconBounds b;
    switch (p.op) {
        case IconOp::Line:
            b = {std::min(p.x, p.u), std::min(p.y, p.v), std::max(p.x, p.u), std::max(p.y, p.v)};
            break;
        case IconOp::Rect:
            b = {p.x, p.y, p.x + p.u, p.y + p.v};
            break;
        case IconOp::Circle:
            b = {p.x - p.u, p.y - p.u, p.x + p.u, p.y + p.u};
            break;
        case IconOp::Arc: {
            // Uc noktalar + yayin gectigi eksen yonleri (0/90/180/270 derece).
            constexpr float kDeg = 3.14159265f / 180.f;
            const float a = std::min(p.p, p.p + p.q), z = std::max(p.p, p.p + p.q);
            if (z - a >= 360.f) {
                b = {p.x - p.u, p.y - p.u, p.x + p.u, p.y + p.u};
                break;
            }
            const float xa = p.x + p.u * std::cos(a * kDeg), ya = p.y + p.u * std::sin(a * kDeg);
            const float xz = p.x + p.u * std::cos(z * kDeg), yz = p.y + p.u * std::sin(z * kDeg);
            b = {std::min(xa, xz), std::min(ya, yz), std::max(xa, xz), std::max(ya, yz)};
            const auto crosses = [&](float k) {  // a..z araliginda k + 360n var mi
                const float n = std::ceil((a - k) / 360.f);
                return k + n * 360.f <= z;
            };
            if (crosses(0.f)) b.max_x = p.x + p.u;
            if (crosses(90.f)) b.max_y = p.y + p.u;
            if (crosses(180.f)) b.min_x = p.x - p.u;
            if (crosses(270.f)) b.min_y = p.y - p.u;
            break;
        }
    }
    const bool stroked = p.op == IconOp::Line || p.op == IconOp::Arc || !p.fill;
    if (with_stroke && stroked) {
        const float h = kIconStroke / 2;
        b = {b.min_x - h, b.min_y - h, b.max_x + h, b.max_y + h};
    }
    return b;
}

}  // namespace ef::ui
