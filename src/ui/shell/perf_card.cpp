// UI-02: Performans bolumu (Panel) — yuvarlak dugme, plan onayi (Evet/Hayir), gercek adimli ilerleme, kurtarma.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/games/game_guide.h"
#include "core/measure/view.h"
#include "core/perf/apply_progress.h"
#include "core/perf/debloat_plan.h"
#include "core/perf/gpu_route_plan.h"
#include "core/service/uac_copy.h"
#include "ui/shell/recovery_text.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
using core::perf::Notice;
using core::perf::Phase;

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

}  // namespace

std::wstring locale_upper(std::wstring_view s, bool turkish) {
    if (s.empty()) return {};
    const wchar_t* loc = turkish ? L"tr-TR" : L"en-US";
    const int n = LCMapStringEx(loc, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr, 0);
    if (n <= 0) return std::wstring(s);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    LCMapStringEx(loc, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr, 0);
    return out;
}

PerfView make_perf_view(const core::perf::PerfController& c) {
    PerfView v;
    v.phase = c.phase();
    v.notice = c.notice();
    for (const auto& i : c.plan().items) v.item_keys.push_back(i.title_key);
    // Uyari yalniz secili gruplar icin: kapatilan grubun UAC/yeniden baslatma uyarisi gosterilmez.
    for (const auto& i : c.plan().items) {
        if (!c.selected(i.group())) continue;
        v.quality = v.quality || i.op.affects_quality;
        v.reboot = v.reboot || i.op.needs_reboot;
        v.admin = v.admin || i.needs_admin;
    }
    v.external_conflict = c.external_conflict();
    v.drift_n = c.drift_items().size();
    v.drift_update = c.os_updated();
    v.drift_confirm = c.drift_confirm();
    if (c.phase() == core::perf::Phase::RecoveryNeeded) v.recovery_items = recovery_items_vm(c.failed_items());  // saha 2026-11-15
    else v.recovery_items = recovery_items_vm(c.off_items());  // acilis uzlastirmasi: gercekte kapali kalan ayarlar (Notice::SomeOff)
    v.reboot_pending = c.reboot_pending();
    v.startup = c.plan().startup_changes();
    v.done = c.progress().done;
    v.total = c.progress().total;
    // T18: "Adim i/n - grup" yalniz uygulama sirasinda; ozet yalniz bu oturumda dogrulanmis sayidan.
    const auto& pr = c.progress();
    v.progress_has_step = c.phase() == core::perf::Phase::Applying && core::perf::has_step_line(pr);
    v.progress_step = pr.step;
    v.progress_steps = pr.steps;
    v.progress_group = pr.group;
    v.progress_phase = core::perf::apply_phase(pr);
    v.verified = c.verified_count();
    v.tailored = c.phase() == core::perf::Phase::Confirming && c.tailored();
    v.active = c.active_changes();
    v.switch_on = c.switch_on();
    // UI-10: gruplar; onay asamasinda gercek plan, aksi halde onizleme belirler
    const auto& src = c.phase() == core::perf::Phase::Confirming ? c.plan() : c.preview();
    for (const auto& g : core::perf::plan_groups()) {
        GroupVM gv;
        gv.key = std::string(g.key);
        gv.desc_key = std::string(g.desc_key);
        gv.known = c.preview_ready() || c.phase() == core::perf::Phase::Confirming;
        for (const auto& it : src.items)
            if (it.group() == gv.key) gv.will_change = true;
        if (!core::perf::group_listed(g, gv.will_change)) continue;  // ADR-040: ogesiz yeni grup satir almaz (Panel 0.9.2)
        gv.field_keys = core::perf::group_field_keys(src, gv.key);  // EF-X25 T03
        gv.details = core::perf::group_field_details(src, gv.key);  // P2
        gv.selected = c.selected(gv.key);
        gv.evidence = g.evidence;
        gv.risk = g.risk;
        gv.advanced = !core::perf::default_on_for(g, c.device_class()) && !gv.selected;  // T04 (T18: cihaza gore varsayilan)
        gv.no_effect = c.no_effect(gv.key);  // P4: kayitli "Fark yok" hukmu (yalniz vurgu; secim ve plan ayni)
        v.groups.push_back(std::move(gv));
    }
    return v;
}

void sync_perf_animations(UiState& s, const PerfView& before, const PerfView& after, const Theme& t, std::int64_t now) {
    const bool rm = s.reduced_motion;
    if (before.switch_on != after.switch_on)
        s.toggle_knob.retarget(after.switch_on ? 1.f : 0.f, now, effective_ms(t.motion.toggle, rm));
    if (before.phase != after.phase) {
        s.card_fade = Tween(0);
        s.card_fade.retarget(1, now, effective_ms(t.motion.details, rm));
        if (after.phase == Phase::Applying || after.phase == Phase::Restoring) s.progress_fill = Tween(0);
    }
    const float frac = after.total ? static_cast<float>(after.done) / static_cast<float>(after.total) : 0.f;
    if (std::fabs(frac - s.progress_fill.target()) > 1e-4f) s.progress_fill.retarget(frac, now, effective_ms(t.motion.toggle, rm));
    s.perf_phase = after.phase;
    s.perf_conflict = after.external_conflict;
    s.perf_drift = after.drift_n > 0 && (after.phase == Phase::On || after.phase == Phase::Off);
}

float Screen::button(Graphics& g, float x, float y, Hit id, std::wstring_view label, bool primary, UiState& s,
                     std::int64_t now, HitMap& hits, bool enabled, bool danger, bool in_row, float width) {
    RectF r = button_rect(g, x, y, label, in_row);
    r.w = std::max(r.w, width);  // liste / satir ortak genisligi (B7); etiket yine ortada
    // DS-04: danger = geri donusuz islem (hata token'i cerceve/metin) + ayri etiket; renk tek basina anlam tasimaz.
    button_face(g, r, id, label, danger ? ButtonKind::Danger : primary ? ButtonKind::Primary : ButtonKind::Secondary, enabled, s,
                now, hits);
    hits.add(id, r, std::wstring(label), AccRole::Button, false, -1, enabled);
    return r.w;
}

Color Screen::button_face(Graphics& g, const RectF& r, Hit id, std::wstring_view label, ButtonKind kind, bool enabled, UiState& s,
                          std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const float rad = theme_.radius_control;
    // DS-02: oge basina hover (motion.hover) ve basma (motion.press) gecisi; azaltilmis harekette aninda.
    const float hv = enabled ? hover_value(s, id, now) : 0.f;
    const float pv = enabled ? press_value(s, id, now) : 0.f;
    Color fg = p.text_primary;
    if (!enabled) {  // RDY-09: kaybolmaz, soluk; odak yerinde kalabilir
        g.fill_round(r, rad, mix(p.surface, p.surface_raised, alpha_scale::kDisabled));
        g.stroke_round(r, rad, p.border);
        fg = p.text_secondary;
    } else if (kind == ButtonKind::Primary && pal_ == &theme_.dark) {
        // BOOST .btn-primary: dikey gradyan #52FF8B -> #2EE66B, 0 0 18px rgba(61,255,122,.22) parlama, ust kenarda
        // inset 1px beyaz %35. Hover: brightness(1.06), parlama 26px %40, inset %45. Basili: koyu ton, parlamasiz.
        using namespace depth;
        if (glow_allowed(s) && pv < 0.5f)
            g.soft_shadow(r, rad, 0.f, 0.f, kBtnGlowBlur + (kBtnGlowBlurHover - kBtnGlowBlur) * hv, mix(kBtnGlow, kBtnGlowHover, hv));
        const float bright = 1.f + (kBtnHoverBright - 1.f) * hv;
        const auto shade = [&](const Color& c) {
            const Color lit{std::min(c.r * bright, 1.f), std::min(c.g * bright, 1.f), std::min(c.b * bright, 1.f), 1.f};
            return mix(lit, p.background, alpha_scale::kPrimaryPressDim * pv);
        };
        const GradientStop st[] = {{0.f, shade(kBtnTop)}, {1.f, shade(kBtnBottom)}};
        g.fill_linear_round(r, rad, {r.x, r.y}, {r.x, r.bottom()}, st);
        if (r.w > 2 * rad) g.fill({r.x + rad, r.y, r.w - 2 * rad, 1.f}, Color{1, 1, 1, kBtnInset + (kBtnInsetHover - kBtnInset) * hv});
        fg = p.on_accent;
    } else if (kind == ButtonKind::Primary) {
        // Spec §4: yesil dolgu + parlama; hover daha acik ton ve guclu parlama; basili koyu ton, parlamasiz.
        if (glow_allowed(s) && pv < 0.5f)
            g.glow_round(r, rad, p.accent, alpha_scale::kPrimaryGlowIdle + (1.f - alpha_scale::kPrimaryGlowIdle) * hv);
        const Color lit = mix(p.accent, p.text_primary, alpha_scale::kPrimaryHoverLift * hv);
        g.fill_round(r, rad, mix(lit, p.background, alpha_scale::kPrimaryPressDim * pv));
        fg = p.on_accent;
    } else if (kind == ButtonKind::DangerPrimary) {
        // Kip penceresinde yikici islemin ana dugmesi (spec 2026-11-12 E): hata rengiyle dolu, parlamasiz. Etiket, dolgu
        // ustunde kontrasti daha yuksek olan renkle (arka plan ya da ana metin) yazilir.
        const Color lit = mix(p.error, p.text_primary, alpha_scale::kPrimaryHoverLift * hv);
        const Color fill = mix(lit, p.background, alpha_scale::kPrimaryPressDim * pv);
        g.fill_round(r, rad, fill);
        fg = contrast_ratio(p.background, fill) >= contrast_ratio(p.text_primary, fill) ? p.background : p.text_primary;
    } else {
        // Ikincil / tehlike / onerilen: cerceveli. Hover'da cerceve + metin vurgu rengine kayar; basinca hafif dolgu.
        const Color tone = kind == ButtonKind::Danger ? p.error : p.accent;
        const bool tinted = kind != ButtonKind::Secondary;  // tehlike/onerilen: ton surekli
        const float k = tinted ? 1.f : std::min(1.f, hv + pv);
        // .btn-secondary: beyaz %3 zemin (yuksek kontrastta duz yuzey).
        const Color base = pal_ == &theme_.dark ? mix(p.surface, Color{1, 1, 1, 1}, 0.03f) : p.surface;
        g.fill_round(r, rad, mix(base, tone, alpha_scale::kChipTint * std::min(1.f, (tinted ? 0.5f : 0.f) + hv * 0.5f + pv)));
        // .btn-secondary cizgisi: beyaz %12 (Panel dugmeleriyle ayni); tehlike / onerilen / hover'da ton cizgisi.
        const Color edge = pal_ == &theme_.dark && kind == ButtonKind::Secondary ? mix(p.surface, Color{1, 1, 1, 1}, 0.12f) : p.border;
        g.stroke_round(r, rad, mix(edge, tone, k));
        fg = mix(p.text_primary, tone, k);
    }
    if (!label.empty()) g.text(button_role(kind, enabled, r.h), label, r, fg, {Align::Center, false, true});
    if (s.focus_visible && s.focus == id) focus_ring(g, r, rad, hits);
    return fg;
}

float Screen::chip_width(Graphics& g, std::wstring_view text) const {
    // Siki yerlesim: chip ic boslugu her yanda 8 DIP; yukseklik Secondary satiri (18).
    return std::ceil(g.measure(Role::Secondary, text, 1000.f, false).w) + 2 * theme_.space(1);
}

float Screen::chip(Graphics& g, Point at, std::wstring_view text, ChipTone tone) const {
    const float w = chip_width(g, text);
    chip(g, {at.x, at.y, w, theme_.role(Role::Secondary).line}, text, tone);
    return w;
}

void Screen::chip(Graphics& g, const RectF& r, std::wstring_view text, ChipTone tone) const {
    const ChipColors c = chip_colors(pal(), tone);
    g.fill_round(r, theme_.radius_control, c.bg);
    g.text(Role::Secondary, text, r, c.fg, {Align::Center, false, true});
}

void Screen::draw_card(Graphics& g, const RectF& r, bool danger, float radius) const {
    draw_card_face(g, r, danger, radius);
    log_card(r);
}

void Screen::draw_card_face(Graphics& g, const RectF& r, bool danger, float radius) const {
    const auto& p = pal();
    // Spec §4: kartlarda parlama yok. Tehlike karti: hata tonlu zemin + cizgi (geri alinamaz islem / hata).
    // Enstruman imzasi (yalniz koyu tema; yuksek kontrastta ve tehlike kartinda yok; BOOST maketi .card-v2): golge
    // (0 10px 26px), 170 derecelik yuzey gradyani, yumusak kenar cizgisi (metin disi >= 3:1), ust kenarda ic isik cizgisi
    // ve sol-ust (vurgu) / sag-alt (camgobegi) 7 DIP kose yaylari (::before / ::after). Kart kutusu (card_log_) ve isabet
    // kutulari degismez; golge kutusu bolgesel cizim icin kaydedilir (shadow_log_).
    const bool signature = pal_ == &theme_.dark && !danger && r.w >= 48.f && r.h >= 32.f;
    const float rad = radius >= 0.f ? radius : theme_.radius_card;
    if (!signature) {
        g.fill_round(r, rad, danger ? mix(p.background, p.error, alpha_scale::kDangerCardFill) : p.surface);
        g.stroke_round(r, rad, danger ? mix(p.surface, p.error, alpha_scale::kDangerCardBorder) : p.border);
        return;
    }
    card_shadow(g, r);
    shadow_log_.push_back(r);
    // linear-gradient(170deg): CSS acisi (0 = yukari, saat yonu); gradyan cizgisinin boyu |w sin| + |h cos|.
    constexpr float kDeg = 3.14159265f / 180.f;
    const float sn = std::sin(depth::kCardAngleDeg * kDeg), cs = -std::cos(depth::kCardAngleDeg * kDeg);
    const float half = (std::fabs(r.w * sn) + std::fabs(r.h * cs)) / 2;
    const float mx = r.x + r.w / 2, my = r.y + r.h / 2;
    const GradientStop face[] = {{0.f, depth::card_top(p)}, {1.f, depth::card_bottom(p)}};
    g.fill_linear_round(r, rad, {mx - sn * half, my - cs * half}, {mx + sn * half, my + cs * half}, face);
    g.stroke_round(r, rad, signature_card_border(p));
    // inset 0 1px 0: kenarligin hemen altindaki satir (yuvarlak koseler arasinda).
    if (r.w > 2 * rad + 2.f) g.fill({r.x + rad, r.y + 1.f, r.w - 2 * rad, 1.f}, depth::kCardInsetTop);
    // ::before / ::after: 7x7 kutu (kart kenar kutusunun kosesinde), yalniz ust+sol / alt+sag kenar 1.5 DIP; radius 9
    // kutuya sigdirilir (7) -> ceyrek yay, duz uclu.
    const float k = depth::kCornerBox, th = depth::kCornerStroke;
    g.arc(r.x + k, r.y + k, k - th / 2, th, 180.f, 90.f, p.accent.with_alpha(depth::kCornerOpacity), false);
    g.arc(r.right() - k, r.bottom() - k, k - th / 2, th, 0.f, 90.f, p.cyan.with_alpha(depth::kCornerOpacity), false);
}

void Screen::card_shadow(Graphics& g, const RectF& r) const {
    g.soft_shadow(r, theme_.radius_card, 0.f, depth::kShadowDy, depth::kShadowBlur, depth::kShadow);
}

void Screen::replay_shadows(Graphics& g, const UiState& s, std::size_t from, std::size_t to) const {
    to = std::min(to, s.shadow_casters.size());
    for (std::size_t i = from; i < to; ++i) card_shadow(g, s.shadow_casters[i]);
}

void Screen::paint_backdrop(Graphics& g, const RectF& area) const {
    // Pencere zemini: duz arka plan rengi. BOOST maketinde iki eliptik isilti (radial-gradient) <body> uzerindedir, yani
    // uygulama penceresinin (#main-window-shell, zemin var(--bg)) DISINDA kalir; pencere ici duzdur (maket 2026-11-16
    // birebir). Bolge kesin (kenarsiz) kirpilir; taban tum pencereyi doldurur ki bolgesel cizim tam kareyle ayni olsun.
    if (area.w <= 0 || area.h <= 0) return;
    const auto& p = pal();
    const float W = backdrop_w_ > 0 ? backdrop_w_ : area.right(), H = backdrop_h_ > 0 ? backdrop_h_ : area.bottom();
    g.push_clip(area);
    g.fill({0, 0, std::max(W, area.right()), std::max(H, area.bottom())}, p.background);
    g.pop_clip();
}

