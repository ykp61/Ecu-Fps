// Spec 2026-11-12 B (D5): Oyunda Test "bu oyunda ne sinirladi" kurali (game_limit_v1) — kare siniri algisi sinirlari,
// GPU mesgul orani pencereleri, hukum sirasi (kare siniri once), HAGS notu, metin anahtarlari ve gecmis metrikleri.
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/diagnosis/game_limit.h"
#include "core/i18n/catalog.h"
#include "core/measure/game_limit_view.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
namespace cm = ef::core::measure;
namespace i18n = ef::core::i18n;

namespace {
// n kare, hepsi ayni aralik ve ayni GPU mesgul suresi.
GameLimitInput steady(std::size_t n, double interval_ms, double busy_ms) {
    GameLimitInput in;
    in.intervals_ms.assign(n, interval_ms);
    in.gpu_busy_ms.assign(n, busy_ms);
    in.avg_fps = 1000.0 / interval_ms;
    return in;
}
bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
}  // namespace

EF_TEST(game_limit_frame_cap_edges_at_three_percent) {
    // Yaygin sinirlar: tam %3 dahil, biraz disi degil (iki yonde).
    for (const double cap : kCommonFrameCapsFps) {
        EF_CHECK(detect_frame_cap(cap, std::nullopt).suspected);
        const auto hi = detect_frame_cap(cap * 1.03, std::nullopt);
        const auto lo = detect_frame_cap(cap * 0.97, std::nullopt);
        EF_CHECK(hi.suspected && near(hi.cap_fps, cap) && !hi.is_refresh);
        EF_CHECK(lo.suspected && near(lo.cap_fps, cap));
    }
    EF_CHECK(!detect_frame_cap(60.0 * 1.031, std::nullopt).suspected);  // 61,86
    EF_CHECK(!detect_frame_cap(60.0 * 0.969, std::nullopt).suspected);  // 58,14
    EF_CHECK(!detect_frame_cap(240.0 * 1.031, std::nullopt).suspected);
    EF_CHECK(!detect_frame_cap(30.0 * 0.969, std::nullopt).suspected);
    // Sinirlar arasindaki degerler eslesmez.
    for (const double fps : {45.0, 80.0, 100.0, 132.0, 155.0, 200.0, 300.0, 20.0})
        EF_CHECK(!detect_frame_cap(fps, std::nullopt).suspected);
    // Yenileme hizi yaygin listede olmasa da denenir (75 / 100 / 170 / 360 Hz).
    const auto r75 = detect_frame_cap(74.2, 75.0);
    EF_CHECK(r75.suspected && r75.is_refresh && near(r75.cap_fps, 75.0));
    EF_CHECK(detect_frame_cap(100.0 * 1.03, 100.0).suspected && !detect_frame_cap(100.0 * 1.031, 100.0).suspected);
    EF_CHECK(detect_frame_cap(352.0, 360.0).suspected && !detect_frame_cap(340.0, 360.0).suspected);
    // Yenileme hizi yaygin sinirla ayni: yenileme hizi bildirilir.
    const auto r144 = detect_frame_cap(143.0, 144.0);
    EF_CHECK(r144.suspected && r144.is_refresh && near(r144.cap_fps, 144.0));
    // Iki aday da tolerans icindeyse en yakin secilir (yenileme 59,94 Hz; ortalama 60,0 -> yaygin 60).
    const auto near60 = detect_frame_cap(60.0, 59.0);
    EF_CHECK(near60.suspected && !near60.is_refresh && near(near60.cap_fps, 60.0));
    // Gecersiz girdiler eslesme uretmez.
    EF_CHECK(!detect_frame_cap(std::nullopt, 60.0).suspected);
    EF_CHECK(!detect_frame_cap(0.0, 60.0).suspected && !detect_frame_cap(-60.0, 60.0).suspected);
    EF_CHECK(!detect_frame_cap(std::nan(""), 60.0).suspected);
    EF_CHECK(!detect_frame_cap(100.0, 0.0).suspected && !detect_frame_cap(100.0, std::nan("")).suspected);
}

