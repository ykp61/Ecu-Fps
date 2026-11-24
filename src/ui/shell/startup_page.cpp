// WIN-09: Baslangic uygulamalari bolumu. Girdi basina anahtar; "Uygula" ayrica onay (uygulama ici kip penceresi; App
// sorar) ister; sonuc geri okunarak gosterilir.
// WIN-10: hizmetler bolumu secim anahtari tasir; secim yalniz tercihtir, oturumluk durdurma Performans Acik planinda
// (broker, makine kapsami) Evet/Hayir onayiyla uygulanir ve Kapali/exit ile geri yuklenir.
// Spec 2026-11-12 B4: Sistem araclari tek sayfasinin ikinci ve ucuncu bolumu. Baslangic: iki sutun satir (anahtar, ad,
// en cok bir etiket: Kapatilmasi onerilir / Acik kalsin / Korunuyor); bekleyen degisiklik varsa baslik satirinda
// "N degisiklik bekliyor" + "Uygula", yoksa ikisi de yok. Hizmetler: uc sutun kutu (anahtar, ad, durum). Aciklama
// cumleleri (gerekce, tanitim, hizmet etkisi) UIA HelpText'te kalir.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/service/uac_copy.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
using core::startup::StartupPhase;
namespace sa = core::adapters::startup;

