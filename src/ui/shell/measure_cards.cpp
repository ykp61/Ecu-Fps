// P9-B5: Olcumler sayfasi — uzun test, deney (P1), bildirim seritleri, bilesen ve karsilastirma kartlari. measure_page.cpp'den ayrildi.
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

// R13/INS-03: uzun test (isinma). Uzun sureli sabit sahne; butce/iptal her ornekte; saglik raporu degil.
// B10: "Test ayarlari" kalktigi icin kart her zaman gorunur (baslatma dugmesi ya da kullanilamama nedeni).
void Screen::measure_endurance_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    UiState& s = mc.s;
    const std::int64_t now = mc.now;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float gap = mc.gap;
    const float section_h = mc.section_h;
    const bool tr = mc.tr;
    const bool busy = mc.busy;
    const float inner_w = slot.area.w - 2 * pad;
    const auto desc = t(StringId::MeasureEnduranceDesc);
    const float desc_h = g.measure(Role::Secondary, desc, inner_w).h;
    const auto& e = m.endurance;
    std::vector<std::wstring> lines;
    std::vector<std::pair<std::wstring, Color>> notes;
    bool show_start = false, show_stop = false;
    if (e.running) {
        show_stop = true;
        // B14: cubugun altinda asama, gecen sure ve (ilk ornekten sonra) anlik satir. Hepsi kosucu saatinden.
        const double elapsed = cm::endurance_elapsed_s(m, now);
        const bool started = e.started_ms > 0 || e.elapsed_s > 0;
        lines.push_back(tk(cm::endurance_phase_key(cm::endurance_phase(started, elapsed, e.budget_s))));
        const auto el = widen(cm::format_number(elapsed, 0, tr));
        const auto budget = widen(cm::format_number(e.budget_s, 0, tr));
        lines.push_back(replace_all(replace_all(t(StringId::MeasureEnduranceElapsed), L"{e}", el), L"{t}", budget));
        if (e.has_live) {
            const auto unreadable = t(StringId::MeasureEnduranceUnreadable);
            const auto num = [&](const std::optional<double>& v, int dec) { return v && std::isfinite(*v) ? widen(cm::format_number(*v, dec, tr)) : std::wstring(); };
            auto fps = num(e.live_fps, 0), clock = num(e.live_gpu_mhz, 0), load = num(e.live_gpu_load_pct, 0);
            if (fps.empty()) fps = unreadable;
            if (e.telemetry_unsupported) {
                // Desteklenmeyen sensor "okunamiyor" degildir: saat/yuk yerine "desteklenmiyor (yalniz NVIDIA)".
                lines.push_back(replace_all(t(StringId::MeasureEnduranceLiveUnsupported), L"{fps}", fps));
            } else {
                clock = clock.empty() ? unreadable : clock + L" MHz";
                load = load.empty() ? unreadable : replace_all(t(StringId::MeasureEnduranceLoadValue), L"{v}", load);
                lines.push_back(replace_all(replace_all(replace_all(t(StringId::MeasureEnduranceLive), L"{fps}", fps), L"{clock}", clock), L"{load}", load));
            }
        }
    } else if (e.failed) {
        notes.push_back({t(StringId::MeasureEnduranceFailed), p.warning});
    } else if (e.has_result) {
        for (const auto& row : cm::endurance_rows(e.result)) {
            std::wstring val = row.value ? widen(cm::format_number(*row.value, row.decimals, tr)) + (row.unit.empty() ? L"" : L" " + widen(row.unit))
                                         : t(StringId::MetricMissing);
            lines.push_back(tk(row.label_key) + L": " + val);
        }
        const auto stop_key = cm::endurance_stop_key(e.result.stop);
        const auto dur = widen(cm::format_number(e.result.duration_s, 0, tr));
        for (const auto& key : cm::endurance_note_keys(e.result)) {
            const auto text = replace_all(tk(key), L"{e}", dur);  // B14: kismi sonuc gercek sureyi yazar
            if (text.empty()) continue;
            const std::string_view k(key);
            const bool warn = (!stop_key.empty() && k == stop_key) || k == "measure.endurance.insufficient" ||
                              k == "measure.endurance.partial" || k == "measure.endurance.too_short" || k == "measure.endurance.not_stable";
            notes.push_back({text, warn ? p.warning : k == "measure.endurance.stable" ? p.success : p.text_secondary});
        }
        if (cm::endurance_can_start(m)) show_start = true;  // yeniden yalniz kullanici baslatir
    }
    if (!e.running && !e.failed && !e.has_result) {
        if (!cm::endurance_available(m)) {
            auto why = m.gate_reason_key.empty() ? std::wstring() : tk(m.gate_reason_key);
            if (why.empty()) why = t(StringId::MeasureEnduranceUnavailable);
            notes.push_back({why, p.text_secondary});
        } else if (busy) {
            notes.push_back({t(StringId::MeasureEnduranceWait), p.text_secondary});
        } else {
            show_start = cm::endurance_can_start(m);
        }
    }
    constexpr float kBarH = 8.f;
    // Calisirken satirlar (asama, sure, anlik) cubugun altinda Secondary rolde; sarilirsa olculen yukseklik kullanilir.
    float run_lines_h = 0;
    if (e.running)
        for (const auto& l : lines) run_lines_h += std::max(theme_.role(Role::Secondary).line, g.measure(Role::Secondary, l, inner_w).h);
    const float bar_block = e.running ? kBarH + theme_.space(1) + run_lines_h : 0.f;
    float lines_h = 0;
    if (!e.running)
        for (const auto& l : lines) lines_h += g.measure(Role::Body, l, inner_w).h + theme_.space(0);
    float notes_h = 0;
    for (const auto& n : notes) notes_h += g.measure(Role::Secondary, n.first, inner_w).h + theme_.space(0);
    const bool has_button = show_start || show_stop;
    const float card_h = pad + section_h + gap + desc_h +
                         (e.running ? gap + bar_block : 0.f) +
                         (lines.empty() || e.running ? 0.f : theme_.space(1) + lines_h) +
                         (notes.empty() ? 0.f : theme_.space(1) + notes_h) +
                         (has_button ? gap + theme_.button_h : 0.f) + pad;
    RectF card;
    if (!slot.place(card_h, card)) return;
    g.fill_round(card, theme_.radius_card, p.surface);
    g.stroke_round(card, theme_.radius_card, e.running ? p.accent.with_alpha(alpha_scale::kOutline) : p.border,
                   e.running ? 2.f : 1.f);
    log_card(card);  // ozel kenarli kart: simetri denetimi icin cerceve kaydi
    float y = card.y + pad;
    g.text(Role::Section, t(StringId::MeasureEnduranceTitle), {card.x + pad, y, inner_w, section_h}, p.text_primary);
    y += section_h + gap;
    g.text(Role::Secondary, desc, {card.x + pad, y, inner_w, desc_h}, p.text_secondary);
    y += desc_h;
    float text_top = y;  // UIA: asama/sure/anlik satirlari, sonuc satirlari ve notlar tek salt okunur ogede
    if (e.running) {
        y += gap;
        const RectF bar{card.x + pad, y, inner_w, kBarH};
        g.fill_round(bar, kBarH / 2, p.surface_raised);
        g.stroke_round(bar, kBarH / 2, ensure_contrast(p.border, p.surface, p.text_secondary, 3.0), 1.f);
        // Ilerleme yalniz GERCEK gecen sureden; sahte yuzde yok.
        const double f = e.budget_s > 0 ? std::min(1.0, cm::endurance_elapsed_s(m, now) / e.budget_s) : 0.0;
        if (f > 0) g.fill_round({bar.x, bar.y, std::max(kBarH, bar.w * static_cast<float>(f)), bar.h}, kBarH / 2, p.accent);
        hits.add_progress(Hit::Progress0, bar, t(StringId::MeasureEnduranceRunning), f * 100.0);
        y += kBarH + theme_.space(1);
        text_top = y;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const float lh = std::max(theme_.role(Role::Secondary).line, g.measure(Role::Secondary, lines[i], inner_w).h);
            TextOpts eo{Align::Leading, true};
            eo.tabular = i > 0;  // asama metni duz; sure ve anlik sayilar sabit genislik
            g.text(Role::Secondary, lines[i], {card.x + pad, y, inner_w, lh}, i == 0 ? p.text_primary : p.text_secondary, eo);
            y += lh;
        }
    } else if (!lines.empty()) {
        y += theme_.space(1);
        for (const auto& l : lines) {
            const float lh = g.measure(Role::Body, l, inner_w).h;
            g.text(Role::Body, l, {card.x + pad, y, inner_w, lh}, p.text_primary);
            y += lh + theme_.space(0);
        }
    }
    if (!notes.empty()) {
        y += theme_.space(1);
        for (const auto& n : notes) {
            const float nh = g.measure(Role::Secondary, n.first, inner_w).h;
            g.text(Role::Secondary, n.first, {card.x + pad, y, inner_w, nh}, n.second);
            y += nh + theme_.space(0);
        }
    }
    if (!lines.empty() || !notes.empty()) {
        // B14 incelemesi: ekran okuyucu asamayi, anlik degerleri ve sabitlik hukmunu da duyar (yalniz cizilen metin).
        std::wstring uia = t(StringId::MeasureEnduranceTitle);
        for (const auto& l : lines) uia = a11y_join(std::move(uia), l);
        for (const auto& n : notes) uia = a11y_join(std::move(uia), n.first);
        hits.add_text(seq.take(), {card.x + pad, text_top, inner_w, std::max(y - text_top, 1.f)}, std::move(uia));
    }
    if (has_button) {
        y = card.bottom() - pad - theme_.button_h;  // eylem dugmesi kartin alt kenarina hizali
        if (show_stop) button(g, card.x + pad, y, Hit::EnduranceStop, t(StringId::MeasureEnduranceStop), false, s, now, hits);
        else button(g, card.x + pad, y, Hit::EnduranceStart, t(StringId::MeasureEnduranceStart), true, s, now, hits);
    }
}

