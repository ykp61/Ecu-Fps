// UI denetimi Track B (Test merkezi dürüstlük/geri bildirim): neden kodu kataloğu (M01), Uygulama Testi başlığı (M02),
// iç kimlik (M03), aşama/geçen süre (M04), meşgul durum (M05/M07), duyurular (M06), durum metinleri/renkleri (M10),
// hata metni (M11), not tekrarları (M09).
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "benchmark/storage/storage_rules.h"
#include "core/i18n/catalog.h"
#include "core/measure/bench_cards.h"
#include "core/measure/bench_text.h"
#include "core/measure/view.h"
#include "ef_test.h"
#include "ui/shell/screen.h"
#include "ui/theme/theme.h"

using namespace ef::core::measure;
namespace i18n = ef::core::i18n;

namespace {

struct Literal {
    std::string file, text;
    bool prefix = false;  // hemen ardindan '+' geliyor: onek ailesi
    int line = 0;
};

// Neden kodu ureten satirlar: "reason", "err =", ".error =" veya "finish(" iceren satirlardaki dize sabitleri.
std::vector<Literal> scan_reason_literals(const std::string& rel) {
    std::vector<Literal> out;
    std::ifstream f(std::string(EF_SOURCE_ROOT) + "/" + rel);
    EF_CHECK(f.good());
    std::string line;
    int no = 0;
    while (std::getline(f, line)) {
        ++no;
        // Yorum: tirnak disindaki ilk "//" sonrasi atilir.
        bool in_str = false;
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {
            if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) in_str = !in_str;
            if (!in_str && line[i] == '/' && line[i + 1] == '/') {
                line.resize(i);
                break;
            }
        }
        const bool relevant = line.find("reason") != std::string::npos || line.find("err = ") != std::string::npos ||
                              line.find(".error = ") != std::string::npos || line.find("finish(") != std::string::npos;
        if (!relevant) continue;
        for (std::size_t p = line.find('"'); p != std::string::npos;) {
            const auto e = line.find('"', p + 1);
            if (e == std::string::npos) break;
            Literal l{rel, line.substr(p + 1, e - p - 1), false, no};
            std::size_t k = e + 1;
            if (k < line.size() && line[k] == ')') ++k;  // std::string("prefix_") + ...
            while (k < line.size() && line[k] == ' ') ++k;
            l.prefix = k < line.size() && line[k] == '+';
            if (!l.text.empty()) out.push_back(std::move(l));
            p = line.find('"', e + 1);
        }
    }
    return out;
}

bool ascii_code(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::set<std::string> placeholders(std::string_view s) {
    std::set<std::string> out;
    for (std::size_t p = s.find('{'); p != std::string_view::npos; p = s.find('{', p + 1)) {
        const auto e = s.find('}', p);
        if (e == std::string_view::npos) break;
        out.insert(std::string(s.substr(p, e - p + 1)));
    }
    return out;
}

const i18n::Catalog& tr_catalog() {
    static const auto c = i18n::Catalog::embedded(i18n::Locale::TrTR).value();
    return c;
}

}  // namespace

