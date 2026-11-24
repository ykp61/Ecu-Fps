// Olcumler "Darbogaz" bolumu (spec 2026-11-12 C / D; denge_v1 kural esikleri degismez). Baslik satiri ("bu test
// sahnesinde" bir kez + renk aciklamasi: islemci kehribar, ekran karti camgobegi) ve yan yana uc es kutu (1080p, 1440p,
// 4K; dar kartta alt alta). Kutu: cozunurluk, D hukum kelimesi ("Islemci darbogazi" turuncu / "Darbogaz yok" yesil /
// Belirlenemedi, Olculemedi), olculen pay ya da neden (tek satir), iki ince cubuk (islemci FPS, ekran karti FPS;
// sayilar sagda; alt sinirsa ">="). Birincil ekranin cozunurlugune denk gelen kutu camgobegi kenarlik + "senin ekranin"
// alir; eslesme yoksa vurgu yok. Kurallar:
//  - Yalniz olculen deger cizilir: eksik deger icin cubuk dolgusu yok, "Olculemedi" yazilir (0 gosterilmez).
//  - Uc kutu ayni boydadir; cubuk olcegi karttaki en yuksek FPS'tir; cubuklar acilista dolar (motion.bars_fill).
//  - Hukum yalniz renkle verilmez (her zaman kelime); kaldirilan notlar (kapsam, bagdastirici, PCIe, ipuclari) UIA'dadir.
//  - Kisa ozet (balance_summary_text: "1080p: islemci · 1440p: yok · 4K: yok") Panel ve gecmis satirlarinda kullanilir.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "core/measure/balance_view.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cm = core::measure;
namespace dg = core::diagnosis;

