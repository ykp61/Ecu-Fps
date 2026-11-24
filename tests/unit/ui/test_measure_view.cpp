#include <cmath>
#include <limits>
#include "core/i18n/catalog.h"
#include "core/measure/view.h"
#include "ef_test.h"

using namespace ef::core::measure;
using ef::contracts::Measurement;
using ef::contracts::MissingReason;
using ef::contracts::Unit;

EF_TEST(measure_scene_only_during_active_builtin_test) {
    MeasureState s;
    s.builtin_available = true;
    for (auto ph : {TestPhase::Idle, TestPhase::Preparing, TestPhase::Stopping, TestPhase::Done, TestPhase::Cancelled,
                    TestPhase::Failed}) {
        s.phase = ph;
        EF_CHECK(!scene_visible(s));
    }
    s.phase = TestPhase::Running;
    EF_CHECK(scene_visible(s));
    s.source = TestSource::InGame;  // Oyunda Test: kendi sahnemiz cizilmez
    EF_CHECK(!scene_visible(s));
    s.source = TestSource::Builtin;
    s.builtin_available = false;
    EF_CHECK(!scene_visible(s));
}

EF_TEST(measure_start_stop_rules) {
    MeasureState s;
    EF_CHECK(!can_start(s));  // kaynak bagli degil
    s.builtin_available = true;
    EF_CHECK(can_start(s) && !can_stop(s));
    s.phase = TestPhase::Running;
    EF_CHECK(!can_start(s) && can_stop(s));
    s.phase = TestPhase::Done;
    s.source = TestSource::InGame;
    EF_CHECK(!can_start(s));
    s.ingame_available = true;
    EF_CHECK(can_start(s));
}

EF_TEST(measure_progress_is_real_or_absent) {
    MeasureState s;
    s.phase = TestPhase::Running;
    s.total_s = 0;
    EF_CHECK(!progress_fraction(s));  // toplam bilinmiyor: sahte yuzde yok
    s.total_s = 60;
    s.elapsed_s = 15;
    EF_CHECK(progress_fraction(s) && *progress_fraction(s) == 0.25);
    s.elapsed_s = 90;
    EF_CHECK(*progress_fraction(s) == 1.0);
    s.phase = TestPhase::Preparing;
    EF_CHECK(!progress_fraction(s));
}

EF_TEST(measure_number_format_locale) {
    EF_CHECK(format_number(143.25, 1, true) == "143,3" || format_number(143.25, 1, true) == "143,2");
    EF_CHECK(format_number(143.2, 1, false) == "143.2");
    EF_CHECK(format_number(6.94, 2, true) == "6,94");
    EF_CHECK(format_number(1234, 0, true) == "1234");       // 4 hane gruplanmaz
    EF_CHECK(format_number(12345.5, 1, true) == "12.345,5");
    EF_CHECK(format_number(12345.5, 1, false) == "12,345.5");
    EF_CHECK(format_number(-0.001, 1, true) == "0,0");
    EF_CHECK(format_number(0.0 / 0.0 == 0 ? 1 : std::numeric_limits<double>::quiet_NaN(), 1, true) == "—");
}

EF_TEST(measure_ingame_result_view_carries_low_confidence) {
    // BM-01: gecersiz/dusuk guvenli Oyunda Test kosusu sonuc gorunumunde de gecerli gorunmez.
    const auto stats = compute_frame_stats(std::vector<double>(3600, 16.0));
    RunManifest m;
    m.source = "in_game";
    m.valid = true;
    auto r = ingame_result_view("Counter-Strike 2", m, stats, 123);
    EF_CHECK(r.source == TestSource::InGame && r.title == "Counter-Strike 2" && r.when_unix_ms == 123);
    EF_CHECK(r.run_valid && r.confidence == ef::contracts::Confidence::Medium);
    EF_CHECK(r.avg_fps.has_value() && r.low1_fps.has_value() && r.p99_ms.has_value() && r.evidence_keys.empty());
    // runtime_mixed: dusuk guven + katalog anahtari + gecersiz isareti.
    m.valid = false;
    m.exclusions = {Exclusion::RuntimeMixed, Exclusion::TooShort};
    r = ingame_result_view("Counter-Strike 2", m, stats, 123);
    EF_CHECK(!r.run_valid && r.confidence == ef::contracts::Confidence::Low);
    EF_CHECK(r.evidence_keys.size() == 2 && r.evidence_keys[0] == "bench.run.runtime_mixed" &&
              r.evidence_keys[1] == "bench.run.too_short");
    for (const auto& k : r.evidence_keys) EF_CHECK(ef::core::i18n::id_of(k).has_value());
    // Yetersiz kare: deger yok (0 degil) ve kosu gecersiz.
    const auto tiny = compute_frame_stats(std::vector<double>(10, 16.0));
    r = ingame_result_view("Game", m, tiny, 0);
    EF_CHECK(!r.avg_fps.has_value() && !r.low1_fps.has_value() && !r.p99_ms.has_value() && !r.run_valid);
}