EF_TEST(game_limit_busy_windows_sum_ratio_and_tail_merge) {
    // 10 ms kareler: 100 kare = 1 sn. 250 kare -> [0,1000) [1000,2000) + 500 ms kuyruk (>= yarim pencere: ayri).
    std::vector<double> iv(250, 10.0), busy(250, 9.5);
    auto w = gpu_busy_windows(iv, busy);
    EF_CHECK(w.size() == 3);
    EF_CHECK(w[0].frames == 100 && near(w[0].start_ms, 0.0) && near(w[0].end_ms, 1000.0));
    EF_CHECK(near(w[0].interval_ms, 1000.0) && near(w[0].busy_ms, 950.0) && near(w[0].ratio, 0.95));
    EF_CHECK(near(w[0].busy_per_frame_ms(), 9.5));
    EF_CHECK(w[2].frames == 50 && near(w[2].start_ms, 2000.0) && near(w[2].end_ms, 2500.0) && near(w[2].ratio, 0.95));
    // 240 kare -> kuyruk 400 ms (< yarim pencere): bir onceki pencereye eklenir.
    iv.assign(240, 10.0);
    busy.assign(240, 5.0);
    w = gpu_busy_windows(iv, busy);
    EF_CHECK(w.size() == 2 && w[1].frames == 140 && near(w[1].end_ms, 2400.0) && near(w[1].ratio, 0.5));
    // Oran pencere toplamidir: kuyruklu karelerde tek karenin 0,7 / 1,3 salinimi pencereyi bozmaz.
    iv.assign(200, 10.0);
    busy.clear();
    for (int i = 0; i < 200; ++i) busy.push_back(i % 2 ? 13.0 : 7.0);
    w = gpu_busy_windows(iv, busy);
    EF_CHECK(w.size() == 2 && near(w[0].ratio, 1.0) && near(w[1].ratio, 1.0));
    // Tek kisa seri: tek pencere (hukum agirligi asgari pencere kuraliyla sinirlanir).
    w = gpu_busy_windows({10.0, 10.0, 10.0}, {1.0, 2.0, 3.0});
    EF_CHECK(w.size() == 1 && w[0].frames == 3 && near(w[0].ratio, 0.2));
    // Hizasiz / bos seri: pencere yok. Gecersiz kare atlanir (0 uydurulmaz).
    EF_CHECK(gpu_busy_windows({10.0, 10.0}, {1.0}).empty());
    EF_CHECK(gpu_busy_windows({}, {}).empty());
    EF_CHECK(gpu_busy_windows({10.0}, {1.0}, 0.0).empty());
    w = gpu_busy_windows({10.0, -1.0, 10.0, std::nan(""), 10.0, 10.0}, {5.0, 5.0, -2.0, 5.0, std::nan(""), 5.0});
    EF_CHECK(w.size() == 1 && w[0].frames == 2 && near(w[0].interval_ms, 20.0) && near(w[0].busy_ms, 10.0));
}

EF_TEST(game_limit_gpu_full_and_gpu_waited_thresholds) {
    // 10 ms kare (100 FPS: yaygin sinirlarin %3 disinda). Oran 0,90 tam mesgul; 0,60 bekleme (ikisi de dahil).
    auto full = steady(2000, 10.0, 9.0);
    auto r = evaluate_game_limit(full);
    EF_CHECK(r.verdict == GameLimit::GpuFull && r.reason == GameLimitReason::None);
    EF_CHECK(r.frames == 2000 && r.windows == 20 && r.full_windows == 20 && r.waited_windows == 0);
    EF_CHECK(r.full_pct && near(*r.full_pct, 100.0) && r.waited_pct && near(*r.waited_pct, 0.0));
    EF_CHECK(r.busy_ratio && near(*r.busy_ratio, 0.9) && !r.cap_fps && !r.gpu_time_approximate);

    auto waited = steady(2000, 10.0, 6.0);
    r = evaluate_game_limit(waited);
    EF_CHECK(r.verdict == GameLimit::GpuWaited && r.waited_pct && near(*r.waited_pct, 100.0) && near(*r.full_pct, 0.0));

    // Arada (0,61 .. 0,89): ne tam mesgul ne bekleme -> Belirlenemedi (mixed); olculen paylar yine yazilir.
    for (const double busy : {6.1, 7.5, 8.9}) {
        r = evaluate_game_limit(steady(2000, 10.0, busy));
        EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::Mixed);
        EF_CHECK(r.full_pct && near(*r.full_pct, 0.0) && near(*r.waited_pct, 0.0));
    }
}

EF_TEST(game_limit_dominant_share_is_sixty_percent_of_frames) {
    // 10 pencere (her biri 100 kare x 10 ms... 100 FPS yaygin sinir degil). k pencere tam mesgul, kalan bekleme.
    auto make = [](int full_windows, int waited_windows, int mid_windows) {
        GameLimitInput in;
        auto add = [&in](int windows, double busy) {
            for (int i = 0; i < windows * 100; ++i) {
                in.intervals_ms.push_back(10.0);
                in.gpu_busy_ms.push_back(busy);
            }
        };
        add(full_windows, 9.6);
        add(waited_windows, 3.0);
        add(mid_windows, 7.5);
        in.avg_fps = 100.0;
        return in;
    };
    auto r = evaluate_game_limit(make(6, 4, 0));
    EF_CHECK(r.verdict == GameLimit::GpuFull && near(*r.full_pct, 60.0) && near(*r.waited_pct, 40.0) && r.windows == 10);
    r = evaluate_game_limit(make(5, 5, 0));
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::Mixed && near(*r.full_pct, 50.0));
    r = evaluate_game_limit(make(4, 6, 0));
    EF_CHECK(r.verdict == GameLimit::GpuWaited && near(*r.waited_pct, 60.0));
    r = evaluate_game_limit(make(5, 0, 5));
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::Mixed && near(*r.waited_pct, 0.0));
}

