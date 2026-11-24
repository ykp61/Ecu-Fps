// MEAS-04: elle hesaplanmis kucuk veri setleri + gecersiz kosu kontrolu.
#include <cmath>
#include <algorithm>
#include <vector>

#include "core/measure/run_manifest.h"
#include "ef_test.h"

using namespace ef::core::measure;

EF_TEST(frame_stats_hand_computed_small_set) {
    // 100 aralik: 90 x 10 ms, 9 x 20 ms, 1 x 100 ms. Toplam 900+180+100 = 1180 ms.
    // avg = 100 / 1,18 = 84,7457...; %1 = ceil(1) en yavas = 100 ms -> 10 FPS.
    // p95: ceil(95) = 95. sira -> sirali dizide 91..99 = 20 -> 20 ms; p99: 99. sira = 20 ms.
    std::vector<double> v(90, 10.0);
    v.insert(v.end(), 9, 20.0);
    v.push_back(100.0);
    const auto s = compute_frame_stats(v);
    EF_CHECK(s.sufficient && std::fabs(*s.avg_fps - 100.0 / 1.18) < 1e-9);
    EF_CHECK(std::fabs(*s.low1_fps - 10.0) < 1e-12 && *s.p95_ms == 20.0 && *s.p99_ms == 20.0);
}

EF_TEST(run_manifest_invalid_runs_controlled) {
    FrameSummary ok;
    ok.validity = Validity::Valid;
    const auto good = compute_frame_stats(std::vector<double>(3600, 16.0));  // 57,6 s
    auto m = make_manifest("builtin", "ef_bench 1", {{"gpu", "rtx5080"}, {"os_build", "26300"}}, ok, good, 0, 30, false);
    EF_CHECK(m.valid && m.exclusions.empty() && m.intervals == 3600);
    // kayip olay
    auto lost = make_manifest("builtin", "ef_bench 1", {}, ok, good, 5, 30, false);
    EF_CHECK(!lost.valid && lost.exclusions.front() == Exclusion::EtwEventsLost);
    // cok kisa (sifir/NaN/negatif disinda yeterli N ama 10 s)
    const auto shortst = compute_frame_stats(std::vector<double>(600, 16.0));  // 9,6 s
    EF_CHECK(!make_manifest("builtin", "a", {}, ok, shortst, 0, 30, false).valid);
    // sifir/NaN/negatif: yetersiz N
    const auto bad = compute_frame_stats({0.0, -1.0, std::nan("")});
    const auto bm = make_manifest("builtin", "a", {}, ok, bad, 0, 30, false);
    EF_CHECK(!bm.valid && bm.excluded_intervals == 3 && to_string(bm.exclusions.front()) == "too_few_frames");
    EF_CHECK(!make_manifest("builtin", "a", {}, ok, good, 0, 30, true).valid);  // iptal
}

EF_TEST(run_manifest_low_confidence_runtime_is_not_valid) {
    // BM-01: runtime_mixed/runtime_other ozette dusuk guven uretir; manifestta dislama olarak gorunur ve
    // kosu gecerli SAYILMAZ (iyimser "gecerli" gorunum yok).
    const auto good = compute_frame_stats(std::vector<double>(3600, 16.0));
    FrameSummary mixed;
    mixed.validity = Validity::LowConfidence;
    mixed.reasons = {"runtime_mixed"};
    auto m = make_manifest("in_game", "game.exe 1.0", {}, mixed, good, 0, 30, false);
    EF_CHECK(!m.valid && m.exclusions.size() == 1 && m.exclusions[0] == Exclusion::RuntimeMixed);
    EF_CHECK(to_string(Exclusion::RuntimeMixed) == "runtime_mixed");
    FrameSummary other;
    other.validity = Validity::LowConfidence;
    other.reasons = {"runtime_other"};
    m = make_manifest("in_game", "game.exe 1.0", {}, other, good, 0, 30, false);
    EF_CHECK(!m.valid && m.exclusions.size() == 1 && m.exclusions[0] == Exclusion::RuntimeOther);
    EF_CHECK(to_string(Exclusion::RuntimeOther) == "runtime_other");
    // Dusuk guven ama dislama kodu yok: yine gecerli degil (validity == Valid sarti).
    FrameSummary low;
    low.validity = Validity::LowConfidence;
    EF_CHECK(!make_manifest("in_game", "a", {}, low, good, 0, 30, false).valid);
    // Tam gecerli kosu değişmedi.
    FrameSummary valid;
    valid.validity = Validity::Valid;
    EF_CHECK(make_manifest("in_game", "a", {}, valid, good, 0, 30, false).valid);
}