// P1: Performans etkisini olc (ABBA kanit deneyi). Sayilar yalniz bu deneyin olcumudur; tahmin / varsayim yoktur.
void Screen::measure_proof_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    UiState& s = mc.s;
    const std::int64_t now = mc.now;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float gap = mc.gap;
    const float section_h = mc.section_h;
    const bool tr = mc.tr;
    const bool busy = mc.busy;
    namespace cp = core::perf;
    const auto& pv = m.proof;
    const float inner_w = slot.area.w - 2 * pad;
    const auto desc = t(StringId::ProofDesc);
    const float desc_h = g.measure(Role::Secondary, desc, inner_w).h;
    const auto num = [&](double v, int dec) { return widen(cm::format_number(v, dec, tr)); };
    const auto sgn = [&](double v, int dec) { return std::wstring(v > 0 ? L"+" : v < 0 ? L"−" : L"") + num(std::fabs(v), dec); };
    std::vector<std::wstring> lines;                       // sonuc satirlari (Body)
    std::vector<std::pair<std::wstring, Color>> notes;     // cumle + notlar (Secondary)
    std::wstring stage, count;                             // calisirken
    bool show_start = false, show_stop = false;
    if (pv.running()) {
        show_stop = true;
        const auto& st = pv.stage;
        const auto sub_run = [&](StringId id) {
            return replace_all(replace_all(t(id), L"{i}", std::to_wstring(pv.progress.scored_done + 1)), L"{n}", std::to_wstring(pv.progress.scored_total));
        };
        if (st == "on") stage = t(StringId::ProofStageOn);
        else if (st == "off") stage = t(StringId::ProofStageOff);
        else if (st == "restore") stage = t(StringId::ProofStageRestore);
        else if (st == "warmup") stage = t(StringId::ProofStageWarmup);
        else if (st == "run_off") stage = sub_run(StringId::ProofStageRunOff);
        else if (st == "run_on") stage = sub_run(StringId::ProofStageRunOn);
        else stage = t(StringId::ProofStagePrepare);
        count = std::to_wstring(pv.progress.scored_done) + L"/" + std::to_wstring(pv.progress.scored_total);
        if (pv.scope_admin) notes.push_back({replace_all(t(StringId::ProofScopeAdmin), L"{n}", std::to_wstring(pv.scope_groups)), p.text_secondary});
        else if (pv.scope_partial) notes.push_back({replace_all(t(StringId::ProofScopeMeasured), L"{n}", std::to_wstring(pv.scope_groups)), p.text_secondary});
    } else if (pv.result) {
        const auto& r = *pv.result;
        const auto metric_line = [&](const cp::ProofMetric& mt, StringId label, int dec) -> std::wstring {
            if (!mt.mean_off || !mt.mean_on) return {};
            std::wstring l = t(label) + L": " + num(*mt.mean_off, dec) + L" → " + num(*mt.mean_on, dec);
            if (mt.diff && mt.ci_low && mt.ci_high)
                l += L" · " + replace_all(replace_all(replace_all(t(StringId::BaDiff), L"{diff}", sgn(*mt.diff, dec)), L"{lo}", sgn(*mt.ci_low, dec)),
                                               L"{hi}", sgn(*mt.ci_high, dec));
            return l;
        };
        const bool numbers = r.verdict == cp::ProofVerdict::Better || r.verdict == cp::ProofVerdict::Worse ||
                             r.verdict == cp::ProofVerdict::NoDifference || r.verdict == cp::ProofVerdict::Mixed;
        if (numbers) {
            for (const auto& l : {metric_line(r.avg_fps, StringId::ProofMetricAvg, 1), metric_line(r.low1_fps, StringId::ProofMetricLow1, 1),
                                  metric_line(r.p99_ms, StringId::ProofMetricP99, 2)})
                if (!l.empty()) lines.push_back(l);
            if (r.avg_fps.sd_off && r.avg_fps.sd_on)
                lines.push_back(replace_all(replace_all(t(StringId::ProofSpread), L"{off}", num(*r.avg_fps.sd_off, 1)), L"{on}", num(*r.avg_fps.sd_on, 1)));
        }
        std::wstring verdict;
        Color vc = p.text_secondary;
        switch (r.verdict) {
            case cp::ProofVerdict::Better: verdict = t(StringId::ProofResultBetter), vc = p.success; break;
            case cp::ProofVerdict::Worse: verdict = t(StringId::ProofResultWorse), vc = p.warning; break;
            case cp::ProofVerdict::NoDifference: verdict = t(StringId::ProofResultNone), vc = p.text_primary; break;
            case cp::ProofVerdict::Mixed: verdict = t(StringId::ProofResultMixed), vc = p.warning; break;
            case cp::ProofVerdict::InsufficientData: verdict = t(StringId::ProofResultInsufficient), vc = p.text_primary; break;
            case cp::ProofVerdict::ConditionsDiffer:
                verdict = replace_all(t(StringId::ProofResultConditions), L"{fields}", widen(r.reason)), vc = p.warning;
                break;
            case cp::ProofVerdict::InvalidRun: {
                auto why = tk("balance.reason." + r.reason);
                if (why.empty()) why = replace_all(t(StringId::ProofWhyOther), L"{code}", widen(r.reason));
                verdict = replace_all(replace_all(t(StringId::ProofResultInvalid), L"{n}", std::to_wstring(r.bad_run)), L"{why}", why), vc = p.warning;
                break;
            }
            case cp::ProofVerdict::Aborted: verdict = t(StringId::ProofResultAborted), vc = p.text_primary; break;
            case cp::ProofVerdict::RestoreFailed: verdict = t(StringId::ProofResultRestoreFailed), vc = p.warning; break;
        }
        notes.push_back({verdict, vc});
        if (r.verdict == cp::ProofVerdict::NoDifference && numbers) notes.push_back({t(StringId::ProofNoteNoneScope), p.text_secondary});
        if (r.low1_differs) notes.push_back({t(StringId::ProofNoteLow1), p.text_secondary});
        if (r.quality_tradeoff && numbers) notes.push_back({t(StringId::ProofNoteQuality), p.warning});
        // Hukum yalniz olculen alt kume icindir: yonetici gerektiren ayarlarin etkisi olculmedi (dogru etiket). P12-B2b: tek
        // satir (kapsam metni "olculmedi"yi de soyler); eski ayri alt-kume notu kalkti (ProofNoteSubset* katalogda duruyor).
        if (numbers && (pv.scope_partial || pv.scope_admin))
            notes.push_back({replace_all(t(pv.scope_admin ? StringId::ProofScopeAdmin : StringId::ProofScopeMeasured), L"{n}", std::to_wstring(pv.scope_groups)),
                             pv.scope_partial ? p.warning : p.text_primary});
        if (numbers) notes.push_back({t(StringId::ProofNoteScope), p.text_secondary});
    }
    const auto refusal_id = [&](cp::ProofRefusal rf) {
        StringId id = StringId::ProofRefuseGameRunning;
        switch (rf) {
            case cp::ProofRefusal::GameRunning: id = StringId::ProofRefuseGameRunning; break;
            case cp::ProofRefusal::OtherTestBusy: id = StringId::ProofRefuseOtherTestBusy; break;
            case cp::ProofRefusal::PerfNotOff: id = StringId::ProofRefusePerfNotOff; break;
            case cp::ProofRefusal::PlanNotReady: id = StringId::ProofRefusePlanNotReady; break;
            case cp::ProofRefusal::PlanEmpty: id = StringId::ProofRefusePlanEmpty; break;
            case cp::ProofRefusal::PlanNeedsAdmin: id = StringId::ProofRefusePlanNeedsAdmin; break;
            case cp::ProofRefusal::PlanNeedsReboot: id = StringId::ProofRefusePlanNeedsReboot; break;
            case cp::ProofRefusal::PlanBackgroundOnly: id = StringId::ProofRefusePlanBackgroundOnly; break;
            case cp::ProofRefusal::NoHardwareAdapter: id = StringId::ProofRefuseNoHardwareAdapter; break;
            case cp::ProofRefusal::None: break;
        }
        return id;
    };
    if (!pv.running() && pv.refusal != cp::ProofRefusal::None) notes.push_back({t(refusal_id(pv.refusal)), p.warning});
    // Baslat dugmesinin ALTINDA gorunur neden / olculecek alt kume (tiklamadan once; dugme metni UIA'da ayni kartta okunur).
    if (!pv.running() && pv.refusal == cp::ProofRefusal::None && cm::proof_can_start(m)) {
        if (pv.precheck != cp::ProofRefusal::None) notes.push_back({t(refusal_id(pv.precheck)), p.warning});
        else if (!pv.result && (pv.pre_partial || pv.pre_admin) && pv.pre_groups > 0)  // P11: sonuc kartinda kapsam satiri zaten var; tekrarlanmaz
            notes.push_back({replace_all(t(pv.pre_admin ? StringId::ProofScopeAdmin : StringId::ProofScopeMeasured), L"{n}", std::to_wstring(pv.pre_groups)), p.text_secondary});
    }
    if (!pv.running()) {
        if (cm::proof_can_start(m)) show_start = true;
        else if (busy || m.endurance.running) notes.push_back({t(StringId::ProofWait), p.text_secondary});
    }
    constexpr float kBarH = 8.f;
    float stage_h = 0;
    if (pv.running()) stage_h = std::max(theme_.role(Role::Secondary).line, g.measure(Role::Secondary, stage, inner_w).h) + theme_.role(Role::Secondary).line;
    float lines_h = 0, notes_h = 0;
    for (const auto& l : lines) lines_h += g.measure(Role::Body, l, inner_w).h + theme_.space(0);
    for (const auto& n : notes) notes_h += g.measure(Role::Secondary, n.first, inner_w).h + theme_.space(0);
    const bool has_button = show_start || show_stop;
    const std::wstring start_label = replace_all(t(StringId::ProofStart), L"{n}", std::to_wstring(std::max(pv.estimate_min, 1)));
    const float card_h = pad + section_h + gap + desc_h + (pv.running() ? gap + kBarH + theme_.space(1) + stage_h : 0.f) +
                         (lines.empty() ? 0.f : theme_.space(1) + lines_h) + (notes.empty() ? 0.f : theme_.space(1) + notes_h) +
                         (has_button ? gap + theme_.button_h : 0.f) + pad;
    RectF card;
    if (!slot.place(card_h, card)) return;
    g.fill_round(card, theme_.radius_card, p.surface);
    g.stroke_round(card, theme_.radius_card, pv.running() ? p.accent.with_alpha(alpha_scale::kOutline) : p.border, pv.running() ? 2.f : 1.f);
    log_card(card);
    float y = card.y + pad;
    g.text(Role::Section, t(StringId::ProofTitle), {card.x + pad, y, inner_w, section_h}, p.text_primary);
    y += section_h + gap;
    g.text(Role::Secondary, desc, {card.x + pad, y, inner_w, desc_h}, p.text_secondary);
    y += desc_h;
    float text_top = y;
    std::wstring uia = t(StringId::ProofTitle);
    if (pv.running()) {
        y += gap;
        const RectF bar{card.x + pad, y, inner_w, kBarH};
        g.fill_round(bar, kBarH / 2, p.surface_raised);
        g.stroke_round(bar, kBarH / 2, ensure_contrast(p.border, p.surface, p.text_secondary, 3.0), 1.f);
        // Ilerleme yalniz tamamlanan puanli olcum sayisindan (gercek); sahte yuzde yok.
        const double f = pv.progress.scored_total ? static_cast<double>(pv.progress.scored_done) / static_cast<double>(pv.progress.scored_total) : 0.0;
        if (f > 0) g.fill_round({bar.x, bar.y, std::max(kBarH, bar.w * static_cast<float>(f)), bar.h}, kBarH / 2, p.accent);
        hits.add_progress(static_cast<Hit>(static_cast<int>(Hit::Progress0) + 1), bar, t(StringId::ProofTitle), f * 100.0);
        y += kBarH + theme_.space(1);
        text_top = y;
        const float sh = std::max(theme_.role(Role::Secondary).line, g.measure(Role::Secondary, stage, inner_w).h);
        g.text(Role::Secondary, stage, {card.x + pad, y, inner_w, sh}, p.text_primary, {Align::Leading, true});
        y += sh;
        TextOpts co{Align::Leading, true};
        co.tabular = true;
        g.text(Role::Secondary, count, {card.x + pad, y, inner_w, theme_.role(Role::Secondary).line}, p.text_secondary, co);
        y += theme_.role(Role::Secondary).line;
        uia = a11y_join(a11y_join(std::move(uia), stage), count);
    }
    if (!lines.empty()) {
        y += theme_.space(1);
        for (const auto& l : lines) {
            const float lh = g.measure(Role::Body, l, inner_w).h;
            g.text(Role::Body, l, {card.x + pad, y, inner_w, lh}, p.text_primary);
            y += lh + theme_.space(0);
            uia = a11y_join(std::move(uia), l);
        }
    }
    if (!notes.empty()) {
        y += theme_.space(1);
        for (const auto& n : notes) {
            const float nh = g.measure(Role::Secondary, n.first, inner_w).h;
            g.text(Role::Secondary, n.first, {card.x + pad, y, inner_w, nh}, n.second);
            y += nh + theme_.space(0);
            uia = a11y_join(std::move(uia), n.first);
        }
    }
    if (!lines.empty() || !notes.empty() || pv.running())
        hits.add_text(seq.take(), {card.x + pad, text_top, inner_w, std::max(y - text_top, 1.f)}, std::move(uia));
    if (has_button) {
        y = card.bottom() - pad - theme_.button_h;
        if (show_stop) button(g, card.x + pad, y, Hit::ProofStop, t(StringId::ProofStop), false, s, now, hits);
        else button(g, card.x + pad, y, Hit::ProofStart, start_label, true, s, now, hits);
    }
}