EF_TEST(bench_reason_every_emitted_code_is_cataloged) {
    // Kosucu/depolama/islemci/bellek/sahne kaynaklarindaki her neden sabiti ya tam katalog anahtarina ya da bir
    // onek ailesine eslenir; Turkce cumle/ham metin neden olarak uretilmez.
    const char* files[] = {"src/benchmark/runner/runner.cpp",       "src/benchmark/storage/storage_test.cpp",
                           "src/benchmark/storage/storage_rules.cpp", "src/benchmark/cpu/cpu_workload.cpp",
                           "src/benchmark/memory/memory_bench.cpp",   "src/benchmark/memory/memory_budget.cpp",
                           "src/benchmark/scene/d3d_renderer.cpp",    "src/benchmark/scene/scene.cpp"};
    std::size_t exact = 0, prefixes = 0;
    for (const char* f : files)
        for (const auto& l : scan_reason_literals(f)) {
            const bool ok_ascii = ascii_code(l.text);
            if (!ok_ascii) std::fprintf(stderr, "  non-code reason %s:%d \"%s\"\n", l.file.c_str(), l.line, l.text.c_str());
            EF_CHECK(ok_ascii);
            if (l.prefix) {
                ++prefixes;
                const auto& fam = reason_families();
                const bool known = std::any_of(fam.begin(), fam.end(), [&](const ReasonFamily& x) { return x.prefix == l.text; });
                if (!known) std::fprintf(stderr, "  uncataloged reason family %s:%d \"%s\"\n", l.file.c_str(), l.line, l.text.c_str());
                EF_CHECK(known);
            } else {
                ++exact;
                const bool known = i18n::id_of("bench.reason." + l.text).has_value();
                if (!known) std::fprintf(stderr, "  uncataloged reason %s:%d \"%s\"\n", l.file.c_str(), l.line, l.text.c_str());
                EF_CHECK(known);
            }
        }
    EF_CHECK(exact >= 50 && prefixes >= 5);  // tarayici bozulursa (0 bulgu) test gecmez
    // Islev donen nedenler (storage_rules): her enum degeri.
    namespace st = ef::benchmark::storage;
    for (auto v : {st::TargetVerdict::Empty, st::TargetVerdict::RawDevice, st::TargetVerdict::Network, st::TargetVerdict::Relative,
                   st::TargetVerdict::NotVolumeRoot, st::TargetVerdict::InvalidChars})
        EF_CHECK(i18n::id_of(std::string("bench.reason.") + st::verdict_reason(v)).has_value());
    for (auto v : {st::LimitsVerdict::FileTooSmall, st::LimitsVerdict::FileNotAligned, st::LimitsVerdict::CacheFileInvalid,
                   st::LimitsVerdict::BudgetBelowPreparation, st::LimitsVerdict::TimeInvalid, st::LimitsVerdict::VerifyInvalid})
        EF_CHECK(i18n::id_of(std::string("bench.reason.") + st::limits_reason(v)).has_value());
    // Aile anahtarlari da katalogda.
    for (const auto& fam : reason_families()) EF_CHECK(i18n::id_of(fam.key).has_value());
}

EF_TEST(bench_reason_text_exact_family_and_generic_fallback) {
    EF_CHECK(reason_text("timeout").key == "bench.reason.timeout" && reason_text("timeout").n.empty());
    EF_CHECK(reason_text("io_error_8").key == "bench.reason.io_error" && reason_text("io_error_8").n == "8");
    EF_CHECK(reason_text("preparation_io_error_112").key == "bench.reason.preparation_io_error" &&
              reason_text("preparation_io_error_112").n == "112");
    EF_CHECK(reason_text("io_port_failed_5").n == "5");
    EF_CHECK(reason_text("marker_exists_refused").key == "bench.reason.marker_failed");
    EF_CHECK(reason_text("marker_write_failed").key == "bench.reason.marker_write_failed");  // tam anahtar aileden once
    EF_CHECK(reason_text("cache_file_invalid").key == "bench.reason.cache_file_invalid");
    EF_CHECK(reason_text("cache_file_hardlink_refused").key == "bench.reason.cache_file_failed");
    EF_CHECK(reason_text("no_overlap_storage_").key == "bench.reason.no_overlap_storage");
    EF_CHECK(reason_text("no_overlap_storage_user_cancel").key == "bench.reason.no_overlap_storage");
    // Sayisal aile ama sonek sayi degil / bilinmeyen / Turkce cumle / bos: genel metin; ham deger tasinmaz.
    for (const char* bad : {"io_error_x", "io_error_", "totally_new_code", "bellek durumu okunamadi", "", "Timeout", "bench.reason.timeout"}) {
        const auto r = reason_text(bad);
        EF_CHECK(r.key == "bench.reason.unknown" && r.n.empty());
    }
    EF_CHECK(i18n::id_of("bench.reason.unknown").has_value());
}

EF_TEST(bench_strings_placeholders_match_and_cover_all_statuses) {
    const auto tr = i18n::Catalog::embedded(i18n::Locale::TrTR);
    const auto en = i18n::Catalog::embedded(i18n::Locale::EnUS);
    EF_CHECK(tr.ok() && en.ok());
    for (std::size_t i = 0; i < i18n::kStringCount; ++i) {
        const auto id = static_cast<i18n::StringId>(i);
        const auto key = i18n::key_of(id);
        if (!key.starts_with("bench.") && !key.starts_with("measure.")) continue;
        const bool same = placeholders(tr.value().get(id)) == placeholders(en.value().get(id));
        if (!same) std::fprintf(stderr, "  placeholder mismatch %.*s\n", static_cast<int>(key.size()), key.data());
        EF_CHECK(same);
    }
    // M10: her alt test x her durum icin kanit metni var (bos metin -> sessizce dusme yok).
    for (const char* id : {"scene", "cpu", "memory", "storage", "combined"})
        for (const char* stt : {"completed", "partial", "skipped", "failed", "unavailable", "cancelled"})
            EF_CHECK(i18n::id_of(std::string("bench.") + id + "." + stt).has_value());
    for (const char* stt : {"partial", "skipped", "failed", "unavailable", "cancelled"})
        EF_CHECK(i18n::id_of(std::string("bench.status.") + stt).has_value());
}