EF_TEST(run_manifest_fingerprint_same_condition) {
    RunManifest a, b;
    a.source = b.source = "in_game";
    a.app = b.app = "cs2.exe 1.40";
    a.context = {{"resolution", "2560x1440"}, {"vsync", "off"}};
    b.context = {{"vsync", "off"}, {"resolution", "2560x1440"}};  // sira farki onemsiz
    EF_CHECK(fingerprint(a) == fingerprint(b) && fingerprint(a).size() == 16);
    b.context["vsync"] = "on";
    EF_CHECK(fingerprint(a) != fingerprint(b));
}

// EF-X18-T02: kayip ETW olayi tam istatistige ragmen manifestoyu gecersiz kilar.
EF_TEST(x18_t02_lost_events_manifest_invalid_even_with_full_stats) {
    FrameSummary s;
    s.validity = Validity::Invalid;
    s.reasons = {"etw_events_lost"};
    const auto good = compute_frame_stats(std::vector<double>(3600, 16.0));
    const auto m = make_manifest("in_game", "game_1", {}, s, good, 3, 30, false);
    EF_CHECK(!m.valid && m.events_lost == 3);
    EF_CHECK(std::find(m.exclusions.begin(), m.exclusions.end(), Exclusion::EtwEventsLost) != m.exclusions.end());
}

// EF-X18-T04: oyun cikisi "tamamlandi" degildir; manifesto gecersiz ve ayri dislama.
EF_TEST(x18_t04_game_exit_is_not_complete) {
    EF_CHECK(classify_capture_outcome(false, false, true) == CaptureOutcome::Complete);
    EF_CHECK(classify_capture_outcome(false, true, true) == CaptureOutcome::GameExited);
    EF_CHECK(classify_capture_outcome(true, true, true) == CaptureOutcome::GameExited);
    EF_CHECK(classify_capture_outcome(false, true, false) == CaptureOutcome::GameExited);
    EF_CHECK(classify_capture_outcome(false, false, false) == CaptureOutcome::TargetChanged);
    EF_CHECK(classify_capture_outcome(true, false, true) == CaptureOutcome::Cancelled);
    EF_CHECK(to_string(CaptureOutcome::GameExited) == "game_exited" && to_string(CaptureOutcome::Complete) == "complete");
    FrameSummary ok;
    ok.validity = Validity::Valid;
    auto m = make_manifest("in_game", "game_1", {}, ok, compute_frame_stats(std::vector<double>(3600, 16.0)), 0, 30, false);
    EF_CHECK(m.valid);
    apply_capture_outcome(m, CaptureOutcome::Complete);
    EF_CHECK(m.valid);
    apply_capture_outcome(m, CaptureOutcome::GameExited);
    apply_capture_outcome(m, CaptureOutcome::GameExited);  // tekrarsiz
    EF_CHECK(!m.valid && m.exclusions.size() == 1 && m.exclusions[0] == Exclusion::GameExited);
    EF_CHECK(to_string(Exclusion::GameExited) == "game_exited");
    auto t = make_manifest("in_game", "game_1", {}, ok, compute_frame_stats(std::vector<double>(3600, 16.0)), 0, 30, false);
    apply_capture_outcome(t, CaptureOutcome::TargetChanged);
    EF_CHECK(!t.valid && t.exclusions.size() == 1 && t.exclusions[0] == Exclusion::TargetMismatch);
    auto n = make_manifest("in_game", "game_1", {}, ok, compute_frame_stats(std::vector<double>(3600, 16.0)), 0, 30, false);
    apply_capture_outcome(n, CaptureOutcome::NotStarted);
    EF_CHECK(!n.valid);  // baslamayan kosu asla gecerli degil
}

