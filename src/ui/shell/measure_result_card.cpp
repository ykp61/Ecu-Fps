// P9-B5: Olcumler sayfasi — Sonuc karti (M2), "bu oyunda ne sinirladi" ve takilma raporu (DIAG-08). measure_page.cpp'den ayrildi.
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

// Ust satir 2/3: Sonuc karti (B15, sahip onayli maket). Ust satir: "Sonuc" solda; tarih · test turu · sure sagda
// (sigmazsa basligin altinda). Buyuk ortalama FPS (Hero rolu, camgobegi data_gpu) + "ort. FPS"; fark cipi ("▲ %3,2
// onceki teste gore"; artis basari, dusus hata tonu) YALNIZ ayni kosullu onceki test ortak politikayla karsilastirilabilirse.
// Yukseltilmis yuzeyde uc kutu: %1 dusuk FPS, ortalama kare ms, kare p99 ms (kayitta yoksa "—"). Kare suresi grafigi
// YALNIZ kayitta saniye basina gercek FPS serisi varsa (yoksa hic cizilmez; uydurulmaz). Uygulama Testi sonucunda
// Islemci / Ekran karti kutulari bilesen raporunun gercek durumunu yazar (yayin kapisi kapaliyken "Olculdu · karar yok").
// Sayilar sayac gibi dolar (motion.result_count; hareket kapali, oyun acik ya da olcum surerken aninda). Test surerken
// canli sayilar (yalniz gercekten olculduyse; yoksa "—"). Kayit yoksa tek satir "Henuz test yok" + ipucu.
void Screen::measure_result_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    UiState& s = mc.s;
    const ViewModel& vm = mc.vm;
    const std::int64_t now = mc.now;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float sl = mc.sl;
    const float bl = mc.bl;
    const float g0 = mc.g0;
    const float g1 = mc.g1;
    const float g2 = mc.g2;
    const bool tr = mc.tr;
    const bool busy = mc.busy;
    const bool dark = mc.dark;
    const bool balance_scope = mc.balance_scope;
    const Color divider = mc.divider;
    const float inner_w = std::max(slot.area.w - 2 * pad, 1.f);
    const bool has = m.result.has_value() && !busy;
    const cm::ResultView* r = has ? &*m.result : nullptr;
    const cm::BalanceView* bal = r && r->balance ? &*r->balance : nullptr;
    // 0.2.1 geri bildirimi: sonuc varken kart hic tumuyle "—" olmaz (denge sonucu hukum + olculen FPS cifti gosterir).
    const ResultCardMode mode = result_card_mode(busy, r != nullptr, bal != nullptr);
    std::wstring head = t(has && result_is_previous(m) ? StringId::MeasureResultPrevious : StringId::MeasureResult);
    const std::wstring when = busy ? t(StringId::MeasureResultMeasuring) : r ? date_text(r->when_unix_ms, tr) : std::wstring();
    struct Num {
        std::wstring label, value, reason;
        std::optional<double> v;
        int dec = 1;
        Color c;
    };
    // 0: ortalama FPS (buyuk), 1: %1 dusuk FPS, 2: ortalama kare ms, 3: kare p99 ms (birim etiketin icinde).
    std::array<Num, 4> nums;
    nums[0].label = t(StringId::MeasureResultAvgLabel);
    nums[1].label = t(StringId::MeasureResultLow1);
    nums[2].label = t(StringId::MeasureResultFrameAvg);
    nums[3].label = t(StringId::MeasureResultFrameP99);
    const auto take = [&](Num& n, const contracts::Measurement* ms, StringId full) {
        if (!ms) return;
        if (ms->has_value()) n.v = *ms->value();
        else if (const auto why = tk(cm::missing_key(ms->missing_reason())); !why.empty())
            n.reason = t(full) + L": " + t(StringId::MetricMissing) + L" — " + why;
    };
    if (busy) {
        // Canli: yalniz kapanmis olcum kovasi varsa (has_numbers) kare hizi ve kare suresi; digerleri "—".
        if (s.live_test.has_numbers && std::isfinite(s.live_test.fps) && s.live_test.fps > 0) nums[0].v = s.live_test.fps;
        if (const auto fm = cm::live_frame_ms(s.live_test); fm && s.live_test.has_numbers) nums[2].v = *fm;
    } else if (r && !bal) {
        take(nums[0], &r->avg_fps, StringId::MeasureMetricAvg);
        take(nums[1], &r->low1_fps, StringId::MeasureMetricLow1);
        take(nums[3], &r->p99_ms, StringId::MeasureMetricP99);
        // Ortalama kare suresi = 1000 / ortalama FPS (ortalama FPS kare sayisi / sure oldugundan ayni olcum).
        if (nums[0].v && *nums[0].v > 0) nums[2].v = 1000.0 / *nums[0].v;
    }
    nums[0].c = ensure_contrast(p.data_gpu, p.surface, p.text_primary, 4.5);
    for (std::size_t i = 1; i < nums.size(); ++i) nums[i].c = p.text_primary;
    const int count_ms = effective_ms(theme_.motion.result_count, s.reduced_motion || s.motion_paused || vm.game_gate_blocked || busy);
    std::array<std::wstring, 4> real;  // UIA ve yerlesim: gercek deger (sayac ara degeri okunmaz)
    for (std::size_t i = 0; i < nums.size(); ++i) {
        Num& n = nums[i];
        if (!n.v) {
            n.value = real[i] = L"—";
            n.c = p.text_secondary;
            s.measure_num_seen[i] = false;
            continue;
        }
        real[i] = widen(cm::format_number(*n.v, n.dec, tr));
        auto& tw = s.measure_num[i];
        const float tf = static_cast<float>(*n.v);
        if (!s.measure_num_seen[i]) {
            tw = Tween(count_ms > 0 ? 0.f : tf);
            tw.retarget(tf, now, count_ms);
            s.measure_num_seen[i] = true;
        } else if (std::fabs(tf - tw.target()) > 1e-3f) {
            tw.retarget(tf, now, count_ms);
        }
        n.value = tw.active(now) ? widen(cm::format_number(static_cast<double>(tw.value(now, theme_.easing)), n.dec, tr)) : real[i];
    }
    // Test turu (kaynak · kapsam · sure) ya da bos durum satiri.
    std::wstring type;
    const auto src_name = [&](cm::TestSource src) {
        return t(src == cm::TestSource::Builtin ? StringId::TestBuiltin : src == cm::TestSource::InGame ? StringId::TestGame : StringId::TestEndurance);
    };
    if (busy) {
        type = src_name(m.source);
        if (m.source == cm::TestSource::Builtin)
            type += (balance_scope ? L" · " + t(StringId::MeasurePillBottleneck) : std::wstring()) + L" · " +
                    t(balance_scope ? (m.mode == cm::TestMode::Full ? StringId::BenchModeBalanceFull : StringId::BenchModeBalanceQuick)
                                    : (m.mode == cm::TestMode::Full ? StringId::BenchModeFull : StringId::BenchModeQuick));
    } else if (r) {
        if (r->source == cm::TestSource::Endurance) type = tk("test.endurance");
        else {
            type = src_name(r->source);
            if (bal) type += L" · " + t(StringId::MeasurePillBottleneck) + L" · " + t(r->mode == cm::TestMode::Full ? StringId::BenchModeBalanceFull : StringId::BenchModeBalanceQuick);
            else if (r->source == cm::TestSource::Builtin) type += L" · " + t(r->mode == cm::TestMode::Full ? StringId::BenchModeFull : StringId::BenchModeQuick);
            else if (const auto ttl = result_title(*r); !ttl.empty()) type = ttl + L" · " + type;
        }
    } else {
        type = t(StringId::MeasureResultNone);
    }
    // Ust satir: baslik solda, kucuk ust bilgi (tarih · tur · sure) sagda; sigmazsa basligin altinda (sarili).
    std::wstring meta;
    if (mode != ResultCardMode::Empty) meta = when.empty() ? type : type.empty() ? when : when + L" · " + type;
    const float head_w = std::ceil(g.measure(Role::Mono10, L"M2 //", kUnbounded, false).w) + 1.f + g1 + std::ceil(g.measure(Role::Title14, head, kUnbounded, false).w) + 1.f;
    const float meta_w = meta.empty() ? 0.f : std::ceil(g.measure(Role::Small11, meta, kUnbounded, false, true).w) + 1.f;
    const bool meta_inline = meta.empty() || head_w + g2 + meta_w <= inner_w;
    const float meta_h = meta_inline ? 0.f : g.measure(Role::Small11, meta, inner_w).h;
    const float header_h = theme_.role(Role::Title14).line + (meta_h > 0 ? g0 + meta_h : 0.f);

    // --- Sayilar (Numbers; BOOST maketi M2): uc esit sutun (ORT. FPS 24px camgobegi | %1 DUSUK | P99), kare suresi cubuklari
    // (yalniz kayittaki gercek saniye serisinden), bilesen kutulari ve altta cizgili serit (onceki testle fark; en iyi / en
    // kotu kare suresi). Maketteki hedef FPS ve %0,1 satiri yok (hedef FPS sahip karariyla kalkti; %0,1 olculmuyor). ---
    const bool numbers = mode == ResultCardMode::Numbers;
    // Fark: ayni kaynak + kosul izli onceki kayit ortak politikayla karsilastirilabilirse ve iki tarafta da ortalama FPS
    // olculduyse (ComparisonView.delta_percent). Tek kosu: fark tanimlayicidir, nedensellik iddiasi yok.
    std::wstring delta_txt, delta_acc;
    Color delta_fg = p.text_secondary;
    if (numbers && r && r->source != cm::TestSource::Endurance && m.comparison.available && m.comparison.comparable &&
        m.comparison.delta_percent && std::isfinite(*m.comparison.delta_percent)) {
        const double d = *m.comparison.delta_percent;
        const double shown = std::round(std::fabs(d) * 10.0) / 10.0;  // gosterilen bir ondalik; %0,0 ise yon yok
        const auto num = widen(cm::format_number(shown, 1, tr));
        const auto pct = tr ? L"%" + num : num + L"%";
        const bool flat = shown == 0.0;
        delta_txt = replace_all(t(flat ? StringId::MeasureResultDeltaFlat : d > 0 ? StringId::MeasureResultDeltaUp : StringId::MeasureResultDeltaDown),
                                L"{pct}", pct);
        delta_acc = replace_all(t(flat ? StringId::MeasureResultDeltaFlatAcc : d > 0 ? StringId::MeasureResultDeltaUpAcc : StringId::MeasureResultDeltaDownAcc),
                                L"{pct}", pct);
        const Color tone = flat ? p.text_secondary : d > 0 ? p.success : p.error;
        delta_fg = ensure_contrast(tone, p.surface, p.text_primary, 4.5);
    }
    constexpr float kBarsH = 40.f, kBarsGap = 4.f, kBarsMax = 16.f;  // maket: h-10, gap-1, 16 cubuk
    const float stat_gap = g2;
    const float stat_w = std::max(std::floor((inner_w - 2 * stat_gap) / 3), 1.f);
    const float stat_lbl_h = theme_.role(Role::Small11).line;
    const float stat_val_h = theme_.role(Role::Num24).line;
    const float stats_h = stat_lbl_h + stat_val_h;
    const std::array<std::wstring, 3> stat_lbl{t(StringId::MeasureShortAvg), t(StringId::MeasureShortLow1), t(StringId::MeasureShortP99) + L" (ms)"};
    const std::array<std::size_t, 3> stat_idx{0, 1, 3};
    // Kare suresi cubuklari: yalniz kayittaki saniye basina gercek FPS serisinden (en az iki nokta). Yoksa hic yok.
    std::vector<double> ft;
    if (numbers && r && !bal) ft = frame_time_bars(r->fps_series, static_cast<std::size_t>(kBarsMax));
    const bool chart = ft.size() >= 2;
    double ft_lo = 0, ft_hi = 0;
    std::wstring chart_cap, chart_range;
    if (chart) {
        // Cubuk olcegi ve "en iyi / en kotu" metni: kayittaki TUM saniye serisinin gercek ucu (ortalanmis kova en kotu
        // saniyeyi gizlemez).
        ft_lo = *std::min_element(ft.begin(), ft.end());
        ft_hi = *std::max_element(ft.begin(), ft.end());
        for (const double f : r->fps_series)
            if (std::isfinite(f) && f > 0) {
                ft_lo = std::min(ft_lo, 1000.0 / f);
                ft_hi = std::max(ft_hi, 1000.0 / f);
            }
        chart_cap = t(StringId::MeasureResultFrameChart);
        chart_range = replace_all(replace_all(t(StringId::MeasureFrameTimeRange), L"{min}", widen(cm::format_number(ft_lo, 1, tr))), L"{max}",
                                  widen(cm::format_number(ft_hi, 1, tr)));
    }
    const float chart_h = chart ? kSrcGap + kBarsH : 0.f;
    // Islemci / Ekran karti: bilesen raporunun gercek durumu (Uygulama Testi sonucunda; rapor yuklenirken yok).
    struct CompTile {
        std::wstring name, value;
        Color c;
        float vh = 0;
    };
    std::vector<CompTile> comps;
    const float comp_gap = g2;
    const float comp_w = std::max(std::floor((inner_w - comp_gap) / 2), 1.f);
    constexpr float kCompIn = 9.f;  // maket well p-2 (8) + cizgi
    const float comp_in = std::max(comp_w - 2 * kCompIn, 1.f);
    if (numbers && r && !bal && r->source == cm::TestSource::Builtin && !vm.components_loading) {
        for (const auto comp : {core::diagnosis::Component::Cpu, core::diagnosis::Component::Gpu}) {
            const auto it = std::find_if(vm.components.begin(), vm.components.end(), [&](const auto& f) { return f.component == comp; });
            if (it == vm.components.end()) continue;
            CompTile ct;
            ct.name = t(comp == core::diagnosis::Component::Cpu ? StringId::MeasureResultCpu : StringId::MeasureResultGpu);
            ct.value = tk(core::diagnosis::verdict_key(*it));
            const Color tone = it->verdict == core::diagnosis::Verdict::NoIssueFound ? p.success
                               : it->verdict == core::diagnosis::Verdict::Limiting   ? p.warning
                                                                                      : p.text_primary;
            ct.c = ensure_contrast(tone, p.surface, p.text_primary, 4.5);
            ct.vh = g.measure(Role::Semi12, ct.value, comp_in).h;
            comps.push_back(std::move(ct));
        }
    }
    float comp_vh = 0;
    for (const auto& c : comps) comp_vh = std::max(comp_vh, c.vh);
    const float comp_lbl_h = theme_.role(Role::Medium10).line;
    const float comp_h = comps.empty() ? 0.f : kCompIn + comp_lbl_h + g0 + comp_vh + kCompIn;
    // Alt serit (maket: pt-2 + border-t, mono 11px): sol fark, sag en iyi / en kotu kare suresi; sigmazsa alt alta.
    const float mono_line = theme_.role(Role::Mono11).line;
    const float delta_tw = delta_txt.empty() ? 0.f : std::ceil(g.measure(Role::Mono11, delta_txt, kUnbounded, false).w) + 1.f;
    const float range_tw = chart_range.empty() ? 0.f : std::ceil(g.measure(Role::Mono11, chart_range, kUnbounded, false).w) + 1.f;
    const bool strip_inline = delta_tw + g2 + range_tw <= inner_w;
    const int strip_lines = (delta_tw > 0 ? 1 : 0) + (range_tw > 0 ? 1 : 0);
    const float strip_h = strip_lines == 0 ? 0.f : 1.f + g2 + (strip_inline ? mono_line : static_cast<float>(strip_lines) * mono_line);
    const float numbers_h = stats_h + chart_h + (comps.empty() ? 0.f : kSrcGap + comp_h) + (strip_h > 0 ? kStripGap + strip_h : 0.f);

    // Denge sonucu: baslik (birincil ekranin cozunurlugu · hukum) + cozunurluk basina tek satir (hukum kelimesi + kayittaki
    // CPU / GPU FPS cifti). Satir sigmazsa FPS cifti ayni satirin altina iner.
    const float small_role_h = theme_.role(Role::StateTitle).line;
    struct BalLine {
        std::wstring res, word, pair, acc;
        Color c;
    };
    std::array<BalLine, core::diagnosis::kBalanceRows> blines;
    std::wstring bhead;
    Color bhead_c = p.text_primary;
    float bres_w = 0, bword_w = 0, bpair_w = 0;
    bool bsplit = false;
    const float bl_gap = g0;
    if (mode == ResultCardMode::Balance) {
        const auto sum = balance_result_summary(*bal, vm.display);
        const auto fps = [&](const std::optional<double>& v, bool at_least) {
            if (!v) return std::wstring(L"—");
            const auto n = widen(cm::format_number(*v, 0, tr));
            return at_least ? replace_all(t(StringId::MeasureResultAtLeast), L"{v}", n) : n;
        };
        for (std::size_t i = 0; i < blines.size(); ++i) {
            const auto& l = sum.lines[i];
            BalLine& b = blines[i];
            b.res = tk(cm::balance_row_key(l.row));
            switch (l.word) {
                case BottleneckWord::Cpu: b.word = t(StringId::PanelBottleneckCpu), b.c = p.orange; break;
                case BottleneckWord::None: b.word = t(StringId::PanelBottleneckNone), b.c = p.success; break;
                case BottleneckWord::Other: b.word = tk(cm::balance_verdict_key(l.verdict)), b.c = p.text_secondary; break;
            }
            b.c = ensure_contrast(b.c, p.surface, p.text_primary, 4.5);
            b.pair = replace_all(replace_all(t(StringId::MeasureResultBalancePair), L"{cpu}", fps(l.cpu_fps, l.cpu_at_least)), L"{gpu}",
                                 fps(l.gpu_fps, l.gpu_at_least));
            b.acc = a11y_join(replace_all(replace_all(tk("balance.card.row_acc"), L"{res}", b.res), L"{verdict}", b.word), b.pair);
            bres_w = std::max(bres_w, std::ceil(g.measure(Role::Secondary, b.res, kUnbounded, false).w) + 1.f);
            bword_w = std::max(bword_w, std::ceil(g.measure(Role::Body, b.word, kUnbounded, false).w) + 1.f);
            bpair_w = std::max(bpair_w, std::ceil(g.measure(Role::Secondary, b.pair, kUnbounded, false, true).w) + 1.f);
        }
        const auto& hl = blines[sum.headline];
        bhead = replace_all(replace_all(t(StringId::MeasureResultBalanceHeadline), L"{res}", hl.res), L"{verdict}", hl.word);
        bhead_c = hl.c;
        bres_w = std::min(bres_w + g1, inner_w * 0.3f);
        bsplit = bres_w + bword_w + theme_.space(2) + bpair_w > inner_w;
    }
    const float bline_h = bsplit ? bl + sl : bl;
    const std::wstring bnote = mode == ResultCardMode::Balance ? t(StringId::MeasureResultBalanceNote) : std::wstring();
    const float bnote_h = bnote.empty() ? 0.f : g.measure(Role::Small11, bnote, inner_w).h;
    const float balance_h = small_role_h + g1 + 3 * bline_h + 2 * bl_gap + (bnote_h > 0 ? g2 + bnote_h : 0.f);
    // Kayit yok: tek soluk satir + kisa ipucu (sayilar "—" ile cizilmez).
    const std::wstring none_hint = mode == ResultCardMode::Empty ? t(StringId::MeasureResultNoneHint) : std::wstring();
    const float hint_h = none_hint.empty() ? 0.f : g.measure(Role::Secondary, none_hint, inner_w).h;
    const float empty_h = bl + g0 + hint_h;
    const float body_h = mode == ResultCardMode::Balance ? balance_h : mode == ResultCardMode::Empty ? empty_h : numbers_h;
    RectF card;
    if (!slot.place(pad + header_h + kSrcGap + body_h + pad, card)) return;
    const float extra_h = mode == ResultCardMode::Numbers ? std::max(0.f, card.h - slot.natural) : 0.f;  // satir yuksekligi artinca kare suresi grafigi buyur (bos alan kalmaz)
    const float bars_h = kBarsH + extra_h;
    const float bal_extra = mode == ResultCardMode::Balance ? std::max(0.f, card.h - slot.natural) / 8.f : 0.f;  // artan yukseklik satirlar arasina (az) dagilir
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + pad;
    float y = card.y + pad;
    {
        const float hh = theme_.role(Role::Title14).line;
        measure_card_head(mc, x, y, meta_inline && !meta.empty() ? inner_w - meta_w - g2 : inner_w, L"M2 //", p.cyan, head, hh);
        const Color meta_c = busy ? p.accent : p.text_secondary;
        if (!meta.empty() && meta_inline) {
            TextOpts mo{Align::Trailing, false, true};
            mo.tabular = true;
            g.text(Role::Small11, meta, {x + inner_w - meta_w, y, meta_w, hh}, meta_c, mo);
        } else if (!meta.empty()) {
            g.text(Role::Small11, meta, {x, y + hh + g0, inner_w, meta_h}, meta_c);
        }
    }
    y += header_h + kSrcGap;
    if (numbers) {
        // Uc esit sutun: etiket 11px soluk; ORT. FPS 24px/700 camgobegi, digerleri 20px/600 beyaz (mt-0.5). Sayilar sayac
        // gibi dolar (motion.result_count). UIA: uzun etiket + gercek deger; ortalama kare ms ekranda yok (cubuklar kare
        // suresini gosterir), UIA'da yasar.
        for (std::size_t i = 0; i < stat_idx.size(); ++i) {
            const auto& n = nums[stat_idx[i]];
            const float cx = x + static_cast<float>(i) * (stat_w + stat_gap);
            g.text(Role::Small11, fit_text(g, Role::Small11, stat_lbl[i], stat_w), {cx, y, stat_w, stat_lbl_h}, p.text_secondary, {Align::Leading, false});
            TextOpts vo{Align::Leading, false, false, true};
            vo.tabular = true;
            const Role vr = i == 0 ? Role::Num24 : Role::Semi20;
            const float vy = y + stat_lbl_h + (i == 0 ? 0.f : 2.f);
            g.text(vr, fit_text(g, vr, n.value, stat_w), {cx, vy, stat_w, theme_.role(vr).line}, n.c, vo);
            if (i > 0) hits.add_text(seq.take(), {cx, y, stat_w, stats_h}, a11y_join(n.label + L" " + real[stat_idx[i]], n.reason));
        }
        hits.add_text(seq.take(), {x, y, inner_w, stats_h}, a11y_join(nums[2].label + L" " + real[2], nums[2].reason));
        y += stats_h;
        if (chart) {
            y += kSrcGap;
            const float cy0 = y;
            const float bw = std::max((inner_w - kBarsGap * static_cast<float>(ft.size() - 1)) / static_cast<float>(ft.size()), 1.f);
            const double mx = std::max(*std::max_element(ft.begin(), ft.end()), 1e-9);
            std::vector<double> sorted = ft;
            std::sort(sorted.begin(), sorted.end());
            const double med = sorted[sorted.size() / 2];
            const Color bar_c = dark ? p.cyan.with_alpha(0.75f) : p.text_primary;
            const Color warn_c = dark ? p.warning : p.text_primary;
            for (std::size_t k = 0; k < ft.size(); ++k) {
                const float h = std::max(2.f, bars_h * static_cast<float>(ft[k] / mx));
                // En yavas kova yalniz ortancanin belirgin ustundeyse vurgulanir (rengin yaninda yukseklik de farkli).
                const bool worst = ft[k] >= mx && mx >= 1.3 * med;
                g.fill_round({x + static_cast<float>(k) * (bw + kBarsGap), y + bars_h - h, bw, h}, 2.f, worst ? warn_c : bar_c);
            }
            y += bars_h;
            hits.add_text(seq.take(), {x, cy0, inner_w, bars_h}, a11y_join(a11y_join(chart_cap, chart_range), t(StringId::MeasureFrameTimeCaption)));
        }
        if (!comps.empty()) {
            y += kSrcGap;
            for (std::size_t i = 0; i < comps.size(); ++i) {
                const auto& c = comps[i];
                const RectF cr{x + static_cast<float>(i) * (comp_w + comp_gap), y, comp_w, comp_h};
                draw_well(g, cr);
                g.text(Role::Medium10, fit_text(g, Role::Medium10, c.name, comp_in), {cr.x + kCompIn, cr.y + kCompIn, comp_in, comp_lbl_h}, p.text_secondary,
                       {Align::Leading, false});
                g.text(Role::Semi12, c.value, {cr.x + kCompIn, cr.y + kCompIn + comp_lbl_h + g0, comp_in, comp_vh}, c.c);
                hits.add_text(seq.take(), cr, c.name + L": " + c.value);
            }
            y += comp_h;
        }
        if (strip_h > 0) {
            const float sy = card.bottom() - pad - strip_h;
            g.fill({x, sy, inner_w, 1.f}, dark ? Color{1.f, 1.f, 1.f, 0.06f} : divider);
            float ty = sy + 1.f + g2;
            TextOpts so{Align::Leading, false, true};
            so.tabular = true;
            if (delta_tw > 0) {
                g.text(Role::Mono11, delta_txt, {x, ty, std::min(delta_tw, inner_w), mono_line}, delta_fg, so);
                if (!strip_inline) ty += mono_line;
            }
            if (range_tw > 0) {
                TextOpts ro{strip_inline && delta_tw > 0 ? Align::Trailing : Align::Leading, false, true};
                ro.tabular = true;
                g.text(Role::Mono11, chart_range, strip_inline && delta_tw > 0 ? RectF{x + inner_w - range_tw, ty, range_tw, mono_line} : RectF{x, ty, std::min(range_tw, inner_w), mono_line},
                       p.text_secondary, ro);
            }
        }
    } else if (mode == ResultCardMode::Balance) {
        g.text(Role::StateTitle, fit_text(g, Role::StateTitle, bhead, inner_w), {x, y, inner_w, small_role_h}, bhead_c, {Align::Leading, false, true});
        y += small_role_h + g1 + bal_extra;
        const float word_x = x + bres_w;
        const float word_w = std::max(std::min(bword_w, inner_w - bres_w), 1.f);
        for (const auto& b : blines) {
            g.text(Role::Secondary, fit_text(g, Role::Secondary, b.res, bres_w), {x, y, bres_w, bl}, p.text_secondary, {Align::Leading, false, true});
            g.text(Role::Body, fit_text(g, Role::Body, b.word, word_w), {word_x, y, word_w, bl}, b.c, {Align::Leading, false, true});
            TextOpts po{bsplit ? Align::Leading : Align::Trailing, false, true};
            po.tabular = true;
            const RectF pr = bsplit ? RectF{word_x, y + bl, std::max(x + inner_w - word_x, 1.f), sl}
                                    : RectF{x + inner_w - bpair_w, y, bpair_w, bl};
            g.text(Role::Secondary, fit_text(g, Role::Secondary, b.pair, pr.w), pr, p.text_primary, po);
            y += bline_h + bl_gap + bal_extra;
        }
        if (bnote_h > 0) g.text(Role::Small11, bnote, {x, y + g2 - bl_gap, inner_w, bnote_h}, p.text_secondary, {Align::Leading, true});
    } else {
        g.text(Role::Body, fit_text(g, Role::Body, type, inner_w), {x, y, inner_w, bl}, p.text_secondary, {Align::Leading, false, true});
        y += bl + g0;
        g.text(Role::Secondary, none_hint, {x, y, inner_w, hint_h}, p.text_secondary);
    }
    {
        // UIA (Status0+2): baslik, zaman, test turu, sonuc basligi, dogrulama / kare deseni (ekranda yok), ortalama FPS ve
        // fark. Sayilar kartinda kutular, grafik ve bilesen kutulari kendi metin ogeleridir (ust uste binmez).
        std::wstring uia = a11y_join(a11y_join(head, when), type);
        if (r) {
            if (bal) {
                uia = a11y_join(std::move(uia), bhead);
                for (const auto& b : blines) uia = a11y_join(std::move(uia), b.acc);
                uia = a11y_join(std::move(uia), tk("balance.summary.lead"));
                for (const auto& l : balance_verdict_lines(*bal)) uia = a11y_join(std::move(uia), l);
            } else {
                uia = a11y_join(std::move(uia), result_headline(*r));
            }
            if (r->show_confidence) uia = a11y_join(std::move(uia), tk(cm::confidence_key(r->confidence)));
            if (r->pattern != cm::FramePattern::Insufficient) {
                const auto key = r->pattern == cm::FramePattern::Stable          ? StringId::MeasurePatternStable
                                 : r->pattern == cm::FramePattern::MicroStutter ? StringId::MeasurePatternMicroStutter
                                                                                : StringId::MeasurePatternSpiky;
                uia = a11y_join(std::move(uia), t(key));
            }
        }
        if (numbers) {
            uia = a11y_join(std::move(uia), nums[0].label + L" " + real[0]);
            uia = a11y_join(std::move(uia), nums[0].reason);
            uia = a11y_join(std::move(uia), delta_acc);
        }
        uia = a11y_join(std::move(uia), none_hint);
        const float uh = numbers ? header_h + kSrcGap + stats_h : card.h - 2 * pad;
        hits.add_text(static_cast<Hit>(static_cast<int>(Hit::Status0) + 2), {x, card.y + pad, inner_w, uh}, std::move(uia));
    }
}