EF_TEST(game_limit_frame_cap_check_comes_first_and_abstains) {
    // 60 FPS'e kilitli oyun: GPU bekliyor gorunur ama hukum "kare siniri olabilir"dir; CPU'ya atfedilmez.
    auto capped = steady(1200, 1000.0 / 60.0, 6.0);
    auto r = evaluate_game_limit(capped);
    EF_CHECK(r.verdict == GameLimit::FrameCapSuspected && r.reason == GameLimitReason::None);
    EF_CHECK(r.cap_fps && near(*r.cap_fps, 60.0) && !r.cap_is_refresh);
    EF_CHECK(r.waited_pct && near(*r.waited_pct, 100.0));  // olculen pay yine tasinir
    // GPU dolu olsa da sinira yakin ortalama cekimser birakir.
    auto capped_full = steady(1200, 1000.0 / 60.0, 16.4);
    EF_CHECK(evaluate_game_limit(capped_full).verdict == GameLimit::FrameCapSuspected);
    // Yenileme hizi: 165 Hz ekranda 162 FPS.
    auto vrr = steady(3000, 1000.0 / 162.0, 5.9);
    vrr.refresh_hz = 165.0;
    r = evaluate_game_limit(vrr);
    EF_CHECK(r.verdict == GameLimit::FrameCapSuspected && r.cap_is_refresh && near(*r.cap_fps, 165.0));
    // GPU suresi olculmediyse de kare siniri suphesi bildirilir (FPS olculdu).
    auto no_gpu = steady(1200, 1000.0 / 60.0, 0.0);
    no_gpu.gpu_busy_ms.clear();
    r = evaluate_game_limit(no_gpu);
    EF_CHECK(r.verdict == GameLimit::FrameCapSuspected && !r.full_pct && !r.waited_pct && !r.busy_ratio);
}

EF_TEST(game_limit_undetermined_reasons_never_invent_a_verdict) {
    // GPU serisi yok.
    auto in = steady(2000, 10.0, 9.5);
    in.gpu_busy_ms.clear();
    auto r = evaluate_game_limit(in);
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::NoGpuTiming && !r.full_pct && r.frames == 0);
    // Hic GPU paketi gorulmedi (hepsi 0): "bekledi" hukmune cevrilmez.
    r = evaluate_game_limit(steady(2000, 10.0, 0.0));
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::NoGpuTiming && !r.waited_pct);
    // Hizasiz seri (boyut farkli) olculmemis sayilir.
    in = steady(2000, 10.0, 9.5);
    in.gpu_busy_ms.pop_back();
    EF_CHECK(evaluate_game_limit(in).reason == GameLimitReason::NoGpuTiming);
    // Az kare / ortalama FPS yok / az pencere.
    r = evaluate_game_limit(steady(kGameLimitMinFrames - 1, 10.0, 9.5));
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::TooFewFrames);
    in = steady(2000, 10.0, 9.5);
    in.avg_fps.reset();
    EF_CHECK(evaluate_game_limit(in).reason == GameLimitReason::TooFewFrames);
    r = evaluate_game_limit(steady(240, 10.0, 9.5));  // 2,4 sn: kuyruk bir oncekine eklenir -> 2 pencere
    EF_CHECK(r.windows == 2 && r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::TooFewFrames);
    EF_CHECK(evaluate_game_limit(steady(250, 10.0, 9.5)).verdict == GameLimit::GpuFull);  // 2,5 sn: 3 pencere
    // Gecersiz kosu: olculen paylar tasinir, hukum verilmez (kare siniri da soylenmez).
    in = steady(2000, 10.0, 9.5);
    in.run_valid = false;
    r = evaluate_game_limit(in);
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::RunInvalid && r.full_pct);
    in = steady(1200, 1000.0 / 60.0, 6.0);
    in.run_valid = false;
    EF_CHECK(evaluate_game_limit(in).reason == GameLimitReason::RunInvalid);
    EF_CHECK(to_string(GameLimit::GpuFull) == "gpu_full" && to_string(GameLimit::FrameCapSuspected) == "frame_cap");
    EF_CHECK(to_string(GameLimitReason::NoGpuTiming) == "no_gpu_timing" && to_string(GameLimitReason::Mixed) == "mixed");
    static_assert(kGameLimitRules == "game_limit_v2");  // Uyumluluk B4: VRR adaylari + en yuklu motor turu
}

EF_TEST(game_limit_hags_marks_gpu_time_approximate_only_when_measured) {
    auto in = steady(2000, 10.0, 9.5);
    in.hags_on = true;
    EF_CHECK(evaluate_game_limit(in).gpu_time_approximate);
    in.hags_on = false;
    EF_CHECK(!evaluate_game_limit(in).gpu_time_approximate);
    in.hags_on.reset();
    EF_CHECK(!evaluate_game_limit(in).gpu_time_approximate);
    in.hags_on = true;
    in.gpu_busy_ms.clear();  // GPU suresi yoksa not da yok
    EF_CHECK(!evaluate_game_limit(in).gpu_time_approximate);

    using S = HagsAdapterState;
    EF_CHECK(!hags_enabled_any({}).has_value());
    EF_CHECK(!hags_enabled_any({S{std::nullopt, std::nullopt}}).has_value());
    EF_CHECK(!hags_enabled_any({S{false, false}}).has_value());        // destekleyen yok
    EF_CHECK(!hags_enabled_any({S{true, std::nullopt}}).has_value());  // destek var, durum okunamadi
    EF_CHECK(hags_enabled_any({S{true, true}}) == std::optional<bool>(true));
    EF_CHECK(hags_enabled_any({S{true, false}}) == std::optional<bool>(false));
    EF_CHECK(hags_enabled_any({S{false, true}, S{true, false}}) == std::optional<bool>(false));  // desteklemeyen sayilmaz
    EF_CHECK(hags_enabled_any({S{true, false}, S{true, true}}) == std::optional<bool>(true));
}