// EF-X18: gozlenemeyen baglam "unknown"; ayni-kosul iddiasi yalniz zorunlu alanlar bilinince.
EF_TEST(x18_run_context_unknown_when_not_observable) {
    RunContextInput in;
    in.game_build = "21345678";
    in.renderer = "d3d12";
    in.os_build = "26300.9711";
    const auto c = make_run_context(in);
    EF_CHECK(c.at("game_build") == "21345678" && c.at("renderer") == "d3d12" && c.at("os_build") == "26300.9711");
    for (const char* k : {"scene", "resolution", "render_scale", "fps_limit", "sync", "upscaler", "frame_gen", "gpu_driver",
                          "collector_commit", "api_confidence"})
        EF_CHECK(c.at(k) == "unknown");
    RunManifest m;
    m.context = c;
    EF_CHECK(!context_comparable(m));
    in.scene = "save_slot_3_bridge";
    in.resolution = "2560x1440";
    in.render_scale = "1.0";
    in.fps_limit = "none";
    in.sync = "off";
    in.upscaler = "off";
    in.frame_gen = "off";
    m.context = make_run_context(in);
    EF_CHECK(context_comparable(m));
    RunContextInput bad = in;
    bad.resolution = "2560x1440\n";  // kontrol karakteri: tahmin/temizlik yok, bilinmiyor
    EF_CHECK(make_run_context(bad).at("resolution") == "unknown");
    RunManifest a = m, b = m;
    b.context["game_build"] = "21345679";
    EF_CHECK(fingerprint(a) != fingerprint(b));  // oyun surumu ayni-kosul izine girer
}

// EF-X18-T03: acik/kapali eslesmis olcum yuku modeli; esik hukmu yok, yalniz olculen fark.
EF_TEST(x18_t03_overhead_pairs_data_model) {
    auto arm = [](bool on, double fps, double p99) {
        OverheadArm a;
        a.collector_on = on;
        a.source = OverheadSource::GameReported;
        a.condition_fingerprint = "fp1";
        a.run_valid = true;
        a.avg_fps = fps;
        a.p99_ms = p99;
        return a;
    };
    EF_CHECK(compare_overhead({}).verdict == OverheadVerdict::InsufficientPairs);
    std::vector<OverheadPair> p{{arm(true, 198, 6.1), arm(false, 200, 6.0)}};
    auto c = compare_overhead(p);
    EF_CHECK(c.verdict == OverheadVerdict::InsufficientPairs && c.reason == "too_few_pairs" && !c.median_avg_fps_delta_pct);
    p.push_back({arm(true, 99, 12.0), arm(false, 100, 11.0)});
    p.push_back({arm(true, 150, 8.0), arm(false, 150, 8.0)});
    c = compare_overhead(p);
    EF_CHECK(c.verdict == OverheadVerdict::Measured && c.pairs_used == 3);
    EF_CHECK(std::fabs(*c.median_avg_fps_delta_pct - (-1.0)) < 1e-9);
    EF_CHECK(std::fabs(*c.min_avg_fps_delta_pct - (-1.0)) < 1e-9 && std::fabs(*c.max_avg_fps_delta_pct) < 1e-9);
    EF_CHECK(c.median_p99_delta_ms && std::fabs(*c.median_p99_delta_ms - 0.1) < 1e-9);
    // Dogrulama bulgusu 4: gecersiz cift atilir (kume reddedilmez); 3 ciftten biri atilinca 2 kalir -> InsufficientPairs.
    auto discarded_as = [](const std::vector<OverheadPair>& v, const char* why) {
        const auto r = compare_overhead(v);
        return r.verdict == OverheadVerdict::InsufficientPairs && r.reason == "too_few_pairs" && r.pairs_discarded == 1 &&
               r.discard_reasons.size() == 1 && r.discard_reasons[0] == why && !r.median_avg_fps_delta_pct;
    };
    auto bad = p;
    bad[1].off.condition_fingerprint = "fp2";  // kollar arasi ayar degisti
    EF_CHECK(discarded_as(bad, "condition_mismatch"));
    bad = p;
    bad[0].off.collector_on = true;
    EF_CHECK(discarded_as(bad, "arm_order"));
    bad = p;
    bad[2].on.run_valid = false;
    EF_CHECK(discarded_as(bad, "pair_invalid"));
    bad = p;
    bad[0].off.source = OverheadSource::AppSceneInternal;
    EF_CHECK(discarded_as(bad, "source_mismatch"));
    bad = p;
    bad[0].off.avg_fps.reset();
    EF_CHECK(discarded_as(bad, "missing_fps"));
    bad = p;
    bad[0].off.avg_fps = 0.0;
    EF_CHECK(discarded_as(bad, "missing_fps"));
    bad = p;
    bad[1].on.p99_ms.reset();
    c = compare_overhead(bad);
    EF_CHECK(c.verdict == OverheadVerdict::Measured && !c.median_p99_delta_ms);  // eksik p99 uydurulmaz
}

