// Panel "Darbogaz" karti (spec 2026-11-12 B.3 / D). Uc kisa hukum satiri (1080p / 1440p / 4K) + "Test et"
// (Hit::OpenMeasure: Olcumler sayfasina goturur). Aciklama cumlesi yoktur (A): kapsam / sinir notlari dugmenin UIA yardim
// metninde durur; kapsam ("bu test sahnesinde") basligin yaninda bir kez yazilir.
//  - En yeni denge testi sonucu varsa satirlar D kelimeleriyle: "Islemci darbogazi" (turuncu) / "Darbogaz yok" (yesil) /
//    Belirlenemedi, Olculemedi (mevcut kelime + mevcut neden). Ikinci parca olculen paydir (balance_share); pay
//    hesaplanamiyorsa yazilmaz. Oyun FPS tahmini yapilmaz.
//  - Denge testi yoksa: son testin calisma kanitli islemci / ekran karti hukmu (mevcut kelimeler). Kesin neden
//    uretilmez; hic test yoksa tek kisa satir ("Henuz test yok").
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/components.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace dg = core::diagnosis;

namespace {
std::wstring put1(std::wstring s, std::wstring_view key, const std::wstring& val) {
    if (const auto pos = s.find(key); pos != std::wstring::npos) s.replace(pos, key.size(), val);
    return s;
}
}  // namespace

