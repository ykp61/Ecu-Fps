// UI-06 / spec 2026-11-12 B7: Ayarlar — tek, ortali liste (Windows ile baslat, Animasyonlar, RGB serit, Oyun onaylari,
// Yazilim guncellemesi, Destek paketi, Lisanslar, Uygulamayi kaldir). Spec G: ilk acilis bildirimi seridi.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "core/guided/site_links.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
}  // namespace

std::pair<int, std::size_t> consent_hit_row(Hit h) {
    const int v = static_cast<int>(h);
    const int t0 = static_cast<int>(Hit::ConsentToggle0), r0 = static_cast<int>(Hit::ConsentReset0);
    if (v >= t0 && v < t0 + kMaxConsentRows) return {1, static_cast<std::size_t>(v - t0)};
    if (v >= r0 && v < r0 + kMaxConsentRows) return {2, static_cast<std::size_t>(v - r0)};
    return {0, 0};
}

void Screen::small_toggle(Graphics& g, const RectF& r, bool on, bool enabled, Hit id, const std::wstring& name, UiState& s,
                          std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const float dim = enabled ? 1.f : alpha_scale::kDisabled;
    // DS-02: hover/basma geri bildirimi tum anahtarlarda ayni (azaltilmis harekette aninda).
    const float hv = enabled ? hover_value(s, id, now) : 0.f;
    const float pv = enabled ? press_value(s, id, now) : 0.f;
    // Spec §4: kapali iz yukseltilmis yuzey + ikincil topuz; acik iz yuzey/accent karisimi + accent topuz ve parlama.
    // Kapali izin siniri 1 DIP cerceve (WCAG 1.4.11; hover'da accent'e kayar). Uygulanirken (enabled=false) soluk.
    const float inset = std::round(r.h * 0.14f);  // 22 DIP anahtarda 3: topuz 16 DIP
    const float kr = r.h / 2 - inset;
    const float kx = on ? r.right() - inset - kr : r.x + inset + kr;
    const float ky = r.y + r.h / 2;
    const float hp = std::min(1.f, hv * alpha_scale::kHoverTint + pv * alpha_scale::kHoverTint);
    if (on) {
        const Color track = mix(p.surface, p.accent, alpha_scale::kToggleOnTrack);
        g.fill_round(r, r.h / 2, mix(track, p.accent, hv * alpha_scale::kSelectionTint + pv * alpha_scale::kPressTint).with_alpha(dim));
        g.stroke_round(r, r.h / 2, p.accent.with_alpha(dim));
        if (enabled && glow_allowed(s)) g.glow_round({kx - kr, ky - kr, 2 * kr, 2 * kr}, kr, p.accent, alpha_scale::kPrimaryGlowIdle);
        g.fill_circle(kx, ky, kr, p.accent.with_alpha(dim));
    } else {
        g.fill_round(r, r.h / 2, mix(p.surface_raised, p.border, hp * alpha_scale::kSelectionTint).with_alpha(dim));
        g.stroke_round(r, r.h / 2, mix(p.border, p.accent, hp).with_alpha(dim));
        g.fill_circle(kx, ky, kr, p.text_secondary.with_alpha(dim));
    }
    // DS-05/TH-01: devre disi anahtar UIA agacinda kalir (IsEnabled=false, durum okunur); tiklanmaz ve Tab
    // sirasinda atlanir (focus_order(s, hits)). Odak zaten uzerindeyse halka cizilir (odak nerede belli olsun).
    if (s.focus_visible && s.focus == id) focus_ring(g, r, r.h / 2, hits);
    toggle_hit(r, on, enabled, id, name, hits);
}

void Screen::toggle_hit(const RectF& r, bool on, bool enabled, Hit id, const std::wstring& name, HitMap& hits) const {
    // Isabet kutusu satir ici dugme yuksekliginde (28; asgari dokunma hedefi 24'ten kucuk olamaz): 34 DIP tablo
    // satirinda komsu satirin kutusuyla cakismaz.
    const float ty = std::max(0.f, (std::max(theme_.touch_min, theme_.button_row_h) - r.h) / 2);
    hits.add(id, {r.x - 4, r.y - ty, r.w + 8, r.h + 2 * ty}, name, AccRole::ToggleSwitch, false, on ? 1 : 0, enabled);
}