// Spec 2026-11-12 B: "bu oyunda ne sinirladi" (kare basi GPU mesgul suresi + kare siniri suphesi). Yalniz
// Oyunda Test sonucunda vardir; hukum kapsamli ("Bu oyunda, bu ayarlarda"), olculmeyen deger yazilmaz.
void Screen::measure_game_limit_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    HitMap& hits = mc.hits;
    StatusSeq& seq = mc.seq;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float gap = mc.gap;
    const float section_h = mc.section_h;
    if (!m.result || !m.result->game_limit) return;
    const float inner_w = slot.area.w - 2 * pad;
    const auto texts = game_limit_ui_texts(*m.result->game_limit);
    const std::wstring title = tk("game_limit.card.title");
    const float head_h = g.measure(Role::Section, texts.headline, inner_w).h;
    std::vector<float> line_h, note_h;
    float lines_total = 0, notes_total = 0;
    for (const auto& l : texts.lines) {
        line_h.push_back(g.measure(Role::Body, l, inner_w).h);
        lines_total += line_h.back() + theme_.space(0);
    }
    for (const auto& n : texts.notes) {
        note_h.push_back(g.measure(Role::Secondary, n, inner_w).h);
        notes_total += note_h.back() + theme_.space(0);
    }
    const float card_h = pad + section_h + gap + head_h + (texts.lines.empty() ? 0.f : theme_.space(1) + lines_total) +
                         (texts.notes.empty() ? 0.f : gap + notes_total) + pad;
    RectF card;
    if (!slot.place(card_h, card)) return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    float y = card.y + pad;
    g.text(Role::Section, title, {card.x + pad, y, inner_w, section_h}, p.text_primary, {Align::Leading, false});
    y += section_h + gap;
    // Hukum bir ariza bildirimi degildir: uyari rengi kullanilmaz; "belirlenemedi" ikincil tonda.
    const bool undetermined = m.result->game_limit->result.verdict == core::diagnosis::GameLimit::Undetermined;
    g.text(Role::Section, texts.headline, {card.x + pad, y, inner_w, head_h}, undetermined ? p.text_secondary : p.text_primary);
    y += head_h;
    if (!texts.lines.empty()) {
        y += theme_.space(1);
        for (std::size_t i = 0; i < texts.lines.size(); ++i) {
            g.text(Role::Body, texts.lines[i], {card.x + pad, y, inner_w, line_h[i]}, p.text_primary);
            y += line_h[i] + theme_.space(0);
        }
    }
    if (!texts.notes.empty()) {
        y += gap;
        for (std::size_t i = 0; i < texts.notes.size(); ++i) {
            g.text(Role::Secondary, texts.notes[i], {card.x + pad, y, inner_w, note_h[i]}, p.text_secondary);
            y += note_h[i] + theme_.space(0);
        }
    }
    std::wstring uia = a11y_join(title, texts.headline);
    for (const auto& l : texts.lines) uia = a11y_join(std::move(uia), l);
    for (const auto& n : texts.notes) uia = a11y_join(std::move(uia), n);
    hits.add_text(seq.take(), card, std::move(uia));
}

