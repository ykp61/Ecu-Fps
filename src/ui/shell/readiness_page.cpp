// CHK (plan v3.5): "Oyun Hazirligi" sayfasi — tek yerde bulgu + neden + duzeltme.
// Bulgular saf kurallardan (core/readiness) gelir; metinler katalog anahtari + {parametre}. Seviye yalniz renkle
// degil metinle de yazilir (Sorun/Oneri/Iyi/Dogrulanamadi/Bilgi). Duzeltme dugmeleri:
//  - Duzelt (otomatik): Performans akisindaki gibi Evet/Hayir onayi ister (App'te), dogrudan yazmaz.
//  - Ayarlari ac / Sayfayi ac: Windows ayar sayfasi veya resmi uretici sayfasi (kullanici tiklar).
//  - Agi test et: kullanici baslatir; yalniz o an ICMP paketleri gonderilir.
// Yerlesim (BOOST maketi view-readiness, 2026-11-16 birebir): baslikta "03 //"; ustte skor karti (84 DIP halka + etiket,
// ozet, aciklama; sagda "Agi test et" ve "Yeniden tara"); altinda tek liste karti, her bulgu bir satir (nokta, ad, cumlenin
// tamami, seviye rozeti, dugmeler). Ekrandan kalkan metinler (kapsam ozeti, sayfa aciklamasi, alan ve seviye) UIA'da yasar.
// UI denetimi Track C (2026-11-05): metne yer kalmayan satirda dugmeler alta iner (RDY-03); Geri al onerilen, Koru sakin +
// uyari isaretli (RDY-04); is surerken dugmeler devre disi cizilir (RDY-09); son islem sonucu ilgili satirda da
// gorunur (RDY-08); UIA adi eylem + bulgu metni, durum HelpText'te (RDY-12).
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "core/readiness/fix_plan.h"
#include "core/service/uac_copy.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cr = core::readiness;

namespace {
std::wstring widen_r(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring fill_args(std::wstring s, const std::map<std::string, std::string>& args) {
    for (const auto& [k, v] : args) {
        const std::wstring key = L"{" + widen_r(k) + L"}";
        const std::wstring val = widen_r(v);
        for (std::size_t p = s.find(key); p != std::wstring::npos; p = s.find(key, p + val.size())) s.replace(p, key.size(), val);
    }
    return s;
}
StringId level_text(cr::Level l) {
    switch (l) {
        case cr::Level::Problem: return StringId::ChkLevelProblem;
        case cr::Level::Advice: return StringId::ChkLevelAdvice;
        case cr::Level::Good: return StringId::ChkLevelGood;
        case cr::Level::Info: return StringId::ChkLevelInfo;
        case cr::Level::Unknown: return StringId::ChkLevelUnknown;
    }
    return StringId::ChkLevelUnknown;
}
StringId section_text(cr::Section s) {
    switch (s) {
        case cr::Section::Display: return StringId::ChkSectionDisplay;
        case cr::Section::Graphics: return StringId::ChkSectionGraphics;
        case cr::Section::Latency: return StringId::ChkSectionLatency;
        case cr::Section::Windows: return StringId::ChkSectionWindows;
        case cr::Section::Hardware: return StringId::ChkSectionHardware;
        case cr::Section::Network: return StringId::ChkSectionNetwork;
        case cr::Section::Software: return StringId::ChkSectionSoftware;
    }
    return StringId::ChkSectionDisplay;
}
StringId action_text(cr::ActionKind a) {
    switch (a) {
        case cr::ActionKind::AutoFix: return StringId::ChkActionFix;
        case cr::ActionKind::OpenSettings: return StringId::ChkActionSettings;
        case cr::ActionKind::OpenUrl: return StringId::ChkActionUrl;
        case cr::ActionKind::RunNetworkTest: return StringId::ChkActionNetTest;
        case cr::ActionKind::IncidentWindow: return StringId::ChkActionIncident30;
        case cr::ActionKind::MeasureMouse: return StringId::ChkActionMouseMeasure;
        case cr::ActionKind::RunDnsBench: return StringId::ChkActionDnsBench;
        case cr::ActionKind::RunHealthTest: return StringId::ChkActionHealthTest;
        case cr::ActionKind::None: break;
    }
    return StringId::ChkActionFix;
}
constexpr wchar_t kWarningGlyph = 0xE7BA;  // Segoe Fluent Icons "Warning"
constexpr float kGlyphW = 16.f;  // DS-13: satir dugmesi simge kutusu (token degil)
}  // namespace

RowButtonStyle readiness_button_style(RowButton b, cr::ActionKind action) {
    switch (b) {
        case RowButton::Undo: return RowButtonStyle::Recommended;  // geri donus guvenli yol: onerilen
        case RowButton::Keep: return RowButtonStyle::Caution;      // geri donussuz: sakin + uyari isareti
        case RowButton::Action: break;
    }
    return action == cr::ActionKind::AutoFix ? RowButtonStyle::Recommended : RowButtonStyle::Neutral;
}

StringId readiness_undo_label(cr::UndoState u) {
    return u == cr::UndoState::Retry ? StringId::ChkActionRetryUndo : StringId::ChkActionUndo;
}

StringId readiness_action_label(const cr::Check& c) {
    // EF-X01: pencere dugmesi hedef pencereyi soyler ("30 gunu tara" / "7 gune don").
    if (c.action == cr::ActionKind::IncidentWindow) return c.action_arg == "7" ? StringId::ChkActionIncident7 : StringId::ChkActionIncident30;
    return action_text(c.action);
}

std::optional<StringId> readiness_undo_state_text(cr::UndoState u) {
    switch (u) {
        case cr::UndoState::Retry: return StringId::ChkUndoStateRecovery;
        case cr::UndoState::Interrupted: return StringId::ChkUndoStateInterrupted;
        case cr::UndoState::Conflict: return StringId::ChkUndoStateConflict;
        case cr::UndoState::None:
        case cr::UndoState::Undo: break;
    }
    return std::nullopt;
}

std::wstring readiness_notice_fill(std::wstring text, const ViewModel::ReadinessView& rv) {
    // P12-B1: toplu duzeltme sonucu "{n} ayar duzeltildi" / "{m} ayar duzeltilemedi".
    for (const auto& [key, val] : {std::pair<std::wstring_view, int>{L"{n}", rv.notice_n}, {L"{m}", rv.notice_failed}})
        for (auto p = text.find(key); p != std::wstring::npos; p = text.find(key, p)) text.replace(p, key.size(), std::to_wstring(val));
    return text;
}

std::optional<std::size_t> readiness_notice_row(const ViewModel::ReadinessView& rv, std::size_t rows) {
    if (rv.notice_key.empty() || rv.notice_check_id.empty()) return std::nullopt;
    for (std::size_t k = 0; k < rows && k < rv.checks.size(); ++k)
        if (rv.checks[k].id == rv.notice_check_id) return k;
    return std::nullopt;
}

ReadinessScore readiness_score(const ViewModel::ReadinessView& rv) {
    ReadinessScore r;
    for (const auto& c : rv.checks) {
        if (c.level == cr::Level::Good) ++r.good;
        else if (c.level == cr::Level::Advice || c.level == cr::Level::Problem) ++r.advice, r.problem += c.level == cr::Level::Problem ? 1 : 0;
        else if (c.level == cr::Level::Unknown) ++r.unmeasured;
    }
    r.total = r.good + r.advice + r.unmeasured;
    return r;
}

bool readiness_advice_group(cr::Level l) {
    return l == cr::Level::Problem || l == cr::Level::Advice || l == cr::Level::Unknown;
}

std::vector<std::size_t> readiness_display_order(const ViewModel::ReadinessView& rv, std::size_t rows) {
    rows = std::min(rows, rv.checks.size());
    std::vector<std::size_t> out;
    out.reserve(rows);
    // ONERILER: once Sorun/Oneri, sonra olculmeyenler; IYI DURUMDA: Iyi, sonra Bilgi. Grup icinde tarama sirasi.
    const auto rank = [](cr::Level l) {
        if (l == cr::Level::Problem || l == cr::Level::Advice) return 0;
        if (l == cr::Level::Unknown) return 1;
        if (l == cr::Level::Good) return 2;
        return 3;
    };
    for (int pass = 0; pass < 4; ++pass)
        for (std::size_t k = 0; k < rows; ++k)
            if (rank(rv.checks[k].level) == pass) out.push_back(k);
    return out;
}

ReadinessBand readiness_ring_band(int good, int total) {
    if (total <= 0 || good < 0) return ReadinessBand::None;
    if (good * 5 < total * 2) return ReadinessBand::Low;  // < %40
    if (good * 5 < total * 4) return ReadinessBand::Mid;  // < %80
    return ReadinessBand::High;
}

ReadinessSplit readiness_split(std::wstring_view text) {
    const auto trim = [](std::wstring_view v) {
        while (!v.empty() && v.front() == L' ') v.remove_prefix(1);
        while (!v.empty() && (v.back() == L' ' || v.back() == L'.')) v.remove_suffix(1);
        return std::wstring(v);
    };
    int depth = 0;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'(') ++depth;
        else if (ch == L')' && depth > 0) --depth;
        else if (depth == 0 && i > 0 && (ch == L'.' || ch == L';' || ch == L',') && text[i + 1] == L' ')
            return {trim(text.substr(0, i)), trim(text.substr(i + 1))};
    }
    return {trim(text), {}};
}