EF_TEST(bench_status_tone_and_evidence_status) {
    EF_CHECK(status_tone("completed") == StatusTone::Success);
    EF_CHECK(status_tone("partial") == StatusTone::Warning && status_tone("skipped") == StatusTone::Warning);
    EF_CHECK(status_tone("failed") == StatusTone::Error && status_tone("unavailable") == StatusTone::Error);
    EF_CHECK(status_tone("cancelled") == StatusTone::Secondary && status_tone("") == StatusTone::Secondary);
    EF_CHECK(evidence_status("bench.cpu.partial") == "partial" && evidence_status("bench.combined.completed") == "completed");
    EF_CHECK(evidence_status("health.xmp_off").empty());
    // BM-01: kosu dislama anahtarlari durum tonu tasimaz (uyari rengi) ve katalogdadir.
    EF_CHECK(evidence_status("bench.run.runtime_mixed").empty());
    for (auto e : {Exclusion::EtwEventsLost, Exclusion::TooFewFrames, Exclusion::TargetMismatch, Exclusion::Overlay,
                   Exclusion::RuntimeMixed, Exclusion::RuntimeOther, Exclusion::RuntimeUnknown, Exclusion::Cancelled,
                   Exclusion::TooShort, Exclusion::GameExited})
        EF_CHECK(i18n::id_of(std::string("bench.run.") + std::string(to_string(e))).has_value());
}

EF_TEST(bench_scene_missing_reason_separates_source_error_from_not_sampled) {
    // BM-05: sahne Failed/Cancelled -> kaynak hatasi; Unavailable -> desteklenmiyor; atlama -> orneklenmedi.
    using ef::contracts::MissingReason;
    EF_CHECK(scene_missing_reason("unavailable") == MissingReason::NotSupported);
    EF_CHECK(scene_missing_reason("failed") == MissingReason::SourceError);
    EF_CHECK(scene_missing_reason("cancelled") == MissingReason::SourceError);
    EF_CHECK(scene_missing_reason("skipped") == MissingReason::NotSampled);
    EF_CHECK(scene_missing_reason("partial") == MissingReason::NotSampled);
    EF_CHECK(scene_missing_reason("completed") == MissingReason::NotSampled);
}

EF_TEST(bench_result_confidence_uses_all_subtests_and_contention) {
    using Sub = std::vector<SubtestOutcome>;
    std::map<std::string, double> clean;
    const Sub all_ok{{"scene", "completed", ""}, {"cpu", "completed", ""}, {"memory", "completed", ""},
                     {"storage", "completed", ""}, {"combined", "completed", ""}};
    EF_CHECK(result_confidence(all_ok, clean) == ef::contracts::Confidence::Medium);
    // Kullanicinin secmedigi depolama/birlesik eksik sayilmaz (baslik yine orta).
    const Sub user_skip{{"scene", "completed", ""}, {"cpu", "completed", ""}, {"memory", "completed", ""},
                        {"storage", "skipped", "no_volume_selected"}, {"combined", "skipped", "no_volume_selected"}};
    EF_CHECK(result_confidence(user_skip, clean) == ef::contracts::Confidence::Medium);
    // RAM atlandi (gercek neden): guven duser.
    EF_CHECK(result_confidence({{"scene", "completed", ""}, {"cpu", "completed", ""},
                                 {"memory", "skipped", "free_memory_below_reserve"}},
                                clean) == ef::contracts::Confidence::Low);
    // Depolama kismi: baslik yalniz sahneye bakmaz, guven duser.
    EF_CHECK(result_confidence({{"scene", "completed", ""}, {"cpu", "completed", ""},
                                 {"memory", "completed", ""}, {"storage", "partial", "io_error_8"}},
                                clean) == ef::contracts::Confidence::Low);
    // Sahne basarisiz: guven dusuk.
    EF_CHECK(result_confidence({{"scene", "failed", "device_removed"}, {"cpu", "completed", ""},
                                 {"memory", "completed", ""}},
                                clean) == ef::contracts::Confidence::Low);
    // CPU yok: yalniz sahne yeterli degil.
    EF_CHECK(result_confidence({{"scene", "completed", ""}}, clean) == ef::contracts::Confidence::Low);
    // BM-03/04: olculmus CPU/RAM MT cekismesi guveni dusurur; bilinmeyen cekisme iddia degildir.
    auto cpu_c = clean;
    cpu_c["cpu_mt_contention_known"] = 1;
    cpu_c["cpu_mt_contended"] = 1;
    EF_CHECK(result_confidence(all_ok, cpu_c) == ef::contracts::Confidence::Low);
    auto mem_c = clean;
    mem_c["mem_mt_contention_known"] = 1;
    mem_c["mem_mt_contended"] = 1;
    EF_CHECK(result_confidence(all_ok, mem_c) == ef::contracts::Confidence::Low);
    auto unknown = clean;
    unknown["mem_mt_contention_known"] = 0;  // olculemedi: cekisme yok denmez ama guven de dusurulmez
    EF_CHECK(result_confidence(all_ok, unknown) == ef::contracts::Confidence::Medium);
}