namespace {

std::wstring widen_b(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring put(std::wstring s, std::wstring_view key, const std::wstring& val) {
    for (std::size_t pos = s.find(key); pos != std::wstring::npos; pos = s.find(key, pos + val.size())) s.replace(pos, key.size(), val);
    return s;
}

}  // namespace

std::wstring Screen::balance_summary_text(const core::measure::BalanceView& v) const {
    std::wstring out;
    for (std::size_t i = 0; i < v.rows.size(); ++i) {
        auto part = put(put(tk("balance.summary.row"), L"{res}", tk(cm::balance_row_key(i))), L"{verdict}", tk(cm::balance_short_key(v.rows[i].verdict)));
        out += (out.empty() ? L"" : L" · ") + part;
    }
    return out + L" — " + tk("balance.summary.suffix");
}

std::vector<std::wstring> Screen::balance_verdict_lines(const core::measure::BalanceView& v) const {
    std::vector<std::wstring> out;
    for (std::size_t i = 0; i < v.rows.size(); ++i)
        out.push_back(put(put(tk("balance.summary.row"), L"{res}", tk(cm::balance_row_key(i))), L"{verdict}", tk(cm::balance_verdict_key(v.rows[i].verdict))));
    return out;
}

void Screen::balance_card(Graphics& g, CardSlot& slot, const core::measure::BalanceView& v, UiState& s, const ViewModel& vm, std::int64_t now,
                          HitMap& hits, StatusSeq& seq) {
    const auto& p = pal();
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const bool dark = pal_ == &theme_.dark;
    constexpr float kIn = 15.f;  // BOOST maketi: card-v2 p-3.5 (14) + 1 DIP cizgi
    const float pad = kIn;
    const float inner_w = std::max(slot.area.w - 2 * pad, 1.f);
    const float section_h = theme_.role(Role::Title14).line;
    const float mono_line = theme_.role(Role::Mono12Semi).line;
    const float sl = theme_.role(Role::Secondary).line;
    const float g0 = theme_.space(0), g1 = theme_.space(1), g2 = theme_.space(2);
    constexpr float kUnbounded = 10000.f;
    constexpr float kHeadGap = 10.f;  // maket mb-2.5
    constexpr float kBarH = 5.f, kDot = 8.f;  // bilesen geometrisi (token degil): ince cubuk, renk aciklamasi noktasi
    const auto one_w = [&](Role role, const std::wstring& t0, bool tabular = false) {
        return std::ceil(g.measure(role, t0, kUnbounded, false, tabular).w) + 1.f;
    };

    // ---- Metinler ----
    const auto title = t(StringId::PanelBottleneckTitle);
    const auto scope = t(StringId::PanelBottleneckScope);
    const auto cpu_legend = t(StringId::MeasureBottleneckCpuLegend), gpu_legend = t(StringId::MeasureBottleneckGpuLegend);
    std::wstring global;
    if (!v.global_reasons.empty()) {
        std::wstring why;
        for (const auto& code : v.global_reasons) why += (why.empty() ? L"" : L"; ") + tk(cm::balance_reason_key(code));
        global = put(tk("balance.card.global"), L"{reason}", why);
        // denge_v4: CPU adiminin asil nedeni (yalniz ilgili genel neden varsa; eski kayitlarda bos).
        const auto has_global = [&](std::string_view g) { return std::find(v.global_reasons.begin(), v.global_reasons.end(), g) != v.global_reasons.end(); };
        if (!v.cpu_step_reason.empty() && has_global("cpu_step_invalid"))
            global += L"\n" + put(tk("balance.card.cpu_step_detail"), L"{reason}", tk(cm::balance_reason_key(v.cpu_step_reason)));
        else if (!v.cpu_repeat_reason.empty() && has_global("cpu_repeat_invalid"))
            global += L"\n" + put(tk("balance.card.cpu_step_detail"), L"{reason}", tk(cm::balance_reason_key(v.cpu_repeat_reason)));
        // Kullanicinin duzeltebilecegi ortam nedeni (arka plan yuku): hata degil, ne yapacagi yazilir.
        if (has_global("background_cpu") || has_global("background_gpu")) global += L"\n" + tk("balance.card.calm_hint");
    }
    const float global_h = global.empty() ? 0.f : g.measure(Role::Secondary, global, inner_w).h;
    // Ekrandan kaldirilan notlar (A): kapsam notu, bagdastirici, CPU paylari, olcum notlari, PCIe, "Genel bilgi" ipuclari
    // UIA metninde kalir.
    std::wstring foot = tk("balance.card.scope_note");
    foot = a11y_join(std::move(foot), v.adapter.empty() ? tk("balance.card.adapter_unknown") : put(tk("balance.card.adapter"), L"{name}", widen_b(v.adapter)));
    if (v.cpu_sim_share && v.cpu_draw_share)
        foot = a11y_join(std::move(foot), put(put(tk("balance.card.cpu_split"), L"{sim}", widen_b(cm::format_number(*v.cpu_sim_share * 100.0, 0, tr))), L"{draw}",
                                              widen_b(cm::format_number(*v.cpu_draw_share * 100.0, 0, tr))));
    for (const auto& code : v.notes)
        if (const auto key = cm::balance_note_key(code); !key.empty()) foot = a11y_join(std::move(foot), tk(key));
    if (!v.pcie_link.empty()) foot = a11y_join(std::move(foot), put(tk("balance.card.pcie_load"), L"{link}", widen_b(v.pcie_link)));
    if (v.has(dg::BalanceVerdict::CpuLimited) || v.has(dg::BalanceVerdict::GpuLimited)) {
        foot = a11y_join(std::move(foot), tk("balance.card.tip_label"));
        if (v.has(dg::BalanceVerdict::CpuLimited)) foot = a11y_join(std::move(foot), tk("balance.card.tip_cpu"));
        if (v.has(dg::BalanceVerdict::GpuLimited)) foot = a11y_join(std::move(foot), tk("balance.card.tip_gpu"));
    }

    struct Value {
        std::wstring text, acc;  // gorunen / ekran okuyucu ("en az 212 FPS")
        std::optional<double> fps;
    };
    const auto value_of = [&](const std::optional<double>& fps, bool lower_bound) {
        Value out;
        out.fps = fps;
        if (!fps) {
            out.text = out.acc = tk("balance.card.not_measured");
            return out;
        }
        const auto num = widen_b(cm::format_number(*fps, 0, tr));
        out.text = put(tk(lower_bound ? "balance.card.fps_at_least" : "balance.card.fps"), L"{v}", num);
        out.acc = put(tk(lower_bound ? "balance.card.fps_at_least_acc" : "balance.card.fps"), L"{v}", num);
        return out;
    };
    const auto cpu_name = tk("balance.card.cpu_bar"), gpu_name = tk("balance.card.gpu_bar");
    const Value cpu_value = value_of(v.cpu_fps, v.cpu_lower_bound);
    const auto at_least = t(StringId::PanelBottleneckAtLeast);
    const auto mine = balance_display_row(vm.display);
    const auto your = t(StringId::MeasureBottleneckYourDisplay);
    struct Tile {
        std::wstring res, word, note;
        Value gpu;
        Color c;
        bool mine = false;
    };
    std::array<Tile, dg::kBalanceRows> tiles;
    double max_fps = v.cpu_fps.value_or(0.0);
    float val_w = one_w(Role::Secondary, cpu_value.text, true);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        Tile& tl = tiles[i];
        const auto& br = v.rows[i];
        tl.res = tk(cm::balance_row_key(i));
        tl.gpu = value_of(br.gpu_fps, br.gpu_lower_bound);
        tl.mine = mine && *mine == i;
        tl.c = p.text_secondary;
        // D: hukum kelimesi + olculen pay (hesaplanamiyorsa ikinci satir yok; negatif pay yazilmaz).
        const auto share = balance_share(v, i);
        switch (bottleneck_word(br.verdict)) {
            case BottleneckWord::Cpu:
                tl.word = t(StringId::PanelBottleneckCpu);
                tl.c = p.orange;
                if (share) tl.note = put(t(StringId::PanelBottleneckCpuShare), L"{share}", share_text(*share, tr, true, at_least));
                break;
            case BottleneckWord::None:
                tl.word = t(StringId::PanelBottleneckNone);
                tl.c = p.success;
                if (br.verdict == dg::BalanceVerdict::Balanced) tl.note = t(StringId::PanelBottleneckBalanced);
                else if (share) tl.note = put(t(StringId::PanelBottleneckGpuShare), L"{share}", share_text(*share, tr, false, at_least));
                break;
            case BottleneckWord::Other:
                tl.word = tk(cm::balance_verdict_key(br.verdict));
                if (!br.reason.empty()) tl.note = tk(cm::balance_reason_key(br.reason));
                break;
        }
        tl.c = ensure_contrast(tl.c, p.surface, p.text_primary, 4.5);
        max_fps = std::max(max_fps, br.gpu_fps.value_or(0.0));
        val_w = std::max(val_w, one_w(Role::Secondary, tl.gpu.text, true));
    }