// Iptal/hata bildirimi (M11: neden + sonraki adim; onceki sonuc asagida kalir)
void Screen::measure_cancel_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    HitMap& hits = mc.hits;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
        const float inner_w = slot.area.w - 2 * pad;
        if (m.phase == cm::TestPhase::Cancelled || m.phase == cm::TestPhase::Failed) {
        const auto msg = m.phase == cm::TestPhase::Cancelled ? t(StringId::MeasureCancelled) : measure_failure_text(m);
        const float h = g.measure(Role::Body, msg, inner_w).h;
        RectF card;
        if (!slot.place(h + 2 * pad, card)) return;
        g.fill_round(card, theme_.radius_card, p.surface);
        g.stroke_round(card, theme_.radius_card, m.phase == cm::TestPhase::Failed ? p.warning.with_alpha(alpha_scale::kOutline) : p.border);
        g.text(Role::Body, msg, card.inset(pad), m.phase == cm::TestPhase::Failed ? p.text_primary : p.text_secondary);
        hits.add_text(static_cast<Hit>(static_cast<int>(Hit::Status0) + 1), card, msg);
        }
}

// Sonuc diske yazilamadi uyarisi (tek satir serit)
void Screen::measure_save_failed_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float g1 = mc.g1;
    if (!m.history_save_failed) return;
    const auto msg = t(StringId::MeasureHistorySaveFailed);
    const float mh = g.measure(Role::Secondary, msg, slot.area.w - 2 * pad).h;
    RectF wc;
    if (!slot.place(g1 + mh + g1, wc)) return;
    g.fill_round(wc, theme_.radius_card, p.surface);
    g.stroke_round(wc, theme_.radius_card, p.warning.with_alpha(alpha_scale::kOutline));
    g.text(Role::Secondary, msg, {wc.x + pad, wc.y + g1, wc.w - 2 * pad, mh}, p.warning);
    hits.add_text(seq.take(), wc, msg);
}