EF_TEST(measure_ingame_unavailable_reason_is_localizable_and_blocks_start) {
    MeasureState s;
    s.source = TestSource::InGame;
    s.ingame_available = false;
    s.ingame_reason_key = "gate.capture_no_game";
    EF_CHECK(!can_start(s));
    EF_CHECK(ef::core::i18n::id_of(s.ingame_reason_key).has_value());
    s.ingame_available = true;
    s.ingame_reason_key.clear();
    EF_CHECK(can_start(s));
    s.phase = TestPhase::Running;
    EF_CHECK(!can_start(s) && can_stop(s));
}

EF_TEST(measure_missing_is_not_zero_and_has_reason_text) {
    const auto m = Measurement::missing(Unit::Fps, MissingReason::NotSupported);
    EF_CHECK(!m.has_value() && !m.value());
    for (auto r : {MissingReason::NotSupported, MissingReason::PermissionDenied, MissingReason::NotSampled,
                   MissingReason::SourceError, MissingReason::Invalid}) {
        const auto key = missing_key(r);
        EF_CHECK(!key.empty() && ef::core::i18n::id_of(key).has_value());
    }
    EF_CHECK(missing_key(MissingReason::None).empty());
    for (auto d : {Diagnosis::None, Diagnosis::Limited, Diagnosis::Mixed, Diagnosis::Insufficient})
        EF_CHECK(ef::core::i18n::id_of(diagnosis_key(d)).has_value());
    for (auto v : {Verdict::Improved, Verdict::Regressed, Verdict::Uncertain, Verdict::NoCohort})
        EF_CHECK(ef::core::i18n::id_of(verdict_key(v)).has_value());
    for (auto c : {ef::contracts::Confidence::Low, ef::contracts::Confidence::Medium, ef::contracts::Confidence::High})
        EF_CHECK(ef::core::i18n::id_of(confidence_key(c)).has_value());
    // Varsayilan sonuc: hicbir metrik dolu degil (yer tutucu sayi yok)
    ResultView r;
    EF_CHECK(!r.avg_fps.has_value() && !r.low1_fps.has_value() && !r.p99_ms.has_value());
    EF_CHECK(r.diagnosis == Diagnosis::Insufficient);
}

EF_TEST(measure_comparison_requires_same_conditions_and_version) {
    using ef::core::insights::HistoryEntry;
    HistoryEntry base, cur;
    base.source = cur.source = "builtin";
    base.fingerprint = cur.fingerprint = "scene_v3|medium|seed";
    base.app_version = cur.app_version = "0.1.0";
    base.scene_version = cur.scene_version = "scene_v3";
    base.protocol_version = cur.protocol_version = "bench_v1";
    base.run_valid = cur.run_valid = true;
    base.metrics["scene_avg_fps"] = 100.0;
    cur.metrics["scene_avg_fps"] = 110.0;
    auto c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && c.comparable);
    EF_CHECK(c.delta_percent && std::fabs(*c.delta_percent - 10.0) < 1e-9);
    EF_CHECK(c.verdict == Verdict::Uncertain);  // tek kosu: dogrulama iddiasi yok
    // Uygulama surumu farkli: metrik farki hesaplanmaz, neden anahtari verilir.
    cur.app_version = "0.2.0";
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && !c.comparable && !c.delta_percent);
    EF_CHECK(c.note_key == "comparison.version_mismatch");
    EF_CHECK(ef::core::i18n::id_of(c.note_key).has_value());
    // Surum bilinmiyorsa da ayni kural (kanitlanmis ortak surum yok).
    cur.app_version.clear();
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && !c.comparable);
    cur.app_version = "0.1.0";
    // B3: sahne/protokol surumu ve kosu gecerliligi de ortak politikadan gecer; fark uretilmez.
    cur.scene_version = "scene_v4";
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && !c.comparable && !c.delta_percent);
    EF_CHECK(c.note_key == "comparison.scene_mismatch");
    cur.scene_version = "scene_v3";
    cur.protocol_version = "bench_v2";
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && !c.comparable && !c.delta_percent);
    EF_CHECK(c.note_key == "comparison.protocol_mismatch");
    cur.protocol_version = "bench_v1";
    cur.run_valid = false;
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && !c.comparable && !c.delta_percent);
    EF_CHECK(c.note_key == "comparison.invalid_run");
    cur.run_valid = true;
    // Kaynak veya kosul izi bos/farkli: karsilastirma yok.
    cur.fingerprint.clear();
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(!c.available);
    cur.fingerprint = base.fingerprint;
    cur.source = "in_game";
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(!c.available);
    // Metrik iki tarafta yok: karsilastirma var, fark yok.
    cur.source = "builtin";
    cur.metrics.clear();
    c = measure_comparison(base, cur, "scene_avg_fps");
    EF_CHECK(c.available && c.comparable && !c.delta_percent);
    EF_CHECK(c.verdict == Verdict::Uncertain);
}