std::wstring readiness_reason(std::wstring_view text, std::wstring_view subject) {
    if (!subject.empty() && text.size() > subject.size() + 2 && text.substr(0, subject.size()) == subject &&
        text.substr(subject.size(), 2) == L": ")
        text.remove_prefix(subject.size() + 2);
    const auto sp = readiness_split(text);
    return sp.extra.empty() ? sp.title : sp.title + L" · " + sp.extra;
}

std::optional<StringId> readiness_undo_warning(cr::UndoState u) {
    switch (u) {
        case cr::UndoState::Retry: return StringId::ReadinessWarnRecovery;
        case cr::UndoState::Interrupted: return StringId::ReadinessWarnInterrupted;
        case cr::UndoState::Conflict: return StringId::ReadinessWarnConflict;
        case cr::UndoState::None:
        case cr::UndoState::Undo: break;
    }
    return std::nullopt;
}

std::vector<std::size_t> readiness_top_advice(const ViewModel::ReadinessView& rv, std::size_t max) {
    std::vector<std::size_t> out;
    for (const std::size_t k : readiness_display_order(rv, rv.checks.size())) {
        if (out.size() >= max) break;
        const auto l = rv.checks[k].level;
        if (l == cr::Level::Problem || l == cr::Level::Advice) out.push_back(k);
    }
    return out;
}

std::vector<ReadinessTile> readiness_tiles(const ViewModel::ReadinessView& rv, const std::vector<std::size_t>& order,
                                           const std::vector<bool>& loud) {
    std::vector<ReadinessTile> out;
    const auto is_loud = [&](std::size_t k) { return k < loud.size() && loud[k]; };
    for (const std::size_t k : order)
        if (k < rv.checks.size() && is_loud(k)) out.push_back({k, {}});
    // Sessiz (dugmesiz iyi / bilgi) bulgu: ayni alanin ilk kutucuguna katlanir; yoksa o alan icin yeni kutucuk.
    for (const std::size_t k : order) {
        if (k >= rv.checks.size() || is_loud(k)) continue;
        const auto it = std::find_if(out.begin(), out.end(), [&](const ReadinessTile& tl) { return rv.checks[tl.k].section == rv.checks[k].section; });
        if (it != out.end()) it->extra.push_back(k);
        else out.push_back({k, {}});
    }
    return out;
}