EF_TEST(game_limit_texts_are_scoped_measured_and_cataloged) {
    auto key = [](const cm::GameLimitLine& l) { return l.key; };
    auto has_line = [](const cm::GameLimitTexts& t, std::string_view k) {
        for (const auto& l : t.lines)
            if (l.key == k) return true;
        return false;
    };
    auto has_note = [](const cm::GameLimitTexts& t, std::string_view k) {
        for (const auto& n : t.note_keys)
            if (n == k) return true;
        return false;
    };
    // GPU tam mesgul: baslikta olculen pay; ayni pay ikinci kez satir olmaz; bekleme payi ayri satir.
    cm::GameLimitView v;
    auto in = steady(2000, 10.0, 9.5);
    in.hags_on = true;
    v.result = evaluate_game_limit(in);
    v.game_gpu_pct = 96.5;
    auto t = cm::game_limit_texts(v);
    EF_CHECK(key(t.headline) == "game_limit.headline.gpu_full" && t.headline.pct && near(*t.headline.pct, 100.0));
    EF_CHECK(!has_line(t, "game_limit.line.gpu_full") && has_line(t, "game_limit.line.gpu_waited") && has_line(t, "game_limit.line.game_gpu"));
    EF_CHECK(has_note(t, "game_limit.note.hags") && has_note(t, "game_limit.note.scope") && !has_note(t, "game_limit.note.cap"));
    // GPU bekledi.
    v.result = evaluate_game_limit(steady(2000, 10.0, 3.0));
    v.game_gpu_pct.reset();
    t = cm::game_limit_texts(v);
    EF_CHECK(key(t.headline) == "game_limit.headline.gpu_waited" && near(*t.headline.pct, 100.0));
    EF_CHECK(has_line(t, "game_limit.line.gpu_full") && !has_line(t, "game_limit.line.gpu_waited") && !has_line(t, "game_limit.line.game_gpu"));
    EF_CHECK(!has_note(t, "game_limit.note.hags") && has_note(t, "game_limit.note.scope"));
    // Kare siniri: parca hukmu yok; ortalama FPS ve eslesen sinir yazilir.
    auto capped = steady(1200, 1000.0 / 60.0, 6.0);
    v.result = evaluate_game_limit(capped);
    t = cm::game_limit_texts(v);
    EF_CHECK(key(t.headline) == "game_limit.headline.frame_cap" && !t.headline.pct);
    EF_CHECK(!t.lines.empty() && t.lines[0].key == "game_limit.line.cap_common" && near(*t.lines[0].cap, 60.0) && near(*t.lines[0].fps, 60.0));
    EF_CHECK(has_note(t, "game_limit.note.cap"));
    capped.refresh_hz = 60.0;
    v.result = evaluate_game_limit(capped);
    EF_CHECK(cm::game_limit_texts(v).lines[0].key == "game_limit.line.cap_refresh");
    // Belirlenemedi: neden satiri; olculmeyen pay icin satir yok.
    auto none = steady(2000, 10.0, 9.5);
    none.gpu_busy_ms.clear();
    v.result = evaluate_game_limit(none);
    t = cm::game_limit_texts(v);
    EF_CHECK(key(t.headline) == "game_limit.headline.undetermined" && t.lines.size() == 1 && t.lines[0].key == "game_limit.reason.no_gpu_timing");
    // Olculen payi olmayan (elle bozulmus) hukum yazilmaz.
    cm::GameLimitView broken;
    broken.result.verdict = GameLimit::GpuFull;
    EF_CHECK(cm::game_limit_texts(broken).headline.key == "game_limit.headline.undetermined");

    // Butun anahtarlar katalogda; hukum metinleri "Bu oyunda, bu ayarlarda" kapsamini tasir (iki dilde yer tutucu ayni).
    const auto tr = i18n::Catalog::embedded(i18n::Locale::TrTR);
    const auto en = i18n::Catalog::embedded(i18n::Locale::EnUS);
    EF_CHECK(tr.ok() && en.ok());
    for (const char* k : {"game_limit.card.title", "game_limit.headline.gpu_full", "game_limit.headline.gpu_waited",
                          "game_limit.headline.frame_cap", "game_limit.headline.undetermined", "game_limit.line.cap_refresh",
                          "game_limit.line.cap_common", "game_limit.line.gpu_full", "game_limit.line.gpu_waited", "game_limit.line.game_gpu",
                          "game_limit.note.cap", "game_limit.note.hags", "game_limit.note.scope"})
        EF_CHECK(i18n::id_of(k).has_value());
    for (const auto reason : {GameLimitReason::RunInvalid, GameLimitReason::TooFewFrames, GameLimitReason::NoGpuTiming, GameLimitReason::Mixed,
                              GameLimitReason::Conflict})
        EF_CHECK(i18n::id_of("game_limit.reason." + std::string(to_string(reason))).has_value());
    for (const char* k : {"game_limit.headline.gpu_full", "game_limit.headline.gpu_waited", "game_limit.headline.frame_cap",
                          "game_limit.headline.undetermined"}) {
        const auto id = i18n::id_of(k);
        EF_CHECK(id.has_value());
        if (!id) continue;
        const std::string trs(tr.value().get(*id)), ens(en.value().get(*id));
        EF_CHECK(trs.rfind("Bu oyunda, bu ayarlarda:", 0) == 0);
        EF_CHECK(ens.rfind("In this game, at these settings:", 0) == 0);
        EF_CHECK((trs.find("{pct}") != std::string::npos) == (ens.find("{pct}") != std::string::npos));
    }
}