    // ---- Yerlesim: baslik satiri + (genel neden) + yan yana uc es kutu (dar kartta alt alta) ----
    const int cols = bottleneck_tile_columns(inner_w, g2);
    const float tile_w = cols == 3 ? std::floor((inner_w - 2 * g2) / 3) : inner_w;
    constexpr float kTileIn = 13.f;  // maket well p-3 (12) + cizgi
    // Sahip duzeni (C): tek sutunda yeterince genisse kutu yatay kompakt satirdir: [cozunurluk] [iki cubuk] [hukum + pay].
    const bool compact = cols == 1 && tile_w >= 340.f;
    const float tpad = compact ? 9.f : kTileIn;
    const float tin_w = std::max(tile_w - 2 * tpad, 1.f);
    const float bar_line = std::max(sl, kBarH);
    const float tile_h = compact ? 2 * tpad + bar_line + g0 + bar_line : tpad + mono_line + g0 + sl + g1 + bar_line + g0 + bar_line + tpad;
    const float tiles_h = cols == 3 ? tile_h : 3 * tile_h + 2 * g1;
    // P3: "Bu PC'de olculemedi" iken "Raporu kaydet" (destek paketi yolu; Masaustune yazar, gonderim yok): aciklama satiri,
    // dugme, kayit durumu (hazirlaniyor / kaydedildi + yol / kaydedilemedi). Yalniz olculemeyen kartta cizilir.
    const bool report = cm::balance_view_unmeasured(v);
    const auto& rep = vm.balance_report;
    s.balance_report_busy = report && rep.busy;
    const std::wstring rep_hint = report ? t(StringId::BalanceReportHint) : std::wstring();
    const std::wstring rep_label = report ? t(StringId::BalanceReportSave) : std::wstring();
    std::wstring rep_status;
    if (report) {
        if (rep.busy) rep_status = t(StringId::BalanceReportBusy);
        else if (rep.done) rep_status = put(t(StringId::BalanceReportSaved), L"{path}", rep.path);
        else if (rep.failed) rep_status = t(StringId::BalanceReportFailed);
    }
    const float rep_hint_h = report ? g.measure(Role::Secondary, rep_hint, inner_w).h : 0.f;
    const float rep_status_h = rep_status.empty() ? 0.f : g.measure(Role::Secondary, rep_status, inner_w).h;
    const float report_h = report ? g2 + rep_hint_h + g1 + theme_.button_h + (rep_status_h > 0 ? g1 + rep_status_h : 0.f) : 0.f;
    const float h = pad + section_h + (global_h > 0 ? g1 + global_h : 0.f) + kHeadGap + tiles_h + report_h + pad;
    RectF card;
    if (!slot.place(h, card)) return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + pad;
    float y = card.y + pad;
    // Baslik + kapsam ("bu test sahnesinde" bir kez) solda; renk aciklamasi (islemci kehribar, ekran karti camgobegi) sagda.
    const std::wstring code = L"M4 //";
    const float code_w = one_w(Role::Mono10, code);
    const float title_w = one_w(Role::Title14, title);
    const float cpu_lw = one_w(Role::Small11, cpu_legend), gpu_lw = one_w(Role::Small11, gpu_legend);
    const float legend_w = kDot + g0 + cpu_lw + g2 + kDot + g0 + gpu_lw;
    const bool legend = code_w + g1 + title_w + g1 + legend_w <= inner_w;
    g.text(Role::Mono10, code, {x, y, code_w, section_h}, dark ? p.cyan : p.text_primary, {Align::Leading, false, true});
    g.text(Role::Title14, fit_text(g, Role::Title14, title, std::max(inner_w - code_w - g1, 1.f)), {x + code_w + g1, y, std::min(title_w, std::max(inner_w - code_w - g1, 1.f)), section_h},
           p.text_primary, {Align::Leading, false, true});
    const float scope_x = x + code_w + g1 + title_w + g1;
    const float scope_w = x + inner_w - (legend ? legend_w + g2 : 0.f) - scope_x;
    if (scope_w >= 48.f)
        g.text(Role::Small12, fit_text(g, Role::Small12, scope, scope_w), {scope_x, y, scope_w, section_h}, p.text_secondary, {Align::Leading, false, true});
    if (legend) {
        float lx = x + inner_w - legend_w;
        const float dy = y + (section_h - kDot) / 2;
        g.fill_round({lx, dy, kDot, kDot}, kDot / 2, p.data_cpu);
        lx += kDot + g0;
        g.text(Role::Small11, cpu_legend, {lx, y, cpu_lw, section_h}, p.text_secondary, {Align::Leading, false, true});
        lx += cpu_lw + g2;
        g.fill_round({lx, dy, kDot, kDot}, kDot / 2, p.data_gpu);
        lx += kDot + g0;
        g.text(Role::Small11, gpu_legend, {lx, y, gpu_lw, section_h}, p.text_secondary, {Align::Leading, false, true});
    }
    const float head_top = y;
    y += section_h;
    if (global_h > 0) {
        y += g1;
        g.text(Role::Secondary, global, {x, y, inner_w, global_h}, p.warning);
        y += global_h;
    }
    hits.add_text(seq.take(), {x, head_top, inner_w, y - head_top},
                  a11y_join(a11y_join(a11y_join(title + L" " + scope, cpu_legend + L", " + gpu_legend), global), foot));
    y += kHeadGap;