std::wstring Screen::check_name(const cr::Check& c) const { return t(section_text(c.section)); }

std::wstring Screen::check_text(const cr::Check& c) const {
    auto s = tk(c.text_key);
    if (s.empty()) s = widen_r(c.text_key);  // eksik metin gizlenmez (denetimde yakalanir)
    return fill_args(std::move(s), c.args);
}

namespace {
// BOOST maketi view-readiness (2026-11-16 birebir; Tailwind px = DIP). Skor karti: card-v2 p-4 (+1 kenar), flex gap-4,
// SVG halka (viewBox 88: r 38, cizgi 7, iz beyaz %8, yuvarlak uc), 84 DIP kutuda; etiket 11px/600 genis harf araligi,
// ozet 18px/600 (mt-0.5), cumle 12px ikincil (mt-0.5); sagda .btn-secondary "Agi test et" + kucuk .btn-primary
// "Yeniden tara" (gap-2). Liste karti: card-v2 p-3.5 (+1), space-y-2; satir well-v2 px-3.5 py-2.5 (+1), 13px: nokta 8,
// gap-2.5, ad 13px/500, aciklama 13px ikincil; sagda (gap-3) rozet 11px px-2 py-0.5 r4 beyaz %5 slate-300 ve dugmeler (gap-2).
constexpr float kScoreIn = 17.f, kScoreGap = 16.f, kRingView = 88.f, kRingR = 38.f, kRingStroke = 7.f, kTextGap = 2.f;
constexpr Color kRingTrack = depth::rgba(255, 255, 255, 0.08f), kBadgeFill = depth::rgba(255, 255, 255, 0.05f);
constexpr float kListIn = 15.f, kRowGapY = 8.f, kRowInX = 15.f, kRowInY = 11.f;
constexpr float kDotD = 8.f, kDotGap = 10.f, kGroupGap = 12.f, kBtnGap = 8.f, kBadgePadX = 8.f, kBadgePadY = 2.f, kBadgeR = 4.f;
constexpr float kCautionGlyph = 12.f, kCautionGlyphGap = 4.f, kQuietPadX = 12.f;

// Satir adi: bulgunun kisa adi (kararli bulgu kimliginden); bilinmeyen kimlikte bos (alan adi kullanilir).
std::optional<StringId> topic_of(const cr::Check& c) {
    const std::string_view id = c.id;
    const auto starts = [&](std::string_view p) { return id.starts_with(p); };
    const auto ends = [&](std::string_view p) { return id.ends_with(p); };
    if (starts("display.")) {
        if (ends(".refresh")) return StringId::ReadinessTopicRefresh;
        if (ends(".hdr")) return StringId::ReadinessTopicHdr;
        if (ends(".gpu")) return StringId::ReadinessTopicDisplayPath;
        if (ends("vrr") || ends("vrr_sync")) return StringId::ReadinessTopicVrr;
        return std::nullopt;
    }
    static constexpr std::pair<std::string_view, StringId> kExact[] = {
        {"windows.game_mode", StringId::ReadinessTopicGameMode},
        {"windows.game_dvr", StringId::FixNameGameDvr},
        {"windows.windowed_opt", StringId::FixNameWindowedOpt},
        {"windows.vrr", StringId::FixNameWindowsVrr},
        {"windows.hags", StringId::ReadinessTopicHags},
        {"windows.fast_startup", StringId::ReadinessTopicFastStartup},
        {"windows.vbs", StringId::ReadinessTopicVbs},
        {"windows.power", StringId::ReadinessTopicPower},
        {"cpu.speed_limit", StringId::ReadinessTopicCpuLimit},
        {"graphics.dx", StringId::ReadinessTopicDirectx},
        {"graphics.dx9", StringId::ReadinessTopicDx9},
        {"graphics.gsync", StringId::ReadinessTopicGsync},
        {"network.adapter", StringId::ReadinessTopicConnection},
        {"network.wifi.signal", StringId::ReadinessTopicWifiSignal},
        {"network.wifi.band", StringId::ReadinessTopicWifiBand},
        {"network.background", StringId::ReadinessTopicBackgroundNet},
        {"network.latency", StringId::ReadinessTopicLatency},
        {"network.gateway", StringId::ReadinessTopicGateway},
        {"network.internet", StringId::ReadinessTopicInternet},
        {"software.bios", StringId::ReadinessTopicBios},
        {"latency.mouse_polling", StringId::ReadinessTopicMousePolling},
    };
    if (starts("network.dns_bench")) return StringId::ReadinessTopicDnsBench;
    if (starts("hardware.health_test")) return StringId::ReadinessTopicHealthTest;
    for (const auto& [k, sid] : kExact)
        if (id == k) return sid;
    if (starts("graphics.driver.")) return StringId::ReadinessTopicGpuDriver;
    if (starts("software.vcredist.")) return StringId::ReadinessTopicVcredist;
    if (starts("software.dotnet.")) return StringId::ReadinessTopicDotnet;
    // Kimlik bilinmiyorsa metin anahtarindan (ayni bulgu ailesi).
    const std::string_view key = c.text_key;
    if (key.starts_with("chk.graphics.driver_")) return StringId::ReadinessTopicGpuDriver;
    if (key.starts_with("chk.win.vbs_") || key.starts_with("chk.win.hvci_")) return StringId::ReadinessTopicVbs;
    return std::nullopt;
}
// Cumlenin oznesi (ekran / ag bagdastiricisi adi): satir adinda parantez icinde yazilir, aciklamadan atilir.
std::wstring subject_of(const cr::Check& c) {
    for (const char* key : {"name", "adapter"})
        if (const auto it = c.args.find(key); it != c.args.end()) return widen_r(it->second);
    return {};
}
}  // namespace

