// UI-03 / P9-B5: Olcumler sayfasi MODELI — metin ve turetim (cizim yok). measure_page.cpp'den ayrildi; davranis degismedi.
// Olculmeyen deger "Olculemedi" + neden olarak yazilir; neden kodlari asla ham gosterilmez (M01); ic kimlik yok (M03).
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

namespace mp {

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

std::wstring date_text(std::int64_t unix_ms, bool turkish) {
    if (unix_ms <= 0) return L"";
    const std::time_t t = static_cast<std::time_t>(unix_ms / 1000);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return L"";
    wchar_t buf[40];
    if (turkish) swprintf_s(buf, L"%02d.%02d.%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    else swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

Color tone_color(const Palette& p, core::measure::StatusTone t) {
    switch (t) {
        case cm::StatusTone::Success: return p.success;
        case cm::StatusTone::Warning: return p.warning;
        case cm::StatusTone::Error: return p.error;
        case cm::StatusTone::Secondary: break;
    }
    return p.text_secondary;
}

// Onceki sonuc: yeni test surerken veya yeni test sonuc uretmeden bittiyse eski sonuc "Onceki sonuc" olarak kalir.
bool result_is_previous(const cm::MeasureState& m) {
    return cm::busy(m) || m.phase == cm::TestPhase::Cancelled || m.phase == cm::TestPhase::Failed;
}

}  // namespace mp

std::wstring Screen::bench_reason_text(std::string_view code) const {
    const auto rt = cm::reason_text(code);
    auto w = tk(rt.key);
    if (w.empty()) w = tk("bench.reason.unknown");
    if (!rt.n.empty()) w = replace_all(std::move(w), L"{n}", widen(rt.n));
    return w;
}

std::wstring Screen::component_status_text(const core::measure::ComponentCard& c) const {
    if (c.status.empty() || c.status == "completed") return {};
    std::wstring s = tk("bench.status." + c.status);
    if (s.empty()) return {};  // bilinmeyen durum: ham anahtar gosterilmez
    if (!c.reason.empty()) {
        std::wstring why = bench_reason_text(c.reason);
        // Bosluk 7: neden metnindeki {need}/{free} gibi yer tutucular kartin gercek parametreleriyle doldurulur.
        for (const auto& [k, v] : c.args) why = replace_all(std::move(why), L"{" + widen(k) + L"}", widen(v));
        s += L": " + why;
    }
    return s;
}

std::wstring Screen::result_headline(const core::measure::ResultView& r) const {
    // Denge sonucu: cozunurluk basina kisa hukum ("... — test sahnesi" kapsamiyla).
    if (r.balance) return balance_summary_text(*r.balance);
    if (!r.headline_key.empty()) {
        auto w = tk(r.headline_key);
        for (const auto& [k, v] : r.headline_args) w = replace_all(std::move(w), L"{" + widen(k) + L"}", widen(v));
        if (!w.empty()) return w;
    }
    return tk(cm::diagnosis_key(r.diagnosis));
}

std::wstring Screen::result_title(const core::measure::ResultView& r) const {
    if (r.balance) return t(StringId::MeasureScopeBalance);  // Uygulama Testi'nin kapsami; ic kimlik gosterilmez
    if (r.source == cm::TestSource::Builtin) {
        const std::string tier = r.tier.empty() ? cm::scene_tier_from_title(r.title) : r.tier;
        if (const auto key = cm::tier_key(tier); !key.empty()) return tk(key);
        return {};  // ic kimlik (scene_vN ...) gosterilmez
    }
    if (r.source == cm::TestSource::Endurance) return {};  // R13: baslik headline anahtarindan gelir
    return widen(r.title);
}

Screen::GameLimitUiTexts Screen::game_limit_ui_texts(const core::measure::GameLimitView& v) const {
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const auto texts = cm::game_limit_texts(v);
    const auto fill = [&](const cm::GameLimitLine& l) {
        std::wstring s = tk(l.key);
        if (l.pct) {
            const auto n = widen(cm::format_number(*l.pct, 0, tr));
            s = replace_all(std::move(s), L"{pct}", tr ? L"%" + n : n + L"%");
        }
        if (l.fps) s = replace_all(std::move(s), L"{fps}", widen(cm::format_number(*l.fps, 1, tr)));
        if (l.cap) s = replace_all(std::move(s), L"{cap}", widen(cm::format_number(*l.cap, 0, tr)));
        return s;
    };
    GameLimitUiTexts out;
    out.headline = fill(texts.headline);
    for (const auto& l : texts.lines)
        if (auto s = fill(l); !s.empty()) out.lines.push_back(std::move(s));
    for (const auto& k : texts.note_keys)
        if (auto s = tk(k); !s.empty()) out.notes.push_back(std::move(s));
    return out;
}

Screen::DropTexts Screen::drop_report_texts(const core::diagnosis::DropReport& r) const {
    namespace dg = core::diagnosis;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    DropTexts out;
    out.headline = tk("drop.headline." + std::string(dg::to_string(r.outcome)));
    if (out.headline.empty()) out.headline = tk("drop.headline.insufficient");
    auto label = [&](std::string_view key) { return tk(std::string(key)); };
    auto field = [&](std::wstring name, std::wstring value) {
        if (!name.empty()) out.fields.push_back(std::move(name) + L": " + std::move(value));
        else out.fields.push_back(std::move(value));
    };

    field(label("drop.label.valid_frames"), std::to_wstring(r.valid_frames));
    std::wstring windows = std::to_wstring(r.total_windows);
    if (r.windows_truncated) windows += L" +";
    field(label("drop.label.windows"), std::move(windows));
    if (r.outcome != dg::DropOutcome::Insufficient) {
        if (!r.worst.empty()) {
            double worst = 0;
            for (const auto& w : r.worst) worst = std::max(worst, w.worst_ms);
            field(label("drop.label.worst_ms"), widen(cm::format_number(worst, 1, tr)) + L" ms");
        } else if (r.outcome == dg::DropOutcome::DropsFound) {
            field(label("drop.label.worst_ms"), t(StringId::MetricMissing));
        }
        if (r.measured_ms > 0) {
            const auto pct = widen(cm::format_number(r.drop_time_ratio * 100.0, 1, tr));
            field(label("drop.label.ratio"), tr ? L"%" + pct : pct + L"%");
        }
    }

    const auto source = r.dominant ? dg::drop_source_key(*r.dominant)
                                   : r.outcome == dg::DropOutcome::NoDrops ? std::string_view("drop.source.no_drops")
                                                                           : std::string_view("drop.source.insufficient");
    field(label("drop.label.dominant"), label(source));
    if (r.confidence) field(label("drop.label.confidence"), label(dg::drop_confidence_key(*r.confidence)));

    // Eksik veri: Insufficient durumunda neyin eksik oldugu acikca yazilir (sessiz "yetersiz" yok).
    if (r.outcome == dg::DropOutcome::Insufficient) {
        std::vector<std::wstring> missing;
        auto add_missing = [&](std::string_view code) {
            const auto txt = tk(dg::drop_evidence_key(code));
            if (!txt.empty() && std::none_of(missing.begin(), missing.end(), [&](const auto& x) { return x == txt; })) missing.push_back(txt);
        };
        for (const auto& code : r.notes)
            if (code == dg::kEvFramesInsufficient || code == dg::kEvNoAlignedMetrics || code == dg::kEvMetricsMissing ||
                code == dg::kEvWindowsBelowMinimum)
                add_missing(code);
        std::wstring line = label("drop.label.missing");
        if (!line.empty()) line += L": ";
        if (missing.empty()) {
            line += out.headline;
        } else {
            for (std::size_t i = 0; i < missing.size(); ++i) line += (i ? L"; " : L"") + missing[i];
        }
        out.fields.push_back(std::move(line));
    }

    // Hizalanamayan telemetri kok neden sayilmadi; bu acikca yazilir.
    if (!r.unaligned_sources.empty()) {
        std::wstring line = label("drop.label.unaligned");
        const auto why = tk(dg::drop_evidence_key(dg::kEvUnalignedSourcesIgnored));
        if (!line.empty()) line += L": ";
        line += why;
        out.fields.push_back(std::move(line));
    }

    // Kosu duzeyi kanit kodlari yerellestirilir; bilinmeyen kod genel durust metne duser, ham kod gorunmez.
    auto add_evidence = [&](std::string_view code) {
        auto text = tk(dg::drop_evidence_key(code));
        if (text.empty()) text = tk("drop.evidence.unknown");
        if (!text.empty() && std::none_of(out.evidence.begin(), out.evidence.end(), [&](const auto& x) { return x == text; }))
            out.evidence.push_back(std::move(text));
    };
    for (const auto& code : r.supporting) add_evidence(code);
    for (const auto& code : r.counter) add_evidence(code);
    for (const auto& code : r.notes) add_evidence(code);
    return out;
}

std::wstring Screen::measure_failure_text(const core::measure::MeasureState& m) const {
    std::wstring cause = m.fail_reason.empty() ? t(StringId::MeasureFailed)
                                               : replace_all(t(StringId::MeasureFailedCause), L"{cause}", bench_reason_text(m.fail_reason));
    return cause + L" " + t(StringId::MeasureFailedNext);
}

std::wstring Screen::measure_stage_text(const core::measure::MeasureState& m) const {
    std::wstring name = m.stage_key.empty() ? std::wstring() : tk(m.stage_key);
    if (const auto pos = cm::stage_position(m.stage_key)) {
        auto of = replace_all(replace_all(t(StringId::MeasureStageOf), L"{n}", std::to_wstring(pos->first)), L"{t}",
                              std::to_wstring(pos->second));
        return name.empty() ? of : of + L" · " + name;
    }
    return name;
}

std::wstring Screen::measure_announcement_text(const core::measure::Announcement& a, const core::measure::MeasureState& m) const {
    switch (a.kind) {
        case cm::AnnounceKind::Started: return t(StringId::MeasureAnnounceStarted);
        case cm::AnnounceKind::Stage: {
            const auto pos = cm::stage_position(m.stage_key);
            if (!pos) return tk(m.stage_key);
            return replace_all(replace_all(replace_all(t(StringId::MeasureAnnounceStage), L"{n}", std::to_wstring(pos->first)), L"{t}",
                                           std::to_wstring(pos->second)),
                               L"{stage}", tk(m.stage_key));
        }
        case cm::AnnounceKind::Stopping: return t(StringId::MeasureStopping);
        case cm::AnnounceKind::Done:
            return replace_all(t(StringId::MeasureAnnounceDone), L"{headline}", m.result ? result_headline(*m.result) : std::wstring());
        case cm::AnnounceKind::Cancelled: return t(StringId::MeasureCancelled);
        case cm::AnnounceKind::Failed: return measure_failure_text(m);
    }
    return {};
}

}  // namespace ef::ui
