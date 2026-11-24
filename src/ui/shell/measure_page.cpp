// UI-03: Olcumler sayfasi — yerlesim (orkestrator), baslik ozeti, Test karti ve ortak kart parcalari. P9-B5: kartlar
// measure_result_card.cpp / measure_cards.cpp / measure_history_card.cpp'ye, metin modeli measure_page_model.cpp'ye ayrildi.
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

void Screen::test_button(Graphics& g, float cx, float cy, const core::measure::MeasureState& m, UiState& s, std::int64_t now, bool enabled,
                         HitMap& hits, const std::wstring& name, const std::wstring& help) {
    // Spec 2026-11-12 C: Test kartinin yuvarlak baslat dugmesi. Bosta: oynat simgesi + camgobegi halka; halka hareket
    // acikken (breathe_live: Panel'in nefes kapilari) yavas nefes alir, kapaliyken sabittir. Test surerken: durdur karesi +
    // iz halkasi uzerinde yesil ilerleme yayi (yalniz gercek oran: sure ya da tamamlanan asama; yoksa yay yok). Durum yalniz
    // renge dayanmaz: altinda "Testi baslat" / "Durdur" yazar. Yuksek kontrastta nefes yok; halka ve simge sistem renkleri.
    const auto& p = pal();
    const bool dark = pal_ == &theme_.dark;
    const bool running = cm::busy(m);
    const Hit id = running ? Hit::TestStop : Hit::TestStart;
    const float d = theme_.test_button_d, r = d / 2;
    const RectF box{cx - r, cy - r, d, d};
    const float hv = enabled ? hover_value(s, id, now) : 0.f;
    const float pr = enabled ? press_value(s, id, now) : 0.f;
    const float dim = enabled ? 1.f : alpha_scale::kDisabled;
    const float thick = theme_.power_ring_thick;
    // Bilesen geometrisi (token degil): simge boyu (cap orani), dis halkanin dugmeye uzakligi ve opakligi.
    constexpr float kGlyph = 0.2f, kOuterW = 1.5f, kOuterNear = 4.f, kOuterFar = 8.f, kOuterAlphaNear = 0.55f, kOuterAlphaFar = 0.12f,
                    kOuterRest = 0.3f;
    const Color ring_c = dark ? p.cyan : p.border;
    if (!running && enabled) {
        const float w = s.breathe_live && dark ? breathe_wave(now, theme_.motion.breathe_period) : kOuterRest;
        const float a = dark ? kOuterAlphaNear + (kOuterAlphaFar - kOuterAlphaNear) * w : 1.f;
        g.arc(cx, cy, r + kOuterNear + (kOuterFar - kOuterNear) * w, kOuterW, 0.f, 360.f, ring_c.with_alpha(a));
    }
    Color body = mix(p.surface_raised, p.accent, dark ? hv * alpha_scale::kChipHoverTint : 0.f);
    body = mix(body, p.background, alpha_scale::kPrimaryPressDim * pr);
    g.fill_circle(cx, cy, r, body);
    if (running) {
        auto f = cm::progress_fraction(m);
        if (!f) f = cm::stage_fraction(m);
        const float frac = f ? static_cast<float>(std::clamp(*f, 0.0, 1.0)) : 0.f;
        const Color track = ensure_contrast(p.border, p.surface_raised, p.text_secondary, 3.0);
        g.ring(cx, cy, r - thick / 2, thick, frac, track.with_alpha(dim), p.accent.with_alpha(dim));
    } else {
        g.ring(cx, cy, r - thick / 2, thick, 1.f, ring_c.with_alpha(dim), ring_c.with_alpha(dim));
    }
    const float gs = d * kGlyph;
    const Color glyph = (running || !dark ? p.text_primary : mix(p.cyan, p.text_primary, hv)).with_alpha(dim);
    if (running) {
        g.fill_round({cx - gs / 2, cy - gs / 2, gs, gs}, 2.f, glyph);  // durdur karesi
    } else {
        // Oynat ucgeni (ic ice ucgenler: dolu gorunur), optik ortalama icin hafif saga.
        for (const float k : {1.f, 0.72f, 0.44f, 0.16f}) {
            const float hx = gs * 0.55f * k, hy = gs * 0.62f * k, ox = gs * 0.08f;
            const std::array<Point, 4> tri{{{cx - hx + ox, cy - hy}, {cx + hx + ox, cy}, {cx - hx + ox, cy + hy}, {cx - hx + ox, cy - hy}}};
            g.polyline(tri, 3.f, glyph);
        }
    }
    if (s.focus_visible && s.focus == id) focus_ring(g, box, r, hits);
    hits.add(id, box, name, AccRole::Button, false, -1, enabled);
    if (!help.empty()) hits.meta[id].help = help;
}

