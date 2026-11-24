// OPT-07: Genel Bakis'ta tek tik sistem kontrolu karti. Baslat -> otomatik (onayla, geri alinabilir) ve
// rehberli (kullanicinin adimi) liste -> Evet/Hayir -> gercek adim sayisiyla ilerleme -> rapor.
// BIOS/surucu/donanim degisikligi yalniz rehberli metindir.
#include <string>
#include <vector>

#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
using core::perf::CheckPhase;
using core::perf::OutcomeKind;

namespace {
std::wstring fill(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
}  // namespace

void Screen::check_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const auto& cv = vm.check;
    const float pad = theme_.card_pad;
    const float inner_w = slot.area.w - 2 * pad;
    const float bl = theme_.role(Role::Body).line, sec = theme_.role(Role::Section).line;
    s.check_phase = cv.phase;
    s.check_toggles.assign(cv.rows.size(), false);

    struct Line {
        std::wstring text;
        Color c;
        Role role;
        float h;
    };
    std::vector<Line> head;  // durum/rapor satirlari
    auto add = [&](std::wstring t, Color c, Role r = Role::Body) {
        const float h = std::max(g.measure(r, t, inner_w).h, theme_.role(r).line);
        head.push_back({std::move(t), c, r, h});
    };
    switch (cv.phase) {
        case CheckPhase::Scanning: add(t(StringId::CheckScanning) + L"…", p.text_secondary); break;
        case CheckPhase::Applying:
        case CheckPhase::Measuring:
            add(fill(fill(t(StringId::CheckApplying), L"{done}", std::to_wstring(cv.done)), L"{total}", std::to_wstring(cv.total)), p.text_secondary);
            break;
        case CheckPhase::Report:
        case CheckPhase::Failed:
            if (cv.report) {
                const auto& r = *cv.report;
                if (r.outcome == OutcomeKind::Active) add(fill(t(StringId::CheckReportApplied), L"{count}", std::to_wstring(r.auto_applied)), p.success);
                else if (r.outcome == OutcomeKind::NothingToDo) add(t(StringId::CheckReportNone), p.text_secondary);
                else add(t(StringId::CheckReportFailed), p.warning);
                if (!r.guided.empty()) add(fill(t(StringId::CheckReportGuided), L"{count}", std::to_wstring(r.guided.size())), p.text_primary);
            }
            break;
        default: break;
    }
    // Review listesi
    const float tog_w = theme_.toggle_w, tog_h = theme_.toggle_h;
    const float text_w = inner_w - tog_w - theme_.space(3);
    std::vector<float> rh;
    float list_h = 0;
    bool any_auto = false, any_guided = false;
    if (cv.phase == CheckPhase::Review) {
        for (const auto& r : cv.rows) {
            const float h = std::max({g.measure(Role::Body, tk(r.text_key), r.automatic ? text_w : inner_w).h, bl, r.automatic ? tog_h : 0.f});
            rh.push_back(h);
            list_h += h + theme_.space(2);
            (r.automatic ? any_auto : any_guided) = true;
        }
        list_h += 2 * (sec + theme_.space(2));
        if (!any_auto) list_h += bl + theme_.space(2);
    }
    float head_h = 0;
    for (const auto& l : head) head_h += l.h + theme_.space(1);
    const bool show_start = cv.phase == CheckPhase::Idle || cv.phase == CheckPhase::Report || cv.phase == CheckPhase::Failed;
    const bool show_yesno = cv.phase == CheckPhase::Review;
    const float q_h = show_yesno ? g.measure(Role::Section, t(StringId::CheckQuestion), inner_w).h + theme_.space(2) : 0.f;
    RectF card;
    if (!slot.place(pad + head_h + list_h + q_h + ((show_start || show_yesno) ? theme_.button_h : 0.f) + pad, card)) return;
    draw_card(g, card);
    float y = card.y + pad;
    for (const auto& l : head) {
        g.text(l.role, l.text, {card.x + pad, y, inner_w, l.h}, l.c);
        y += l.h + theme_.space(1);
    }
    if (cv.phase == CheckPhase::Review) {
        for (int pass = 0; pass < 2; ++pass) {
            const bool automatic = pass == 0;
            g.text(Role::Section, t(automatic ? StringId::CheckAutoTitle : StringId::CheckGuidedTitle), {card.x + pad, y, inner_w, sec}, p.text_primary);
            y += sec + theme_.space(2);
            if (automatic && !any_auto) {
                g.text(Role::Body, t(StringId::CheckNoneAuto), {card.x + pad, y, inner_w, bl}, p.text_secondary);
                y += bl + theme_.space(2);
            }
            for (std::size_t i = 0; i < cv.rows.size(); ++i) {
                const auto& r = cv.rows[i];
                if (r.automatic != automatic) continue;
                if (automatic) {
                    const Hit id = hit_of(HitKind::CheckToggle, i);
                    small_toggle(g, {card.x + pad, y, tog_w, tog_h}, r.selected, true, id, tk(r.text_key), s, now, hits);
                    s.check_toggles[i] = true;
                    g.text(Role::Body, tk(r.text_key), {card.x + pad + tog_w + theme_.space(3), y, text_w, rh[i]}, p.text_primary);
                } else {
                    g.text(Role::Body, tk(r.text_key), {card.x + pad, y, inner_w, rh[i]}, p.text_secondary);
                }
                y += rh[i] + theme_.space(2);
            }
        }
        const auto q = t(StringId::CheckQuestion);
        const float qh = q_h - theme_.space(2);
        g.text(Role::Section, q, {card.x + pad, y, inner_w, qh}, p.text_primary);
        y += q_h;
        const float yw = vm.game_gate_blocked ? -theme_.space(2) : button(g, card.x + pad, y, Hit::CheckYes, t(StringId::ActionConfirm), true, s, now, hits);
        button(g, card.x + pad + yw + theme_.space(2), y, Hit::CheckNo, t(StringId::ActionCancel), false, s, now, hits);
    } else if (show_start) {
        // REL-06: Report/Failed sonrasi yeniden tarama gercekten mumkun (SystemCheck::start Report/Failed kabul
        // eder); dugme devre disi CIZILMEZ. Aksi halde ekran yalan soyluyordu (tiklanabilir ama gri).
        button(g, card.x + pad, y, Hit::CheckStart, t(StringId::CheckStart), true, s, now, hits);
    }
}

}  // namespace ef::ui