// BENCH v2: bilesen karti (gercek metrikler; olculmeyen "Olculemedi"). Kartlar okuma sirasiyla ikiser esit
// satira yerlesir (spec 2026-11-12 §3).
void Screen::measure_component_card(const MeasureCtx& mc, const cm::ComponentCard& c, CardSlot& slot) {
    Graphics& g = mc.g;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const float pad = mc.pad;
    const float section_h = mc.section_h;
    const float sl = mc.sl;
    const float bl = mc.bl;
    const float g0 = mc.g0;
    const float g1 = mc.g1;
    const bool tr = mc.tr;
    const float row_h = std::max(bl, sl);
    // EF-X22: dogrulama notu yer tutuculari (gercek sayilar / komut seti adlari).
    auto note_text = [&](const cm::CardMetric& mt) {
        std::wstring n = tk(mt.note_key);
        for (const auto& [ak, av] : mt.note_args) n = replace_all(std::move(n), L"{" + widen(ak) + L"}", widen(av));
        return n;
    };
    {
        const float in_w = slot.area.w - 2 * pad;
        const std::wstring status = component_status_text(c);
        float h = pad + section_h + g1;
        if (!status.empty()) h += g.measure(Role::Secondary, status, in_w).h + g1;
        for (const auto& mt : c.metrics) {
            h += row_h + g0;
            if (!mt.note_key.empty()) h += g.measure(Role::Secondary, note_text(mt), in_w).h + g0;
        }
        RectF card;
        if (!slot.place(h - g0 + pad, card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        std::wstring title = tk(c.title_key);
        for (const auto& [ak, av] : c.args) title = replace_all(title, L"{" + widen(ak) + L"}", widen(av));
        g.text(Role::Section, title, {card.x + pad, card.y + pad, in_w, section_h}, p.text_primary, {Align::Leading, false});
        // EF-A11Y: kartin cizilen metni (baslik, durum, satir: deger, not) UIA'da tek salt okunur oge.
        std::wstring uia = title;
        float y = card.y + pad + section_h + g1;
        if (!status.empty()) {
            uia = a11y_join(std::move(uia), status);
            const float sh = g.measure(Role::Secondary, status, in_w).h;
            const auto tone = cm::status_tone(c.status);
            g.text(Role::Secondary, status, {card.x + pad, y, in_w, sh}, tone == cm::StatusTone::Secondary ? p.text_secondary : tone_color(p, tone));
            y += sh + g1;
        }
        for (const auto& mt : c.metrics) {
            const auto label = tk(mt.label_key);
            std::wstring val;
            Color vc = mt.value ? p.text_primary : p.text_secondary;
            if (!mt.value_key.empty()) {
                // EF-X22: durum metni (gecti / hesap tutarsizligi / desteklenmiyor / tamamlanmadi / calismadi).
                val = tk(mt.value_key);
                vc = mt.value_tone == "success" ? p.success : mt.value_tone == "error" ? p.error
                     : mt.value_tone == "warning"                                 ? p.warning
                                                                                 : p.text_secondary;
            } else if (mt.value) val = widen(cm::format_number(*mt.value, mt.decimals, tr)) + L" " + tk(mt.unit_key);
            else val = t(StringId::MetricMissing);
            const float vw = std::ceil(g.measure(Role::Body, val, kUnbounded, false, true).w);
            g.text(Role::Secondary, label, {card.x + pad, y, in_w - vw - theme_.space(2), row_h}, p.text_secondary, {Align::Leading, false});
            TextOpts vo{Align::Leading, false};
            vo.tabular = true;  // M13
            g.text(Role::Body, val, {card.x + card.w - pad - vw, y, vw, row_h}, vc, vo);
            uia = a11y_join(std::move(uia), label + L": " + val);
            y += row_h + g0;
            if (!mt.note_key.empty()) {
                const auto note = note_text(mt);
                const float nh = g.measure(Role::Secondary, note, in_w).h;
                g.text(Role::Secondary, note, {card.x + pad, y, in_w, nh}, p.text_secondary);
                uia = a11y_join(std::move(uia), note);
                y += nh + g0;
            }
        }
        hits.add_text(seq.take(), card, std::move(uia));
    }
}

// Karsilastirma
// B15: olculen fark Sonuc kartindaki cipte gosteriliyorsa ayni cumle ayrica tekrarlanmaz (karsilastirilamaz notu kalir).
void Screen::measure_comparison_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float gap = mc.gap;
    const float section_h = mc.section_h;
    const bool tr = mc.tr;
    const bool busy = mc.busy;
    const bool delta_in_result = !busy && m.result && !m.result->balance && m.result->source != cm::TestSource::Endurance &&
                                 m.comparison.available && m.comparison.comparable && m.comparison.delta_percent &&
                                 std::isfinite(*m.comparison.delta_percent);
    const float inner_w = slot.area.w - 2 * pad;
    if (m.comparison.available && !delta_in_result) {
    std::wstring txt;
    Color c = p.text_secondary;
    if (!m.comparison.comparable) {
        txt = tk(m.comparison.note_key);  // surum farki: metrik farki iddia edilmez
        if (txt.empty()) txt = tk(cm::verdict_key(m.comparison.verdict));
    } else if (m.comparison.delta_percent) {
        const double d = *m.comparison.delta_percent;
        const auto num = widen(cm::format_number(std::fabs(d), 1, tr));
        const auto pct = (d > 0 ? L"+" : d < 0 ? L"-" : L"") + (tr ? L"%" + num : num + L"%");
        txt = replace_all(t(StringId::ComparisonMeasuredDelta), L"{delta}", pct);
        if (txt.empty()) txt = tk(cm::verdict_key(m.comparison.verdict));
    } else {
        txt = tk(cm::verdict_key(m.comparison.verdict));
    }
    const float h = g.measure(Role::Body, txt, inner_w).h;
    RectF card;
    if (!slot.place(pad + section_h + gap + h + pad, card)) return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    g.text(Role::Section, t(StringId::MeasureCompare), {card.x + pad, card.y + pad, inner_w, section_h}, p.text_primary);
    if (m.comparison.comparable && m.comparison.verdict == cm::Verdict::Improved) c = p.success;
    if (m.comparison.comparable && m.comparison.verdict == cm::Verdict::Regressed) c = p.warning;
    g.text(Role::Body, txt, {card.x + pad, card.y + pad + section_h + gap, inner_w, h}, c);
    hits.add_text(seq.take(), card, a11y_join(t(StringId::MeasureCompare), txt));
}
}

}  // namespace ef::ui