namespace {
OverheadArm x18v_arm(bool on, double fps, double p99, const char* fp = "fp1", OverheadSource src = OverheadSource::GameReported) {
    OverheadArm a;
    a.collector_on = on;
    a.source = src;
    a.condition_fingerprint = fp;
    a.run_valid = true;
    a.avg_fps = fps;
    a.p99_ms = p99;
    return a;
}
}  // namespace

// EF-X18 dogrulama bulgusu 4: gecersiz cift filtrelenir, sonra kMinOverheadPairs uygulanir.
EF_TEST(x18v_overhead_discards_invalid_pair_then_applies_min_pairs) {
    std::vector<OverheadPair> p{{x18v_arm(true, 198, 6.1), x18v_arm(false, 200, 6.0)},
                                {x18v_arm(true, 99, 12.0), x18v_arm(false, 100, 11.0)},
                                {x18v_arm(true, 150, 8.0), x18v_arm(false, 150, 8.0)}};
    p.insert(p.begin() + 1, OverheadPair{x18v_arm(true, 50, 20.0), x18v_arm(false, 300, 3.0, "fp_changed")});  // atilacak
    const auto c = compare_overhead(p);
    EF_CHECK(c.verdict == OverheadVerdict::Measured && c.reason.empty());
    EF_CHECK(c.pairs_used == 3 && c.pairs_discarded == 1 && c.discard_reasons.size() == 1 && c.discard_reasons[0] == "condition_mismatch");
    // atilan cift (-%83) sonuca girmez
    EF_CHECK(std::fabs(*c.median_avg_fps_delta_pct - (-1.0)) < 1e-9 && std::fabs(*c.min_avg_fps_delta_pct - (-1.0)) < 1e-9);
    // iki gecersiz -> 2 gecerli -> InsufficientPairs (NotComparable degil)
    auto q = p;
    q[0].on.run_valid = false;
    const auto d = compare_overhead(q);
    EF_CHECK(d.verdict == OverheadVerdict::InsufficientPairs && d.reason == "too_few_pairs" && d.pairs_discarded == 2);
    EF_CHECK(d.discard_reasons.size() == 2 && d.discard_reasons[0] == "pair_invalid" && d.discard_reasons[1] == "condition_mismatch");
}

// Gecerli ciftler farkli sahnedeyse birlestirilmez; model kaynagi gercek hukum vermez; p99 <= 0 kullanilmaz.
EF_TEST(x18v_overhead_set_level_rejections_and_p99_positive) {
    std::vector<OverheadPair> p{{x18v_arm(true, 198, 6.1), x18v_arm(false, 200, 6.0)},
                                {x18v_arm(true, 99, 12.0), x18v_arm(false, 100, 11.0)},
                                {x18v_arm(true, 150, 8.0, "fp2"), x18v_arm(false, 150, 8.0, "fp2")}};
    auto c = compare_overhead(p);
    EF_CHECK(c.verdict == OverheadVerdict::NotComparable && c.reason == "condition_mismatch" && !c.median_avg_fps_delta_pct);
    p[2] = {x18v_arm(true, 150, 8.0), x18v_arm(false, 150, 8.0)};
    auto mixed = p;
    mixed[2] = {x18v_arm(true, 150, 8.0, "fp1", OverheadSource::AppSceneInternal), x18v_arm(false, 150, 8.0, "fp1", OverheadSource::AppSceneInternal)};
    c = compare_overhead(mixed);
    EF_CHECK(c.verdict == OverheadVerdict::NotComparable && c.reason == "source_mismatch");
    std::vector<OverheadPair> model;
    for (int i = 0; i < 3; ++i)
        model.push_back({x18v_arm(true, 100, 10.0, "fp1", OverheadSource::AppSceneInternal),
                         x18v_arm(false, 101, 10.0, "fp1", OverheadSource::AppSceneInternal)});
    c = compare_overhead(model);
    EF_CHECK(c.verdict == OverheadVerdict::NotComparable && c.reason == "model_only_source" && !c.median_avg_fps_delta_pct);
    auto zero = p;
    zero[0].off.p99_ms = 0.0;
    c = compare_overhead(zero);
    EF_CHECK(c.verdict == OverheadVerdict::Measured && !c.median_p99_delta_ms);
    zero = p;
    zero[1].on.p99_ms = -1.0;
    c = compare_overhead(zero);
    EF_CHECK(c.verdict == OverheadVerdict::Measured && !c.median_p99_delta_ms);
    c = compare_overhead(p);
    EF_CHECK(c.verdict == OverheadVerdict::Measured && c.median_p99_delta_ms);
}