// Secim cipi (kaynak/sure/birim; BOOST maketi: secili = vurgu %15 zemin + %40 cizgi + yari kalin vurgu metni, degil =
// beyaz %4 zemin + beyaz %10 cizgi + soluk metin). Test islerken devre disi: soluk, hover yok; secim kenari gorunur kalir.
// Yuksek kontrastta secim 2 DIP vurgu kenari + sistem renkleri. radius < 0: kontrol koseli.
void Screen::measure_chip(const MeasureCtx& mc, const RectF& r, Hit id, const std::wstring& text, const std::wstring& acc_name,
                         bool selected, Color idle_fill, float radius, bool off) {
    Graphics& g = mc.g;
    UiState& s = mc.s;
    const std::int64_t now = mc.now;
    HitMap& hits = mc.hits;
    const Palette& p = mc.p;
    const bool locked = mc.locked;
    const bool dark = mc.dark;
    const float rad = radius < 0 ? theme_.radius_control : radius;
    const bool en = !locked && !off;
    // DS-02: cip basina hover/basma (paylasilan slotlar kaldirildi); azaltilmis harekette aninda.
    const float hv = en ? hover_value(s, id, now) : 0.f;
    const float pv = en ? press_value(s, id, now) : 0.f;
    const float dim = en ? 1.f : alpha_scale::kDisabled;
    if (dark) {
        const Color base = selected ? mix(p.surface, p.accent, alpha_scale::kChipTint) : Color{1.f, 1.f, 1.f, 0.04f};
        // Secenek cipi (rounded): secili degilken cizgisiz, beyaz %5 zemin (maket bg-white/5); kaynak dugmesi beyaz %4 + %10 cizgi.
        const bool flat_idle = !selected && radius == kOptRadius;
        Color bg = flat_idle ? Color{1.f, 1.f, 1.f, 0.05f} : base;
        if (en && (hv > 0.f || pv > 0.f)) bg = mix(bg, p.accent.with_alpha(selected ? 0.22f : 0.10f), std::min(1.f, hv + pv));
        g.fill_round(r, rad, bg.with_alpha(bg.a * dim));
        const Color edge = selected ? p.accent.with_alpha(0.40f) : Color{1.f, 1.f, 1.f, flat_idle ? 0.f : 0.10f};
        const Color eh = mix(edge, p.accent, std::min(1.f, hv * 0.6f + pv));
        g.stroke_round(r.inset(0.5f), rad, eh.with_alpha(eh.a * dim));
        const Color fg = selected ? p.accent : mix(p.text_secondary, p.text_primary, hv);
        g.text(selected ? Role::Semi12 : Role::Small12, text, r, fg.with_alpha(dim), {Align::Center, false, true});
    } else {
        const Color fill = selected ? mix(p.surface_raised, p.accent, en ? alpha_scale::kSelectionTint : alpha_scale::kQuietSelectionTint)
                                    : mix(idle_fill, p.accent, std::min(1.f, hv * alpha_scale::kChipHoverTint + pv * alpha_scale::kBadgeTint));
        g.fill_round(r, rad, fill);
        // Secim yalniz renkle degil: kalin vurgu kenari (devre disiyken yari saydam).
        const Color stroke = selected ? (en ? p.accent : p.accent.with_alpha(alpha_scale::kDisabled))
                                      : mix(p.border, p.accent, std::min(1.f, hv * alpha_scale::kHoverTint + pv));
        g.stroke_round(r, rad, stroke, selected ? 2.f : 1.f);
        g.text(Role::Body, text, r, selected && en ? p.text_primary : mix(p.text_secondary, p.text_primary, hv), {Align::Center, false, true});
    }
    if (en && s.focus_visible && s.focus == id) focus_ring(g, r, rad, hits);
    hits.add(id, r, acc_name, AccRole::RadioButton, selected, -1, en);
}

// Kart basligi (maket: "M1 //" 10px mono + gap-2 + 14px/600 baslik; kod rengi karta ozgu, yuksek kontrastta metin rengi).
// Satir yuksekligi dondurulmez; cagiran head_h verir. Baslik genisligi doner.
float Screen::measure_card_head(const MeasureCtx& mc, float hx, float hy, float hw, std::wstring_view code, const Color& tag,
                                const std::wstring& title, float h) {
    Graphics& g = mc.g;
    const Palette& p = mc.p;
    const bool dark = mc.dark;
    constexpr float kUb = 10000.f;
    const std::wstring c(code);
    const float cw = std::ceil(g.measure(Role::Mono10, c, kUb, false).w) + 1.f;
    g.text(Role::Mono10, c, {hx, hy, cw, h}, dark ? tag : p.text_primary, {Align::Leading, false, true});
    const float tx = hx + cw + theme_.space(1);
    const float tw = std::max(hw - (tx - hx), 1.f);
    g.text(Role::Title14, fit_text(g, Role::Title14, title, tw), {tx, hy, tw, h}, p.text_primary, {Align::Leading, false, true});
    return std::ceil(g.measure(Role::Title14, title, kUb, false).w) + 1.f + (tx - hx);
}

float Screen::measure_opt_w(const MeasureCtx& mc, const std::wstring& text) {
    Graphics& g = mc.g;
    const bool dark = mc.dark;
    return dark ? std::ceil(g.measure(Role::Semi12, text, kUnbounded, false).w) + 18.f
                : std::max(theme_.button_min_w, std::ceil(g.measure(Role::Body, text, kUnbounded, false).w) + 2 * theme_.space(2));
}

