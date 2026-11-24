// Spec 2026-11-12 E: uygulama ici kip penceresi. Ust yari: durum makinesi (saf; birim testte dogrulanir). Alt yari:
// cizim (Screen::modal_layer) ve kip ogesi <-> isabet kimligi eslemesi.
#include "ui/shell/modal.h"

#include <algorithm>
#include <cmath>

#include "ui/shell/screen.h"

namespace ef::ui {

namespace {

void finish(ModalState& m, ModalAnswer a, bool dismissed) {
    m.result.answer = a;
    m.result.dismissed = dismissed;
    m.result.checked = m.checked;
    m.result.choice = m.choice;
    m.result.text = m.text;
    m.done = true;
}

bool input_char_ok(const ModalSpec& sp, wchar_t c) {
    if (c < 0x20 || c == 0x7f) return false;
    return sp.input_allowed.empty() || sp.input_allowed.find(c) != std::wstring::npos;
}

// Fazla satirlar son izinli satira eklenir (metin kaybolmaz).
void fold_tail(std::vector<std::wstring>& v, std::size_t max, std::wstring_view sep) {
    std::erase_if(v, [](const std::wstring& s) { return s.empty(); });
    if (v.size() <= max || max == 0) return;
    for (std::size_t i = max; i < v.size(); ++i) {
        v[max - 1] += sep;
        v[max - 1] += v[i];
    }
    v.resize(max);
}

bool same_stop(const ModalFocus& a, const ModalFocus& b) {
    return a.item == b.item;  // secim listesi tek duraktir (indeks gezinir)
}

}  // namespace

ModalState modal_open(ModalSpec spec) {
    fold_tail(spec.body, kModalMaxBody, L" ");
    fold_tail(spec.list, kModalMaxList, L" · ");
    if (spec.choices.size() > kModalMaxChoices) spec.choices.resize(kModalMaxChoices);
    if (spec.countdown_s < 0) spec.countdown_s = 0;
    ModalState m;
    m.checked = !spec.checkbox.empty() && spec.checkbox_checked;
    if (spec.initial_choice >= 0 && spec.initial_choice < static_cast<int>(spec.choices.size())) m.choice = spec.initial_choice;
    m.seconds_left = spec.countdown_s;
    if (!spec.input_label.empty()) {
        if (spec.input_max == 0) spec.input_max = 64;
        for (const wchar_t c : spec.input_text)
            if (input_char_ok(spec, c) && m.text.size() < spec.input_max) m.text.push_back(c);
    }
    const bool has_input = !spec.input_label.empty();
    m.spec = std::move(spec);
    m.focus = {has_input ? ModalItem::Input : ModalItem::No, 0};  // yazi alaninda ilk odak: yazmaya hemen baslanir
    m.result = modal_not_shown();
    return m;
}

bool modal_has_input(const ModalState& m) { return !m.spec.input_label.empty(); }

bool modal_yes_enabled(const ModalState& m) {
    if (modal_has_input(m)) {
        const bool ok = m.spec.input_valid ? m.spec.input_valid(m.text) : !m.text.empty();
        if (!ok) return false;
    }
    return m.spec.choices.empty() || m.choice >= 0;
}

bool modal_input_char(ModalState& m, wchar_t c) {
    if (m.done || !modal_has_input(m) || !input_char_ok(m.spec, c) || m.text.size() >= m.spec.input_max) return false;
    m.text.push_back(c);
    return true;
}

bool modal_input_backspace(ModalState& m) {
    if (m.done || !modal_has_input(m) || m.text.empty()) return false;
    m.text.pop_back();
    return true;
}

bool modal_input_clear(ModalState& m) {
    if (m.done || !modal_has_input(m) || m.text.empty()) return false;
    m.text.clear();
    return true;
}

bool modal_input_paste(ModalState& m, const std::wstring& text) {
    bool any = false;
    for (const wchar_t c : text) any = modal_input_char(m, c) || any;
    return any;
}

std::vector<ModalFocus> modal_focus_order(const ModalState& m) {
    std::vector<ModalFocus> v;
    if (modal_has_input(m)) v.push_back({ModalItem::Input, 0});
    if (!m.spec.choices.empty()) v.push_back({ModalItem::Choice, m.choice >= 0 ? static_cast<std::size_t>(m.choice) : 0});
    if (!m.spec.checkbox.empty()) v.push_back({ModalItem::Check, 0});
    v.push_back({ModalItem::No, 0});
    if (modal_yes_enabled(m)) v.push_back({ModalItem::Yes, 0});
    return v;
}

void modal_activate(ModalState& m, ModalFocus item) {
    if (m.done) return;
    switch (item.item) {
        case ModalItem::None: return;
        case ModalItem::Input:
            if (!modal_has_input(m)) return;
            m.focus = {ModalItem::Input, 0};  // yalniz odak; hicbir sey calismaz
            return;
        case ModalItem::Choice:
            if (item.index >= m.spec.choices.size()) return;
            m.choice = static_cast<int>(item.index);
            m.focus = item;
            return;
        case ModalItem::Check:
            if (m.spec.checkbox.empty()) return;
            m.checked = !m.checked;
            m.focus = {ModalItem::Check, 0};
            return;
        case ModalItem::No:
            m.focus = {ModalItem::No, 0};
            finish(m, ModalAnswer::No, false);
            return;
        case ModalItem::Yes:
            if (!modal_yes_enabled(m)) return;
            m.focus = {ModalItem::Yes, 0};
            finish(m, ModalAnswer::Yes, false);
            return;
    }
}

void modal_key(ModalState& m, ModalKey k) {
    if (m.done) return;
    switch (k) {
        case ModalKey::Escape: finish(m, ModalAnswer::No, true); return;
        case ModalKey::Enter:
        case ModalKey::Space: modal_activate(m, m.focus); return;
        case ModalKey::Tab:
        case ModalKey::ShiftTab: {
            const auto order = modal_focus_order(m);
            const auto n = static_cast<std::ptrdiff_t>(order.size());
            const auto it = std::find_if(order.begin(), order.end(), [&](const ModalFocus& f) { return same_stop(f, m.focus); });
            // Odak sirada degilse (ornek: devre disi kalan Evet) ileri = ilk durak, geri = son durak.
            if (it == order.end()) {
                m.focus = k == ModalKey::Tab ? order.front() : order.back();
                return;
            }
            const auto i = ((it - order.begin()) + (k == ModalKey::Tab ? 1 : -1) + n) % n;
            m.focus = order[static_cast<std::size_t>(i)];
            return;
        }
        case ModalKey::Up:
        case ModalKey::Left:
        case ModalKey::Down:
        case ModalKey::Right: {
            const bool fwd = k == ModalKey::Down || k == ModalKey::Right;
            if (m.focus.item == ModalItem::Choice && !m.spec.choices.empty()) {
                // Roving radyo: ok tusu gezer ve secer (uclarda durur).
                const std::size_t last = m.spec.choices.size() - 1;
                std::size_t i = std::min(m.focus.index, last);
                if (fwd && i < last) ++i;
                else if (!fwd && i > 0) --i;
                m.focus = {ModalItem::Choice, i};
                m.choice = static_cast<int>(i);
            } else if (k == ModalKey::Left || k == ModalKey::Right) {
                // Dugme satiri: Sol = Hayir, Sag = Evet (etkinse). Yalniz odak tasinir; hicbir sey calismaz.
                if (m.focus.item == ModalItem::Yes && !fwd) m.focus = {ModalItem::No, 0};
                else if (m.focus.item == ModalItem::No && fwd && modal_yes_enabled(m)) m.focus = {ModalItem::Yes, 0};
            }
            return;
        }
    }
}

void modal_tick(ModalState& m) {
    if (m.done || m.spec.countdown_s <= 0) return;
    if (m.seconds_left > 0) --m.seconds_left;
    if (m.seconds_left <= 0) finish(m, ModalAnswer::No, true);
}

void modal_dismiss(ModalState& m) {
    if (!m.done) finish(m, ModalAnswer::No, true);
}

std::wstring modal_body_text(const ModalState& m, std::size_t line) {
    if (line >= m.spec.body.size()) return {};
    std::wstring s = m.spec.body[line];
    if (m.spec.countdown_s > 0)
        if (const auto p = s.find(L"{s}"); p != std::wstring::npos) s.replace(p, 3, std::to_wstring(m.seconds_left));
    return s;
}

// ---- Isabet kimligi eslemesi ----

Hit modal_hit(const ModalFocus& f) {
    switch (f.item) {
        case ModalItem::None: return Hit::None;
        case ModalItem::Input: return Hit::ModalInput;
        case ModalItem::Choice: return hit_of(HitKind::ModalChoice, f.index);
        case ModalItem::Check: return Hit::ModalCheck;
        case ModalItem::No: return Hit::ModalNo;
        case ModalItem::Yes: return Hit::ModalYes;
    }
    return Hit::None;
}

ModalFocus modal_focus_of(Hit h) {
    if (h == Hit::ModalNo) return {ModalItem::No, 0};
    if (h == Hit::ModalYes) return {ModalItem::Yes, 0};
    if (h == Hit::ModalCheck) return {ModalItem::Check, 0};
    if (h == Hit::ModalInput) return {ModalItem::Input, 0};
    if (const HitSlot slot = classify_hit(h); slot.kind == HitKind::ModalChoice) return {ModalItem::Choice, slot.index};
    return {};
}

// ---- Cizim ----

void Screen::open_modal_motion(UiState& s, std::int64_t now) const {
    s.modal_fade = Tween(0);
    // Spec F: animasyonlar kapaliyken ve hareketin durdugu kosullarda (oyun acik, test / olcum suruyor, pilde) gecis
    // yoktur; kart aninda yerindedir (animasyon zamanlayicisi istenmez).
    s.modal_fade.retarget(1, now, effective_ms(theme_.motion.dialog_open, s.reduced_motion || s.motion_paused));
}

void Screen::modal_layer(Graphics& g, float w, float h, UiState& s, std::int64_t now, HitMap& hits) {
    const ModalState& m = *s.modal;
    const ModalSpec& sp = m.spec;
    const auto& p = pal();
    // Kart olculeri (DIP). Genislik dar pencerede kenar payiyla sinirlanir.
    // Secenekli pencerede (DNS hazir listeleri: IPv4 + IPv6 adresleri) kart genis: satir sarmasi azalir, kart pencereye sigar.
    const float kCardW = sp.choices.empty() ? 480.f : 640.f;
    constexpr float kRise = 8.f;
    constexpr float kBulletR = 2.f, kBulletCol = 16.f;  // madde isareti ve metin girintisi
    constexpr float kRadioD = 16.f, kRadioDot = 4.f, kCheckBox = 18.f, kCheckRadius = 4.f, kMarkGap = 12.f;
    const float margin = theme_.space(5), pad = theme_.space(4);
    const float block_gap = theme_.space(2), line_gap = theme_.space(0), title_gap = theme_.space(1), button_gap = theme_.space(4);
    const float card_w = std::max(std::min(kCardW, w - 2 * margin), 1.f);
    const float inner_w = std::max(card_w - 2 * pad, 1.f);
    const float body_line = theme_.role(Role::Body).line;
    const float row_h = std::max(theme_.button_row_h, theme_.touch_min);

    // Sayfa ogeleri kip acikken ne fareyle ne klavyeyle ne de UIA ile erisilir: yalniz kip ogeleri kalir.
    hits.items.clear();
    hits.meta.clear();
    hits.focus_drawn = false;

    // ---- Olcum ----
    const float title_h = g.measure(Role::PageTitle, sp.title, inner_w).h;
    std::vector<std::wstring> body;
    std::vector<float> body_h;
    for (std::size_t i = 0; i < sp.body.size(); ++i) {
        body.push_back(modal_body_text(m, i));
        body_h.push_back(g.measure(Role::Body, body.back(), inner_w).h);
    }
    std::vector<float> list_h;
    for (const auto& item : sp.list) list_h.push_back(g.measure(Role::Body, item, inner_w - kBulletCol).h);
    // Secenek / isaret kutusu metni uzun olabilir (DNS adresleri): sarilir, satir boyu metne gore buyur. Isaret (radyo /
    // kutu) ilk metin satiriyla ayni hizadadir.
    const float mark_pad = (row_h - body_line) / 2;  // ilk satirin satir kutusundaki dikey payi
    const float choice_text_w = std::max(inner_w - kRadioD - kMarkGap, 1.f);
    std::vector<float> choice_h;
    for (const auto& c : sp.choices) choice_h.push_back(std::max(g.measure(Role::Body, c, choice_text_w).h + 2 * mark_pad, row_h));
    const float check_text_w = sp.checkbox.empty() ? 0.f : std::max(inner_w - kCheckBox - kMarkGap, 1.f);
    const float check_h = sp.checkbox.empty() ? 0.f : std::max(g.measure(Role::Body, sp.checkbox, check_text_w).h + 2 * mark_pad, row_h);
    // Isabet kutusu: isaret + metin; tek bir denetim kenar cubugundan genis / control_max_h'den yuksek olmaz (uzun
    // metnin kalani salt okunur kalir). Odak halkasi isaretin dokunma kutusunu cevreler.
    const auto mark_hit = [&](float x0, float y0, float mark_w, float text_w, float full_h) {
        return RectF{x0, y0, std::min(mark_w + kMarkGap + text_w, theme_.sidebar_max), std::min(full_h, theme_.control_max_h)};
    };
    const auto mark_focus = [&](float x0, float y0, float mark_w) {
        const float side = std::max(theme_.touch_min, mark_w);
        return RectF{x0 + mark_w / 2 - side / 2, y0 + row_h / 2 - side / 2, side, side};
    };
    const bool has_input = !sp.input_label.empty();
    const float input_label_h = has_input ? g.measure(Role::Body, sp.input_label, inner_w).h : 0.f;
    float content_h = title_h;
    if (!body.empty()) {
        content_h += title_gap;
        for (std::size_t i = 0; i < body.size(); ++i) content_h += body_h[i] + (i ? line_gap : 0.f);
    }
    if (!sp.list.empty()) {
        content_h += block_gap;
        for (std::size_t i = 0; i < list_h.size(); ++i) content_h += list_h[i] + (i ? line_gap : 0.f);
    }
    if (has_input) content_h += block_gap + input_label_h + line_gap + row_h;
    if (!sp.choices.empty()) {
        content_h += block_gap;
        for (const float ch : choice_h) content_h += ch;
    }
    if (!sp.checkbox.empty()) content_h += block_gap + check_h;
    content_h += button_gap + theme_.button_h;
    const float card_h = pad + content_h + pad;

    // ---- Karartma + kart (acilis gecisi: belirir ve hafifce yukselir; azaltilmis harekette aninda) ----
    const float fade = s.modal_fade.value(now, theme_.easing);
    g.fill({0, 0, w, h}, p.background.with_alpha(alpha_scale::kModalScrim * fade));
    const RectF card{(w - card_w) / 2, std::max(margin, (h - card_h) / 2) + (1.f - fade) * kRise, card_w, card_h};
    g.set_alpha(fade);
    g.fill_round(card, theme_.radius_dialog, p.surface);
    g.stroke_round(card, theme_.radius_dialog, p.border);

    // Pencerenin kendisi (UIA Window + IsModal): ad = baslik; HelpText = gorunmeyen pencere basligi + govde.
    std::wstring help = sp.caption;
    for (const auto& line : body) help = a11y_join(std::move(help), line);
    hits.add(Hit::Modal, card, sp.title, AccRole::Window);
    hits.meta[Hit::Modal].help = std::move(help);

    const float x = card.x + pad;
    float y = card.y + pad;
    g.text(Role::PageTitle, sp.title, {x, y, inner_w, title_h}, p.text_primary);
    hits.add_text(Hit::ModalTitle, {x, y, inner_w, title_h}, sp.title);
    y += title_h;
    if (!body.empty()) {
        y += title_gap;
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (i) y += line_gap;
            const RectF r{x, y, inner_w, body_h[i]};
            g.text(Role::Body, body[i], r, p.text_secondary);
            hits.add_text(i == 0 ? Hit::ModalBody0 : Hit::ModalBody1, r, body[i]);
            y += body_h[i];
        }
    }
    if (!sp.list.empty()) {
        y += block_gap;
        for (std::size_t i = 0; i < sp.list.size(); ++i) {
            if (i) y += line_gap;
            g.fill_circle(x + kBulletCol / 2, y + body_line / 2, kBulletR, p.text_secondary);
            const RectF r{x + kBulletCol, y, inner_w - kBulletCol, list_h[i]};
            g.text(Role::Body, sp.list[i], r, p.text_primary);
            hits.add_text(hit_of(HitKind::ModalList, i), {x, y, inner_w, list_h[i]}, sp.list[i]);
            y += list_h[i];
        }
    }
    if (has_input) {  // R-C4: tek satir yazi alani (etiket + kutu + sondan imlec); UIA Edit, deger = metin
        y += block_gap;
        g.text(Role::Body, sp.input_label, {x, y, inner_w, input_label_h}, p.text_secondary);
        y += input_label_h + line_gap;
        const RectF box{x, y, std::min(inner_w, theme_.sidebar_max), row_h};  // tek denetim kenar cubugundan genis olmaz
        const bool focused = s.focus == Hit::ModalInput;
        const float hv = hover_value(s, Hit::ModalInput, now);
        g.fill_round(box, theme_.radius_control, p.background);
        g.stroke_round(box, theme_.radius_control, focused ? p.accent : mix(p.border, p.text_primary, hv), focused ? 2.f : 1.f);
        constexpr float kFieldPad = 10.f;
        const float tx = box.x + kFieldPad, tw = std::max(box.w - 2 * kFieldPad, 1.f);
        const RectF trect{tx, box.y + (row_h - body_line) / 2, tw, body_line};
        float caret_x = tx;
        if (m.text.empty()) {
            if (!sp.input_hint.empty()) g.text(Role::Body, sp.input_hint, trect, p.text_secondary);
        } else {
            g.text(Role::Body, m.text, trect, p.text_primary);
            caret_x += std::ceil(g.measure(Role::Body, m.text, 10000.f, false).w) + 1.f;
        }
        if (focused) g.fill({std::min(caret_x, tx + tw - 2.f), trect.y, 1.5f, body_line}, p.text_primary);
        hits.add(Hit::ModalInput, box, sp.input_label, AccRole::Edit);
        hits.meta[Hit::ModalInput].value = m.text;
        if (s.focus_visible && focused) focus_ring(g, box, theme_.radius_control, hits);
        y += row_h;
    }
    if (!sp.choices.empty()) {
        y += block_gap;
        const std::size_t roving = m.choice >= 0 ? static_cast<std::size_t>(m.choice) : 0;
        for (std::size_t i = 0; i < sp.choices.size(); ++i) {
            const Hit id = hit_of(HitKind::ModalChoice, i);
            const bool on = m.choice == static_cast<int>(i);
            const float ch = choice_h[i];
            const float text_w = std::min(std::ceil(g.measure(Role::Body, sp.choices[i], choice_text_w).w), choice_text_w);
            const RectF r = mark_hit(x, y, kRadioD, text_w, ch);
            const float cx = x + kRadioD / 2, cy = y + row_h / 2;
            const float hv = hover_value(s, id, now);
            const Color ring = on ? p.accent : mix(p.border, p.text_primary, hv);
            g.fill_circle(cx, cy, kRadioD / 2, ring);
            g.fill_circle(cx, cy, kRadioD / 2 - 1.5f, p.surface);
            if (on) g.fill_circle(cx, cy, kRadioDot, p.accent);
            g.text(Role::Body, sp.choices[i], {x + kRadioD + kMarkGap, y + mark_pad, text_w, ch - 2 * mark_pad}, p.text_primary);
            hits.add(id, r, sp.choices[i], AccRole::RadioButton, on);
            hits.meta[id].tab_stop = i == roving;  // roving: tek Tab duragi, ok tuslariyla gezilir
            if (s.focus_visible && s.focus == id) focus_ring(g, mark_focus(x, y, kRadioD), theme_.touch_min / 2, hits);
            y += ch;
        }
    }
    if (!sp.checkbox.empty()) {
        y += block_gap;
        const float text_w = std::min(std::ceil(g.measure(Role::Body, sp.checkbox, check_text_w).w), check_text_w);
        const RectF r = mark_hit(x, y, kCheckBox, text_w, check_h);
        const RectF box{x, y + (row_h - kCheckBox) / 2, kCheckBox, kCheckBox};
        const float hv = hover_value(s, Hit::ModalCheck, now);
        if (m.checked) {
            g.fill_round(box, kCheckRadius, p.accent);
            const Point mark[] = {{box.x + 4.f, box.y + 9.5f}, {box.x + 7.5f, box.y + 13.f}, {box.x + 14.f, box.y + 5.5f}};
            g.polyline(mark, 2.f, p.on_accent);
        } else {
            g.fill_round(box, kCheckRadius, p.surface);
            g.stroke_round(box, kCheckRadius, mix(p.border, p.text_primary, hv), 1.5f);
        }
        g.text(Role::Body, sp.checkbox, {x + kCheckBox + kMarkGap, y + mark_pad, text_w, check_h - 2 * mark_pad}, p.text_primary);
        hits.add(Hit::ModalCheck, r, sp.checkbox, AccRole::CheckBox, false, m.checked ? 1 : 0);
        if (s.focus_visible && s.focus == Hit::ModalCheck) focus_ring(g, mark_focus(x, y, kCheckBox), theme_.radius_control, hits);
        y += check_h;
    }
    // Dugmeler: sag altta ikincil "Hayir", ardindan ana "Evet" (es genislik; yikici islemde hata renginde).
    y += button_gap;
    const float gap = theme_.space(1);
    const float bw = std::min(button_group_w(g, {sp.no, sp.yes}), (inner_w - gap) / 2);
    const bool yes_on = modal_yes_enabled(m);
    const RectF yes_r{card.right() - pad - bw, y, bw, theme_.button_h};
    const RectF no_r{yes_r.x - gap - bw, y, bw, theme_.button_h};
    button_face(g, no_r, Hit::ModalNo, sp.no, ButtonKind::Secondary, true, s, now, hits);
    hits.add(Hit::ModalNo, no_r, sp.no, AccRole::Button);
    button_face(g, yes_r, Hit::ModalYes, sp.yes, sp.destructive ? ButtonKind::DangerPrimary : ButtonKind::Primary, yes_on, s, now, hits);
    hits.add(Hit::ModalYes, yes_r, sp.yes, AccRole::Button, false, -1, yes_on);
    g.set_alpha(1);
}

}  // namespace ef::ui