void Screen::hero_glow(Graphics& g, const RectF& card, float cx, float cy, float on) const {
    // #hero-glow: 320 x 144 hap, blur(64px) (sigma 64). Gauss bulanikligi ayrisabilir: eksen boyunca profil
    // 0.5 [erf((L/2 - d) / (s v2)) + erf((L/2 + d) / (s v2))]. Elips yaricapi L/2 + 2s (profil ~%3); duraklar iki eksenin
    // ortalama profilinden. Kart icine (padding kutusu; overflow-hidden) kirpilir; tepe alfasi rengin alfasi x merkez profili.
    if (pal_ != &theme_.dark || card.w <= 2.f || card.h <= 2.f) return;
    const auto& p = pal();
    using namespace depth;
    const float s2 = kHeroGlowBlur * 1.41421356f;
    const auto prof = [&](float len, float d) { return 0.5f * (std::erf((len / 2 - d) / s2) + std::erf((len / 2 + d) / s2)); };
    const float rx = kHeroGlowW / 2 + 2 * kHeroGlowBlur, ry = kHeroGlowH / 2 + 2 * kHeroGlowBlur;
    const float px0 = prof(kHeroGlowW, 0), py0 = prof(kHeroGlowH, 0);
    const float o = std::clamp(on, 0.f, 1.f);
    const Color base = mix(p.error, p.accent, o);
    const float peak = (kHeroGlowOff + (kHeroGlowOn - kHeroGlowOff) * o) * px0 * py0;
    constexpr int kStops = 9;
    GradientStop st[kStops];
    for (int i = 0; i < kStops; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kStops - 1);
        const float f = i == kStops - 1 ? 0.f : 0.5f * (prof(kHeroGlowW, t * rx) / px0 + prof(kHeroGlowH, t * ry) / py0);
        st[i] = {t, base.with_alpha(peak * f)};
    }
    const RectF clip = card.inset(1.f);
    const float x0 = std::max(clip.x, std::floor(cx - rx)), y0 = std::max(clip.y, std::floor(cy - ry));
    const float x1 = std::min(clip.right(), std::ceil(cx + rx)), y1 = std::min(clip.bottom(), std::ceil(cy + ry));
    if (x1 <= x0 || y1 <= y0) return;
    g.push_clip(clip);
    g.fill_radial({x0, y0, x1 - x0, y1 - y0}, {cx, cy}, rx, ry, st);
    g.pop_clip();
}

void Screen::draw_well(Graphics& g, const RectF& r, bool highlight) const {
    // Gomulu telemetri yuvasi (02 // BOOST .well-v2): kart icinde / kartsiz bolumde degerleri cevreleyen koyu cukur.
    // log_card cagirmaz (kart simetri / cakisma denetimlerine girmez); isabet kutusu yok. Yuksek kontrastta yalniz cizgi.
    if (r.w <= 0.f || r.h <= 0.f) return;
    const auto& p = pal();
    if (pal_ == &theme_.dark) {
        using namespace depth;
        constexpr float kHighlight = 0.55f;
        const float rad = std::min(kWellRadius, std::min(r.w, r.h) / 2);
        // Zemin (kenar kutusu) + nokta dokusu (padding kutusuna hizali 10 DIP karolar).
        g.fill_round(r, rad, kWellFill);
        g.fill_dots(r.inset(1.f), std::max(rad - 1.f, 0.f), kWellPitch, kWellDotR, kWellDot);
        // inset 0 2px 8px rgba(0,0,0,.65), padding kutusuna kirpili: kenardan d derinlikte a * Phi(-d / sigma)
        // (es merkezli 1 DIP cizgiler) + ustte 2 DIP kaymanin ek payi (dikey gradyan seridi).
        const RectF in = r.inset(1.f);
        const float sigma = kWellInsetBlur / 2;
        const auto phi_neg = [&](float x) { return 0.5f * std::erfc(x / 1.41421356f); };  // Phi(-x)
        for (int k = 0; k < 6; ++k) {
            const RectF ring = in.inset(static_cast<float>(k));
            if (ring.w <= 2.f || ring.h <= 2.f) break;
            g.stroke_round(ring, std::max(rad - 1.f - static_cast<float>(k), 0.f), Color{0, 0, 0, kWellInsetA * phi_neg((static_cast<float>(k) + 0.5f) / sigma)});
        }
        const float band_h = std::min(2 * sigma + kWellInsetDy, in.h);
        if (in.w > 2 * rad && band_h > 0) {
            const auto extra = [&](float d) { return kWellInsetA * (phi_neg((d - kWellInsetDy) / sigma) - phi_neg(d / sigma)); };
            const GradientStop st[] = {{0.f, {0, 0, 0, extra(0.f)}},
                                       {0.25f, {0, 0, 0, extra(band_h * 0.25f)}},
                                       {0.5f, {0, 0, 0, extra(band_h * 0.5f)}},
                                       {0.75f, {0, 0, 0, extra(band_h * 0.75f)}},
                                       {1.f, {0, 0, 0, 0}}};
            g.fill_linear_round({in.x + rad - 1.f, in.y, in.w - 2 * (rad - 1.f), band_h}, 0.f, {0, in.y}, {0, in.y + band_h}, st);
        }
        g.stroke_round(r, rad, highlight ? p.cyan.with_alpha(kHighlight) : kWellBorder);
    } else {
        g.stroke_round(r, theme_.radius_control, highlight ? p.focus_ring : p.border);
    }
}

// Maket dugme yazisi: .btn-primary 12px/600 (28 DIP satir ici), .btn-secondary 12px/500,
// .btn-danger 12px/600. Kip penceresinin yikici ana dugmesi (DangerPrimary) onceki Body rolunde kalir.
Role Screen::button_role(ButtonKind kind, bool enabled, float h) const {
    if (kind == ButtonKind::DangerPrimary) return Role::Body;
    if (!enabled) return Role::Medium12;
    // 32 DIP'lik (satir disi) ana dugme Body'de kalir: ui_snapshot BUTTON_RULE ink ortalama siniri (1,5 DIP) 1,5x DPI'da
    // 13px/600 css tabanli satirla asilir. Satir ici ana dugme 12px/600.
    if (kind == ButtonKind::Primary) return h >= theme_.button_h - 0.5f ? Role::Body : Role::Semi12;
    if (kind == ButtonKind::Danger) return Role::Semi12;
    return Role::Medium12;
}

RectF Screen::button_rect(Graphics& g, float x, float y, std::wstring_view label, bool in_row) const {
    // Siki yerlesim (spec 2026-11-11 §2): dugme 32 DIP (satir ici 28), asgari genislik 72; ic bosluk 16 (satir ici 12).
    // Olcu, ayni dugmenin en genis olasi yazisiyla (kalin) alinir: etiket hicbir turde tasmaz.
    const float lw = g.measure(in_row ? Role::Semi12 : Role::Body, label, 1000.f, false).w;
    const float pad = in_row ? theme_.space(2) : theme_.space(3);
    return {x, y, std::max(theme_.button_min_w, std::ceil(lw) + 2 * pad), in_row ? theme_.button_row_h : theme_.button_h};
}

float Screen::button_group_w(Graphics& g, std::initializer_list<std::wstring_view> labels, bool in_row) const {
    float w = 0;
    for (const auto label : labels) w = std::max(w, button_rect(g, 0, 0, label, in_row).w);
    return w;
}

namespace {
// Maket .btn-primary / .btn-secondary olculeri (Tailwind px = DIP).
constexpr float kPanelPrimaryH = 32.f, kPanelPrimaryPad = 16.f, kPanelQuietH = 28.f, kPanelQuietPad = 12.f, kPlayIcon = 14.f, kPlayGap = 6.f;
}  // namespace

RectF Screen::panel_button_rect(Graphics& g, std::wstring_view label, bool primary, bool play, bool compact) const {
    if (pal_ != &theme_.dark) {
        RectF r = button_rect(g, 0, 0, label, !primary || compact);
        if (compact) r.h = theme_.button_row_h;
        return r;
    }
    const bool big = primary && !compact;
    const float lw = std::ceil(g.measure(big ? Role::Semi13 : primary ? Role::Semi12 : Role::Medium12, label, 1000.f, false).w);
    const float pad = big ? kPanelPrimaryPad : kPanelQuietPad;
    // Yazi olcegiyle (EF_TEXT_SCALE) dugme de buyur: satir yuksekligi farki kadar (with_text_scale kurali).
    const float grow = theme_.button_h - default_theme().button_h;
    return {0, 0, lw + 2 * pad + (play ? kPlayIcon + kPlayGap : 0.f), (big ? kPanelPrimaryH : kPanelQuietH) + grow};
}

float Screen::panel_button(Graphics& g, float x, float y, Hit id, std::wstring_view label, bool primary, UiState& s, std::int64_t now,
                           HitMap& hits, bool enabled, bool play, bool compact, float min_w) {
    RectF r = panel_button_rect(g, label, primary, play, compact);
    r.x = x, r.y = y, r.w = std::max(r.w, min_w);
    if (pal_ != &theme_.dark) {
        button_face(g, r, id, label, primary ? ButtonKind::Primary : ButtonKind::Secondary, enabled, s, now, hits);
        hits.add(id, r, std::wstring(label), AccRole::Button, false, -1, enabled);
        return r.w;
    }
    const auto& p = pal();
    const float hv = enabled ? hover_value(s, id, now) : 0.f;
    const float pv = enabled ? press_value(s, id, now) : 0.f;
    Color fg = p.text_primary;
    float rad = theme_.radius_control;
    if (primary) {
        rad = depth::kPanelPrimaryRadius;
        if (enabled) {
            using namespace depth;
            if (glow_allowed(s) && pv < 0.5f)
                g.soft_shadow(r, rad, 0.f, 0.f, kBtnGlowBlur + (kBtnGlowBlurHover - kBtnGlowBlur) * hv, mix(kBtnGlow, kBtnGlowHover, hv));
            const float bright = 1.f + (kBtnHoverBright - 1.f) * hv;
            const auto shade = [&](const Color& c) {
                const Color lit{std::min(c.r * bright, 1.f), std::min(c.g * bright, 1.f), std::min(c.b * bright, 1.f), 1.f};
                return mix(lit, p.background, alpha_scale::kPrimaryPressDim * pv);
            };
            const GradientStop st[] = {{0.f, shade(kBtnTop)}, {1.f, shade(kBtnBottom)}};
            g.fill_linear_round(r, rad, {r.x, r.y}, {r.x, r.bottom()}, st);
            if (r.w > 2 * rad) g.fill({r.x + rad, r.y, r.w - 2 * rad, 1.f}, Color{1, 1, 1, kBtnInset + (kBtnInsetHover - kBtnInset) * hv});
            fg = p.on_accent;
        } else {
            g.fill_round(r, rad, mix(p.surface, p.surface_raised, alpha_scale::kDisabled));
            g.stroke_round(r, rad, p.border);
            fg = p.text_secondary;
        }
    } else {
        // .btn-secondary: beyaz %3 zemin + beyaz %12 cizgi; hover / basma: vurgu cizgisi + metin, vurgu %8 zemin.
        const float k = std::min(1.f, hv + pv);
        g.fill_round(r, rad, k > 0.f ? mix(depth::kQuietFill, p.accent.with_alpha(0.08f + 0.08f * pv), k) : depth::kQuietFill);
        g.stroke_round(r.inset(0.5f), rad, enabled ? mix(depth::kQuietBorder, p.accent, k) : depth::kQuietBorder);
        fg = enabled ? mix(p.text_primary, p.accent, k) : p.text_secondary;
    }
    const Role role = primary ? (compact ? Role::Semi12 : Role::Semi13) : Role::Medium12;
    if (play) {
        const float lw = std::ceil(g.measure(role, label, 1000.f, false).w);
        // Simge + etiket grubu ortali; murekkep ortali kalsin diye (dugme kurali B7) ucgenin sol boslugu (8/24) ile
        // etiketin sag yan boslugu farkinin yarisi kadar sola kaydirilir.
        const float x0 = r.x + (r.w - (kPlayIcon + kPlayGap + lw)) / 2 - (8.f / 24.f * kPlayIcon - 1.f) / 2;
        // Oynat ucgeni: maket SVG'si (viewBox 24: M8 5v14l11-7z), 14 DIP kutuda.
        const float k = kPlayIcon / 24.f, oy = r.y + (r.h - kPlayIcon) / 2;
        const Point tri[] = {{x0 + 8 * k, oy + 5 * k}, {x0 + 8 * k, oy + 19 * k}, {x0 + 19 * k, oy + 12 * k}};
        g.fill_polygon(tri, fg);
        g.text(role, label, {x0 + kPlayIcon + kPlayGap, r.y, lw + 1.f, r.h}, fg, {Align::Leading, false, true});
    } else {
        g.text(role, label, r, fg, {Align::Center, false, true});
    }
    if (s.focus_visible && s.focus == id) focus_ring(g, r, rad, hits);
    hits.add(id, r, std::wstring(label), AccRole::Button, false, -1, enabled);
    return r.w;
}

float Screen::badge_button_w(Graphics& g, std::wstring_view label) const {
    return std::ceil(g.measure(Role::Medium11, label, 1000.f, false).w) + 2 * 8.f + 2.f;
}

void Screen::badge_button(Graphics& g, const RectF& r, Hit id, std::wstring_view label, const Color& tone, bool enabled, UiState& s,
                          std::int64_t now, HitMap& hits) {
    // .quick-fix-btn: px-2 (8) + 1 DIP cizgi, 11px/500, ton %15 zemin (hover %25) + %30 cizgi, yaricap 4. Yukseklik
    // dokunma hedefi (24; maket 22.5). Yuksek kontrastta sistem cizgisi + metin.
    const auto& p = pal();
    const bool dark = pal_ == &theme_.dark;
    const float hv = enabled ? hover_value(s, id, now) : 0.f;
    const float pv = enabled ? press_value(s, id, now) : 0.f;
    constexpr float kRad = 4.f;
    const Color c = enabled ? tone : p.text_secondary;
    if (dark) {
        g.fill_round(r, kRad, c.with_alpha(0.15f + 0.10f * std::min(1.f, hv + pv)));
        g.stroke_round(r.inset(0.5f), kRad, c.with_alpha(0.30f + 0.30f * hv));
    } else {
        g.stroke_round(r, kRad, enabled ? mix(p.border, p.accent, hv) : p.border);
    }
    g.text(Role::Medium11, label, r, dark ? c : (enabled ? p.text_primary : p.text_secondary), {Align::Center, false, true});
    if (s.focus_visible && s.focus == id) focus_ring(g, r, kRad, hits);
    hits.add(id, r, std::wstring(label), AccRole::Button, false, -1, enabled);
}

void Screen::hero_conduits(Graphics& g, const RectF& card, bool on) const {
    // Maket: <svg viewBox="0 0 1100 130" preserveAspectRatio="none"> kartin ic kutusuna (kenarlik ici) yayilir; hatlar
    // izgaranin (yuvalar ve dugme) altinda kalir. Acikken vurgu rengi (maketteki kesikli nabiz animasyonu yerine sabit;
    // yeni zamanlayici yok).
    if (pal_ != &theme_.dark) return;
    const auto& p = pal();
    const RectF in = card.inset(1.f);
    const float kx = in.w / depth::kConduitViewW, ky = in.h / depth::kConduitViewH;
    const auto at = [&](float x, float y) { return Point{in.x + x * kx, in.y + y * ky}; };
    const Color c = on ? p.accent.with_alpha(0.65f) : p.error.with_alpha(0.28f);
    const Point l[] = {at(430, 65), at(365, 65), at(350, 50), at(330, 50)};
    const Point r[] = {at(670, 65), at(735, 65), at(750, 80), at(770, 80)};
    g.polyline(l, depth::kConduitW, c);
    g.polyline(r, depth::kConduitW, c);
    g.fill_circle(l[3].x, l[3].y, depth::kConduitDotR, p.cyan.with_alpha(0.6f));
    g.fill_circle(r[3].x, r[3].y, depth::kConduitDotR, p.accent.with_alpha(0.6f));
}

