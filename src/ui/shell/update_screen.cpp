// Acilista zorunlu guncelleme (spec 2026-11-11-zorunlu-guncelleme-design.md §1). Ust serit: kontrol suruyor / kontrol
// edilemedi / dogrulanamadi / guncellendi / eski surum korundu. Tam ekran guncelleme karti: yeni surum, aciklama,
// gercek indirme ilerlemesi, "Simdi guncelle" (veya klasor yazilamiyorsa "Indirme sayfasini ac"), Performans acikken
// "Onceki ayarlara don" ve "Cik". Gezinme sekmeleri kartta yoktur; geri alma guncellemeye bagli degildir.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/measure/view.h"
#include "ui/brand/brand.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::update::UpdateOutcome;

namespace {

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring replace_all(std::wstring s, std::wstring_view key, const std::wstring& val) {
    for (std::size_t pos = s.find(key); pos != std::wstring::npos; pos = s.find(key, pos + val.size()))
        s.replace(pos, key.size(), val);
    return s;
}

constexpr float kGateCardMaxW = 560.f;  // guncelleme karti en genis (bilesen olcusu)
constexpr float kGateMarkH = 40.f;      // kart ustundeki amblem
constexpr float kGateBarH = 8.f;        // ilerleme cubugu kalinligi

bool perf_busy(const PerfView& pv) {
    using core::perf::Phase;
    return pv.phase == Phase::Planning || pv.phase == Phase::Applying || pv.phase == Phase::Restoring;
}

}  // namespace

bool show_restore(const ViewModel& vm) {
    using core::perf::Phase;
    const auto ph = vm.perf.phase;
    return vm.update.rollback_available || vm.perf.switch_on || vm.perf.active > 0 || ph == Phase::On || ph == Phase::ConfirmRestore ||
           ph == Phase::Restoring || ph == Phase::RecoveryNeeded;
}

std::wstring Screen::update_banner_text(const ViewModel& vm, ChipTone* tone) const {
    const auto& u = vm.update;
    ChipTone tn = ChipTone::Neutral;
    std::wstring text;
    if (!u.notice_key.empty() && (u.outcome == UpdateOutcome::UpToDate || u.outcome == UpdateOutcome::CheckFailed)) {
        // Sonuc bildirimi (guncellendi / eski surum korundu) kontrol sonucundan once gelir.
        text = replace_all(tk(u.notice_key), L"{v}", widen(u.notice_version));
        tn = u.notice_key == "update.updated" ? ChipTone::Good : ChipTone::Warn;
    } else if (u.outcome == UpdateOutcome::Checking) {
        text = tk("update.checking");
    } else if (u.outcome == UpdateOutcome::CheckFailed) {
        text = tk("update.failed");
        tn = ChipTone::Warn;
    } else if (u.outcome == UpdateOutcome::VerifyFailed) {
        text = tk("update.verify_failed");
        tn = ChipTone::Warn;
    }
    if (tone) *tone = tn;
    return text;
}

float Screen::update_banner(Graphics& g, const RectF& area, const RectF& column, const ViewModel& vm, HitMap& hits) {
    if (update_gate_shown(vm)) return 0.f;
    ChipTone tone = ChipTone::Neutral;
    const std::wstring text = update_banner_text(vm, &tone);
    if (text.empty() || area.w <= 0) return 0.f;
    const auto& p = pal();
    // Siki yerlesim: zemin pencere genisliginde; metin icerik sutununda (logo, sayfa basligi ve kartlarla ayni sol
    // kenar; genis pencerede sutunla birlikte ortalanir).
    const float pad_y = theme_.space(1);
    const float tx = std::max(column.x, area.x);
    const float tw = std::max(std::min(column.right(), area.right()) - tx, 0.f);
    const float th = std::max(g.measure(Role::Secondary, text, tw).h, theme_.role(Role::Secondary).line);
    const RectF band{area.x, area.y, area.w, th + 2 * pad_y};
    // Renk tek basina anlam tasimaz: metin her zaman durumu soyler; zemin/yazi chip tonlariyla (HC paletinde sistem renkleri).
    const ChipColors c = chip_colors(p, tone);
    g.fill(band, c.bg);
    g.fill({band.x, band.bottom() - 1, band.w, 1}, p.border);
    const RectF tr{tx, band.y + pad_y, tw, th};
    g.text(Role::Secondary, text, tr, c.fg);
    hits.add_text(Hit::UpdateStatus, tr, text);
    return band.h;
}