    // Cubuklar acilista dolar (motion.bars_fill; hareket kapali / oyun / olcum surerken aninda).
    if (!s.measure_bars_seen) {
        const int ms = effective_ms(theme_.motion.bars_fill, s.reduced_motion || s.motion_paused || vm.game_gate_blocked);
        s.measure_bars = Tween(ms > 0 ? 0.f : 1.f);
        s.measure_bars.retarget(1.f, now, ms);
        s.measure_bars_seen = true;
    }
    const float grow = std::clamp(s.measure_bars.value(now, theme_.easing), 0.f, 1.f);
    const auto bar_row = [&](float bx, float by, float bw, const Value& val, const Color& fill) {
        TextOpts vo{Align::Trailing, false, true};
        vo.tabular = true;
        const float track_w = std::max(bw - val_w - g1, 1.f);
        const RectF track{bx, by + (bar_line - kBarH) / 2, track_w, kBarH};
        g.fill_round(track, kBarH / 2, p.surface_raised);
        if (!dark) g.stroke_round(track, kBarH / 2, p.border);  // yuksek kontrast: iz cerceveyle belli
        if (val.fps && max_fps > 0) {
            const float f = static_cast<float>(std::clamp(*val.fps / max_fps, 0.0, 1.0)) * grow;
            if (f > 0) g.fill_round({track.x, track.y, std::max(kBarH, track.w * f), track.h}, kBarH / 2, fill);
        }
        g.text(Role::Secondary, val.text, {bx + bw - val_w, by, val_w, bar_line}, val.fps ? p.text_primary : p.text_secondary, vo);
    };
    const Color your_c = dark ? p.cyan : p.focus_ring;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const Tile& tl = tiles[i];
        const float tx = cols == 3 ? x + static_cast<float>(i) * (tile_w + g2) : x;
        const float ty = cols == 3 ? y : y + static_cast<float>(i) * (tile_h + g1);
        const RectF tile{tx, ty, tile_w, tile_h};
        if (dark) {
            // .well-v2 (+ "senin ekranin": cyan %4 zemin, cyan %50 cizgi).
            draw_well(g, tile);
            if (tl.mine) {
                g.fill_round(tile, depth::kWellRadius, p.cyan.with_alpha(0.04f));
                g.stroke_round(tile.inset(0.5f), depth::kWellRadius, p.cyan.with_alpha(0.50f));
            }
        } else {
            g.fill_round(tile, theme_.radius_control, p.surface_raised);
            g.stroke_round(tile, theme_.radius_control, tl.mine ? your_c : mix(p.surface, p.border, alpha_scale::kDivider), tl.mine ? 2.f : 1.f);
        }
        const float ix = tx + tpad;
        float iy = ty + tpad;
        if (compact) {
            // Mockup (C): cozunurluk solda; ortada iki genis cubuk (deger yazisi yok); sagda hukum + olculen FPS cifti. Pay / neden
            // metni gorunmez ama UIA satirinda durur.
            const std::wstring pair = cpu_value.text + L" · " + tl.gpu.text;
            const float res_w = std::min(std::max(one_w(Role::Mono12Semi, tl.res), one_w(Role::Small11, your)) + g1, tin_w * 0.22f);
            const float ver_w = std::min(std::max(one_w(Role::Semi12, tl.word), one_w(Role::Small11, pair)) + g2 + g0, tin_w * 0.42f);
            const float mid_x = ix + res_w + g2, mid_w = std::max(tin_w - res_w - ver_w - 2 * g2, 1.f);
            g.text(Role::Mono12Semi, fit_text(g, Role::Mono12Semi, tl.res, res_w), {ix, iy, res_w, mono_line}, p.text_primary, {Align::Leading, false, true});
            if (tl.mine) g.text(Role::Small11, fit_text(g, Role::Small11, your, res_w), {ix, iy + mono_line, res_w, sl}, dark ? p.cyan : p.focus_ring, {Align::Leading, false, true});
            const auto bar_only = [&](float by, const Value& val, const Color& fill) {
                const RectF track{mid_x, by + (bar_line - kBarH) / 2, mid_w, kBarH};
                g.fill_round(track, kBarH / 2, p.surface_raised);
                if (!dark) g.stroke_round(track, kBarH / 2, p.border);
                if (val.fps && max_fps > 0) {
                    const float f = static_cast<float>(std::clamp(*val.fps / max_fps, 0.0, 1.0)) * grow;
                    if (f > 0) g.fill_round({track.x, track.y, std::max(kBarH, track.w * f), track.h}, kBarH / 2, fill);
                }
            };
            bar_only(iy, cpu_value, p.data_cpu);
            bar_only(iy + bar_line + g0, tl.gpu, p.data_gpu);
            g.text(Role::Semi12, fit_text(g, Role::Semi12, tl.word, ver_w), {ix + tin_w - ver_w, iy, ver_w, mono_line}, tl.c, {Align::Trailing, false, true});
            TextOpts po{Align::Trailing, false, true};
            po.tabular = true;
            g.text(Role::Small11, fit_text(g, Role::Small11, pair, ver_w), {ix + tin_w - ver_w, iy + mono_line, ver_w, sl}, p.text_secondary, po);
        } else {
            // Cozunurluk (mono 12px/600; birincil ekrana denk gelen kutuda "· senin ekranin") solda, hukum kelimesi sagda.
            const std::wstring res_txt = tl.mine ? tl.res + L" · " + your : tl.res;
            const float word_w = std::min(one_w(Role::Semi12, tl.word), tin_w * 0.6f);
            g.text(Role::Mono12Semi, fit_text(g, Role::Mono12Semi, res_txt, std::max(tin_w - word_w - g1, 1.f)), {ix, iy, std::max(tin_w - word_w - g1, 1.f), mono_line},
                   p.text_primary, {Align::Leading, false, true});
            g.text(Role::Semi12, fit_text(g, Role::Semi12, tl.word, word_w), {ix + tin_w - word_w, iy, word_w, mono_line}, tl.c, {Align::Trailing, false, true});
            iy += mono_line + g0;
            if (!tl.note.empty())
                g.text(Role::Small11, fit_text(g, Role::Small11, tl.note, tin_w), {ix, iy, tin_w, sl}, p.text_secondary, {Align::Leading, false, true});
            iy += sl + g1;
            bar_row(ix, iy, tin_w, cpu_value, p.data_cpu);
            iy += bar_line + g0;
            bar_row(ix, iy, tin_w, tl.gpu, p.data_gpu);
        }
        // UIA: kutunun tamami tek salt okunur oge; hukum "bu test sahnesinde" kapsamiyla, pay / neden kisaltilmadan.
        std::wstring uia = put(put(tk("balance.card.row_acc"), L"{res}", tl.res), L"{verdict}", tl.word);
        uia = a11y_join(std::move(uia), tl.note);
        uia = a11y_join(std::move(uia), cpu_name + L" " + cpu_value.acc);
        uia = a11y_join(std::move(uia), gpu_name + L" " + tl.gpu.acc);
        if (tl.mine) uia = a11y_join(std::move(uia), your);
        hits.add_text(seq.take(), tile, std::move(uia));
    }
    if (report) {
        float ry = y + tiles_h + g2;
        g.text(Role::Secondary, rep_hint, {x, ry, inner_w, rep_hint_h}, p.text_secondary);
        hits.add_text(seq.take(), {x, ry, inner_w, rep_hint_h}, rep_hint);
        ry += rep_hint_h + g1;
        button(g, x, ry, Hit::BalanceReport, rep_label, false, s, now, hits, !rep.busy);
        ry += theme_.button_h;
        if (rep_status_h > 0) {
            ry += g1;
            g.text(Role::Secondary, rep_status, {x, ry, inner_w, rep_status_h}, rep.failed ? p.warning : p.text_secondary);
            hits.add_text(seq.take(), {x, ry, inner_w, rep_status_h}, rep_status);
        }
    }
}

}  // namespace ef::ui
