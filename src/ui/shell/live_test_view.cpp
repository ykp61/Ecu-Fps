// Canli test ekrani (spec 2026-11-12 A4): denge testi surerken ana pencerenin icerik alani (ust menunun alti) bu gorunume
// gecer. Sahne onizlemesi alana sigdirilir (en-boy korunur); ustune gosterge panelleri cizilir:
//   sol ust  : adim adi + asama n/9 + gecen sure, cozunurluk + yuk ayari, faz cipi, ilerleme cubugu
//   sag ust  : kare hizi (FPS) ve kare suresi (ms)
//   sol alt  : CPU (kare hazirlama) ve GPU (cizim) yuk cubuklari — kullanim rampasi renkleri, 12 parcali LED
//   orta alt : kare suresi cubuk grafigi (son saniyeler) + kapsam notu
//   sag alt  : "Durdurmak icin Esc" + "Testi durdur" (Hit::TestStop; mevcut durdurma yolu)
// Sayilar FrameTap'ten okunan olculmus degerlerdir (anlik onizleme; sonuc degildir). Olcum kovasi kapanmadan sayi
// gosterilmez ("—"); deger uydurulmaz. Yuksek kontrastta paneller opak sistem renkleriyle cizilir, rampa / parlama yok.
// Azaltilmis hareket onizlemeyi kapatmaz (test icerigidir); yalniz yuk cubugu gecisleri aninda olur.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/measure/live_view.h"
#include "core/measure/view.h"
#include "ui/shell/screen.h"
#include "ui/theme/ramp.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cm = core::measure;

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring replace_all(std::wstring s, std::wstring_view key, const std::wstring& val) {
    for (std::size_t pos = s.find(key); pos != std::wstring::npos; pos = s.find(key, pos + val.size()))
        s.replace(pos, key.size(), val);
    return s;
}
std::wstring pct_text(double v, bool turkish) {
    const auto n = std::to_wstring(static_cast<long long>(std::floor(v + 0.5)));
    return turkish ? L"%" + n : n + L"%";
}
constexpr float kUnbounded = 10000.f;
constexpr float kBarH = 6.f;       // ilerleme cubugu (Olcumler sayfasindaki calisan test seridiyle ayni)
constexpr float kLedH = 8.f;       // LED parca yuksekligi (Panel canli kutucuklariyla ayni)
constexpr float kLedGap = 3.f;
constexpr float kLedRadius = 2.f;
constexpr float kChartBarGap = 2.f;
constexpr std::size_t kChartMaxBars = cm::kLiveRingSize;
constexpr double kChartSpikeRatio = 1.2;  // en yavas kova en hizlidan bu kat yavassa uyari renginde (kucuk oynama isaretlenmez)
constexpr float kChartMinW = 200.f;   // grafik paneli bundan dar kalacaksa cizilmez (yuk paneli genisler)
constexpr float kLoadPanelMinW = 240.f, kLoadPanelMaxW = 340.f;
constexpr float kStepPanelMaxW = 560.f;
constexpr int kHudTweenMs = 220;      // yuk cubugu gecisi (ornekler ~4 Hz gelir)
}  // namespace

void Screen::set_live_sample(UiState& s, const cm::LiveSample& v, std::int64_t now) const {
    s.live_test = v;
    const int ms = effective_ms(kHudTweenMs, s.reduced_motion);
    const auto target = [&](Tween& t, bool& seen, const std::optional<double>& pct) {
        if (!pct) {
            seen = false;
            return;
        }
        const auto f = static_cast<float>(*pct);
        if (!seen) t = Tween(f);  // ilk deger (ya da eksikten donus): gecis yok
        else if (std::fabs(f - t.target()) > 0.05f) t.retarget(f, now, ms);
        seen = true;
    };
    target(s.live_cpu, s.live_cpu_seen, cm::live_cpu_load_pct(v));
    target(s.live_gpu, s.live_gpu_seen, cm::live_gpu_load_pct(v));
}

