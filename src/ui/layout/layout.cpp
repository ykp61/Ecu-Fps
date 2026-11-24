#include "ui/layout/layout.h"

#include <algorithm>
#include <cmath>

namespace ef::ui {

ColumnSet content_columns(const RectF& area, const Theme& t, float min_column) {
    ColumnSet c;
    const float gap = t.card_gap;  // kartlar arasi bosluk (10 DIP)
    if (area.w >= 2.f * min_column + gap) {
        const float col_w = (area.w - gap) / 2.f;
        c.left = {area.x, area.y, col_w, area.h};
        c.right = {area.x + col_w + gap, area.y, area.w - col_w - gap, area.h};
        c.stacked = false;
    } else {
        c.left = area;
        c.right = {area.x, area.y, 0, area.h};
    }
    return c;
}

std::vector<float> equal_widths(float total_w, std::size_t n, float gap) {
    std::vector<float> w(n, 0.f);
    if (n == 0) return w;
    const float avail = std::max(total_w - gap * static_cast<float>(n - 1), 0.f);
    const float each = std::floor(avail / static_cast<float>(n));
    for (auto& v : w) v = each;
    w.back() = avail - each * static_cast<float>(n - 1);
    return w;
}

RowFrames equal_row_frames(const std::vector<std::vector<float>>& natural_heights, const std::vector<float>& widths, float gap,
                           bool stacked) {
    RowFrames out;
    const std::size_t n = natural_heights.size();
    out.cells.resize(n);
    out.cards.resize(n);
    const auto width_of = [&](std::size_t c) { return c < widths.size() ? std::max(widths[c], 0.f) : 0.f; };
    if (stacked) {
        // Tek sutun: kartlar okuma sirasiyla, dogal boylarinda alt alta.
        float y = 0;
        bool any = false;
        for (std::size_t c = 0; c < n; ++c) {
            const float w = width_of(c);
            float top = y, bottom = y;
            bool cell_any = false;
            for (const float h : natural_heights[c]) {
                if (h <= 0) {
                    out.cards[c].push_back({0, y, w, 0});
                    continue;
                }
                if (any) y += gap;
                if (!cell_any) top = y;
                out.cards[c].push_back({0, y, w, h});
                y += h;
                bottom = y;
                any = cell_any = true;
            }
            out.cells[c] = {0, top, w, cell_any ? bottom - top : 0.f};
        }
        out.height = y;
        return out;
    }
    // Yan yana: satir yuksekligi = en uzun hucre (kartlarin toplami + aralarindaki bosluk).
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::vector<float> cell_h(n, 0.f);
    std::vector<std::size_t> last(n, kNone);
    float row_h = 0;
    for (std::size_t c = 0; c < n; ++c) {
        for (std::size_t k = 0; k < natural_heights[c].size(); ++k) {
            const float h = natural_heights[c][k];
            if (h <= 0) continue;
            cell_h[c] += h + (last[c] != kNone ? gap : 0.f);
            last[c] = k;
        }
        row_h = std::max(row_h, cell_h[c]);
    }
    float x = 0;
    for (std::size_t c = 0; c < n; ++c) {
        const float w = width_of(c);
        float y = 0;
        bool any = false;
        for (std::size_t k = 0; k < natural_heights[c].size(); ++k) {
            float h = natural_heights[c][k];
            if (h <= 0) {
                out.cards[c].push_back({x, y, w, 0});
                continue;
            }
            if (any) y += gap;
            if (k == last[c]) h += row_h - cell_h[c];  // artan bosluk son karta
            out.cards[c].push_back({x, y, w, h});
            y += h;
            any = true;
        }
        out.cells[c] = {x, 0, w, any ? row_h : 0.f};
        x += w + gap;
    }
    out.height = row_h;
    return out;
}

namespace {

using CellList = std::vector<const std::vector<CardFn>*>;

// Cizimsiz olcum gecisi: her kartin dogal yuksekligi (kart yoksa 0).
std::vector<float> measure_cell(const std::vector<CardFn>& cards, float w) {
    std::vector<float> out;
    out.reserve(cards.size());
    for (const auto& fn : cards) {
        CardSlot s;
        s.area = {0, 0, w, 0};
        s.measuring = true;
        if (fn) fn(s);
        out.push_back(s.natural);
    }
    return out;
}

bool any_card(const std::vector<float>& hs) {
    return std::any_of(hs.begin(), hs.end(), [](float h) { return h > 0; });
}

// Olculmus hucreleri yerlestirir ve cizer. Kullanilan yuksekligi dondurur.
float draw_cells(const RectF& area, float gap, bool stacked, const CellList& cells, const std::vector<float>& widths,
                 const std::vector<std::vector<float>>& nat, RowLog* log) {
    const RowFrames f = equal_row_frames(nat, widths, gap, stacked);
    for (std::size_t c = 0; c < cells.size(); ++c)
        for (std::size_t k = 0; k < cells[c]->size(); ++k) {
            const RectF& r = f.cards[c][k];
            const CardFn& fn = (*cells[c])[k];
            if (r.h <= 0 || !fn) continue;
            CardSlot s;
            s.area = {area.x + r.x, area.y + r.y, r.w, 0};
            s.row_h = r.h;
            fn(s);
        }
    if (log && !stacked && cells.size() >= 2) {
        std::vector<RectF> row;
        for (const auto& c : f.cells) row.push_back({area.x + c.x, area.y + c.y, c.w, c.h});
        log->push_back(std::move(row));
    }
    return f.height;
}

}  // namespace

float EqualRow::run() const {
    CellList all;
    for (const auto& c : cells) all.push_back(&c);
    if (all.empty()) return 0.f;
    std::vector<float> widths = stacked ? std::vector<float>(all.size(), area.w) : equal_widths(area.w, all.size(), gap);
    std::vector<std::vector<float>> nat;
    for (std::size_t c = 0; c < all.size(); ++c) nat.push_back(measure_cell(*all[c], widths[c]));
    if (!stacked) {
        // Karti olmayan hucre satirdan duser; genislik degistigi icin kalan hucreler yeniden olculur.
        CellList live;
        for (std::size_t c = 0; c < all.size(); ++c)
            if (any_card(nat[c])) live.push_back(all[c]);
        if (live.empty()) return 0.f;
        if (live.size() != all.size()) {
            all = live;
            widths = equal_widths(area.w, all.size(), gap);
            nat.clear();
            for (std::size_t c = 0; c < all.size(); ++c) nat.push_back(measure_cell(*all[c], widths[c]));
        }
    }
    return draw_cells(area, gap, stacked || all.size() == 1, all, widths, nat, log);
}

float CardFlow::full(const CardFn& fn) {
    if (!fn) return 0.f;
    CardSlot s;
    s.area = {area.x, area.y + cursor, area.w, 0};
    fn(s);
    if (s.natural <= 0) return 0.f;
    cursor += s.frame.h + gap;
    return s.frame.h;
}

float CardFlow::row(std::vector<std::vector<CardFn>> cells) {
    EqualRow r;
    r.area = {area.x, area.y + cursor, area.w, 0};
    r.gap = gap;
    r.stacked = stacked;
    r.log = log;
    r.cells = std::move(cells);
    const float h = r.run();
    advance(h);
    return h;
}

void CardFlow::pairs(const std::vector<Item>& items) {
    if (stacked) {
        for (const auto& it : items) full(it.fn);
        return;
    }
    const std::vector<float> widths = equal_widths(area.w, 2, gap);
    std::vector<CardFn> left, right;  // esini bekleyen sol kart / sag kart
    std::vector<float> left_h;
    bool pending = false;
    const auto flush = [&] {  // esi olmayan kart: tam genislik, dogal boy
        if (pending) full(left.front());
        pending = false;
    };
    for (const auto& it : items) {
        if (!it.fn) continue;
        if (it.full) {
            flush();
            full(it.fn);
            continue;
        }
        if (!pending) {
            left.assign(1, it.fn);
            left_h = measure_cell(left, widths[0]);
            pending = any_card(left_h);
            continue;
        }
        right.assign(1, it.fn);
        const std::vector<float> right_h = measure_cell(right, widths[1]);
        if (!any_card(right_h)) continue;
        if (pair_max_ratio > 0 && std::max(left_h[0], right_h[0]) > pair_max_ratio * std::min(left_h[0], right_h[0])) {
            // Boylar benzer degil: bekleyen kart tam genislik, dogal boyda cizilir; bu kart siradaki kartla eslenmeyi
            // bekler (esi cikmazsa o da tam genislik alir). Okuma sirasi degismez.
            flush();
            left.assign(1, it.fn);
            left_h = measure_cell(left, widths[0]);
            pending = any_card(left_h);
            continue;
        }
        advance(draw_cells({area.x, area.y + cursor, area.w, 0}, gap, false, {&left, &right}, widths, {left_h, right_h}, log));
        pending = false;
    }
    flush();
}

TopbarFit fit_topbar(float avail, float margin, const TopbarWidths& w) {
    const float n2 = 2.f * static_cast<float>(w.tabs);
    const float all_gaps = w.tabs > 1 ? kTopbarTabGap * static_cast<float>(w.tabs - 1) : 0.f;
    // Sikisik bicim (BOOST maketi sigmadiginda): sekmeler bitisik (aralik 0) ve durum gostergesi hapsiz (nokta + metin).
    // Verilmediyse durum genisligi aynidir (yalniz araliklar kalkar).
    const float compact_status = w.status_compact > 0 ? w.status_compact : w.status;
    // pad = 0 icin gereken genislik: marka + sabit bosluk + etiketler + sekme araliklari + bosluk + durum gostergesi.
    const auto fixed = [&](TopbarMode mode, bool icons, bool roomy) {
        return (topbar_mark_only(mode) ? w.mark : w.lockup) + kTopbarBrandGap + w.labels - (topbar_short_labels(mode) ? w.short_saving : 0.f) +
               (roomy ? all_gaps : 0.f) + margin + (mode == TopbarMode::DotOnly ? w.status_dot : roomy ? w.status : compact_status) +
               (icons ? w.icons : 0.f);
    };
    // Her adimda once maket bicimi (araliklar + hap), sigmazsa ayni adim sikisik bicimle denenir; sonra sonraki adim.
    const auto at = [&](TopbarMode mode, bool icons, float& pad, bool& roomy) {
        for (const bool r : {true, false}) {
            pad = n2 > 0 ? std::floor((avail - fixed(mode, icons, r)) / n2) : kTopbarTabPad;
            if (pad >= kTopbarTabPadMin) {
                roomy = r;
                return true;
            }
        }
        return false;
    };
    float pad = kTopbarTabPad;
    bool roomy = true;
    // Sekme ikonlari yatay logo ve dar sekme araligiyla birlikte denenir (logo ambleme inebilir, aralik daralabilir);
    // yine sigmazsa once ikonlar birakilir, sonra eski sira (kisa etiket, yalniz nokta) uygulanir.
    if (w.icons > 0) {
        if (fixed(TopbarMode::Full, true, true) + n2 * kTopbarTabPad <= avail) return {TopbarMode::Full, kTopbarTabPad, true, true, true};
        if (at(TopbarMode::MarkOnly, true, pad, roomy)) return {TopbarMode::MarkOnly, std::min(pad, kTopbarTabPad), true, true, roomy};
    }
    // Yatay logo yalniz tam ic boslukla sigiyorsa kalir: once logo ambleme iner, sonra sekme araligi daralir.
    if (fixed(TopbarMode::Full, false, true) + n2 * kTopbarTabPad <= avail) return {TopbarMode::Full, kTopbarTabPad, true, false, true};
    for (const TopbarMode mode : {TopbarMode::MarkOnly, TopbarMode::ShortLabels, TopbarMode::DotOnly})
        if (at(mode, false, pad, roomy)) return {mode, std::min(pad, kTopbarTabPad), true, false, roomy};
    return {TopbarMode::DotOnly, kTopbarTabPadMin, false, false, false};
}

ShellLayout compute_shell(float w, float h, const Theme& t, const TopbarMetrics& m) {
    ShellLayout l;
    w = std::max(w, 0.f);
    h = std::max(h, 0.f);
    l.window = {0, 0, w, h};
    const float strip_h = std::min(std::max(t.strip_h, 0.f), h);
    const float bar_h = std::min(std::max(t.topbar_h, 0.f), std::max(h - strip_h, 0.f));
    l.strip = {0, 0, w, strip_h};
    l.topbar = {0, strip_h, w, bar_h};

    const float margin = std::max(t.page_margin, 0.f);  // 16: yan bosluk ve ust menu bolum aralari
    // Icerik sutunu: en fazla content_max_w, pencerede ortali (genis ekranda iki yanda esit bosluk).
    const float avail = std::min(std::max(w - 2 * margin, 0.f), std::max(t.content_max_w, 0.f));
    const float col_x = std::max((w - avail) / 2, 0.f);
    const auto clamp0 = [](float v) { return std::max(v, 0.f); };
    // Ust menu (spec 2026-11-12 B5): logo (murekkebi sutunun sol kenarinda) -> sabit bosluk -> sekmeler (hepsinde ayni
    // ic bosluk; kutular bitisik ve sabit: secili sekmenin zemini komsulari itmez) -> en sagda durum gostergesi (sutunun
    // sag kenarinda). Sekme genisligi = etiket (tam DIP'e yuvarlanir) + 2 x ic bosluk; etiket kutuda ortalanir.
    const std::size_t readiness_slot = 2;  // kNavOrder icinde Readiness
    static_assert(kNavOrder[2] == Page::Readiness);
    const auto label = [&](std::size_t i, bool short_readiness) {
        return std::ceil(clamp0((short_readiness && i == readiness_slot) ? m.label_w_short_readiness : m.label_w[i]));
    };
    TopbarWidths tw;
    tw.tabs = kNavTabCount;
    for (std::size_t i = 0; i < kNavTabCount; ++i) tw.labels += label(i, false);
    tw.short_saving = label(readiness_slot, false) - label(readiness_slot, true);
    tw.lockup = std::ceil(clamp0(m.lockup_w));
    tw.mark = std::ceil(clamp0(m.mark_w));
    tw.status = clamp0(m.status_w);
    tw.status_compact = clamp0(m.status_compact_w);
    tw.status_dot = clamp0(m.status_dot_w);
    const float icon_w = std::ceil(clamp0(m.nav_icon_w));
    tw.icons = icon_w * static_cast<float>(kNavTabCount);
    const TopbarFit fit = fit_topbar(avail, margin, tw);
    l.mode = fit.mode;
    l.tab_pad = fit.tab_pad;
    l.tab_gap = fit.roomy ? kTopbarTabGap : 0.f;
    l.status_pill = fit.roomy;
    l.nav_icons = fit.icons;
    const bool short_labels = topbar_short_labels(l.mode);
    const bool mark_only = topbar_mark_only(l.mode);
    const float brand_w = mark_only ? tw.mark : tw.lockup;
    const float st_w = l.mode == TopbarMode::DotOnly ? tw.status_dot : l.status_pill ? tw.status : (tw.status_compact > 0 ? tw.status_compact : tw.status);
    // Sekme: topbar_tab_h yuksekliginde (>= asgari dokunma hedefi), seridin icinde dikey ortali.
    const float tab_h = std::min(bar_h, std::max(t.topbar_tab_h, t.touch_min));
    const float tab_y = l.topbar.y + (bar_h - tab_h) / 2;
    // Marka kutusu = logonun murekkep kutusu; dikeyde sekme yazilarinin ortasina hizali (B5, secenek A).
    const float ink_h = clamp0(mark_only ? m.mark_h : m.lockup_h);
    const float brand_h = ink_h > 0 ? std::min(ink_h, bar_h) : bar_h;
    l.brand = {col_x, tab_y + (tab_h - brand_h) / 2, std::min(brand_w, avail), brand_h};
    l.status = {std::max(col_x + avail - st_w, col_x), l.topbar.y, std::min(st_w, avail), bar_h};
    // Sekme grubu logo ile durum gostergesinin arasinda ortali (maket justify-between); logodan en az kTopbarBrandGap.
    float nav_w = l.tab_gap * static_cast<float>(kNavTabCount - 1);
    for (std::size_t i = 0; i < kNavTabCount; ++i) nav_w += label(i, short_labels) + (l.nav_icons ? icon_w : 0.f) + 2 * l.tab_pad;
    float x = l.brand.right() + std::max(kTopbarBrandGap, std::floor((l.status.x - l.brand.right() - nav_w) / 2));
    for (std::size_t i = 0; i < kNavTabCount; ++i) {
        const float w_i = label(i, short_labels) + (l.nav_icons ? icon_w : 0.f) + 2 * l.tab_pad;
        l.nav[static_cast<std::size_t>(kNavOrder[i])] = {x, tab_y, w_i, tab_h};
        x += w_i + l.tab_gap;
    }
    l.nav[static_cast<std::size_t>(Page::Startup)] = {0, tab_y, 0, tab_h};
    l.nav[static_cast<std::size_t>(Page::System)] = {0, tab_y, 0, tab_h};

    const float top = l.topbar.bottom();
    l.content = {0, top, w, std::max(h - top, 0.f)};
    // Baslik seridi: tek satir (header_h); ustunde ve altinda kucuk bosluk. Govde alt kenarda page_margin birakir.
    const float pad = t.space(1);  // 8
    l.header = {col_x, top + pad, avail, std::min(std::max(t.header_h, 0.f), std::max(h - top - pad, 0.f))};
    const float body_y = l.header.bottom() + pad;
    l.body = {col_x, body_y, avail, std::max(h - body_y - margin, 0.f)};
    return l;
}

}  // namespace ef::ui
