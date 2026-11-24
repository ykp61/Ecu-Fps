// ADR-033 / spec 3: kurulum penceresi cizimi. Metinler katalogdan, olculer design/tokens.json setup.* ve ortak
// token'lardan; renkler paletten. Isabet / UIA kimlikleri screen.h (Setup*).
#include "ui/setup/setup_view.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "ui/brand/brand.h"
#include "ui/shell/panel_text.h"

namespace ef::ui {

using core::i18n::StringId;
using core::setup::FailKind;
using core::setup::OfflineNote;
using core::setup::SetupPhase;
using core::setup::SetupRow;
using core::setup::SetupVm;
using core::setup::StepState;

namespace {

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring rep(std::wstring s, std::wstring_view key, std::wstring_view value) {
    if (const auto p = s.find(key); p != std::wstring::npos) s.replace(p, key.size(), value);
    return s;
}

constexpr SetupRow kRows[] = {SetupRow::Files, SetupRow::Shortcut, SetupRow::Service, SetupRow::Arp};

}  // namespace

std::wstring SetupView::version_w(const std::string& v) const { return v.empty() ? std::wstring(L"—") : widen(v); }

std::wstring SetupView::window_title() const { return screen_.t(StringId::SetupWindowTitle); }

ModalSpec SetupView::confirm_spec() const {
    ModalSpec m;
    m.title = screen_.t(StringId::SetupConfirmTitle);
    m.body = {screen_.t(StringId::SetupConfirmBody)};
    m.yes = screen_.t(StringId::SetupConfirmYes);
    m.no = screen_.t(StringId::SetupConfirmNo);
    m.caption = window_title();
    return m;
}

std::wstring SetupView::row_label(SetupRow r) const {
    switch (r) {
        case SetupRow::Files: return screen_.t(StringId::SetupStepFiles);
        case SetupRow::Shortcut: return screen_.t(StringId::SetupStepShortcut);
        case SetupRow::Service: return screen_.t(StringId::SetupStepService);
        case SetupRow::Arp: return screen_.t(StringId::SetupStepArp);
    }
    return {};
}

std::wstring SetupView::title_text(const SetupVm& vm) const {
    switch (vm.phase) {
        case SetupPhase::Installing: return screen_.t(StringId::SetupInstalling);
        case SetupPhase::Done: return screen_.t(StringId::SetupDoneTitle);
        case SetupPhase::Failed: return screen_.t(StringId::SetupFailedTitle);
        default: return screen_.t(StringId::SetupTitle);
    }
}

SetupView::Line SetupView::status_line(const SetupVm& vm, const std::wstring& install_dir) const {
    const auto& p = screen_.pal();
    const std::wstring ver = version_w(vm.version);
    const std::wstring dir = install_dir.empty() ? screen_.t(StringId::SetupUnknown) : install_dir;
    switch (vm.phase) {
        case SetupPhase::Checking: return {screen_.t(StringId::SetupChecking), p.text_secondary};
        case SetupPhase::Ready:
        case SetupPhase::Confirm:
        case SetupPhase::UacDenied:
        case SetupPhase::AppRunning:
            if (vm.latest_known) return {rep(screen_.t(StringId::SetupVersionLatest), L"{ver}", ver), p.accent};
            return {rep(screen_.t(StringId::SetupVersion), L"{ver}", ver), p.text_secondary};
        case SetupPhase::Offline: {
            StringId id = StringId::SetupOffline;
            if (vm.offline_note == OfflineNote::DownloadStopped) id = StringId::SetupDownloadStopped;
            else if (vm.offline_note == OfflineNote::DownloadFailed) id = StringId::SetupDownloadFailed;
            else if (vm.offline_note == OfflineNote::LaunchFailed) id = StringId::SetupLaunchFailed;
            return {screen_.t(id), p.warning};
        }
        case SetupPhase::Downloading:
            if (vm.handoff) return {screen_.t(StringId::SetupHandoff), p.accent};
            return {rep(screen_.t(StringId::SetupNewerFound), L"{ver}", version_w(vm.newer_version)), p.text_primary};
        case SetupPhase::VerifyFailed: return {screen_.t(StringId::SetupVerifyFailed), p.error};
        case SetupPhase::Installing: return {rep(screen_.t(StringId::SetupVersion), L"{ver}", ver), p.text_secondary};
        case SetupPhase::Done: return {rep(screen_.t(StringId::SetupDoneDetail), L"{dir}", dir), p.accent};
        case SetupPhase::Failed: {
            if (vm.fail_kind == FailKind::ResultUnknown) return {screen_.t(StringId::SetupFailedUnknown), p.error};
            if (vm.fail_kind == FailKind::NotStarted && vm.rollback_clean)
                return {screen_.t(vm.fail_untrusted ? StringId::SetupFailedUntrusted : StringId::SetupFailedNotStarted), p.error};
            if (vm.rollback_clean) return {screen_.t(StringId::SetupFailedClean), p.warning};
            std::wstring steps;
            std::vector<SetupRow> seen;
            for (const auto s : vm.undo_failed) {
                const auto r = core::setup::row_of(s);
                if (!r || std::find(seen.begin(), seen.end(), *r) != seen.end()) continue;
                seen.push_back(*r);
                if (!steps.empty()) steps += L", ";
                steps += row_label(*r);
            }
            if (steps.empty()) steps = screen_.t(StringId::SetupUnknown);
            return {rep(screen_.t(StringId::SetupFailedDirty), L"{steps}", steps), p.error};
        }
    }
    return {};
}

std::vector<SetupView::Line> SetupView::notes(const SetupVm& vm) const {
    const auto& p = screen_.pal();
    std::vector<Line> out;
    switch (vm.phase) {
        case SetupPhase::UacDenied: out.push_back({screen_.t(StringId::SetupUacDenied), p.warning}); break;
        case SetupPhase::AppRunning: out.push_back({screen_.t(StringId::SetupAppRunning), p.warning}); break;
        case SetupPhase::Offline:
            if (vm.offline_note == OfflineNote::CheckFailed || vm.offline_note == OfflineNote::DownloadFailed)
                out.push_back({screen_.t(StringId::SetupOfflineDetail), p.text_secondary});
            break;
        case SetupPhase::Installing: out.push_back({screen_.t(StringId::SetupRollbackNote), p.text_secondary}); break;
        case SetupPhase::Done:
            if (vm.from_portable) out.push_back({screen_.t(StringId::SetupPortableNote), p.text_secondary});
            if (vm.autostart_failed) out.push_back({screen_.t(StringId::SetupAutostartFailed), p.warning});
            break;
        case SetupPhase::Failed:
            // Teknik ayrinti (kanal metni, or. "setup worker exited early (14)"): neden tahmin edilmez, oldugu gibi.
            if (!vm.fail_detail.empty() && (vm.fail_kind == FailKind::NotStarted || vm.fail_kind == FailKind::ResultUnknown))
                out.push_back({widen(vm.fail_detail), p.text_secondary});
            break;
        default: break;
    }
    return out;
}

std::wstring SetupView::announcement(const SetupVm& vm, const std::wstring& install_dir) const {
    return a11y_join(title_text(vm), status_line(vm, install_dir).text);
}

float SetupView::rows_card(Graphics& g, float x, float y, float w, UiState& s, const SetupVm& vm, const std::wstring& install_dir,
                           std::int64_t now, HitMap& hits, StatusSeq& seq) {
    const auto& p = screen_.pal();
    const float pad = theme_.card_pad;
    const float row_h = theme_.setup.row_h;
    const float inner_w = std::max(w - 2 * pad, 1.f);
    const float bl = theme_.role(Role::Body).line, sl = theme_.role(Role::Secondary).line;
    const RectF card{x, y, w, 4 * row_h};
    screen_.draw_card(g, card);
    const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
    const bool editable = vm.phase != SetupPhase::Confirm;
    const auto show = screen_.t(StringId::SetupShow);
    const float bw = screen_.button_group_w(g, {show}, true);
    const float ctrl_w = std::max(bw, theme_.toggle_w);
    const std::wstring labels[4] = {screen_.t(StringId::SetupRowLocation), screen_.t(StringId::SetupRowService),
                                    screen_.t(StringId::SetupRowAutostart), screen_.t(StringId::SetupRowLicense)};
    float label_w = 0;
    for (const auto& l : labels) label_w = std::max(label_w, std::ceil(g.measure(Role::Body, l, inner_w, false).w) + 1.f);
    label_w = std::min(label_w, inner_w * 0.5f);
    const float value_x = x + pad + label_w + theme_.space(3);
    const float value_w = std::max(x + w - pad - value_x, 1.f);
    for (int i = 0; i < 4; ++i) {
        const float ry = y + row_h * static_cast<float>(i);
        if (i) g.fill({x + pad, ry, inner_w, 1}, divider);
        const RectF lr{x + pad, ry + (row_h - bl) / 2, label_w, bl};
        const auto label = screen_.fit_text(g, Role::Body, labels[i], label_w);
        g.text(Role::Body, label, lr, p.text_primary, {Align::Leading, false, true});
        if (i < 2) {
            const std::wstring value =
                i == 0 ? (install_dir.empty() ? screen_.t(StringId::SetupUnknown) : install_dir) : screen_.t(StringId::SetupRowServiceValue);
            const auto shown = screen_.fit_text(g, Role::Secondary, value, value_w);
            const RectF vr{value_x, ry + (row_h - sl) / 2, value_w, sl};
            g.text(Role::Secondary, shown, vr, p.text_secondary, {Align::Trailing, false, true});
            hits.add_text(seq.take(), {x + pad, ry, inner_w, row_h}, labels[i] + L": " + value);
            continue;
        }
        hits.add_text(seq.take(), lr, labels[i]);
        if (i == 2) {
            const RectF tr{x + w - pad - theme_.toggle_w, ry + (row_h - theme_.toggle_h) / 2, theme_.toggle_w, theme_.toggle_h};
            screen_.small_toggle(g, tr, vm.autostart, editable, Hit::SetupAutostart, labels[2], s, now, hits);
        } else {
            const float by = ry + (row_h - theme_.button_row_h) / 2;
            screen_.button(g, x + w - pad - ctrl_w, by, Hit::SetupLicense, show, false, s, now, hits, editable, false, true, ctrl_w);
            hits.meta[Hit::SetupLicense].name = show + L": " + labels[3];
            hits.meta[Hit::SetupLicense].selected = vm.license_open;
        }
    }
    return card.h;
}

float SetupView::steps_card(Graphics& g, float x, float y, float w, const SetupVm& vm, HitMap& hits, StatusSeq& seq) {
    const auto& p = screen_.pal();
    const float pad = theme_.card_pad;
    const float row_h = theme_.setup.row_h;
    const float inner_w = std::max(w - 2 * pad, 1.f);
    const float bl = theme_.role(Role::Body).line, sl = theme_.role(Role::Secondary).line;
    const float dot = theme_.setup.step_dot;
    const RectF card{x, y, w, 4 * row_h};
    screen_.draw_card(g, card);
    const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
    const std::wstring states[5] = {screen_.t(StringId::SetupStatePending), screen_.t(StringId::SetupStateRunning),
                                    screen_.t(StringId::SetupStateDone), screen_.t(StringId::SetupStateReverted),
                                    screen_.t(StringId::SetupStateFailed)};
    float chip_w = 0;
    for (const auto& st : states) chip_w = std::max(chip_w, screen_.chip_width(g, st));
    for (std::size_t i = 0; i < core::setup::kSetupRows; ++i) {
        const float ry = y + row_h * static_cast<float>(i);
        if (i) g.fill({x + pad, ry, inner_w, 1}, divider);
        const StepState st = vm.steps[i];
        ChipTone tone = ChipTone::Neutral;
        Color dc = p.border;
        if (st == StepState::Running) tone = ChipTone::Warn, dc = p.warning;
        else if (st == StepState::Done) tone = ChipTone::Good, dc = p.accent;
        else if (st == StepState::Failed) tone = ChipTone::Danger, dc = p.error;
        else if (st == StepState::Reverted) dc = p.text_secondary;
        const float cy = ry + row_h / 2;
        g.fill_circle(x + pad + dot / 2, cy, dot / 2, dc);
        const float lx = x + pad + dot + theme_.space(2);
        const float lw = std::max(x + w - pad - chip_w - theme_.space(2) - lx, 1.f);
        const auto label = row_label(kRows[i]);
        g.text(Role::Body, screen_.fit_text(g, Role::Body, label, lw), {lx, cy - bl / 2, lw, bl}, p.text_primary, {Align::Leading, false, true});
        const std::wstring& word = states[static_cast<std::size_t>(st)];
        screen_.chip(g, RectF{x + w - pad - chip_w, cy - sl / 2, chip_w, sl}, word, tone);
        hits.add_text(seq.take(), {x + pad, ry, inner_w, row_h}, label + L": " + word);
    }
    return card.h;
}

float SetupView::download_card(Graphics& g, float x, float y, float w, const SetupVm& vm, HitMap& hits, StatusSeq& seq) {
    const auto& p = screen_.pal();
    const float pad = theme_.card_pad;
    const float inner_w = std::max(w - 2 * pad, 1.f);
    const float sl = theme_.role(Role::Secondary).line;
    const float bar_h = theme_.setup.progress_h;
    const auto note = screen_.t(StringId::SetupNewerVerify);
    const float note_h = g.measure(Role::Secondary, note, inner_w).h;
    const float card_h = pad + sl + theme_.space(1) + bar_h + theme_.space(2) + note_h + pad;
    screen_.draw_card(g, {x, y, w, card_h});
    const int pct = static_cast<int>(std::lround(std::clamp(vm.progress, 0.0, 1.0) * 100.0));
    const std::wstring pct_text = percent_text(pct, tr_);
    float cy = y + pad;
    const RectF pr{x + pad, cy, inner_w, sl};
    g.text(Role::Secondary, pct_text, pr, p.text_primary, {Align::Trailing, false, true, true});
    const auto name = screen_.t(StringId::SetupProgressName);
    g.text(Role::Secondary, screen_.fit_text(g, Role::Secondary, name, inner_w * 0.6f), {x + pad, cy, inner_w * 0.6f, sl}, p.text_secondary,
           {Align::Leading, false, true});
    cy += sl + theme_.space(1);
    const RectF bar{x + pad, cy, inner_w, bar_h};
    g.fill_round(bar, bar_h / 2, p.surface_raised);
    const float frac = static_cast<float>(std::clamp(vm.progress, 0.0, 1.0));
    if (frac > 0) g.fill_round({bar.x, bar.y, std::max(bar.w * frac, bar_h), bar_h}, bar_h / 2, p.accent);
    hits.add_progress(hit_of(HitKind::Progress, 0), bar, name, static_cast<double>(pct));
    cy += bar_h + theme_.space(2);
    const RectF nr{x + pad, cy, inner_w, note_h};
    g.text(Role::Secondary, note, nr, p.text_secondary);
    hits.add_text(seq.take(), nr, note);
    return card_h;
}

void SetupView::license_panel(Graphics& g, const RectF& r, UiState& s, HitMap& hits, StatusSeq& seq) {
    const auto& p = screen_.pal();
    const float pad = theme_.card_pad;
    screen_.draw_card(g, r);
    const float inner_w = std::max(r.w - 2 * pad - theme_.scrollbar_inset, 1.f);
    struct Para {
        std::wstring text;
        Role role;
        Color color;
    };
    const std::vector<Para> paras = {
        {screen_.t(StringId::SetupRowLicense), Role::Section, p.text_primary},
        {screen_.t(StringId::SetupPrivacy), Role::Body, p.text_primary},
        {screen_.t(StringId::AboutTitle), Role::Section, p.text_primary},
        {screen_.t(StringId::AboutComponents), Role::Secondary, p.text_primary},
        {screen_.t(StringId::AboutMit1), Role::Secondary, p.text_secondary},
        {screen_.t(StringId::AboutMit2), Role::Secondary, p.text_secondary},
        {screen_.t(StringId::AboutMit3), Role::Secondary, p.text_secondary},
        {screen_.ofl_text(), Role::Secondary, p.text_secondary},  // gomulu JetBrains Mono (SIL OFL 1.1)
    };
    std::vector<float> heights;
    float total = 0;
    for (std::size_t i = 0; i < paras.size(); ++i) {
        heights.push_back(g.measure(paras[i].role, paras[i].text, inner_w).h);
        total += heights.back() + (i ? theme_.space(2) : 0.f);
    }
    const RectF view{r.x + pad, r.y + pad, inner_w, std::max(r.h - 2 * pad, 1.f)};
    s.content_height = total;
    const float max_scroll = std::max(0.f, total - view.h);
    s.scroll = std::clamp(s.scroll, 0.f, max_scroll);
    g.push_clip(view);
    float yy = view.y - s.scroll;
    for (std::size_t i = 0; i < paras.size(); ++i) {
        if (i) yy += theme_.space(2);
        const RectF pr{view.x, yy, inner_w, heights[i]};
        if (pr.bottom() > view.y && pr.y < view.bottom()) g.text(paras[i].role, paras[i].text, pr, paras[i].color);
        const Hit id = seq.take();
        // Gorunen kutu: kismen kirpilan paragrafin erisilebilirlik dikdortgeni pencerenin disina tasmaz.
        const RectF vis{pr.x, std::max(pr.y, view.y), pr.w, std::max(std::min(pr.bottom(), view.bottom()) - std::max(pr.y, view.y), 0.f)};
        hits.add_text(id, vis.h > 0 ? vis : pr, paras[i].text);
        if (auto it = hits.meta.find(id); it != hits.meta.end()) it->second.offscreen = !(pr.bottom() > view.y && pr.y < view.bottom());
        yy += heights[i];
    }
    g.pop_clip();
    if (total > view.h && view.h > 0) {
        const float thumb_h = std::max(theme_.touch_min, view.h * view.h / total);
        const float thumb_y = view.y + (view.h - thumb_h) * (s.scroll / std::max(1.f, max_scroll));
        g.fill_round({r.right() - theme_.scrollbar_inset, thumb_y, theme_.scrollbar_thumb_w, thumb_h}, theme_.scrollbar_thumb_w / 2, p.border);
    }
}

void SetupView::footer(Graphics& g, float x, float y, float w, UiState& s, const SetupVm& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    const auto& p = screen_.pal();
    const auto b = core::setup::setup_buttons(vm);
    const float gap = theme_.space(1);
    const float bh = theme_.button_h;
    if (vm.license_open) {
        const auto close = screen_.t(StringId::SetupClose);
        const float cw = screen_.button_group_w(g, {close});
        screen_.button(g, x + w - cw, y, Hit::SetupLicenseClose, close, false, s, now, hits, true, false, false, cw);
        hits.meta[Hit::SetupLicenseClose].name = close + L": " + screen_.t(StringId::SetupRowLicense);
        return;
    }
    struct Btn {
        Hit id;
        std::wstring label;
        bool primary;
        bool enabled;
    };
    std::vector<Btn> right;
    if (b.cancel) right.push_back({Hit::SetupCancel, screen_.t(StringId::SetupCancel), false, true});
    if (b.close) right.push_back({Hit::SetupClose, screen_.t(StringId::SetupClose), false, true});
    if (b.stop) right.push_back({Hit::SetupStop, screen_.t(StringId::SetupStop), false, b.stop_enabled});
    if (b.retry) right.push_back({Hit::SetupRetry, screen_.t(StringId::SetupRetry), true, true});
    if (b.install) right.push_back({Hit::SetupInstall, screen_.t(StringId::SetupInstall), true, b.install_enabled});
    if (b.open) right.push_back({Hit::SetupOpen, screen_.t(StringId::SetupOpen), true, true});
    float bw = 0;
    for (const auto& r : right) bw = std::max(bw, screen_.button_group_w(g, {r.label}));
    const float group_w = right.empty() ? 0.f : bw * static_cast<float>(right.size()) + gap * static_cast<float>(right.size() - 1);
    float bx = x + w - group_w;
    for (const auto& r : right) {
        screen_.button(g, bx, y, r.id, r.label, r.primary, s, now, hits, r.enabled, false, false, bw);
        bx += bw + gap;
    }
    // Sol yuva: Offline'da "<surum> surumunu kur"; kurulum sunulan evrelerde "Yonetici izni bir kez istenir".
    const float left_w = std::max(w - group_w - theme_.space(3), 1.f);
    if (b.install_anyway) {
        const auto label = rep(screen_.t(StringId::SetupInstallAnyway), L"{ver}", version_w(vm.version));
        screen_.button(g, x, y, Hit::SetupInstallAnyway, label, false, s, now, hits);
        return;
    }
    if (b.install || b.retry) {
        if (vm.phase == SetupPhase::VerifyFailed) return;
        const float sl = theme_.role(Role::Secondary).line;
        const auto note = screen_.t(StringId::SetupAdminOnce);
        const RectF nr{x, y + (bh - sl) / 2, left_w, sl};
        g.text(Role::Secondary, screen_.fit_text(g, Role::Secondary, note, left_w), nr, p.text_secondary, {Align::Leading, false, true});
        hits.add_text(seq.take(), nr, note);
    }
}

HitMap SetupView::paint(Graphics& g, float w, float h, UiState& s, const SetupVm& vm, const std::wstring& install_dir, std::int64_t now) {
    HitMap hits;
    const auto& p = screen_.pal();
    screen_.card_log_.clear();
    g.clear(p.background);
    screen_.paint_strip(g, w, s, now);
    const float m = theme_.setup.margin;
    const float cw = std::max(w - 2 * m, 1.f);
    StatusSeq seq;
    float y = theme_.strip_h + m;

    // Marka lockup'i: murekkep kutusu kutuya oranli sigdirilir (sola dayali, dikey ortali; ust menuyle ayni kural).
    {
        const bool dark = screen_.pal_ == &theme_.dark;
        const auto& doc = brand::logo(dark ? brand::Logo::LockupDark : brand::Logo::LockupWhite);
        const float ink_w = doc.max_x - doc.min_x, ink_h = doc.max_y - doc.min_y;
        const RectF lb{m, y, std::min(theme_.setup.logo_w, cw), theme_.setup.logo_h};
        if (!doc.shapes.empty() && ink_w > 0 && ink_h > 0) {
            const float k = std::min(lb.w / ink_w, lb.h / ink_h);
            const RectF box{lb.x - doc.min_x * k, lb.y + lb.h / 2 - (doc.min_y + doc.max_y) / 2 * k, doc.view_w * k, doc.view_h * k};
            g.svg(doc, box, dark ? std::nullopt : std::optional<Color>(p.text_primary), p.accent);
        } else {
            g.text(Role::Section, screen_.t(StringId::AppTitle), lb, p.text_primary, {Align::Leading, false, true});
        }
        y += lb.h + theme_.space(3);
    }

    // Baslik + durum satiri
    const auto title = title_text(vm);
    const float th = theme_.role(Role::PageTitle).line;
    g.text(Role::PageTitle, screen_.fit_text(g, Role::PageTitle, title, cw), {m, y, cw, th}, p.text_primary, {Align::Leading, false, true});
    hits.add_text(seq.take(), {m, y, cw, th}, title);
    y += th + theme_.space(0);
    const Line st = status_line(vm, install_dir);
    if (!st.text.empty()) {
        const float sh = g.measure(Role::Body, st.text, cw).h;
        g.text(Role::Body, st.text, {m, y, cw, sh}, st.color);
        hits.add_text(seq.take(), {m, y, cw, sh}, st.text);
        y += sh;
    }
    y += theme_.space(3);

    const float footer_y = h - m - theme_.button_h;
    const auto b = core::setup::setup_buttons(vm);
    if (vm.license_open && b.rows) {
        license_panel(g, {m, y, cw, std::max(footer_y - theme_.space(3) - y, 1.f)}, s, hits, seq);
    } else {
        s.content_height = 0;
        s.scroll = 0;
        if (b.rows) y += rows_card(g, m, y, cw, s, vm, install_dir, now, hits, seq);
        else if (vm.phase == SetupPhase::Downloading) y += download_card(g, m, y, cw, vm, hits, seq);
        else if (vm.phase == SetupPhase::Installing || vm.phase == SetupPhase::Done || vm.phase == SetupPhase::Failed)
            y += steps_card(g, m, y, cw, vm, hits, seq);
        for (const auto& n : notes(vm)) {
            y += theme_.space(2);
            const float nh = g.measure(Role::Secondary, n.text, cw).h;
            g.text(Role::Secondary, n.text, {m, y, cw, nh}, n.color);
            hits.add_text(seq.take(), {m, y, cw, nh}, n.text);
            y += nh;
        }
    }
    footer(g, m, footer_y, cw, s, vm, now, hits, seq);

    if (s.modal) {
        // Spec 3: Kur -> uygulama ici Evet/Hayir (ana pencereyle ayni kip karti); yalniz kip ogeleri kalir.
        screen_.modal_layer(g, w, h, s, now, hits);
    }
    hits.cards = std::move(screen_.card_log_);
    screen_.card_log_.clear();
    return hits;
}

std::vector<Hit> setup_focus_order(const SetupVm& vm, const UiState& s) {
    std::vector<Hit> v;
    if (s.modal) {
        for (const ModalFocus& f : modal_focus_order(*s.modal)) v.push_back(modal_hit(f));
        return v;
    }
    const auto b = core::setup::setup_buttons(vm);
    if (vm.license_open && b.rows) {
        v.push_back(Hit::SetupLicenseClose);
        return v;
    }
    if (b.rows) {
        v.push_back(Hit::SetupAutostart);
        v.push_back(Hit::SetupLicense);
    }
    if (b.install_anyway) v.push_back(Hit::SetupInstallAnyway);
    if (b.cancel) v.push_back(Hit::SetupCancel);
    if (b.close) v.push_back(Hit::SetupClose);
    if (b.stop) v.push_back(Hit::SetupStop);
    if (b.retry) v.push_back(Hit::SetupRetry);
    if (b.install) v.push_back(Hit::SetupInstall);
    if (b.open) v.push_back(Hit::SetupOpen);
    return v;
}

std::vector<Hit> setup_focus_order(const SetupVm& vm, const UiState& s, const HitMap& hits) {
    std::vector<Hit> out;
    for (const Hit h : setup_focus_order(vm, s))
        if (hits.tabbable(h)) out.push_back(h);
    return out;
}

}  // namespace ef::ui
