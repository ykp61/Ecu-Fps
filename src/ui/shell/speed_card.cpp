// Task 12 (spec §7) + spec 2026-11-12 C: Olcumler ust satirindaki "Internet hizi" karti. Kadran buyuk ve kartin
// ust-ortasinda (180 derece; olcek 0..1000 Mbps parcali dogrusal; yay camgobegi -> mor renk gecisli, HC'de tek renk; ibre;
// yayin icinde buyuk sayi: indirme camgobegi, yukleme mor); altinda tek satir "↓ indirme / ↑ yukleme / gecikme"; en altta
// "Hizi test et" / "Yeniden test et" / "Testi durdur". Sayilar yalniz olculmus degerdir: calisirken o anki parcanin gercek
// hizi, bitince sonuc; iptal/hata sonuc sayisi gostermez ("—"). Harcanan veri, sunucu notu ve asama durumu UIA'dadir.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "core/measure/view.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cn = core::net;

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

// Kadran olcegi (core::net::gauge_fraction ile ayni noktalar); etiketler sayidir, ceviri gerektirmez.


constexpr float kArcStart = 180.f, kArcSweep = 180.f;  // sol uctan saat yonunde ust yarim daire

int phase_index(cn::SpeedPhase p) {
    switch (p) {
        case cn::SpeedPhase::Latency: return 0;
        case cn::SpeedPhase::Download: return 1;
        case cn::SpeedPhase::Upload: return 2;
        case cn::SpeedPhase::Done: return 3;
        default: return -1;
    }
}

// Kadranda gosterilecek gercek deger: calisirken anlik parca hizi, bitince indirme sonucu; aksi halde yok.
std::optional<double> shown_mbps(const ViewModel::SpeedView& v) {
    if (v.running) {
        if (v.phase == cn::SpeedPhase::Download || v.phase == cn::SpeedPhase::Upload)
            if (std::isfinite(v.live_mbps) && v.live_mbps > 0) return v.live_mbps;
        return std::nullopt;
    }
    if (v.phase == cn::SpeedPhase::Done) return v.down;
    return std::nullopt;
}

}  // namespace

std::wstring Screen::speed_state_text(const ViewModel::SpeedView& v) const {
    if (v.running) {
        switch (v.phase) {
            case cn::SpeedPhase::Download: return tk("speed.state.down");
            case cn::SpeedPhase::Upload: return tk("speed.state.up");
            default: return tk("speed.state.latency");
        }
    }
    if (v.fail != cn::SpeedFail::None) return tk(cn::speed_fail_key(v.fail));
    if (v.phase == cn::SpeedPhase::Done) return tk("speed.state.done");
    if (v.phase == cn::SpeedPhase::Cancelled) return tk("speed.state.cancelled");
    return tk("speed.state.ready");
}

std::wstring Screen::speed_announcement_text(const ViewModel::SpeedView& v) const {
    std::wstring a = a11y_join(tk("speed.title"), speed_state_text(v));
    if (!v.running && v.phase == cn::SpeedPhase::Done) {
        const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
        auto mbps = [&](const std::optional<double>& x) {
            return x ? replace_all(tk("speed.mbps"), L"{v}", widen(core::measure::format_number(*x, 1, tr))) : std::wstring(L"—");
        };
        a = a11y_join(std::move(a), tk("speed.down") + L": " + mbps(v.down));
        a = a11y_join(std::move(a), tk("speed.up") + L": " + mbps(v.up));
    }
    return a;
}