EF_TEST(game_limit_history_metrics_only_when_measured) {
    cm::GameLimitView v;
    v.result = evaluate_game_limit(steady(2000, 10.0, 9.5));
    v.game_gpu_pct = 97.0;
    auto m = cm::game_limit_metrics(v);
    EF_CHECK(m.size() == 4 && near(m.at("gpu_full_frame_pct"), 100.0) && near(m.at("gpu_waited_frame_pct"), 0.0));
    EF_CHECK(near(m.at("gpu_busy_ratio"), 0.95) && near(m.at("game_gpu_3d_pct"), 97.0));
    auto none = steady(2000, 10.0, 9.5);
    none.gpu_busy_ms.clear();
    v.result = evaluate_game_limit(none);
    v.game_gpu_pct.reset();
    EF_CHECK(cm::game_limit_metrics(v).empty());  // olculmeyen deger 0 yazilmaz
}

// Review D5: GPU suresi hukmu, oyun surecinin kendi 3D motor kullanimiyla (PDH) celisiyorsa hukum verilmez.
// Esikler classify_window ile aynidir: kGpuHighPct (>= %90 yuksek), kGpuLowPct (< %70 dusuk).
EF_TEST(game_limit_conflicting_process_gpu_load_abstains) {
    static_assert(kGpuHighPct == 90.0 && kGpuLowPct == 70.0);
    // "GPU bekledi" + oyunun 3D motoru >= %90 dolu: celiski. Sinir dahil; hemen alti hukmu bozmaz.
    auto waited = steady(2000, 10.0, 3.0);
    waited.game_gpu_pct = 90.0;
    auto r = evaluate_game_limit(waited);
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::Conflict);
    EF_CHECK(r.waited_pct && near(*r.waited_pct, 100.0) && r.busy_ratio && near(*r.busy_ratio, 0.3));  // olculenler tasinir
    waited.game_gpu_pct = 97.0;
    EF_CHECK(evaluate_game_limit(waited).reason == GameLimitReason::Conflict);
    waited.game_gpu_pct = 89.9;
    EF_CHECK(evaluate_game_limit(waited).verdict == GameLimit::GpuWaited);
    waited.game_gpu_pct = 35.0;
    EF_CHECK(evaluate_game_limit(waited).verdict == GameLimit::GpuWaited);
    // "GPU tam mesgul" + kosu geneli oran >= 0,90 iken oyunun 3D motoru < %70: celiski. %70 ve ustu hukmu bozmaz.
    auto full = steady(2000, 10.0, 9.5);
    full.game_gpu_pct = 69.9;
    r = evaluate_game_limit(full);
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::Conflict && r.full_pct && near(*r.full_pct, 100.0));
    full.game_gpu_pct = 70.0;
    EF_CHECK(evaluate_game_limit(full).verdict == GameLimit::GpuFull);
    full.game_gpu_pct = 98.0;
    EF_CHECK(evaluate_game_limit(full).verdict == GameLimit::GpuFull);
    // Kosu geneli oran tam 0,90 (sinir dahil) -> celiski; 0,89 -> degil (hukum zaten "mixed" olur: pencere orani < 0,90).
    auto edge = steady(2000, 10.0, 9.0);
    edge.game_gpu_pct = 40.0;
    EF_CHECK(evaluate_game_limit(edge).reason == GameLimitReason::Conflict);
    // Hukum karelerin %60'iyla verildiyse kosu ortalamasi dusuktur (0,6 x 0,96 + 0,4 x 0,30 = 0,696): %66 kullanim
    // bununla TUTARLIDIR, celiski sayilmaz.
    GameLimitInput split;
    for (int i = 0; i < 1000; ++i) {
        split.intervals_ms.push_back(10.0);
        split.gpu_busy_ms.push_back(i < 600 ? 9.6 : 3.0);
    }
    split.avg_fps = 100.0;
    split.game_gpu_pct = 66.0;
    r = evaluate_game_limit(split);
    EF_CHECK(r.verdict == GameLimit::GpuFull && near(*r.full_pct, 60.0) && near(*r.busy_ratio, 0.696));
    // Surec sayaci okunamadiysa (nullopt / sonlu degil) denetim yapilmaz: hukum degismez.
    full.game_gpu_pct.reset();
    EF_CHECK(evaluate_game_limit(full).verdict == GameLimit::GpuFull);
    waited.game_gpu_pct = std::nan("");
    EF_CHECK(evaluate_game_limit(waited).verdict == GameLimit::GpuWaited);
    // Kare siniri ve "baskin durum yok" onceliklidir; denetim yalniz parca hukmune uygulanir.
    auto capped = steady(1200, 1000.0 / 60.0, 6.0);
    capped.game_gpu_pct = 99.0;
    EF_CHECK(evaluate_game_limit(capped).verdict == GameLimit::FrameCapSuspected);
    auto mid = steady(2000, 10.0, 7.5);
    mid.game_gpu_pct = 99.0;
    EF_CHECK(evaluate_game_limit(mid).reason == GameLimitReason::Mixed);
    EF_CHECK(to_string(GameLimitReason::Conflict) == "conflict");

    // Metin: belirlenemedi basligi + celiski nedeni; olculen iki pay ve oyunun kendi GPU kullanimi satir olarak kalir.
    cm::GameLimitView v;
    waited.game_gpu_pct = 96.0;
    v.result = evaluate_game_limit(waited);
    v.game_gpu_pct = 96.0;
    const auto t = cm::game_limit_texts(v);
    EF_CHECK(t.headline.key == "game_limit.headline.undetermined" && !t.headline.pct);
    EF_CHECK(t.lines.size() == 4 && t.lines[0].key == "game_limit.reason.conflict" && t.lines[1].key == "game_limit.line.gpu_full" &&
              t.lines[2].key == "game_limit.line.gpu_waited" && t.lines[3].key == "game_limit.line.game_gpu");
}