// B15 inceleme: yeniden acilista fark yalniz en yeni Uygulama Testi kaydi gosteriliyorsa ve ayni kosullu taban varsa.
EF_TEST(b15_reopen_comparison_only_for_newest_builtin) {
    using ef::core::insights::HistoryEntry;
    HistoryEntry newest, base;
    newest.source = base.source = "builtin";
    newest.fingerprint = base.fingerprint = "scene_v3|medium|seed";
    newest.app_version = base.app_version = "0.1.0";
    newest.scene_version = base.scene_version = "scene_v3";
    newest.protocol_version = base.protocol_version = "bench_v1";
    newest.run_valid = base.run_valid = true;
    newest.when_unix_ms = 20;
    base.when_unix_ms = 10;
    newest.metrics["scene_avg_fps"] = 90.0;
    base.metrics["scene_avg_fps"] = 100.0;
    ResultView shown;
    shown.when_unix_ms = 20;
    // 1) En yeni builtin + eslesen taban: fark test sonundakiyle ayni (%-10).
    auto c = reopen_comparison({newest, base}, &shown);
    EF_CHECK(c.available && c.comparable && c.delta_percent && std::fabs(*c.delta_percent + 10.0) < 1e-9);
    // 2) Taban yok (tek kayit): fark yok.
    c = reopen_comparison({newest}, &shown);
    EF_CHECK(!c.available && !c.delta_percent);
    // 3) En yeni kayit Oyunda Test: manifest yok, fark iddia edilmez (onceki builtin kayit olsa bile).
    HistoryEntry game = newest;
    game.source = "in_game";
    c = reopen_comparison({game, base}, &shown);
    EF_CHECK(!c.available && !c.delta_percent);
    // 4) Kosul izi farkli: taban degil; ayni iz ama surum farkli: karsilastirilamaz, fark yok.
    HistoryEntry other_fp = base;
    other_fp.fingerprint = "scene_v3|high|seed";
    c = reopen_comparison({newest, other_fp}, &shown);
    EF_CHECK(!c.available && !c.delta_percent);
    HistoryEntry old_ver = base;
    old_ver.app_version = "0.0.9";
    c = reopen_comparison({newest, old_ver}, &shown);
    EF_CHECK(c.available && !c.comparable && !c.delta_percent && c.note_key == "comparison.version_mismatch");
    // Gosterilen sonuc en yeni kayit degil (when_unix_ms farkli) veya sonuc yok: fark yok.
    shown.when_unix_ms = 19;
    EF_CHECK(!reopen_comparison({newest, base}, &shown).available);
    EF_CHECK(!reopen_comparison({newest, base}, nullptr).available);
    EF_CHECK(!reopen_comparison({}, nullptr).available);
}

// B10 (sahip istegi) + inceleme: envanter gelince sistem birimi (C:\) listede olsa da depolama testi "Yapma" kalir;
// kullanici secimi birim adiyla korunur, birim kaybolursa "Yapma"ya doner; liste kMaxStorageRoots ile sinirlidir.
EF_TEST(b10_storage_default_stays_skip_after_inventory) {
    MeasureState s;
    EF_CHECK(s.storage_choice == 0);
    adopt_storage_roots(s, {"C:\\", "D:\\"});
    EF_CHECK(s.storage_roots.size() == 2 && s.storage_roots[0] == "C:\\");
    EF_CHECK(s.storage_choice == 0);
    s.storage_choice = 2;  // kullanici D:\ secti
    adopt_storage_roots(s, {"D:\\", "C:\\", "E:\\"});
    EF_CHECK(s.storage_choice == 1 && s.storage_roots[0] == "D:\\");
    adopt_storage_roots(s, {"C:\\"});  // D:\ artik yok
    EF_CHECK(s.storage_choice == 0);
    std::vector<std::string> many;
    for (char c = 'C'; c <= 'Z'; ++c) many.push_back(std::string(1, c) + ":\\");
    adopt_storage_roots(s, many);
    EF_CHECK(s.storage_roots.size() == kMaxStorageRoots && s.storage_choice == 0);
}