void Screen::power_button(Graphics& g, float cx, float cy, const PerfView& pv, UiState& s, std::int64_t now, bool enabled, HitMap& hits,
                          const std::wstring& name, const std::wstring& help) {
    // Yuvarlak Performans dugmesi (spec 2026-11-12 B.1). Acikken vurgu rengi halka + parlama + yavas nefes alan dis
    // halka; kapaliyken hata rengi (kirmizi) halka ve kalp atisi (cift vurus + disa acilip sonen halka: acmaya tesvik
    // eder). Hareket kapaliyken (breathe_live = false) halka sabittir. Acik/kapali gecisi toggle_knob ile (azaltilmis
    // harekette aninda): halka tepeden saat yonunde vurgu rengine doner. Durum yalniz renge dayanmaz: altinda
    // AÇIK/KAPALI metni ve UIA Toggle durumu vardir. Is surerken / onay acikken soluk ama UIA'da kalir (DS-05). Yuksek
    // kontrastta parlama / nefes / kalp atisi yok; halka ve simge sistem renkleriyle. Kalp atisi yalniz cizimi
    // olcekler: isabet kutusu ve odak halkasi sabit kalir.
    const auto& p = pal();
    const bool dark = pal_ == &theme_.dark;
    const float d = theme_.power_button_d, r = d / 2;
    const RectF box{cx - r, cy - r, d, d};
    const float on = std::clamp(s.toggle_knob.value(now, theme_.easing), 0.f, 1.f);
    const float hv = enabled ? hover_value(s, Hit::Toggle, now) : 0.f;
    const float pr = enabled ? press_value(s, Hit::Toggle, now) : 0.f;
    const float dim = enabled ? 1.f : alpha_scale::kDisabled;
    const float thick = theme_.power_ring_thick;
    // Bilesen geometrisi (token degil): simge yaricapi / cizgisi, dis halkanin dugmeye uzakligi ve opakligi.
    constexpr float kGlyphR = 0.19f, kGlyphW = 3.5f, kOuterW = 1.5f;
    constexpr float kOuterNear = 4.f, kOuterFar = 9.f, kOuterAlphaNear = 0.5f, kOuterAlphaFar = 0.12f, kOuterRest = 0.3f;
    // Kalp atisi: yalniz Kapali + etkin + hareket acikken (breathe_live; yuksek kontrastta hic). k: cizim olcegi.
    HeartbeatFrame beat;
    if (on < 0.01f && enabled && s.breathe_live) beat = heartbeat_frame(now, theme_.motion.heartbeat_period);
    const float k = beat.scale;
    if (dark) {
        // BOOST maketi (#btn-power-circle, 92 DIP; SVG viewBox 96 -> olcek u = d / 96): #080C13 disk (hover #101724),
        // r 45u noktali halka (beyaz %14, cizgi 2.5u, kesik 1.5u / 6.35u, tepeden), r 40u iz (beyaz %7, cizgi 4u) ve durum
        // halkasi (Kapali hata, Acik vurgu; gecis toggle_knob ile tepeden saat yonunde), 32 DIP kutuda guc simgesi
        // (viewBox 40: yay r 11, cizgi 3.2; ustte 79 derecelik bosluk, sap y 7 -> 18.5). #power-pulse-ring: 108'lik
        // kapsayicida inset 6 (r 48), 1.5 DIP; Kapali: kalp atisiyla 0.96 -> 1.2 olcekte acilip soner (opaklik .5 -> 0),
        // Acik: 1.03 -> 1.14 nefes (opaklik .45 -> .14). Hepsi parlama payinin (12) icinde: hizli yol ayni pikselleri yazar.
        constexpr float kPulseR = 48.f, kPulseW = 1.5f;
        const float u = d / 96.f;
        if (on > 0.01f && enabled) {
            const float w = s.breathe_live ? breathe_wave(now, theme_.motion.breathe_period) : kOuterRest;
            g.arc(cx, cy, kPulseR * (1.03f + 0.11f * w), kPulseW, 0.f, 360.f, p.accent.with_alpha((0.45f + (0.14f - 0.45f) * w) * on));
        } else if (beat.ring < 1.f) {
            g.arc(cx, cy, kPulseR * (0.96f + 0.24f * beat.ring), kPulseW, 0.f, 360.f, p.error.with_alpha(0.5f * (1.f - beat.ring)));
        }
        Color disc = mix(depth::kPowerDisc, depth::kPowerDiscHover, hv);
        disc = mix(disc, p.background, alpha_scale::kPrimaryPressDim * pr);
        g.fill_circle(cx, cy, r * k, disc);
        // Noktali halka: 36 kesik (cevre 2 pi 45 / 7.85), her biri 1.5 birim yay, duz uclu.
        {
            constexpr float kPi = 3.14159265f;
            const float rr = 45.f * u * k, dash_deg = 1.5f / 45.f * 180.f / kPi, step_deg = 7.85f / 45.f * 180.f / kPi;
            const Color dc = depth::kDialDots.with_alpha(depth::kDialDots.a * dim);
            for (float a0 = -90.f; a0 < 270.f - 0.01f; a0 += step_deg) g.arc(cx, cy, rr, 2.5f * u * k, a0, dash_deg, dc, false);
        }
        const float ring_r = 40.f * u * k, ring_w = 4.f * u * k;
        g.arc(cx, cy, ring_r, ring_w, 0.f, 360.f, depth::kDialTrack.with_alpha(depth::kDialTrack.a * dim));
        g.ring(cx, cy, ring_r, ring_w, on, mix(p.error, p.text_primary, hv * alpha_scale::kPressTint).with_alpha(dim), p.accent.with_alpha(dim));
        const float gk = 32.f / 40.f * k;  // simge kutusu 32 DIP, viewBox 40
        const Color glyph = mix(mix(p.error, p.text_primary, hv * alpha_scale::kHoverTint), p.accent, on).with_alpha(dim);
        g.arc(cx, cy, 11.f * gk, 3.2f * gk, -50.53f, 281.06f, glyph);
        const Point stem[] = {{cx, cy - 13.f * gk}, {cx, cy - 1.5f * gk}};
        g.polyline(stem, 3.2f * gk, glyph);
        if (s.focus_visible && s.focus == Hit::Toggle) focus_ring(g, box, r, hits);
        hits.add(Hit::Toggle, box, name, AccRole::ToggleSwitch, false, pv.switch_on ? 1 : 0, enabled);
        hits.meta[Hit::Toggle].help = help;
        return;
    }
    if (on > 0.01f && enabled) {
        // Dis halka: acikken yavas nefes alir (breathe_live); hareket kapaliyken sabit durur.
        const float w = s.breathe_live ? breathe_wave(now, theme_.motion.breathe_period) : kOuterRest;
        const float a = dark ? (kOuterAlphaNear + (kOuterAlphaFar - kOuterAlphaNear) * w) * on : on;
        g.arc(cx, cy, r + kOuterNear + (kOuterFar - kOuterNear) * w, kOuterW, 0.f, 360.f, p.accent.with_alpha(a));
        if (on > 0.5f && glow_allowed(s) && pr < 0.5f) g.glow_round(box, r, p.accent, alpha_scale::kPrimaryGlowIdle * on);
    } else if (beat.ring < 1.f) {
        // Vurusla birlikte dugmenin kenarindan disa acilip sonen halka (hata rengi).
        g.arc(cx, cy, r + kOuterFar * beat.ring, kOuterW, 0.f, 360.f, p.error.with_alpha(kOuterAlphaNear * (1.f - beat.ring)));
    }
    // Kadran olcegi (yalniz koyu tema): dugmenin hemen disinda 8 yonlu kisa centik. Parlama payinin (kHalo = 12) icinde
    // kalir: nefes hizli yolu (paint_power_region) ayni fonksiyonu cizdigi icin tam cizimle ayni pikseller cikar.
    if (dark) {
        constexpr float kTickIn = 1.8f, kTickOut = 3.6f, kTickW = 1.2f, kTickMix = 0.35f, kTickAlpha = 0.32f;
        constexpr float kStep = 0.785398163f;  // 45 derece
        const Color tick_c = mix(p.border, on > 0.5f ? p.accent : p.error, kTickMix).with_alpha(kTickAlpha * dim);
        const float r_in = (r + kTickIn) * k, r_out = (r + kTickOut) * k;
        for (int i = 0; i < 8; ++i) {
            const float cs = std::cos(kStep * static_cast<float>(i)), sn = std::sin(kStep * static_cast<float>(i));
            const Point tick[] = {{cx + cs * r_in, cy + sn * r_in}, {cx + cs * r_out, cy + sn * r_out}};
            g.polyline(tick, kTickW, tick_c);
        }
    }
    // Govde: yuzey; acikken hafif vurgu tonu, hover'da kabarir, basinca koyulasir.
    Color body = mix(mix(p.surface, p.surface_raised, hv), p.accent, dark ? alpha_scale::kChipTint * on : 0.f);
    body = mix(body, p.background, alpha_scale::kPrimaryPressDim * pr);
    g.fill_circle(cx, cy, r * k, body);
    // Kapali halka: hata rengi (spec B.1: kirmizi; hareket kapaliyken de sabit kirmizi). Yuksek kontrastta sistem cizgisi.
    const Color off = dark ? p.error : p.border;
    g.ring(cx, cy, (r - thick / 2) * k, thick, on, mix(off, p.text_primary, hv * alpha_scale::kPressTint).with_alpha(dim), p.accent.with_alpha(dim));
    // Guc simgesi: ustu acik yay + dikey cizgi.
    const float gr = d * kGlyphR * k;
    const Color glyph = (dark ? mix(mix(off, p.text_primary, hv * alpha_scale::kHoverTint), p.accent, on) : p.text_primary).with_alpha(dim);
    g.arc(cx, cy, gr, kGlyphW, -60.f, 300.f, glyph);
    const Point stem[] = {{cx, cy - gr * 1.3f}, {cx, cy - gr * 0.25f}};
    g.polyline(stem, kGlyphW, glyph);
    if (s.focus_visible && s.focus == Hit::Toggle) focus_ring(g, box, r, hits);
    hits.add(Hit::Toggle, box, name, AccRole::ToggleSwitch, false, pv.switch_on ? 1 : 0, enabled);
    hits.meta[Hit::Toggle].help = help;
}

float Screen::groups_panel(Graphics& g, float x, float y, float w, const PerfView& pv, UiState& s, std::int64_t now, bool editable,
                           bool measure_only, HitMap* hits, bool admin_without_uac) {
    const auto& p = pal();
    const float tog_w = theme_.toggle_w, tog_h = theme_.toggle_h;
    // Siki yerlesim: genis seritte gruplar iki sutun (soldan saga, yukaridan asagi: grup sirasi = okuma sirasi);
    // anahtar kendi sutununun saginda kalir, metinden uzaga dusmez. Dar alanda tek sutun. Sutunlar arasinda dikey
    // ayirici cizgi durur: sol sutunun anahtari sag sutunun basligina ait gibi okunmaz (onay akisinda yanlis ayari
    // kapatma riski).
    constexpr float kGroupColMinW = 320.f;
    const float col_gap = 2 * theme_.space(3);
    const int cols = w >= 2 * kGroupColMinW + col_gap ? 2 : 1;
    const float cw = cols == 2 ? (w - col_gap) / 2 : w;
    const float text_w = std::max(cw - tog_w - theme_.space(3), 1.f);
    s.group_count = pv.groups.size();
    if (!measure_only) s.group_enabled.assign(pv.groups.size(), false);
    // Onay asamasinda sade: yalniz degisecek gruplar, aciklamasiz (Evet/Hayir kucuk pencerede de gorunur kalsin).
    const bool compact = pv.phase == Phase::Confirming;
    const float row_gap = compact ? theme_.space(1) : theme_.space(2);
    struct Item {
        std::size_t index = 0;
        std::wstring title, desc;
        StringId st_id = StringId::PerfGroupUnknown;
        Color st_c;
        // T04: kanit / risk cipleri. Durum satirina sigarsa ayni satirda; sigmazsa Dusuk risk cipi atilir, yine sigmazsa ayri satir.
        std::wstring ev_txt, rk_txt;
        ChipTone ev_tone = ChipTone::Neutral, rk_tone = ChipTone::Neutral;
        float ev_w = 0, rk_w = 0, st_w = 0;
        bool chips_own_line = false;
        bool adv = false;
        float tl = 0, dl = 0, h = 0;
    };
    const float sl = theme_.role(Role::Secondary).line;
    const float chip_gap = theme_.space(1);
    std::vector<Item> items;
    for (std::size_t i = 0; i < pv.groups.size(); ++i) {
        const auto& gr = pv.groups[i];
        if (compact && !gr.will_change) continue;
        Item it;
        it.index = i;
        it.adv = gr.advanced || gr.no_effect;  // P4: "Fark yok" olculen gruplar da katli satirin altinda (uygulanmaya devam eder)
        it.title = tk(gr.key);
        // EF-X25 T03: onayda aciklama yerine yalniz uygulanacak alanlar (alan adi olan gruplarda; digerleri sade kalir).
        if (!compact) it.desc = tk(core::service::admin_copy_key(gr.desc_key, admin_without_uac));
        else
            for (const auto& f : gr.field_keys) {
                if (!it.desc.empty()) it.desc += L"\n";
                it.desc += tk(f);
            }
        // P4: olculmus "Fark yok" notu (yalniz bu PC, Arena testi, birlikte olculen gruplar). Etki ya da FPS vaadi yok.
        if (gr.no_effect) it.desc = it.desc.empty() ? t(StringId::PlanNoEffectNote) : t(StringId::PlanNoEffectNote) + L"\n" + it.desc;
        // P2: arka plan programi satirlari (gercek, olculmus). Panelde ve onayda gorunur.
        for (const auto& d : gr.details) {
            if (!it.desc.empty()) it.desc += L"\n";
            if (gr.key == core::perf::kGpuRouteGroup) {  // T01: yalniz oyun adi; olculmus CPU/RAM degeri yok
                it.desc += replace_all(tk("plan.field.gpu_route"), L"{name}", widen(d.name));
                continue;
            }
            if (gr.key == core::perf::kDebloatAppsGroup) {  // T13: yalniz uygulama adi; olculmus deger yok
                it.desc += replace_all(tk("plan.field.debloat_app"), L"{name}", widen(d.name));
                continue;
            }
            const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
            it.desc += replace_all(replace_all(replace_all(tk("plan.field.background"), L"{name}", widen(d.name)), L"{cpu}",
                                               widen(core::measure::format_number(d.cpu_pct, 1, tr))),
                                   L"{ram}", widen(core::measure::format_number(static_cast<double>(d.ram_bytes) / 1048576.0, 0, tr)));
        }
        it.st_c = p.text_secondary;
        if (gr.known) {
            if (!gr.will_change) it.st_id = StringId::PerfGroupOk, it.st_c = p.success;
            else if (!gr.selected) it.st_id = StringId::PerfGroupSkipped, it.st_c = p.warning;
            else it.st_id = StringId::PerfGroupWillChange, it.st_c = p.accent;
        }
        // T04: cipler (renk tek basina anlam tasimaz: metin her zaman yazili).
        it.ev_txt = tk(core::perf::evidence_key(gr.evidence));
        it.ev_tone = gr.evidence == core::perf::Evidence::Measured   ? ChipTone::Good
                     : gr.evidence == core::perf::Evidence::Disputed ? ChipTone::Warn
                                                                     : ChipTone::Neutral;
        it.rk_txt = tk(core::perf::risk_key(gr.risk));
        it.rk_tone = gr.risk == core::perf::Risk::High     ? ChipTone::Danger
                     : gr.risk == core::perf::Risk::Medium ? ChipTone::Warn
                                                           : ChipTone::Neutral;
        it.ev_w = chip_width(g, it.ev_txt);
        it.rk_w = chip_width(g, it.rk_txt);
        it.st_w = std::ceil(g.measure(Role::Secondary, t(it.st_id), text_w, false).w);
        const auto fits = [&](bool with_risk) { return it.st_w + chip_gap + it.ev_w + (with_risk ? chip_gap + it.rk_w : 0.f) <= text_w; };
        if (!fits(true)) {
            if (gr.risk == core::perf::Risk::Low) it.rk_txt.clear();  // Dusuk risk cipi atilir (yalniz Orta/Yuksek gosterilir)
            if (!fits(!it.rk_txt.empty())) it.chips_own_line = true;  // yine sigmadiysa kendi satirinda
        }
        it.tl = std::max(g.measure(Role::Body, it.title, text_w).h, theme_.role(Role::Body).line);
        it.dl = it.desc.empty() ? 0.f : g.measure(Role::Secondary, it.desc, text_w).h;
        it.h = std::max(it.tl + sl + (it.chips_own_line ? sl : 0.f) + (it.desc.empty() ? 0.f : theme_.space(0) + it.dl), tog_h);
        items.push_back(std::move(it));
    }
    // T04 + P4: ana gruplar once; varsayilani Hayir ve acikca secilmemis gruplar ile bu PC'de "Fark yok" olculen gruplar
    // "Ayrintilar (N)" satirinin altinda (katli).
    std::vector<std::size_t> main_idx, adv_idx;
    for (std::size_t k = 0; k < items.size(); ++k) (items[k].adv ? adv_idx : main_idx).push_back(k);
    if (!measure_only) {
        s.group_advanced.assign(pv.groups.size(), false);
        for (const auto k : adv_idx) s.group_advanced[items[k].index] = true;
        s.group_advanced_count = adv_idx.size();
    }
    float total = 0;
    const auto lay = [&](const std::vector<std::size_t>& idx) {
        for (std::size_t k = 0; k < idx.size(); k += static_cast<std::size_t>(cols)) {
            float row_h = 0;
            for (std::size_t c = 0; c < static_cast<std::size_t>(cols) && k + c < idx.size(); ++c) row_h = std::max(row_h, items[idx[k + c]].h);
            if (!measure_only) {
                for (std::size_t c = 0; c < static_cast<std::size_t>(cols) && k + c < idx.size(); ++c) {
                    const auto& it = items[idx[k + c]];
                    const auto& gr = pv.groups[it.index];
                    const float cx = x + static_cast<float>(c) * (cw + col_gap);
                    const float cy = y + total;
                    g.text(Role::Body, it.title, {cx, cy, text_w, it.tl}, p.text_primary);
                    g.text(Role::Secondary, t(it.st_id), {cx, cy + it.tl, text_w, sl}, it.st_c, {Align::Leading, false});
                    // Cipler: durum metninin hemen sagi (sigmazsa altindaki satir).
                    const float chx = it.chips_own_line ? cx : cx + it.st_w + chip_gap;
                    const float chy = it.chips_own_line ? cy + it.tl + sl : cy + it.tl;
                    chip(g, Point{chx, chy}, it.ev_txt, it.ev_tone);
                    if (!it.rk_txt.empty()) chip(g, Point{chx + it.ev_w + chip_gap, chy}, it.rk_txt, it.rk_tone);
                    const float dy = cy + it.tl + sl + (it.chips_own_line ? sl : 0.f) + theme_.space(0);
                    if (!it.desc.empty()) g.text(Role::Secondary, it.desc, {cx, dy, text_w, it.dl}, p.text_secondary);
                    const bool can = editable && gr.known && gr.will_change;
                    if (gr.will_change) {
                        const Hit id = hit_of(HitKind::Group, it.index);
                        small_toggle(g, {cx + cw - tog_w, cy + 2, tog_w, tog_h}, gr.selected, can, id, it.title, s, now, *hits);
                        s.group_enabled[it.index] = can;
                    }
                }
            }
            total += row_h + row_gap;
        }
    };
    lay(main_idx);
    if (!measure_only && cols == 2 && main_idx.size() > 1)
        g.fill({std::floor(x + cw + col_gap / 2), y, 1, total - row_gap}, mix(p.hero, p.border, alpha_scale::kDivider));
    if (!adv_idx.empty()) {
        // "Ayrintilar (N)" satiri (T04 + P4): yalniz gorunum durumu (acik/kapali kaydedilmez); UIA Expander.
        const float row_h = theme_.button_row_h;
        const bool open = s.advanced_open;
        if (!measure_only) {
            const auto label = replace_all(t(StringId::PlanAdvanced), L"{n}", std::to_wstring(adv_idx.size())) + (open ? L" ▴" : L" ▾");
            const float lw = std::min(std::ceil(g.measure(Role::Medium12, label, 1000.f, false).w) + 2 * theme_.space(1), w);
            const RectF lr{x, y + total, lw, row_h};
            const float hv = hover_value(s, Hit::PerfAdvanced, now);
            const float press = press_value(s, Hit::PerfAdvanced, now);
            g.fill_round(lr, theme_.radius_control, mix(p.surface, p.text_primary, 0.05f + 0.05f * std::min(1.f, hv + press)));
            g.text(Role::Medium12, label, lr, mix(p.text_primary, p.accent, std::min(1.f, hv + press)), {Align::Center, false, true});
            if (s.focus_visible && s.focus == Hit::PerfAdvanced) focus_ring(g, lr, theme_.radius_control, *hits);
            hits->add(Hit::PerfAdvanced, lr, label, AccRole::Expander, open);
        }
        total += row_h + row_gap;
        if (open) {
            const float before = total;
            lay(adv_idx);
            if (!measure_only && cols == 2 && adv_idx.size() > 1)
                g.fill({std::floor(x + cw + col_gap / 2), y + before, 1, total - row_gap - before}, mix(p.hero, p.border, alpha_scale::kDivider));
        }
    }
    return total <= 0 ? 0.f : total - row_gap;
}