EF_TEST(bench_builtin_summary_headline_never_insufficient) {
    // Tum bolumler tamam; disk secilmedi (kullanici secimi) eksik sayilmaz.
    auto s = builtin_summary({{"scene", "completed", ""}, {"cpu", "completed", ""}, {"memory", "completed", ""},
                              {"storage", "skipped", "no_volume_selected"}, {"combined", "skipped", "no_volume_selected"}});
    EF_CHECK(s.headline_key == "bench.result.completed" && s.done == 3 && s.total == 3 && s.any_data);
    // Kismi: 5 bolumden 2'si tamam; argumanlar dolu.
    s = builtin_summary({{"scene", "completed", ""}, {"cpu", "completed", ""}, {"memory", "skipped", "free_memory_below_reserve"},
                         {"storage", "partial", "io_error_8"}, {"combined", "partial", "no_overlap_storage_"}});
    EF_CHECK(s.headline_key == "bench.result.partial" && s.args.at("done") == "2" && s.args.at("total") == "5" && s.fail_reason.empty());
    // Hic veri yok: neden ilk basarisiz bolumden (sonraki adimli hata metni icin).
    s = builtin_summary({{"scene", "failed", "device_removed"}, {"cpu", "failed", "checksum_mismatch"}, {"storage", "skipped", "no_volume_selected"}});
    EF_CHECK(s.headline_key == "bench.result.none" && !s.any_data && s.fail_reason == "device_removed");
    for (const char* k : {"bench.result.completed", "bench.result.partial", "bench.result.none", "bench.result.incomplete"})
        EF_CHECK(i18n::id_of(k).has_value());
    EF_CHECK(builtin_history_headline({"storage.skipped", "combined.skipped"}) == "bench.result.completed");
    EF_CHECK(builtin_history_headline({"memory.skipped"}) == "bench.result.incomplete");
}

EF_TEST(bench_internal_scene_id_never_shown) {
    EF_CHECK(scene_tier_from_title("scene_v3 medium") == "medium" && scene_tier_from_title("scene_v2 high") == "high");
    EF_CHECK(scene_tier_from_title("Counter-Strike 2").empty() && scene_tier_from_title("scene_v3 weird").empty());
    EF_CHECK(tier_key("low") == "bench.tier.low" && tier_key("ultra").empty());
    for (const char* t : {"low", "medium", "high"}) EF_CHECK(i18n::id_of(tier_key(t)).has_value());

    const auto& theme = ef::ui::default_theme();
    ef::ui::Screen screen(theme, tr_catalog());
    ResultView legacy;
    legacy.source = TestSource::Builtin;
    legacy.title = "scene_v3 medium";
    EF_CHECK(screen.result_title(legacy) == L"Sahne kademesi: orta");
    legacy.title = "scene_v9 unknown";
    EF_CHECK(screen.result_title(legacy).empty());  // taninmayan ic kimlik: hic gosterilmez
    ResultView game;
    game.source = TestSource::InGame;
    game.title = "Counter-Strike 2";
    EF_CHECK(screen.result_title(game) == L"Counter-Strike 2");
}