void Screen::paint_live_region(Graphics& g, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const RectF& r = s.live_region;
    if (r.w <= 0 || r.h <= 0) return;
    g.fill(r, pal().background);
    // Icerik sutunu (compute_shell ile ayni): en fazla content_max_w, pencerede ortali; ustte kucuk bosluk, altta kenar.
    const float margin = std::max(theme_.page_margin, 0.f);
    const float avail = std::min(std::max(r.w - 2 * margin, 0.f), std::max(theme_.content_max_w, 0.f));
    const float top = theme_.space(1);
    live_test_view(g, {r.x + std::max((r.w - avail) / 2, 0.f), r.y + top, avail, std::max(r.h - top - margin, 0.f)}, s, vm, now, hits);
}

void Screen::live_test_view(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const auto& m = vm.measure;
    const auto& lv = s.live_test;
    s.measure = m;  // odak sirasi (Testi durdur) icin
    if (area.w <= 0 || area.h <= 0) return;
    const bool dark = pal_ == &theme_.dark;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const float pad = theme_.card_pad;
    const float g0 = theme_.space(0), g1 = theme_.space(1);
    const float margin = theme_.space(2);
    const float section_h = theme_.role(Role::Section).line;
    const float sec_h = theme_.role(Role::Secondary).line;
    const float label_h = theme_.role(Role::Label).line;
    const float metric_h = theme_.role(Role::Metric).line;
    StatusSeq seq{6};  // Status0..5 Olcumler sayfasinin sabit durum metinleridir (iptal / hata bildirimi Status1)

    // --- Sahne: zemin + onizleme (alana sigdirilir, en-boy orani korunur, ortalanir) ---
    g.fill_round(area, theme_.radius_card, p.surface);
    float aw = 16.f, ah = 9.f;
    if (lv.preview && lv.preview->w > 0 && lv.preview->h > 0) aw = static_cast<float>(lv.preview->w), ah = static_cast<float>(lv.preview->h);
    else if (lv.width > 0 && lv.height > 0) aw = static_cast<float>(lv.width), ah = static_cast<float>(lv.height);
    float pw = area.w, ph = std::floor(pw * ah / aw);
    if (ph > area.h) {
        ph = area.h;
        pw = std::floor(ph * aw / ah);
    }
    const RectF view{area.x + std::floor((area.w - pw) / 2), area.y + std::floor((area.h - ph) / 2), pw, ph};
    const bool full_bleed = view.w >= area.w - 0.5f && view.h >= area.h - 0.5f;
    if (!g.image(lv.preview, view, full_bleed ? theme_.radius_card : 0.f, cm::kLivePreviewMaxSide)) {
        // Henuz kare yok (hazirlik) ya da onizleme uretilemiyor: bos sahne + neden. Sayilar yine canlidir.
        g.fill_round(view, full_bleed ? theme_.radius_card : 0.f, p.surface_raised);
        g.text(Role::Body, t(StringId::LiveViewNoPreview), view, p.text_secondary, {Align::Center, false, true});
    }
    g.stroke_round(area, theme_.radius_card, p.border);

    // Gosterge paneli zemini: onizlemenin ustunde okunur kalsin diye arka plan renginde ortu; HC'de opak sistem rengi.
    const auto panel = [&](const RectF& r) {
        g.fill_round(r, theme_.radius_control, dark ? p.background.with_alpha(alpha_scale::kHudScrim) : p.background);
        g.stroke_round(r, theme_.radius_control, dark ? p.border.with_alpha(alpha_scale::kHudBorder) : p.border);
    };
    const RectF inner = area.inset(margin);
    if (inner.w <= 0 || inner.h <= 0) return;

    // ---- Ust satir: adim paneli (sol) + kare hizi paneli (sag); ayni yukseklik ----
    const auto fps_label = t(StringId::LiveViewFrameRate);
    const auto ft_label = t(StringId::MeasureFrameTime);
    const auto frame_ms = cm::live_frame_ms(lv);
    const std::wstring fps_value = frame_ms ? widen(cm::format_number(lv.fps, lv.fps < 99.95 ? 1 : 0, tr)) : std::wstring(L"—");
    const std::wstring ms_value = frame_ms ? widen(cm::format_number(*frame_ms, 2, tr)) + L" ms" : std::wstring(L"—");
    const std::wstring ft_line = ft_label + L" " + ms_value;
    // Genislik en genis olasi degere gore sabitlenir (sayilar degisirken panel oynamaz).
    const float fps_unit_w = std::ceil(g.measure(Role::Secondary, L"FPS", kUnbounded, false).w) + 1.f;
    const float fps_num_w = std::ceil(g.measure(Role::Metric, L"0000", kUnbounded, false, true).w) + 1.f;
    const float ft_w = std::ceil(g.measure(Role::Secondary, ft_label + L" 000,00 ms", kUnbounded, false, true).w) + 1.f;
    const float fps_label_w = std::ceil(g.measure(Role::Label, fps_label, kUnbounded, false).w) + 1.f;
    const float fps_panel_w = std::min(std::max({fps_num_w + g0 + fps_unit_w, ft_w, fps_label_w}) + 2 * pad, inner.w);
    const float top_h = pad + std::max(section_h + g0 + sec_h + g1 + kBarH, label_h + metric_h + sec_h) + pad;
    const RectF fps_panel{inner.right() - fps_panel_w, inner.y, fps_panel_w, top_h};
    const float step_w = std::min(std::max(inner.w - fps_panel_w - g1, 0.f), kStepPanelMaxW);

    // Adim paneli.
    StringId state_title = StringId::MeasureRunning;
    if (m.phase == cm::TestPhase::Preparing) state_title = StringId::TestPreparing;
    if (m.phase == cm::TestPhase::Stopping) state_title = StringId::MeasureStopping;
    std::wstring stage = m.phase == cm::TestPhase::Stopping ? std::wstring() : measure_stage_text(m);
    if (stage.empty()) stage = t(state_title);
    const std::wstring elapsed = replace_all(t(StringId::MeasureElapsedOpen), L"{e}", widen(cm::format_number(cm::display_elapsed_s(m, now), 0, tr)));
    std::wstring config;
    if (lv.step_index > 0 && lv.width > 0 && lv.height > 0) config = std::to_wstring(lv.width) + L"×" + std::to_wstring(lv.height);
    {
        std::wstring load;
        switch (cm::live_load(lv.step_code)) {
            case cm::LiveLoad::CpuHeavy: load = t(StringId::LiveViewLoadCpu); break;
            case cm::LiveLoad::GpuHeavy: load = t(StringId::LiveViewLoadGpu); break;
            case cm::LiveLoad::Both: load = t(StringId::LiveViewLoadBoth); break;
            case cm::LiveLoad::Unknown: break;
        }
        if (!load.empty()) config += config.empty() ? load : L" · " + load;
    }
    std::wstring phase_text;
    ChipTone phase_tone = ChipTone::Neutral;
    if (m.phase != cm::TestPhase::Stopping) {
        switch (lv.phase) {
            case cm::LivePhase::Prepare: phase_text = t(StringId::LiveViewPhasePrepare); break;
            case cm::LivePhase::Warmup: phase_text = t(StringId::LiveViewPhaseWarmup), phase_tone = ChipTone::Warn; break;
            case cm::LivePhase::Measure: phase_text = t(StringId::LiveViewPhaseMeasure), phase_tone = ChipTone::Good; break;
            case cm::LivePhase::Idle: break;
        }
    }
    if (step_w > 2 * pad) {
        const RectF sp{inner.x, inner.y, step_w, top_h};
        panel(sp);
        const float x = sp.x + pad, w = sp.w - 2 * pad;
        float y = sp.y + pad;
        const float el_w = std::ceil(g.measure(Role::Secondary, elapsed, kUnbounded, false, true).w) + 1.f;
        const float title_w = std::max(w - el_w - g1, 1.f);
        g.text(Role::Section, fit_text(g, Role::Section, stage, title_w), {x, y, title_w, section_h}, p.text_primary, {Align::Leading, false, true});
        TextOpts eo{Align::Trailing, false, true};
        eo.tabular = true;  // saniye degisirken genislik oynamaz
        g.text(Role::Secondary, elapsed, {x + w - el_w, y, el_w, section_h}, p.text_secondary, eo);
        y += section_h + g0;
        const float chip_w = phase_text.empty() ? 0.f : chip_width(g, phase_text);
        const float cfg_w = std::max(w - (chip_w > 0 ? chip_w + g1 : 0.f), 1.f);
        if (!config.empty()) g.text(Role::Secondary, fit_text(g, Role::Secondary, config, cfg_w), {x, y, cfg_w, sec_h}, p.text_secondary, {Align::Leading, false, true});
        if (chip_w > 0 && chip_w <= w) chip(g, RectF{x + w - chip_w, y, chip_w, sec_h}, phase_text, phase_tone);
        y += sec_h + g1;
        // Ilerleme: tamamlanan adim orani (gercek adim; sahte yuzde yok). Adim baslamadiysa dolgu yok.
        const RectF bar{x, y, w, kBarH};
        g.fill_round(bar, kBarH / 2, p.surface_raised);
        g.stroke_round(bar, kBarH / 2, ensure_contrast(p.border, p.background, p.text_secondary, 3.0), 1.f);
        auto frac = cm::live_step_fraction(lv);
        if (!frac) frac = cm::stage_fraction(m);
        if (frac && *frac > 0) g.fill_round({bar.x, bar.y, std::max(kBarH, bar.w * static_cast<float>(*frac)), bar.h}, kBarH / 2, p.accent);
        if (frac) hits.add_progress(Hit::Progress0, bar, t(StringId::MeasureProgressName), *frac * 100.0);
        std::wstring name = a11y_join(t(state_title), stage == t(state_title) ? elapsed : stage + L" · " + elapsed);
        name = a11y_join(std::move(name), config);
        name = a11y_join(std::move(name), phase_text);
        hits.add_text(Hit::LiveStep, sp, std::move(name));
    }

    // Kare hizi paneli.
    {
        panel(fps_panel);
        const float x = fps_panel.x + pad, w = fps_panel.w - 2 * pad;
        float y = fps_panel.y + pad;
        g.text(Role::Label, fps_label, {x, y, w, label_h}, p.text_secondary, {Align::Leading, false, true});
        y += label_h;
        const Color num = frame_ms ? ensure_contrast(p.accent, p.background, p.text_primary, 4.5) : p.text_secondary;
        g.text(Role::Metric, fps_value, {x, y, std::max(w - fps_unit_w - g0, 1.f), metric_h}, num, {Align::Leading, false, true, true});
        if (frame_ms) {
            const float vw = std::ceil(g.measure(Role::Metric, fps_value, kUnbounded, false, true).w);
            const float ux = x + vw + g0;
            g.text(Role::Secondary, L"FPS", {ux, y + metric_h - sec_h - g0, std::max(x + w - ux, 1.f), sec_h}, p.text_secondary, {Align::Leading, false, true});
        }
        y += metric_h;
        TextOpts fo{Align::Leading, false, true};
        fo.tabular = true;
        g.text(Role::Secondary, ft_line, {x, y, w, sec_h}, p.text_secondary, fo);
        const auto name = frame_ms ? replace_all(replace_all(t(StringId::LiveViewAccFps), L"{fps}", fps_value), L"{ms}", widen(cm::format_number(*frame_ms, 2, tr)))
                                   : t(StringId::LiveViewAccFpsNone);
        hits.add_text(Hit::LiveFps, fps_panel, name);
    }

    // ---- Alt satir: yuk cubuklari (sol) | kare suresi grafigi (orta) | durdurma (sag); ayni yukseklik ----
    const auto hint = t(StringId::LiveViewEscHint);
    const auto note = t(StringId::LiveViewScopeNote);
    const bool can_stop = cm::can_stop(m);
    const auto stop_label = can_stop ? t(StringId::TestStop) : t(StringId::MeasureStoppingButton);
    // Dugme genisligi iki etiketin genisine gore (Durduruluyor'a gecince panel oynamaz).
    const float btn_w = std::max(button_rect(g, 0, 0, t(StringId::TestStop)).w, button_rect(g, 0, 0, t(StringId::MeasureStoppingButton)).w);
    const float hint_w = std::ceil(g.measure(Role::Secondary, hint, kUnbounded, false).w) + 1.f;
    const float stop_w = std::min(std::max(btn_w, hint_w) + 2 * pad, inner.w);
    const float row_h = sec_h + g0 + kLedH;                                  // etiket + deger satiri, altinda LED cubuk
    const float bottom_h = pad + std::max(2 * row_h + g1, sec_h + g1 + theme_.button_h) + g0 + sec_h + pad;
    const float by = inner.bottom() - bottom_h;
    if (by < inner.y + top_h + g1) {
        // Alan cok kisa (asgari pencerenin altinda): alt satir sigmiyor. Durdurma dugmesi yine erisilebilir olmali.
        const RectF sr{inner.right() - btn_w, std::max(inner.bottom() - theme_.button_h, inner.y), btn_w, theme_.button_h};
        button_face(g, sr, Hit::TestStop, stop_label, ButtonKind::Secondary, can_stop, s, now, hits);
        hits.add(Hit::TestStop, sr, stop_label, AccRole::Button, false, -1, can_stop);
        hits.add_text(seq.take(), view, t(StringId::LiveViewAccPreview));
        return;
    }
    const RectF stop_panel{inner.right() - stop_w, by, stop_w, bottom_h};
    const float rest = std::max(inner.w - stop_w - g1, 0.f);
    float load_w = std::clamp(std::floor(rest * 0.36f), kLoadPanelMinW, kLoadPanelMaxW);
    float chart_w = rest - load_w - g1;
    if (chart_w < kChartMinW) {  // grafik sigmiyor: yuk paneli kalan alani alir
        chart_w = 0;
        load_w = std::min(rest, kLoadPanelMaxW + kChartMinW);
    }
    load_w = std::min(load_w, rest);

    // Yuk cubuklari: yuzde = kare basi sure / kare araligi (CPU: kare hazirlama; GPU: cizim). Renk kullanim rampasi.
    if (load_w > 2 * pad) {
        const RectF lp{inner.x, by, load_w, bottom_h};
        panel(lp);
        const float x = lp.x + pad, w = lp.w - 2 * pad;
        const Color track = p.surface_raised;
        struct Row {
            std::wstring label;
            std::optional<double> pct;   // olculen deger (UIA / sayi)
            std::optional<float> shown;  // cubukta gosterilen (gecisli) deger
            StringId acc;
        };
        const auto shown = [&](const std::optional<double>& pct, const Tween& tw, bool seen) -> std::optional<float> {
            if (!pct) return std::nullopt;
            return seen ? std::clamp(tw.value(now, theme_.easing), 0.f, 100.f) : static_cast<float>(*pct);
        };
        const auto cpu = cm::live_cpu_load_pct(lv), gpu = cm::live_gpu_load_pct(lv);
        const Row rows[] = {{t(StringId::LiveViewCpuBar), cpu, shown(cpu, s.live_cpu, s.live_cpu_seen), StringId::LiveViewAccLoadCpu},
                            {t(StringId::LiveViewGpuBar), gpu, shown(gpu, s.live_gpu, s.live_gpu_seen), StringId::LiveViewAccLoadGpu}};
        const float block_h = 2 * row_h + g1;
        float y = lp.y + pad + std::floor((bottom_h - 2 * pad - block_h) / 2);  // iki satir panelde dikey ortali
        std::wstring name;
        for (const auto& r : rows) {
            const std::wstring value = r.pct ? pct_text(*r.pct, tr) : std::wstring(L"—");
            const float vw = std::ceil(g.measure(Role::Secondary, tr ? L"%100" : L"100%", kUnbounded, false, true).w) + 1.f;
            const float lw = std::max(w - vw - g1, 1.f);
            g.text(Role::Secondary, fit_text(g, Role::Secondary, r.label, lw), {x, y, lw, sec_h}, p.text_primary, {Align::Leading, false, true});
            TextOpts vo{Align::Trailing, false, true};
            vo.tabular = true;
            const Color vc = !r.pct ? p.text_secondary : dark ? ensure_contrast(usage_color(p, *r.pct), p.background, p.text_primary, 4.5) : p.text_primary;
            g.text(Role::Secondary, value, {x + w - vw, y, vw, sec_h}, vc, vo);
            const float ly = y + sec_h + g0;
            const float seg_w = std::max((w - kLedGap * static_cast<float>(kLedSegments - 1)) / static_cast<float>(kLedSegments), 1.f);
            for (int i = 0; i < kLedSegments; ++i) {
                const RectF seg{x + static_cast<float>(i) * (seg_w + kLedGap), ly, seg_w, kLedH};
                const bool lit = r.shown && led_segment_lit(i, static_cast<double>(*r.shown));
                if (dark) g.fill_round(seg, kLedRadius, lit ? usage_color(p, led_segment_pct(i)) : track);
                else if (lit) g.fill_round(seg, kLedRadius, p.accent);
                else g.stroke_round(seg.inset(0.5f), kLedRadius, p.border);
            }
            y += row_h + g1;
            const auto part = replace_all(t(r.acc), L"{v}", r.pct ? value : t(StringId::MetricMissing));
            name += name.empty() ? part : L", " + part;
        }
        hits.add_text(Hit::LiveLoad, lp, std::move(name));
    }

    // Kare suresi grafigi: halkadaki her kova (~0,25 sn) bir cubuk; en kotu kova uyari renginde. Altinda kapsam notu.
    if (chart_w > 2 * pad) {
        const RectF cp{inner.x + load_w + g1, by, chart_w, bottom_h};
        panel(cp);
        const float x = cp.x + pad, w = cp.w - 2 * pad;
        float y = cp.y + pad;
        const auto title = t(StringId::LiveViewChart);
        const auto bars = cm::live_frame_bars(lv, kChartMaxBars);
        std::wstring range;
        if (!bars.empty()) {
            const auto [lo, hi] = std::minmax_element(bars.begin(), bars.end());
            range = replace_all(replace_all(t(StringId::MeasureFrameTimeRange), L"{min}", widen(cm::format_number(*lo, 2, tr))), L"{max}",
                                widen(cm::format_number(*hi, 2, tr)));
        }
        const float range_w = range.empty() ? 0.f : std::ceil(g.measure(Role::Secondary, range, kUnbounded, false, true).w) + 1.f;
        const float title_w = std::ceil(g.measure(Role::Secondary, title, kUnbounded, false).w) + 1.f;
        const bool show_range = range_w > 0 && title_w + g1 + range_w <= w;
        g.text(Role::Secondary, fit_text(g, Role::Secondary, title, show_range ? w - range_w - g1 : w), {x, y, show_range ? w - range_w - g1 : w, sec_h},
               p.text_primary, {Align::Leading, false, true});
        if (show_range) {
            TextOpts ro{Align::Trailing, false, true};
            ro.tabular = true;
            g.text(Role::Secondary, range, {x + w - range_w, y, range_w, sec_h}, p.text_secondary, ro);
        }
        y += sec_h + g0;
        const RectF plot{x, y, w, std::max(cp.bottom() - pad - sec_h - g0 - y, 1.f)};
        if (!bars.empty()) {
            // Taban cizgisi + cubuklar; olcek en yavas kovaya gore (cubuk yuksekligi = ms / en buyuk ms).
            g.fill({plot.x, plot.bottom() - 1.f, plot.w, 1.f}, dark ? p.border.with_alpha(alpha_scale::kHudBorder) : p.border);
            const double top_ms = std::max(*std::max_element(bars.begin(), bars.end()), 1e-6);
            const double hi = top_ms, lo = *std::min_element(bars.begin(), bars.end());
            const float pitch = plot.w / static_cast<float>(kChartMaxBars);  // sabit adim: cubuklar soldan dolar, genislik oynamaz
            bool worst_marked = false;
            for (std::size_t i = 0; i < bars.size(); ++i) {
                const float bh = std::max(2.f, static_cast<float>(bars[i] / top_ms) * (plot.h - 1.f));
                const bool worst = !worst_marked && bars[i] >= hi && hi > lo * kChartSpikeRatio;
                worst_marked = worst_marked || worst;
                const float x0 = plot.x + std::round(static_cast<float>(i) * pitch);
                const float x1 = std::min(plot.x + std::round(static_cast<float>(i + 1) * pitch) - kChartBarGap, plot.right());
                g.fill({x0, plot.bottom() - 1.f - bh, std::max(x1 - x0, 1.f), bh}, worst ? p.warning : p.accent);
            }
        } else {
            g.text(Role::Secondary, t(StringId::MeasureFrameTimeNone), plot, p.text_secondary, {Align::Center, false, true});
        }
        g.text(Role::Secondary, fit_text(g, Role::Secondary, note, w), {x, cp.bottom() - pad - sec_h, w, sec_h}, p.text_secondary, {Align::Leading, false, true});
        std::wstring name = a11y_join(title, bars.empty() ? t(StringId::MeasureFrameTimeNone) : range);
        hits.add_text(seq.take(), cp, a11y_join(std::move(name), note));
    }

    // Durdurma paneli: ipucu ustte, dugme altta (mevcut durdurma yolu; Durduruluyor'da devre disi).
    {
        panel(stop_panel);
        const float x = stop_panel.x + pad, w = stop_panel.w - 2 * pad;
        // Ipucu yalniz Esc gercekten durdurabiliyorken: Durduruluyor'da Esc bir sey yapmaz, metin de UIA yardimi da yok
        // (panel genisligi ipucuna gore sabit kalir: duruma gecince yerlesim oynamaz).
        if (can_stop) g.text(Role::Secondary, hint, {x, stop_panel.y + pad, w, sec_h}, p.text_secondary, {Align::Leading, false, true});
        const RectF br{x, stop_panel.bottom() - pad - theme_.button_h, std::min(btn_w, w), theme_.button_h};
        button_face(g, br, Hit::TestStop, stop_label, ButtonKind::Secondary, can_stop, s, now, hits);
        hits.add(Hit::TestStop, br, stop_label, AccRole::Button, false, -1, can_stop);
        if (const auto it = hits.meta.find(Hit::TestStop); it != hits.meta.end() && can_stop) it->second.help = hint;
        // Grafik paneli yoksa kapsam notu burada okunur metin olarak kalir (gorunen metin grafik panelindedir).
        if (chart_w <= 2 * pad) hits.add_text(seq.take(), {x, stop_panel.y + pad, w, sec_h}, can_stop ? a11y_join(hint, note) : note);
    }
    // Onizleme ogesi EN SONDA: isabet sirasinda paneller once gelir (HitMap::element_at ilk eslesmeyi dondurur).
    hits.add_text(seq.take(), view, t(StringId::LiveViewAccPreview));
}

}  // namespace ef::ui