void Screen::limit_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq, float avail_h) {
    const auto& p = pal();
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    constexpr float kUnbounded = 10000.f;
    constexpr float kNoteMinW = 64.f;  // olculen pay bundan dar kalacaksa yazilmaz (tam metin UIA adinda)
    const auto one_w = [&](Role role, const std::wstring& text) { return std::ceil(g.measure(role, text, kUnbounded, false).w) + 1.f; };

    struct Row {
        std::wstring name, word, note, a11y;
        Color c;
    };
    std::vector<Row> rows;
    std::wstring empty_line, help;  // help: kaldirilan aciklama cumlesi (ekran okuyucuda dugmenin yardim metni)
    const core::measure::BalanceView* bal = vm.measure.balance_latest ? &*vm.measure.balance_latest : nullptr;
    if (bal) {
        help = tk("balance.card.scope_note");
        const auto at_least = t(StringId::PanelBottleneckAtLeast);
        for (std::size_t i = 0; i < bal->rows.size(); ++i) {
            const auto& br = bal->rows[i];
            Row row{tk(core::measure::balance_row_key(i)), {}, {}, {}, p.text_secondary};
            const auto share = balance_share(*bal, i);
            switch (bottleneck_word(br.verdict)) {
                case BottleneckWord::Cpu:
                    row.word = t(StringId::PanelBottleneckCpu);
                    row.c = p.orange;
                    if (share) row.note = put1(t(StringId::PanelBottleneckCpuShare), L"{share}", share_text(*share, tr, true, at_least));
                    break;
                case BottleneckWord::None:
                    row.word = t(StringId::PanelBottleneckNone);
                    row.c = p.success;
                    if (br.verdict == dg::BalanceVerdict::Balanced) row.note = t(StringId::PanelBottleneckBalanced);
                    else if (share) row.note = put1(t(StringId::PanelBottleneckGpuShare), L"{share}", share_text(*share, tr, false, at_least));
                    break;
                case BottleneckWord::Other:
                    row.word = tk(core::measure::balance_verdict_key(br.verdict));
                    if (!br.reason.empty()) row.note = tk(core::measure::balance_reason_key(br.reason));
                    break;
            }
            // UIA: hukum "bu test sahnesinde" kapsamiyla okunur; olculen pay / neden kisaltilmadan.
            row.a11y = a11y_join(put1(put1(tk("balance.card.row_acc"), L"{res}", row.name), L"{verdict}", row.word), row.note);
            rows.push_back(std::move(row));
        }
    } else if (vm.components_loading) {
        empty_line = t(StringId::OverviewLoading);
    } else if (vm.measure.history.empty()) {
        empty_line = t(StringId::PanelBottleneckEmpty);
        help = t(StringId::OverviewLimitNoTest);
    } else {
        // Yalniz calisma kanitli bilesenler: RAM / depolama envanter bulgusu test sonucu gibi sunulmaz (Olcumler'de).
        bool limiting = false;
        for (const auto& f : vm.components) {
            if (f.component != dg::Component::Cpu && f.component != dg::Component::Gpu) continue;
            Row row{t(f.component == dg::Component::Cpu ? StringId::DiagCompCpu : StringId::DiagCompGpu), tk(dg::verdict_key(f)), {}, {}, p.text_secondary};
            if (f.verdict == dg::Verdict::NoIssueFound) row.c = p.success;
            else if (f.verdict == dg::Verdict::Limiting) row.c = p.orange, limiting = true;
            row.a11y = row.name + L": " + row.word;
            rows.push_back(std::move(row));
        }
        if (rows.empty()) empty_line = t(StringId::DiagVerdictAbstain);
        help = t(limiting ? StringId::OverviewLimitNote : StringId::OverviewLimitUnknownDesc);
    }

    // Sahip onayli maket (2026-11-16 birebir; olculer HTML/CSS kaynagindan, Tailwind px = DIP): kart p-3.5 + 1 DIP kenar
    // (15); baslik satiri 28 (items-center): "06 //" (11px/600 mono, camgobegi), baslik 14px/600, kapsam 12px, renk anahtari
    // (ml-2, gap 10; nokta 8 + gap 4 + 11px), sagda .btn-secondary "Test et"; mb 10. Satirlar space-y-2 (8); her satir bir
    // yuva (px-3 py-2 + 1 DIP kenar): solda 80 DIP cozunurluk sutunu (12px/600 mono; kendi ekraninda beyaz + altinda
    // "senin ekranın" 10px/500 camgobegi; maketteki satir kutusu nedeniyle bu satir 60 DIP), gap 12, sagda hukum (12px/600)
    // + olculen pay / neden (12px, saga yasli) ve altinda (mt 6) bolme cubugu (h 6, parcalar arasi 4; sol parca solda,
    // sag parca sagda yuvarlak). Cubuk YALNIZ olculen paydan (balance_share, satirdaki metinle ayni veri): sinirlayan taraf
    // kendi kapasitesinin tamamini (100), diger taraf 100 - pay kullanir; sinirlamayan taraf %35 soluk. Ortadaki 2 x 10
    // centik es bolunme noktasidir (beyaz %70). Pay yoksa (dengede / belirlenemedi) cubuk cizilmez (oran uydurulmaz).
    constexpr float kIn = 15.f, kHeadGap = 10.f, kRowGap = 8.f, kRowPadX = 13.f, kNameCol = 80.f, kColGap = 12.f, kBarH = 6.f, kBarGap = 6.f;
    constexpr float kDot = 8.f;
    const float in_w = std::max(slot.area.w - 2 * kIn, 1.f);
    std::vector<std::optional<float>> split(rows.size());
    std::vector<bool> cpu_limited(rows.size(), false);
    if (bal)
        for (std::size_t i = 0; i < rows.size() && i < bal->rows.size(); ++i)
            if (const auto share = balance_share(*bal, i)) {
                const bool cpu = bal->rows[i].verdict == dg::BalanceVerdict::CpuLimited;
                const float other = 100.f - static_cast<float>(share->pct);
                const float cpu_load = cpu ? 100.f : other, gpu_load = cpu ? other : 100.f;
                if (cpu_load + gpu_load > 0.f) split[i] = cpu_load / (cpu_load + gpu_load), cpu_limited[i] = cpu;
            }
    bool any_bar = std::any_of(split.begin(), split.end(), [](const auto& v) { return v.has_value(); });
    const auto mine = bal ? balance_display_row(vm.display) : std::nullopt;
    const auto your = t(StringId::MeasureBottleneckYourDisplay);

    const auto open_label = t(StringId::PanelTest);
    const RectF btn = panel_button_rect(g, open_label, false);
    const auto title = t(StringId::PanelBottleneckTitle);
    const auto scope = bal ? t(StringId::PanelBottleneckScope) : std::wstring();
    const std::wstring idx = L"06 //";  // teknik dizin (cevrilmez)
    const float idx_w = one_w(Role::Mono11Semi, idx);
    const float head_w = std::max(in_w - btn.w - theme_.space(1), 1.f);
    const float title_w = std::min(one_w(Role::Title14, title), std::max(head_w - idx_w - theme_.space(1), 1.f));
    const float head_h = std::max(theme_.role(Role::Title14).line, btn.h);
    const float line_h = theme_.role(Role::Small12).line;
    const float res_h = theme_.role(Role::Mono12Semi).line, your_h = theme_.role(Role::Medium10).line;
    // Dogal boy Panel'de kalan yukseklige sigmazsa (buyuk metin olcegi / kisa pencere) sikisik satir: daha az dikey
    // bosluk ve bolme cubugu yok (pay metni satirda kalir).
    float kRowPadY = 9.f, h = 0;
    const auto inner_of = [&](bool is_mine) {
        const float right = line_h + (any_bar ? kBarGap + kBarH : 0.f);
        return std::max(right, is_mine ? res_h + 1.6f * your_h : res_h);
    };
    const auto row_h_of = [&](std::size_t i) { return inner_of(mine && *mine == i) + 2 * kRowPadY; };
    for (int pass = 0; pass < 2; ++pass) {
        float rows_h = rows.empty() ? line_h : 0.f;
        for (std::size_t i = 0; i < rows.size(); ++i) rows_h += row_h_of(i) + (i ? kRowGap : 0.f);
        h = kIn + head_h + kHeadGap + rows_h + kIn;
        if (h <= avail_h + 0.5f || rows.empty()) break;
        any_bar = false, kRowPadY = theme_.space(0) + 1.f;
    }
    RectF card;
    if (!slot.place(h, card)) return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + kIn;
    float y = card.y + kIn;
    const bool dark = pal_ == &theme_.dark;
    const auto mid = [&](Role role) { return y + (head_h - theme_.role(role).line) / 2; };
    float hx = x;
    g.text(Role::Mono11Semi, idx, {hx, mid(Role::Mono11Semi), idx_w, theme_.role(Role::Mono11Semi).line}, dark ? p.cyan : p.text_secondary, {Align::Leading, false});
    hx += idx_w + theme_.space(1) - 1.f;
    g.text(Role::Title14, title, {hx, mid(Role::Title14), title_w, theme_.role(Role::Title14).line}, p.text_primary, {Align::Leading, false});
    hx += title_w + theme_.space(1) - 1.f;
    float room = x + head_w - hx;
    if (const float sw = one_w(Role::Small12, scope); !scope.empty() && room >= kNoteMinW) {
        const float w = std::min(sw, room);
        g.text(Role::Small12, fit_text(g, Role::Small12, scope, w), {hx, mid(Role::Small12), w, line_h}, p.text_secondary, {Align::Leading, false});
        hx += w - 1.f + 2 * theme_.space(1);
        room = x + head_w - hx;
    }
    // Renk anahtari (yalniz bolme cubugu varken ve sigarsa; anlam satir metninde de var).
    if (any_bar) {
        const auto cpu_l = t(StringId::LiveCpu), gpu_l = t(StringId::LiveGpu);
        const float cw = one_w(Role::Small11, cpu_l), gw = one_w(Role::Small11, gpu_l);
        constexpr float kItemGap = 4.f, kLegendGap = 10.f;
        const float need = 2 * (kDot + kItemGap) + cw + gw + kLegendGap;
        if (need <= room) {
            const float dy = y + (head_h - kDot) / 2;
            const float ty = mid(Role::Small11), th = theme_.role(Role::Small11).line;
            g.fill_round({hx, dy, kDot, kDot}, kDot / 2, p.data_cpu);
            g.text(Role::Small11, cpu_l, {hx + kDot + kItemGap, ty, cw, th}, p.text_secondary, {Align::Leading, false});
            const float gx = hx + kDot + kItemGap + cw - 1.f + kLegendGap;
            g.fill_round({gx, dy, kDot, kDot}, kDot / 2, p.data_gpu);
            g.text(Role::Small11, gpu_l, {gx + kDot + kItemGap, ty, gw, th}, p.text_secondary, {Align::Leading, false});
        }
    }
    panel_button(g, card.right() - kIn - btn.w, y + (head_h - btn.h) / 2, Hit::OpenMeasure, open_label, false, s, now, hits);
    hits.meta[Hit::OpenMeasure].help = a11y_join(t(StringId::OverviewOpenTests), help);
    y += head_h + kHeadGap;
    if (rows.empty()) {
        g.text(Role::Small12, empty_line, {x, y, in_w, line_h}, p.text_secondary, {Align::Leading, false, true});
        hits.add_text(seq.take(), {x, y, in_w, line_h}, a11y_join(title, empty_line));
        return;
    }
    const float name_w = std::min(kNameCol, in_w * 0.3f);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const bool is_mine = mine && *mine == i;
        const float row_h = row_h_of(i), inner = row_h - 2 * kRowPadY;
        const RectF well{x, y, in_w, row_h};
        // Kendi ekraninin satiri: koyu temada camgobegi %50 cizgi + %4 zemin; diger temalarda "senin ekranın" sozcugu
        // yeter (odak halkasiyla karismasin).
        draw_well(g, well, is_mine && dark);
        if (is_mine && dark) g.fill_round(well.inset(1.f), depth::kWellRadius - 1.f, p.cyan.with_alpha(0.04f));
        const float rx = x + kRowPadX, ry = y + kRowPadY, rw = std::max(in_w - 2 * kRowPadX, 1.f);
        const Color res_c = is_mine ? p.text_primary : (dark ? depth::kSlate300 : p.text_primary);
        if (is_mine) {
            g.text(Role::Mono12Semi, fit_text(g, Role::Mono12Semi, r.name, name_w), {rx, ry - 1.f, name_w, res_h}, res_c, {Align::Leading, false});
            g.text(Role::Medium10, fit_text(g, Role::Medium10, your, name_w), {rx, ry + inner - 1.6f * your_h + 6.2f, name_w, your_h},
                   dark ? p.cyan : p.text_secondary, {Align::Leading, false});
        } else {
            g.text(Role::Mono12Semi, fit_text(g, Role::Mono12Semi, r.name, name_w), {rx, ry + (inner - res_h) / 2 + 2.3f, name_w, res_h}, res_c,
                   {Align::Leading, false});
        }
        const float cx0 = rx + name_w + kColGap, cw = std::max(rx + rw - cx0, 1.f);
        const float right_h = line_h + (any_bar && split[i] ? kBarGap + kBarH : 0.f);
        const float top_y = ry + (inner - right_h) / 2;
        const float word_w = std::min(one_w(Role::Semi12, r.word), cw);
        g.text(Role::Semi12, fit_text(g, Role::Semi12, r.word, word_w), {cx0, top_y, word_w, line_h}, r.c, {Align::Leading, false});
        if (const float nw = cw - word_w - theme_.space(2); !r.note.empty() && nw >= kNoteMinW)
            g.text(Role::Small12, fit_text(g, Role::Small12, r.note, nw), {cx0 + cw - nw, top_y, nw, line_h}, p.text_secondary, {Align::Trailing, false});
        if (any_bar && split[i]) {
            const float by = top_y + line_h + kBarGap;
            constexpr float kPartGap = 4.f, kDim = 0.35f, kR = kBarH / 2;
            const float cpu_w = std::max((cw - kPartGap) * *split[i], 0.f), gpu_w = std::max(cw - kPartGap - cpu_w, 0.f);
            if (cpu_w > 0.5f) {
                const Color c = p.data_cpu.with_alpha(cpu_limited[i] ? 1.f : kDim);
                const RectF part{cx0, by, cpu_w, kBarH};
                // rounded-l-full: sol uc yari daire (kirpilmis), gerisi duz (yari saydam parcada ust uste binme yok).
                g.push_clip({part.x, part.y, std::min(part.w, kR), part.h});
                g.fill_round({part.x, part.y, kR * 2, part.h}, kR, c);
                g.pop_clip();
                if (part.w > kR) g.fill({part.x + kR, part.y, part.w - kR, part.h}, c);
            }
            if (gpu_w > 0.5f) {
                const Color c = p.data_gpu.with_alpha(cpu_limited[i] ? kDim : 1.f);
                const RectF part{cx0 + cpu_w + kPartGap, by, gpu_w, kBarH};
                // rounded-r-full
                g.push_clip({std::max(part.x, part.right() - kR), part.y, std::min(part.w, kR), part.h});
                g.fill_round({part.right() - kR * 2, part.y, kR * 2, part.h}, kR, c);
                g.pop_clip();
                if (part.w > kR) g.fill({part.x, part.y, part.w - kR, part.h}, c);
            }
            g.fill({std::floor(cx0 + cw / 2) - 1.f, by - 2.f, 2.f, kBarH + 4.f}, p.text_primary.with_alpha(0.7f));
        }
        hits.add_text(seq.take(), well, is_mine ? a11y_join(r.a11y, your) : r.a11y);
        y += row_h + kRowGap;
    }
}

}  // namespace ef::ui