// Review D5: hic GPU paketi gorulmeyen pencere "GPU bekledi" payina yazilmaz (izleme boslugu, olcum degil).
EF_TEST(game_limit_windows_without_any_gpu_packet_are_not_counted_as_waited) {
    // 10 pencere: 5'inde GPU gercekten bekledi (oran 0,30), 5'inde hic paket yok (0). Bekleme payi %50 -> hukum yok.
    GameLimitInput in;
    for (int i = 0; i < 1000; ++i) {
        in.intervals_ms.push_back(10.0);
        in.gpu_busy_ms.push_back(i < 500 ? 3.0 : 0.0);
    }
    in.avg_fps = 100.0;
    auto r = evaluate_game_limit(in);
    EF_CHECK(r.windows == 10 && r.waited_windows == 5 && r.full_windows == 0 && r.frames == 1000);
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::Mixed && near(*r.waited_pct, 50.0));
    // Bosluk azinliktaysa (10 pencerenin 3'u) olculen bekleme payi %70 ile hukum verilir.
    for (int i = 500; i < 700; ++i) in.gpu_busy_ms[static_cast<std::size_t>(i)] = 3.0;
    r = evaluate_game_limit(in);
    EF_CHECK(r.waited_windows == 7 && r.verdict == GameLimit::GpuWaited && near(*r.waited_pct, 70.0));
    // Pencerede tek bir paket bile varsa (oran > 0) olcum sayilir.
    for (int i = 700; i < 1000; i += 100) in.gpu_busy_ms[static_cast<std::size_t>(i)] = 0.5;
    r = evaluate_game_limit(in);
    EF_CHECK(r.waited_windows == 10 && near(*r.waited_pct, 100.0));
}