// DIAG-08: takilma raporu (BOOST maketi DIAG: "DIAG //" sari + baslik, sagda sonuc rozeti; alanlar well kutulari, altinda
// kanit satirlari). Yalniz gercek kare + hizali telemetriyle; eksik veri acikca yazilir.
void Screen::measure_stutter_card(const MeasureCtx& mc, CardSlot& slot) {
    Graphics& g = mc.g;
    HitMap& hits = mc.hits;
    const Palette& p = mc.p;
    const cm::MeasureState& m = mc.m;
    const float pad = mc.pad;
    const float g0 = mc.g0;
    const float g2 = mc.g2;
    const bool tr = mc.tr;
    const bool dark = mc.dark;
    const float inner_w = slot.area.w - 2 * pad;
    if (m.result && m.result->drops) {
        const auto& rep = *m.result->drops;
        const auto texts = drop_report_texts(rep);
        const std::wstring ttl = t(StringId::DropUiTitle);
        const float head_h = theme_.role(Role::Title14).line;
        // Sonuc rozeti (sagda): ton yalniz renk degil, sozcuk de yazar.
        const Color hc = rep.outcome == core::diagnosis::DropOutcome::NoDrops
                             ? p.success
                             : rep.outcome == core::diagnosis::DropOutcome::DropsFound ? p.warning : p.text_secondary;
        const float badge_in = 8.f, badge_h = theme_.role(Role::Small11).line + 4.f;
        const float badge_w = std::min(std::ceil(g.measure(Role::Small11, texts.headline, kUnbounded, false).w) + 1.f + 2 * badge_in, inner_w * 0.6f);
        // Alanlar: "etiket: deger" -> well (etiket 10px buyuk harf, deger mono 12px/600); iki noktasiz satir kanit gibi altta.
        struct Fld {
            std::wstring label, value;
        };
        std::vector<Fld> flds;
        std::vector<std::wstring> loose;
        const std::wstring lcid = tr ? L"tr-TR" : L"en-US";
        const auto upper = [&](const std::wstring& in) {
            if (in.empty()) return in;
            std::wstring out(in.size() * 2 + 4, L'\0');
            const int n = LCMapStringEx(lcid.c_str(), LCMAP_UPPERCASE, in.c_str(), static_cast<int>(in.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr, 0);
            if (n <= 0) return in;
            out.resize(static_cast<std::size_t>(n));
            return out;
        };
        for (const auto& f : texts.fields) {
            const auto at = f.find(L": ");
            if (at == std::wstring::npos || at == 0) loose.push_back(f);
            else flds.push_back({upper(f.substr(0, at)), f.substr(at + 2)});
        }
        constexpr float kWellInD = 9.f, kWGap = 8.f;
        const int cols = inner_w >= 400.f ? 4 : 2;
        const float ww = std::max(std::floor((inner_w - (static_cast<float>(cols) - 1.f) * kWGap) / static_cast<float>(cols)), 1.f);
        const float win = std::max(ww - 2 * kWellInD, 1.f);
        const float lbl_h = theme_.role(Role::Medium10).line;
        std::vector<float> vh;
        const auto val_role = [](const std::wstring& v) {  // sayili degerler mono (maket: 2.763, 15 sn, 7,4 ms); sozcukler Segoe (Yuksek)
            return std::any_of(v.begin(), v.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }) ? Role::Mono12Semi : Role::Semi12;
        };
        for (const auto& f : flds) vh.push_back(g.measure(val_role(f.value), f.value, win).h);
        std::vector<float> row_h;
        float wells_h = 0;
        for (std::size_t i = 0; i < flds.size(); i += static_cast<std::size_t>(cols)) {
            float rh = 0;
            for (std::size_t k = i; k < std::min(flds.size(), i + static_cast<std::size_t>(cols)); ++k) rh = std::max(rh, vh[k]);
            row_h.push_back(2 * kWellInD + lbl_h + g0 + rh);
            wells_h += row_h.back() + (row_h.size() > 1 ? kWGap : 0.f);
        }
        std::vector<std::wstring> lines = loose;
        for (const auto& e : texts.evidence) lines.push_back(e);
        std::vector<float> lh;
        float lines_h = 0;
        for (const auto& l : lines) {
            lh.push_back(g.measure(Role::Small11, l, inner_w).h);
            lines_h += lh.back() + g0;
        }
        const float card_h = pad + head_h + kSrcGap + wells_h + (lines.empty() ? 0.f : g2 + lines_h) + pad;
        RectF card;
        if (!slot.place(card_h, card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        const float x = card.x + pad;
        float y = card.y + pad;
        const float tw = std::max(inner_w - badge_w - g2, 1.f);
        measure_card_head(mc, x, y, tw, L"DIAG //", p.warning, ttl, head_h);
        {
            const RectF br{x + inner_w - badge_w, y + (head_h - badge_h) / 2, badge_w, badge_h};
            if (dark) g.fill_round(br, kOptRadius, hc.with_alpha(0.15f));
            else g.stroke_round(br, kOptRadius, p.border);
            g.text(Role::Small11, fit_text(g, Role::Small11, texts.headline, badge_w - 2 * badge_in), br, dark ? hc : p.text_primary, {Align::Center, false, true});
        }
        y += head_h + kSrcGap;
        for (std::size_t i = 0; i < flds.size(); ++i) {
            const std::size_t row = i / static_cast<std::size_t>(cols), col = i % static_cast<std::size_t>(cols);
            float wy = y;
            for (std::size_t k = 0; k < row; ++k) wy += row_h[k] + kWGap;
            const RectF well{x + static_cast<float>(col) * (ww + kWGap), wy, ww, row_h[row]};
            draw_well(g, well);
            g.text(Role::Medium10, fit_text(g, Role::Medium10, flds[i].label, win), {well.x + kWellInD, well.y + kWellInD, win, lbl_h}, p.text_secondary, {Align::Leading, false});
            g.text(val_role(flds[i].value), flds[i].value, {well.x + kWellInD, well.y + kWellInD + lbl_h + g0, win, vh[i]}, p.text_primary, {Align::Leading, true});
        }
        y += wells_h;
        if (!lines.empty()) {
            y += g2;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                g.text(Role::Small11, lines[i], {x, y, inner_w, lh[i]}, i < loose.size() ? p.text_primary : p.text_secondary);
                y += lh[i] + g0;
            }
        }
        std::wstring uia = ttl + L". " + texts.headline;
        for (const auto& f : texts.fields) uia += L". " + f;
        for (const auto& e : texts.evidence) uia += L". " + e;
        hits.add_text(static_cast<Hit>(static_cast<int>(Hit::Status0) + 3), card, std::move(uia));
    }
}

}  // namespace ef::ui