namespace {
std::wstring widen_s(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring rep_s(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
}  // namespace

float Screen::startup_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    const auto& p = pal();
    const auto& sv = vm.startup;
    const float sl = theme_.role(Role::Secondary).line;
    s.startup_phase = sv.phase;
    s.startup_toggles.assign(sv.rows.size(), false);
    s.startup_service_toggles.assign(sv.services.size(), false);  // WIN-10 (hizmetler bolumu yazar)
    s.startup_can_apply = sv.pending > 0 && !vm.game_gate_blocked;  // dugme oyun calisirken cizilmez
    const float pad = theme_.card_pad;
    const float x = area.x + pad, inner_w = std::max(area.w - 2 * pad, 1.f);

    // --- Spec 2026-11-14 B12 kart baslik seridi: ikon, baslik, durum ozeti (bekleyen degisiklik / taraniyor / girdi
    // sayisi) ve sagda "Uygula" (yalniz bekleyen degisiklik varken) ya da okuma hatasinda yeniden tarama ---
    std::wstring summary;
    Color sc = p.text_secondary;
    if (sv.phase == StartupPhase::Scanning || sv.phase == StartupPhase::Idle) summary = t(StringId::StartupScanning) + L"…";
    else if (sv.phase == StartupPhase::Applying) summary = t(StringId::StartupApplying) + L"…";
    else if (sv.phase == StartupPhase::Failed) summary = t(StringId::StartupScanFailed), sc = p.error;
    else if (sv.pending > 0) summary = rep_s(t(StringId::StartupPending), L"{count}", std::to_wstring(sv.pending)), sc = p.text_primary;
    else if (!sv.rows.empty()) summary = rep_s(t(StringId::SystoolsStartupCount), L"{n}", std::to_wstring(sv.rows.size()));
    const bool show_apply = sv.phase == StartupPhase::Ready && sv.pending > 0 && !vm.game_gate_blocked;
    const bool show_rescan = sv.phase == StartupPhase::Failed;
    const auto btn_label = show_rescan ? t(StringId::CleanupPreview) : t(StringId::StartupApplyShort);
    const float btn_w = show_apply || show_rescan ? button_rect(g, 0, 0, btn_label).w : 0.f;
    RectF slot{};
    // P12-B2b: okunurken ayni "okunuyor" metni hem baslikta hem govdede yazilmaz; govde satiri yazar (UIA'da da orada okunur).
    const bool body_status = sv.rows.empty() && (sv.phase == StartupPhase::Scanning || sv.phase == StartupPhase::Idle);
    const float line_y = section_head_v2(g, area, L"02", false, t(StringId::StartupAppsTitle), body_status ? std::wstring() : summary, sc, btn_w, hits, seq,
                                a11y_join(t(StringId::StartupIntro), sv.pending == 0 && sv.phase == StartupPhase::Ready ? t(StringId::StartupNoPending) : std::wstring()),
                                &slot, true);
    if (btn_w > 0) {
        // Okuma hatasinda "Onizleme" yeniden tarar; bekleyen degisiklikte ana dugme "Uygula" (UIA adi tam metin).
        const Hit id = show_rescan ? Hit::StartupRescan : Hit::StartupApply;
        // Baslik seridi eylemleri her kartta ayni boyutta (button_h; Temizlik dugmeleriyle es).
        button(g, slot.x, slot.y, id, btn_label, true, s, now, hits);
        if (!show_rescan)
            if (auto it = hits.meta.find(id); it != hits.meta.end()) it->second.name = t(StringId::StartupApply);
    }
    // Govde (ince cizginin altinda). Govde bos kalirsa (okunuyor) kart baslik seridinde biter, cizgi cizilmez.
    const float body_top = line_y <= area.y + 0.5f ? area.y + pad : line_y + 1 + pad;
    float y = body_top;

    // Onay: uygulama ici kip penceresi (App sorar; spec E). Sayfadaki Evet/Hayir seridi yalniz kip penceresi acik degilken
    // cizilir (yedek yol; kimlikler StartupYes / StartupNo korunur).
    if (sv.phase == StartupPhase::Confirming && !s.modal) {
        const float iw = inner_w - 2 * pad;
        const float section_h = theme_.role(Role::Section).line;
        const auto q = t(StringId::StartupQuestion);
        const auto d = t(StringId::StartupQuestionDesc);
        const auto yes = t(StringId::ActionConfirm), no = t(StringId::ActionCancel);
        const float yes_w = vm.game_gate_blocked ? 0.f : button_rect(g, 0, 0, yes).w + theme_.space(1);
        const float cbtn_w = yes_w + button_rect(g, 0, 0, no).w;
        constexpr float kTextMinW = 280.f;
        const bool side = iw >= kTextMinW + theme_.space(3) + cbtn_w;
        const float text_w = side ? iw - cbtn_w - theme_.space(3) : iw;
        const float qh = std::max(g.measure(Role::Section, q, text_w).h, section_h);
        const float dh = g.measure(Role::Secondary, d, text_w).h;
        const float text_h = qh + dh;
        const float h = pad + (side ? std::max(text_h, theme_.button_h) : text_h + theme_.space(1) + theme_.button_h) + pad;
        const RectF card{x, y, inner_w, h};
        g.fill_round(card, theme_.radius_card, p.surface_raised);
        g.stroke_round(card, theme_.radius_card, p.accent.with_alpha(alpha_scale::kOutline));
        const float cx = card.x + pad;
        const float ty = card.y + pad + (side ? (std::max(text_h, theme_.button_h) - text_h) / 2 : 0.f);
        g.text(Role::Section, q, {cx, ty, text_w, qh}, p.text_primary);
        g.text(Role::Secondary, d, {cx, ty + qh, text_w, dh}, p.text_secondary);
        float bx = side ? card.right() - pad - cbtn_w : cx;
        const float by = side ? card.y + (h - theme_.button_h) / 2 : card.y + pad + text_h + theme_.space(1);
        if (!vm.game_gate_blocked) bx += button(g, bx, by, Hit::StartupYes, yes, true, s, now, hits) + theme_.space(1);
        button(g, bx, by, Hit::StartupNo, no, false, s, now, hits);
        y += h + theme_.space(1);
    }
    // Son uygulama sonucu (geri okunarak dogrulandi / uygulanamadi / kapi nedeni): kisa satirlar.
    if (sv.last && (sv.phase == StartupPhase::Ready || sv.phase == StartupPhase::Failed)) {
        std::vector<std::pair<std::wstring, Color>> results;
        if (sv.last->applied > 0) results.push_back({rep_s(t(StringId::StartupResult), L"{count}", std::to_wstring(sv.last->applied)), p.success});
        if (sv.last->failed > 0) results.push_back({rep_s(t(StringId::StartupResultFailed), L"{count}", std::to_wstring(sv.last->failed)), p.warning});
        if (sv.last->skipped > 0 && !sv.last->gate_reason.empty()) results.push_back({tk(sv.last->gate_reason), p.warning});
        for (const auto& [text, c] : results) {
            const float h = g.measure(Role::Secondary, text, inner_w).h;
            const RectF r{x, y, inner_w, h};
            g.text(Role::Secondary, text, r, ensure_contrast(c, p.surface, p.text_primary, 4.5));
            hits.add_text(seq.take(), r, text);
            y += h;
        }
        if (!results.empty()) y += theme_.space(1);
    }
    if (sv.phase == StartupPhase::Ready && sv.rows.empty()) {
        const auto empty = t(StringId::StartupEmpty);
        const RectF r{x, y, inner_w, sl};
        g.text(Role::Secondary, fit_text(g, Role::Secondary, empty, inner_w), r, p.text_secondary, {Align::Leading, false, true});
        hits.add_text(seq.take(), r, empty);
        section_card_rule(g, area, line_y);
        return y + sl + pad - area.y;
    }
    if (sv.rows.empty()) {
        if (y <= body_top) {
            // Okunuyor / hata: kart tek satirlik durum metniyle (baslik satirindaki ozet kartta da okunur).
            if (summary.empty()) return 0.f;
            g.text(Role::Secondary, fit_text(g, Role::Secondary, summary, inner_w), {x, y, inner_w, sl}, ensure_contrast(sc, p.surface, p.text_primary, 4.5), {Align::Leading, false, true});
            if (body_status) hits.add_text(seq.take(), {x, y, inner_w, sl}, summary);
            return y + sl + pad - area.y;
        }
        section_card_rule(g, area, line_y);
        return y - theme_.space(1) + pad - area.y;
    }
    section_card_rule(g, area, line_y);

    // --- Satirlar: iki sutun (satir-oncelikli: 0 1 / 2 3 ...); anahtar · ad · tek etiket ---
    const float col_gap = theme_.space(4);
    const int cols = systools_columns(inner_w, col_gap, 2, kSysToolsTileMinW);
    const std::size_t ucols = static_cast<std::size_t>(cols);
    const float cw = (inner_w - col_gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    // BOOST maketi: her girdi bir cukur (.well-v2 r8, p-3): anahtar | ad 13px/600 + gerekce 12px | etiket; kutular arasi 8.
    constexpr float kTilePad = 12.f, kWellGap = 8.f;
    const float nl = theme_.role(Role::Semi13).line, rl = theme_.role(Role::Small12).line;
    // P12-B2b: satirda yalniz anahtar + ad + en cok bir etiket; gerekce cumlesi anahtarin HelpText'inde okunur.
    (void)rl;
    const float row_h = std::max(theme_.row_h, nl + 2 * 8.f);
    const float tog_w = theme_.toggle_w, tog_h = theme_.toggle_h;
    const float tog_dx = tog_w + theme_.space(2);
    const bool editable_phase = sv.phase == StartupPhase::Ready;
    for (std::size_t i = 0; i < sv.rows.size(); i += ucols) {
        const bool visible = y + row_h >= s.view_top && y <= s.view_bottom;
        if (visible) ++s.rows_drawn;
        for (std::size_t k = i; k < std::min(sv.rows.size(), i + ucols); ++k) {
            const auto& r = sv.rows[k];
            const float cx = x + static_cast<float>(k - i) * (cw + col_gap);
            const Hit id = hit_of(HitKind::StartupToggle, k);
            const bool can = editable_phase && r.editable;
            s.startup_toggles[k] = can;
            const std::wstring name = widen_s(r.name);
            std::wstring chip_text;
            ChipTone tone = ChipTone::Neutral;
            if (!r.editable) chip_text = t(StringId::StartupProtected), tone = ChipTone::Danger;  // korunan / guvenlik: anahtar kilitli
            else if (r.advice == sa::Advice::RecommendDisable) chip_text = t(StringId::StartupBadgeDisable), tone = ChipTone::Warn;
            else if (r.advice == sa::Advice::Keep) chip_text = t(StringId::StartupBadgeKeep), tone = ChipTone::Good;
            const RectF tg{cx + kTilePad, y + (row_h - tog_h) / 2, tog_w, tog_h};
            if (visible) draw_well(g, {cx, y, cw, row_h});
            if (visible) small_toggle(g, tg, r.desired, can, id, name, s, now, hits);
            else toggle_hit(tg, r.desired, can, id, name, hits);  // RDY-10: cizilmeden kayit
            if (id != Hit::None) {
                // Kaldirilan gerekce ve durum cumleleri: anahtarin HelpText'i (bilgi kaybolmaz).
                std::wstring help = tk(r.reason_key);
                if (r.ours) help = a11y_join(std::move(help), t(StringId::StartupOurs));
                if (r.admin) help = a11y_join(std::move(help), t(StringId::StartupAdmin));
                if (!chip_text.empty()) help = a11y_join(std::move(help), chip_text);
                else if (r.advice == sa::Advice::NoAdvice) help = a11y_join(std::move(help), t(StringId::StartupBadgeNone));
                hits.meta[id].help = std::move(help);
            }
            if (!visible) continue;
            const float chip_w = chip_text.empty() ? 0.f : chip_width(g, chip_text);
            const float tx = cx + kTilePad + tog_dx;
            const float nw = std::max(cw - kTilePad - tog_dx - kTilePad - (chip_w > 0 ? theme_.space(2) + chip_w : 0.f), 1.f);
            const float ty = y + (row_h - nl) / 2;
            g.text(Role::Semi13, fit_text(g, Role::Semi13, name, nw), {tx, ty, nw, nl}, p.text_primary, {Align::Leading, false, true});
            if (chip_w > 0) chip(g, RectF{cx + cw - kTilePad - chip_w, y + (row_h - sl) / 2, chip_w, sl}, chip_text, tone);
        }
        y += row_h + (i + ucols < sv.rows.size() ? kWellGap : 0.f);
    }
    return y + pad - area.y;
}

float Screen::services_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    const auto& sv = vm.startup;
    if (sv.services.empty()) return 0.f;
    const auto& p = pal();
    constexpr float kUnbounded = 10000.f;
    const float pad = theme_.card_pad;
    const float x = area.x + pad, inner_w = std::max(area.w - 2 * pad, 1.f);
    // Spec 2026-11-14 B12: kendi kartinda; ozet hizmet sayisi. Eylem yok (secim Performans Acik planinda uygulanir).
    const std::wstring summary = rep_s(t(StringId::SystoolsServicesCount), L"{n}", std::to_wstring(sv.services.size()));
    // P12-B2b: baslik seridinde yalniz hizmet sayisi; uzun aciklama (Performans Acik / UAC) anahtarin degil baslik metninin
    // HelpText'inde okunur (bilgi kaybolmaz).
    float y = section_card_rule(g, area,
                                section_head_v2(g, area, L"03", false, t(StringId::StartupServicesTitle), summary, p.text_secondary, 0.f, hits, seq,
                                                t(core::service::admin_copy(StringId::StartupServicesDesc, vm.admin_without_uac)), nullptr, false));
    const float gap = theme_.space(2);
    const int cols = systools_columns(inner_w, gap, 3, kSysToolsServiceMinW);
    const std::size_t ucols = static_cast<std::size_t>(cols);
    const float cw = (inner_w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float tile_h = theme_.row_h + theme_.space(1);
    const float tp = theme_.space(2);
    const float tog_w = theme_.toggle_w, tog_h = theme_.toggle_h;
    const float tog_dx = tog_w + theme_.space(2);
    for (std::size_t i = 0; i < sv.services.size(); i += ucols) {
        const bool visible = y + tile_h >= s.view_top && y <= s.view_bottom;
        for (std::size_t k = i; k < std::min(sv.services.size(), i + ucols); ++k) {
            const auto& sr = sv.services[k];
            const RectF tile{x + static_cast<float>(k - i) * (cw + gap), y, cw, tile_h};
            const std::wstring name = widen_s(sr.display_name);
            const std::wstring state =
                !sr.present ? t(StringId::StartupSvcAbsent) : sr.running ? t(StringId::StartupSvcRunning) : t(StringId::StartupSvcStopped);
            if (visible) {
                g.fill_round(tile, theme_.radius_control, sr.present ? p.surface : p.background);
                g.stroke_round(tile, theme_.radius_control, sr.selected && sr.present ? p.cyan : p.border, sr.selected && sr.present ? 1.5f : 1.f);
            }
            if (sr.present) {  // WIN-10: secim yalniz var olan hizmette; durmus hizmet devre disi
                const Hit id = hit_of(HitKind::StartupServiceToggle, k);
                const bool can = sr.editable && sv.phase == StartupPhase::Ready;
                s.startup_service_toggles[k] = can;
                const RectF tg{tile.x + tp, tile.y + (tile_h - tog_h) / 2, tog_w, tog_h};
                if (visible) small_toggle(g, tg, sr.selected, can, id, name, s, now, hits);
                else toggle_hit(tg, sr.selected, can, id, name, hits);
                if (id != Hit::None) hits.meta[id].help = a11y_join(tk(sr.breaks_key), state);
            }
            // Olmayan hizmetin salt okunur metni gorunurlukten bagimsiz kaydedilir (RDY-10 gibi): aksi halde UIA agaci kaydirmayla
            // degisir ve sonraki tum status.N kimlikleri kayar (ayni AutomationId/RuntimeId baska metne gecer).
            if (!sr.present) hits.add_text(seq.take(), tile, a11y_join(name, state));
            if (!visible) continue;
            const float tx = tile.x + tp + tog_dx;  // anahtar yoksa da ad sutunu hizali kalir
            const float state_w = std::min(std::ceil(g.measure(Role::Secondary, state, kUnbounded, false).w) + 1.f, (tile.right() - tp - tx) * 0.5f);
            const float nw = std::max(tile.right() - tp - state_w - theme_.space(2) - tx, 1.f);
            g.text(Role::Body, fit_text(g, Role::Body, name, nw), {tx, tile.y, nw, tile_h}, sr.present ? p.text_primary : p.text_secondary,
                   {Align::Leading, false, true});
            const RectF stb{tile.right() - tp - state_w, tile.y, state_w, tile_h};
            g.text(Role::Secondary, fit_text(g, Role::Secondary, state, state_w), stb, sr.running ? p.text_primary : p.text_secondary,
                   {Align::Trailing, false, true});
        }
        y += tile_h + (i + ucols < sv.services.size() ? gap : 0.f);
    }
    return y + pad - area.y;
}

}  // namespace ef::ui