namespace {
// Surum satiri: "Surum {ver} · imzasiz alpha" + kisa derleme kimligi (ayni surum numarali derlemeleri ayirir).
std::wstring version_line(std::wstring pattern, const ViewModel& vm) {
    const std::wstring v = vm.version.empty() ? std::wstring(L"—") : widen(vm.version);
    if (const auto pos = pattern.find(L"{ver}"); pos != std::wstring::npos) pattern.replace(pos, 5, v);
    if (!vm.build_id.empty()) pattern += L" · " + widen(vm.build_id.substr(0, 7));
    return pattern;
}
}  // namespace

void Screen::settings_header(Graphics& g, HeaderBar& bar, const ViewModel& vm, HitMap& hits) {
    // Spec B7: baslik seridi liste genisligindedir (header_strip daraltir); solda "Ayarlar", sagda sari surum satiri
    // (zorunlu "imzasiz alpha" uyarisi). Salt okunur UIA Text; kaldirilan "Tema: Koyu" bilgi satiri HelpText'te.
    constexpr float kUnbounded = 10000.f;
    const auto ver = version_line(t(StringId::SettingsVersionAlpha), vm);
    const float w = std::ceil(g.measure(Role::Mono12, ver, kUnbounded, false).w) + 1.f;
    const RectF r = bar.take(w, theme_.role(Role::Mono12).line);
    if (r.w <= 0) return;
    g.text(Role::Mono12, ver, r, pal().warning, {Align::Trailing, false, true});
    const Hit id = hit_of(HitKind::Status, static_cast<std::size_t>(kMaxStatusItems) - 1);
    hits.add_text(id, r, ver);
    hits.meta[id].help = t(StringId::SettingsTheme) + L": " + t(StringId::SettingsThemeDarkOnly) + L". " + t(StringId::SettingsThemeDesc);
}

float Screen::autostart_notice(Graphics& g, const RectF& area, const RectF& column, UiState& s, const ViewModel& vm, std::int64_t now,
                               HitMap& hits) {
    s.autostart_notice = false;
    if (!vm.settings.autostart_notice || area.w <= 0) return 0.f;
    const auto& p = pal();
    const auto text = t(StringId::AutostartNotice);
    const auto off = t(StringId::AutostartNoticeOff);
    const float pad_y = theme_.space(1);
    // Maket #notice-autostart: .well-v2 px-3.5 py-2, max-w-[780px] ortali sutunun ICINDE (serit degil kuyu).
    const float lw = settings_list_width(std::min(column.w, area.w));
    const float lx = std::max(column.x + (std::min(column.w, area.w) - lw) / 2, area.x);
    const float pad_x = 14.f;
    const float tx = lx + pad_x;
    const float tw = std::max(lw - 2 * pad_x, 0.f);
    const float bw = button_group_w(g, {off}, true);
    const float text_w = std::max(tw - bw - theme_.space(3), 1.f);
    const float sl = theme_.role(Role::Secondary).line;
    const float line = std::max(sl, theme_.button_row_h);
    const float gap = theme_.space(1);
    const RectF band{lx, area.y + gap, lw, line + 2 * pad_y};
    // Engellemez, tek satir; sutun icinde kuyu (maket well-v2, yaricap 8).
    draw_well(g, band);
    const auto shown = fit_text(g, Role::Secondary, text, text_w);
    const RectF tr{tx, band.y + pad_y + (line - sl) / 2, text_w, sl};
    g.text(Role::Secondary, shown, tr, p.text_primary);
    hits.add_text(Hit::AutostartNotice, tr, text);
    const bool enabled = vm.settings.autostart_known && vm.settings.uninstall_phase != SettingsView::UninstallPhase::Running;
    button(g, tx + tw - bw, band.y + pad_y + (line - theme_.button_row_h) / 2, Hit::AutostartNoticeOff, off, false, s, now, hits, enabled,
           false, true, bw);
    hits.meta[Hit::AutostartNoticeOff].name = off + L": " + t(StringId::SettingsAutostart);
    hits.meta[Hit::AutostartNoticeOff].help = text;
    s.autostart_notice = true;
    return band.h + 2 * gap;
}

