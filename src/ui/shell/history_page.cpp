// Handoff v1 REC-01/REC-02: "Islem Gecmisi" sayfasi — kalici journal kayitlari (en yeni once). Her islem:
// kapsam, tarih, durum; her adim: ayar adi, kaydedilmis onceki deger ve yazilan deger. Degerler journal'dan
// birebir (bilinen GUID/acik-kapali metne cevrilir; gerisi ham). Geri alma bu sayfadan degil sahibi olan
// kontrolden yapilir (Performans Kapali / Baslangic anahtari) — tek islem geri alma dugmesi yok, bos dugme yok.
//
// R15: "Akran karsilastirmasi" karti. Yerel akran havuzu bugun BOS; durum daima "veri havuzu kurulmadi"
// (Insufficient, N=0) olarak durustce gosterilir. Yeterli gercek ornek olmadan yuzdelik/siralama yazilmaz.
// R16: yerel test gecmisi: iki kayit secilir; yalniz ayni kaynak + kosul izi karsilastirilir. Farklar
// gercek olculen metriklerden (mutlak + yuzde; eksik metrik 0 degil), gecerlilik farklari ayrica gosterilir.
// Disa aktarma yalniz kullanici dugmeyle baslatinca yerel dosyaya yazilir (ag/yukleme yok).
//
// Spec 2026-11-14 B13: baslik seridinde gorunum cipleri (Tumu / Degisiklikler / Testler). Degisiklikler kendi kartinda,
// gun basliklari (Bugun / Dun / tarih) altinda tek satirlik kayitlar (durum noktasi, saat, baslik, durum cipi, ok);
// ayrinti yalniz satir acikken. Kapsam alt suzgeci (Performans / Hazirlik / Baslangic) kartin baslik seridinde. Testler
// ayri kartta; bir onceki test karsilastirilabilirse (ayni kaynak + kosul izi + surumler + gecerlilik) FPS farki satirda.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

#include "core/insights/comparison_policy.h"
#include "core/measure/bench_text.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cr = core::recovery;
namespace ci = core::insights;

