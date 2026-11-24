// OPT-08: Genel Bakis'ta Performans Acik sonrasi "Once / sonra" karti. Degerler olculen 1 Hz
// orneklerin ortalamasidir; fark %95 Welch araligiyla verilir, aralik 0'i iceriyorsa "anlamli fark yok".
// Tahmin/varsayilan deger gosterilmez.
#include <windows.h>

#include <cmath>
#include <string>

#include "core/measure/view.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cp = core::perf;

std::wstring format_bytes(std::uint64_t b, bool tr);  // cleanup_page.cpp

namespace {
std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring sub(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
bool is_pct(const std::string& key) { return key == "cpu_pct" || key == "gpu_pct" || key == "ram_used_pct"; }
std::wstring fmt(const std::string& key, double v, bool tr, bool signed_) {
    std::wstring sign = signed_ ? (v > 0 ? L"+" : v < 0 ? L"−" : L"") : L"";
    const double a = signed_ ? std::fabs(v) : v;
    if (is_pct(key)) {
        const auto n = wide(core::measure::format_number(a, 1, tr));
        return sign + (tr ? L"%" + n : n + L"%");
    }
    return sign + format_bytes(static_cast<std::uint64_t>(std::llround(std::max(0.0, a))), tr);
}
}  // namespace

void Screen::before_after_card(Graphics& g, CardSlot& slot, const ViewModel& vm) {
    using Stage = cp::ReportCollector::Stage;
    const auto& ba = vm.before_after;
    if (vm.perf.phase != core::perf::Phase::On || ba.stage == Stage::Idle || ba.stage == Stage::Applying) return;
    const auto& p = pal();
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;

    struct Line {
        std::wstring label, value, verdict;
        Color vc;
    };
    std::vector<Line> lines;
    std::wstring status;
    if (ba.stage == Stage::CollectingAfter)
        status = sub(sub(t(StringId::BaCollecting), L"{n}", std::to_wstring(ba.after_n)), L"{total}", std::to_wstring(ba.target));
    if (ba.stage == Stage::Done && ba.report) {
        for (const auto& m : ba.report->metrics) {
            Line l;
            if (m.key == "cpu_pct") l.label = t(StringId::BaMetricCpu);
            else if (m.key == "gpu_pct") l.label = t(StringId::BaMetricGpu);
            else if (m.key == "ram_used_pct") l.label = t(StringId::BaMetricRam);
            else if (m.key == "ram_free_bytes") l.label = t(StringId::BaMetricRamFree);
            else if (m.key.rfind("disk_free_bytes:", 0) == 0) l.label = sub(t(StringId::BaMetricDisk), L"{root}", wide(m.key.substr(16)));
            else continue;
            if (m.mean_before && m.mean_after) l.value = fmt(m.key, *m.mean_before, tr, false) + L" → " + fmt(m.key, *m.mean_after, tr, false);
            switch (m.verdict) {
                case cp::Verdict::Significant:
                    l.verdict = sub(sub(sub(t(StringId::BaDiff), L"{diff}", fmt(m.key, *m.diff, tr, true)), L"{lo}", fmt(m.key, *m.ci_low, tr, true)),
                                    L"{hi}", fmt(m.key, *m.ci_high, tr, true));
                    if (m.improved) l.verdict += L" · " + t(*m.improved ? StringId::BaImproved : StringId::BaWorse);
                    l.vc = m.improved && *m.improved ? p.success : p.warning;
                    break;
                case cp::Verdict::NoSignificantDifference: l.verdict = t(StringId::BaNoDiff), l.vc = p.text_secondary; break;
                case cp::Verdict::InsufficientData: l.verdict = t(StringId::BaInsufficient), l.vc = p.text_secondary; break;
                case cp::Verdict::Unsupported: l.verdict = t(StringId::BaUnsupported), l.vc = p.text_secondary; break;
            }
            lines.push_back(std::move(l));
        }
    }
    // BOOST maketi OPT-08: "OPT-08 //" + baslik, sagda gercek ornek sayisi rozeti; her metrik bir well satiri (soluk etiket solda,
    // sagda mono "once -> sonra" + renkli sonuc; sigmazsa sonuc alt satira); altta 11px not.
    const bool dark = pal_ == &theme_.dark;
    constexpr float kIn = 15.f, kRowInX = 13.f, kRowInY = 7.f, kRowGap = 6.f, kHeadGap = 10.f;
    const float g1 = theme_.space(1), g2 = theme_.space(2);
    const float pad = kIn;
    const float inner_w = std::max(slot.area.w - 2 * pad, 1.f);
    const float head_h = theme_.role(Role::Title14).line;
    const float line12 = theme_.role(Role::Mono12).line;
    constexpr float kUnbounded = 10000.f;
    std::size_t n_b = 0, n_a = 0;
    if (ba.stage == Stage::Done && ba.report)
        for (const auto& m : ba.report->metrics) n_b = std::max(n_b, m.n_before), n_a = std::max(n_a, m.n_after);
    const std::wstring samples = n_b + n_a > 0 ? sub(sub(t(StringId::BaSamples), L"{b}", std::to_wstring(n_b)), L"{a}", std::to_wstring(n_a)) : std::wstring();
    const float badge_in = 8.f, badge_h = theme_.role(Role::Mono11).line + 4.f;
    const float badge_w = samples.empty() ? 0.f : std::min(std::ceil(g.measure(Role::Mono11, samples, kUnbounded, false).w) + 1.f + 2 * badge_in, inner_w * 0.5f);
    const auto note = t(StringId::BaNote);
    const float row_in_w = std::max(inner_w - 2 * kRowInX, 1.f);
    const float label_w = std::min(220.f, row_in_w * 0.4f);
    const float right_w = std::max(row_in_w - label_w - g2, 1.f);
    struct Geo {
        float vw = 0, dw = 0, h = 0;
        bool inline_ = true;
    };
    std::vector<Geo> geo;
    float total = 0;
    for (const auto& l : lines) {
        Geo q;
        q.vw = l.value.empty() ? 0.f : std::ceil(g.measure(Role::Mono12, l.value, kUnbounded, false).w) + 1.f;
        q.dw = std::ceil(g.measure(Role::Mono11, l.verdict, kUnbounded, false).w) + 1.f;
        q.inline_ = q.vw + (q.vw > 0 ? g2 : 0.f) + q.dw <= right_w;
        const float rh = q.inline_ ? line12 : (q.vw > 0 ? line12 : 0.f) + g.measure(Role::Mono11, l.verdict, right_w).h;
        q.h = 2 * kRowInY + std::max(g.measure(Role::Small12, l.label, label_w).h, rh);
        geo.push_back(q);
        total += q.h + kRowGap;
    }
    const float sh = status.empty() ? 0.f : g.measure(Role::Small12, status, inner_w).h + g2;
    const float nh = g.measure(Role::Small11, note, inner_w).h;
    RectF card;
    if (!slot.place(pad + head_h + kHeadGap + sh + total + nh + pad, card)) return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + pad;
    float y = card.y + pad;
    {
        const std::wstring code = L"OPT-08 //";
        const std::wstring title = t(StringId::BaTitle);
        const float cw = std::ceil(g.measure(Role::Mono10, code, kUnbounded, false).w) + 1.f;
        g.text(Role::Mono10, code, {x, y, cw, head_h}, dark ? p.accent : p.text_primary, {Align::Leading, false, true});
        const float tw = std::max(inner_w - cw - g1 - (badge_w > 0 ? badge_w + g2 : 0.f), 1.f);
        g.text(Role::Title14, fit_text(g, Role::Title14, title, tw), {x + cw + g1, y, tw, head_h}, p.text_primary, {Align::Leading, false, true});
        if (badge_w > 0) {
            const RectF br{x + inner_w - badge_w, y + (head_h - badge_h) / 2, badge_w, badge_h};
            if (dark) g.fill_round(br, 4.f, p.cyan.with_alpha(0.15f));
            else g.stroke_round(br, 4.f, p.border);
            g.text(Role::Mono11, fit_text(g, Role::Mono11, samples, br.w - 2 * badge_in), br, dark ? p.cyan : p.text_primary, {Align::Center, false, true});
        }
    }
    y += head_h + kHeadGap;
    if (sh > 0) {
        g.text(Role::Small12, status, {x, y, inner_w, sh - g2}, p.text_secondary);
        y += sh;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        const auto& q = geo[i];
        const RectF row{x, y, inner_w, q.h};
        draw_well(g, row);
        const float lx = x + kRowInX, ty = y + kRowInY;
        g.text(Role::Small12, l.label, {lx, ty, label_w, q.h - 2 * kRowInY}, p.text_secondary, {Align::Leading, true});
        const float rx = lx + label_w + g2;
        TextOpts ro{Align::Trailing, false, false};
        ro.tabular = true;
        if (q.inline_) {
            if (q.vw > 0) {
                g.text(Role::Mono12, l.value, {rx + right_w - q.dw - (q.vw > 0 ? g2 : 0.f) - q.vw, ty, q.vw, line12}, p.text_primary, {Align::Leading, false, true});
            }
            g.text(Role::Mono11, l.verdict, {rx + right_w - q.dw, ty, q.dw, line12}, l.vc, {Align::Trailing, false, true});
        } else {
            float ry = ty;
            if (q.vw > 0) {
                g.text(Role::Mono12, l.value, {rx, ry, right_w, line12}, p.text_primary, ro);
                ry += line12;
            }
            g.text(Role::Mono11, l.verdict, {rx, ry, right_w, g.measure(Role::Mono11, l.verdict, right_w).h}, l.vc, {Align::Trailing, true});
        }
        y += q.h + kRowGap;
    }
    g.text(Role::Small11, note, {x, y - kRowGap + g1, inner_w, nh}, p.text_secondary);
}

}  // namespace ef::ui