float Screen::perf_card(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits,
                        StatusSeq& seq) {
    // Performans bolumu (spec 2026-11-12 B.1): kartsiz, ortali. Ortada yuvarlak dugme; altinda "PERFORMANS AÇIK" (vurgu
    // rengi) / "KAPALI" (hata rengi), tek kisa satir ("N ayar · geri alinir"; kapaliyken "Acmak icin bas") ve "Ayrinti"
    // baglantisi; solunda SON TEST FPS ve %1 DUSUK,
    // saginda HAZIRLIK ve "Testi baslat" (dar pencerede yan gruplar dugmenin altina iner). Ayrinti paneli, onay
    // (Evet/Hayir), ilerleme ve kurtarma icerigi bolumun altinda kart olarak acilir; akis ve isabet kimlikleri degismez.
    // Yonlendirme cumleleri (A) cizilmez: ekran okuyucuda dugmenin yardim metninde durur.
    const auto& p = pal();
    const auto& e = theme_.easing;
    const auto& pv = vm.perf;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const float pad = theme_.card_pad;
    const float gap = theme_.space(2);
    const float d = theme_.power_button_d;
    const float label_h = theme_.role(Role::Label).line;
    const float hero_h = theme_.role(Role::StateTitle).line;  // kahraman sayilar 20 DIP
    const float sec_h = theme_.role(Role::Secondary).line;
    constexpr float kUnbounded = 10000.f;
    constexpr float kHalo = 12.f;      // dugmenin cevresinde parlama / nefes halkasi icin bos birakilan pay
    constexpr float kTextMaxW = 560.f; // dugmenin altindaki metin sutunu (ortali)

    StringId title_id = StringId::PerformanceOff;
    std::wstring hint;
    Color title_color = p.text_primary;
    switch (pv.phase) {
        case Phase::Off: hint = t(StringId::PanelPerfOffLine); break;  // "Acmak icin bas"; uzun cumle UIA yardim metninde
        case Phase::Planning: hint = t(StringId::PerfPlanning); break;
        case Phase::Confirming: break;
        case Phase::Applying: title_id = StringId::PerfApplying; break;
        case Phase::On:
            title_id = StringId::PerformanceOn;
            hint = replace_all(t(StringId::PanelPerfOnLine), L"{count}", std::to_wstring(pv.active));
            break;
        case Phase::ConfirmRestore: title_id = StringId::PerformanceOn; break;
        case Phase::Restoring: title_id = StringId::PerfRestoring; break;
        case Phase::RecoveryNeeded:
            title_id = StringId::RecoveryPending;
            title_color = p.error;
            hint = t(StringId::PerfRecoveryHint);  // islem sonucu uyarisi: kalir
            // Saha 2026-11-15: hangi ayar, hangi nedenle (journal'dan; kullanicidan istenecek destek paketiyle ayni bilgi).
            if (const auto items = recovery_items_text(pv.recovery_items, [this](std::string_view k) { return tk(k); }); !items.empty())
                hint += L"\n" + items;
            break;
    }
    // Acilis uzlastirmasi: hangi ayarlar gercekte kapali (hedef okunup bakildi; kayit bayragi degil).
    if (pv.notice == Notice::SomeOff && (pv.phase == Phase::On || pv.phase == Phase::Off)) {
        if (const auto items = recovery_items_text(pv.recovery_items, [this](std::string_view k) { return tk(k); }, 3, "perf.off_items"); !items.empty()) {
            if (!hint.empty()) hint += L"\n";
            hint += items;
        }
    }
    // T18: tamamlanma ozeti; sayi Outcome'dan (bu oturumda uygulanip geri okunarak dogrulanan), tahmin degil.
    if (pv.phase == Phase::On && pv.verified > 0) {
        if (!hint.empty()) hint += L"\n";
        hint += replace_all(t(StringId::PerfProgressSummary), L"{k}", std::to_wstring(pv.verified));
    }
    // OPT-10: HAGS gibi ayarlar yeniden baslatmadan sonra etkin olur; bu durum gizlenmez.
    if (pv.reboot_pending && (pv.phase == Phase::On || pv.phase == Phase::Off)) {
        if (!hint.empty()) hint += L"\n";
        hint += t(StringId::PerfRebootPending);
    }
    std::wstring notice;
    Color notice_color = p.warning;
    switch (pv.notice) {
        case Notice::NoChanges: notice = t(StringId::StateNoChanges); notice_color = p.text_secondary; break;
        case Notice::Denied: notice = t(StringId::SupportDenied); break;
        case Notice::FailedRestored: notice = t(StringId::PerfFailedRestored); break;
        case Notice::PlanFailed: notice = t(StringId::PerfPlanFailed); break;
        case Notice::NoneSelected: notice = t(StringId::PerfNoneSelected); notice_color = p.text_secondary; break;
        case Notice::PlanChanged: notice = t(StringId::PerfPlanChanged); break;
        case Notice::PartialApplied: notice = t(StringId::PerfPartialApplied); break;
        case Notice::AdminSkipped: notice = t(StringId::PerfAdminSkipped); break;
        case Notice::RestorePointFailed: notice = t(StringId::PerfRestorePointFailed); break;
        case Notice::AdminBusy: notice = t(core::service::admin_copy(StringId::PerfAdminBusy, vm.admin_without_uac)); break;
        case Notice::GameBlocked: notice = t(StringId::PerfGameBlocked); break;
        case Notice::SomeOff: if (pv.drift_n == 0) notice = t(StringId::PerfSomeOff); break;  // sapma karti ayni bilgiyi verir
        case Notice::DriftFailed: notice = t(StringId::PerfDriftFailed); break;
        case Notice::LegacyUnmigrated: notice = t(StringId::PerfLegacyUnmigrated); break;
        case Notice::RestoreFailed:
        case Notice::None: break;
    }
    const auto title = t(title_id);  // UIA dugme adi (onceki davranis); gorunur baslik yalniz ara asamalarda
    const bool show_title = pv.phase == Phase::Applying || pv.phase == Phase::Restoring || pv.phase == Phase::RecoveryNeeded;

    // Panel: Kapali/Acik'ta "Ayrinti" ile acilir; onay asamasinda hep acik.
    const bool panel_toggle = pv.phase == Phase::Off || pv.phase == Phase::On;
    const bool panel_open = pv.phase == Phase::Confirming || (panel_toggle && s.details_open);
    const bool editable = pv.phase == Phase::Off || pv.phase == Phase::Confirming;
    const float inner_w = std::max(area.w - 2 * pad, 1.f);  // alttaki kartin ic genisligi
    const float panel_h = panel_open ? groups_panel(g, 0, 0, inner_w, pv, s, now, editable, true, nullptr, vm.admin_without_uac) : 0.f;

    // --- Kahraman kart (sahip onayli maket 2026-11-16, "02 // BOOST"): solda SON TEST FPS | %1 DUSUK yuvasi, ortada
    // yuvarlak dugme, saginda HAZIRLIK + "Testi baslat" yuvasi; yuvalari dugmeye ince enerji hatlari baglar (yalniz koyu
    // tema). Yalniz kayitli gercek olcum ve gercek tarama bulgulari; veri yoksa "—" (sayi uydurulmaz). Ek satirlar
    // (onceki karsilastirilabilir teste gore fark + taban, %1 dusuk kare suresi, son testin kaynagi) yalniz gercek veri
    // varken ve yalniz genis duzende cizilir; bilgi UIA adinda da durur. Sayilar sayfaya girince / deger degisince sayac
    // gibi akar (motion.count; hareket kapali, oyun acik ya da olcum surerken aninda); UIA adi her zaman gercek degerdir.
    // Kart zemini yuzey rengidir (nefes hizli yolu paint_power_region ayni rengi doldurur); kart kaydina (card_log_)
    // girmez: bolum eskiden oldugu gibi kart simetri denetimlerinin disindadir (acilan alt kart kayitlidir).
    const bool dark = pal_ == &theme_.dark;
    const int count_ms = effective_ms(theme_.motion.count, s.reduced_motion || s.motion_paused || vm.game_gate_blocked);
    const auto counted = [&](std::size_t i, double target) {
        auto& tw = s.panel_num[i];
        const float tf = static_cast<float>(target);
        if (!s.panel_num_seen[i]) {
            tw = Tween(count_ms > 0 ? 0.f : tf);
            tw.retarget(tf, now, count_ms);
            s.panel_num_seen[i] = true;
        } else if (std::fabs(tf - tw.target()) > 1e-3f) {
            tw.retarget(tf, now, count_ms);
        }
        return tw.active(now) ? static_cast<double>(tw.value(now, e)) : target;
    };
    const auto one_w = [&](Role role, std::wstring_view text, bool tabular = false) {
        return text.empty() ? 0.f : std::ceil(g.measure(role, text, kUnbounded, false, tabular).w) + 1.f;
    };
    const auto pct_txt = [&](double v) {
        const auto n = widen(core::measure::format_number(v, 0, tr));
        return tr ? L"%" + n : n + L"%";
    };
    struct Stat {
        std::wstring label, value, a11y;
        Color c;
        float w = 0;      // sade genislik: etiket / deger (dar duzen)
        float vw = 0;     // deger genisligi (gercek deger ve sayac ara degerinin genisi)
    };
    Stat stats[3];
    const core::measure::ResultView* last_run = nullptr;  // FPS olcen en son test (ritim seridi icin; yoksa cizilmez)
    std::wstring delta_txt, base_txt, ms_txt, adv_txt, test_line;
    std::wstring real_out[3] = {L"—", L"—", L"—"};  // BOOST genis duzen: gercek deger genisligi (sayac akarken sabit)
    std::wstring base_num, base_title;              // BAZ satirinin parcalari (sayi ayri renkte)
    std::optional<double> smooth_pct;               // %1 dusuk / ortalama (yalniz ikisi de olculduyse)
    Color delta_c = p.text_secondary;
    const auto sc = readiness_score(vm.readiness);
    const bool scored = !vm.readiness.checks.empty() && sc.total > 0;
    {
        const auto& list = vm.measure.history;
        stats[0].label = t(StringId::OverviewStripFps);
        stats[1].label = t(StringId::OverviewStripLow1);
        stats[2].label = t(StringId::OverviewStripReadiness);
        stats[0].value = stats[1].value = stats[2].value = L"—";
        stats[0].c = stats[1].c = stats[2].c = p.text_secondary;
        std::wstring real[3] = {L"—", L"—", L"—"};  // UIA: gercek deger (sayac ara degeri okunmaz)
        const auto number = [&](std::size_t i, const contracts::Measurement& m) {
            if (!m.has_value()) {
                s.panel_num_seen[i] = false;
                return;
            }
            real[i] = widen(core::measure::format_number(*m.value(), 1, tr));
            stats[i].value = widen(core::measure::format_number(counted(i, *m.value()), 1, tr));
        };
        // Denge kaydi tek bir FPS degeri tasimaz (sonucu alttaki "Darbogaz" kartindadir): bolum, FPS olcen en son testi
        // gosterir. Yalniz denge kaydi varsa deger "—" kalir (sayi uydurulmaz).
        const auto last = std::find_if(list.begin(), list.end(), [](const core::measure::ResultView& r) { return !r.balance; });
        if (last != list.end()) {
            const auto& r = *last;
            last_run = &r;
            number(0, r.avg_fps);
            number(1, r.low1_fps);
            // Renk rolu (F): kare hizi veri rengi (camgobegi), yesil degil. Gecersiz/eski kosu: uyari tonu + UIA adinda neden.
            if (r.avg_fps.has_value()) stats[0].c = r.run_valid ? p.data_gpu : p.warning;
            if (r.low1_fps.has_value()) stats[1].c = p.text_primary;
            // UIA: bolumde yalniz sayi gorunur; kaynak, baslik ve olcumun siniri (yerlesik sahne oyun FPS'i degildir vb.)
            // adin devaminda okunur. Ayrintili sonuc Olcumler sayfasindadir.
            namespace cm = core::measure;
            const auto source = t(r.source == cm::TestSource::Builtin  ? StringId::TestBuiltin
                                  : r.source == cm::TestSource::InGame ? StringId::TestGame
                                                                       : StringId::TestEndurance);
            auto test_title = result_title(r);
            if (test_title.empty()) test_title = result_headline(r);
            stats[0].a11y = test_title.empty() ? source : source + L" · " + test_title;
            stats[0].a11y = a11y_join(std::move(stats[0].a11y), t(r.source == cm::TestSource::Builtin  ? StringId::OverviewLastTestLimitBuiltin
                                                                  : r.source == cm::TestSource::InGame ? StringId::OverviewLastTestLimitGame
                                                                                                       : StringId::OverviewLastTestLimitEndurance));
            if (!r.run_valid) stats[0].a11y = a11y_join(std::move(stats[0].a11y), t(StringId::OverviewLastTestInvalid));
            // Son testi tanimlayan kisa satir ("Testi baslat" altinda): kaynak + kayitli baslik (ic kimlik gosterilmez).
            const auto shown_title = result_title(r);
            test_line = shown_title.empty() ? source : source + L" · " + shown_title;
            // Fark + taban: yalniz gosterilen kayit Olcumler'deki karsilastirmanin kaydiysa ve ortak politika
            // karsilastirilabilir dediyse (ayni kaynak + kosul izi + surumler). Taban, olculen fark yuzdesinden geri
            // hesaplanir (fark = (yeni - taban) / taban); tahmin yoktur.
            const auto& cmp = vm.measure.comparison;
            if (r.avg_fps.has_value() && r.source != cm::TestSource::Endurance && vm.measure.result &&
                vm.measure.result->when_unix_ms == r.when_unix_ms && !vm.measure.result->balance && cmp.available && cmp.comparable &&
                cmp.delta_percent && std::isfinite(*cmp.delta_percent) && *cmp.delta_percent > -100.0) {
                const double cur = *r.avg_fps.value();
                const double dpct = *cmp.delta_percent;
                const double base = cur / (1.0 + dpct / 100.0);
                if (std::isfinite(base) && base > 0) {
                    const double diff = std::round(std::fabs(cur - base) * 10.0) / 10.0;
                    const double dp = std::round(std::fabs(dpct));
                    const bool flat = diff == 0.0;
                    delta_txt = std::wstring(flat ? L"" : dpct > 0 ? L"+" : L"−") + widen(core::measure::format_number(diff, 1, tr)) + L" (" + pct_txt(dp) + L")";
                    delta_c = flat ? p.text_secondary : dpct > 0 ? p.success : p.error;
                    const double shown = std::round(std::fabs(dpct) * 10.0) / 10.0;
                    const auto pnum = widen(core::measure::format_number(shown, 1, tr));
                    const auto acc = replace_all(t(shown == 0.0 ? StringId::MeasureResultDeltaFlatAcc
                                                   : dpct > 0   ? StringId::MeasureResultDeltaUpAcc
                                                                : StringId::MeasureResultDeltaDownAcc),
                                                 L"{pct}", tr ? L"%" + pnum : pnum + L"%");
                    base_num = widen(core::measure::format_number(base, 1, tr));
                    base_title = shown_title;
                    base_txt = replace_all(t(StringId::PanelHeroBase), L"{fps}", base_num);
                    if (!shown_title.empty()) base_txt += L" · " + shown_title;
                    stats[0].a11y = a11y_join(a11y_join(std::move(stats[0].a11y), acc), base_txt);
                }
            }
            // Akicilik (maket "%81 AKICI"): olculen %1 dusuk / olculen ortalama FPS; ikisi de gecerliyse.
            if (r.low1_fps.has_value() && r.avg_fps.has_value() && *r.avg_fps.value() > 0 && std::isfinite(*r.low1_fps.value()) &&
                std::isfinite(*r.avg_fps.value()) && *r.low1_fps.value() >= 0)
                smooth_pct = std::clamp(std::round(*r.low1_fps.value() / *r.avg_fps.value() * 100.0), 0.0, 100.0);
            // %1 dusuk degerinin kare suresi karsiligi (1000 / FPS): gosterilen sayinin birim donusumu.
            if (r.low1_fps.has_value() && *r.low1_fps.value() > 0 && std::isfinite(*r.low1_fps.value())) {
                const auto v = widen(core::measure::format_number(1000.0 / *r.low1_fps.value(), 1, tr));
                ms_txt = replace_all(t(StringId::PanelHeroFrameMs), L"{v}", v);
                stats[1].a11y = replace_all(t(StringId::PanelHeroFrameMsAcc), L"{v}", v);
            }
        } else {
            s.panel_num_seen[0] = s.panel_num_seen[1] = false;
            if (list.empty()) stats[0].a11y = t(StringId::OverviewNoTestTitle);  // "Henuz test yapilmadi"
        }
        if (scored) {
            const auto good = static_cast<int>(std::floor(counted(2, sc.good) + 0.5));
            real[2] = std::to_wstring(sc.good) + L"/" + std::to_wstring(sc.total);
            stats[2].value = std::to_wstring(good) + L"/" + std::to_wstring(sc.total);
            stats[2].c = p.text_primary;
            if (sc.advice > 0) {
                adv_txt = replace_all(t(StringId::ReadinessLineAdvice), L"{n}", std::to_wstring(sc.advice));
                stats[2].a11y = adv_txt;
            }
        } else {
            s.panel_num_seen[2] = false;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            auto& st = stats[i];
            st.a11y = a11y_join(st.label + L" " + real[i], st.a11y);
            // Genislik gercek degere gore: sayac akarken yerlesim oynamaz.
            st.vw = std::max(one_w(Role::StateTitle, real[i], true), one_w(Role::StateTitle, st.value, true));
            st.w = std::max(one_w(Role::Label, st.label), st.vw);
            real_out[i] = real[i];
        }
    }
    const float stat_gap = theme_.space(4);
    const bool can_start = core::measure::can_start(vm.measure) && vm.measure.source == core::measure::TestSource::Builtin;
    const auto start_label = t(StringId::MeasureStart);
    const RectF start_r = button_rect(g, 0, 0, start_label);
    const float side_h = std::max(label_h + hero_h, start_r.h);

    // Teknik dizin etiketleri (maket: "A1 //", "02 //", "A2 //"); cevrilmez, yalniz gorsel siradir.
    const std::wstring idx_fps = L"A1 //", idx_perf = L"02 //", idx_rdy = L"A2 //";
    const float idx_gap = theme_.space(0);
    const float idx_fps_w = one_w(Role::Label, idx_fps), idx_rdy_w = one_w(Role::Label, idx_rdy), idx_perf_w = one_w(Role::Label, idx_perf);
    const Color idx_cyan = dark ? p.cyan : p.text_secondary, idx_green = dark ? p.accent : p.text_secondary;
    // Ayrinti duzeyi: Full = etiket + deger + ek satir (fark / taban, kare suresi, oneri sayisi, son test satiri); Compact =
    // etiket + deger + mini serit. Ek satirlarin bilgisi UIA adinda her iki duzeyde de vardir.
    // Olculer maketin HTML kaynagindan (Tailwind px = DIP): yuva p-3 (12), %1 DUSUK sutunu min-w 122, ritim seridi h-2.5
    // (10) ve 3 DIP aralik, hazirlik parcasi 12x6 / 4 DIP aralik, kart p-3.5 (14), sutun araligi gap-4 (16).
    constexpr float kStripRowH = 12.f, kStripMaxW = 122.f, kRdySeg = 12.f, kRdySegGap = 4.f;
    constexpr float kWellPadX = 12.f, kWellPadY = 12.f;
    const float rdy_strip_w = scored ? static_cast<float>(sc.total) * (kRdySeg + kRdySegGap) - kRdySegGap : 0.f;
    const float delta_w = one_w(Role::Secondary, delta_txt, true), base_w = one_w(Role::Secondary, base_txt);
    const float ms_w = one_w(Role::Secondary, ms_txt, true), adv_w = one_w(Role::Secondary, adv_txt);
    const float fps_full_w = std::max({idx_fps_w + idx_gap + one_w(Role::Label, stats[0].label), stats[0].vw + (delta_w > 0 ? theme_.space(1) + delta_w : 0.f), base_w});
    const float low_full_w = std::max({one_w(Role::Label, stats[1].label), stats[1].vw + (ms_w > 0 ? theme_.space(0) + ms_w : 0.f), last_run ? kStripMaxW : 0.f});
    const float rdy_full_w = std::max({idx_rdy_w + idx_gap + one_w(Role::Label, stats[2].label), stats[2].vw + (adv_w > 0 ? theme_.space(1) + adv_w : 0.f), rdy_strip_w});
    const float fps_cmp_w = std::max(stats[0].w, idx_fps_w + idx_gap + one_w(Role::Label, stats[0].label));
    const float low_cmp_w = stats[1].w;
    const float rdy_cmp_w = std::max(stats[2].w, idx_rdy_w + idx_gap + one_w(Role::Label, stats[2].label));
    const bool has_extra_row = !base_txt.empty() || (last_run && last_run->fps_series.size() >= 2) || scored;
    const float test_w = std::min(one_w(Role::Secondary, test_line), 220.f);
    const auto block_h = [&](bool full) { return label_h + hero_h + (full ? (has_extra_row ? sec_h : 0.f) : kStripRowH); };
    const auto right_col_h = [&](bool full) { return full && !test_line.empty() ? start_r.h + theme_.space(0) + sec_h : start_r.h; };
    const auto left_w_of = [&](bool full) { return (full ? fps_full_w + low_full_w : fps_cmp_w + low_cmp_w) + stat_gap; };
    const auto right_w_of = [&](bool full) { return (full ? rdy_full_w + std::max(start_r.w, test_line.empty() ? 0.f : test_w) : rdy_cmp_w + start_r.w) + stat_gap; };

    // --- Metin sutunu: durum (02 // PERFORMANS + AÇIK/KAPALI), (ara asama basligi), kisa satir + "Ayrinti", bildirim.
    constexpr float hero_pad = 14.f;  // maket: card-v2 p-3.5
    const float inner_hero = std::max(area.w - 2 * hero_pad, 1.f);
    const float cx = area.x + area.w / 2;
    const auto perf_label = t(StringId::OverviewPerfLabel);
    const auto word = t(pv.switch_on ? StringId::OverviewPerfStateOn : StringId::OverviewPerfStateOff);
    const float plw = std::ceil(g.measure(Role::Label, perf_label, kUnbounded, false).w);
    const float ww = std::ceil(g.measure(Role::StateTitle, word, kUnbounded, false).w);
    const auto link_label = t(StringId::OverviewPerfDetailsShort) + (s.details_open ? L" ▴" : L" ▾");
    const float link_w = panel_toggle ? std::ceil(g.measure(Role::Secondary, link_label, kUnbounded, false).w) + 2 * theme_.space(1) : 0.f;
    const float link_h = theme_.touch_min;  // 24 DIP dokunma hedefi (WCAG 2.5.8)
    const float hint_one_w = hint.empty() ? 0.f : std::ceil(g.measure(Role::Secondary, hint, kUnbounded, false).w);
    const bool hint_multiline = hint.find(L'\n') != std::wstring::npos;
    const float line_h = std::max(sec_h, panel_toggle ? link_h : 0.f);
    // Sutun olcumu: w genisliginde; sol hizali (genis duzen: dugmenin saginda) ya da ortali (dugmenin altinda).
    struct TextCol {
        float w = 0, th = 0, hint_h = 0, hh = 0, nh = 0, h = 0;
        bool hint_single = false, link_own_line = false;
    };
    const auto text_col = [&](float w, bool wide) {
        TextCol c;
        c.w = w;
        c.hint_single = !hint.empty() && !hint_multiline && hint_one_w + link_w <= w;
        c.hint_h = hint.empty() ? 0.f : c.hint_single ? sec_h : std::max(g.measure(Role::Secondary, hint, w).h, sec_h);
        c.link_own_line = panel_toggle && !c.hint_single;
        c.hh = hint.empty() ? (panel_toggle ? line_h : 0.f) : c.hint_single ? line_h : c.hint_h + (c.link_own_line ? line_h : 0.f);
        c.th = show_title ? std::max(g.measure(Role::Section, title, w).h, theme_.role(Role::Section).line) : 0.f;
        c.nh = notice.empty() ? 0.f : g.measure(Role::Secondary, notice, w).h;
        c.h = (wide ? label_h : 0.f) + hero_h + c.th + c.hh + (c.nh > 0 ? theme_.space(0) + c.nh : 0.f);
        return c;
    };

    // --- BOOST genis duzen (sahip onayli maket 2026-11-16, olculer HTML/CSS kaynagindan; Tailwind px = DIP): kart
    // p-3.5 + 1 DIP kenar (15), ic alan 12 sutunlu izgara gap-4 (16), uc es sutun (col-span-4). Sol sutun yuva (p-3):
    // SON TEST FPS blogu | 1 DIP x 44 ayirici | %1 DUSUK blogu (min-w 122), justify-between. Orta sutunda dugme (108'lik
    // kapsayici) + gap-4 + metin sutunu birlikte ortali. Sag sutun yuva: HAZIRLIK blogu | dugme + mono satir (items-end).
    // Satir yuksekligi en yuksek ogeden; yuvalar ve metin dikeyde ortali. Ara asamalar (uygulaniyor / kurtarma) ve sigmayan
    // pencerede eski duzenler (Side / Stacked) kullanilir.
    struct Boost {
        float col_w = 0, cap_h = 0, m10_h = 0, num_h = 0;
        float fps_w = 0, fps_h = 0, low_w = 0, low_h = 0, lw_h = 0;
        float rdy_w = 0, rdy_h = 0, rb_w = 0, rb_h = 0, rw_h = 0;
        float text_w = 0, text_h = 0, hint_w = 0, link_w = 0, title_h = 0, notice_h = 0;
        float vw[3] = {0, 0, 0}, delta_w = 0, ms_w = 0, smooth_w = 0, adv_w = 0, line_w = 0, base_w = 0;
        float idx_fps_w = 0, idx_rdy_w = 0, idx_perf_w = 0, cap_fps_w = 0, cap_low_w = 0, cap_rdy_w = 0, cap_perf_w = 0, word_w = 0;
        float seg_w = 0, h = 0;
        RectF btn;
        std::wstring smooth, line, link;
    } bm;
    constexpr float kBIn = 15.f, kBGap = 16.f, kBWellPad = 13.f, kBDot = 108.f, kBRowMin = 108.f, kBStrip = 10.f, kBDiv = 44.f;
    constexpr float kBSeg = 12.f, kBSegGap = 4.f, kBSegH = 6.f, kBLowMin = 122.f, kBLinkH = 24.f;
    const bool dark_ui = pal_ == &theme_.dark;
    bool boost = false;
    {
        const auto w1 = [&](Role role, std::wstring_view text) { return text.empty() ? 0.f : std::ceil(g.measure(role, text, kUnbounded, false).w) + 1.f; };
        bm.col_w = (area.w - 2 * kBIn - 2 * kBGap) / 3;
        bm.cap_h = theme_.role(Role::Caps11).line;
        bm.m10_h = theme_.role(Role::Mono10).line;
        bm.num_h = theme_.role(Role::Num22).line;
        const auto w1t = [&](std::wstring_view text) { return text.empty() ? 0.f : std::ceil(g.measure(Role::Num22, text, kUnbounded, false, true).w) + 1.f; };
        for (int i = 0; i < 3; ++i) bm.vw[i] = std::max(w1t(real_out[i]), w1t(stats[i].value));
        bm.idx_fps_w = w1(Role::Mono10, idx_fps);
        bm.idx_rdy_w = w1(Role::Mono10, idx_rdy);
        bm.idx_perf_w = w1(Role::Mono10, idx_perf);
        bm.cap_fps_w = w1(Role::Caps11, stats[0].label);
        bm.cap_low_w = w1(Role::Caps11, stats[1].label);
        bm.cap_rdy_w = w1(Role::Caps11, stats[2].label);
        bm.cap_perf_w = w1(Role::Caps11, t(StringId::OverviewPerfLabel));
        bm.word_w = w1(Role::Num22, t(pv.switch_on ? StringId::OverviewPerfStateOn : StringId::OverviewPerfStateOff));
        bm.delta_w = w1(Role::Mono11Semi, delta_txt);
        bm.ms_w = w1(Role::Mono10, ms_txt);
        if (smooth_pct) bm.smooth = replace_all(t(StringId::PanelHeroSmooth), L"{pct}", pct_txt(*smooth_pct));
        bm.smooth_w = w1(Role::Mono10, bm.smooth);
        bm.adv_w = w1(Role::Medium11, adv_txt);
        bm.base_w = base_txt.empty() ? 0.f : w1(Role::Mono10, replace_all(t(StringId::PanelHeroBase), L"{fps}", base_num));
        // SON TEST FPS blogu: etiket satiri, deger satiri (mt 2), BAZ satiri (mt 4; yalniz karsilastirilabilir taban varken).
        bm.fps_w = std::max({bm.idx_fps_w + 6.f + bm.cap_fps_w, bm.vw[0] + (bm.delta_w > 0 ? 8.f + bm.delta_w : 0.f), bm.base_w});
        bm.fps_h = bm.cap_h + 2.f + bm.num_h + (bm.base_w > 0 ? 4.f + bm.m10_h : 0.f);
        // %1 DUSUK blogu: etiket + akicilik, deger + kare suresi, ritim seridi (mt 6, h 10; yalniz kayitli seri varken).
        const bool strip = last_run && last_run->fps_series.size() >= 2;
        bm.low_w = std::max({kBLowMin, bm.cap_low_w + (bm.smooth_w > 0 ? 8.f + bm.smooth_w : 0.f), bm.vw[1] + (bm.ms_w > 0 ? 6.f + bm.ms_w : 0.f)});
        bm.low_h = bm.cap_h + 2.f + bm.num_h + (strip ? 6.f + kBStrip : 0.f);
        bm.lw_h = std::max(bm.fps_h, bm.low_h) + 2 * kBWellPad;
        // HAZIRLIK blogu ve sag blok (dugme + "D3D11 ARENA · 15 SN" yerine gercek ayar: kapsam · sure).
        bm.seg_w = scored ? static_cast<float>(sc.total) * (kBSeg + kBSegGap) - kBSegGap : 0.f;
        bm.rdy_w = std::max({bm.idx_rdy_w + 6.f + bm.cap_rdy_w, bm.vw[2] + (bm.adv_w > 0 ? 8.f + bm.adv_w : 0.f), bm.seg_w});
        bm.rdy_h = bm.cap_h + 2.f + bm.num_h + (scored ? 6.f + kBSegH : 0.f);
        {
            namespace cm = core::measure;
            const bool bal = vm.measure.scope == cm::BuiltinScope::Balance;
            const bool quick = vm.measure.mode == cm::TestMode::Quick;
            const auto chip = t(bal ? (quick ? StringId::MeasureModeBalanceQuickChip : StringId::MeasureModeBalanceFullChip)
                                    : (quick ? StringId::MeasureModeQuickChip : StringId::MeasureModeFullChip));
            bm.line = locale_upper(t(bal ? StringId::MeasureScopeBalance : StringId::MeasureScopeGeneral) + L" · " + chip, tr);
        }
        bm.line_w = w1(Role::Mono10, bm.line);
        bm.btn = panel_button_rect(g, start_label, true, true);
        bm.rb_w = std::max(bm.btn.w, bm.line_w);
        bm.rb_h = bm.btn.h + 6.f + bm.m10_h;
        bm.rw_h = std::max(bm.rdy_h, bm.rb_h) + 2 * kBWellPad;
        // Metin sutunu: 02 // PERFORMANS, AÇIK/KAPALI (mt 2), kisa satir + "Ayrinti" (mt 4; dokunma hedefi 24).
        bm.link = t(StringId::OverviewPerfDetailsShort) + (s.details_open ? L" ▴" : L" ▾");
        bm.hint_w = w1(Role::Small12, hint);
        bm.link_w = panel_toggle ? w1(Role::Medium12, bm.link) + 16.f : 0.f;
        bm.text_w = std::max({bm.idx_perf_w + 6.f + bm.cap_perf_w, bm.word_w, bm.hint_w + (bm.hint_w > 0 && bm.link_w > 0 ? 6.f : 0.f) + bm.link_w});
        const float text_max = bm.col_w - kBDot - kBGap;
        bm.title_h = show_title ? std::max(g.measure(Role::Title14, title, std::max(text_max, 1.f)).h, theme_.role(Role::Title14).line) : 0.f;
        bm.notice_h = notice.empty() ? 0.f : g.measure(Role::Small12, notice, std::max(text_max, 1.f)).h;
        if (show_title || !notice.empty()) bm.text_w = text_max;
        const bool hint_row = !hint.empty() || panel_toggle;
        bm.text_h = bm.cap_h + 2.f + bm.num_h + (hint_row ? 4.f + kBLinkH : 0.f) + (bm.title_h > 0 ? 4.f + bm.title_h : 0.f) +
                    (bm.notice_h > 0 ? 4.f + bm.notice_h : 0.f);
        const float lw_need = bm.fps_w + 12.f + 1.f + 12.f + bm.low_w + 2 * kBWellPad;
        const float rw_need = bm.rdy_w + 12.f + bm.rb_w + 2 * kBWellPad;
        boost = dark_ui && !hint_multiline && bm.col_w >= lw_need && bm.col_w >= rw_need && bm.text_w <= text_max;
        bm.h = 2 * kBIn + std::max({kBRowMin, bm.lw_h, bm.rw_h, bm.text_h});
    }

    // --- Duzen secimi: Wide (metin dugmenin saginda, yuvalar karti doldurur) -> Side (yuvalar dugmenin iki yaninda, metin
    // altinda; once tam, sigmazsa sade ayrinti) -> Stacked (sayilar ve "Testi baslat" metnin altinda, yuvasiz).
    enum class Layout { Wide, Side, Stacked };
    constexpr float kMinTrace = 16.f;   // maket gap-4: yuva ile dugme / metin arasi asgari pay (hat burada)
    constexpr float kTextGap = 8.f;     // dugmenin parlama payi ile metin sutunu arasi
    constexpr float kTextColMax = 250.f;
    constexpr float kEdgeY = 8.f;       // dugme bolgesi (power_region) ile kart kenari arasi
    const float halo_r = d / 2 + kHalo;
    const float side_room = inner_hero / 2 - halo_r;
    // Genis duzen maketteki gibi uc es sutundur (grid-cols-3, gap-4): yuvalar dis sutunlar; metin dugmenin saginda orta
    // sutunun ve sutun araliginin icinde kalir.
    const float col3_w = (inner_hero - 2 * kMinTrace) / 3;
    const float wide_tw_max = std::max(col3_w / 2 + kMinTrace - halo_r - kTextGap - 4.f, 1.f);
    const float wide_tw = std::min({kTextColMax, wide_tw_max, std::max({idx_perf_w + idx_gap + plw, ww, hint_multiline ? 0.f : hint_one_w + link_w, link_w})});
    const TextCol wide_col = text_col(std::max(wide_tw, 1.f), true);
    Layout layout = Layout::Stacked;
    bool full = false;
    if (col3_w >= left_w_of(true) + 2 * kWellPadX && col3_w >= right_w_of(true) + 2 * kWellPadX &&
        wide_tw >= std::max({idx_perf_w + idx_gap + plw, ww, link_w})) {
        layout = Layout::Wide, full = true;
    } else {
        const float room = side_room - kMinTrace;
        for (const bool f : {true, false})
            if (room >= left_w_of(f) + 2 * kWellPadX && room >= right_w_of(f) + 2 * kWellPadX) {
                layout = Layout::Side, full = f;
                break;
            }
    }
    const float text_w = std::min(inner_hero, kTextMaxW);
    const TextCol col = layout == Layout::Wide ? wide_col : text_col(text_w, false);
    const float bh = block_h(full);
    const float content_h = std::max(bh, layout == Layout::Stacked ? 0.f : right_col_h(full));
    const float well_h = content_h + 2 * kWellPadY;
    const float region_h = d + 2 * kHalo;
    // Dar duzen: uc sayi + dugme tek satirda, ortali; o da sigmazsa dugme sayilarin altina iner.
    const float stats_w = stats[0].w + stats[1].w + stats[2].w + 2 * stat_gap;
    const bool below_one_row = stats_w + stat_gap + start_r.w <= inner_hero;
    const float below_h = gap + (below_one_row ? side_h : label_h + hero_h + theme_.space(1) + start_r.h);
    float hero_h_total = 0, cy = 0;
    if (boost) {
        hero_h_total = bm.h;
        cy = area.y + bm.h / 2;
    } else switch (layout) {
        case Layout::Wide:
            hero_h_total = std::max({region_h + 2 * kEdgeY, well_h + 2 * theme_.space(2), col.h + 2 * theme_.space(2)});
            cy = area.y + hero_h_total / 2;
            break;
        case Layout::Side: {
            const float top_block = std::max(region_h, well_h);
            hero_h_total = kEdgeY + top_block + 2.f + col.h + theme_.space(2);
            cy = area.y + kEdgeY + top_block / 2;
            break;
        }
        case Layout::Stacked:
            hero_h_total = kEdgeY + region_h + 2.f + col.h + below_h + theme_.space(2);
            cy = area.y + kEdgeY + kHalo + d / 2;
            break;
    }
    const float section_h = hero_h_total;

    // --- Bolumun altindaki kart (ayrinti paneli, onay, ilerleme, kurtarma).
    std::wstring question;
    std::vector<std::wstring> impacts;
    std::vector<Color> impact_colors;
    bool buttons = false, retry = false, progress = false, drift = false;
    std::wstring prog_line, prog_phase;  // T18
    if (pv.phase == Phase::Confirming) {
        question = t(pv.drift_confirm ? StringId::PerfDriftConfirm : StringId::ActionApplyQuestion);
        auto add = [&](StringId id, Color c, std::size_t n = 0) {
            auto txt = t(id);
            if (n) txt = replace_all(std::move(txt), L"{n}", std::to_wstring(n));
            impacts.push_back(std::move(txt));
            impact_colors.push_back(c);
        };
        if (pv.quality) add(StringId::PerfImpactQuality, p.warning);
        if (pv.startup) add(StringId::PerfImpactStartup, p.warning, pv.startup);
        if (pv.reboot) add(StringId::PerfImpactReboot, p.warning);
        if (pv.admin) add(core::service::admin_copy(StringId::PerfImpactAdmin, vm.admin_without_uac), p.text_secondary);
        if (impacts.empty()) add(StringId::PerfImpactNone, p.text_secondary);
        if (pv.tailored) add(StringId::PlanTailoredNote, p.text_secondary);  // T18: varsayilan cihaza gore uyarlandi (sade not)
        buttons = true;
    } else if (pv.phase == Phase::ConfirmRestore) {
        question = t(StringId::ActionRestoreQuestion);
        impacts.push_back(t(StringId::PerfRestoreHint));
        impact_colors.push_back(p.text_secondary);
        buttons = true;
    } else if (pv.phase == Phase::Applying || pv.phase == Phase::Restoring) {
        progress = true;
        // T18: yalniz uygulamada; adim ve grup motorun gercek ilerlemesinden (sure tahmini yok).
        const int pct = pv.total ? static_cast<int>(pv.done * 100 / pv.total) : 0;
        const std::wstring num = std::to_wstring(pct);
        const std::wstring prog_pct = tr ? L"%" + num : num + L"%";
        std::wstring group_title;
        if (pv.phase == Phase::Applying && pv.progress_has_step) group_title = tk(pv.progress_group);
        if (!group_title.empty()) {
            auto st = replace_all(t(StringId::PerfProgressStep), L"{i}", std::to_wstring(pv.progress_step));
            st = replace_all(std::move(st), L"{n}", std::to_wstring(pv.progress_steps));
            st = replace_all(std::move(st), L"{group}", group_title);
            prog_line = prog_pct + L" · " + st;
        } else {
            auto st = replace_all(t(StringId::PerfStep), L"{d}", std::to_wstring(pv.done));
            st = replace_all(std::move(st), L"{t}", std::to_wstring(pv.total));
            prog_line = prog_pct + L" · " + st;
        }
        if (pv.phase == Phase::Applying) prog_phase = tk(core::perf::apply_phase_key(pv.progress_phase));
    } else if (pv.phase == Phase::RecoveryNeeded) {
        retry = true;
    } else if ((pv.phase == Phase::On || pv.phase == Phase::Off) && pv.drift_n > 0 && !panel_open) {
        // Koruma Modu (T07): Windows/baska program yazdigimiz degerleri geri cevirdi; sessiz uygulama yok.
        question = replace_all(t(pv.drift_update ? StringId::PerfDriftNoticeUpdate : StringId::PerfDriftNotice), L"{n}", std::to_wstring(pv.drift_n));
        drift = true;
    }
    // T20: basarili Acik sonrasi, "Ayrinti" paneli acikken panelin altinda katli "Oyun ici oneriler" satiri (en cok 4
    // sabit, sayisiz madde; FPS vaadi yok). Kapaliyken Panel'in tek ekran yuksekligi degismez (spec B.4 kabul olcutu).
    const bool tips = pv.phase == Phase::On && panel_open && panel_toggle;
    s.perf_tips_shown = tips;
    float detail_h = 0;
    std::vector<float> impact_h;
    if (panel_open && panel_toggle) detail_h += panel_h;
    if (!question.empty()) detail_h += g.measure(Role::Section, question, inner_w).h + gap;
    if (panel_open && pv.phase == Phase::Confirming) detail_h += panel_h + gap;
    for (const auto& i : impacts) {
        impact_h.push_back(g.measure(Role::Secondary, i, inner_w).h);
        detail_h += impact_h.back() + theme_.space(0);
    }
    if (buttons) detail_h += gap - theme_.space(0) + theme_.button_h;
    if (retry || drift) detail_h += theme_.button_h;
    // T20: oneri satiri (ac/kapa) ve acikken maddeler; ustundeki icerikle gap kadar ayrilir.
    const float tips_row_h = theme_.button_row_h;
    std::vector<float> tip_h;
    std::vector<std::wstring> tip_txt;
    float tips_y_off = 0.f;
    if (tips) {
        if (detail_h > 0) detail_h += gap;
        tips_y_off = detail_h;
        detail_h += tips_row_h;
        if (s.perf_tips_open)
            for (const auto k : core::games::perf_tip_keys()) {
                tip_txt.push_back(L"• " + tk(k));
                tip_h.push_back(g.measure(Role::Secondary, tip_txt.back(), inner_w).h);
                detail_h += theme_.space(0) + tip_h.back();
            }
    }
    float prog_line_h = 0, prog_phase_h = 0;
    if (progress) {
        prog_line_h = std::max(sec_h, g.measure(Role::Secondary, prog_line, inner_w).h);
        if (!prog_phase.empty()) prog_phase_h = std::max(sec_h, g.measure(Role::Secondary, prog_phase, inner_w).h);
        detail_h += 6 + theme_.space(1) + prog_line_h + prog_phase_h;
    }
    const bool has_detail = detail_h > 0;
    const float card_gap = theme_.space(1);
    const float total_h = section_h + (has_detail ? card_gap + pad + detail_h + pad : 0.f);

    // --- Cizim: kart zemini (kayitsiz; bkz. ust not), sonra dugme.
    const RectF hero_card{area.x, area.y, area.w, hero_h_total};
    draw_card_face(g, hero_card, false, depth::kPanelCardRadius);
    // #hero-glow: kartin ortasinda yumusak parlama (maket: left-1/2 top-1/2; Kapali hata, Acik vurgu tonu; gecis dugme
    // halkasiyla ayni tween).
    hero_glow(g, hero_card, hero_card.x + hero_card.w / 2, hero_card.y + hero_card.h / 2, s.toggle_knob.value(now, theme_.easing));
    // Genis duzende dugme orta sutunda metinle birlikte ortalanir; enerji hatlari dugmenin altinda kalir.
    const float in_x = area.x + kBIn;
    const float mid_x = in_x + bm.col_w + kBGap;
    const float group_x = mid_x + (bm.col_w - (kBDot + kBGap + bm.text_w)) / 2;
    const float pcx = boost ? group_x + kBDot / 2 : cx;
    if (boost) hero_conduits(g, hero_card, pv.switch_on);
    // Bolge yalniz tamami gorunur govdedeyken kaydedilir (nefes hizli yolu kirpilmis bolgeyi cizmez).
    {
        const RectF region{pcx - d / 2 - kHalo, cy - d / 2 - kHalo, d + 2 * kHalo, d + 2 * kHalo};
        if (region.y >= s.view_top + 4.f && region.bottom() <= s.view_bottom) {
            s.power_region = {region.x, region.y - s.scroll, region.w, region.h};
            s.power_card = {hero_card.x, hero_card.y - s.scroll, hero_card.w, hero_card.h};
            s.power_glow = true;
            s.power_conduits = boost;
        }
    }
    // Is surerken veya onay acikken etkisiz (soluk); dogrulanmadan "acik" gorunmez. Kaldirilan yonlendirme cumlesi
    // ekran okuyucuda yardim metnidir.
    power_button(g, pcx, cy, pv, s, now, panel_toggle, hits, title, pv.phase == Phase::Off ? t(StringId::OverviewPerfHintOff) : std::wstring());
    if (boost) {
        const float cap_dy = 0.f, m10_dy = (bm.cap_h - bm.m10_h) / 2;  // etiket satiri: items-center
        const auto base_dy = [&](Role a, Role b) { return g.baseline(a) - g.baseline(b); };  // items-baseline
        const Color idx_c_fps = p.cyan, idx_c_green = p.accent;
        const auto label_row = [&](const std::wstring& idx, float idx_w, const Color& ic, const std::wstring& label, float x, float y) {
            g.text(Role::Mono10, idx, {x, y + m10_dy, idx_w, bm.m10_h}, ic, {Align::Leading, false});
            g.text(Role::Caps11, label, {x + idx_w + 6.f - 1.f, y + cap_dy, std::ceil(g.measure(Role::Caps11, label, kUnbounded, false).w) + 1.f, bm.cap_h},
                   p.text_secondary, {Align::Leading, false});
        };
        // --- Sol yuva: SON TEST FPS | ayirici | %1 DUSUK.
        const RectF lw{in_x, cy - bm.lw_h / 2, bm.col_w, bm.lw_h};
        draw_well(g, lw);
        const float fx = lw.x + kBWellPad, fy = cy - bm.fps_h / 2;
        label_row(idx_fps, bm.idx_fps_w, idx_c_fps, stats[0].label, fx, fy);
        const float vy = fy + bm.cap_h + 2.f;
        g.text(Role::Num22, stats[0].value, {fx, vy, bm.vw[0], bm.num_h}, stats[0].c, {Align::Leading, false, false, true});
        if (bm.delta_w > 0)
            g.text(Role::Mono11Semi, delta_txt, {fx + bm.vw[0] + 8.f - 1.f, vy + base_dy(Role::Num22, Role::Mono11Semi), bm.delta_w, theme_.role(Role::Mono11Semi).line},
                   delta_c, {Align::Leading, false});
        if (bm.base_w > 0) {
            // "BAZ: " ikincil, "171,8 FPS" slate-300, " · BASLIK" ikincil (buyuk harf; sigmazsa kisalir).
            const auto tpl = t(StringId::PanelHeroBase);
            const auto at = tpl.find(L"{fps}");
            const std::wstring pre = at == std::wstring::npos ? tpl : tpl.substr(0, at);
            const std::wstring num = at == std::wstring::npos ? base_num : base_num + tpl.substr(at + 5);
            const float by = vy + bm.num_h + 4.f;
            float x = fx;
            const float pw = std::ceil(g.measure(Role::Mono10, pre, kUnbounded, false).w), nw = std::ceil(g.measure(Role::Mono10, num, kUnbounded, false).w);
            g.text(Role::Mono10, pre, {x, by, pw + 1.f, bm.m10_h}, p.text_secondary, {Align::Leading, false});
            x += pw;
            g.text(Role::Mono10, num, {x, by, nw + 1.f, bm.m10_h}, depth::kSlate300, {Align::Leading, false});
            x += nw;
            if (!base_title.empty()) {
                const float room = lw.right() - kBWellPad - bm.low_w - 12.f - 1.f - 12.f - x;
                if (room > 24.f)
                    g.text(Role::Mono10, fit_text(g, Role::Mono10, L" · " + locale_upper(base_title, tr), room), {x, by, room, bm.m10_h}, p.text_secondary,
                           {Align::Leading, false});
            }
        }
        hits.add_text(seq.take(), {fx, fy, bm.fps_w, bm.fps_h}, stats[0].a11y);
        const float lx = lw.right() - kBWellPad - bm.low_w;
        const float div_x = std::floor(fx + bm.fps_w + (lx - fx - bm.fps_w - 1.f) / 2);
        g.fill({div_x, cy - kBDiv / 2, 1.f, kBDiv}, Color{1, 1, 1, 0.07f});
        const float ly = cy - bm.low_h / 2;
        g.text(Role::Caps11, stats[1].label, {lx, ly, bm.cap_low_w, bm.cap_h}, p.text_secondary, {Align::Leading, false});
        if (bm.smooth_w > 0)
            g.text(Role::Mono10, bm.smooth, {lx + bm.low_w - bm.smooth_w, ly + m10_dy, bm.smooth_w, bm.m10_h}, *smooth_pct >= 75.0 ? p.accent : p.warning,
                   {Align::Trailing, false});
        const float lvy = ly + bm.cap_h + 2.f;
        g.text(Role::Num22, stats[1].value, {lx, lvy, bm.vw[1], bm.num_h}, stats[1].c, {Align::Leading, false, false, true});
        if (bm.ms_w > 0)
            g.text(Role::Mono10, ms_txt, {lx + bm.vw[1] + 6.f - 1.f, lvy + base_dy(Role::Num22, Role::Mono10), bm.ms_w, bm.m10_h}, p.text_secondary,
                   {Align::Leading, false});
        hits.add_text(seq.take(), {lx, ly, bm.low_w, bm.low_h}, stats[1].a11y);
        // Ritim seridi: son testin kayitli FPS serisi en fazla 10 kovaya (paylasilan frame_time_bars); yukseklik en yuksek
        // kovaya oranli, ortalamanin %90'inin altindaki kova uyari tonunda (maket: flex-1, gap 3, h 10, yaricap 1).
        if (last_run && last_run->fps_series.size() >= 2) {
            constexpr std::size_t kMaxBars = 10;
            constexpr double kDipFrac = 0.9;
            const std::vector<double> ms = frame_time_bars(last_run->fps_series, kMaxBars);
            double top = 0;
            for (const double v : ms)
                if (std::isfinite(v) && v > 0) top = std::max(top, 1000.0 / v);
            const std::optional<double> avg = last_run->avg_fps.has_value() ? std::optional<double>(*last_run->avg_fps.value()) : std::nullopt;
            const float sy = lvy + bm.num_h + 6.f;
            if (top > 0 && !ms.empty()) {
                const float bw = (bm.low_w - 3.f * static_cast<float>(kMaxBars - 1)) / static_cast<float>(kMaxBars);
                for (std::size_t i = 0; i < ms.size() && i < kMaxBars; ++i) {
                    if (!std::isfinite(ms[i]) || ms[i] <= 0) continue;
                    const double v = 1000.0 / ms[i];
                    const float bar = std::max(1.f, kBStrip * static_cast<float>(v / top));
                    const bool dip = avg && *avg > 0 && v < *avg * kDipFrac;
                    const Color c = !last_run->run_valid ? p.text_secondary : dip ? p.warning : p.accent;
                    g.fill_round({lx + static_cast<float>(i) * (bw + 3.f), sy + kBStrip - bar, bw, bar}, 1.f, c);
                }
            }
        }
        // --- Orta: metin sutunu (dugmenin saginda).
        {
            const float tx = group_x + kBDot + kBGap;
            float y = cy - bm.text_h / 2;
            label_row(idx_perf, bm.idx_perf_w, idx_c_green, t(StringId::OverviewPerfLabel), tx, y);
            y += bm.cap_h + 2.f;
            g.text(Role::Num22, t(pv.switch_on ? StringId::OverviewPerfStateOn : StringId::OverviewPerfStateOff), {tx, y, bm.word_w, bm.num_h},
                   pv.switch_on ? p.accent : p.error, {Align::Leading, false});
            y += bm.num_h;
            if (!hint.empty() || panel_toggle) {
                y += 4.f;
                float hx = tx;
                if (!hint.empty()) {
                    g.text(Role::Small12, hint, {tx, y, bm.hint_w, kBLinkH}, p.text_secondary, {Align::Leading, false, true});
                    hx += bm.hint_w + 6.f - 1.f;
                }
                if (panel_toggle) {
                    // #btn-details-expander: px-2, 12px/500, beyaz %5 zemin (hover %10, metin vurgu), yaricap 4.
                    const RectF lr{hx, y, bm.link_w, kBLinkH};
                    const float hv = hover_value(s, Hit::Details, now);
                    const float press = press_value(s, Hit::Details, now);
                    g.fill_round(lr, 4.f, Color{1, 1, 1, 0.05f + 0.05f * std::min(1.f, hv + press)});
                    g.text(Role::Medium12, bm.link, lr, mix(p.text_primary, p.accent, std::min(1.f, hv + press)), {Align::Center, false, true});
                    if (s.focus_visible && s.focus == Hit::Details) focus_ring(g, lr, 4.f, hits);
                    hits.add(Hit::Details, lr, t(StringId::OverviewPerfDetailsShort), AccRole::Expander, s.details_open);
                    hits.meta[Hit::Details].help = t(StringId::PerfDetails);
                }
                y += kBLinkH;
            }
            if (bm.title_h > 0) {
                y += 4.f;
                g.text(Role::Title14, title, {tx, y, bm.text_w, bm.title_h}, title_color);
                y += bm.title_h;
            }
            if (bm.notice_h > 0) {
                y += 4.f;
                g.text(Role::Small12, notice, {tx, y, bm.text_w, bm.notice_h}, notice_color);
            }
        }
        // --- Sag yuva: HAZIRLIK | Testi baslat + mono satir.
        const RectF rw{area.right() - kBIn - bm.col_w, cy - bm.rw_h / 2, bm.col_w, bm.rw_h};
        draw_well(g, rw);
        const float rx = rw.x + kBWellPad, ry = cy - bm.rdy_h / 2;
        label_row(idx_rdy, bm.idx_rdy_w, idx_c_green, stats[2].label, rx, ry);
        const float rvy = ry + bm.cap_h + 2.f;
        g.text(Role::Num22, stats[2].value, {rx, rvy, bm.vw[2], bm.num_h}, stats[2].c, {Align::Leading, false, false, true});
        if (bm.adv_w > 0)
            g.text(Role::Medium11, adv_txt, {rx + bm.vw[2] + 8.f - 1.f, rvy + base_dy(Role::Num22, Role::Medium11), bm.adv_w, theme_.role(Role::Medium11).line},
                   p.warning, {Align::Leading, false});
        hits.add_text(seq.take(), {rx, ry, bm.rdy_w, bm.rdy_h}, stats[2].a11y);
        if (scored)
            for (int i = 0; i < sc.total; ++i) {
                const Color c = i < sc.good ? p.accent : i < sc.good + sc.advice ? p.warning : Color{1, 1, 1, 0.12f};
                g.fill_round({rx + static_cast<float>(i) * (kBSeg + kBSegGap), rvy + bm.num_h + 6.f, kBSeg, kBSegH}, 2.f, c);
            }
        const float rby = cy - bm.rb_h / 2;
        panel_button(g, rw.right() - kBWellPad - bm.btn.w, rby, Hit::TestStart, start_label, true, s, now, hits, can_start, true);
        g.text(Role::Mono10, bm.line, {rw.right() - kBWellPad - bm.line_w, rby + bm.btn.h + 6.f, bm.line_w, bm.m10_h}, p.text_secondary,
               {Align::Trailing, false});
    } else {

    // --- Metin sutunu. x: sutunun sol kenari; wide: sol hizali (dugmenin saginda), aksi halde ortali.
    const auto draw_text_col = [&](const TextCol& c, float x, float y, bool wide) {
        const Align al = wide ? Align::Leading : Align::Center;
        const float mid = x + c.w / 2;
        if (wide) {
            g.text(Role::Label, idx_perf, {x, y, idx_perf_w, label_h}, idx_green, {Align::Leading, false, true});
            g.text(Role::Label, perf_label, {x + idx_perf_w + idx_gap, y, plw + 1, label_h}, p.text_secondary, {Align::Leading, false, true});
            y += label_h;
            g.text(Role::StateTitle, word, {x, y, ww + 1, hero_h}, pv.switch_on ? p.accent : p.error, {Align::Leading, false, true});
        } else {
            const float pair_w = plw + theme_.space(1) + ww;
            const float px = mid - pair_w / 2;
            g.text(Role::Label, perf_label, {px, y, plw + 1, hero_h}, p.text_secondary, {Align::Leading, false, true});
            g.text(Role::StateTitle, word, {px + plw + theme_.space(1), y, ww + 1, hero_h}, pv.switch_on ? p.accent : p.error,
                   {Align::Leading, false, true});
        }
        y += hero_h;
        if (c.th > 0) {
            g.text(Role::Section, title, {x, y, c.w, c.th}, title_color, {al, true});
            y += c.th;
        }
        if (c.hh > 0) {
            float lx = wide ? x : mid - link_w / 2, ly = y;
            if (c.hint_single) {
                const float hx = wide ? x : mid - (hint_one_w + link_w) / 2;
                g.text(Role::Secondary, hint, {hx, y, hint_one_w + 1, line_h}, p.text_secondary, {Align::Leading, false, true});
                lx = hx + hint_one_w;
            } else if (!hint.empty()) {
                g.text(Role::Secondary, hint, {x, y, c.w, c.hint_h}, p.text_secondary, {al, true});
                ly = y + c.hint_h;
            }
            if (panel_toggle) {
                // Baglanti dugmesi: ok yonu acik/kapali durumunu da gosterir (yalniz renge dayanmaz).
                const RectF lr{lx, ly + (line_h - link_h) / 2, link_w, link_h};
                // DS-02: baglanti dugmesinin kendi hover/basma durumu (Evet/Hayir ile ayni gecisi paylasmaz).
                const float hv = hover_value(s, Hit::Details, now);
                const float press = press_value(s, Hit::Details, now);
                // Maket: hafif dolgulu kucuk dugme (yalniz koyu temada surekli; digerlerinde hover ile).
                const float rest = dark ? 0.55f : 0.f;
                if (hv > 0.01f || press > 0.01f || rest > 0.f)
                    g.fill_round(lr, theme_.radius_control, p.surface_raised.with_alpha(std::min(1.f, rest + hv + press * alpha_scale::kPressFill)));
                g.text(Role::Secondary, link_label, lr, mix(p.text_primary, p.accent, std::min(1.f, hv + press)), {Align::Center, false, true});
                if (s.focus_visible && s.focus == Hit::Details) focus_ring(g, lr, theme_.radius_control, hits);
                hits.add(Hit::Details, lr, t(StringId::OverviewPerfDetailsShort), AccRole::Expander, s.details_open);
                hits.meta[Hit::Details].help = t(StringId::PerfDetails);  // "Bu ayar neleri degistirir" (UIA HelpText)
            }
            y += c.hh;
        }
        if (c.nh > 0) {
            y += theme_.space(0);
            g.text(Role::Secondary, notice, {x, y, c.w, c.nh}, notice_color, {al, true});
        }
    };
    float tx = 0;
    if (layout == Layout::Wide) {
        tx = cx + halo_r + kTextGap;
        draw_text_col(col, tx, cy - col.h / 2, true);
    } else {
        draw_text_col(col, area.x + (area.w - text_w) / 2, cy + d / 2 + kHalo + 2.f, false);
    }

    // --- Yuvalar ve sayilar.
    const auto stat_text = [&](const Stat& st, float sx, float sy, float w) { hits.add_text(seq.take(), {sx, sy, w, label_h + hero_h}, st.a11y); };
    const auto label_row = [&](const std::wstring& idx, float idx_w, const Color& idx_c, const std::wstring& label, float x, float y) {
        float lx = x;
        if (!idx.empty()) {
            g.text(Role::Label, idx, {x, y, idx_w, label_h}, idx_c, {Align::Leading, false, true});
            lx += idx_w + idx_gap;
        }
        g.text(Role::Label, label, {lx, y, one_w(Role::Label, label), label_h}, p.text_secondary, {Align::Leading, false, true});
    };
    if (layout == Layout::Stacked) {
        float y = cy + d / 2 + kHalo + 2.f + col.h + gap;
        const float row_w = below_one_row ? stats_w + stat_gap + start_r.w : stats_w;
        float sx = std::max(area.x + hero_pad, cx - row_w / 2);
        const float sy = y + (below_one_row ? (side_h - label_h - hero_h) / 2 : 0.f);
        for (const auto& st : stats) {
            g.text(Role::Label, st.label, {sx, sy, st.w + 1, label_h}, p.text_secondary, {Align::Leading, false, true});
            g.text(Role::StateTitle, st.value, {sx, sy + label_h, st.w + 1, hero_h}, st.c, {Align::Leading, false, true, true});
            stat_text(st, sx, sy, st.w);
            sx += st.w + stat_gap;
        }
        if (below_one_row) button(g, sx, y + (side_h - start_r.h) / 2, Hit::TestStart, start_label, true, s, now, hits, can_start);
        else button(g, cx - start_r.w / 2, y + label_h + hero_h + theme_.space(1), Hit::TestStart, start_label, true, s, now, hits, can_start);
    } else {
        const float lw_c = left_w_of(full), rw_c = right_w_of(full);
        const float fps_w = full ? fps_full_w : fps_cmp_w, low_w = full ? low_full_w : low_cmp_w, rdy_w = full ? rdy_full_w : rdy_cmp_w;
        // Yuva kutulari. Wide: kartin dis kenarlarina dayali, iki yanda es genislik (icerikten dar olmaz, kWellMaxW'yi
        // gecmez). Side: icerik kadar, dugmeden kMinTrace + parlama payi uzakta.
        RectF l_well, r_well;
        const float wy = cy - well_h / 2;
        if (layout == Layout::Wide) {
            l_well = {area.x + hero_pad, wy, col3_w, well_h};
            r_well = {area.right() - hero_pad - col3_w, wy, col3_w, well_h};
        } else {
            const float lw = lw_c + 2 * kWellPadX, rw = rw_c + 2 * kWellPadX;
            l_well = {cx - halo_r - kMinTrace - lw, wy, lw, well_h};
            r_well = {cx + halo_r + kMinTrace, wy, rw, well_h};
        }
        draw_well(g, l_well);
        draw_well(g, r_well);
        const float sy = cy - bh / 2;
        const float row3_y = sy + label_h + hero_h;
        // Sol yuva: SON TEST FPS (sol) | %1 DUSUK (yuvanin sag tarafina yasli); arada dikey ayirici.
        const float fx = l_well.x + kWellPadX;
        const float lx1 = l_well.right() - kWellPadX - low_w;
        label_row(idx_fps, idx_fps_w, idx_cyan, stats[0].label, fx, sy);
        g.text(Role::StateTitle, stats[0].value, {fx, sy + label_h, stats[0].vw, hero_h}, stats[0].c, {Align::Leading, false, true, true});
        if (full && delta_w > 0)
            g.text(Role::Secondary, delta_txt, {fx + stats[0].vw + theme_.space(1), sy + label_h, delta_w, hero_h}, delta_c, {Align::Leading, false, true, true});
        if (full && base_w > 0) g.text(Role::Secondary, base_txt, {fx, row3_y, base_w, sec_h}, p.text_secondary, {Align::Leading, false, true});
        stat_text(stats[0], fx, sy, fps_w);
        const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
        g.fill({std::floor((fx + fps_w + lx1) * 0.5f), sy + 2.f, 1.f, bh - 4.f}, divider);
        g.text(Role::Label, stats[1].label, {lx1, sy, low_w, label_h}, p.text_secondary, {Align::Leading, false, true});
        g.text(Role::StateTitle, stats[1].value, {lx1, sy + label_h, stats[1].vw, hero_h}, stats[1].c, {Align::Leading, false, true, true});
        if (full && ms_w > 0)
            g.text(Role::Secondary, ms_txt, {lx1 + stats[1].vw + theme_.space(0), sy + label_h, ms_w, hero_h}, p.text_secondary, {Align::Leading, false, true, true});
        stat_text(stats[1], lx1, sy, low_w);
        constexpr float kStripH = 10.f;
        const float strip_y = full ? row3_y + (sec_h - kStripH) / 2 : row3_y + 1.f;
        // Ritim seridi (%1 DUSUK altinda; temsil ettigi deger saniye basina ortalama FPS'tir): son testin kayitli gercek
        // FPS serisi (fps_series) Olcumler sayfasiyla ayni paylasilan kovalayici (frame_time_bars) ile tum kosu boyunca en
        // fazla 10 kovaya ozetlenir. Yukseklik en yuksek kovaya oranli; olculen ortalamanin %90'inin altindaki kovalar
        // uyari tonunda. Gecersiz / eski kosuda tum serit notr tonda. Kayitli seri yoksa (eski kayit / denge testi) hic
        // cizilmez.
        if (last_run && last_run->fps_series.size() >= 2) {
            constexpr std::size_t kMaxBars = 10;
            constexpr double kDipFrac = 0.9;
            const std::vector<double> ms = frame_time_bars(last_run->fps_series, kMaxBars);
            double top = 0;
            for (const double v : ms)
                if (std::isfinite(v) && v > 0) top = std::max(top, 1000.0 / v);
            const std::optional<double> avg = last_run->avg_fps.has_value() ? std::optional<double>(*last_run->avg_fps.value()) : std::nullopt;
            if (top > 0 && ms.size() >= 2) {
                const float bw = std::min(low_w, kStripMaxW) / static_cast<float>(kMaxBars);
                for (std::size_t i = 0; i < ms.size(); ++i) {
                    if (!std::isfinite(ms[i]) || ms[i] <= 0) continue;
                    const double v = 1000.0 / ms[i];
                    const float bar = std::max(1.f, kStripH * static_cast<float>(v / top));
                    const bool dip = avg && *avg > 0 && v < *avg * kDipFrac;
                    const Color c = !last_run->run_valid ? p.text_secondary : dip ? p.warning : p.accent;
                    g.fill({lx1 + static_cast<float>(i) * bw, strip_y + kStripH - bar, std::max(1.f, bw - 3.f), bar}, c.with_alpha(0.85f));
                }
            }
        }
        // Sag yuva: HAZIRLIK (+ oneri sayisi) ve hazirlik seridi; sagda "Testi baslat" (+ son test satiri).
        const float rx = r_well.x + kWellPadX;
        label_row(idx_rdy, idx_rdy_w, idx_green, stats[2].label, rx, sy);
        g.text(Role::StateTitle, stats[2].value, {rx, sy + label_h, stats[2].vw, hero_h}, stats[2].c, {Align::Leading, false, true, true});
        if (full && adv_w > 0)
            g.text(Role::Secondary, adv_txt, {rx + stats[2].vw + theme_.space(1), sy + label_h, adv_w, hero_h}, p.warning, {Align::Leading, false, true});
        stat_text(stats[2], rx, sy, rdy_w);
        // Hazirlik seridi: gercek tarama sayimi, kontrol basina bir parca; iyi (vurgu), oneri (uyari), olculmedi (notr).
        if (scored) {
            const float step = std::min(kRdySeg + kRdySegGap, (rdy_w + kRdySegGap) / static_cast<float>(sc.total));
            for (int i = 0; i < sc.total; ++i) {
                const Color c = i < sc.good ? p.accent : i < sc.good + sc.advice ? p.warning : mix(p.surface, p.border, alpha_scale::kDivider);
                g.fill_round({rx + static_cast<float>(i) * step, strip_y + (kStripH - 6.f) / 2, std::max(1.f, step - kRdySegGap), 6.f}, 2.f, c);
            }
        }
        const float rc_h = right_col_h(full);
        const float bx = r_well.right() - kWellPadX - start_r.w;
        const float by = cy - rc_h / 2;
        button(g, bx, by, Hit::TestStart, start_label, true, s, now, hits, can_start);
        if (full && !test_line.empty())
            g.text(Role::Secondary, fit_text(g, Role::Secondary, test_line, test_w), {r_well.right() - kWellPadX - test_w, by + start_r.h + theme_.space(0), test_w, sec_h},
                   p.text_secondary, {Align::Trailing, false, true});
        // Enerji hatlari (yalniz koyu tema; parlama payinin disinda: nefes hizli yolu etkilenmez). Uc noktalari durumu
        // izler: Acik -> camgobegi (sol) / vurgu (sag); Kapali -> hata tonu.
        if (dark) {
            constexpr float kTraceW = 1.3f, kJog = 4.f, kDotR = 2.2f, kMinSpan = 6.f;
            const Color trace_c = pv.switch_on ? p.accent.with_alpha(0.35f) : p.error.with_alpha(0.40f);
            const Color dot_l = pv.switch_on ? p.cyan.with_alpha(0.55f) : p.error.with_alpha(0.55f);
            const Color dot_r = pv.switch_on ? p.accent.with_alpha(0.55f) : p.error.with_alpha(0.55f);
            const float l_start = cx - halo_r - 4.f, l_end = l_well.right();
            if (l_start > l_end + kMinSpan) {
                const float mid = (l_start + l_end) * 0.5f;
                const Point tr_l[] = {{l_start, cy}, {mid + 2.f, cy}, {mid - 2.f, cy - kJog}, {l_end, cy - kJog}};
                g.polyline(tr_l, kTraceW, trace_c);
                g.fill_circle(l_end, cy - kJog, kDotR, dot_l);
            }
            const float r_start = layout == Layout::Wide ? tx + col.w + kTextGap : cx + halo_r + 4.f, r_end = r_well.x;
            if (r_end > r_start + kMinSpan) {
                const float mid = (r_start + r_end) * 0.5f;
                const Point tr_r[] = {{r_start, cy}, {mid - 2.f, cy}, {mid + 2.f, cy + kJog}, {r_end, cy + kJog}};
                g.polyline(tr_r, kTraceW, trace_c);
                g.fill_circle(r_end, cy + kJog, kDotR, dot_r);
            }
        }
    }
    }  // !boost
    if (!panel_open) s.group_enabled.clear();
    if (!has_detail) return total_h;

    // --- Alt kart: onceki seridin devam icerigi (ayni icerik, ayni kimlikler).
    const RectF card{area.x, area.y + section_h + card_gap, area.w, pad + detail_h + pad};
    float y = 0;
    const bool consent = pv.phase == Phase::Confirming || pv.phase == Phase::ConfirmRestore;
    g.fill_round(card, theme_.radius_card, p.hero);
    g.stroke_round(card, theme_.radius_card,
                   pv.phase == Phase::RecoveryNeeded ? p.error.with_alpha(alpha_scale::kOutline)
                   : consent                         ? p.accent.with_alpha(alpha_scale::kSoftOutline)
                                                     : p.border);
    log_card(card);
    const float x = card.x + pad;
    y = card.y + pad;
    const auto draw_tips = [&](float tx0, float card_y) {
        if (!tips) return;
        // Katli satir: yalniz gorunum durumu (kaydedilmez); UIA Expander. Maddeler sabit metindir; sayi/FPS vaadi yok.
        const float ty = card_y + pad + tips_y_off;
        const bool open = s.perf_tips_open;
        const auto label = t(StringId::PerfTipsTitle) + (open ? L" ▴" : L" ▾");
        const float lw = std::min(std::ceil(g.measure(Role::Medium12, label, kUnbounded, false).w) + 2 * theme_.space(1), inner_w);
        const RectF lr{tx0, ty, lw, tips_row_h};
        const float hv = hover_value(s, Hit::PerfTips, now);
        const float press = press_value(s, Hit::PerfTips, now);
        g.fill_round(lr, theme_.radius_control, mix(p.surface, p.text_primary, 0.05f + 0.05f * std::min(1.f, hv + press)));
        g.text(Role::Medium12, label, lr, mix(p.text_primary, p.accent, std::min(1.f, hv + press)), {Align::Center, false, true});
        if (s.focus_visible && s.focus == Hit::PerfTips) focus_ring(g, lr, theme_.radius_control, hits);
        hits.add(Hit::PerfTips, lr, label, AccRole::Expander, open);
        float ly = ty + tips_row_h;
        for (std::size_t i = 0; i < tip_txt.size(); ++i) {
            ly += theme_.space(0);
            g.text(Role::Secondary, tip_txt[i], {tx0, ly, inner_w, tip_h[i]}, p.text_secondary);
            ly += tip_h[i];
        }
    };
    if (panel_open && panel_toggle) {
        groups_panel(g, x, y, inner_w, pv, s, now, editable, false, &hits, vm.admin_without_uac);
        draw_tips(x, card.y);
        return total_h;
    }
    const float base_alpha = s.page_fade.value(now, e);
    g.set_alpha(base_alpha * s.card_fade.value(now, e));
    if (!question.empty()) {
        const float qh = g.measure(Role::Section, question, inner_w).h;
        g.text(Role::Section, question, {x, y, inner_w, qh}, p.text_primary);
        y += qh + gap;
    }
    if (panel_open && pv.phase == Phase::Confirming) {
        groups_panel(g, x, y, inner_w, pv, s, now, editable, false, &hits, vm.admin_without_uac);
        y += panel_h + gap;
    }
    for (std::size_t i = 0; i < impacts.size(); ++i) {
        g.text(Role::Secondary, impacts[i], {x, y, inner_w, impact_h[i]}, impact_colors[i]);
        y += impact_h[i] + theme_.space(0);
    }
    if (buttons) {
        y += gap - theme_.space(0);
        // Oyun calisirken Evet gosterilmez (neden ust kartta); Hayir taslagi birakir. Evet / Hayir es genislikte (B7).
        const auto yes = t(StringId::ActionConfirm), no = t(StringId::ActionCancel);
        const float pair_w = button_group_w(g, {yes, no});
        const float yes_w = vm.game_gate_blocked ? -theme_.space(2) : button(g, x, y, Hit::Yes, yes, true, s, now, hits, true, false, false, pair_w);
        button(g, x + yes_w + theme_.space(2), y, Hit::No, no, false, s, now, hits, true, false, false, pair_w);
    }
    if (retry) {
        // Kurtarma: "Tekrar dene" ile (dis degisiklik varsa) "Mevcut degeri koru" ayni satirda es genislikte (B7).
        const auto again = t(StringId::PerfRetry), keep = t(StringId::PerfKeepButton);
        const float pair_w = pv.external_conflict ? button_group_w(g, {again, keep}) : 0.f;
        const float retry_w = button(g, x, y, Hit::Retry, again, true, s, now, hits, true, false, false, pair_w);
        if (pv.external_conflict) button(g, x + retry_w + theme_.space(2), y, Hit::PerfKeep, keep, false, s, now, hits, true, false, false, pair_w);
    }
    if (drift) {
        // Koruma Modu: "Yeniden uygula" (Evet/Hayir onayina gider; oyun calisirken gosterilmez) ve "Boyle kalsin" es genislikte.
        const auto again = t(StringId::PerfDriftReapply), keep = t(StringId::PerfDriftKeep);
        const float pair_w = button_group_w(g, {again, keep});
        const float again_w = vm.game_gate_blocked ? -theme_.space(2) : button(g, x, y, Hit::PerfDriftReapply, again, true, s, now, hits, true, false, false, pair_w);
        button(g, x + again_w + theme_.space(2), y, Hit::PerfDriftKeep, keep, false, s, now, hits, true, false, false, pair_w);
    }
    if (progress) {
        // Gercek adim orani: tamamlanan / toplam (sahte yuzde yok).
        const RectF bar{x, y, inner_w, 6};
        g.fill_round(bar, 3, p.surface_raised);
        const float f = std::clamp(s.progress_fill.value(now, e), 0.f, 1.f);
        if (f > 0) g.fill_round({bar.x, bar.y, std::max(6.f, bar.w * f), bar.h}, 3, p.accent);
        y += 6 + theme_.space(1);
        g.text(Role::Secondary, prog_line, {x, y, inner_w, prog_line_h}, p.text_secondary, {Align::Leading, false});
        if (!prog_phase.empty()) g.text(Role::Secondary, prog_phase, {x, y + prog_line_h, inner_w, prog_phase_h}, p.text_secondary, {Align::Leading, false});
    }
    g.set_alpha(base_alpha);
    return total_h;
}