// Ust satir 1/3: Test karti (BOOST maketi 2026-11-16 birebir, M1). Baslik ("M1 //" + "Test"); kaynak dugmeleri
// (Uygulama Testi | Oyunda Test, esit genislik); secili kaynagin tek aciklamasi; baslatma oncesi uyari / neden satirlari;
// altta cizgiyle ayrilmis satir: sagda "Testi baslat" (test surerken "Durdur", solda "kalan N sn"). Kapsam / sure / depolama
// / canli ekran secenekleri maketteki secenek paneli gibi hemen altindaki kartta (test_options) her zaman gorunur: ek tik
// yok. Maketteki disli dugmesi ve "Canli Arena HUD" yok (secenekler zaten acik; canli ekran anahtari secenek panelinde).
void Screen::measure_test_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    UiState& s = mc.s;
    const std::int64_t now = mc.now;
    HitMap& hits = mc.hits;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float sl = mc.sl;
    const float label_h = mc.label_h;
    const float g0 = mc.g0;
    const float g1 = mc.g1;
    const float g2 = mc.g2;
    const float opt_h = mc.opt_h;
    const float src_h = mc.src_h;
    const bool tr = mc.tr;
    const bool busy = mc.busy;
    const bool dark = mc.dark;
    const bool balance_scope = mc.balance_scope;
    const Color divider = mc.divider;
    const Color pill_idle = mc.pill_idle;
    const bool available = m.source == cm::TestSource::Builtin ? m.builtin_available && !m.gate_blocked : m.ingame_available;
    std::wstring note;
    if (!available && !busy) {
        // Oyunda Test: neden gercek izleyici + uyumluluk kapisindan gelir (MeasureState.ingame_reason_key).
        // Katalogda metin yoksa ham kod yazilmaz; genel "kullanilamiyor" metnine duser.
        if (m.source == cm::TestSource::InGame) note = m.ingame_reason_key.empty() ? std::wstring() : tk(m.ingame_reason_key);
        else note = m.gate_reason_key.empty() ? std::wstring() : tk(m.gate_reason_key);
        if (note.empty()) note = t(StringId::MeasureUnavailable);
    }
    // R13: uzun test surerken normal test baslatilmaz; nedeni kartta yazilir.
    const std::wstring endurance_note = m.endurance.running && !busy && available ? t(StringId::MeasureEnduranceLocked) : std::wstring();
    // EF-X22: karisik yuk (sahne + disk) yalniz disk secildiginde kosar; baslatmadan once acik is uyarisi (bilgi).
    const bool mixed_load = m.source == cm::TestSource::Builtin && !balance_scope && !busy && available && !m.endurance.running && !m.storage_blocked &&
                            m.storage_choice > 0 && m.storage_choice <= m.storage_roots.size();
    const std::wstring mixed_note = mixed_load ? t(StringId::MeasureMixedLoadWarning) : std::wstring();
    // Disk veri riski bildirdiyse disk testi calismaz: neden her zaman kartta yazilir (ayar karti kapaliyken de).
    const std::wstring storage_note = m.source == cm::TestSource::Builtin && m.storage_blocked ? t(StringId::BenchStorageWriteBlocked) : std::wstring();
    // 0.2.1 geri bildirimi: baslatmadan once kirmizi "!" + tek kisa uyari (test surerken gizli). Disk secili karisik yukte
    // disk uyarisi bununla tek satirda birlesir (iki uzun uyari ust uste binmez); tam karisik yuk metni yardim metninde kalir.
    const auto pre = pretest_warning(m.source == cm::TestSource::Builtin, busy, available, m.endurance.running, m.storage_blocked, mixed_load);
    const std::wstring pretest_note = pre == PretestWarning::StressDisk ? t(StringId::MeasurePretestWarningDisk)
                                      : pre == PretestWarning::Stress   ? t(StringId::MeasurePretestWarning)
                                                                        : std::wstring();

    struct RowChip {
        Hit id;
        std::wstring text, acc;
        bool selected;
        float w;
        RectF rel;  // secenek alanina gore
        bool off = false;  // Darbogaz kapsaminda depolama cipleri devre disi
    };
    struct Opt {
        std::wstring label, help, note;
        std::vector<RowChip> chips;
        bool toggle = false, toggle_on = false;
        float label_h = 0, opts_h = 0, note_h = 0, h = 0;
    };
    struct OptLayout {
        std::vector<Opt> opts;
        int cols = 1;
        float ww = 0, win = 0, wgap = 0, total_h = 0;
        std::vector<float> row_h;
    };
    // Test karti icindeki secenek bolumu (sahip duzeni): Kapsam / Sure / Depolama renkli seritli kutular. Yalniz Uygulama Testi.
    auto build_options = [&](float inner_w) -> OptLayout {
        OptLayout L;
        if (m.source != cm::TestSource::Builtin) return L;
    std::vector<Opt> opts;
    {
        Opt o;
        o.label = t(StringId::MeasureScopeTitle);
        const std::array<std::pair<Hit, StringId>, 2> scopes{{{Hit::ScopeGeneral, StringId::MeasurePillGeneral}, {Hit::ScopeBalance, StringId::MeasurePillBottleneck}}};
        const std::array<StringId, 2> scope_full{StringId::MeasureScopeGeneral, StringId::MeasureScopeBalance};
        for (std::size_t i = 0; i < scopes.size(); ++i) {
            auto text = t(scopes[i].second);
            o.chips.push_back({scopes[i].first, text, o.label + L": " + t(scope_full[i]), (scopes[i].first == Hit::ScopeBalance) == balance_scope, measure_opt_w(mc, text), {}});
        }
        opts.push_back(std::move(o));
    }
    {
        Opt o;
        o.label = t(StringId::MeasureRowDuration);
        o.help = a11y_join(t(balance_scope ? (m.mode == cm::TestMode::Full ? StringId::BenchModeBalanceFullNote : StringId::BenchModeBalanceQuickNote)
                                           : (m.mode == cm::TestMode::Full ? StringId::BenchModeFullNote : StringId::BenchModeQuickNote)),
                           balance_scope ? std::wstring() : t(StringId::MeasureDurationNote));
        for (const auto& [id, sid] : std::array<std::pair<Hit, StringId>, 2>{
                 {{Hit::ModeQuick, balance_scope ? StringId::MeasureModeBalanceQuickChip : StringId::MeasureModeQuickChip},
                  {Hit::ModeFull, balance_scope ? StringId::MeasureModeBalanceFullChip : StringId::MeasureModeFullChip}}}) {
            auto text = t(sid);
            const bool selected = (id == Hit::ModeFull) == (m.mode == cm::TestMode::Full);
            o.chips.push_back({id, text, o.label + L": " + text, selected, measure_opt_w(mc, text), {}});
        }
        opts.push_back(std::move(o));
    }
    if (!m.storage_roots.empty()) {  // Darbogaz kapsaminda da gorunur (devre disi): depolama testi yalniz Genel kapsamda calisir
        Opt o;
        o.label = t(StringId::MeasureRowStorage);
        o.help = t(StringId::MeasureStorageNote);
        const auto none = t(StringId::MeasureStorageNone);
        o.chips.push_back({hit_of(HitKind::StorageChoice, 0), none, o.label + L": " + none, !balance_scope && m.storage_choice == 0, measure_opt_w(mc, none), {}, balance_scope});
        for (std::size_t k = 0; k < m.storage_roots.size() && k < cm::kMaxStorageRoots; ++k) {
            auto root = widen(m.storage_roots[k]);
            o.chips.push_back({hit_of(HitKind::StorageChoice, k + 1), root, o.label + L": " + root, !balance_scope && m.storage_choice == k + 1,
                               std::max(theme_.touch_min, measure_opt_w(mc, root)), {}, balance_scope});
        }
        // Birim secildiyse ne yazilacagi kartta kisaca gorunur (tam kurallar yardim metninde); sure ust siniri da yazar.
        if (balance_scope) o.note = t(StringId::MeasureStorageGeneralOnly);
        else if (m.storage_choice > 0 && m.storage_choice <= m.storage_roots.size())
            o.note = t(StringId::MeasureStorageShortNote) + L" " +
                     t(m.mode == cm::TestMode::Full ? StringId::MeasureStorageTimeFull : StringId::MeasureStorageTimeQuick);
        opts.push_back(std::move(o));
    }
    s.measure_live_toggle = false;
    s.measure_option_note = false;
    s.measure_option_note_bottom = 0;
    // Sahip duzeni: tek sutun, "etiket | secenekler" satirlari; kutu ici kutu yok, satirlari ince cizgi ayirir. Her satirin
    // etiketinin onunde kendi renginde kucuk bir nokta (renk tek basina anlam tasimaz: etiket metni de yazili).
    float label_col = 0;
    for (auto& o : opts) label_col = std::max(label_col, std::ceil(g.measure(Role::Semi12, o.label, kUnbounded, false).w));
    label_col = std::min(label_col + 14.f, std::floor(inner_w * 0.34f));  // +14: nokta + bosluk
    const float chips_w = std::max(inner_w - label_col - g2, 1.f);
    const float rpad = g1;  // satir ic dikey bosluk (8)
    std::vector<float> row_h;
    float total_h = 0;
    for (auto& o : opts) {
        o.label_h = g.measure(Role::Semi12, o.label, std::max(label_col - 14.f, 1.f)).h;
        float cx = 0, cy = 0;
        for (auto& c : o.chips) {
            c.w = std::min(c.w, chips_w);
            if (cx > 0 && cx + c.w > chips_w) cx = 0, cy += opt_h + g0;
            c.rel = {cx, cy, c.w, opt_h};
            cx += c.w + g1;
        }
        o.opts_h = o.toggle ? 0.f : cy + opt_h;
        o.note_h = o.note.empty() ? 0.f : g.measure(Role::Small11, o.note, chips_w).h;
        o.h = 2 * rpad + std::max(o.label_h, o.opts_h) + (o.note_h > 0 ? g0 + o.note_h : 0.f);
        row_h.push_back(o.h);
        total_h += o.h + (row_h.size() > 1 ? 1.f : 0.f);  // 1 DIP ayirici cizgi
    }
    L.opts = std::move(opts);
    L.cols = 1;
    L.ww = inner_w;
    L.win = chips_w;
    L.wgap = label_col;
    L.total_h = total_h;
    L.row_h = std::move(row_h);
    return L;
    };

    const float inner_w = std::max(slot.area.w - 2 * pad, 1.f);
    const OptLayout ol = build_options(inner_w);
    const std::wstring title = t(StringId::MeasureCardTest);
    const std::wstring label = cm::can_stop(m) ? t(StringId::MeasureTestStopTitle) : busy ? t(StringId::MeasureStoppingButton) : t(StringId::MeasureStart);
    std::wstring sub;  // test surerken: "kalan N sn" (toplam sure biliniyorsa) ya da gecen sure
    if (busy) {
        const double el = cm::display_elapsed_s(m, now);
        if (const auto left = test_remaining_s(m.total_s, el)) sub = replace_all(t(StringId::MeasureTestRemaining), L"{n}", std::to_wstring(*left));
        else sub = replace_all(t(StringId::MeasureElapsedOpen), L"{e}", widen(cm::format_number(el, 0, tr)));
    }
    // Notlar: baslatma oncesi uyari (kirmizi "!" simgeli; karisik yuk uyarisi bunun icinde birlesik), disk riski, uzun
    // test kilidi, kullanilamama nedeni.
    constexpr float kWarnIcon = 14.f;  // "!" simgesinin capi (bilesen geometrisi; token degil)
    struct Note {
        const std::wstring* text;
        Color c;
        bool icon;
        float h = 0, w = 0;  // w: simgeli notta metin kutusu genisligi
    };
    const std::array<Note, 4> note_src{{{&pretest_note, p.text_primary, true}, {&storage_note, p.warning, false},
                                        {&endurance_note, p.text_secondary, false}, {&note, p.text_secondary, false}}};
    std::vector<Note> notes;
    float notes_h = 0;
    for (const auto& n : note_src)
        if (!n.text->empty()) {
            Note k = n;
            if (k.icon) {
                const float avail = std::max(inner_w - kWarnIcon - g1, 1.f);
                k.w = std::min(std::ceil(g.measure(Role::Small12, *k.text, kUnbounded, false).w) + 1.f, avail);
                k.h = std::max(g.measure(Role::Small12, *k.text, k.w).h, kWarnIcon);
            } else {
                k.h = g.measure(Role::Small12, *k.text, inner_w).h;
            }
            notes_h += g0 + k.h;
            notes.push_back(k);
        }
    const std::array<std::pair<Hit, StringId>, 2> segs{{{Hit::SourceBuiltin, StringId::TestBuiltin}, {Hit::SourceGame, StringId::TestGame}}};
    const float head_h = theme_.role(Role::Title14).line;
    const float btn_h = panel_button_rect(g, label, !busy, false, true).h;
    const float bw = panel_button_rect(g, label, !busy, false, true).w;
    // --- Yatay serit (sahip duzeni, C): [M1 // Test] [kaynak] | Kapsam | Sure | Depolama ... [Testi baslat]. Bloklar soldan saga
    // dizilir, sigmayan blok alt satira sarar; baslat dugmesi son satirin sag ucundadir. ---
    const float seg_gap = g1;
    const auto strip_chip_w = [&](const std::wstring& tx) { return std::max(measure_opt_w(mc, tx) - 3.f, 30.f); };  // seritte chip biraz dar
    std::array<float, 2> seg_w{};
    for (std::size_t i = 0; i < segs.size(); ++i) seg_w[i] = std::max(measure_opt_w(mc, t(segs[i].second)) + g2, 92.f);
    const float head_code_w = std::ceil(g.measure(Role::Mono10, L"M1 //", kUnbounded, false).w) + 1.f;
    const float head_w = head_code_w + g1 + std::ceil(g.measure(Role::Title14, title, kUnbounded, false).w) + 1.f;
    const float sub_w = sub.empty() ? 0.f : std::min(std::ceil(g.measure(Role::Small12, sub, kUnbounded, false).w) + 1.f, inner_w * 0.3f);
    const float start_w = bw + (busy ? std::max(sub_w, 70.f) + g2 : 0.f);
    const float blk_gap = g1 + 2.f;  // bloklar arasi (serit tek satira sigsin)
    constexpr float kDiv = 11.f;    // grup oncesi dikey ayirici + bosluk
    struct Blk {
        int kind;  // 0 baslik, 1 kaynak, 2 grup, 3 baslat
        std::size_t idx;
        float w, h;
        float x = 0;
        std::size_t row = 0;
    };
    std::vector<Blk> blks;
    blks.push_back({0, 0, head_w, head_h});
    blks.push_back({1, 0, seg_w[0] + seg_w[1] + seg_gap, src_h});
    for (std::size_t i = 0; i < ol.opts.size(); ++i) {
        const auto& o = ol.opts[i];
        const float lw = std::ceil(g.measure(Role::Semi12, o.label, kUnbounded, false).w) + 1.f + 9.f;  // nokta + bosluk
        float cw = 0;
        for (const auto& c : o.chips) cw += strip_chip_w(c.text) + g1;
        blks.push_back({2, i, kDiv + lw + g1 + cw, std::max(opt_h, label_h)});
    }
    std::vector<float> row_y, row_h;
    {
        float cx = 0, cy = 0, rh = 0;
        std::size_t row = 0;
        const auto close_row = [&] {
            row_h.push_back(rh);
            row_y.push_back(cy);
            cy += rh + g1;
            cx = 0;
            rh = 0;
            ++row;
        };
        for (auto& b : blks) {
            const float need = b.w + (cx > 0 ? blk_gap : 0.f);
            if (cx > 0 && cx + need > inner_w) close_row();
            b.x = cx > 0 ? cx + blk_gap : 0.f;
            b.row = row;
            cx = b.x + b.w;
            rh = std::max(rh, b.h);
        }
        // Baslat dugmesi: son satirin sag ucu; sigmazsa yeni satir.
        if (cx > 0 && cx + blk_gap + start_w > inner_w) close_row();
        Blk sb{3, 0, start_w, std::max(btn_h, src_h)};
        sb.x = inner_w - start_w;
        sb.row = row;
        blks.push_back(sb);
        rh = std::max(rh, sb.h);
        close_row();
    }
    float strip_h = 0;
    for (std::size_t r = 0; r < row_h.size(); ++r) strip_h += row_h[r] + (r + 1 < row_h.size() ? g1 : 0.f);
    // Secenek notlari (ornek: secili birim icin yazma + sure notu) seridin altinda kisa satirlar.
    float onotes_h = 0;
    for (const auto& o : ol.opts)
        if (!o.note.empty()) onotes_h += g0 + g.measure(Role::Small11, o.note, inner_w).h;
    const float notes_block = notes.empty() ? 0.f : g2 + 1.f + notes_h;  // bosluk + ince cizgi + uyarilar
    RectF card;
    if (!slot.place(pad + strip_h + onotes_h + notes_block + pad, card)) return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + pad;
    const float y0 = card.y + pad;
    for (const auto& b : blks) {
        const float ry = y0 + row_y[b.row], rh = row_h[b.row];
        const float bx = x + b.x;
        const float by = ry + (rh - b.h) / 2;
        if (b.kind == 0) {
            g.text(Role::Mono10, L"M1 //", {bx, by, head_code_w, b.h}, dark ? p.accent : p.text_primary, {Align::Leading, false, true});
            g.text(Role::Title14, title, {bx + head_code_w + g1, by, std::max(b.w - head_code_w - g1, 1.f), b.h}, p.text_primary, {Align::Leading, false, true});
        } else if (b.kind == 1) {
            for (std::size_t i = 0; i < segs.size(); ++i) {
                const bool selected = (segs[i].first == Hit::SourceBuiltin) == (m.source == cm::TestSource::Builtin);
                const auto full = t(segs[i].second);
                measure_chip(mc, {bx + (i == 0 ? 0.f : seg_w[0] + seg_gap), by, seg_w[i], src_h}, segs[i].first, full, full, selected, pill_idle, kSrcRadius);
            }
        } else if (b.kind == 2) {
            const auto& o = ol.opts[b.idx];
            const Color dots[] = {p.data_gpu, p.warning, p.accent, p.text_secondary};
            g.fill({bx + 2.f, ry + 3.f, 1.f, std::max(rh - 6.f, 1.f)}, dark ? Color{1.f, 1.f, 1.f, 0.08f} : divider);
            float cx = bx + kDiv;
            g.fill_circle(cx + 3.f, by + b.h / 2, 3.f, dots[std::min<std::size_t>(b.idx, 3)]);
            const float lw = std::ceil(g.measure(Role::Semi12, o.label, kUnbounded, false).w) + 1.f;
            g.text(Role::Semi12, o.label, {cx + 9.f, by, lw, b.h}, p.text_primary, {Align::Leading, false, true});
            cx += 9.f + lw + g1;
            for (const auto& c : o.chips) {
                const float cw = strip_chip_w(c.text);
                measure_chip(mc, {cx, by + (b.h - opt_h) / 2, cw, opt_h}, c.id, fit_text(g, Role::Small12, c.text, std::max(cw - 2 * g1, 1.f)), c.acc, c.selected, pill_idle,
                     kOptRadius, c.off);
                if (const auto it = hits.meta.find(c.id); it != hits.meta.end()) it->second.help = o.help;
                cx += cw + g1;
            }
        } else {
            const bool enabled = busy ? cm::can_stop(m) : cm::can_start(m);
            const std::wstring help = a11y_join(a11y_join(a11y_join(pretest_note, mixed_note), endurance_note), note);
            const Hit bid = busy ? Hit::TestStop : Hit::TestStart;
            panel_button(g, bx + b.w - bw, ry + (rh - btn_h) / 2, bid, label, !busy, s, now, hits, enabled, false, true);
            if (!help.empty()) hits.meta[bid].help = help;
            if (busy) {
                const float sw = std::max(b.w - bw - g2, 1.f);
                // M04/M06: durum basligi + asama n/5 + gecen sure tek salt okunur ogede (ekranda yalniz "kalan N sn").
                StringId st = StringId::MeasureRunning;
                if (m.phase == cm::TestPhase::Preparing) st = StringId::TestPreparing;
                if (m.phase == cm::TestPhase::Stopping) st = StringId::MeasureStopping;
                std::wstring line = measure_stage_text(m);
                const auto e = widen(cm::format_number(cm::display_elapsed_s(m, now), 0, tr));
                const auto elapsed = m.total_s > 0 ? replace_all(replace_all(t(StringId::MeasureElapsed), L"{e}", e), L"{t}", widen(cm::format_number(m.total_s, 0, tr)))
                                                   : replace_all(t(StringId::MeasureElapsedOpen), L"{e}", e);
                line = line.empty() ? elapsed : line + L" · " + elapsed;
                const RectF sr{bx, ry + (rh - btn_h) / 2, sw, btn_h};
                if (!sub.empty()) {
                    TextOpts so{Align::Trailing, false, true};
                    so.tabular = true;  // M13: saniye degisirken genislik oynamaz
                    g.text(Role::Small12, fit_text(g, Role::Small12, sub, sr.w), sr, p.text_secondary, so);
                }
                hits.add_text(Hit::Status0, sr, a11y_join(a11y_join(t(st), line), sub));
                // Ilerleme: kartin alt kenarinda ince cizgi (gercek oran; yoksa ilerleme ogesi yok).
                auto f = cm::progress_fraction(m);
                if (!f) f = cm::stage_fraction(m);
                if (f) {
                    const RectF bar{card.x + 8.f, card.bottom() - 5.f, card.w - 16.f, 2.f};
                    g.fill_round(bar, 1.f, dark ? Color{1.f, 1.f, 1.f, 0.10f} : p.border);
                    g.fill_round({bar.x, bar.y, std::max(2.f, bar.w * static_cast<float>(std::clamp(*f, 0.0, 1.0))), 2.f}, 1.f, p.accent);
                    hits.add_progress(Hit::Progress0, {bar.x, bar.y - 4.f, bar.w, 10.f}, t(StringId::MeasureProgressName), *f * 100.0);
                }
            }
        }
    }
    float y = y0 + strip_h;
    for (const auto& o : ol.opts)
        if (!o.note.empty()) {
            y += g0;
            const float nh = g.measure(Role::Small11, o.note, inner_w).h;
            g.text(Role::Small11, o.note, {x, y, inner_w, nh}, p.text_secondary, {Align::Leading, true});
            const float nb = y + nh - s.scroll;
            s.measure_option_note_bottom = s.measure_option_note ? std::max(s.measure_option_note_bottom, nb) : nb;
            s.measure_option_note = true;
            y += nh;
        }
    if (!notes.empty()) {
        y += g2;
        g.fill({x, y, inner_w, 1.f}, dark ? Color{1.f, 1.f, 1.f, 0.06f} : divider);
        y += 1.f;
    }
    for (const auto& n : notes) {
        y += g0;
        if (n.icon) {
            // Kirmizi "!" (hata rengi; metin disi 3:1) + uyari metni; uzun metin simgenin yaninda kirilir.
            const Color ic = ensure_contrast(p.error, p.surface, p.text_primary, 3.0);
            const float r0 = kWarnIcon / 2, icx = x + r0, icy = y + std::min(sl, n.h) / 2;
            constexpr float kRing = 1.5f, kBarW = 1.8f, kBarTop = 3.6f, kBarBottom = 0.9f, kDotY = 3.3f, kDotR = 1.1f;  // simge geometrisi
            g.arc(icx, icy, r0 - kRing / 2, kRing, 0.f, 360.f, ic);
            const std::array<Point, 2> bar{{{icx, icy - kBarTop}, {icx, icy + kBarBottom}}};
            g.polyline(bar, kBarW, ic);
            g.fill_circle(icx, icy + kDotY, kDotR, ic);
            g.text(Role::Small12, *n.text, {x + kWarnIcon + g1, y, n.w, n.h}, n.c, {Align::Leading, true});
        } else {
            g.text(Role::Small12, *n.text, {x, y, inner_w, n.h}, n.c, {Align::Leading, true});
        }
        y += n.h;
    }
    if (!note.empty()) {
        const Hit src_hit = m.source == cm::TestSource::InGame ? Hit::SourceGame : Hit::SourceBuiltin;
        if (const auto it = hits.meta.find(src_hit); it != hits.meta.end()) it->second.help = note;
    }
}