float Screen::install_offer_notice(Graphics& g, const RectF& area, const RectF& column, UiState& s, const ViewModel& vm, std::int64_t now,
                                   HitMap& hits) {
    s.install_offer = false;
    if (!vm.install_offer || area.w <= 0) return 0.f;
    const auto& p = pal();
    const auto text = t(StringId::InstallOfferText);
    const auto install = t(StringId::SetupInstall);
    const float pad_y = theme_.space(1);
    const float tx = std::max(column.x, area.x);
    const float tw = std::max(std::min(column.right(), area.right()) - tx, 0.f);
    const float bw = button_group_w(g, {install}, true);
    const float text_w = std::max(tw - bw - theme_.space(3), 1.f);
    const float sl = theme_.role(Role::Secondary).line;
    const float line = std::max(sl, theme_.button_row_h);
    const RectF band{area.x, area.y, area.w, line + 2 * pad_y};
    // Ilk acilis bildirimiyle ayni notr serit; engellemez (uygulama kullanilmaya devam eder).
    const ChipColors c = chip_colors(p, ChipTone::Neutral);
    g.fill(band, c.bg);
    g.fill({band.x, band.bottom() - 1, band.w, 1}, p.border);
    const auto shown = fit_text(g, Role::Secondary, text, text_w);
    const RectF tr{tx, band.y + pad_y + (line - sl) / 2, text_w, sl};
    g.text(Role::Secondary, shown, tr, c.fg);
    hits.add_text(Hit::InstallOfferText, tr, text);
    const bool enabled = vm.settings.uninstall_phase != SettingsView::UninstallPhase::Running;
    button(g, tx + tw - bw, band.y + pad_y + (line - theme_.button_row_h) / 2, Hit::InstallOfferInstall, install, true, s, now, hits, enabled,
           false, true, bw);
    hits.meta[Hit::InstallOfferInstall].name = install + L": " + t(StringId::SetupTitle);
    hits.meta[Hit::InstallOfferInstall].help = text;
    s.install_offer = true;
    return band.h;
}

std::wstring Screen::ofl_text() const {
    static constexpr StringId kParts[] = {StringId::AboutOfl01, StringId::AboutOfl02, StringId::AboutOfl03, StringId::AboutOfl04,
                                          StringId::AboutOfl05, StringId::AboutOfl06, StringId::AboutOfl07, StringId::AboutOfl08,
                                          StringId::AboutOfl09, StringId::AboutOfl10, StringId::AboutOfl11, StringId::AboutOfl12};
    std::wstring out;
    for (const StringId id : kParts) out += (out.empty() ? L"" : L"\n") + t(id);
    return out;
}