void Screen::paint_power_region(Graphics& g, UiState& s, const ViewModel& vm, std::int64_t now) {
    // Nefes / kalp atisi zamanlayicisinin hizli yolu: yalniz dugme bolgesi (zemin + dugme). Isabet haritasi degismez.
    const RectF& r = s.power_region;
    if (r.w <= 0 || r.h <= 0) return;
    HitMap scratch;
    // Dugme bir kartin icindedir: zemin, tam karedeki kart yuzeyinin (koyu temada gradyan; opak) ayni kutuyla yeniden
    // cizimidir. Kesin kirpma 1 DIP genisletilir: pencerenin piksel bolgesi (yuvarlanmis) tamamen yazilir; bu payda da
    // tam karede yalniz kart yuzeyi + parlama vardir.
    g.push_clip({r.x - 1.f, r.y - 1.f, r.w + 2.f, r.h + 2.f});
    const std::size_t keep = shadow_log_.size();
    if (s.power_card.w > 0) draw_card_face(g, s.power_card, false, s.power_glow ? depth::kPanelCardRadius : -1.f);
    else g.fill(r, pal().surface);
    shadow_log_.resize(keep);
    if (s.page == Page::Measurements) {
        // Olcumler Test karti (spec 2026-11-12 C): bolge yalniz bosta ve baslatilabilirken yazilir.
        test_button(g, r.x + r.w / 2, r.y + r.h / 2, vm.measure, s, now, true, scratch, {}, {});
    } else {
        if (s.power_glow)
            hero_glow(g, s.power_card, s.power_card.x + s.power_card.w / 2, s.power_card.y + s.power_card.h / 2, s.toggle_knob.value(now, theme_.easing));
        if (s.power_conduits) hero_conduits(g, s.power_card, vm.perf.switch_on);
        const bool enabled = vm.perf.phase == Phase::Off || vm.perf.phase == Phase::On;
        power_button(g, r.x + r.w / 2, r.y + r.h / 2, vm.perf, s, now, enabled, scratch, {}, {});
    }
    g.pop_clip();
}

}  // namespace ef::ui