namespace {
std::wstring widen_h(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring rep_h(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
bool local_tm(std::int64_t unix_ms, std::tm& tm) {
    if (unix_ms <= 0) return false;
    const std::time_t t = static_cast<std::time_t>(unix_ms / 1000);
    return localtime_s(&tm, &t) == 0;
}
std::wstring when_text(std::int64_t unix_ms, bool turkish) {
    std::tm tm{};
    if (!local_tm(unix_ms, tm)) return L"—";
    wchar_t buf[40];
    if (turkish) swprintf_s(buf, L"%02d.%02d.%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    else swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}
// B13: satirdaki saat (gun basligin altinda) ve gun basliginin tarih bicimi.
std::wstring clock_text(std::int64_t unix_ms) {
    std::tm tm{};
    if (!local_tm(unix_ms, tm)) return L"—";
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}
std::wstring date_text(std::int64_t unix_ms, bool turkish) {
    std::tm tm{};
    if (!local_tm(unix_ms, tm)) return L"—";
    wchar_t buf[24];
    if (turkish) swprintf_s(buf, L"%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    else swprintf_s(buf, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}
int tm_day_key(const std::tm& tm) { return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday; }
// Gercek degerin gorunen bicimi; deger yoksa cagiran "eksik" yazar (0 degil).
std::wstring num_text(double v, bool turkish) {
    if (!std::isfinite(v)) return L"—";
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f", v);
    std::wstring s = buf;
    while (s.size() > 1 && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    if (turkish) std::replace(s.begin(), s.end(), L'.', L',');  // diger sayfalarla ayni ondalik isareti
    return s;
}
std::wstring signed_num_text(double v, bool turkish) { return (v >= 0 ? L"+" : L"") + num_text(v, turkish); }
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
// "startup:user_run:537465616d" -> "Steam" (ad hex olarak saklanir). Cozulemezse bos.
std::string startup_name(std::string_view target) {
    if (!target.starts_with("startup:")) return {};
    const auto p = target.rfind(':');
    const auto hex = target.substr(p + 1);
    if (hex.empty() || hex.size() % 2 || hex.size() > 512) return {};
    std::string out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int a = nib(hex[i]), b = nib(hex[i + 1]);
        if (a < 0 || b < 0) return {};
        const char ch = static_cast<char>(a * 16 + b);
        if (static_cast<unsigned char>(ch) < 0x20) return {};
        out.push_back(ch);
    }
    return out;
}
}  // namespace

int history_day_key(std::int64_t unix_ms) {
    std::tm tm{};
    return local_tm(unix_ms, tm) ? tm_day_key(tm) : 0;
}

std::wstring Screen::history_value(std::string_view raw) const {
    const auto v = lower(std::string(raw));
    if (v == "enabled" || v == "on" || v == "true") return t(StringId::HistoryValueOn);
    if (v == "disabled" || v == "off" || v == "false") return t(StringId::HistoryValueOff);
    if (v == "381b4222-f694-41f0-9685-ff5bb260df2e") return t(StringId::HistorySchemeBalanced);
    if (v == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") return t(StringId::HistorySchemeHigh);
    if (v == "a1841308-3541-4fab-bc81-f71556f20b4a") return t(StringId::HistorySchemeSaver);
    if (v == "e9a42b02-d5df-448d-aa00-03f14749eb61") return t(StringId::HistorySchemeUltimate);
    auto w = widen_h(raw);
    if (w.size() > 48) w = w.substr(0, 47) + L"…";
    return w;
}

HistoryDot history_dot(cr::HistoryStatus st) {
    switch (st) {
        case cr::HistoryStatus::Active: return HistoryDot::Good;
        case cr::HistoryStatus::PartlyRestored: return HistoryDot::Partial;
        case cr::HistoryStatus::RecoveryRequired: return HistoryDot::Recovery;
        default: return HistoryDot::Neutral;
    }
}

Page history_undo_page(std::string_view scope) {
    // Sistem araclari, Baslangic bolumune kaydirilmis (eski Baslangic kimligi ayni tek sayfayi o bolumde acar).
    if (scope == "startup") return Page::Startup;
    if (scope == "fixes" || scope == "machine_fixes") return Page::Readiness;
    return Page::Overview;
}

bool history_undo_shown(cr::HistoryStatus st) { return st == cr::HistoryStatus::Active || st == cr::HistoryStatus::RecoveryRequired; }

float Screen::history_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin (pb-1) 16 DIP altinda baslatir; ortak duzen 8 verir (Oyun
    // hazirligi / Olcumler ile ayni 5 DIP pay).
    constexpr float kTopInset = 5.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    // Spec 2026-11-14 B13: iki kart (Degisiklikler, Son 3 test); gorunum cipi hangilerinin cizilecegini secer. Kart cercevesi
    // icerikten once son cizimdeki yukseklikle cizilir; yukseklik degisirse kabuk ayni karede bir kez daha cizer
    // (history_relayout). Kartlarin altinda disa aktarma sonucu ve kisa akran satiri.
    const auto& p = pal();
    const auto& hv = vm.history;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const bool hc = pal_ != &theme_.dark;
    const float bl = theme_.role(Role::Body).line;
    const float sl = theme_.role(Role::Secondary).line;
    constexpr float kUnbounded = 10000.f;
    const float x = body.x, inner_w = body.w;
    const float pad = theme_.card_pad;
    const float gap = 12.f;  // maket: Gecmis gap-3
    sec_head_gap_ = gap;
    const Color cyan_card = ensure_contrast(p.cyan, p.surface, p.text_primary, 4.5);
    StatusSeq seq{0};
    float y = body.y;
    const auto single_w = [&](Role role, const std::wstring& text) { return std::ceil(g.measure(role, text, kUnbounded, false, true).w) + 1.f; };
    const HistoryTab tab = s.history_tabs ? s.history_tab : HistoryTab::All;

    auto source_label = [&](std::string_view source) -> std::wstring {
        if (source == "builtin") return t(StringId::HistorySourceBuiltin);
        if (source == "in_game") return t(StringId::HistorySourceGame);
        if (source == "endurance") return t(StringId::TestEndurance);
        if (source == core::measure::kBalanceSource) return t(StringId::HistorySourceBalance);
        return t(StringId::HistorySourceUnknown);
    };
    auto condition_label = [&](std::string_view key) -> std::wstring {
        // EF-X15 dogrulama bulgusu 2: etiketler ortak politika tablosundan; ham alan kimligi kullaniciya gosterilmez.
        if (const auto id = ci::comparison_field_label(key)) return t(*id);
        return t(StringId::HistoryCompareCondUnknown);
    };
    auto join_labels = [&](const std::vector<std::string>& keys) -> std::wstring {
        std::wstring out;
        for (const auto& k : keys) out += (out.empty() ? L"" : L", ") + condition_label(k);
        return out;
    };
    auto exclusion_text = [&](const std::string& code) -> std::wstring {
        auto s2 = tk("bench." + code);  // "run.too_short" -> bench.run.too_short
        if (s2.empty() && code == "drop.insufficient") s2 = tk("drop.outcome.insufficient");
        if (s2.empty() && code == "drop.unaligned_sources") s2 = tk("drop.evidence.unaligned_sources_ignored");
        return s2.empty() ? widen_h(code) : s2;
    };
    auto exclusion_list = [&](const std::vector<std::string>& codes) -> std::wstring {
        std::wstring out;
        for (const auto& c : codes) out += (out.empty() ? L"" : L", ") + exclusion_text(c);
        return out;
    };
    // Karsilastirmada gosterilen kullanici metrikleri tek tabloda; ic anahtarlar (scene_util_sampled...) gosterilmez.
    struct MetricInfo {
        std::string_view key, label, unit;
    };
    static constexpr MetricInfo kMetricInfo[] = {
        {"avg_fps", "bench.metric.avg_fps", "unit.fps"},
        {"scene_avg_fps", "bench.metric.avg_fps", "unit.fps"},
        {"combined_avg_fps", "bench.metric.avg_fps", "unit.fps"},
        {"low1_fps", "bench.metric.low1_fps", "unit.fps"},
        {"scene_low1_fps", "bench.metric.low1_fps", "unit.fps"},
        {"combined_low1_fps", "bench.metric.low1_fps", "unit.fps"},
        {"p99_ms", "bench.metric.p99_ms", "unit.ms"},
        {"scene_p99_ms", "bench.metric.p99_ms", "unit.ms"},
        {"combined_p99_ms", "bench.metric.p99_ms", "unit.ms"},
        {"balance.cpu_fps", "balance.metric.cpu_fps", "unit.fps"},
        {"balance.gpu_fps_1080", "balance.metric.gpu_fps_1080", "unit.fps"},
        {"balance.gpu_fps_1440", "balance.metric.gpu_fps_1440", "unit.fps"},
        {"balance.gpu_fps_2160", "balance.metric.gpu_fps_2160", "unit.fps"},
        {"cold_fps", "measure.endurance.cold", "unit.fps"},
        {"warm_fps", "measure.endurance.warm", "unit.fps"},
        {"fps_change_pct", "measure.endurance.change", ""},
        {"cpu_st_units_per_s", "bench.metric.cpu_st", ""},
        {"cpu_mt_units_per_s", "bench.metric.cpu_mt", ""},
        {"mem_mt_read_gbps", "bench.metric.mem_read", ""},
        {"mem_mt_write_gbps", "bench.metric.mem_write", ""},
        {"mem_mt_copy_gbps", "bench.metric.mem_copy", ""},
        {"mem_latency_ns", "bench.metric.mem_latency", ""},
        {"storage_seq_1m_qd8_read_mbps", "bench.metric.seq_read", ""},
        {"storage_seq_1m_qd8_write_mbps", "bench.metric.seq_write", ""},
    };
    auto metric_info = [](std::string_view key) -> const MetricInfo* {
        for (const auto& m : kMetricInfo)
            if (m.key == key) return &m;
        return nullptr;
    };
    auto metric_label = [&](std::string_view key) -> std::wstring {
        if (const auto* m = metric_info(key))
            if (auto s2 = tk(m->label); !s2.empty()) return s2;
        return widen_h(key);
    };
    auto metric_unit = [&](std::string_view key) -> std::wstring {
        const auto* m = metric_info(key);
        return m && !m->unit.empty() ? tk(m->unit) : std::wstring();
    };
    auto test_title = [&](const ci::HistoryEntry& e) -> std::wstring {
        if (e.source == "in_game" && !e.title.empty()) return widen_h(e.title);
        const auto tier = core::measure::scene_tier_from_title(e.title);
        if (!tier.empty())
            if (auto s2 = tk(core::measure::tier_key(tier)); !s2.empty()) return s2;
        return source_label(e.source);  // taninmayan baslik: ic kimlik yazilmaz
    };

    // Kart: cerceve son cizimdeki yukseklikle (icerikten once), sonra icerik; yukseklik degistiyse yeniden cizim istenir.
    bool any_card = false;
    s.history_export_enabled = false;
    const auto card = [&](std::size_t idx, auto&& draw) {
        if (s.history_card_h[idx] > 0) y += sec_head_h() + sec_head_gap_;  // .sec-rule basligi kartin ustunde (section_head_v2)
        const RectF c{x, y, inner_w, s.history_card_h[idx]};
        if (c.h > 0) {
            if (c.bottom() >= s.view_top && c.y <= s.view_bottom) draw_card(g, c);
            else log_card(c);
        }
        const float used = draw(c);
        if (std::fabs(used - c.h) > 0.5f) {
            s.history_card_h[idx] = used;
            s.history_relayout = true;
        }
        if (used > 0) y += used + gap, any_card = true;
    };

    // ======================= Degisiklikler (gun gruplu tek satirlar) =======================
    s.history_op_rows.clear();
    s.history_go_rows.clear();
    s.history_filters = false;
    s.history_filter_shown = {false, false, false, false};
    const auto changes_card = [&](const RectF& c) -> float {
        const float cx = c.x + pad, cw = std::max(c.w - 2 * pad, 1.f);
        const bool has_rows = !hv.loading && !hv.view.rows.empty();
        // Baslik ozeti: kayit sayisi; okunamayan kayit varsa (zorunlu uyari) kisa sari metin, tam cumle HelpText'te.
        const std::size_t bad = hv.view.corrupt + hv.view.foreign + hv.view.newer;
        std::wstring summary;
        Color summary_c = p.text_secondary;
        std::wstring help = a11y_join(t(StringId::HistoryIntroTitle), t(StringId::HistoryIntro));
        if (bad > 0) {
            summary = rep_h(t(StringId::HistoryUnreadableShort), L"{count}", std::to_wstring(bad));
            summary_c = p.warning;
            help = a11y_join(rep_h(t(StringId::HistoryUnreadable), L"{count}", std::to_wstring(bad)), help);
        } else if (has_rows) {
            summary = rep_h(t(StringId::HistoryCount), L"{n}", std::to_wstring(std::max<std::size_t>(hv.view.total, hv.view.rows.size())));
        }
        // Kapsam alt suzgeci (Tumu / Performans / Hazirlik / Baslangic): en az iki kapsamda kayit varsa ya da biri seciliyse;
        // sayisi 0 olan kapsam cipi cizilmez (secili olan haric). "Tumu" her zaman ilk cip (tum kayitlara acik donus yolu).
        // Hepsi kart genisligine sigmazsa cizilmez ve tum kayitlar gosterilir.
        struct F {
            HistoryFilter f;
            StringId label;
        };
        static constexpr F fs[] = {{HistoryFilter::All, StringId::HistoryFilterAll},
                                   {HistoryFilter::Perf, StringId::HistoryFilterPerf},
                                   {HistoryFilter::Fixes, StringId::HistoryFilterFixes},
                                   {HistoryFilter::Startup, StringId::HistoryFilterStartup}};
        std::array<std::wstring, std::size(fs)> labels, counts;
        std::array<float, std::size(fs)> ws{};
        std::array<bool, std::size(fs)> on{};
        const float chip_gap = theme_.space(1);
        float chips_w = 0;
        int scopes = 0;
        if (has_rows) {
            for (std::size_t i = 0; i < std::size(fs); ++i) {
                labels[i] = t(fs[i].label);
                const std::size_t n = history_filter_rows(hv.view, fs[i].f).size();
                counts[i] = std::to_wstring(n);
                const bool all = fs[i].f == HistoryFilter::All;
                scopes += !all && n > 0 ? 1 : 0;
                on[i] = all || n > 0 || fs[i].f == s.history_filter;
                ws[i] = filter_chip_width(g, labels[i], counts[i]);
                if (on[i]) chips_w += (chips_w > 0 ? chip_gap : 0.f) + ws[i];
            }
        }
        const bool sub = has_rows && (scopes >= 2 || s.history_filter != HistoryFilter::All) && chips_w > 0 && chips_w <= cw;
        RectF slot{};
        const float line_y = section_head_v2(g, c, L"01", false, t(StringId::HistorySectionChanges), summary, summary_c, sub ? chips_w : 0.f, hits, seq,
                                             help, sub ? &slot : nullptr, true);
        if (sub) {
            s.history_filters = true;
            float sx = slot.x;
            for (std::size_t i = 0; i < std::size(fs); ++i) {
                s.history_filter_shown[static_cast<std::size_t>(fs[i].f)] = on[i];
                if (!on[i]) continue;
                const std::wstring name = rep_h(rep_h(t(StringId::HistoryFilterCount), L"{label}", labels[i]), L"{n}", counts[i]);
                constexpr float kPillH = 24.f;  // maket: .filter-pill
                filter_chip(g, {sx, slot.y + (slot.h - kPillH) / 2, ws[i], kPillH}, history_filter_hit(fs[i].f), labels[i], counts[i], s.history_filter == fs[i].f, name, s, now, hits);
                sx += ws[i] + chip_gap;
            }
        }
        float yy = section_card_rule(g, c, line_y);
        if (hv.view.total > hv.view.rows.size()) {
            const auto more = rep_h(rep_h(t(StringId::HistoryMore), L"{shown}", std::to_wstring(hv.view.rows.size())), L"{total}", std::to_wstring(hv.view.total));
            const RectF mr{cx, yy, cw, sl};
            g.text(Role::Secondary, fit_text(g, Role::Secondary, more, cw), mr, p.text_secondary, {Align::Leading, false, true});
            hits.add_text(seq.take(), mr, more);
            yy += sl + theme_.space(1);
        }

        const auto shown = history_filter_rows(hv.view, sub ? s.history_filter : HistoryFilter::All);
        if (hv.loading || shown.empty()) {
            // Bos durum en cok bir kisa satir; eski iki cumlelik aciklama satirin HelpText'inde.
            const bool none = !hv.loading && hv.view.rows.empty();
            const auto msg = t(hv.loading ? StringId::HistoryLoading : none ? StringId::HistoryEmptyShort : StringId::HistoryFilterEmpty);
            const RectF er{cx, yy, cw, bl};
            g.text(Role::Body, fit_text(g, Role::Body, msg, cw), er, p.text_secondary, {Align::Leading, false, true});
            const Hit eid = seq.take();
            hits.add_text(eid, er, msg);
            if (none) hits.meta[eid].help = t(StringId::PageHistoryEmpty);
            yy += bl;
            return std::ceil(yy + pad - c.y);
        }
        struct StepLine {
            std::wstring name, before, after, uia;
            int count = 1;
        };
        struct OpRow {
            std::size_t ri = 0;
            int day = 0;
            std::wstring when, clock, op, state, state_full, head, full, hint, go;
            ChipTone tone = ChipTone::Warn;
            Color dot;
            bool open = false, go_shown = false;
            float detail_h = 0, f = 0;
            std::vector<StepLine> steps;
        };
        std::vector<OpRow> ops;
        float time_w = 0;
        for (const std::size_t ri : shown) {
            const auto& r = hv.view.rows[ri];
            OpRow o;
            o.ri = ri;
            const StringId scope = r.scope == "fixes"           ? StringId::HistoryScopeFixes
                                   : r.scope == "machine_fixes" ? StringId::HistoryScopeMachineFixes  // M2: HAGS duzeltmesi
                                   : r.scope == "startup"       ? StringId::HistoryScopeStartup
                                   : r.scope == "machine"       ? StringId::HistoryScopeMachine
                                                                : StringId::HistoryScopeUser;
            StringId state_id = StringId::HistoryStateIncomplete, state_short = StringId::HistoryStateShortIncomplete;
            switch (r.status) {
                case cr::HistoryStatus::Active: state_id = state_short = StringId::HistoryStateActive, o.tone = ChipTone::Good; break;
                case cr::HistoryStatus::Restored: state_id = state_short = StringId::HistoryStateRestored, o.tone = ChipTone::Good; break;
                case cr::HistoryStatus::PartlyRestored:
                    state_id = StringId::HistoryStatePartial, state_short = StringId::HistoryStateShortPartial, o.tone = ChipTone::Warn;
                    break;
                case cr::HistoryStatus::RecoveryRequired: state_id = state_short = StringId::HistoryStateRecovery, o.tone = ChipTone::Danger; break;
                case cr::HistoryStatus::KeptCurrent:
                    state_id = StringId::HistoryStateKept, state_short = StringId::HistoryStateShortKept, o.tone = ChipTone::Neutral;
                    break;
                case cr::HistoryStatus::Incomplete: break;
            }
            switch (history_dot(r.status)) {
                case HistoryDot::Good: o.dot = p.success; break;
                case HistoryDot::Partial: o.dot = p.warning; break;
                case HistoryDot::Recovery: o.dot = p.error; break;
                case HistoryDot::Neutral: o.dot = p.text_secondary; break;
            }
            o.day = history_day_key(r.created_unix_ms);
            o.when = when_text(r.created_unix_ms, tr);
            o.clock = clock_text(r.created_unix_ms);
            o.state = t(state_short);
            o.state_full = t(state_id);
            o.head = t(scope) + L" · " + o.when;
            o.full = t(scope) + L" · " + o.state_full;  // satirdan kalkan kapsam: acik ayrintinin ilk satiri
            const std::string key = history_row_key(r);
            o.open = s.history_all_open || s.history_open.count(key) != 0;
            o.f = o.open ? 1.f : 0.f;
            if (key == s.history_anim_key && s.history_anim.active(now)) o.f = std::clamp(s.history_anim.value(now, theme_.easing), 0.f, 1.f);
            // Adimlar: ayni degisikligin tekrarlari tek satir (xN). Islem ozeti: farkli adim adlari, virgulle.
            for (const auto& sr : r.steps) {
                StepLine l;
                l.name = tk("history.op." + std::string(contracts::to_string(sr.op)));
                if (const auto nm = startup_name(sr.target); !nm.empty()) l.name += L" — " + widen_h(nm);
                l.before = sr.existed ? history_value(sr.old_value) : t(StringId::HistoryNotExisted);
                l.after = sr.written.empty() ? t(StringId::HistoryNotWritten) : history_value(sr.written);
                // Kullanici dis degeri korudu: hata degil, kendi karari (ayar yazilmadi).
                if (sr.state == cr::StepState::KeptExternal) l.after += L" (" + t(StringId::HistoryStepKept) + L")";
                else if (sr.has_error) l.after += L" (" + t(StringId::HistoryStepError) + L")";
                const auto same = std::find_if(o.steps.begin(), o.steps.end(), [&](const StepLine& q) {
                    return q.name == l.name && q.before == l.before && q.after == l.after;
                });
                if (same != o.steps.end()) {
                    ++same->count;
                    continue;
                }
                l.uia = t(StringId::HistoryBefore) + L" " + l.before + L" → " + t(StringId::HistoryWritten) + L" " + l.after;
                if (std::none_of(o.steps.begin(), o.steps.end(), [&](const StepLine& q) { return q.name == l.name; }))
                    o.op += (o.op.empty() ? L"" : L", ") + l.name;
                o.steps.push_back(std::move(l));
            }
            if (o.op.empty()) o.op = L"—";
            for (auto& l : o.steps)
                if (l.count > 1) l.name += L" ×" + std::to_wstring(l.count);
            // Geri alma erisimi: sahibi olan sayfaya goturen dugme; eski yonlendirme cumlesi dugmenin HelpText'i.
            if (history_undo_shown(r.status)) {
                o.go_shown = true;
                const Page target = history_undo_page(r.scope);
                o.go = t(target == Page::Overview ? StringId::HistoryGoPanel : target == Page::Readiness ? StringId::HistoryGoFixes : StringId::HistoryGoStartup);
                o.hint = t(systools_page(target) ? StringId::HistoryUndoHintStartup
                           : target == Page::Readiness ? StringId::HistoryUndoHintFixes
                                                       : StringId::HistoryUndoHintPerf);
            }
            time_w = std::max(time_w, single_w(Role::Secondary, o.clock));
            ops.push_back(std::move(o));
        }
        // Gun basliklari: Bugun / Dun / tarih (yerel takvim).
        std::int64_t now_unix = hv.now_unix_ms;
        if (now_unix <= 0) now_unix = static_cast<std::int64_t>(std::time(nullptr)) * 1000;
        int today = 0, yesterday = 0;
        {
            std::tm tm{};
            if (local_tm(now_unix, tm)) {
                today = tm_day_key(tm);
                tm.tm_mday -= 1;
                tm.tm_hour = 12;
                tm.tm_isdst = -1;
                std::mktime(&tm);  // ay / yil sinirini duzeltir
                yesterday = tm_day_key(tm);
            }
        }
        // Satir: [durum noktasi][saat][baslik][durum cipi][ok]. Sigmiyorsa cip basligin altina (row2_h).
        const float g1 = theme_.space(1), g2 = theme_.space(2), g3 = theme_.space(3);
        constexpr float kDotR = 5.f;
        const float chev_w = theme_.button_row_h;
        float state_w = 0;
        for (const auto& o : ops) state_w = std::max(state_w, chip_width(g, o.state));
        constexpr float kOpMinW = 150.f;
        const float dot_x = cx + g2 + kDotR;
        const float time_x = dot_x + kDotR + g2;
        const float op_x = time_x + time_w + g3;
        const float right = cx + cw;
        const bool one_line = op_x + kOpMinW + g2 + state_w + g2 + chev_w <= right;
        const float op_w = one_line ? right - chev_w - g2 - state_w - g2 - op_x : std::max(right - chev_w - g2 - op_x, 1.f);
        const float band = one_line ? theme_.row_h : theme_.row2_h;
        const float line_h = one_line ? band : theme_.row_h - 4.f;
        const float step_h = sl + theme_.space(0);
        constexpr float kStepWellH = 30.f, kStepWellGap = 8.f;  // maket: adim cukuru (.well-v2 r8, py 7) ve aralari
        for (auto& o : ops)
            if (o.f > 0)
                o.detail_h = theme_.space(0) + step_h + static_cast<float>(o.steps.size()) * (kStepWellH + kStepWellGap) + (o.go_shown ? g1 + theme_.button_row_h : 0.f) + g2;
        const auto toggle_name = t(StringId::HistoryRowDetails);
        int prev_day = -1;
        for (auto& o : ops) {
            if (o.day != prev_day) {
                if (prev_day != -1) yy += g2;
                const std::wstring day = o.day == 0 ? std::wstring(L"—") : o.day == today ? t(StringId::HistoryDayToday)
                                                    : o.day == yesterday ? t(StringId::HistoryDayYesterday)
                                                                         : date_text(hv.view.rows[o.ri].created_unix_ms, tr);
                const RectF dr{cx, yy, cw, sl};
                g.text(Role::Secondary, fit_text(g, Role::Secondary, day, cw), dr, p.text_secondary, {Align::Leading, false, true});
                hits.add_text(seq.take(), dr, day);
                yy += sl + g1;
                prev_day = o.day;
            }
            const float total_h = band + o.detail_h * o.f;
            const bool visible = yy + total_h >= s.view_top && yy <= s.view_bottom;
            const Hit id = hit_of(HitKind::HistoryRow, o.ri);
            const RectF row{cx, yy, cw, band};
            // Satirin kendisi ac/kapa dugmesidir (satira basinca / Enter); UIA adi tam satir metni.
            std::wstring uia = a11y_join(a11y_join(rep_h(toggle_name, L"{row}", o.head), o.state_full), o.op);
            if (id != Hit::None) {
                if (visible) {
                    const float hv2 = hover_value(s, id, now);
                    if (hv2 > 0.f || o.open) g.fill_round(row, theme_.radius_control, mix(p.surface, p.surface_raised, std::max(hv2, o.open ? 0.6f : 0.f)));
                    if (s.focus_visible && s.focus == id) focus_ring(g, row, theme_.radius_control, hits);
                }
                hits.add(id, row, uia, AccRole::Expander, o.open);
                s.history_op_rows.push_back(o.ri);
            }
            if (visible) {
                ++s.rows_drawn;
                const float dot_y = yy + line_h / 2;
                g.fill_circle(dot_x, dot_y, kDotR, o.dot);
                if (hc) g.arc(dot_x, dot_y, kDotR + 1.f, 1.f, 0.f, 360.f, p.text_primary);
                g.text(Role::Secondary, o.clock, {time_x, yy, time_w, line_h}, p.text_secondary, {Align::Leading, false, true, true});
                g.text(Role::Body, fit_text(g, Role::Body, o.op, op_w), {op_x, yy, op_w, line_h}, p.text_primary, {Align::Leading, false, true});
                if (one_line) {
                    const float sw = chip_width(g, o.state);  // okun yanina sag yasli
                    chip(g, RectF{right - chev_w - g1 - sw, yy + (band - sl) / 2, sw, sl}, o.state, o.tone);
                } else {
                    const float sw = chip_width(g, o.state);
                    if (op_x + sw <= right - chev_w) chip(g, Point{op_x, yy + theme_.row_h - 6.f}, o.state, o.tone);
                }
                g.text(Role::Body, o.open ? L"▴" : L"▾", {right - chev_w, yy, chev_w, line_h}, p.text_secondary, {Align::Center, false, true});
            }
            yy += band;
            if (o.f > 0) {
                // Acilan ayrinti: kapsam · tam durum, sonra her ayar tek satir "ayar · eski → yeni" (ok ve yeni deger camgobegi).
                // Acilis gecisinde satirlar acilan yukseklige sigdikca belirir (kirpma yok).
                const float reveal = o.detail_h * o.f;
                const float dx = op_x;
                const float dw = std::max(right - dx, 1.f);
                float dy = yy + theme_.space(0);
                if (visible && dy + sl <= yy + reveal + 0.5f)
                    g.text(Role::Secondary, fit_text(g, Role::Secondary, o.full, dw), {dx, dy, dw, sl}, p.text_secondary, {Align::Leading, false, true});
                dy += step_h;
                for (const auto& l : o.steps) {
                    uia = a11y_join(a11y_join(std::move(uia), l.name), l.uia);
                    if (dy + kStepWellH > yy + reveal + 0.5f || !visible) {
                        dy += kStepWellH + kStepWellGap;
                        continue;
                    }
                    // Adim cukuru: solda ad (13px/600 beyaz), sagda mono 12px "eski -> yeni" (eski gri, ok + yeni yesil).
                    const RectF wr{dx, dy, dw, kStepWellH};
                    draw_well(g, wr);
                    constexpr float kWellPadX = 12.f;
                    const std::wstring after = L" → " + l.after;
                    const float aw = std::min(single_w(Role::Mono12, after), (wr.w - 2 * kWellPadX) * 0.55f);
                    const float bw = std::min(single_w(Role::Mono12, l.before), std::max(wr.w - 2 * kWellPadX - aw, 1.f) * 0.6f);
                    const float rx = wr.right() - kWellPadX;
                    g.text(Role::Mono12, fit_text(g, Role::Mono12, after, aw), {rx - aw, wr.y, aw, wr.h}, hc ? p.text_primary : p.accent, {Align::Trailing, false, true});
                    g.text(Role::Mono12, fit_text(g, Role::Mono12, l.before, bw), {rx - aw - bw, wr.y, bw, wr.h}, p.text_secondary, {Align::Trailing, false, true});
                    const float nw = std::max(wr.w - 2 * kWellPadX - aw - bw - theme_.space(2), 1.f);
                    g.text(Role::Semi13, fit_text(g, Role::Semi13, l.name, nw), {wr.x + kWellPadX, wr.y, nw, wr.h}, p.text_primary, {Align::Leading, false, true});
                    dy += kStepWellH + kStepWellGap;
                }
                if (o.go_shown && o.open) {
                    const float by = dy + g1;
                    const Hit gid = hit_of(HitKind::HistoryGo, o.ri);
                    if (gid != Hit::None) {
                        if (by + theme_.button_row_h <= yy + reveal + 0.5f && visible) {
                            button(g, dx, by, gid, o.go, false, s, now, hits, true, false, true);
                        } else {
                            hits.add(gid, button_rect(g, dx, by, o.go, true), o.go, AccRole::Button);  // acilis gecisi: cizilmeden kayit
                        }
                        hits.meta[gid].help = o.hint;
                        s.history_go_rows.push_back(o.ri);
                    }
                }
                // Tam kapsam + durum metni ve geri alma ipucu: ayrinti blogunun UIA adinda (bilgi kaybolmaz).
                uia = a11y_join(a11y_join(std::move(uia), o.full), o.hint);
                hits.add_text(seq.take(), {dx, yy, dw, std::max(reveal, 1.f)}, uia);
                yy += reveal;
            }
        }
        return std::ceil(yy + pad - c.y);
    };

    // ======================= Son 3 test (ayri kart) =======================
    s.history_test_rows = 0;
    const auto tests_card = [&](const RectF& c) -> float {
        const float tx0 = c.x + pad, tw0 = std::max(c.w - 2 * pad, 1.f);
        const std::size_t rows = hv.loading ? 0 : std::min<std::size_t>(hv.tests.size(), static_cast<std::size_t>(kMaxMeasureRecent));
        s.history_test_rows = rows;
        std::wstring sel_line;
        if (rows >= 2 && hv.selected.size() < 2) sel_line = rep_h(t(StringId::HistoryTestsSelectCount), L"{count}", std::to_wstring(hv.selected.size()));
        // R16: "Disa aktar" 02 bolum basliginin saginda; yalniz kullanici baslatinca yerel dosyaya yazar; veri yokken devre disi.
        const auto export_label = t(StringId::HistoryExportButton);
        const float export_w = button_rect(g, 0, 0, export_label, true).w;
        RectF export_slot{};
        const float line_y = section_head_v2(g, c, L"02", false, t(StringId::MeasureRecentTitle), sel_line, p.text_secondary, export_w, hits, seq,
                                             a11y_join(t(StringId::HistoryTestsIntro), rows >= 2 ? t(StringId::HistoryTestsNeedTwo) : std::wstring()),
                                             &export_slot, true);
        {
            const bool enabled = hv.export_enabled && rows > 0;
            button_face(g, export_slot, Hit::HistoryExport, export_label, ButtonKind::Secondary, enabled, s, now, hits);
            hits.add(Hit::HistoryExport, export_slot, export_label, AccRole::Button, false, -1, enabled);
            hits.meta[Hit::HistoryExport].help = t(StringId::HistoryExportNote);  // spec B6: kayit notu ekrandan kalkti
            s.history_export_enabled = enabled;
        }
        if (hv.loading) {
            // Okunuyor: tek satir (kart bos kalmaz; baslik kartin ustundedir).
            const auto msg = t(StringId::HistoryLoading);
            const RectF lr{tx0, c.y + pad, tw0, bl};
            g.text(Role::Body, fit_text(g, Role::Body, msg, tw0), lr, p.text_secondary, {Align::Leading, false, true});
            hits.add_text(seq.take(), lr, msg);
            return std::ceil(2 * pad + bl);
        }
        float yy = section_card_rule(g, c, line_y);
        if (rows == 0) {
            const auto empty = t(StringId::MeasureResultNone);  // bos durum tek kisa satir
            const RectF er{tx0, yy, tw0, sl};
            g.text(Role::Secondary, empty, er, p.text_secondary, {Align::Leading, false, true});
            hits.add_text(seq.take(), er, a11y_join(t(StringId::MeasureRecentTitle), empty));
            yy += sl;
            s.history_bars_key.clear();
            return std::ceil(yy + pad - c.y);
        }
        const float row_h = theme_.row_h;
        const auto select_text = t(StringId::HistoryTestsSelect);
        const auto selected_text = t(StringId::HistoryTestsSelected);
        for (std::size_t i = 0; i < rows; ++i) {
            if (i > 0) yy += theme_.space(1);
            const auto& e = hv.tests[i];
            const bool selected = std::find(hv.selected.begin(), hv.selected.end(), i) != hv.selected.end();
            const std::wstring title = test_title(e);
            std::wstring sub = when_text(e.when_unix_ms, tr) + L" · " +
                               (e.source == core::measure::kBalanceSource ? t(StringId::HistorySourceBuiltin) : source_label(e.source));
            std::wstring fps;
            std::string fps_key;
            for (const char* key : {"scene_avg_fps", "avg_fps"})
                if (const auto it = e.metrics.find(key); it != e.metrics.end()) {
                    fps = num_text(it->second, tr) + L" " + metric_unit(key);
                    fps_key = key;
                    break;
                }
            // B13: bir onceki (daha eski) test karsilastirilabilirse (ortak kural: ayni kaynak + kosul izi + surumler + kosu
            // gecerliligi) ayni FPS metriginin farki; degilse hicbir sey yazilmaz.
            std::wstring delta;
            double delta_v = 0;
            if (!fps_key.empty() && i + 1 < hv.tests.size()) {
                const auto cmp = ci::compare_history(hv.tests[i + 1], e);
                if (cmp.compatibility.compatible)
                    for (const auto& m : cmp.metrics)
                        if (m.key == fps_key && m.delta && std::isfinite(*m.delta)) {
                            delta_v = *m.delta;
                            delta = signed_num_text(delta_v, tr) + L" " + metric_unit(fps_key);
                            break;
                        }
            }
            const Hit id = hit_of(HitKind::HistorySelect, i);
            const RectF row{tx0, yy, tw0, row_h};
            // Satirin tamami secim dugmesidir (iki satir secilince karsilastirma seridi acilir; ucuncu secim en eskisini birakir).
            const float hv2 = hover_value(s, id, now);
            const Color row_bg = selected ? mix(p.surface, p.cyan, alpha_scale::kSelectionTint) : mix(p.surface, p.surface_raised, 0.5f + hv2 * 0.5f);
            g.fill_round(row, theme_.radius_control, row_bg);
            g.stroke_round(row, theme_.radius_control, selected ? p.cyan : p.border, selected ? 1.5f : 1.f);
            constexpr float kMarkR = 6.f;
            const float mx = row.x + theme_.space(2) + kMarkR;
            // Maket: onay kutusu (16 DIP, r4): secili camgobegi dolgu + isaret, degilse ikincil cerceve.
            const RectF cbox{mx - 8.f, row.y + row_h / 2 - 8.f, 16.f, 16.f};
            if (selected) {
                g.fill_round(cbox, 4.f, p.cyan);
                g.fill_round(cbox.inset(4.f), 2.f, p.background);  // secili isareti (metin degil: buyuk metinde tasmaz)
            } else {
                g.stroke_round(cbox, 4.f, p.text_secondary, 1.5f);
            }
            const float tx = mx + kMarkR + theme_.space(2);
            const float fps_w = fps.empty() ? 0.f : single_w(Role::Body, fps);
            const float delta_w = delta.empty() ? 0.f : single_w(Role::Secondary, delta);
            const float fx = row.right() - theme_.space(2) - fps_w;
            const float dx = fx - (delta_w > 0 ? theme_.space(2) + delta_w : 0.f);
            const float avail = std::max(dx - theme_.space(3) - tx, 1.f);
            const float title_w = std::min(single_w(Role::Body, title), avail * 0.55f);
            g.text(Role::Body, fit_text(g, Role::Body, title, title_w), {tx, row.y, title_w, row_h}, p.text_primary, {Align::Leading, false, true});
            const float sx = tx + title_w + theme_.space(3);
            const float sw = std::max(tx + avail - sx, 1.f);
            g.text(Role::Secondary, fit_text(g, Role::Secondary, sub, sw), {sx, row.y, sw, row_h}, p.text_secondary, {Align::Leading, false, true});
            if (delta_w > 0) {
                const Color dc = delta_v > 0 ? ensure_contrast(p.success, row_bg, p.text_primary, 4.5)
                                 : delta_v < 0 ? ensure_contrast(p.error, row_bg, p.text_primary, 4.5)
                                               : p.text_secondary;
                g.text(Role::Secondary, delta, {dx, row.y, delta_w, row_h}, dc, {Align::Trailing, false, true, true});
            }
            if (fps_w > 0)
                g.text(Role::Body, fps, {fx, row.y, fps_w, row_h}, ensure_contrast(p.cyan, row_bg, p.text_primary, 4.5), {Align::Trailing, false, true, true});
            if (s.focus_visible && s.focus == id) focus_ring(g, row, theme_.radius_control, hits);
            hits.add(id, row,
                     t(StringId::MeasureRecentTitle) + L" " + std::to_wstring(i + 1) + L": " + title + L" · " + sub + (fps.empty() ? L"" : L" · " + fps) +
                         (delta.empty() ? L"" : L" · " + rep_h(t(StringId::HistoryTestsDeltaPrev), L"{delta}", delta)) + L" · " + (selected ? selected_text : select_text),
                     AccRole::Button, selected);
            yy += row_h;
        }
        // --- Karsilastirma seridi (iki secim) ---
        if (hv.selected.size() == 2 && hv.comparison) {
            yy += theme_.space(2);
            const auto& c2 = *hv.comparison;
            // Ekrandan kalkan ayrinti satirlari (yorum, diger metrikler, haric tutulanlar, kohort): seridin HelpText'i.
            std::wstring help;
            std::wstring warn_line;
            // Karsilastirma kurali degismez: ayni kaynak + kosul izi (+ surumler) uyusmali (conditions_match). Yalniz kosu
            // gecerliligi eksikse sayilar yine yazilir; uyumsuzluk cumlesi seritte sari uyari satiridir.
            const bool comparable = c2.compatibility.compatible || c2.compatibility.conditions_match;
            if (!c2.compatibility.compatible) {
                help = rep_h(t(StringId::HistoryCompareIncompatible), L"{fields}", join_labels(c2.compatibility.differing));
                if (!c2.compatibility.note_key.empty()) help = a11y_join(std::move(help), tk(c2.compatibility.note_key));
                if (comparable) warn_line = help;
            }
            const ci::MetricChange* fps_m = nullptr;
            if (comparable) {
                switch (c2.attribution) {
                    case ci::Attribution::NoChangeInContext: help = a11y_join(std::move(help), t(StringId::HistoryCompareAttrNone)); break;
                    case ci::Attribution::SingleChangeCoincides:
                        help = a11y_join(std::move(help), rep_h(t(StringId::HistoryCompareAttrSingle), L"{key}", join_labels(c2.context.changed)));
                        break;
                    case ci::Attribution::ConfoundedMultipleChanges: {
                        const auto l = rep_h(t(StringId::HistoryCompareAttrConfounded), L"{keys}", join_labels(c2.context.changed));
                        if (warn_line.empty()) warn_line = l;
                        else help = a11y_join(std::move(help), l);
                        break;
                    }
                    case ci::Attribution::ContextMissing:
                        if (warn_line.empty()) warn_line = t(StringId::HistoryCompareAttrMissing);
                        else help = a11y_join(std::move(help), t(StringId::HistoryCompareAttrMissing));
                        break;
                }
                for (const auto& m : c2.metrics) {
                    if (!metric_info(m.key)) continue;
                    const auto missing = t(StringId::HistoryCompareMissing);
                    const auto unit = metric_unit(m.key);
                    std::wstring before = m.base_value ? num_text(*m.base_value, tr) : missing;
                    std::wstring after = m.current_value ? num_text(*m.current_value, tr) : missing;
                    std::wstring line = metric_label(m.key) + L": " + before + L" → " + after;
                    if (m.delta) {
                        line += L" · " + signed_num_text(*m.delta, tr) + (unit.empty() ? L"" : L" " + unit);
                        line += m.percent ? L" · " + signed_num_text(*m.percent, tr) + L"%" : L" · " + t(StringId::HistoryCompareZeroBase);
                    } else if (m.base_value && m.current_value && !m.not_comparable_key.empty()) {
                        line += L" · " + tk(m.not_comparable_key);
                    }
                    help = a11y_join(std::move(help), line);
                    if (!fps_m && m.delta && m.base_value && m.current_value && metric_unit(m.key) == tk("unit.fps") &&
                        (m.key == "avg_fps" || m.key == "scene_avg_fps" || m.key == "combined_avg_fps" || m.key == "balance.cpu_fps" || m.key == "cold_fps"))
                        fps_m = &m;
                }
                if (!c2.exclusions_only_base.empty()) {
                    const auto l = rep_h(t(StringId::HistoryCompareExclBase), L"{list}", exclusion_list(c2.exclusions_only_base));
                    if (warn_line.empty()) warn_line = l;
                    else help = a11y_join(std::move(help), l);
                }
                if (!c2.exclusions_only_current.empty()) {
                    const auto l = rep_h(t(StringId::HistoryCompareExclCurrent), L"{list}", exclusion_list(c2.exclusions_only_current));
                    if (warn_line.empty()) warn_line = l;
                    else help = a11y_join(std::move(help), l);
                }
            }
            if (c2.compatibility.compatible) {
                const auto num = [](double v) {
                    wchar_t b[32];
                    swprintf(b, 32, L"%+.1f", v);
                    return std::wstring(b);
                };
                if (hv.cohort.enough) {
                    auto line = rep_h(rep_h(rep_h(rep_h(rep_h(t(StringId::HistoryCohortResult), L"{a}", std::to_wstring(hv.cohort.n_off)), L"{b}", std::to_wstring(hv.cohort.n_on)),
                                                  L"{delta}", num(*hv.cohort.delta)), L"{low}", num(*hv.cohort.ci_low)), L"{high}", num(*hv.cohort.ci_high));
                    if (hv.cohort.uncertain) line += L" " + t(StringId::HistoryCohortUncertain);
                    help = a11y_join(std::move(help), line);
                } else if (hv.cohort.applicable) {
                    help = a11y_join(std::move(help), t(StringId::HistoryCohortNeedRuns));
                }
            }
            const float strip_pad = theme_.space(2);
            const Color strip_bg = mix(p.surface, p.surface_raised, 0.5f);
            if (!comparable) {
                // Uymayan cift: tek satir, sayi yazilmaz.
                const auto line = t(StringId::HistoryCompareIncomparable);
                const RectF strip{tx0, yy, tw0, row_h};
                g.fill_round(strip, theme_.radius_control, strip_bg);
                g.stroke_round(strip, theme_.radius_control, p.warning.with_alpha(alpha_scale::kOutline));
                const RectF lr{tx0 + strip_pad, yy, tw0 - 2 * strip_pad, row_h};
                g.text(Role::Body, fit_text(g, Role::Body, line, lr.w), lr, ensure_contrast(p.warning, strip_bg, p.text_primary, 4.5), {Align::Leading, false, true});
                const Hit cid = seq.take();
                hits.add_text(cid, strip, line);
                hits.meta[cid].help = help;
                yy += row_h;
                s.history_bars_key.clear();
            } else {
                // Fark (FPS, isaretli; iyilesme yesil, dusus kirmizi) + iki cubuk (eski / yeni; motion.compare_bars ile dolar).
                const std::size_t a_i = hv.selected[0], b_i = hv.selected[1];
                const bool a_first = hv.tests[a_i].when_unix_ms <= hv.tests[b_i].when_unix_ms;
                const std::size_t old_i = a_first ? a_i : b_i, new_i = a_first ? b_i : a_i;
                const std::string key = std::to_string(old_i) + "/" + std::to_string(new_i);
                if (key != s.history_bars_key) {
                    const int ms = effective_ms(theme_.motion.compare_bars, s.reduced_motion || s.motion_paused || vm.game_gate_blocked);
                    s.history_bars = Tween(ms > 0 ? 0.f : 1.f);
                    s.history_bars.retarget(1.f, now, ms);
                    s.history_bars_key = key;
                }
                // Maket #history-compare-box: bg #00D4FF @4%, border #00D4FF @40% (yuksek kontrastta duz cizgi), kuyu yaricapi 8.
                constexpr float kCmpRadius = 8.f;
                const Color cmp_bg = hc ? strip_bg : mix(strip_bg, p.cyan, 0.04f);
                const float grow = std::clamp(s.history_bars.value(now, theme_.easing), 0.f, 1.f);
                const float bar_row = sl + theme_.space(1);
                const float strip_h = strip_pad + bl + theme_.space(1) + 2 * bar_row + (warn_line.empty() ? 0.f : sl) + strip_pad;
                const RectF strip{tx0, yy, tw0, strip_h};
                g.fill_round(strip, kCmpRadius, cmp_bg);
                g.stroke_round(strip, kCmpRadius, hc ? p.border : p.cyan.with_alpha(0.40f));
                const float sx = tx0 + strip_pad, sw = tw0 - 2 * strip_pad;
                float sy = yy + strip_pad;
                const auto unit = fps_m ? metric_unit(fps_m->key) : tk("unit.fps");
                std::wstring delta = L"—";
                Color dc = p.text_secondary;
                if (fps_m && fps_m->delta) {
                    delta = signed_num_text(*fps_m->delta, tr) + L" " + unit;
                    if (fps_m->percent) delta += L" (" + signed_num_text(*fps_m->percent, tr) + L"%)";
                    dc = *fps_m->delta > 0 ? ensure_contrast(p.success, cmp_bg, p.text_primary, 4.5)
                         : *fps_m->delta < 0 ? ensure_contrast(p.error, cmp_bg, p.text_primary, 4.5)
                                             : p.text_primary;
                }
                const auto dlabel = t(StringId::HistoryCompareDelta);
                const float dlw = single_w(Role::Body, dlabel);
                g.text(Role::Body, dlabel, {sx, sy, dlw, bl}, p.text_secondary, {Align::Leading, false, true});
                g.text(Role::Body, fit_text(g, Role::Body, delta, std::max(sw - dlw - theme_.space(2), 1.f)), {sx + dlw + theme_.space(2), sy, std::max(sw - dlw - theme_.space(2), 1.f), bl}, dc,
                       {Align::Leading, false, true, true});
                const Hit cid = seq.take();
                hits.add_text(cid, strip, dlabel + L": " + delta);
                hits.meta[cid].help = help;
                sy += bl + theme_.space(1);
                const double va = fps_m && fps_m->base_value ? *fps_m->base_value : 0.0;
                const double vb = fps_m && fps_m->current_value ? *fps_m->current_value : 0.0;
                const double vmax = std::max(va, vb);
                const std::wstring la = test_title(hv.tests[old_i]) + L" · " + when_text(hv.tests[old_i].when_unix_ms, tr);
                const std::wstring lb = test_title(hv.tests[new_i]) + L" · " + when_text(hv.tests[new_i].when_unix_ms, tr);
                const float label_w = std::min(std::max(single_w(Role::Secondary, la), single_w(Role::Secondary, lb)), sw * 0.4f);
                const auto bar = [&](const std::wstring& label, std::optional<double> v, const Color& col) {
                    g.text(Role::Secondary, fit_text(g, Role::Secondary, label, label_w), {sx, sy, label_w, sl}, p.text_secondary, {Align::Leading, false, true});
                    const std::wstring vt = v ? num_text(*v, tr) + L" " + unit : L"—";
                    const float vw = single_w(Role::Secondary, vt);
                    const float bx = sx + label_w + theme_.space(2);
                    const float bw = std::max(sx + sw - vw - theme_.space(2) - bx, 1.f);
                    const RectF track{bx, sy + (sl - 6) / 2, bw, 6};
                    g.fill_round(track, 3, p.surface_raised);
                    if (hc) g.stroke_round(track, 3, p.border);
                    if (v && vmax > 0) {
                        const float w = track.w * grow * static_cast<float>(std::clamp(*v / vmax, 0.0, 1.0));
                        if (w > 0.5f) g.fill_round({track.x, track.y, std::max(6.f, w), track.h}, 3, col);
                    }
                    g.text(Role::Secondary, vt, {sx + sw - vw, sy, vw, sl}, p.text_primary, {Align::Trailing, false, true, true});
                    hits.add_text(seq.take(), {sx, sy, sw, sl}, label + L": " + vt);
                    sy += bar_row;
                };
                bar(la, fps_m ? fps_m->base_value : std::nullopt, p.text_secondary);
                bar(lb, fps_m ? fps_m->current_value : std::nullopt, p.cyan);
                if (!warn_line.empty()) {
                    const RectF wr{sx, sy, sw, sl};
                    g.text(Role::Secondary, fit_text(g, Role::Secondary, warn_line, sw), wr, ensure_contrast(p.warning, cmp_bg, p.text_primary, 4.5),
                           {Align::Leading, false, true});
                    hits.add_text(seq.take(), wr, warn_line);
                }
                yy += strip_h;
            }
        } else {
            s.history_bars_key.clear();
        }
        return std::ceil(yy + pad - c.y);
    };

    if (history_shows_changes(tab)) card(0, changes_card);
    else if (const std::size_t bad = hv.view.corrupt + hv.view.foreign + hv.view.newer; bad > 0) {
        // Okunamayan kayit uyarisi zorunlu: degisiklik karti gizliyken (Testler gorunumu) kartlarin ustunde kisa sari satir;
        // tam cumle HelpText'te. Gorunum cipleri yalniz gorunumu degistirir, uyariyi gizlemez.
        const auto msg = rep_h(t(StringId::HistoryUnreadableShort), L"{count}", std::to_wstring(bad));
        const RectF wr{x, y, inner_w, sl};
        g.text(Role::Secondary, fit_text(g, Role::Secondary, msg, inner_w), wr, ensure_contrast(p.warning, p.background, p.text_primary, 4.5),
               {Align::Leading, false, true});
        const Hit wid = seq.take();
        hits.add_text(wid, wr, msg);
        hits.meta[wid].help = rep_h(t(StringId::HistoryUnreadable), L"{count}", std::to_wstring(bad));
        y += sl + theme_.space(2);
    }
    if (history_shows_tests(tab)) card(1, tests_card);
    if (any_card) y -= gap;

    // Disa aktarma sonucu (kayit yolu / hata): kisa satir. Kayit notu "Disa aktar" dugmesinin HelpText'inde.
    std::wstring export_status;
    Color export_c = p.text_secondary;
    if (hv.export_done) {
        export_status = rep_h(rep_h(t(StringId::HistoryExportSaved), L"{path}", hv.export_path), L"{bytes}", std::to_wstring(hv.export_bytes));
        export_c = ensure_contrast(p.success, p.background, p.text_primary, 4.5);
    } else if (hv.export_failed) {
        export_status = hv.export_error_key.empty() ? t(StringId::HistoryExportFailedWrite) : tk(hv.export_error_key);
        if (export_status.empty()) export_status = t(StringId::HistoryExportFailedWrite);
        export_c = ensure_contrast(p.warning, p.background, p.text_primary, 4.5);
    }
    if (!export_status.empty()) {
        const float eh = g.measure(Role::Secondary, export_status, inner_w).h;
        const RectF er{x, y + theme_.space(2), inner_w, eh};
        g.text(Role::Secondary, export_status, er, export_c);
        hits.add_text(seq.take(), er, export_status);
        y = er.bottom();
    }
    // R15: akran karsilastirmasi (durust yetersiz durum) tek kisa satir; tam durum / kosul / gereksinim / not HelpText'te.
    if (!hv.loading && history_shows_tests(tab)) {
        const auto& peer = hv.peer;
        std::wstring status, full;
        switch (peer.outcome) {
            case ci::PeerOutcome::Comparable:
                status = full = rep_h(rep_h(t(StringId::HistoryPeerComparable), L"{count}", std::to_wstring(peer.matching_samples)), L"{pct}",
                                      num_text(peer.percentile.value_or(0.0), tr));
                break;
            case ci::PeerOutcome::Insufficient:
                switch (peer.reason) {
                    case ci::PeerInsufficientReason::NoPool:
                        full = t(StringId::HistoryPeerNoPool);
                        status = t(StringId::HistoryPeerNoneShort);
                        break;
                    case ci::PeerInsufficientReason::NoMatchingSamples:
                    case ci::PeerInsufficientReason::InvalidQuery:
                        full = t(StringId::HistoryPeerNoMatching);
                        status = t(StringId::HistoryPeerNoneShort);
                        break;
                    case ci::PeerInsufficientReason::BelowMinimum:
                        full = rep_h(rep_h(t(StringId::HistoryPeerBelowMin), L"{count}", std::to_wstring(peer.matching_samples)), L"{min}",
                                     std::to_wstring(peer.required_samples));
                        status = rep_h(rep_h(t(StringId::HistoryPeerBelowShort), L"{count}", std::to_wstring(peer.matching_samples)), L"{min}",
                                       std::to_wstring(peer.required_samples));
                        break;
                }
                break;
        }
        std::wstring conditions = hv.peer_query.source.empty() ? t(StringId::HistoryPeerConditionNone) : source_label(hv.peer_query.source);
        for (const auto& [k, v] : hv.peer_query.conditions) conditions += L" · " + widen_h(k) + L"=" + widen_h(v);
        std::wstring help = rep_h(t(StringId::HistoryPeerConditions), L"{conditions}", conditions);
        if (peer.outcome != ci::PeerOutcome::Comparable)
            help = a11y_join(rep_h(t(StringId::HistoryPeerRequirement), L"{min}", std::to_wstring(peer.required_samples)), help);
        help = a11y_join(a11y_join(full, std::move(help)), t(StringId::HistoryPeerNote));
        const std::wstring line = t(StringId::HistoryPeerTitle) + L" · " + status;
        const RectF pr{x, y + theme_.space(2), inner_w, sl};
        g.text(Role::Secondary, fit_text(g, Role::Secondary, line, inner_w), pr, p.text_secondary, {Align::Leading, false, true});
        const Hit pid = seq.take();
        hits.add_text(pid, pr, line);
        hits.meta[pid].help = std::move(help);
        y = pr.bottom();
    }
    return y - body.y + kTopInset;
}

void Screen::history_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi (Gecmis): baslik seridinde sekme / cip yok; iki bolum alt alta, "Disa aktar" 02 bolum basliginin saginda
    // (tests_card). Gorunum cipleri kalkti: her zaman iki bolum.
    (void)g, (void)bar, (void)vm, (void)now, (void)hits;
    s.history_tabs = false;
}


bool history_filter_match(HistoryFilter f, std::string_view scope) {
    switch (f) {
        case HistoryFilter::All: return true;
        case HistoryFilter::Fixes: return scope == "fixes" || scope == "machine_fixes";
        case HistoryFilter::Startup: return scope == "startup";
        case HistoryFilter::Perf: return scope != "fixes" && scope != "machine_fixes" && scope != "startup";
    }
    return true;
}

std::vector<std::size_t> history_filter_rows(const cr::HistoryView& v, HistoryFilter f) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < v.rows.size(); ++i)
        if (history_filter_match(f, v.rows[i].scope)) out.push_back(i);
    return out;
}

}  // namespace ef::ui
