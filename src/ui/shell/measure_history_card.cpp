// P9-B5: Olcumler sayfasi — Son 3 test karti. measure_page.cpp'den ayrildi.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <map>
#include <string>

#include "core/measure/bench_text.h"
#include "core/measure/frame_stats.h"
#include "ui/shell/measure_page.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cm = core::measure;
using namespace mp;

// Son 3 test (spec 2026-11-12 C): en yeni uc kayit (kalici gecmis de yalniz uc kayit saklar). 34 DIP satir —
// zaman · test · sonuc · ortalama FPS · gecmisteki en yuksek FPS'e gore mini cubuk (gercek degerler); dar kartta iki
// satirli (48 DIP) satir. Satirin tamami dugmedir (Hit MeasureRecent + indeks): basinca o testin "Sonuc ayrintisi"
// maddeleri satirin altinda acilir (ayri kart yok). Ayrinti: kanit maddeleri (durum renkli nokta); kanit yoksa sonuc
// basligi ve olculen sayilar; hicbiri yoksa "Ayrinti yok".
void Screen::measure_history_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    UiState& s = mc.s;
    const std::int64_t now = mc.now;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float section_h = mc.section_h;
    const float sl = mc.sl;
    const float bl = mc.bl;
    const float label_h = mc.label_h;
    const float g0 = mc.g0;
    const float g1 = mc.g1;
    const float g2 = mc.g2;
    const bool tr = mc.tr;
    const bool dark = mc.dark;
    const Color divider = mc.divider;
    const float inner_w = slot.area.w - 2 * pad;
    const auto empty = t(StringId::MeasureResultNone);
    constexpr float kOneLineMinW = 760.f, kMiniBarW = 80.f, kMiniBarH = 6.f, kChevronW = 16.f;  // bilesen geometrisi (token degil)
    const std::size_t n = std::min<std::size_t>(m.history.size(), static_cast<std::size_t>(kMaxMeasureRecent));
    const bool none = n == 0;
    const bool one_line = inner_w >= kOneLineMinW;
    const float row_h = one_line ? theme_.row_h : theme_.row2_h;
    const float head_h = one_line && !none ? label_h + g0 : 0.f;
    const int open = s.measure_recent_open >= 0 && s.measure_recent_open < static_cast<int>(n) ? s.measure_recent_open : -1;
    // Acik satirin ayrinti maddeleri. En yeni satir bu oturumun sonucuysa kanit anahtarlari sonuctan okunur.
    std::vector<std::pair<std::wstring, Color>> detail;
    if (open >= 0) {
        const cm::ResultView& h = m.history[static_cast<std::size_t>(open)];
        const cm::ResultView& src = open == 0 && m.result && m.result->when_unix_ms == h.when_unix_ms ? *m.result : h;
        for (const auto& k : src.evidence_keys) {
            auto w = tk(k);
            if (w.empty() || std::any_of(detail.begin(), detail.end(), [&](const auto& e) { return e.first == w; })) continue;
            const auto status = cm::evidence_status(k);
            detail.push_back({std::move(w), status.empty() ? p.warning : tone_color(p, cm::status_tone(status))});
        }
        if (detail.empty()) {
            detail.push_back({result_headline(src), p.text_secondary});
            std::wstring nums;
            const auto add_num = [&](StringId label, const contracts::Measurement& ms, int dec, const wchar_t* unit) {
                if (!ms.has_value()) return;
                nums += (nums.empty() ? L"" : L" · ") + t(label) + L" " + widen(cm::format_number(*ms.value(), dec, tr)) + unit;
            };
            add_num(StringId::MeasureShortAvg, src.avg_fps, 1, L"");
            add_num(StringId::MeasureShortLow1, src.low1_fps, 1, L" FPS");
            add_num(StringId::MeasureShortP99, src.p99_ms, 2, L" ms");
            add_num(StringId::MeasureShortFrameWorst, src.worst_ms, 1, L" ms");
            if (!nums.empty()) detail.push_back({std::move(nums), p.text_secondary});
            if (detail.front().first.empty()) detail.erase(detail.begin());
        }
        if (detail.empty()) detail.push_back({t(StringId::MeasureRecentNoDetails), p.text_secondary});
    }
    const float dx = kChevronW + g1;  // ayrinti maddeleri satirin metin kenarindan
    std::vector<float> dh;
    float detail_h = 0;
    for (const auto& e : detail) {
        dh.push_back(std::max(g.measure(Role::Body, e.first, std::max(inner_w - dx - 16, 1.f)).h, bl));
        detail_h += dh.back() + g0;
    }
    if (!detail.empty()) detail_h += g1;
    const float content = none ? sl : head_h + static_cast<float>(n) * row_h + detail_h;
    RectF card;
    if (!slot.place(pad + section_h + g1 + content + (none ? pad : g1), card)) return;
    const bool card_visible = card.bottom() >= s.view_top && card.y <= s.view_bottom;
    if (card_visible) {
        draw_card(g, card, false, depth::kPanelCardRadius);
        g.text(Role::Section, t(StringId::MeasureRecentTitle), {card.x + pad, card.y + pad, inner_w, section_h}, p.text_primary);
    } else {
        log_card(card);
    }
    const float x = card.x + pad;
    float y = card.y + pad + section_h + g1;
    if (none) {
        // A: bos durum en cok bir kisa satir.
        if (card_visible) g.text(Role::Secondary, empty, {x, y, inner_w, sl}, p.text_secondary, {Align::Leading, false, true});
        hits.add_text(seq.take(), {x, y, inner_w, sl}, a11y_join(t(StringId::MeasureRecentTitle), empty));
        return;
    }
    double max_fps = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (m.history[i].avg_fps.has_value()) max_fps = std::max(max_fps, *m.history[i].avg_fps.value());
    const GradientStop bar_stops[] = {{0.f, mix(p.surface_raised, p.accent, alpha_scale::kToggleOnTrack)}, {1.f, p.accent}};
    const auto missing = t(StringId::MetricMissing);
    // Sutunlar: [ok][zaman][test][sonuc][ort. FPS][cubuk]. Zaman ve FPS sutunu en genis gercek degere gore; kalan alan
    // test (%42) ve sonuc (%58) arasinda bolunur.
    const auto col_w = [&](StringId id) { return std::ceil(g.measure(Role::Label, t(id), kUnbounded, false).w) + 1.f; };
    const float tx = x + dx, tw_all = std::max(inner_w - dx, 1.f);
    const float time_w = std::max(col_w(StringId::HistoryColTime),
                                  std::ceil(g.measure(Role::Secondary, date_text(m.history.front().when_unix_ms, tr), kUnbounded, false, true).w) + 2.f);
    const float fps_w = std::max({col_w(StringId::MeasureColFps), std::ceil(g.measure(Role::Body, missing, kUnbounded, false).w) + 1.f,
                                  std::ceil(g.measure(Role::Body, widen(cm::format_number(max_fps, 1, tr)), kUnbounded, false, true).w) + 2.f});
    const float flex = std::max(tw_all - time_w - fps_w - kMiniBarW - 4 * g2, 2.f);
    const float title_w = std::floor(flex * 0.42f), result_w = flex - title_w;
    const float title_x = tx + time_w + g2, result_x = title_x + title_w + g2, fps_x = result_x + result_w + g2, bar_x = x + inner_w - kMiniBarW;
    if (one_line) {
        if (card_visible) {
            const TextOpts lo{Align::Leading, false, true};
            g.text(Role::Label, t(StringId::HistoryColTime), {tx, y, time_w, label_h}, p.text_secondary, lo);
            g.text(Role::Label, t(StringId::MeasureColTest), {title_x, y, title_w, label_h}, p.text_secondary, lo);
            g.text(Role::Label, t(StringId::MeasureColResult), {result_x, y, result_w, label_h}, p.text_secondary, lo);
            g.text(Role::Label, t(StringId::MeasureColFps), {fps_x, y, fps_w, label_h}, p.text_secondary, {Align::Trailing, false, true});
        }
        y += head_h;
    }
    const float vpad2 = std::max((theme_.row2_h - bl - sl) / 2, 0.f);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& h = m.history[i];
        const Hit id = hit_of(HitKind::MeasureRecent, i);
        const bool is_open = open == static_cast<int>(i);
        const RectF row{x, y, inner_w, row_h};
        const auto src = t(h.source == cm::TestSource::Builtin ? StringId::TestBuiltin
                           : h.source == cm::TestSource::InGame ? StringId::TestGame
                                                               : StringId::TestEndurance);
        // M03: Uygulama Testi'nde "Uygulama Testi · Sahne kademesi: orta"; ic kimlik gosterilmez.
        const auto ttl = result_title(h);
        const std::wstring title = ttl.empty() ? src : (h.source == cm::TestSource::Builtin ? src + L" · " + ttl : ttl + L" · " + src);
        const bool has_fps = h.avg_fps.has_value();
        const std::wstring fps = has_fps ? widen(cm::format_number(*h.avg_fps.value(), 1, tr)) : h.balance ? std::wstring(L"—") : missing;
        const std::wstring headline = result_headline(h);
        const std::wstring when = date_text(h.when_unix_ms, tr);
        ++s.measure_recent_rows;
        // UIA: satirin tamami tek dugme (Selected = ayrinti acik); ad kisaltilmadan.
        hits.add(id, row, a11y_join(a11y_join(a11y_join(title, headline), when), t(StringId::MeasureColFps) + L" " + fps), AccRole::Expander, is_open);
        hits.meta[id].help = t(StringId::MeasureResultDetail);
        if (y + row_h < s.view_top || y > s.view_bottom) {
            y += row_h;
            if (is_open) {
                // Gorunmeyen acik satirin ayrintisi cizilmez ama UIA agacinda kalir (kaydirmadan da okunur; buyuk
                // metin boyutunda satir gorunum disina dusebilir).
                std::wstring uia = t(StringId::MeasureResultDetail);
                for (const auto& d : detail) uia = a11y_join(std::move(uia), d.first);
                hits.add_text(seq.take(), {x + dx, y, std::max(inner_w - dx, 1.f), detail_h}, std::move(uia));
                y += detail_h;
            }
            continue;
        }
        ++s.rows_drawn;
        const float hv = hover_value(s, id, now), pv = press_value(s, id, now);
        if (hv > 0.01f || pv > 0.01f || is_open)
            g.fill_round(row, theme_.radius_control, mix(p.surface, p.surface_raised, std::min(1.f, (is_open ? 0.6f : 0.f) + hv + pv)));
        if (i > 0 || one_line) g.fill({x, std::floor(y), inner_w, 1}, divider);
        {
            // Ac/kapa oku (sag: kapali, asagi: acik).
            const float ax = x + kChevronW / 2, ay = y + row_h / 2;
            constexpr float kArrow = 3.5f;
            const std::array<Point, 3> arrow = is_open ? std::array<Point, 3>{{{ax - kArrow, ay - kArrow / 2}, {ax, ay + kArrow / 2}, {ax + kArrow, ay - kArrow / 2}}}
                                                       : std::array<Point, 3>{{{ax - kArrow / 2, ay - kArrow}, {ax + kArrow / 2, ay}, {ax - kArrow / 2, ay + kArrow}}};
            g.polyline(arrow, 1.6f, mix(p.text_secondary, p.text_primary, hv));
        }
        const float frac = has_fps && max_fps > 0 ? static_cast<float>(std::clamp(*h.avg_fps.value() / max_fps, 0.0, 1.0)) : 0.f;
        const auto mini_bar = [&](float by) {
            const RectF br{bar_x, by, kMiniBarW, kMiniBarH};
            g.fill_round(br, kMiniBarH / 2, p.surface_raised);
            // Yuksek kontrastta gradyan yok: cerceveli iz + tek renk dolgu.
            if (dark) {
                g.fill_gradient_round(br, kMiniBarH / 2, bar_stops, frac);
            } else {
                g.stroke_round(br, kMiniBarH / 2, p.border);
                if (frac > 0) g.fill_round({br.x, br.y, std::max(kMiniBarH, br.w * frac), br.h}, kMiniBarH / 2, p.accent);
            }
        };
        TextOpts vo{Align::Trailing, false, true};
        vo.tabular = true;  // M13
        if (one_line) {
            g.text(Role::Secondary, when, {tx, y, time_w, row_h}, p.text_secondary, {Align::Leading, false, true, true});
            g.text(Role::Body, fit_text(g, Role::Body, title, title_w), {title_x, y, title_w, row_h}, p.text_primary, {Align::Leading, false, true});
            g.text(Role::Secondary, fit_text(g, Role::Secondary, headline, result_w), {result_x, y, result_w, row_h}, p.text_secondary,
                   {Align::Leading, false, true});
            g.text(Role::Body, fps, {fps_x, y, fps_w, row_h}, has_fps ? p.text_primary : p.text_secondary, vo);
            if (has_fps && max_fps > 0) mini_bar(y + (row_h - kMiniBarH) / 2);
        } else {
            const std::wstring val = has_fps ? fps + L" FPS" : fps;
            const float vw = std::ceil(g.measure(Role::Body, val, kUnbounded, false, true).w) + 1.f;
            const float tw = std::max(tw_all - vw - g2, 1.f);
            g.text(Role::Body, fit_text(g, Role::Body, title, tw), {tx, y + vpad2, tw, bl}, p.text_primary, {Align::Leading, false});
            vo.vcenter = false;
            g.text(Role::Body, val, {x + inner_w - vw, y + vpad2, vw, bl}, has_fps ? p.text_primary : p.text_secondary, vo);
            const bool bar = has_fps && max_fps > 0 && tw_all > 2 * kMiniBarW;
            const float mw = std::max(bar ? tw_all - kMiniBarW - g2 : tw_all, 1.f);
            const auto meta = headline + L" · " + when;
            g.text(Role::Secondary, fit_text(g, Role::Secondary, meta, mw), {tx, y + vpad2 + bl, mw, sl}, p.text_secondary, {Align::Leading, false});
            if (bar) mini_bar(y + vpad2 + bl + (sl - kMiniBarH) / 2);
        }
        if (s.focus_visible && s.focus == id) focus_ring(g, row, theme_.radius_control, hits);
        y += row_h;
        if (is_open) {
            // Sonuc ayrintisi maddeleri (satirin altinda; durum renkli nokta + metin).
            const float ix = x + dx;
            float iy = y + g0;
            std::wstring uia = t(StringId::MeasureResultDetail);
            for (std::size_t k = 0; k < detail.size(); ++k) {
                g.fill_circle(ix + 4, iy + bl / 2, 3, detail[k].second);
                g.text(Role::Body, detail[k].first, {ix + 16, iy, std::max(inner_w - dx - 16, 1.f), dh[k]}, p.text_primary);
                uia = a11y_join(std::move(uia), detail[k].first);
                iy += dh[k] + g0;
            }
            hits.add_text(seq.take(), {ix, y, std::max(inner_w - dx, 1.f), detail_h}, std::move(uia));
            y += detail_h;
        }
    }
}

}  // namespace ef::ui