float Screen::settings_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin (pb-1) 16 DIP altinda baslatir; ortak duzen 8 verir (Oyun
    // hazirligi / Olcumler ile ayni 5 DIP pay).
    constexpr float kTopInset = 5.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    using UP = SettingsView::UninstallPhase;
    const auto& p = pal();
    const auto& sv = vm.settings;
    const float pad = theme_.card_pad;
    const float sp1 = theme_.space(1);
    const float bl = theme_.role(Role::Body).line;
    const float tog_w = theme_.toggle_w, tog_h = theme_.toggle_h;
    const float btn_h = theme_.button_row_h;
    const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
    const std::size_t games_n = std::min(sv.games.size(), static_cast<std::size_t>(kMaxConsentRows));
    s.consent_rows = s.settings_advanced_open && s.settings_games_open ? games_n : 0;
    // R20: geri alma/veri silme surerken baska yazan ayar yolu yoktur (anahtarlar, onaylar, paket devre disi).
    const bool lock = sv.uninstall_phase == UP::Running;

    // Liste: icerik alaninda ortali tek kart. Satir: ad solda, kontrol sagda tek hizada; satirlar ince cizgiyle ayrilir.
    const float lw = settings_list_width(body.w);
    const RectF list{body.x + std::floor((body.w - lw) / 2), body.y, lw, 0};
    const float x0 = list.x + pad, inner_w = std::max(lw - 2 * pad, 1.f);
    const auto show = t(StringId::SettingsShow), hide = t(StringId::SettingsHide);
    const auto create = t(StringId::SettingsSupportCreate);
    const auto reset = t(StringId::SettingsReset);
    const auto check_update = t(StringId::SettingsUpdateCheck);
    const auto open_link = t(StringId::SettingsAboutOpen);  // T20: yasal sayfa baglantilari
    // Dugme kurali (B7): listedeki dugmeler es genislik ve es yukseklikte.
    // Maket: satir dugmeleri w-24 (96 DIP).
    const float bw = std::max(96.f, button_group_w(g, {show, hide, create, reset, check_update, open_link}, true));
    const float ctrl_w = std::max(bw, tog_w);
    const float label_w = std::max(inner_w - ctrl_w - theme_.space(3), 1.f);
    const float row_h = std::max({theme_.row_h, btn_h + 2 * sp1, tog_h + 2 * sp1});

    // Satir alti durum satirlari (aciklama degil: islem sonucu / zorunlu uyari).
    struct Line {
        std::wstring text;
        Color color;
    };
    std::vector<Line> auto_lines, support_lines, update_lines, language_lines;
    // Denetim 5.2: dil satiri. Ad su anki tercihi soyler; kaydedilince "yeniden baslatinca" satiri.
    using LN = SettingsView::LanguageNotice;
    if (sv.language_notice == LN::Restart) language_lines.push_back({t(StringId::SettingsLanguageRestart), p.text_secondary});
    if (sv.language_notice == LN::SaveFailed) {
        language_lines.push_back({t(StringId::SettingsLanguageSaveFailed), p.warning});
        if (sv.language_restart_pending) language_lines.push_back({t(StringId::SettingsLanguageRestart), p.text_secondary});
    }
    using UC = SettingsView::UpdateCheck;
    // Acilistaki otomatik denetim surerken de dugme devre disi (ayni anda iki denetim yok).
    const bool update_busy = sv.update_check == UC::Checking || vm.update.outcome == core::update::UpdateOutcome::Checking;
    if (update_busy) {
        update_lines.push_back({t(StringId::SettingsUpdateChecking), p.text_secondary});
    } else if (sv.update_check == UC::UpToDate) {
        auto txt = t(StringId::SettingsUpdateUpToDate);
        if (const auto pos = txt.find(L"{ver}"); pos != std::wstring::npos) txt.replace(pos, 5, vm.version.empty() ? std::wstring(L"—") : widen(vm.version));
        update_lines.push_back({txt, p.text_secondary});
    } else if (sv.update_check == UC::Failed) {
        update_lines.push_back({t(StringId::SettingsUpdateFailed), p.warning});
    } else if (sv.update_check == UC::Unavailable) {
        update_lines.push_back({t(StringId::SettingsUpdateUnavailable), p.text_secondary});
    }
    if (!sv.autostart_known) {
        auto_lines.push_back({t(StringId::SettingsAutostartUnknown), p.warning});
    } else {
        using F = core::settings::AutostartFailure;
        if (sv.autostart_failure == F::WriteFailed) auto_lines.push_back({t(StringId::SettingsAutostartFailedWrite), p.warning});
        else if (sv.autostart_failure == F::VerifyFailed) auto_lines.push_back({t(StringId::SettingsAutostartFailedVerify), p.warning});
        else if (sv.autostart_failure == F::RemoveFailed) auto_lines.push_back({t(StringId::SettingsAutostartFailedRemove), p.warning});
        else if (sv.autostart_failure == F::ReadFailed) auto_lines.push_back({t(StringId::SettingsAutostartUnknown), p.warning});
    }
    if (sv.bundle_busy) support_lines.push_back({t(StringId::SettingsSupportBusy), p.text_secondary});
    else if (sv.bundle_done) support_lines.push_back({t(StringId::SettingsSupportSaved) + L" " + sv.bundle_path, p.text_secondary});
    else if (sv.bundle_failed) support_lines.push_back({t(StringId::SettingsSupportFailed), p.warning});
    if (sv.device_unverified) {
        // Uyumluluk B8 (bosluk 12): engellemeyen not (ozellik kapatmaz). Hangi parca test edilmedi: Windows / GPU ureticisi.
        std::wstring what;
        const auto add = [&what](const std::wstring& part) { what += (what.empty() ? L"" : L", ") + part; };
        if (sv.device_os_unverified) add(t(StringId::SettingsDeviceUnverifiedOs));
        if (sv.device_gpu_unverified) add(t(StringId::SettingsDeviceUnverifiedGpu));
        if (what.empty()) {  // parcalar ayri ayri dogrulandi ama birlikte degil
            add(t(StringId::SettingsDeviceUnverifiedOs));
            add(t(StringId::SettingsDeviceUnverifiedGpu));
        }
        auto txt = t(StringId::SettingsDeviceUnverified);
        if (const auto pos = txt.find(L"{what}"); pos != std::wstring::npos) txt.replace(pos, 6, what);
        support_lines.push_back({std::move(txt), p.text_secondary});
    }
    // Oyun satirlari (sanal liste: yukseklik yalniz ada bagli; satir sayisi kMaxConsentRows ile sinirli).
    const float name_w = std::max(label_w, 80.f);
    std::vector<float> name_hs(s.consent_rows, bl);
    for (std::size_t i = 0; i < s.consent_rows; ++i) name_hs[i] = std::max(bl, g.measure(Role::Body, widen(sv.games[i].name), name_w).h);
    const auto comps = t(StringId::AboutComponents);
    const auto mit = t(StringId::AboutMit1) + L" " + t(StringId::AboutMit2) + L" " + t(StringId::AboutMit3);

    std::size_t status_i = 0;
    // Tek gecis: draw=false yalniz yukseklik olcer (kart cercevesi icin); draw=true cizer ve kaydeder.
    auto pass = [&](bool draw) -> float {
        float y = list.y;
        bool first = true;
        auto rule = [&] {
            if (!first && draw) g.fill({x0, std::floor(y), inner_w, 1}, divider);
            if (!first) y += 1;
            first = false;
        };
        // Ana satir: ad + sagda kontrol. control(rect) yalniz cizimde cagrilir.
        // BOOST maketi satiri: ad 13px/600, altinda 11px ikincil aciklama (her satirda); px = kart ic boslugu, py 12.
        auto row = [&](const std::wstring& label, Color col, auto&& control, const std::wstring& desc = {}) {
            rule();
            constexpr float kRowPadY = 12.f, kDescGap = 2.f;
            // Satir adi 13px/500; yalniz hata renginde (Uygulamayi kaldir) 600.
            const Role lrole = col == p.error ? Role::Semi13 : Role::Medium13;
            const float lh = std::max(theme_.role(lrole).line, g.measure(lrole, label, label_w).h);
            const float dh = desc.empty() ? 0.f : g.measure(Role::Small11, desc, label_w).h;
            const float block = lh + (dh > 0 ? kDescGap + dh : 0.f);
            const float h = std::max(std::max(btn_h, tog_h) + 2 * kRowPadY, block + 2 * kRowPadY);
            if (draw && y + h >= s.view_top && y <= s.view_bottom) {
                // Uygulamayi kaldir satiri: hata %3 zemin (maket bg-[#FF5C7A]/[0.03]).
                if (col == p.error && pal_ == &theme_.dark) g.fill({list.x + 1, std::floor(y), lw - 2, std::ceil(h)}, Color{p.error.r, p.error.g, p.error.b, 0.03f});
                const float ty = y + (h - block) / 2;
                g.text(lrole, label, {x0, ty, label_w, lh}, col);
                if (dh > 0) g.text(Role::Small11, desc, {x0, ty + lh + kDescGap, label_w, dh}, p.text_secondary);
            }
            if (draw) control(RectF{x0 + inner_w - ctrl_w, y, ctrl_w, h});
            y += h;
        };
        auto lines = [&](const std::vector<Line>& ls) {
            for (const auto& l : ls) {
                const float h = g.measure(Role::Secondary, l.text, inner_w).h;
                if (draw) {
                    g.text(Role::Secondary, l.text, {x0, y, inner_w, h}, l.color);
                    if (status_i < static_cast<std::size_t>(kMaxStatusItems) - 1) hits.add_text(hit_of(HitKind::Status, status_i++), {x0, y, inner_w, h}, l.text);
                }
                y += h + sp1;
            }
        };
        auto toggle_at = [&](const RectF& c, bool on, bool enabled, Hit id, const std::wstring& name, const std::wstring& help) {
            small_toggle(g, {c.right() - tog_w, c.y + (c.h - tog_h) / 2, tog_w, tog_h}, on, enabled, id, name, s, now, hits);
            hits.meta[id].help = help;
        };
        auto button_at = [&](const RectF& c, Hit id, const std::wstring& label, bool enabled, bool danger, const std::wstring& name,
                             const std::wstring& help, bool selected = false) {
            button(g, c.right() - bw, c.y + (c.h - btn_h) / 2, id, label, false, s, now, hits, enabled, danger, true, bw);
            auto& m = hits.meta[id];
            m.name = name;
            m.help = help;
            m.selected = selected;
        };

        // 1. Dil — Degistir (denetim 5.2; secim kip penceresinde, yeniden baslatinca gecerli).
        std::wstring lang_name;
        if (sv.ui_language == "tr-TR") {
            lang_name = t(StringId::SettingsLanguageTurkish);
        } else if (sv.ui_language == "en-US") {
            lang_name = t(StringId::SettingsLanguageEnglish);
        } else {
            lang_name = t(StringId::SettingsLanguageSystem);
            if (const auto q = lang_name.find(L"{lang}"); q != std::wstring::npos)
                lang_name.replace(q, 6, t(sv.windows_tr ? StringId::SettingsLanguageTurkish : StringId::SettingsLanguageEnglish));
        }
        const auto lang_label = t(StringId::SettingsLanguageRow) + L" · " + lang_name;
        const auto lang_change = t(StringId::SettingsLanguageChange);
        row(lang_label, p.text_primary, [&](const RectF& c) {
            button_at(c, Hit::SettingsLanguage, lang_change, !lock, false, lang_change + L": " + lang_label, t(StringId::SettingsLanguageDesc));
        });
        lines(language_lines);
        // 2. Yazilim guncellemesi — Denetle (el ile Ecu Fps guncelleme denetimi; sonuc satiri altta).
        const auto upd_label = t(StringId::SettingsUpdateRow);
        row(upd_label, p.text_primary, [&](const RectF& c) {
            button_at(c, Hit::SettingsUpdateCheck, check_update, !update_busy && !lock, false, check_update + L": " + upd_label,
                      a11y_join(t(StringId::SettingsUpdateDesc), t(StringId::SettingsNetNoteUpdate)));
        });
        lines(update_lines);
        // P12-B3: "Gelismis" acilir satiri (varsayilan kapali; UIA Expander, Selected = acik). Altindaki satirlar yalniz
        // acikken cizilir ve kaydedilir; hicbir ayar silinmedi, yalniz gorunurluk.
        const auto adv_label = t(StringId::SettingsAdvanced);
        row(adv_label, p.text_primary, [&](const RectF& c) {
            button_at(c, Hit::SettingsAdvanced, s.settings_advanced_open ? hide : show, true, false,
                      (s.settings_advanced_open ? hide : show) + L": " + adv_label, t(StringId::SettingsAdvancedDesc),
                      s.settings_advanced_open);
            hits.meta[Hit::SettingsAdvanced].role = AccRole::Expander;
        });
        if (s.settings_advanced_open) {
            // 1. Windows ile baslat (G). Okunamadiysa devre disi (UIA'da durum belirsiz) ve neden satiri.
            const auto auto_label = t(StringId::SettingsAutostart);
            row(auto_label, p.text_primary, [&](const RectF& c) {
                const bool en = sv.autostart_known && !lock;
                toggle_at(c, sv.autostart, en, Hit::Autostart, en ? auto_label : auto_label + L": " + t(StringId::SettingsAutostartUnknown),
                          t(StringId::SettingsAutostartDesc));
                if (!sv.autostart_known) hits.meta[Hit::Autostart].toggle = -1;  // TH-01
            });  // P12-B2a: satir aciklamasi ekrandan kalkti; HelpText'te
            lines(auto_lines);
            // 2. Animasyonlar (app_animations), 3. RGB serit (strip_enabled).
            const auto anim = t(StringId::SettingsAnimations);
            row(anim, p.text_primary,
                [&](const RectF& c) { toggle_at(c, sv.app_animations, !lock, Hit::AppAnimations, anim, t(StringId::SettingsAnimationsDesc)); });
            const auto strip = t(StringId::SettingsStrip);
            row(strip, p.text_primary,
                [&](const RectF& c) { toggle_at(c, sv.strip_enabled, !lock, Hit::SettingsStrip, strip, t(StringId::SettingsStripDesc)); });
            const auto chint = t(StringId::SettingsCleanupHint);  // R-C2
            row(chint, p.text_primary, [&](const RectF& c) {
                toggle_at(c, sv.cleanup_hint, !lock, Hit::SettingsCleanupHint, chint, t(StringId::SettingsCleanupHintDesc));
            });
            // 4. Oyun onaylari · N — Goster / Gizle; acikken oyun adi + Kaldir satirlari (bossa tek kisa satir).
            const auto games_label = t(StringId::SettingsGames) + L" · " + std::to_wstring(sv.games.size());
            row(games_label, p.text_primary, [&](const RectF& c) {
                button_at(c, Hit::SettingsGamesShow, s.settings_games_open ? hide : show, true, false,
                          (s.settings_games_open ? hide : show) + L": " + games_label, t(StringId::SettingsGamesAutoOff), s.settings_games_open);
            });
            if (s.settings_games_open) {
                if (sv.games.empty()) {
                    lines({{t(StringId::SettingsGamesEmpty), p.text_secondary}});
                } else {
                    for (std::size_t i = 0; i < s.consent_rows; ++i) {
                        const float rh = std::max(row_h, name_hs[i] + 2 * sp1);
                        const Hit hit = consent_reset_hit(i);
                        const auto name = widen(sv.games[i].name);
                        const RectF rb{x0 + inner_w - bw, y + 1 + (std::min(rh, row_h) - btn_h) / 2, bw, btn_h};
                        if (draw) {
                            if (y + rh < s.view_top || y > s.view_bottom) {
                                // Sanal liste: gorunmeyen satir cizilmez; dugmesi cizilmeden kaydedilir (Tab ile gelince kaydirilir).
                                hits.add(hit, rb, reset + L": " + name, AccRole::Button, false, -1, !lock);
                            } else {
                                ++s.rows_drawn;
                                g.fill({x0, std::floor(y), inner_w, 1}, divider);
                                g.text(Role::Body, name, {x0, y + 1 + (rh - name_hs[i]) / 2, name_w, name_hs[i]}, p.text_primary);
                                // Kaldir dugmesi: ekran okuyucu adi oyun adini da icerir. DS-02: ortak ikincil dugme yuzu.
                                button_face(g, rb, hit, reset, ButtonKind::Secondary, !lock, s, now, hits);
                                hits.add(hit, rb, reset + L": " + name, AccRole::Button, false, -1, !lock);
                            }
                        }
                        y += 1 + rh;
                    }
                }
            }
            // 6. Destek paketi — Olustur (cokme kaydi secenegi Olustur'un kip penceresinde).
            const auto support = t(StringId::SettingsSupport);
            row(support, p.text_primary, [&](const RectF& c) {
                button_at(c, Hit::SupportCreate, create, !sv.bundle_busy && !lock, false, t(StringId::SupportBundleCreate),
                          t(StringId::SettingsSupportDesc));
            });
            lines(support_lines);
        }
        // 3. Hakkinda ve lisanslar — Goster / Gizle; tam lisans metni acilir bolumde (uygulamada bulunmak zorunda).
        const auto lic = t(StringId::SettingsLicenses);
        row(lic, p.text_primary, [&](const RectF& c) {
            button_at(c, Hit::SettingsLicenses, s.settings_licenses_open ? hide : show, true, false,
                      (s.settings_licenses_open ? hide : show) + L": " + lic, a11y_join(t(StringId::AboutTitle), t(StringId::SettingsLicensesDesc)),
                      s.settings_licenses_open);
        });
        // P12-B2a: "Hakkinda" acilirinda (varsayilan kapali): ag aciklamasi (P9-B7: guncelleme denetimi ne zaman / nereye;
        // internet hizi testi yalniz Evet sonrasi; sabit metin), sonra bilesenler ve lisans metinleri. Ekrandan kalkan ag notlari
        // ayrica Denetle dugmesinin HelpText'inde okunur.
        if (s.settings_licenses_open)
            lines({{t(StringId::SettingsNetNoteUpdate), p.text_secondary},
                   {t(StringId::SettingsNetNoteSpeed), p.text_secondary},
                   {comps, p.text_primary},
                   {mit, p.text_secondary},
                   {ofl_text(), p.text_secondary}});
        return y - list.y;
    };
    const float h = pass(false);
    draw_card(g, {list.x, list.y, lw, h}, false, depth::kPanelCardRadius);
    status_i = 0;
    (void)pass(true);
    return h + kTopInset;
}

}  // namespace ef::ui