// Spec 2026-11-12 C3 (D6): oyun yuksek performansli ekran kartinda degilse not (masaustu: oneri, dizustu: bilgi).
EF_TEST(game_limit_adapter_note_is_advice_on_desktop_and_info_on_laptop) {
    const auto notes = [](const cm::GameLimitView& v) { return cm::game_limit_texts(v).note_keys; };
    const auto count = [](const std::vector<std::string>& n, std::string_view k) { return std::count(n.begin(), n.end(), k); };
    cm::GameLimitView v;
    v.result = evaluate_game_limit(steady(2000, 10.0, 3.0));
    // Varsayilan (ayni kart / bilinmiyor): not yok, metrik yok.
    auto n = notes(v);
    EF_CHECK(count(n, "game_limit.note.adapter_advice") == 0 && count(n, "game_limit.note.adapter_info") == 0);
    EF_CHECK(cm::game_limit_metrics(v).count("game_adapter_other") == 0);
    // Masaustu: oneri. Kapsam notu en sonda kalir.
    v.adapter_level = GameAdapterLevel::Advice;
    n = notes(v);
    EF_CHECK(count(n, "game_limit.note.adapter_advice") == 1 && count(n, "game_limit.note.adapter_info") == 0);
    EF_CHECK(!n.empty() && n.back() == "game_limit.note.scope");
    EF_CHECK(cm::game_limit_metrics(v).at("game_adapter_other") == 2.0);
    // Dizustu: bilgi.
    v.adapter_level = GameAdapterLevel::Info;
    n = notes(v);
    EF_CHECK(count(n, "game_limit.note.adapter_info") == 1 && count(n, "game_limit.note.adapter_advice") == 0);
    EF_CHECK(cm::game_limit_metrics(v).at("game_adapter_other") == 1.0);
    // Hukum verilemese de (kare siniri / belirlenemedi) olculen bagdastirici notu yazilir; hukmu degistirmez.
    v.result = evaluate_game_limit(steady(1200, 1000.0 / 60.0, 6.0));
    EF_CHECK(v.result.verdict == GameLimit::FrameCapSuspected);
    EF_CHECK(count(notes(v), "game_limit.note.adapter_info") == 1 && cm::game_limit_texts(v).headline.key == "game_limit.headline.frame_cap");
    // Metinler katalogda, iki dilde de "bu oyunda, bu olcumde" kapsamiyla; neden kesinlemesi yok.
    const auto tr = i18n::Catalog::embedded(i18n::Locale::TrTR);
    const auto en = i18n::Catalog::embedded(i18n::Locale::EnUS);
    EF_CHECK(tr.ok() && en.ok());
    for (const char* k : {"game_limit.note.adapter_advice", "game_limit.note.adapter_info"}) {
        const auto id = i18n::id_of(k);
        EF_CHECK(id.has_value());
        if (!id) continue;
        const std::string trs(tr.value().get(*id)), ens(en.value().get(*id));
        EF_CHECK(trs.rfind("Bu oyunda, bu \xC3\xB6l\xC3\xA7\xC3\xBCmde", 0) == 0);
        EF_CHECK(ens.rfind("In this game, in this measurement", 0) == 0);
    }
}

// Uyumluluk B4 (bosluk 14): VRR otomatik kare siniri adaylari (yenileme - yenileme^2/3600, yenileme - 3) yalniz dusuk
// kare araligi CV'si ile ve yenileme / yaygin sinir eslesmezse kabul edilir.
EF_TEST(game_limit_vrr_auto_cap_candidates_need_low_cv) {
    auto c = vrr_cap_candidates(144.0);
    EF_CHECK(c.size() == 2 && near(c[0], 138.24) && near(c[1], 141.0));
    c = vrr_cap_candidates(240.0);
    EF_CHECK(c.size() == 2 && near(c[0], 224.0) && near(c[1], 237.0));
    EF_CHECK(vrr_cap_candidates(0.0).empty() && vrr_cap_candidates(std::nan("")).empty());

    // 240 Hz + G-SYNC + Reflex: 224 FPS (yenileme 240'in %6,7 altinda, yaygin sinir degil).
    EF_CHECK(!detect_frame_cap(224.0, 240.0).suspected);                // CV bilinmiyor: eski davranis
    EF_CHECK(!detect_frame_cap(224.0, 240.0, 0.25).suspected);          // kararsiz akis: kabul edilmez
    auto m = detect_frame_cap(224.0, 240.0, 0.02);
    EF_CHECK(m.suspected && m.is_vrr && !m.is_refresh && near(m.cap_fps, 224.0));
    m = detect_frame_cap(224.0, 240.0, kLowVarianceCv);                  // esik dahil
    EF_CHECK(m.suspected && m.is_vrr);
    EF_CHECK(!detect_frame_cap(224.0, 240.0, kLowVarianceCv + 0.001).suspected);
    EF_CHECK(!detect_frame_cap(224.0, std::nullopt, 0.02).suspected);   // yenileme bilinmiyorsa aday yok
    // 3% siniri: 224 * 0,97 = 217,28 icerde; 216 disarda.
    EF_CHECK(detect_frame_cap(217.3, 240.0, 0.02).suspected && !detect_frame_cap(216.0, 240.0, 0.02).suspected);
    // Yenileme hizi eslesiyorsa o bildirilir (VRR adayi denenmez).
    m = detect_frame_cap(141.0, 144.0, 0.0);
    EF_CHECK(m.suspected && m.is_refresh && !m.is_vrr && near(m.cap_fps, 144.0));
    // 144 Hz: 138 FPS (yenilemenin %4,2 altinda) yalniz VRR adayi ile.
    EF_CHECK(!detect_frame_cap(138.0, 144.0).suspected);
    m = detect_frame_cap(138.0, 144.0, 0.01);
    EF_CHECK(m.suspected && m.is_vrr && near(m.cap_fps, 138.24));

    // Kural: sabit akista ve GPU beklerken hukum "kare siniri olabilir", degisken akista parca hukmu korunur.
    auto in = steady(3000, 1000.0 / 224.0, 1000.0 / 224.0 * 0.5);  // GPU sinirda bekliyor
    in.refresh_hz = 240.0;
    auto r = evaluate_game_limit(in);
    EF_CHECK(r.verdict == GameLimit::FrameCapSuspected && r.cap_is_vrr && !r.cap_is_refresh && r.cap_fps && near(*r.cap_fps, 224.0));
    // Inceleme B4: GPU tam mesgul (sabit akis, dusuk CV) -> VRR adayi GPU suresiyle yalanlanir, hukum GpuFull kalir.
    in = steady(3000, 1000.0 / 224.0, 1000.0 / 224.0 * 0.95);
    in.refresh_hz = 240.0;
    EF_CHECK(frame_interval_cv(in.intervals_ms).value_or(1) <= kLowVarianceCv);
    r = evaluate_game_limit(in);
    EF_CHECK(r.verdict == GameLimit::GpuFull && !r.cap_fps && !r.cap_is_vrr);
    // Ayni durum 144 / 165 / 60 Hz'de: 138 / 156 / 57 FPS GPU sinirli.
    for (const auto& [hz, fps] : {std::pair{144.0, 138.0}, std::pair{165.0, 156.0}, std::pair{60.0, 57.0}}) {
        auto g = steady(3000, 1000.0 / fps, 1000.0 / fps * 0.97);
        g.refresh_hz = hz;
        EF_CHECK(evaluate_game_limit(g).verdict == GameLimit::GpuFull);
    }
    // GPU suresi olculmedi ama oyunun GPU kullanimi >= %90: aday yine reddedilir (GPU zamanlamasi yok -> hukum yok).
    in = steady(3000, 1000.0 / 224.0, 0.0);
    in.gpu_busy_ms.clear();
    in.refresh_hz = 240.0;
    in.game_gpu_pct = 97.0;
    r = evaluate_game_limit(in);
    EF_CHECK(r.verdict == GameLimit::Undetermined && r.reason == GameLimitReason::NoGpuTiming && !r.cap_fps);
    in.game_gpu_pct = 55.0;
    EF_CHECK(evaluate_game_limit(in).verdict == GameLimit::FrameCapSuspected);
    // Yenileme hizi eslesmesi bu kapidan gecmez (onceki davranis): 240 FPS + GPU dolu -> kare siniri suphesi.
    in = steady(3000, 1000.0 / 240.0, 1000.0 / 240.0 * 0.95);
    in.refresh_hz = 240.0;
    r = evaluate_game_limit(in);
    EF_CHECK(r.verdict == GameLimit::FrameCapSuspected && r.cap_is_refresh);
    // Ayni ortalama, degisken kare araliklari (CV > 0,10): VRR adayi kabul edilmez.
    GameLimitInput noisy;
    for (int i = 0; i < 3000; ++i) {
        const double iv = i % 2 ? 1000.0 / 224.0 * 0.8 : 1000.0 / 224.0 * 1.2;
        noisy.intervals_ms.push_back(iv);
        noisy.gpu_busy_ms.push_back(iv * 0.95);
    }
    noisy.avg_fps = 224.0;
    noisy.refresh_hz = 240.0;
    EF_CHECK(frame_interval_cv(noisy.intervals_ms).value_or(0) > kLowVarianceCv);
    r = evaluate_game_limit(noisy);
    EF_CHECK(r.verdict == GameLimit::GpuFull && !r.cap_fps);
    // CV yardimcisi: sabit -> 0, < 2 aralik -> yok.
    EF_CHECK(near(frame_interval_cv(std::vector<double>(10, 5.0)).value_or(-1), 0.0));
    EF_CHECK(!frame_interval_cv({5.0}) && !frame_interval_cv({}));
}