EF_TEST(bench_screen_texts_have_no_raw_keys) {
    const auto& theme = ef::ui::default_theme();
    ef::ui::Screen screen(theme, tr_catalog());
    EF_CHECK(screen.bench_reason_text("io_error_1117") == L"disk işlemi Windows hatasıyla durdu (kod 1117)");
    EF_CHECK(screen.bench_reason_text("some_future_code") == L"teknik bir nedenle tamamlanamadı");
    ComponentCard c;
    c.status = "partial";
    c.reason = "timeout";
    EF_CHECK(screen.component_status_text(c) == L"Kısmi: süre sınırı doldu; o ana kadarki ölçüm gösteriliyor");
    c.status = "bogus_status";  // bilinmeyen durum: ham anahtar yerine hic
    EF_CHECK(screen.component_status_text(c).empty());

    // M02: Uygulama Testi basligi "Veri yetersiz" degil; guven satiri kapali.
    ResultView r;
    r.source = TestSource::Builtin;
    r.headline_key = "bench.result.partial";
    r.headline_args = {{"done", "2"}, {"total", "5"}};
    r.show_confidence = false;
    const auto h = screen.result_headline(r);
    EF_CHECK(h == L"Uygulama Testi kısmen tamamlandı: 5 bölümden 2 bölüm tamamlandı");
    EF_CHECK(h.find(L"yetersiz") == std::wstring::npos);

    // M11: hata metni neden + "hicbir ayar degismedi" + sonraki adim.
    MeasureState m;
    m.phase = TestPhase::Failed;
    m.fail_reason = "device_removed";
    const auto f = screen.measure_failure_text(m);
    EF_CHECK(f.find(L"ekran kartı sürücüsü test sırasında sıfırlandı") != std::wstring::npos);
    EF_CHECK(f.find(L"Hiçbir ayar değişmedi") != std::wstring::npos && f.find(L"yeniden başlatın") != std::wstring::npos);
    EF_CHECK(f.find(L'_') == std::wstring::npos);

    // M04: asama metni
    m.phase = TestPhase::Running;
    m.stage_key = "measure.stage_cpu";
    EF_CHECK(screen.measure_stage_text(m) == L"Aşama 2/5 · İşlemci");
    m.stage_key = "measure.stage_prepare";
    EF_CHECK(screen.measure_stage_text(m).find(L"Aşama") == std::wstring::npos);
}