float Screen::readiness_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin (pb-1) 16 DIP altinda baslatir. Bu sayfanin seridinde dugme
    // yok (yalniz 24 DIP baslik; Oyunlar'da 28 DIP dugmeler): serit maketlerde daha kisa, govde Edge cizimine gore 5 DIP iner.
    constexpr float kTopInset = 5.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    const auto& p = pal();
    const auto& rv = vm.readiness;
    const bool hc = pal_ != &theme_.dark;
    const float pad = theme_.card_pad;
    constexpr float kUnbounded = 10000.f;
    const auto one_w = [&](Role role, std::wstring_view text) {
        return text.empty() ? 0.f : std::ceil(g.measure(role, std::wstring(text), kUnbounded, false).w) + 1.f;
    };
    const auto line = [&](Role role) { return theme_.role(role).line; };
    Stack full{body, theme_.card_gap};
    game_banner(g, full, body, vm, s, now, hits);
    s.readiness_rows = std::min(rv.checks.size(), static_cast<std::size_t>(kMaxReadinessRows));
    s.readiness_actions.assign(s.readiness_rows, false);
    s.readiness_undos.assign(s.readiness_rows, false);
    s.readiness_keeps.assign(s.readiness_rows, false);
    // B11 "Ayrinti" kalkti: her bulgu kendi satirinda, cumlenin tamami ekranda (sigmazsa satir sarilir).
    s.readiness_details.assign(s.readiness_rows, false);
    s.readiness_order = readiness_display_order(rv, s.readiness_rows);
    s.readiness_busy = rv.loading || rv.net_running || rv.mouse_running || rv.dns_running || rv.health_running || rv.fix_running;
    s.readiness_rescan = true;
    s.readiness_net = rv.net_available;
    const bool enabled = !s.readiness_busy;  // RDY-09: is surerken dugmeler devre disi (kaybolmaz)
    StatusSeq seq{2};  // Status0 halka (skor), Status1 ozet satiri; sonrasi satir metinleri

    // Son duzeltme/geri alma sonucu (UAC reddi, hata, yeniden baslatma bekliyor ...): gizlenmez.
    std::wstring notice = rv.notice_key.empty() ? std::wstring() : tk(rv.notice_key);
    if (notice.empty() && !rv.notice_key.empty()) notice = widen_r(rv.notice_key);
    notice = readiness_notice_fill(std::move(notice), rv);
    const Color notice_color = rv.notice_warning ? p.warning : p.text_primary;
    const auto notice_row = notice.empty() ? std::nullopt : readiness_notice_row(rv, s.readiness_rows);

    // --- Skor karti: halka (skor "iyi/toplam" ortada; yay oranla dolar, renk oranla turuncu < %40 <= sari < %80 <= yesil),
    // etiket, ozet ("N iyi durumda · N oneri · N olculmedi"; sayilar yalniz gercek tarama bulgularindan, sonuc yokken "—"),
    // aciklama cumlesi ve sagda "Agi test et" (olculmus gecikme varsa "(N ms)") + "Yeniden tara". Kapsam ozeti ve sayfa
    // aciklamasi halkanin UIA HelpText'inde. ---
    const auto draw_score = [&](CardSlot& slot) {
        const auto score = readiness_score(rv);
        const bool has_score = !rv.loading && score.total > 0;  // tarama sonucu yoksa sayi uretilmez
        std::wstring big = has_score ? t(StringId::ReadinessScoreValue) : t(StringId::ReadinessScoreNone);
        if (has_score)
            for (auto [k, v] : std::array<std::pair<const wchar_t*, int>, 2>{{{L"{good}", score.good}, {L"{total}", score.total}}})
                for (std::size_t q = big.find(k); q != std::wstring::npos; q = big.find(k)) big.replace(q, wcslen(k), std::to_wstring(v));
        const auto with_n = [](std::wstring text, int n) {
            constexpr std::wstring_view key = L"{n}";
            for (std::size_t q = text.find(key); q != std::wstring::npos; q = text.find(key)) text.replace(q, key.size(), std::to_wstring(n));
            return text;
        };
        std::wstring summary;
        if (has_score) {
            const auto add = [&](StringId id, int n) { summary += (summary.empty() ? L"" : L" · ") + with_n(t(id), n); };
            add(StringId::ReadinessLineGoodState, score.good);
            if (score.problem > 0) add(StringId::ReadinessLineProblem, score.problem);
            add(StringId::ReadinessLineAdvice, score.advice - score.problem);
            add(StringId::ReadinessLineUnmeasured, score.unmeasured);
        } else {
            summary = t(rv.loading ? StringId::ChkScanning : StringId::ChkEmpty);
        }
        std::wstring running;
        if (rv.fix_running) running = t(core::service::admin_copy(StringId::ChkAdminRunning, vm.admin_without_uac));
        else if (rv.net_running) running = t(StringId::ChkNetRunning);
        const auto title = t(StringId::ReadinessScoreTitle);
        const auto note = t(StringId::ReadinessScoreNote);
        // Dugmeler: ag testi etiketi yalniz olculmus gecikmeyi tasir (bulgu parametresi "avg"; tahmin yok).
        const auto rescan_label = t(StringId::ChkRescan);
        std::wstring net_label = t(StringId::ChkActionNetTest);
        for (const auto& c : rv.checks)
            if (c.section == cr::Section::Network)
                if (const auto it = c.args.find("avg"); it != c.args.end() && !it->second.empty()) {
                    net_label += L" (" + fill_args(t(StringId::ReadinessTileMs), {{"ms", it->second}}) + L")";
                    break;
                }
        const RectF rb = panel_button_rect(g, rescan_label, true, false, true);
        const RectF nb = rv.net_available ? panel_button_rect(g, net_label, false) : RectF{};
        const float btn_w = rb.w + (rv.net_available ? nb.w + kBtnGap : 0.f);
        const float btn_h = std::max(rb.h, nb.h);
        const float d = kReadinessRingD;
        const float inner_w = std::max(slot.area.w - 2 * kScoreIn, 1.f);
        float text_w = inner_w - d - kScoreGap - kScoreGap - btn_w;
        const bool below = text_w < kReadinessMinTextW;  // flex-wrap: dugmeler alt satira (gap-4)
        if (below) text_w = std::max(inner_w - d - kScoreGap, 1.f);
        const float label_h = line(Role::Caps11);
        const float sum_h = g.measure(Role::Semi18, summary, text_w).h;
        const float note_h = g.measure(Role::Small12, note, text_w).h;
        const float run_h = running.empty() ? 0.f : g.measure(Role::Small12, running, text_w).h;
        const float block_h = label_h + kTextGap + sum_h + kTextGap + note_h + (run_h > 0 ? kTextGap + run_h : 0.f);
        const float top_h = std::max({d, block_h, below ? 0.f : btn_h});
        RectF card;
        if (!slot.place(2 * kScoreIn + top_h + (below ? kScoreGap + btn_h : 0.f), card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        const float x = card.x + kScoreIn, y = card.y + kScoreIn;
        const RectF ring{x, y + (top_h - d) / 2, d, d};
        const float tx = x + d + kScoreGap;
        float ty = y + (top_h - block_h) / 2;
        // EF-A11Y: skor ve ozet UIA'da salt okunur metin; kapsam ozeti + sayfa aciklamasi halkanin HelpText'inde.
        hits.add_text(hit_of(HitKind::Status, 0), ring, title + L" " + big);
        {
            const auto cov = cr::summarize_checks(rv.checks);
            std::wstring cover;
            if (!rv.loading) {
                cover = t(StringId::ChkSummaryCoverage);
                for (auto [k, v] : std::array<std::pair<const wchar_t*, std::size_t>, 5>{{{L"{total}", cov.total}, {L"{good}", cov.verified}, {L"{action}", cov.action}, {L"{unknown}", cov.unknown}, {L"{rules}", static_cast<std::size_t>(cr::kReadinessRulesVersion)}}})
                    for (std::size_t q = cover.find(k); q != std::wstring::npos; q = cover.find(k)) cover.replace(q, wcslen(k), std::to_wstring(v));
            }
            hits.meta[hit_of(HitKind::Status, 0)].help = a11y_join(cover, t(StringId::ChkIntro));
        }
        hits.add_text(hit_of(HitKind::Status, 1), {tx, ty + label_h + kTextGap, text_w, sum_h}, a11y_join(summary, running));
        // Yay: ilk gosterimde 0'dan oranina dolar. Sonuc yokken iz halkasi (sayi uretilmez).
        if (!has_score) {
            s.readiness_ring_seen = false;
        } else if (!s.readiness_ring_seen) {
            const int ms = effective_ms(theme_.motion.ring_fill, s.reduced_motion || s.motion_paused || vm.game_gate_blocked);
            s.readiness_ring = Tween(ms > 0 ? 0.f : 1.f);
            s.readiness_ring.retarget(1.f, now, ms);
            s.readiness_ring_seen = true;
        }
        const float k = d / kRingView;
        const float th = kRingStroke * k, rr = kRingR * k;
        const float ratio = has_score ? static_cast<float>(score.good) / static_cast<float>(score.total) : 0.f;
        const float grow = has_score ? std::clamp(s.readiness_ring.value(now, theme_.easing), 0.f, 1.f) : 0.f;
        Color fill = p.text_secondary;
        switch (readiness_ring_band(score.good, has_score ? score.total : 0)) {
            case ReadinessBand::Low: fill = p.orange; break;
            case ReadinessBand::Mid: fill = p.warning; break;
            case ReadinessBand::High: fill = p.accent; break;
            case ReadinessBand::None: break;
        }
        const float rcx = ring.x + d / 2, rcy = ring.y + d / 2;
        if (hc) {
            // Yuksek kontrast: iz dolu sistem cizgisi renginde olursa (beyaz) sari / camgobegi yay ondan ayirt edilemez
            // (kontrast < 3:1). Iz, zemin ustunde iki ince sistem cizgisidir; yay aralarinda zemine karsi okunur.
            constexpr float kEdge = 1.f;
            g.arc(rcx, rcy, rr + th / 2 - kEdge / 2, kEdge, 0.f, 360.f, p.border);
            g.arc(rcx, rcy, rr - th / 2 + kEdge / 2, kEdge, 0.f, 360.f, p.border);
            g.ring(rcx, rcy, rr, th - 2 * kEdge, ratio * grow, p.background, fill);
        } else {
            g.ring(rcx, rcy, rr, th, ratio * grow, kRingTrack, fill);
        }
        g.text(Role::Num18, big, ring, has_score ? p.text_primary : p.text_secondary, {Align::Center, false, true, true});
        g.text(Role::Caps11, title, {tx, ty, text_w, label_h}, p.text_secondary, {Align::Leading, false});
        ty += label_h + kTextGap;
        g.text(Role::Semi18, summary, {tx, ty, text_w, sum_h}, has_score ? p.text_primary : p.text_secondary);
        ty += sum_h + kTextGap;
        g.text(Role::Small12, note, {tx, ty, text_w, note_h}, p.text_secondary);
        ty += note_h + kTextGap;
        if (run_h > 0) g.text(Role::Small12, running, {tx, ty, text_w, run_h}, p.text_secondary);
        // Sira soldan saga (odak sirasi ayni): Agi test et | Yeniden tara.
        float bx = below ? x : card.right() - kScoreIn - btn_w;
        const float by = below ? y + top_h + kScoreGap : y + (top_h - btn_h) / 2;
        if (rv.net_available) bx += panel_button(g, bx, by + (btn_h - nb.h) / 2, Hit::ChkNetTest, net_label, false, s, now, hits, enabled) + kBtnGap;
        panel_button(g, bx, by + (btn_h - rb.h) / 2, Hit::ChkRescan, rescan_label, true, s, now, hits, enabled, false, true);
    };

    // --- Bulgu listesi: her bulgu tek satir (yuva): durum noktasi, ad (kisa ad + ozne), aciklama (bulgu cumlesinin tamami;
    // adin yaninda sigmazsa alt satirlara sarilir, kesilmez), sagda seviye rozeti ve mevcut dugmeler: Duzelt / Ac |
    // Geri al | (genis aralik) Mevcut degeri koru. Dugmelere metin yeri kalmazsa (RDY-03) dugmeler satirin altina iner.
    // Ozel durumlar (geri alma dogrulanamadi, yarim kaldi, deger disaridan degisti) tek kisa uyari satiri; son islem
    // bildirimi ilgili satirda da (RDY-08). ---
    using US = cr::UndoState;
    const auto undo_of = [&](std::size_t k) { return k < rv.undo.size() ? rv.undo[k] : US::None; };
    // Duzeltmesi zaten kayitli (ornek: HAGS yeniden baslatma bekliyor) bulguda ikinci "Duzelt" gosterilmez.
    const auto action_of = [&](std::size_t k) {
        const auto& c = rv.checks[k];
        // P12-B1: yalniz gercekten bir sey yapan eylem dugme olur (otomatik duzeltme, secimli DNS, istege bagli isler, testler);
        // Windows ayar sayfasi / web sayfasi acip birakan yonlendirmeler ve uygulamanin yazamadigi "Duzelt" gosterilmez.
        return cr::row_action_shown(c) && ((c.action != cr::ActionKind::RunNetworkTest && c.action != cr::ActionKind::RunDnsBench) || rv.net_available) &&
               !(undo_of(k) != US::None && c.action == cr::ActionKind::AutoFix);
    };
    const bool glyph = g.has_icon_font();
    const float gap_far = theme_.space(4);  // ... | Mevcut degeri koru (geri donussuz: onerilen eylemden uzak; RDY-04)
    enum class Face { Primary, Quiet, Caution };
    struct Btn {
        Hit id = Hit::None;
        std::wstring label;
        Face face = Face::Quiet;
        ButtonKind hc_kind = ButtonKind::Secondary;
        float w = 0;
    };
    struct Row {
        std::size_t k = 0;
        std::wstring name, desc, badge, warn, note, state, full, uia;
        Color dot;
        std::vector<Btn> btns;
        float bw = 0;
    };
    const auto btn_w_of = [&](const std::wstring& label, Face f) {
        if (f != Face::Caution) return panel_button_rect(g, label, f == Face::Primary, false, true).w;
        if (hc)
            return std::max(theme_.button_min_w,
                            std::ceil(g.measure(Role::Body, label, kUnbounded, false).w) + (glyph ? kGlyphW + theme_.space(0) : 0.f) + 2 * theme_.space(2));
        return one_w(Role::Medium12, label) + (glyph ? kCautionGlyph + kCautionGlyphGap : 0.f) + 2 * kQuietPadX;
    };
    std::vector<Row> rows;
    for (const std::size_t k : s.readiness_order) {
        const auto& c = rv.checks[k];
        const auto ust = undo_of(k);
        Row r;
        r.k = k;
        const auto subj = subject_of(c);
        const auto topic = topic_of(c);
        r.name = topic ? t(*topic) : check_name(c);
        if (!subj.empty()) r.name += L" (" + subj + L")";
        r.full = check_text(c);
        std::wstring_view desc = r.full;  // ozne ("DELL S2721DGF: ") aciklamadan atilir (adda yazilir)
        if (!subj.empty() && desc.size() > subj.size() + 2 && desc.starts_with(subj) && desc.substr(subj.size(), 2) == L": ")
            desc.remove_prefix(subj.size() + 2);
        r.desc = desc;
        const bool fixed = ust == US::Undo;
        r.badge = fixed ? t(StringId::ReadinessTileFixed)
                        : c.level == cr::Level::Unknown ? t(StringId::ReadinessCountUnmeasured) : t(level_text(c.level));
        switch (c.level) {
            case cr::Level::Problem: r.dot = p.error; break;
            case cr::Level::Advice: r.dot = p.warning; break;
            case cr::Level::Good: r.dot = p.accent; break;
            case cr::Level::Unknown:
            case cr::Level::Info: r.dot = p.text_secondary; break;
        }
        if (fixed) r.dot = p.accent;
        if (const auto sid = readiness_undo_state_text(ust)) r.state = t(*sid);
        if (const auto wid = readiness_undo_warning(ust)) r.warn = t(*wid);
        if (notice_row && *notice_row == k) r.note = notice;
        if (action_of(k)) {
            const auto style = readiness_button_style(RowButton::Action, c.action);
            const bool primary = readiness_action_primary(c.level);
            r.btns.push_back({hit_of(HitKind::ChkAction, k), t(readiness_action_label(c)), style == RowButtonStyle::Recommended ? Face::Primary : Face::Quiet,
                              primary ? ButtonKind::Primary : style == RowButtonStyle::Recommended ? ButtonKind::Recommended : ButtonKind::Secondary});
        }
        if (ust != US::None) r.btns.push_back({hit_of(HitKind::ChkUndo, k), t(readiness_undo_label(ust)), Face::Quiet, ButtonKind::Recommended});
        if (ust == US::Conflict) r.btns.push_back({hit_of(HitKind::ChkKeep, k), t(StringId::ChkActionKeep), Face::Caution, ButtonKind::Secondary});
        for (std::size_t i = 0; i < r.btns.size(); ++i) {
            auto& b = r.btns[i];
            b.w = btn_w_of(b.label, b.face);
            r.bw += b.w + (i == 0 ? 0.f : b.face == Face::Caution ? gap_far : kBtnGap);
        }
        // EF-A11Y: satirin salt okunur metni — seviye + alan + bulgu cumlesi (+ duzeltildi / uyari) + bildirim.
        r.uia = a11y_join(a11y_join(t(level_text(c.level)) + L" · " + check_name(c) + L": " + r.full, fixed ? t(StringId::ReadinessFixed) : r.warn), r.note);
        rows.push_back(std::move(r));
    }
    struct RowGeo {
        float avail = 0, name_w = 0, badge_w = 0, desc_h = 0, warn_h = 0, note_h = 0, content_h = 0, first_h = 0, h = 0;
        bool below = false, inline_desc = true;
    };
    const float name_line = line(Role::Medium13);
    const float badge_h = line(Role::Small11) + 2 * kBadgePadY;
    const float btn_h = theme_.button_row_h;
    const auto geo_of = [&](const Row& r, float w) {
        RowGeo q;
        const float cw = std::max(w - 2 * kRowInX, 1.f);
        q.badge_w = one_w(Role::Small11, r.badge) - 1.f + 2 * kBadgePadX;
        const float text_x = kDotD + kDotGap;
        q.avail = cw - text_x - kGroupGap - q.badge_w - (r.btns.empty() ? 0.f : kBtnGap + r.bw);
        q.below = readiness_buttons_below(q.avail, static_cast<int>(r.btns.size()));
        if (q.below) q.avail = cw - text_x - kGroupGap - q.badge_w;
        q.avail = std::max(q.avail, 1.f);
        q.name_w = std::min(one_w(Role::Medium13, r.name), q.avail);
        q.inline_desc = q.name_w + kDotGap + one_w(Role::Small13, r.desc) <= q.avail;
        q.desc_h = q.inline_desc ? 0.f : g.measure(Role::Small13, r.desc, q.avail).h;
        q.warn_h = r.warn.empty() ? 0.f : g.measure(Role::Small12, r.warn, q.avail).h;
        q.note_h = r.note.empty() ? 0.f : g.measure(Role::Small13, r.note, q.avail).h;
        q.content_h = name_line + q.desc_h + q.warn_h + q.note_h;
        q.first_h = std::max(q.content_h, (!q.below && !r.btns.empty()) ? btn_h : badge_h);
        q.h = 2 * kRowInY + q.first_h + (q.below ? kBtnGap + btn_h : 0.f);
        return q;
    };
    const auto mark_row = [&](std::size_t k, Hit id) {
        const auto kind = classify_hit(id).kind;
        if (kind == HitKind::ChkAction) s.readiness_actions[k] = true;
        else if (kind == HitKind::ChkUndo) s.readiness_undos[k] = true;
        else if (kind == HitKind::ChkKeep) s.readiness_keeps[k] = true;
    };
    const auto caution_face = [&](const RectF& rb, Hit id, const std::wstring& label) {
        // .btn-secondary + uyari: sari metin, sari %40 cizgi, uyari isareti (hover: sari %8 zemin, tam sari cizgi).
        const float hv = enabled ? hover_value(s, id, now) : 0.f;
        const float pv = enabled ? press_value(s, id, now) : 0.f;
        const float kk = std::min(1.f, hv + pv);
        const float rad = theme_.radius_control;
        g.fill_round(rb, rad, kk > 0.f ? mix(depth::kQuietFill, p.warning.with_alpha(0.08f + 0.08f * pv), kk) : depth::kQuietFill);
        g.stroke_round(rb.inset(0.5f), rad, enabled ? mix(p.warning.with_alpha(0.40f), p.warning, kk) : depth::kQuietBorder);
        const Color fg = enabled ? p.warning : p.text_secondary;
        const float lw = one_w(Role::Medium12, label);
        const float gw = glyph ? kCautionGlyph + kCautionGlyphGap : 0.f;
        const float x0 = rb.x + (rb.w - gw - lw) / 2;
        if (glyph) g.icon(kWarningGlyph, {x0, rb.y, kCautionGlyph, rb.h}, fg, kCautionGlyph);
        g.text(Role::Medium12, label, {x0 + gw, rb.y, lw, rb.h}, fg, {Align::Leading, false, true});
        if (s.focus_visible && s.focus == id) focus_ring(g, rb, rad, hits);
        hits.add(id, rb, label, AccRole::Button, false, -1, enabled);
    };
    const auto row_button = [&](const Row& r, const Btn& b, float bx, float by, bool visible) {
        const RectF rb{bx, by, b.w, btn_h};
        if (!visible) {
            hits.add(b.id, rb, b.label, AccRole::Button, false, -1, enabled);
        } else if (!hc && b.face != Face::Caution) {
            panel_button(g, bx, by, b.id, b.label, b.face == Face::Primary, s, now, hits, enabled, false, true, b.w);
        } else if (!hc) {
            caution_face(rb, b.id, b.label);
        } else {
            const bool cue = b.face == Face::Caution && glyph;
            const Color fg = button_face(g, rb, b.id, cue ? std::wstring_view{} : std::wstring_view{b.label}, b.hc_kind, enabled, s, now, hits);
            if (cue) {
                const float lw = std::ceil(g.measure(Role::Body, b.label, kUnbounded, false).w);
                const float x0 = rb.x + (rb.w - (kGlyphW + theme_.space(0) + lw)) / 2;
                g.icon(kWarningGlyph, {x0, rb.y, kGlyphW, rb.h}, enabled ? p.warning : p.text_secondary, 14.f);
                g.text(Role::Body, b.label, {x0 + kGlyphW + theme_.space(0), rb.y, lw + 1.f, rb.h}, fg, {Align::Leading, false, true});
            }
            hits.add(b.id, rb, b.label, AccRole::Button, false, -1, enabled);
        }
        // RDY-12: UIA adi eylem + bulgu cumlesi; durum HelpText'te.
        hits.meta[b.id].name = b.label + L": " + r.full;
        if (!r.state.empty()) hits.meta[b.id].help = r.state;
        mark_row(r.k, b.id);
    };
    const auto draw_list = [&](CardSlot& slot) {
        if (rows.empty()) return;
        const float iw = std::max(slot.area.w - 2 * kListIn, 1.f);
        std::vector<RowGeo> geos;
        float h = 2 * kListIn;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            geos.push_back(geo_of(rows[i], iw));
            h += geos.back().h + (i ? kRowGapY : 0.f);
        }
        RectF card;
        if (!slot.place(h, card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        float y = card.y + kListIn;
        const float x = card.x + kListIn;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            const RowGeo& q = geos[i];
            const RectF well{x, y, iw, q.h};
            // RDY-10: gorunur alan disindaki satir cizilmez, ama dugmeleri yine kaydedilir (Tab sirasi + UIA).
            const bool visible = well.bottom() >= s.view_top && well.y <= s.view_bottom;
            const float cx = x + kRowInX, cy = y + kRowInY, cw = iw - 2 * kRowInX;
            const float tx = cx + kDotD + kDotGap;
            const float ty = cy + (q.first_h - q.content_h) / 2;
            hits.add_text(seq.take(), {tx, ty, q.avail, q.content_h}, r.uia);
            if (visible) {
                ++s.rows_drawn;
                draw_well(g, well);
                g.fill_circle(cx + kDotD / 2, ty + name_line / 2, kDotD / 2, r.dot);
                g.text(Role::Medium13, fit_text(g, Role::Medium13, r.name, q.name_w), {tx, ty, q.name_w, name_line}, p.text_primary, {Align::Leading, false, true});
                float ly = ty + name_line;
                if (q.inline_desc) {
                    const float dx = tx + q.name_w - 1.f + kDotGap;
                    g.text(Role::Small13, r.desc, {dx, ty, std::max(tx + q.avail - dx, 1.f), name_line}, p.text_secondary, {Align::Leading, false, true});
                } else {
                    g.text(Role::Small13, r.desc, {tx, ly, q.avail, q.desc_h}, p.text_secondary);
                    ly += q.desc_h;
                }
                if (q.warn_h > 0) {  // zorunlu uyari: tek kisa satir
                    g.text(Role::Small12, r.warn, {tx, ly, q.avail, q.warn_h}, ensure_contrast(p.warning, p.surface, p.text_primary, 4.5));
                    ly += q.warn_h;
                }
                if (q.note_h > 0) g.text(Role::Small13, r.note, {tx, ly, q.avail, q.note_h}, notice_color);  // RDY-08
                // Rozet (seviye metni; renk tek basina anlam tasimaz).
                const float right_w = q.badge_w + (q.below || r.btns.empty() ? 0.f : kBtnGap + r.bw);
                const RectF badge{cx + cw - right_w, cy + (q.first_h - badge_h) / 2, q.badge_w, badge_h};
                if (hc) g.stroke_round(badge, kBadgeR, p.border);
                else g.fill_round(badge, kBadgeR, kBadgeFill);
                g.text(Role::Small11, r.badge, badge, hc ? p.text_primary : depth::kSlate300, {Align::Center, false, true});
            }
            float bx = q.below ? tx : cx + cw - r.bw;
            const float by = q.below ? cy + q.first_h + kBtnGap : cy + (q.first_h - btn_h) / 2;
            for (std::size_t j = 0; j < r.btns.size(); ++j) {
                if (j > 0) bx += r.btns[j].face == Face::Caution ? gap_far : kBtnGap;
                row_button(r, r.btns[j], bx, by, visible);
                bx += r.btns[j].w;
            }
            y += q.h + kRowGapY;
        }
    };
    // Son islem bildirimi (UAC reddi, hata, yeniden baslatma bekliyor ...): listenin ustunde tam genislik kart; ilgili
    // satirda da tekrarlanir (RDY-08).
    const auto draw_notice = [&](CardSlot& slot) {
        if (notice.empty()) return;
        const float inner = std::max(slot.area.w - 2 * pad, 1.f);
        const float nh = g.measure(Role::Body, notice, inner).h;
        RectF card;
        if (!slot.place(pad + nh + pad, card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        g.text(Role::Body, notice, {card.x + pad, card.y + pad, inner, nh}, notice_color);
    };
    CardFlow flow;
    flow.area = {body.x, body.y, body.w, 0};
    flow.gap = theme_.card_gap;
    flow.log = &hits.rows;
    flow.cursor = full.cursor;
    flow.full(draw_score);
    flow.full(draw_notice);
    flow.full(draw_list);
    // Spec 2026-11-12 B.4: Panel'den tasinan kartlar — Sistem kontrolu bulgulari (liste) ve tek tik kontrol (OPT-07;
    // asamaya gore liste). Ikisi de tam genislik; kimlikler, Evet/Hayir akisi ve oyun kilidi aynidir.
    flow.full([&](CardSlot& c) { findings_card(g, c, s, vm, now, hits, seq); });
    flow.full([&](CardSlot& c) { check_card(g, c, s, vm, now, hits); });
    return flow.used() + kTopInset;
}

}  // namespace ef::ui