float Screen::measure_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin 16 DIP altinda baslatir; bu sayfanin seridinde dugme yok (yalniz
    // 24 DIP baslik), govde Edge cizimine gore 5 DIP iner (Oyun hazirligi ile ayni).
    constexpr float kTopInset = 5.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    const auto& p = pal();
    const auto& m = vm.measure;
    s.measure = m;
    constexpr float kCardIn = 15.f;  // BOOST maketi: card-v2 p-3.5 (14) + 1 DIP cizgi
    const float pad = kCardIn;
    const float gap = theme_.space(2);
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const bool busy = cm::busy(m);
    // EF-A11Y: sabit durum metinleri Status0+0..5; bilesen/teshis/karsilastirma metinleri 6'dan itibaren sirali.
    StatusSeq seq{6};
    Stack full{body, theme_.card_gap};
    game_banner(g, full, body, vm, s, now, hits);
    const float top = full.cursor;
    // R13: uzun test surerken kaynak/sure/birim/hedef degismez (ikinci test ayni anda baslamaz).
    const bool locked = busy || m.endurance.running;
    const float section_h = theme_.role(Role::Section).line;

    // Sayfa olculeri (kartlar bunlari MeasureCtx uzerinden okur). Secim cipi / kart basligi ciziciler measure_chip /
    // measure_card_head (yukarida); maket olculeri: kaynak dugmesi 30, secenek cipi 28 DIP.
    const bool dark = pal_ == &theme_.dark;
    const float sl = theme_.role(Role::Secondary).line;
    const float bl = theme_.role(Role::Body).line;
    const float label_h = theme_.role(Role::Label).line;
    const float g0 = theme_.space(0), g1 = theme_.space(1), g2 = theme_.space(2);
    const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
    const float chip_h = theme_.button_row_h;
    const float opt_h = dark ? std::max(28.f, theme_.role(Role::Semi12).line + 10.f) : chip_h;
    const float src_h = dark ? opt_h + 2.f : chip_h;
    const Color pill_idle = mix(p.surface, p.surface_raised, alpha_scale::kDisabled);
    const bool balance_scope = m.source == cm::TestSource::Builtin && m.scope == cm::BuiltinScope::Balance;

    MeasureCtx mc{g,     s,       vm,    now,   hits,  seq,   p,      m,        pad,    gap,   section_h, sl,
                  bl,    label_h, g0,    g1,    g2,    chip_h, opt_h, src_h,    tr,     busy,  locked,    dark,
                  balance_scope, divider, pill_idle};
    const auto test_card = [&](CardSlot& slot) { measure_test_card(mc, slot); };
    const auto result_card = [&](CardSlot& slot) { measure_result_card(mc, slot); };
    const auto draw_endurance = [&](CardSlot& slot) { measure_endurance_card(mc, slot); };
    const auto draw_proof = [&](CardSlot& slot) { measure_proof_card(mc, slot); };  // P1: Performans etkisini olc (kullanici baslatir)
    const auto draw_cancel = [&](CardSlot& slot) { measure_cancel_card(mc, slot); };
    const auto draw_save_failed = [&](CardSlot& slot) { measure_save_failed_card(mc, slot); };
    const auto draw_game_limit = [&](CardSlot& slot) { measure_game_limit_card(mc, slot); };
    const auto draw_stutter = [&](CardSlot& slot) { measure_stutter_card(mc, slot); };
    const auto draw_comparison = [&](CardSlot& slot) { measure_comparison_card(mc, slot); };
    s.measure_recent_rows = 0;
    const auto draw_history = [&](CardSlot& slot) { measure_history_card(mc, slot); };

    // Yerlesim (spec 2026-11-12 C): [uyari / iptal seritleri] -> ust satir uc es kart (Test | Sonuc | Internet hizi; dar
    // govdede Test | Sonuc, altinda Internet hizi tam genislikte; daha darsa alt alta) -> uzun test (tam genislik) -> Darbogaz (yan yana uc es kutu; tam genislik) -> Son 3 test (tam
    // genislik; satir ayrintisi satirin altinda) -> kalan kartlar okuma sirasiyla ikiser esit satir; bilesen raporu
    // (liste) tam genislik. Yan yana duran kartlar ayni ust kenarda ve ayni yukseklikte.
    CardFlow flow;
    flow.area = {body.x, body.y, body.w, 0};
    flow.gap = theme_.card_gap;
    flow.log = &hits.rows;
    flow.cursor = top;
    flow.full(draw_save_failed);
    flow.full(draw_cancel);
    // Darbogaz: gorunen sonuc denge testiyse o; degilse en yeni denge kaydi (Panel "Test et" buraya getirir). Yoksa yok.
    const cm::BalanceView* bv = cm::balance_card_view(m);
    if (!bv) {
        s.measure_bars_seen = false;
        s.balance_report_busy = false;
    }
    const CardFn balance_fn = [&, bv](CardSlot& slot) {
        if (bv) {
            balance_card(g, slot, *bv, s, vm, now, hits, seq);
            return;
        }
        // Henuz Darbogaz kaydi yok: kart yine yerinde durur (Genel testte de Darbogaz alani gorunur), kisa yonlendirme yazar.
        const float in_w = std::max(slot.area.w - 2 * pad, 1.f);
        const std::wstring hint = t(StringId::MeasureBalanceEmpty);
        const float hh = theme_.role(Role::Title14).line, th = g.measure(Role::Small12, hint, in_w).h;
        RectF card;
        if (!slot.place(pad + hh + g2 + th + pad, card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        measure_card_head(mc, card.x + pad, card.y + pad, in_w, L"M4 //", p.cyan, t(StringId::PanelBottleneckTitle), hh);
        g.text(Role::Small12, hint, {card.x + pad, card.y + pad + hh + g2, in_w, th}, p.text_secondary);
        hits.add_text(seq.take(), {card.x + pad, card.y + pad, in_w, hh + g2 + th}, a11y_join(t(StringId::PanelBottleneckTitle), hint));
    };
    {
        const int tcols = measure_top_columns(body.w, flow.gap);
        flow.full(test_card);  // sahip duzeni (C): yatay Test seridi
        if (tcols >= 2) {  // altinda Sonuc | Darbogaz yan yana, ayni yukseklikte
            flow.row({{result_card}, {balance_fn}});
        } else {
            flow.full(result_card);
            flow.full(balance_fn);
        }
        flow.stacked = content_columns(flow.area, theme_).stacked;
    }
    flow.full([&](CardSlot& c) { speed_card(g, c, s, vm, now, hits, seq); });  // sahip duzeni: Internet hizi Darbogaz'in altinda, dar tek cubuk
    flow.full(draw_endurance);
    flow.full(draw_proof);  // P1: Performans etkisini olc (kullanici baslatir)
    // Maket sirasi: Darbogaz, altinda yan yana Kare Dususu teshisi + Performans Acik once/sonra (ikisi de varsa her zaman esli).
    flow.pair_max_ratio = 0.f;
    flow.pairs({{draw_stutter}, {[&](CardSlot& slot) { before_after_card(g, slot, vm); }}});
    flow.full(draw_history);
    {
        std::vector<CardFlow::Item> items;
        items.push_back({draw_game_limit});  // spec 2026-11-12 B: Oyunda Test "ne sinirladi" (yoksa cizilmez)
        if (m.result)
            for (const auto& c : m.result->components) items.push_back({[&, pc = &c](CardSlot& slot) { measure_component_card(mc, *pc, slot); }});
        items.push_back({draw_comparison});
        items.push_back({[&](CardSlot& slot) { components_card(g, slot, vm, hits, seq); }, true});  // DIAG-05: liste, tam genislik
        flow.pair_max_ratio = kPairMaxRatio;  // sonuc kartlari: boylar benzer degilse (tek satirlik karsilastirma) tam genislik
        flow.pairs(items);
    }
    return flow.used();
}

void Screen::measure_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // Baslik seridi: secili degerlerin ozeti (sure · birim). Seritte dugme yoktur; secenekler Test kartinin icindedir.
    (void)s;
    (void)now;
    (void)hits;
    const auto& m = vm.measure;
    std::wstring summary;
    const auto add = [&](const std::wstring& part) { summary += summary.empty() ? part : L" · " + part; };
    if (m.source == cm::TestSource::Builtin && m.scope == cm::BuiltinScope::Balance) {
        add(t(StringId::MeasureScopeBalance));
        add(t(m.mode == cm::TestMode::Full ? StringId::BenchModeBalanceFull : StringId::BenchModeBalanceQuick));
    } else if (m.source == cm::TestSource::Builtin) {
        add(t(m.mode == cm::TestMode::Full ? StringId::BenchModeFull : StringId::BenchModeQuick));
        // Disk engeli nedeni Test kartinda tam metinle yazilir; ozet yalniz secili birimi soyler. Disk testi
        // secilmediyse ("Yapma") ozete girmez: baglamsiz tek kelime neyin yapilmadigini soylemez.
        if (!m.storage_blocked && m.storage_choice > 0 && m.storage_choice <= m.storage_roots.size())
            add(replace_all(t(StringId::MeasureSummaryStorage), L"{root}", widen(m.storage_roots[m.storage_choice - 1])));
    }
    if (summary.empty()) return;
    // BOOST maketi: sagda 12px mono soluk ozet ("15 sn · Disk: C: · Hedef: 240 FPS"; hedef FPS yok).
    const float sw = std::ceil(g.measure(Role::Mono12, summary, kUnbounded, false).w) + 1.f;
    if (const RectF sr = bar.take(sw, theme_.role(Role::Mono12).line); sr.w > 0)
        g.text(Role::Mono12, summary, sr, pal().text_secondary, {Align::Trailing, false, true});
}

std::vector<double> frame_time_bars(const std::vector<double>& fps_series, std::size_t max_bars) {
    std::vector<double> ms;
    ms.reserve(fps_series.size());
    for (const double f : fps_series)
        if (std::isfinite(f) && f > 0) ms.push_back(1000.0 / f);
    if (ms.empty() || max_bars == 0) return {};
    if (ms.size() <= max_bars) return ms;
    std::vector<double> out(max_bars, 0.0);
    const std::size_t n = ms.size();
    for (std::size_t i = 0; i < max_bars; ++i) {
        const std::size_t lo = i * n / max_bars, hi = std::max(lo + 1, (i + 1) * n / max_bars);
        double sum = 0;
        for (std::size_t k = lo; k < hi; ++k) sum += ms[k];
        out[i] = sum / static_cast<double>(hi - lo);
    }
    return out;
}
}  // namespace ef::ui