EF_TEST(measure_busy_stage_and_live_elapsed) {
    MeasureState s;
    s.builtin_available = true;
    for (auto ph : {TestPhase::Preparing, TestPhase::Running, TestPhase::Stopping}) {
        s.phase = ph;
        EF_CHECK(busy(s) && !can_start(s));
    }
    s.phase = TestPhase::Stopping;
    EF_CHECK(!can_stop(s));  // M05: Durduruluyor'da tek gecerli eylem yok
    for (auto ph : {TestPhase::Idle, TestPhase::Done, TestPhase::Cancelled, TestPhase::Failed}) {
        s.phase = ph;
        EF_CHECK(!busy(s));
    }
    EF_CHECK(stage_position("measure.stage_scene") == (std::pair<int, int>{1, 5}));
    EF_CHECK(stage_position("measure.stage_combined") == (std::pair<int, int>{5, 5}));
    EF_CHECK(!stage_position("measure.stage_prepare") && !stage_position("measure.stage_done"));
    // Asama orani: tamamlanan asamalar (sure degil); yalniz Calisiyor + Uygulama Testi.
    s.phase = TestPhase::Running;
    s.stage_key = "measure.stage_memory";
    EF_CHECK(stage_fraction(s) && *stage_fraction(s) == 0.4);
    s.source = TestSource::InGame;
    EF_CHECK(!stage_fraction(s));
    s.source = TestSource::Builtin;
    s.phase = TestPhase::Preparing;
    EF_CHECK(!stage_fraction(s));
    // M04: total_s = 0 iken gecen sure saatten akar (1 Hz cizim); geri gitmez; bitince son deger.
    s.phase = TestPhase::Running;
    s.started_ms = 10'000;
    s.elapsed_s = 3;
    EF_CHECK(display_elapsed_s(s, 10'000 + 42'500) == 42.5);
    EF_CHECK(display_elapsed_s(s, 11'000) == 3.0);  // bildirilen deger daha buyukse o
    EF_CHECK(display_elapsed_s(s, 5'000) == 3.0);   // saat geriye: canli deger yok
    s.phase = TestPhase::Done;
    s.elapsed_s = 61;
    EF_CHECK(display_elapsed_s(s, 999'999) == 61.0);
    s.started_ms = 0;
    s.phase = TestPhase::Running;
    EF_CHECK(display_elapsed_s(s, 999'999) == 61.0);
}

EF_TEST(measure_announcements_on_state_changes) {
    MeasureState idle, prep, run1, run1b, run2, stop, done, canc, fail;
    prep.phase = TestPhase::Preparing;
    run1.phase = TestPhase::Running;
    run1.stage_key = "measure.stage_scene";
    run1b = run1;
    run1b.elapsed_s = 5;  // ayni asama: yeni duyuru yok
    run2 = run1;
    run2.stage_key = "measure.stage_cpu";
    stop.phase = TestPhase::Stopping;
    done.phase = TestPhase::Done;
    canc.phase = TestPhase::Cancelled;
    fail.phase = TestPhase::Failed;
    auto kind = [](const MeasureState& a, const MeasureState& b) { return measure_announcement(a, b); };
    EF_CHECK(kind(idle, prep) && kind(idle, prep)->kind == AnnounceKind::Started && !kind(idle, prep)->completed);
    EF_CHECK(kind(prep, run1) && kind(prep, run1)->kind == AnnounceKind::Stage);
    EF_CHECK(!kind(run1, run1b));
    EF_CHECK(kind(run1, run2) && kind(run1, run2)->kind == AnnounceKind::Stage);
    EF_CHECK(kind(run2, stop) && kind(run2, stop)->kind == AnnounceKind::Stopping);
    EF_CHECK(kind(run2, done) && kind(run2, done)->kind == AnnounceKind::Done && kind(run2, done)->completed);
    EF_CHECK(kind(stop, canc) && kind(stop, canc)->kind == AnnounceKind::Cancelled && kind(stop, canc)->completed);
    EF_CHECK(kind(run2, fail) && kind(run2, fail)->kind == AnnounceKind::Failed && kind(run2, fail)->completed);
    EF_CHECK(!kind(done, done) && !kind(idle, done));  // gorunum yenilemesi duyuru uretmez

    const auto& theme = ef::ui::default_theme();
    ef::ui::Screen screen(theme, tr_catalog());
    EF_CHECK(screen.measure_announcement_text({AnnounceKind::Stage, false}, run2) == L"Aşama 2/5: İşlemci");
    ResultView r;
    r.headline_key = "bench.result.completed";
    done.result = r;
    EF_CHECK(screen.measure_announcement_text({AnnounceKind::Done, true}, done) == L"Test bitti. Uygulama Testi tamamlandı");
    fail.fail_reason = "gpu_timeout";
    EF_CHECK(screen.measure_announcement_text({AnnounceKind::Failed, true}, fail).find(L"zamanında yanıt vermedi") != std::wstring::npos);
}

EF_TEST(measure_progress_track_contrast_at_least_3_to_1) {
    // M04: iz kenari karta karsi >= 3:1 (WCAG 1.4.11); dolgu (accent) izin icinde ayirt edilir.
    const auto& t = ef::ui::default_theme();
    const auto edge = ef::ui::ensure_contrast(t.dark.border, t.dark.surface, t.dark.text_secondary, 3.0);
    EF_CHECK(ef::ui::contrast_ratio(edge, t.dark.surface) >= 3.0);
    EF_CHECK(ef::ui::contrast_ratio(t.dark.accent, t.dark.surface_raised) >= 3.0);
    // TH-05: border token'i artik kendi basina >= 3:1; ensure_contrast degistirmeden dondurur.
    EF_CHECK(ef::ui::contrast_ratio(t.dark.border, t.dark.surface) >= 3.0);
    EF_CHECK(edge == t.dark.border);
}

EF_TEST(bench_cards_combined_card_and_note_dedupe) {
    // M10: birlesik yuk kosulduysa ayri kart (durum + neden); eski kayitta kart yok.
    const auto with = component_cards({{"combined_avg_fps", 71.2}}, {{"combined", {"partial", "no_overlap_storage_"}}});
    EF_CHECK(with.size() == 5 && with[4].id == "combined" && with[4].status == "partial" && with[4].metrics[0].value == 71.2);
    EF_CHECK(i18n::id_of(with[4].title_key).has_value());
    EF_CHECK(component_cards({}, {}).size() == 4);
    // M09: bellek notu yalniz olculen satirlarda ve kart basina bir kez.
    const auto mem = component_cards({{"mem_mt_read_gbps", 61.0}, {"mem_mt_write_gbps", 35.0}, {"mem_mt_buffer_l3_ratio", 4.0}}, {})[2];
    int notes = 0;
    for (const auto& x : mem.metrics) notes += x.note_key.empty() ? 0 : 1;
    EF_CHECK(notes == 1 && mem.metrics[1].note_key == "bench.note.large_buffer" && mem.metrics[2].note_key.empty());
}