EF_TEST(game_limit_vrr_cap_and_busiest_engine_texts) {
    cm::GameLimitView v;
    auto in = steady(3000, 1000.0 / 224.0, 2.0);
    in.refresh_hz = 240.0;
    v.result = evaluate_game_limit(in);
    auto t = cm::game_limit_texts(v);
    EF_CHECK(t.headline.key == "game_limit.headline.frame_cap" && !t.lines.empty() && t.lines[0].key == "game_limit.line.cap_vrr");
    EF_CHECK(i18n::id_of("game_limit.line.cap_vrr").has_value() && i18n::id_of("game_limit.line.game_gpu_max").has_value());
    // Uyumluluk B4 (bosluk 15): en yuklu motor turu birincil satir, 3D ikincil; metrikler ayri anahtarlarda.
    v.result = evaluate_game_limit(steady(2000, 10.0, 9.5));
    v.game_gpu_max_pct = 96.0;
    v.game_gpu_pct = 71.0;
    t = cm::game_limit_texts(v);
    std::size_t max_at = t.lines.size(), d3_at = t.lines.size();
    for (std::size_t i = 0; i < t.lines.size(); ++i) {
        if (t.lines[i].key == "game_limit.line.game_gpu_max") max_at = i;
        if (t.lines[i].key == "game_limit.line.game_gpu") d3_at = i;
    }
    EF_CHECK(max_at < d3_at && d3_at < t.lines.size() && near(*t.lines[max_at].pct, 96.0) && near(*t.lines[d3_at].pct, 71.0));
    const auto m = cm::game_limit_metrics(v);
    EF_CHECK(near(m.at("game_gpu_max_pct"), 96.0) && near(m.at("game_gpu_3d_pct"), 71.0));
    v.game_gpu_max_pct.reset();
    EF_CHECK(cm::game_limit_metrics(v).count("game_gpu_max_pct") == 0);
}
