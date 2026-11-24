// Spec 2026-11-12 B (D5): Oyunda Test "bu oyunda ne sinirladi" karti metinleri — kapsamli hukum ("Bu oyunda, bu
// ayarlarda"), olculen sayi yerel bicimle, yer tutucu / ham anahtar kalmaz, olculmeyen deger icin satir yok.
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;
namespace cm = ef::core::measure;
namespace dg = ef::core::diagnosis;
namespace i18n = ef::core::i18n;

namespace {
bool contains(const std::wstring& s, std::wstring_view needle) { return s.find(needle) != std::wstring::npos; }
// Ekrana ham anahtar / yer tutucu sizmasin.
bool clean(const Screen::GameLimitUiTexts& t) {
    std::vector<std::wstring> all{t.headline};
    all.insert(all.end(), t.lines.begin(), t.lines.end());
    all.insert(all.end(), t.notes.begin(), t.notes.end());
    for (const auto& s : all)
        if (s.empty() || contains(s, L"{") || contains(s, L"}") || contains(s, L"game_limit.") || contains(s, L"_")) return false;
    return true;
}
cm::GameLimitView view(dg::GameLimit verdict, dg::GameLimitReason reason) {
    cm::GameLimitView v;
    v.result.verdict = verdict;
    v.result.reason = reason;
    v.result.frames = 6000;
    v.result.windows = 60;
    return v;
}
}  // namespace

EF_TEST(game_limit_ui_texts_are_scoped_and_localized) {
    const auto tr_cat = i18n::Catalog::embedded(i18n::Locale::TrTR);
    const auto en_cat = i18n::Catalog::embedded(i18n::Locale::EnUS);
    EF_CHECK(tr_cat.ok() && en_cat.ok());
    const auto& theme = default_theme();
    Screen tr(theme, tr_cat.value()), en(theme, en_cat.value());

    // GPU tam mesgul + HAGS: yuzde Turkce'de basta ("%92"), Ingilizce'de sonda ("92%").
    auto full = view(dg::GameLimit::GpuFull, dg::GameLimitReason::None);
    full.result.full_pct = 92.4;
    full.result.waited_pct = 3.0;
    full.result.busy_ratio = 0.95;
    full.result.gpu_time_approximate = true;
    full.game_gpu_pct = 97.2;
    auto t = tr.game_limit_ui_texts(full);
    EF_CHECK(clean(t));
    EF_CHECK(t.headline.rfind(L"Bu oyunda, bu ayarlarda:", 0) == 0 && contains(t.headline, L"%92") && contains(t.headline, L"GPU tam meşguldü"));
    EF_CHECK(t.lines.size() == 2 && contains(t.lines[0], L"%3") && contains(t.lines[1], L"%97"));
    EF_CHECK(t.notes.size() == 2 && contains(t.notes[0], L"yaklaşık"));
    auto e = en.game_limit_ui_texts(full);
    EF_CHECK(clean(e) && e.headline.rfind(L"In this game, at these settings:", 0) == 0 && contains(e.headline, L"92%"));

    // GPU bekledi: CPU kesin hukum degil, "olabilir".
    auto waited = view(dg::GameLimit::GpuWaited, dg::GameLimitReason::None);
    waited.result.full_pct = 6.0;
    waited.result.waited_pct = 81.0;
    t = tr.game_limit_ui_texts(waited);
    EF_CHECK(clean(t) && contains(t.headline, L"%81") && contains(t.headline, L"GPU bekledi") && contains(t.headline, L"olabilir"));
    EF_CHECK(t.lines.size() == 1 && contains(t.lines[0], L"%6") && t.notes.size() == 1);

    // Kare siniri: parca adi verilmez; ortalama FPS ondalikli (Turkce virgul), sinir tam sayi.
    auto cap = view(dg::GameLimit::FrameCapSuspected, dg::GameLimitReason::None);
    cap.result.avg_fps = 143.6;
    cap.result.cap_fps = 144.0;
    cap.result.cap_is_refresh = true;
    t = tr.game_limit_ui_texts(cap);
    EF_CHECK(clean(t) && contains(t.headline, L"kare sınırı olabilir"));
    EF_CHECK(!t.lines.empty() && contains(t.lines[0], L"143,6") && contains(t.lines[0], L"144 Hz"));
    EF_CHECK(t.notes.size() == 2);  // kare siniri notu + kapsam notu
    cap.result.cap_is_refresh = false;
    e = en.game_limit_ui_texts(cap);
    EF_CHECK(clean(e) && !e.lines.empty() && contains(e.lines[0], L"143.6") && contains(e.lines[0], L"144 FPS"));

    // Belirlenemedi: her neden kendi metniyle; olculmeyen pay icin satir yok.
    for (const auto reason : {dg::GameLimitReason::RunInvalid, dg::GameLimitReason::TooFewFrames, dg::GameLimitReason::NoGpuTiming,
                              dg::GameLimitReason::Mixed, dg::GameLimitReason::Conflict}) {
        const auto u = tr.game_limit_ui_texts(view(dg::GameLimit::Undetermined, reason));
        EF_CHECK(clean(u) && contains(u.headline, L"belirlenemedi") && u.lines.size() == 1 && u.notes.size() == 1);
        EF_CHECK(clean(en.game_limit_ui_texts(view(dg::GameLimit::Undetermined, reason))));
    }
}