void Screen::update_gate(Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const auto& u = vm.update;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    g.clear(p.background);
    paint_backdrop(g, {0, 0, w, h});  // BOOST pencere zemini (yalniz koyu tema isiltilari)
    paint_strip(g, w, s, now);

    const bool not_writable = u.outcome == UpdateOutcome::NotWritable;
    const bool repair = !not_writable && u.service_unavailable;
    const bool busy = u.step != ViewModel::UpdateView::Step::Idle;
    const bool restore = show_restore(vm);
    const bool restore_enabled = restore && !busy && !perf_busy(vm.perf);
    s.update_gate = true;
    s.update_not_writable = not_writable;
    s.update_busy = busy;
    s.update_restore = restore_enabled;
    s.update_repair = repair;

    const float margin = theme_.space(5);
    const float card_w = std::min(kGateCardMaxW, std::max(w - 2 * margin, 0.f));
    const float pad = theme_.space(5);
    const float inner = std::max(card_w - 2 * pad, 0.f);
    const float gap = theme_.space(3);

    const std::wstring title = replace_all(tk("update.gate_title"), L"{v}", widen(u.new_version));
    const std::wstring body = tk(update_gate_body_key(vm));
    std::wstring status;
    bool status_warn = false;
    if (u.step == ViewModel::UpdateView::Step::Downloading) {
        auto mb = [&](std::uint64_t b) { return widen(core::measure::format_number(static_cast<double>(b) / 1e6, 1, tr)); };
        status = replace_all(replace_all(tk("update.downloading"), L"{done}", mb(u.bytes)), L"{total}", u.total ? mb(u.total) : std::wstring(L"—"));
    } else if (u.step == ViewModel::UpdateView::Step::Installing) {
        status = tk("update.installing");
    } else if (u.step == ViewModel::UpdateView::Step::Repairing) {
        status = tk("update.repairing");
    } else if (!u.notice_key.empty()) {
        status = replace_all(tk(u.notice_key), L"{v}", widen(u.notice_version));
        status_warn = true;
    }
    const bool bar = u.step == ViewModel::UpdateView::Step::Downloading;

    struct Btn {
        Hit id;
        std::wstring label;
        ButtonKind kind;
        bool enabled;
        RectF r;
    };
    std::vector<Btn> btns;
    if (not_writable) btns.push_back({Hit::UpdateOpenPage, tk("update.open_page"), ButtonKind::Primary, true, {}});
    else if (repair) btns.push_back({Hit::UpdateRepairService, tk("update.repair_service"), ButtonKind::Primary, !busy, {}});
    else btns.push_back({Hit::UpdateNow, tk("update.now"), ButtonKind::Primary, !busy, {}});
    if (restore) btns.push_back({Hit::UpdateRestore, tk("update.restore"), ButtonKind::Secondary, restore_enabled, {}});
    btns.push_back({Hit::UpdateQuit, tk("update.quit"), ButtonKind::Secondary, true, {}});
    float row_w = 0;
    for (auto& b : btns) {
        b.r = button_rect(g, 0, 0, b.label);
        b.r.w = std::min(b.r.w, inner);
        row_w += b.r.w;
    }
    const float bgap = theme_.space(2);
    row_w += bgap * static_cast<float>(btns.size() - 1);
    const bool one_row = row_w <= inner;
    const float btn_h = theme_.button_h;
    const float btns_h = one_row ? btn_h : btn_h * static_cast<float>(btns.size()) + bgap * static_cast<float>(btns.size() - 1);

    const float title_h = g.measure(Role::PageTitle, title, inner).h;
    const float body_h = g.measure(Role::Body, body, inner).h;
    const float status_h = status.empty() ? 0.f : g.measure(Role::Secondary, status, inner).h;
    float card_h = pad + kGateMarkH + gap + title_h + theme_.space(1) + body_h;
    if (!status.empty()) card_h += gap + status_h;
    if (bar) card_h += theme_.space(1) + kGateBarH;
    card_h += gap + btns_h + pad;

    const float top = theme_.strip_h + margin;
    const RectF card{std::round((w - card_w) / 2), std::max(top, std::round((h - card_h) / 2)), card_w, card_h};
    draw_card(g, card);
    float y = card.y + pad;
    const float x = card.x + pad;
    {
        using ef::ui::brand::Logo;
        const bool dark = pal_ == &theme_.dark;
        const auto& doc = ef::ui::brand::logo(dark ? Logo::MarkColor : Logo::MarkCurrent);
        if (!doc.shapes.empty()) g.svg(doc, {x, y, kGateMarkH, kGateMarkH}, dark ? std::nullopt : std::optional<Color>(p.text_primary), p.accent);
    }
    y += kGateMarkH + gap;
    StatusSeq seq;
    const RectF title_r{x, y, inner, title_h};
    g.text(Role::PageTitle, title, title_r, p.text_primary);
    hits.add_text(seq.take(), title_r, title);
    y += title_h + theme_.space(1);
    const RectF body_r{x, y, inner, body_h};
    g.text(Role::Body, body, body_r, p.text_secondary);
    hits.add_text(seq.take(), body_r, body);
    y += body_h;
    if (!status.empty()) {
        y += gap;
        const RectF st_r{x, y, inner, status_h};
        g.text(Role::Secondary, status, st_r, status_warn ? ensure_contrast(p.warning, p.surface, p.text_primary, 4.5) : p.text_secondary);
        hits.add_text(seq.take(), st_r, status);
        y += status_h;
    }
    if (bar) {
        y += theme_.space(1);
        const RectF track{x, y, inner, kGateBarH};
        const double frac = std::clamp(u.progress01, 0.0, 1.0);
        g.fill_round(track, kGateBarH / 2, p.surface_raised);
        g.stroke_round(track, kGateBarH / 2, p.border);
        if (frac > 0) g.fill_round({track.x, track.y, std::max(kGateBarH, static_cast<float>(inner * frac)), kGateBarH}, kGateBarH / 2, p.accent);
        hits.add_progress(hit_of(HitKind::Progress, 0), track, tk("update.progress"), frac * 100.0);
        y += kGateBarH;
    }
    y += gap;
    float bx = x;
    for (auto& b : btns) {
        b.r.x = bx;
        b.r.y = y;
        button_face(g, b.r, b.id, b.label, b.kind, b.enabled, s, now, hits);
        hits.add(b.id, b.r, b.label, AccRole::Button, false, -1, b.enabled);
        if (one_row) bx += b.r.w + bgap;
        else y += btn_h + bgap;
    }
}

}  // namespace ef::ui