// EF-X18 dogrulama bulgusu 2: tam pencereden sonraki cikis GameExited degildir.
EF_TEST(x18v_exit_after_full_window_is_not_game_exited) {
    // dongu suresi doldu (60 >= 60), oyun durdurma sirasinda kapandi
    auto o = resolve_target_exit(std::nullopt, true, 60.05, 60.0, 60.4);
    EF_CHECK(!o.early_exit_s && o.exited_after_window);
    EF_CHECK(classify_capture_outcome(false, o.early_exit_s.has_value(), true) == CaptureOutcome::Complete);
    // dongu icinde, pencere icinde cikis -> erken
    o = resolve_target_exit(15.2, true, 15.2, 60.0, 15.5);
    EF_CHECK(o.early_exit_s && *o.early_exit_s == 15.2 && !o.exited_after_window);
    EF_CHECK(classify_capture_outcome(false, true, true) == CaptureOutcome::GameExited);
    // dongu icinde ama pencere dolduktan sonra gozlendi -> bilgi
    o = resolve_target_exit(60.02, true, 60.02, 60.0, 60.3);
    EF_CHECK(!o.early_exit_s && o.exited_after_window);
    // iptal ile erken biten dongu + durdurmada cikis -> erken cikis (tamamlanmadi)
    o = resolve_target_exit(std::nullopt, true, 20.0, 60.0, 20.3);
    EF_CHECK(o.early_exit_s && *o.early_exit_s == 20.3 && !o.exited_after_window);
    // cikis yok
    o = resolve_target_exit(std::nullopt, false, 60.1, 60.0, 60.2);
    EF_CHECK(!o.early_exit_s && !o.exited_after_window);
}

// EF-X18 dogrulama bulgusu 3: sayaclar ayri yazilir; dropped toplamda bir kez sayilir.
EF_TEST(x18v_capture_counters_are_separate) {
    CaptureLossCounters c;
    c.events_lost = 2;
    c.buffers_lost = 3;
    c.overflowed_presents = 5;
    c.dropped_presents = 7;
    EF_CHECK(stream_events_lost(c) == 14 && manifest_loss_total(c) == 17);
    EF_CHECK(capture_counters_line("0123456789ab", c) ==
              "build_id=0123456789ab events_lost=2 buffers_lost=3 overflowed=5 dropped=7 manifest_loss_total=17");
    EF_CHECK(capture_counters_line("bad id", c).rfind("build_id=unknown ", 0) == 0);
    EF_CHECK(capture_counters_line("", c).rfind("build_id=unknown ", 0) == 0);
    CaptureLossCounters big;
    big.dropped_presents = 0x1'0000'0000ull;
    big.buffers_lost = 1;
    EF_CHECK(stream_events_lost(big) == 0xFFFFFFFFu && manifest_loss_total(big) == 0xFFFFFFFFu);  // doyar, sarmaz
    // manifesto: yalniz tampon kaybi da kosuyu gecersiz kilar
    FrameSummary ok;
    ok.validity = Validity::Valid;
    CaptureLossCounters only_buf;
    only_buf.buffers_lost = 1;
    const auto m = make_manifest("in_game", "game_1", {}, ok, compute_frame_stats(std::vector<double>(3600, 16.0)), manifest_loss_total(only_buf),
                                 30, false);
    EF_CHECK(!m.valid && m.events_lost == 1);
}