void Screen::speed_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    // BOOST maketi (2026-11-16 birebir, M3): card-v2 p-3.5; baslik ("M3 //" mor + 14px/600) ve sagda sunucu; ortada 112x64
    // yarim daire kadran (cyan yay, ibre yok) solda, sagda uc satir (↓ indirme cyan / ↑ yukleme mor / gecikme); altta cizgiyle
    // ayrilmis satirda sagda ikincil dugme. Sayilar yalniz olculmus degerdir (calisirken anlik parca hizi, bitince sonuc;
    // iptal/hata "—"). Harcanan veri, sunucu notu ve asama durumu UIA'dadir.
    const auto& p = pal();
    const auto& v = vm.speed;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const bool dark = pal_ == &theme_.dark;
    constexpr float kIn = 15.f;  // p-3.5 (14) + 1 DIP cizgi
    const float pad = kIn;
    const float g1 = theme_.space(1), g2 = theme_.space(2);
    const float inner_w = std::max(slot.area.w - 2 * pad, 1.f);
    constexpr float kUnbounded = 10000.f;
    // Tek aktif test: olcum veya uzun test surerken hiz testi baslatilamaz (dugme devre disi, Tab almaz).
    const bool other_test = core::measure::busy(vm.measure) || vm.measure.endurance.running;
    const bool can_start = !v.running && !vm.game_gate_blocked && !other_test;

    auto fmt = [&](double x, int decimals) { return widen(core::measure::format_number(x, decimals, tr)); };
    auto mbps_text = [&](double x) { return replace_all(tk("speed.mbps"), L"{v}", fmt(x, x >= 100 ? 0 : 1)); };

    const auto value = shown_mbps(v);
    const bool upload = v.running && v.phase == cn::SpeedPhase::Upload;
    const Color down_c = dark ? p.data_net_down : ensure_contrast(p.data_net_down, p.surface, p.text_primary, 4.5);
    const Color up_c = dark ? p.violet : ensure_contrast(p.data_net_up, p.surface, p.text_primary, 4.5);
    const std::wstring state = speed_state_text(v);
    const bool state_warn = !v.running && v.fail != cn::SpeedFail::None && v.fail != cn::SpeedFail::Gated;
    const bool done = !v.running && v.phase == cn::SpeedPhase::Done;
    // Kadran ortasi: sayi + altinda "Mbps"; deger yoksa "—".
    std::wstring unit = replace_all(tk("speed.mbps"), L"{v}", L"");
    while (!unit.empty() && unit.front() == L' ') unit.erase(unit.begin());
    const std::wstring big = value ? fmt(*value, *value >= 100 ? 0 : 1) : std::wstring(L"—");
    const std::wstring big_acc = value ? mbps_text(*value) : std::wstring(L"—");

    // Sonuc satirlari: sayi yalniz test bitince. Harcanan veri, MB/s karsiliklari UIA metnindedir.
    struct Row {
        std::wstring label, text, full;
        Color lc, vc;
    };
    const auto mbps_or_dash = [&](const std::optional<double>& x) { return done && x ? mbps_text(*x) : std::wstring(L"—"); };
    const auto with_mbs = [&](const std::optional<double>& x) {
        return done && x ? mbps_text(*x) + L" (" + replace_all(tk("speed.mbs"), L"{v}", fmt(cn::mbps_to_mbs(*x), 1)) + L")" : std::wstring(L"—");
    };
    const std::wstring lat = done && v.latency ? replace_all(tk("speed.ms"), L"{v}", fmt(*v.latency, 0)) : std::wstring(L"—");
    const std::array<Row, 3> rows{{
        {L"↓ " + tk("speed.down"), mbps_or_dash(v.down), tk("speed.down") + L": " + with_mbs(v.down), down_c, done && v.down ? p.text_primary : p.text_secondary},
        {L"↑ " + tk("speed.up"), mbps_or_dash(v.up), tk("speed.up") + L": " + with_mbs(v.up), up_c, done && v.up ? p.text_primary : p.text_secondary},
        {tk("speed.latency"), lat, tk("speed.latency") + L": " + lat, p.text_secondary, done && v.latency ? p.text_primary : p.text_secondary},
    }};

    // Baslik: sagda sunucu adi (calisirken / hata / iptalde durum metni). Alt satir: cizgi + dugme.
    const float head_h = theme_.role(Role::Title14).line;
    const std::wstring right = (v.running || v.fail != cn::SpeedFail::None || v.phase == cn::SpeedPhase::Cancelled) ? state : std::wstring(L"Cloudflare");
    const std::wstring title = tk("speed.title");
    const std::wstring btn_label = v.running ? tk("speed.stop") : tk(v.phase == cn::SpeedPhase::Idle ? "speed.test_start" : "speed.again");
    const RectF btn = panel_button_rect(g, btn_label, false);
    const std::wstring blocked = (!v.running && vm.game_gate_blocked) ? tk("speed.blocked_game") : std::wstring();
    const float blocked_h = blocked.empty() ? 0.f : g.measure(Role::Small12, blocked, inner_w).h;
    // Sahip duzeni: dar kart, kadran yok. Baslik; altinda TEK cubuk (canli / son deger); altinda tek satirda indirme, yukleme,
    // gecikme ve sagda dugme. Sayilar yalniz olculmus degerdir (calisirken anlik parca hizi, bitince sonuc; yoksa "—").
    constexpr float kBarH = 8.f;
    const float row_line = theme_.role(Role::Small12).line;
    const float bar_row_h = std::max(kBarH, theme_.role(Role::Semi12).line);
    const float vals_h = std::max(btn.h, row_line);
    constexpr float kGap = 10.f;
    const float blocked_blk = blocked_h > 0 ? blocked_h + g1 : 0.f;
    // R-C1: yuk altinda gecikme satiri (yalniz sonuc varken; bos hat gecikmesi + en az bir yonde yeterli ornek gerekir).
    std::wstring bloat;
    if (done) {
        const auto grade = cn::bloat_grade(v.latency, v.down_loaded, v.up_loaded);
        if (grade != cn::BloatGrade::Insufficient) {
            const auto ms = [&](const std::optional<double>& x) { return x ? replace_all(tk("speed.ms"), L"{v}", fmt(*x, 0)) : std::wstring(L"—"); };
            bloat = replace_all(replace_all(tk("speed.bloat.line"), L"{d}", ms(v.down_loaded)), L"{u}", ms(v.up_loaded)) + L" · " +
                    tk(std::string(cn::bloat_key(grade)));
        }
    }
    const float bloat_h = bloat.empty() ? 0.f : g.measure(Role::Small12, bloat, inner_w).h;
    const float bloat_blk = bloat_h > 0 ? bloat_h + g1 : 0.f;
    // R-C4: oyun sunucusu ping satiri (her zaman; sol metin = olculen deger / durum, sagda "Sunucu ping" dugmesi).
    // Sayilar yalniz olculmus degerdir; yanit yoksa "veri yetersiz" (ICMP kapali olabilir), uydurma deger yok.
    const bool srv_can = !v.running && !v.server_running && !vm.game_gate_blocked && !other_test;
    const std::wstring srv_btn_label = tk("speed.server.btn");
    const RectF srv_btn = panel_button_rect(g, srv_btn_label, false);
    bool srv_warn = false;
    const std::wstring srv_text = [&] {
        if (v.server_ip.empty()) return tk("speed.server.none");
        const std::wstring ip = widen(v.server_ip);
        const auto with_ip = [&](const std::wstring& k) { return replace_all(k, L"{ip}", ip); };
        switch (v.server_state) {
            case cn::ServerPingState::Running: return with_ip(tk("speed.server.running"));
            case cn::ServerPingState::Done: {
                const double loss = v.server_sent > 0 ? 100.0 * (v.server_sent - v.server_received) / v.server_sent : 0.0;
                const auto ms = [&](double x) { return replace_all(tk("speed.ms"), L"{v}", fmt(x, 0)); };
                return replace_all(replace_all(replace_all(with_ip(tk("speed.server.result")), L"{avg}", ms(v.server_avg_ms)), L"{jit}", ms(v.server_jitter_ms)),
                                   L"{loss}", fmt(loss, 0));
            }
            case cn::ServerPingState::Insufficient:
                srv_warn = true;
                return replace_all(replace_all(with_ip(tk("speed.server.insufficient")), L"{recv}", std::to_wstring(v.server_received)), L"{sent}",
                                   std::to_wstring(v.server_sent));
            case cn::ServerPingState::Failed: srv_warn = true; return with_ip(tk("speed.server.failed"));
            case cn::ServerPingState::Cancelled: return with_ip(tk("speed.server.cancelled"));
            case cn::ServerPingState::None: break;
        }
        return with_ip(tk("speed.server.saved"));
    }();
    const float srv_tw = std::max(inner_w - srv_btn.w - g2, 1.f);
    const float srv_h = std::max(srv_btn.h, g.measure(Role::Small12, srv_text, srv_tw).h);
    const float srv_blk = g2 + srv_h;
    RectF card;
    if (!slot.place(pad + head_h + kGap + bar_row_h + kGap + blocked_blk + vals_h + bloat_blk + srv_blk + pad, card)) return;  // olcum gecisi
    s.speed_running = v.running;
    s.speed_can_start = can_start;
    s.server_ping_can_start = srv_can;
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + pad;
    const float y = card.y + pad;
    {  // Baslik satiri
        const std::wstring code = L"M3 //";
        const float cw = std::ceil(g.measure(Role::Mono10, code, kUnbounded, false).w) + 1.f;
        g.text(Role::Mono10, code, {x, y, cw, head_h}, dark ? p.violet : p.text_primary, {Align::Leading, false, true});
        const float rw = std::min(std::ceil(g.measure(Role::Small11, right, kUnbounded, false).w) + 1.f, inner_w * 0.5f);
        const float tw = std::max(inner_w - cw - g1 - rw - g1, 1.f);
        g.text(Role::Title14, fit_text(g, Role::Title14, title, tw), {x + cw + g1, y, tw, head_h}, p.text_primary, {Align::Leading, false, true});
        g.text(Role::Small11, fit_text(g, Role::Small11, right, rw), {x + inner_w - rw, y, rw, head_h}, state_warn && right == state ? p.warning : p.text_secondary,
               {Align::Trailing, false, true});
    }
    float cy = y + head_h + kGap;
    {  // Tek cubuk: iz + dolgu (0..1000 Mbps parcali dogrusal olcek) + sagda buyuk deger
        const std::wstring num = value ? mbps_text(*value) : std::wstring(L"—");
        TextOpts bo{Align::Trailing, false, true};
        bo.tabular = true;
        const float nw = std::min(std::ceil(g.measure(Role::Semi12, num, kUnbounded, false, true).w) + 1.f, inner_w * 0.4f);
        const float bw = std::max(inner_w - nw - g2, 1.f);
        const RectF track{x, cy + (bar_row_h - kBarH) / 2, bw, kBarH};
        g.fill_round(track, kBarH / 2, dark ? Color{1.f, 1.f, 1.f, 0.08f} : p.border);
        const float target = value ? static_cast<float>(cn::gauge_fraction(*value)) : 0.f;
        // Azaltilmis harekette dolgu aninda yerine gecer; yoksa canli deger gecisi kadar yumusar.
        const int dur = effective_ms(theme_.motion.live_value, s.reduced_motion);
        if (std::fabs(target - s.speed_needle.target()) > 1e-3f) s.speed_needle.retarget(target, now, dur);
        const float frac = std::clamp(s.speed_needle.value(now, theme_.easing), 0.f, 1.f);
        if (frac > 0.002f) g.fill_round({track.x, track.y, std::max(kBarH, bw * frac), kBarH}, kBarH / 2, upload ? up_c : dark ? p.cyan : p.accent);
        g.text(Role::Semi12, num, {x + inner_w - nw, cy, nw, bar_row_h}, value ? p.text_primary : p.text_secondary, bo);
        hits.add_text(seq.take(), {x, cy, inner_w, bar_row_h}, a11y_join(a11y_join(tk("speed.gauge"), big_acc), state));
    }
    cy += bar_row_h + kGap;
    if (blocked_h > 0) {
        g.text(Role::Small12, blocked, {x, cy, inner_w, blocked_h}, p.text_secondary, {Align::Leading, true});
        cy += blocked_h + g1;
    }
    {  // Tek satir: indirme / yukleme / gecikme (etiket + deger) ve sagda dugme
        const float bx = x + inner_w - btn.w;
        const float avail = std::max(bx - g2 - x, 1.f);
        const float cw3 = std::floor(avail / 3.f);
        std::wstring summary = tk("speed.result");
        TextOpts vo{Align::Leading, false, true};
        vo.tabular = true;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            const float ix = x + static_cast<float>(i) * cw3;
            const float lw = std::min(std::ceil(g.measure(Role::Small12, r.label, kUnbounded, false).w) + 1.f, cw3 * 0.5f);
            g.text(Role::Small12, fit_text(g, Role::Small12, r.label, lw), {ix, cy + (vals_h - row_line) / 2, lw, row_line}, r.lc, {Align::Leading, false, true});
            const float vw = std::max(cw3 - lw - g1, 1.f);
            g.text(Role::Small12, fit_text(g, Role::Small12, r.text, vw), {ix + lw + g1, cy + (vals_h - row_line) / 2, vw, row_line}, r.vc, vo);
            summary = a11y_join(std::move(summary), r.full);
        }
        if (v.bytes > 0) summary = a11y_join(std::move(summary), tk("speed.data_used") + L": " + replace_all(tk("speed.mb"), L"{v}", fmt(static_cast<double>(v.bytes) / 1e6, 1)));
        // Asama durumu (eski asama cipleri): tamamlanan / suren asamalar.
        const int cur = v.running ? phase_index(v.phase) : (v.phase == cn::SpeedPhase::Done ? 3 : -1);
        const std::array<const char*, 3> names{"speed.phase.latency", "speed.phase.down", "speed.phase.up"};
        for (std::size_t i = 0; i < names.size(); ++i)
            if (cur >= static_cast<int>(i)) summary = a11y_join(std::move(summary), tk(names[i]));
        if (!bloat.empty()) summary = a11y_join(a11y_join(std::move(summary), bloat), tk("speed.bloat.note"));
        hits.add_text(seq.take(), {x, cy, avail, vals_h}, a11y_join(a11y_join(std::move(summary), tk("speed.server")), tk("speed.note")));
        const float byy = cy + (vals_h - btn.h) / 2;
        if (v.running) panel_button(g, bx, byy, Hit::SpeedStop, btn_label, false, s, now, hits);
        else panel_button(g, bx, byy, Hit::SpeedStart, btn_label, false, s, now, hits, can_start);
        if (!blocked.empty())
            if (const auto it = hits.meta.find(Hit::SpeedStart); it != hits.meta.end()) it->second.help = blocked;
        if (bloat_h > 0) g.text(Role::Small12, bloat, {x, cy + vals_h + g1, inner_w, bloat_h}, p.text_secondary, {Align::Leading, true});
    }
    {  // R-C4: oyun sunucusu ping satiri
        const float sy = cy + vals_h + bloat_blk + g2;
        g.text(Role::Small12, srv_text, {x, sy, srv_tw, srv_h}, srv_warn ? p.warning : p.text_secondary, {Align::Leading, true});
        hits.add_text(seq.take(), {x, sy, srv_tw, srv_h}, a11y_join(srv_text, tk("speed.server.note")));
        panel_button(g, x + inner_w - srv_btn.w, sy + (srv_h - srv_btn.h) / 2, Hit::SpeedServerPing, srv_btn_label, false, s, now, hits, srv_can);
        if (!srv_can && vm.game_gate_blocked)
            if (const auto it = hits.meta.find(Hit::SpeedServerPing); it != hits.meta.end()) it->second.help = tk("speed.blocked_game");
    }
}

}  // namespace ef::ui
