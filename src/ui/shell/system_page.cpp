// Spec 2026-11-12 B4: Sistem araclari sekmesiz tek sayfa. Bolumler alt alta, her birinin basinda ince cizgili baslik:
// Temizlik -> Baslangic uygulamalari -> Oyun sirasinda durdurulacak hizmetler -> Sistem (Panel'den tasinan envanter +
// Depolama karti icerigi). Hicbir bilgi kaybolmaz: kaldirilan aciklamalar UIA HelpText'te okunur.
// Spec 2026-11-14 B12: her bolum kendi kartinda; kart basinda baslik seridi (ikon rozeti, baslik, kisa ozet, sagda
// eylemler) ve altinda ince cizgi. Kartlar arasi bosluk card_gap.
//  - Sistem kontrolu bulgulari (liste) Oyun hazirligi sayfasinda cizilir (findings_card).
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/guided/bios_guide.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace ci = core::inventory;

namespace {
constexpr float kSecGap = 14.f;  // maket: bolumler arasi gap-3.5
std::wstring widen_y(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring rep_y(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
std::wstring root_text(const std::string& r) {
    std::wstring w = widen_y(r);
    while (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
    return w;
}
}  // namespace

float Screen::section_rule(Graphics& g, float x, float y, float w, const std::wstring& title, HitMap& hits, StatusSeq& seq,
                           const std::wstring& help, float right_reserved) {
    // Bolum basligi: metin + saginda dikey ortali ince cizgi (sag yuvaya kadar). UIA'da salt okunur metin; kaldirilan
    // bolum aciklamasi HelpText'te.
    const auto& p = pal();
    constexpr float kUnbounded = 10000.f;
    const float h = std::max(theme_.role(Role::Section).line, theme_.button_row_h);
    const float tw = std::min(std::ceil(g.measure(Role::Section, title, kUnbounded, false).w) + 1.f, w);
    const RectF tr{x, y, tw, h};
    g.text(Role::Section, title, tr, p.text_primary, {Align::Leading, false, true});
    const float lx = x + tw + theme_.space(2);
    const float lw = w - (lx - x) - right_reserved;
    if (lw > theme_.space(2)) {
        const RectF line{lx, std::floor(y + h / 2), lw, 1};
        if (pal_ == &theme_.dark) {
            // BOOST .sec-rule-line: vurgu -> camgobegi -> neredeyse saydam beyaz (soldan saga).
            const GradientStop st[] = {{0.f, depth::kRuleA}, {0.5f, depth::kRuleB}, {1.f, depth::kRuleC}};
            g.fill_linear_round(line, 0.f, {line.x, line.y}, {line.right(), line.y}, st);
        } else {
            g.fill(line, mix(p.background, p.border, alpha_scale::kDivider));
        }
    }
    const Hit id = seq.take();
    hits.add_text(id, tr, title);
    if (!help.empty()) hits.meta[id].help = help;
    return h;
}

float Screen::section_card_head(Graphics& g, const RectF& card, Icon icon, const std::wstring& title, const std::wstring& summary,
                                const Color& summary_c, float right_w, HitMap& hits, StatusSeq& seq, const std::wstring& help, RectF* slot) {
    const auto& p = pal();
    const bool hc = pal_ != &theme_.dark;
    constexpr float kUnbounded = 10000.f;
    constexpr float kIconBox = 32.f;      // ikon rozeti (Oyun hazirligi kutucuklariyla ayni)
    constexpr float kIconInset = 6.f;     // rozet icinde 20 DIP ikon
    constexpr float kSummaryMinW = 64.f;  // ozetin cizilecegi asgari genislik (altinda yalniz UIA'da)
    const float pad = theme_.card_pad;
    const float x = card.x + pad, w = std::max(card.w - 2 * pad, 1.f);
    const float y = card.y + pad;
    const float row_h = std::max(kIconBox, theme_.button_h);
    const float tx = x + kIconBox + theme_.space(2);
    const float title_w = std::ceil(g.measure(Role::Section, title, kUnbounded, false).w) + 1.f;
    const float sum_w = summary.empty() ? 0.f : std::ceil(g.measure(Role::Secondary, summary, kUnbounded, false).w) + 1.f;
    // Uyari/hata tonlu ozet (or. okunamayan kayit) kirpilmaz: sag oge satira sigmazsa ikinci satira iner.
    const bool keep_sum = summary_c == p.warning || summary_c == p.error;
    const float need = title_w + (sum_w > 0 ? theme_.space(2) + (keep_sum ? sum_w : std::min(sum_w, kSummaryMinW)) : 0.f);
    const bool inline_slot = right_w <= 0 || tx + need + theme_.space(3) + right_w <= x + w;
    const float text_right = right_w > 0 && inline_slot ? x + w - right_w - theme_.space(3) : x + w;
    {
        const RectF ib{x, y + (row_h - kIconBox) / 2, kIconBox, kIconBox};
        if (hc) {
            g.stroke_round(ib, theme_.radius_control, p.border);
            g.draw_icon(icon, ib.inset(kIconInset), p.text_primary);
        } else {
            g.fill_round(ib, theme_.radius_control, mix(p.surface, p.accent, alpha_scale::kIconBoxTint));
            g.draw_icon(icon, ib.inset(kIconInset), ensure_contrast(p.accent, p.surface, p.text_primary, 3.0));
        }
        const float tw = std::min(title_w, std::max(text_right - tx, 1.f));
        g.text(Role::Section, fit_text(g, Role::Section, title, tw), {tx, y, tw, row_h}, p.text_primary, {Align::Leading, false, true});
        const float sx = tx + tw + theme_.space(2);
        const float sw = std::min(sum_w, text_right - sx);
        if (sum_w > 0 && sw >= std::min(sum_w, kSummaryMinW))
            g.text(Role::Secondary, fit_text(g, Role::Secondary, summary, sw), {sx, y, sw, row_h}, ensure_contrast(summary_c, p.surface, p.text_primary, 4.5),
                   {Align::Leading, false, true});
    }
    // UIA: baslik + ozet tek salt okunur metin; kaldirilan bolum aciklamasi HelpText'te.
    const Hit id = seq.take();
    hits.add_text(id, {x, y, std::max(text_right - x, 1.f), row_h}, summary.empty() ? title : title + L" · " + summary);
    if (!help.empty()) hits.meta[id].help = help;
    float bottom = y + row_h;
    if (right_w > 0 && slot) {
        if (inline_slot) {
            *slot = {x + w - right_w, y + (row_h - theme_.button_h) / 2, right_w, theme_.button_h};
        } else {
            *slot = {std::max(x, x + w - right_w), bottom + theme_.space(1), std::min(right_w, w), theme_.button_h};
            bottom = slot->bottom();
        }
    }
    return std::floor(bottom + pad);
}

float Screen::section_card_rule(Graphics& g, const RectF& card, float line_y) {
    if (line_y <= card.y + 0.5f) return card.y + theme_.card_pad;  // kart ici ust satir yok (section_head_v2)
    const auto& p = pal();
    g.fill({card.x + 1, line_y, std::max(card.w - 2, 1.f), 1}, mix(p.surface, p.border, alpha_scale::kDivider));
    return line_y + 1 + theme_.card_pad;
}

float Screen::section_head_v2(Graphics& g, const RectF& card, std::wstring_view code, bool violet, const std::wstring& title, const std::wstring& summary,
                              const Color& summary_c, float right_w, HitMap& hits, StatusSeq& seq, const std::wstring& help, RectF* slot,
                              bool slot_in_heading) {
    const auto& p = pal();
    constexpr float kUnbounded = 10000.f;
    const bool dark = pal_ == &theme_.dark;
    const float hy = card.y - sec_head_gap_ - sec_head_h();
    const float x = card.x, w = card.w;
    const std::wstring code_txt = std::wstring(code) + L" //";
    const float cw = std::ceil(g.measure(Role::Mono11Semi, code_txt, kUnbounded, false).w) + 1.f;
    g.text(Role::Mono11Semi, code_txt, {x, hy, cw, sec_head_h()}, dark ? (violet ? p.violet : p.accent) : p.text_primary, {Align::Leading, false, true});
    const float tx = x + cw + 8.f;
    const float tw = std::min(std::ceil(g.measure(Role::Section, title, kUnbounded, false).w) + 1.f, std::max(w - (tx - x), 1.f));
    g.text(Role::Section, title, {tx, hy, tw, sec_head_h()}, p.text_primary, {Align::Leading, false, true});
    float rx = x + w;  // sag sinir (yuva / ozet icin)
    const bool card_toolbar = !slot_in_heading && right_w > 0;
    if (slot_in_heading && right_w > 0 && slot) {
        *slot = {x + w - right_w, hy + (sec_head_h() - theme_.button_h) / 2, right_w, theme_.button_h};
        rx = slot->x - 12.f;
    }
    if (!summary.empty() && !card_toolbar) {
        const float avail = std::max(rx - (tx + tw + 24.f), 0.f);
        const float sw = std::min(std::ceil(g.measure(Role::Small12, summary, kUnbounded, false).w) + 1.f, avail);
        if (sw > 24.f) {
            g.text(Role::Small12, fit_text(g, Role::Small12, summary, sw), {rx - sw, hy, sw, sec_head_h()}, ensure_contrast(summary_c, p.background, p.text_primary, 4.5),
                   {Align::Trailing, false, true});
            rx -= sw + 12.f;
        }
    }
    const float lx = tx + tw + 12.f;
    if (rx - lx > 8.f) {
        const RectF line{lx, std::floor(hy + sec_head_h() / 2), rx - lx, 1};
        if (dark) {
            const GradientStop st[] = {{0.f, depth::kRuleA}, {0.5f, depth::kRuleB}, {1.f, depth::kRuleC}};
            g.fill_linear_round(line, 0.f, {line.x, line.y}, {line.right(), line.y}, st);
        } else {
            g.fill(line, mix(p.background, p.border, alpha_scale::kDivider));
        }
    }
    const Hit id = seq.take();
    hits.add_text(id, {x, hy, std::max(tx + tw - x, 1.f), sec_head_h()}, summary.empty() || card_toolbar ? title : title + L" · " + summary);
    if (!help.empty()) hits.meta[id].help = help;
    if (!card_toolbar) return card.y;
    // Kart ici ust satir: ozet solda, eylem yuvasi sagda (sigmazsa yuva altta saga yasli).
    const float pad = theme_.card_pad;
    const float ix = card.x + pad, iw = std::max(card.w - 2 * pad, 1.f);
    const float row_h = theme_.button_h;
    const float sum_w = summary.empty() ? 0.f : std::ceil(g.measure(Role::Secondary, summary, kUnbounded, false).w) + 1.f;
    const bool inline_slot = ix + std::min(sum_w, 64.f) + theme_.space(3) + right_w <= ix + iw;
    const float text_right = inline_slot ? ix + iw - right_w - theme_.space(3) : ix + iw;
    const float y = card.y + pad;
    if (!summary.empty()) {
        const float sw = std::max(std::min(sum_w, text_right - ix), 1.f);
        g.text(Role::Secondary, fit_text(g, Role::Secondary, summary, sw), {ix, y, sw, row_h}, ensure_contrast(summary_c, p.surface, p.text_primary, 4.5),
               {Align::Leading, false, true});
        hits.add_text(seq.take(), {ix, y, sw, row_h}, summary);
    }
    float bottom = y + row_h;
    if (slot) {
        if (inline_slot) {
            *slot = {ix + iw - right_w, y, right_w, row_h};
        } else {
            *slot = {std::max(ix, ix + iw - right_w), bottom + theme_.space(1), std::min(right_w, iw), row_h};
            bottom = slot->bottom();
        }
    }
    return std::floor(bottom + pad);
}

float Screen::systools_jump_strip(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    // BOOST maketi "Bolumler:" seridi (card-v2 r10, p-2.5): 12px ikincil etiket, her bolum icin "NN // ad" mono hapi (beyaz
    // %5, r6) ve sagda kirmizi "Temizlik geri alinamaz" rozeti. Hap sayilari yalniz gercek okunan listelerden.
    const auto& p = pal();
    const bool dark = pal_ == &theme_.dark;
    // .card-v2 p-2 / px-3 (+ 1 DIP cizgi) -> 9 / 13; hap: 12px/400 Segoe, py-1 px-2.5, r6, beyaz %3 (hover %6).
    constexpr float kUnbounded = 10000.f, kPad = 13.f, kPadY = 9.f, kPillH = 24.f, kGap = 8.f, kPillPadX = 10.f, kRowGap = 6.f;
    std::wstring names[4] = {t(StringId::SystoolsJumpCleanup), t(StringId::SystoolsJumpStartup), t(StringId::SystoolsJumpServices), t(StringId::SystoolsJumpSystem)};
    if (!vm.startup.rows.empty()) names[1] += L" (" + std::to_wstring(vm.startup.rows.size()) + L")";
    if (!vm.startup.services.empty()) names[2] += L" (" + std::to_wstring(vm.startup.services.size()) + L")";
    const std::wstring label = t(StringId::SystoolsJumpLabel);
    const std::wstring badge = t(StringId::CleanupIrreversibleChip);
    const float lab_w = std::ceil(g.measure(Role::Small12, label, kUnbounded, false).w) + 1.f;
    const float badge_w = chip_width(g, badge);
    const float line_h = theme_.role(Role::Secondary).line;
    float pw[4];
    for (int i = 0; i < 4; ++i)
        pw[i] = std::ceil(g.measure(Role::Small12, L"0" + std::to_wstring(i + 1) + L" // " + names[i], kUnbounded, false).w) + 2 * kPillPadX;
    const float iw = std::max(area.w - 2 * kPad, 1.f);
    // Yerlesim: etiket + hap akisi; satira sigmayan hap alt satira iner. Rozet ilk satirin sagindadir (sigmazsa kendi satiri).
    float cx = lab_w + kGap, cy = 0;
    RectF pr[4];
    float first_row_right = lab_w;
    for (int i = 0; i < 4; ++i) {
        if (cx + pw[i] > iw && !(cx == 0 && cy > 0)) cx = 0, cy += kPillH + kRowGap;
        pr[i] = {cx, cy, std::min(pw[i], iw), kPillH};
        cx += pr[i].w + kGap;
        if (cy == 0) first_row_right = pr[i].right();
    }
    const bool badge_inline = first_row_right + kGap + badge_w <= iw;
    float rows_h = cy + kPillH;
    const float badge_y = badge_inline ? 0.f : rows_h + kRowGap;
    if (!badge_inline) rows_h = badge_y + line_h;
    const float card_h = rows_h + 2 * kPadY;
    const RectF card{area.x, area.y, area.w, card_h};
    const bool visible = card.bottom() >= s.view_top && card.y <= s.view_bottom;
    if (visible) draw_card(g, card, false, depth::kPanelCardRadius);
    else log_card(card);
    const float ox = card.x + kPad, oy = card.y + kPadY;
    if (visible) g.text(Role::Small12, label, {ox, oy, lab_w, kPillH}, p.text_secondary, {Align::Leading, false, true});
    hits.add_text(seq.take(), {ox, oy, lab_w, kPillH}, label);
    for (int i = 0; i < 4; ++i) {
        const RectF r{ox + pr[i].x, oy + pr[i].y, pr[i].w, kPillH};
        const Hit id = static_cast<Hit>(static_cast<int>(Hit::SysJump0) + i);
        const std::wstring txt = L"0" + std::to_wstring(i + 1) + L" // " + names[i];
        if (visible) {
            const float hv = hover_value(s, id, now);
            if (dark) g.fill_round(r, 6.f, depth::rgba(255, 255, 255, 0.03f + 0.03f * hv));
            else g.stroke_round(r, 6.f, hv > 0.f ? p.accent : p.border);
            g.text(Role::Small12, fit_text(g, Role::Small12, txt, std::max(r.w - 2 * kPillPadX, 1.f)), r, mix(p.text_secondary, p.text_primary, hv),
                   {Align::Center, false, true});
            if (s.focus_visible && s.focus == id) focus_ring(g, r, 6.f, hits);
        }
        hits.add(id, r, txt, AccRole::Button);
    }
    const RectF br{ox + iw - badge_w, oy + badge_y + (badge_inline ? (kPillH - line_h) / 2 : 0.f), badge_w, line_h};
    if (visible) chip(g, br, badge, ChipTone::Danger);
    hits.add_text(hit_of(HitKind::Status, static_cast<std::size_t>(kMaxStatusItems) - 1), br, t(StringId::CleanupIrreversible));
    return card_h;
}

float Screen::system_tools_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin (pb-1) 16 DIP altinda baslatir; ortak duzen 8 verir (Oyun
    // hazirligi / Olcumler ile ayni 5 DIP pay).
    constexpr float kTopInset = 5.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    sec_head_gap_ = kSecHeadGap;
    Stack full{body, theme_.card_gap};
    game_banner(g, full, body, vm, s, now, hits);
    StatusSeq seq{0};  // salt okunur UIA metinleri (bolum basliklari, halka, sonuc satirlari, envanter satirlari)
    const float gap = kSecGap;
    float y = body.y + full.cursor;
    y += systools_jump_strip(g, {body.x, y, body.w, 0}, s, vm, now, hits, seq) + gap;
    // Spec 2026-11-14 B12: her bolum kendi kartinda (baslik seridi + ince cizgi + govde). Kart cercevesi icerikten once,
    // son cizimdeki yukseklikle cizilir; yukseklik degistiyse kabuk ayni karede bir kez daha cizilir (systools_relayout).
    // systools_sections: bolum kartlarinin govde koordinatindaki ust kenari (eski alt sekme baglantisi o bolume kaydirir).
    const auto section = [&](std::size_t idx, auto&& draw) {
        s.systools_sections[idx] = y - body_in.y;
        if (s.systools_card_h[idx] > 0) y += sec_head_h() + sec_head_gap_;  // .sec-rule basligi kartin ustunde (section_head_v2 cizer)
        const RectF card{body.x, y, body.w, s.systools_card_h[idx]};
        if (card.h > 0) {
            if (card.bottom() >= s.view_top && card.y <= s.view_bottom) draw_card(g, card);
            else log_card(card);
        }
        const float used = draw(card);
        if (std::fabs(used - card.h) > 0.5f) {
            s.systools_card_h[idx] = used;
            s.systools_relayout = true;
        }
        if (used > 0) y += used + gap;
    };
    section(0, [&](const RectF& c) { return cleanup_section(g, c, s, vm, now, hits, seq); });
    section(1, [&](const RectF& c) { return startup_section(g, c, s, vm, now, hits, seq); });
    section(2, [&](const RectF& c) { return services_section(g, c, s, vm, now, hits, seq); });
    section(3, [&](const RectF& c) { return inventory_section(g, c, s, vm, hits, seq); });
    return std::max(y - gap - body.y, 0.f) + kTopInset;
}

float Screen::inventory_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, HitMap& hits, StatusSeq& seq) {
    // Sistem bolumu (Panel'den tasinan envanter): parca turu, ad, kisa ayrinti — CPU, GPU, RAM, depolama (+ birim
    // doluluklari), ekran, anakart + BIOS, Windows. Yalniz envanterin okudugu alanlar; okunamayan "—" + "Okunamadi".
    // Spec 2026-11-14 B12: kendi kartinda; ozet Windows surumu; govde siki etiket/deger izgarasi (genis govdede iki
    // sutun: ustte kucuk etiket, altta deger + ayrinti). Izgaranin altinda Depolama karti icerigi: birim basina kok · tur ·
    // model, doluluk cubugu, bos alan, durum (+ oyun / HDD notu). Kaldirilan oneri kurali UIA HelpText'tedir.
    const auto& p = pal();
    const bool hc = pal_ != &theme_.dark;
    constexpr float kUnbounded = 10000.f;
    const float pad = theme_.card_pad;
    const float x = area.x + pad, inner_w = std::max(area.w - 2 * pad, 1.f);
    const float row_h = theme_.row_h;
    const float sl = theme_.role(Role::Secondary).line;
    const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
    const auto type_text = [&](const ci::VolumeAdvice& a) -> std::wstring {
        if (a.bus == ci::BusKind::Nvme) return t(StringId::StorageTypeNvme);
        if (a.media == ci::MediaKind::Ssd) return t(StringId::StorageTypeSsd);
        if (a.media == ci::MediaKind::Hdd) return t(StringId::StorageTypeHdd);
        return {};
    };
    // Depolama satiri: sistem diskinin adi + boyutu; ayrinti birimlerin dolulugu ("C: %68 dolu · D: %86 dolu").
    std::wstring sto_main, sto_detail;
    if (!vm.storage_loading && !vm.storage.empty()) {
        const auto& a = vm.storage.front();
        const std::wstring type = type_text(a);
        sto_main = !a.disk_model.empty() ? widen_y(a.disk_model) : !type.empty() ? type : root_text(a.root);
        if (a.total_gib) sto_main += L" · " + rep_y(t(StringId::SysStorageSize), L"{v}", std::to_wstring(*a.total_gib));
        for (const auto& v : vm.storage) {
            if (!v.free_pct) continue;
            const int used = static_cast<int>(std::lround(100.0 - *v.free_pct));
            if (!sto_detail.empty()) sto_detail += L" · ";
            sto_detail += root_text(v.root) + L" " + rep_y(t(StringId::SystoolsStorageFill), L"{pct}", std::to_wstring(std::clamp(used, 0, 100)));
        }
    }
    struct Row {
        StringId label;
        std::wstring main, detail;
        bool loading = false, unknown = false;
    };
    const auto make = [&](StringId label, std::wstring main, std::wstring detail, bool loading) {
        Row r{label, std::move(main), std::move(detail), loading};
        if (!loading && r.main.empty()) r.main = L"—", r.detail = t(StringId::SysUnknown), r.unknown = true;
        return r;
    };
    const bool sys_loading = vm.system_loading;
    const SysCell cpu = sys_cell_split(vm.cpu), gpu = sys_cell_split(vm.gpu), ram = sys_cell_split(vm.ram), display = sys_cell_split(vm.display),
                  board = sys_cell_split(vm.board), os = sys_os_split(vm.os);
    const Row rows[] = {
        make(StringId::SysCellCpu, widen_y(cpu.main), widen_y(cpu.detail), sys_loading),
        make(StringId::SysCellGpu, widen_y(gpu.main), widen_y(gpu.detail), sys_loading),
        make(StringId::SysCellRam, widen_y(ram.main), widen_y(ram.detail), sys_loading),
        make(StringId::SysCellStorage, sto_main, sto_detail, vm.storage_loading),
        make(StringId::SysCellDisplay, widen_y(display.main), widen_y(display.detail), sys_loading),
        make(StringId::SystoolsInvBoard, widen_y(board.main), widen_y(board.detail), sys_loading),
        make(StringId::SysCellOs, widen_y(os.main), os.detail.empty() ? std::wstring() : rep_y(t(StringId::SysBuild), L"{v}", widen_y(os.detail)), sys_loading),
    };
    // Baslik "Sistem bilgisi" (sekme adi "Sistem" ile karismaz). Ozet yalniz okunurken; izgaradaki bir degeri (Windows
    // surumu vb.) baslikta tekrar etmez.
    const std::wstring summary = sys_loading ? t(StringId::OverviewLoading) : std::wstring();
    section_head_v2(g, area, L"04", true, t(StringId::SystoolsInfoTitle), summary, p.text_secondary, 0.f, hits, seq, {}, nullptr, false);
    // BOOST maketi: etiket sutunu (12px/600 ikincil) | deger (13px/600) | sagda mono 12px ayrinti; satirlar 6% cizgiyle
    // ayrilir (py 10). Kart ici ust bosluk yok (satirlar kartin kenarina dayanir).
    constexpr float kLabelColW = 136.f, kRowPadY = 10.f, kColGap = 12.f;
    const float lab_col = std::min(kLabelColW, inner_w * 0.32f);
    const float vrow_h = theme_.role(Role::Semi13).line + 2 * kRowPadY;
    const std::size_t nrows = std::size(rows);
    float y = area.y + 1.f;
    for (std::size_t i = 0; i < nrows; ++i) {
        const auto& r = rows[i];
        const bool visible = y + vrow_h >= s.view_top && y <= s.view_bottom;
        const auto label = t(r.label);
        std::wstring name = label + L": ";
        if (r.loading) name += t(StringId::OverviewLoading);
        else name += r.main + (r.detail.empty() ? L"" : L", " + r.detail);
        if (visible) {
            if (i > 0) g.fill({x, std::floor(y), inner_w, 1}, divider);
            g.text(Role::Semi12, fit_text(g, Role::Semi12, label, lab_col), {x, y, lab_col, vrow_h}, p.text_secondary, {Align::Leading, false, true});
            const float vx = x + lab_col + kColGap;
            const float vw = std::max(x + inner_w - vx, 1.f);
            if (r.loading) {
                // Iskelet: sabit, sakin gri cubuk (surekli parlama animasyonu yok).
                g.fill_round({vx, y + (vrow_h - 10) / 2, vw * 0.4f, 10}, 4, p.surface_raised);
            } else {
                const float mw = std::min(std::ceil(g.measure(Role::Semi13, r.main, kUnbounded, false).w) + 1.f, r.detail.empty() ? vw : vw * 0.6f);
                g.text(Role::Semi13, fit_text(g, Role::Semi13, r.main, mw), {vx, y, mw, vrow_h}, r.unknown ? p.text_secondary : p.text_primary,
                       {Align::Leading, false, true});
                const float dx = vx + mw + kColGap;
                const float dw = vx + vw - dx;
                if (!r.detail.empty() && dw > theme_.space(5))
                    g.text(Role::Mono12, fit_text(g, Role::Mono12, r.detail, dw), {dx, y, dw, vrow_h}, p.text_secondary, {Align::Trailing, false, true});
            }
        }
        hits.add_text(seq.take(), {x, y, inner_w, vrow_h}, std::move(name));
        y += vrow_h;
    }
    const float col_x = x, col_w = inner_w;
    if (!vm.storage_loading && !vm.storage.empty()) {
        y += theme_.space(2);
        // Depolama karti icerigi (DIAG-08): birim basina tek satir (+ oyun / HDD notu varsa ikinci satir); satirlar ince
        // cizgiyle ayrilir.
        for (const auto& a : vm.storage) {
            std::wstring head = root_text(a.root);
            if (const auto ty = type_text(a); !ty.empty()) head += L" · " + ty;
            else head += L" · " + t(StringId::StorageTypeUnknown);
            if (!a.disk_model.empty()) head += L" · " + widen_y(a.disk_model);
            std::wstring free, status, note;
            float used = -1;
            if (a.free_gib && a.total_gib && a.free_pct) {
                free = rep_y(rep_y(rep_y(t(StringId::StorageFree), L"{free}", std::to_wstring(*a.free_gib)), L"{total}", std::to_wstring(*a.total_gib)), L"{pct}",
                             std::to_wstring(static_cast<int>(std::lround(*a.free_pct))));
                used = static_cast<float>(1.0 - *a.free_pct / 100.0);
            }
            Color status_c = p.text_secondary;
            switch (a.status) {
                case ci::SpaceStatus::Ok: status = t(StringId::StorageOk), status_c = p.success; break;
                case ci::SpaceStatus::Warning: status = t(StringId::StorageWarning), status_c = p.warning; break;
                case ci::SpaceStatus::Critical: status = t(StringId::StorageCritical), status_c = p.error; break;
                case ci::SpaceStatus::Unknown: status = t(StringId::StorageUnknown); break;
            }
            if (a.games > 0 && a.recommended_free_gib)
                note = rep_y(rep_y(rep_y(rep_y(t(StringId::StorageGames), L"{n}", std::to_wstring(a.games)), L"{name}", widen_y(a.largest_game)), L"{size}",
                                   std::to_wstring(a.largest_game_gib)),
                             L"{rec}", std::to_wstring(*a.recommended_free_gib));
            const std::wstring hdd = a.games_on_hdd ? t(StringId::StorageHdd) : std::wstring();
            const bool two = !note.empty() || !hdd.empty();
            const float vh = two ? theme_.row2_h : row_h;
            if (y + vh >= s.view_top && y <= s.view_bottom) {
                g.fill({col_x, std::floor(y), col_w, 1}, divider);
                const float bar_w = std::min(120.f, col_w * 0.18f);
                const float free_w = std::min(std::ceil(g.measure(Role::Secondary, free, kUnbounded, false).w) + 1.f, col_w * 0.3f);
                const float stat_w = std::min(std::ceil(g.measure(Role::Secondary, status, kUnbounded, false).w) + 1.f, col_w * 0.22f);
                const float head_w = std::max(col_w - bar_w - free_w - stat_w - 3 * theme_.space(3), 1.f);
                const float ly = two ? y + (vh - 2 * sl) / 2 : y;
                const float lh = two ? sl : row_h;
                g.text(Role::Secondary, fit_text(g, Role::Secondary, head, head_w), {col_x, ly, head_w, lh}, p.text_primary, {Align::Leading, false, true});
                float cx = col_x + head_w + theme_.space(3);
                const RectF bar{cx, ly + (lh - 6) / 2, bar_w, 6};
                g.fill_round(bar, 3, p.surface_raised);
                if (hc) g.stroke_round(bar, 3, p.border);
                if (used >= 0) {
                    const float w = std::clamp(used, 0.f, 1.f) * bar.w;
                    if (w > 0.5f) g.fill_round({bar.x, bar.y, std::max(6.f, w), bar.h}, 3, a.status == ci::SpaceStatus::Ok ? p.accent : status_c);
                }
                cx += bar_w + theme_.space(3);
                if (!free.empty())
                    g.text(Role::Secondary, fit_text(g, Role::Secondary, free, free_w), {cx, ly, free_w, lh}, p.text_secondary, {Align::Leading, false, true});
                g.text(Role::Secondary, fit_text(g, Role::Secondary, status, stat_w), {x + inner_w - stat_w, ly, stat_w, lh},
                       ensure_contrast(status_c, p.surface, p.text_primary, 4.5), {Align::Trailing, false, true});
                if (two) {
                    std::wstring second = note;
                    if (!hdd.empty()) second = second.empty() ? hdd : second + L" · " + hdd;
                    g.text(Role::Secondary, fit_text(g, Role::Secondary, second, col_w), {col_x, ly + sl, col_w, sl},
                           hdd.empty() ? p.text_secondary : ensure_contrast(p.warning, p.surface, p.text_primary, 4.5), {Align::Leading, false, true});
                }
            }
            std::wstring uia = a11y_join(a11y_join(a11y_join(head, free), status), note);
            uia = a11y_join(std::move(uia), hdd);
            const Hit vid = seq.take();
            hits.add_text(vid, {col_x, y, col_w, vh}, std::move(uia));
            // P12-B2b: gorunen satirda yalniz olgu; oneri cumleleri (bos alan onerisi, HDD -> SSD) HelpText'te okunur.
            if (!note.empty() || !hdd.empty()) {
                std::wstring help;
                if (!note.empty() && a.recommended_free_gib) help = rep_y(tk("storage.rec_tip"), L"{rec}", std::to_wstring(*a.recommended_free_gib));
                if (!hdd.empty()) help = a11y_join(std::move(help), tk("storage.hdd_tip"));
                if (!note.empty()) help = a11y_join(std::move(help), t(StringId::StorageRule));
                hits.meta[vid].help = std::move(help);
            }
            y += vh;
        }
    }
    return y + 1.f - area.y;
}

// Gate P3b: Oyun hazirligi sayfasinda katlanir bolum, varsayilan kapali. Baslik satiri "Sistem kontrolu · N bulgu" ve en
// agir bulgunun renginde nokta (uyari > bilgi); sag uctaki ac/kapa dugmesi Hit::ChkCheckup (UIA Selected = acik).
// Spec 2026-11-14 B11: baslik altinda grup kutucuklari (Hit::ChkGroup0 + grup); acik grubun her bulgusu tek satirdir
// (sigmayan "…"; tam cumle satirin UIA metninde). Ac/kapa dugmesi tum gruplari acar. Kapaliyken bulgularin tamami dugmenin
// HelpText'inde okunur (bilgi kaybolmaz). Bulgu yok / okunuyor durumunda sayi uretilmez.
void Screen::findings_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    const auto& p = pal();
    const float pad = theme_.card_pad;
    const float dot = 8;
    const float head_h = theme_.button_row_h;
    const float row_h = theme_.row_h;
    const float label_h = theme_.role(Role::Label).line;
    const bool hc = pal_ != &theme_.dark;
    struct Line {
        std::wstring text;
        bool warning = false;
        FindingGroup group = FindingGroup::Other;
        bool xmp = false;  // T20: "health.xmp_off" bulgusu; altinda salt okunur BIOS rehberi
    };
    std::vector<Line> lines;
    if (!vm.findings_loading)
        for (const auto& f : vm.findings) {
            auto w = tk(f.text_key);
            if (!w.empty()) lines.push_back({std::move(w), f.warning, finding_group(f.text_key), f.text_key == "health.xmp_off"});
        }
    const bool any_warning = std::any_of(lines.begin(), lines.end(), [](const Line& l) { return l.warning; });
    std::wstring count;
    if (vm.findings_loading) {
        count = t(StringId::ReadinessCheckupLoading);
    } else if (lines.empty()) {
        count = t(StringId::ReadinessCheckupNone);
    } else if (lines.size() == 1) {
        count = t(StringId::ReadinessCheckupCountOne);
    } else {
        count = t(StringId::ReadinessCheckupCount);
        if (const auto at = count.find(L"{n}"); at != std::wstring::npos) count.replace(at, 3, std::to_wstring(lines.size()));
    }
    // Spec 2026-11-14 B11: bulgular gercek kaynaklarina gore gruplanir; kapali bolumde grup kutucuklari (ad, sayi, en
    // agir ton) gorunur, kutucuga basinca o grubun tek satirli bulgulari altinda acilir. Bolumun ac/kapa dugmesi tum
    // gruplari birlikte acar.
    struct Grp {
        FindingGroup id = FindingGroup::Other;
        int n = 0;
        bool warn = false, open = false;
        std::wstring name, count;
    };
    static constexpr StringId kGroupName[kFindingGroupCount] = {StringId::ReadinessGroupPower,    StringId::ReadinessGroupSecurity,
                                                                StringId::ReadinessGroupHardware, StringId::ReadinessGroupStorage,
                                                                StringId::ReadinessGroupUpdates,  StringId::ReadinessGroupOther};
    const bool all_open = s.readiness_checkup_open && !lines.empty();
    std::vector<Grp> groups;
    for (int gi = 0; gi < kFindingGroupCount; ++gi) {
        Grp gr;
        gr.id = static_cast<FindingGroup>(gi);
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i].group == gr.id) ++gr.n, gr.warn = gr.warn || lines[i].warning;
        if (gr.n == 0) continue;
        gr.open = all_open || ((s.readiness_group_open >> gi) & 1u) != 0;
        gr.name = t(kGroupName[gi]);
        if (gr.n == 1) {
            gr.count = t(StringId::ReadinessCheckupCountOne);
        } else {
            gr.count = t(StringId::ReadinessCheckupCount);
            if (const auto at = gr.count.find(L"{n}"); at != std::wstring::npos) gr.count.replace(at, 3, std::to_wstring(gr.n));
        }
        groups.push_back(std::move(gr));
    }
    s.readiness_groups.clear();
    for (const auto& gr : groups) s.readiness_groups.push_back(gr.id);
    const float inner_w = std::max(slot.area.w - 2 * pad, 1.f);
    constexpr float kGroupTileMinW = 136.f;  // grup kutucugu asgari genisligi (ad + sayi)
    const float tile_gap = theme_.space(1);
    const std::size_t cols = groups.empty() ? 1
                             : std::clamp<std::size_t>(static_cast<std::size_t>((inner_w + tile_gap) / (kGroupTileMinW + tile_gap)), 1, groups.size());
    const std::size_t grid_rows = (groups.size() + cols - 1) / cols;
    const float grid_h = groups.empty() ? 0.f : static_cast<float>(grid_rows) * row_h + static_cast<float>(grid_rows - 1) * tile_gap;
    const std::size_t open_groups = static_cast<std::size_t>(std::count_if(groups.begin(), groups.end(), [](const Grp& gr) { return gr.open; }));
    // T20: XMP bulgusu acik grupta gorunuyorsa altinda "Adim adim" satiri; acikken uretici bazli, yalniz okunur adimlar
    // (uygulama firmware'e hicbir sey yazmaz). Rehber saf cekirdek islevinden gelir; SMBIOS uretici metni yoksa genel.
    const float guide_text_w = std::max(inner_w - dot - theme_.space(2), 1.f);
    std::vector<std::wstring> guide_txt;
    std::vector<float> guide_h;
    bool guide_row = false;
    float guide_extra = 0.f;
    for (const auto& gr : groups)
        if (gr.open)
            for (const auto& l : lines)
                if (l.group == gr.id && l.xmp) guide_row = true;
    s.bios_guide_shown = guide_row;
    if (guide_row) {
        guide_extra = theme_.button_row_h;
        if (s.bios_guide_open) {
            const auto bg = core::guided::bios_guide_for(vm.updates.board_vendor, vm.updates.cpu_vendor);
            const auto profile = tk(bg.profile_name_key);
            auto head = tk("bios.guide.header");
            head = rep_y(std::move(head), L"{vendor}", tk(core::guided::bios_vendor_name_key(bg)));
            head = rep_y(std::move(head), L"{enter}", tk(bg.enter_key_text_key));
            head = rep_y(std::move(head), L"{profile}", profile);
            guide_txt.push_back(std::move(head));
            std::size_t n = 0;
            for (const auto& k : bg.step_keys)
                guide_txt.push_back(std::to_wstring(++n) + L". " + rep_y(tk(k), L"{profile}", profile));
            guide_txt.push_back(tk("bios.guide.readonly"));
            for (const auto& tx : guide_txt) {
                guide_h.push_back(g.measure(Role::Secondary, tx, guide_text_w).h);
                guide_extra += theme_.space(0) + guide_h.back();
            }
        }
    }
    float list_h = 0;
    for (const auto& gr : groups)
        if (gr.open) {
            list_h += (list_h > 0 ? theme_.space(1) : 0.f) + (open_groups > 1 ? label_h + theme_.space(0) : 0.f) + static_cast<float>(gr.n) * row_h;
            for (const auto& l : lines)
                if (l.group == gr.id && l.xmp) list_h += guide_extra;
        }
    const float body_h = (groups.empty() ? 0.f : theme_.space(2) + grid_h) + (list_h > 0 ? theme_.space(2) + list_h : 0.f);
    RectF card;
    if (!slot.place(pad + head_h + body_h + pad, card)) return;
    const bool card_visible = card.bottom() >= s.view_top && card.y <= s.view_bottom;
    if (card_visible) draw_card(g, card);
    else log_card(card);
    // Baslik satiri: nokta + "Sistem kontrolu · N bulgu" ve sag uçta ortak ac/kapa dugmesi (expander; History / Temizlik
    // satirlariyla ayni). Dugmenin UIA adi baslik metnidir.
    const Color head_dot = vm.findings_loading ? p.text_secondary : lines.empty() ? p.success : any_warning ? p.warning : p.accent;
    const std::wstring title = t(StringId::OverviewCheckup) + L" · " + count;
    const float hx = card.x + pad;
    const float hy = card.y + pad;
    const float exp_w = head_h;
    const float tx = hx + dot + theme_.space(2);
    const float tw = std::max(card.right() - pad - exp_w - theme_.space(2) - tx, 1.f);
    if (card_visible) {
        g.fill_circle(hx + dot / 2, hy + head_h / 2, dot / 2, head_dot);
        g.text(Role::Section, fit_text(g, Role::Section, title, tw), {tx, hy, tw, head_h}, p.text_primary, {Align::Leading, false, true});
    }
    expander(g, {card.right() - pad - exp_w, hy, exp_w, exp_w}, Hit::ChkCheckup, all_open, title, s, now, hits, card_visible);
    {
        std::wstring help = t(StringId::ReadinessCheckupToggle);
        if (!all_open)
            for (const auto& l : lines) help = a11y_join(std::move(help), l.text);
        hits.meta[Hit::ChkCheckup].help = std::move(help);
    }
    if (groups.empty()) return;
    // Grup kutucuklari: nokta (grubun en agir tonu: uyari > bilgi) · grup adi · sagda sayi. Acik grup accent tonlu zemin
    // + cerceve (UIA Selected = acik). Kapali grubun bulgulari kutucugun HelpText'inde okunur (bilgi kaybolmaz).
    const float gx = card.x + pad;
    float y = card.y + pad + head_h + theme_.space(2);
    {
        const auto widths = equal_widths(inner_w, cols, tile_gap);
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const auto& gr = groups[i];
            const std::size_t col = i % cols, row = i / cols;
            float tx0 = gx;
            for (std::size_t c = 0; c < col; ++c) tx0 += widths[c] + tile_gap;
            const RectF r{tx0, y + static_cast<float>(row) * (row_h + tile_gap), widths[col], row_h};
            const Hit id = hit_of(HitKind::ChkGroup, static_cast<std::size_t>(gr.id));
            if (card_visible && r.bottom() >= s.view_top && r.y <= s.view_bottom) {
                const float hv = hover_value(s, id, now);
                const float pv = press_value(s, id, now);
                const Color bg = gr.open ? mix(p.surface, p.accent, alpha_scale::kChipTint) : mix(p.surface, p.surface_raised, std::min(1.f, 0.5f + hv * 0.5f));
                const Color edge = gr.open ? (hc ? p.accent : mix(p.surface, p.accent, alpha_scale::kDangerCardBorder))
                                           : mix(p.border, p.accent, std::min(1.f, hv + pv));
                g.fill_round(r, theme_.radius_control, bg);
                g.stroke_round(r, theme_.radius_control, edge);
                const float ix = r.x + theme_.space(2);
                g.fill_circle(ix + dot / 2, r.y + row_h / 2, dot / 2, gr.warn ? p.warning : p.accent);
                const std::wstring n = std::to_wstring(gr.n);
                const float nw = std::ceil(g.measure(Role::Secondary, n, 1000.f, false, true).w) + 1.f;
                const float nx = r.right() - theme_.space(2) - nw;
                const float lx = ix + dot + theme_.space(1);
                const float lw = std::max(nx - theme_.space(1) - lx, 1.f);
                const Color fg = gr.open ? ensure_contrast(p.accent, bg, p.text_primary, 4.5) : p.text_primary;
                g.text(Role::Body, fit_text(g, Role::Body, gr.name, lw), {lx, r.y, lw, row_h}, fg, {Align::Leading, false, true});
                g.text(Role::Secondary, n, {nx, r.y, nw, row_h}, ensure_contrast(p.text_secondary, bg, p.text_primary, 4.5), {Align::Trailing, false, true, true});
                if (s.focus_visible && s.focus == id) focus_ring(g, r, theme_.radius_control, hits);
            }
            hits.add(id, r, gr.name + L" · " + gr.count, AccRole::Expander, gr.open);
            std::wstring help = t(StringId::ReadinessGroupToggle);
            if (!gr.open)
                for (const auto& l : lines)
                    if (l.group == gr.id) help = a11y_join(std::move(help), l.text);
            hits.meta[id].help = std::move(help);
        }
        y += grid_h;
    }
    if (list_h <= 0) return;
    // Acik gruplarin tek satirli bulgulari (sigmayan "…"; tam cumle satirin UIA metninde). Birden cok grup aciksa her
    // grubun adi ince etiketle ayrilir.
    y += theme_.space(2);
    const float text_x = gx + dot + theme_.space(2);
    const float text_w = std::max(card.right() - pad - text_x, 1.f);
    bool first = true;
    for (const auto& gr : groups) {
        if (!gr.open) continue;
        if (!first) y += theme_.space(1);
        first = false;
        if (open_groups > 1) {
            if (card_visible && y + label_h >= s.view_top && y <= s.view_bottom)
                g.text(Role::Label, fit_text(g, Role::Label, gr.name, inner_w), {gx, y, inner_w, label_h}, p.text_secondary, {Align::Leading, false, true});
            y += label_h + theme_.space(0);
        }
        for (const auto& l : lines) {
            if (l.group != gr.id) continue;
            const RectF lr{text_x, y, text_w, row_h};
            hits.add_text(seq.take(), lr, l.text);
            if (card_visible && lr.bottom() >= s.view_top && lr.y <= s.view_bottom) {
                g.fill_circle(gx + dot / 2, y + row_h / 2, dot / 2, l.warning ? p.warning : p.accent);
                g.text(Role::Body, fit_text(g, Role::Body, l.text, text_w), lr, p.text_primary, {Align::Leading, false, true});
            }
            y += row_h;
            if (l.xmp) {
                // T20: "Adim adim" ac/kapa (UIA Expander; yalniz gorunum durumu) ve acikken salt okunur adimlar.
                const bool open = s.bios_guide_open;
                const auto label = t(StringId::BiosGuideToggle) + (open ? L" ▴" : L" ▾");
                const float lw = std::min(std::ceil(g.measure(Role::Medium12, label, 10000.f, false).w) + 2 * theme_.space(1), text_w);
                const RectF br{text_x, y, lw, theme_.button_row_h};
                if (card_visible && br.bottom() >= s.view_top && br.y <= s.view_bottom) {
                    const float hv = hover_value(s, Hit::BiosGuideToggle, now);
                    const float pr = press_value(s, Hit::BiosGuideToggle, now);
                    g.fill_round(br, theme_.radius_control, mix(p.surface, p.text_primary, 0.05f + 0.05f * std::min(1.f, hv + pr)));
                    g.text(Role::Medium12, label, br, mix(p.text_primary, p.accent, std::min(1.f, hv + pr)), {Align::Center, false, true});
                    if (s.focus_visible && s.focus == Hit::BiosGuideToggle) focus_ring(g, br, theme_.radius_control, hits);
                }
                hits.add(Hit::BiosGuideToggle, br, label, AccRole::Expander, open);
                y += theme_.button_row_h;
                for (std::size_t gi = 0; gi < guide_txt.size(); ++gi) {
                    y += theme_.space(0);
                    const RectF tr{text_x, y, guide_text_w, guide_h[gi]};
                    if (card_visible && tr.bottom() >= s.view_top && tr.y <= s.view_bottom)
                        g.text(Role::Secondary, guide_txt[gi], tr, gi == 0 || gi + 1 == guide_txt.size() ? p.text_secondary : p.text_primary);
                    hits.add_text(seq.take(), tr, guide_txt[gi]);
                    y += guide_h[gi];
                }
            }
        }
    }
}

FindingGroup finding_group(std::string_view key) {
    // Guc / guvenlik notlari (power_security), donanim sagligi ve disk alani (inventory/health), guncelleme denetimi
    // (update_check); digerleri Other.
    for (std::string_view k : {"diag.on_battery", "diag.battery_saver", "diag.efficiency_overlay", "diag.power_saver_plan", "diag.battery_unknown"})
        if (key == k) return FindingGroup::Power;
    if (key == "diag.hvci_running" || key == "diag.vbs_running") return FindingGroup::Security;
    if (key == "health.low_disk") return FindingGroup::Storage;
    if (key.starts_with("health.")) return FindingGroup::Hardware;
    if (key.starts_with("update.")) return FindingGroup::Updates;
    return FindingGroup::Other;
}

}  // namespace ef::ui
