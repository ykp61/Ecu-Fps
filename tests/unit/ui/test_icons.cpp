#include <cmath>
#include <iterator>

#include "ef_test.h"
#include "ui/render/icons.h"

using namespace ef::ui;

namespace {
constexpr Icon kAll[] = {Icon::Cpu, Icon::Ram, Icon::Gpu, Icon::Disk, Icon::Net, Icon::Display, Icon::Windows,
                         Icon::NavOverview, Icon::NavGames, Icon::NavReadiness, Icon::NavMeasurements, Icon::NavUpdates, Icon::NavSystem, Icon::NavHistory, Icon::NavSettings};
}

// Spec 2026-11-11 §3: her ikon bos degil; her ilkel (cizgi kalinligi dahil) 24 birimlik izgara kutusunda kalir.
EF_TEST(ui_icons_non_empty_and_inside_grid) {
    static_assert(std::size(kAll) == kIconCount);
    static_assert(kIconGrid == 24.f && kIconStroke == 1.6f);  // spec: 24 birim izgara, 1,6 cizgi
    for (const Icon i : kAll) {
        const auto prims = icon_prims(i);
        EF_CHECK(!prims.empty());
        bool stroked = false;
        for (const auto& p : prims) {
            for (const bool with_stroke : {false, true}) {
                const auto b = icon_prim_bounds(p, with_stroke);
                EF_CHECK(b.min_x >= 0 && b.min_y >= 0 && b.max_x <= kIconGrid && b.max_y <= kIconGrid);
                EF_CHECK(b.max_x >= b.min_x && b.max_y >= b.min_y);
            }
            // Gecerli olculer: dikdortgen/daire/yay sifir boyutlu degil, kose yaricapi kenarin yarisini asmaz.
            if (p.op == IconOp::Rect) EF_CHECK(p.u > 0 && p.v > 0 && p.p >= 0 && p.p <= std::fmin(p.u, p.v) / 2);
            if (p.op == IconOp::Circle) EF_CHECK(p.u > 0);
            if (p.op == IconOp::Arc) EF_CHECK(p.u > 0 && std::fabs(p.q) > 0.01f && std::fabs(p.q) <= 360.f);
            if (p.op == IconOp::Line) EF_CHECK(p.x != p.u || p.y != p.v);
            stroked |= p.op == IconOp::Line || p.op == IconOp::Arc || !p.fill;
        }
        EF_CHECK(stroked);  // ikon yalniz dolu noktalardan olusmaz
    }
    EF_CHECK(icon_prims(static_cast<Icon>(99)).empty());
}

EF_TEST(ui_icon_arc_bounds_follow_the_swept_part) {
    using namespace icon_prim;
    // Tam daire yayi: merkez +- yaricap.
    const auto full = icon_prim_bounds(arc(12, 12, 5, 0, 360), false);
    EF_CHECK(std::fabs(full.min_x - 7) < 0.01f && std::fabs(full.max_x - 17) < 0.01f);
    // Ust yarim daire (180 -> 360): alt sinir merkez, ust sinir merkez - r.
    const auto top = icon_prim_bounds(arc(12, 12, 5, 180, 180), false);
    EF_CHECK(std::fabs(top.min_y - 7) < 0.01f && std::fabs(top.max_y - 12) < 0.01f);
    EF_CHECK(std::fabs(top.min_x - 7) < 0.01f && std::fabs(top.max_x - 17) < 0.01f);
    // Kure boylami: merkezi izgara disinda olan buyuk dairenin yalniz izgara icindeki yayi sayilir.
    const auto meridian = icon_prim_bounds(arc(20.125f, 12, 12.125f, 132.07f, 95.86f), false);
    EF_CHECK(std::fabs(meridian.min_x - 8) < 0.05f && meridian.max_x < 12.1f);
    EF_CHECK(std::fabs(meridian.min_y - 3) < 0.05f && std::fabs(meridian.max_y - 21) < 0.05f);
    // Cizgi kalinligi yalniz cizgili ilkelde eklenir.
    const auto dot = icon_prim_bounds(circle(12, 12, 1, true), true);
    EF_CHECK(std::fabs(dot.min_x - 11) < 0.01f);
    const auto ring = icon_prim_bounds(circle(12, 12, 1), true);
    EF_CHECK(std::fabs(ring.min_x - (11 - kIconStroke / 2)) < 0.01f);
}
