// EF-X22 T01: GPU hesap/geri okuma dogrulamasinin saf kurallari (D3D yok): CPU referansi, tolerans
// karsilastirmasi, butce/yapilandirma siniri ve sonuc -> sahne alt testi etkisi eslemesi.
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "benchmark/runner/runner.h"
#include "benchmark/scene/gpu_verify.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::benchmark::gpu_verify;
namespace i18n = ef::core::i18n;

EF_TEST(x22_gpu_reference_is_deterministic_and_index_dependent) {
    std::vector<std::uint32_t> a, b;
    reference_fill(a, 256, 0x1234u, 64);
    reference_fill(b, 256, 0x1234u, 64);
    EF_CHECK(a == b);
    EF_CHECK(a.size() == 256);
    // Komsu kelimeler ve farkli tohum farkli sonuc verir (sabit/tekduze cikti dogrulama sayilmaz).
    std::size_t same_neighbors = 0;
    for (std::size_t i = 1; i < a.size(); ++i) same_neighbors += a[i] == a[i - 1];
    EF_CHECK(same_neighbors == 0);
    EF_CHECK(reference_word(7, 0x1234u, 64) != reference_word(7, 0x1235u, 64));
    EF_CHECK(reference_word(7, 0x1234u, 64) != reference_word(7, 0x1234u, 65));
    EF_CHECK(a[5] == reference_word(5, 0x1234u, 64));
    // Sifir durum sikismasi yok: tohum index*sabite esit olsa da cikti dongu sayisina bagli.
    EF_CHECK(reference_word(0, 0, 8) != reference_word(0, 0, 9));
}

EF_TEST(x22_gpu_compare_exact_detects_injected_fault) {
    std::vector<std::uint32_t> ref;
    reference_fill(ref, 128, 99u, 32);
    auto got = ref;
    auto ok = compare_words(ref.data(), ref.size(), got.data(), got.size(), Tolerance::exact());
    EF_CHECK(ok.mismatched_words == 0 && !ok.first_mismatch_index);
    got[17] ^= 1u;  // tek bit hatali hesap
    got[90] ^= 0x80000000u;
    auto bad = compare_words(ref.data(), ref.size(), got.data(), got.size(), Tolerance::exact());
    EF_CHECK(bad.mismatched_words == 2);
    EF_CHECK(bad.first_mismatch_index && *bad.first_mismatch_index == 17);
    // Isaret degeriyle dolu (dispatch calismadi) tampon tamamen uyusmaz.
    std::vector<std::uint32_t> stale(ref.size(), kSentinel);
    EF_CHECK(compare_words(ref.data(), ref.size(), stale.data(), stale.size(), Tolerance::exact()).mismatched_words == ref.size());
    // Kisa geri okuma: eksik kelimeler uyusmaz sayilir.
    auto shortr = compare_words(ref.data(), ref.size(), ref.data(), ref.size() - 3, Tolerance::exact());
    EF_CHECK(shortr.mismatched_words == 3 && shortr.first_mismatch_index && *shortr.first_mismatch_index == ref.size() - 3);
    EF_CHECK(compare_words(ref.data(), 0, ref.data(), 0, Tolerance::exact()).mismatched_words == 0);
}

EF_TEST(x22_gpu_ulp_tolerance) {
    const auto one = std::bit_cast<std::uint32_t>(1.0f);
    const auto next = one + 1;  // 1 ULP
    const auto far = one + 3;
    EF_CHECK(ulp_distance(one, one) == 0u);
    EF_CHECK(ulp_distance(one, next) == 1u);
    EF_CHECK(ulp_distance(next, one) == 1u);
    EF_CHECK(ulp_distance(std::bit_cast<std::uint32_t>(0.0f), std::bit_cast<std::uint32_t>(-0.0f)) == 0u);
    // En kucuk pozitif ve negatif denormal arasi 2 ULP (sifirdan gecerek).
    EF_CHECK(ulp_distance(1u, 0x80000001u) == 2u);
    EF_CHECK(!ulp_distance(std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()), one));
    EF_CHECK(!ulp_distance(one, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity())));
    Tolerance t;
    t.kind = Tolerance::Kind::FloatUlp;
    t.max_ulp = 2;
    EF_CHECK(word_matches(one, next, t));
    EF_CHECK(!word_matches(one, far, t));
    EF_CHECK(!word_matches(one, next, Tolerance::exact()));  // urun cekirdegi tam esitlik ister
    const auto nan = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    EF_CHECK(!word_matches(one, nan, t));
    EF_CHECK(word_matches(nan, nan, t));  // bit-ayni: ayni hesap
}

EF_TEST(x22_gpu_config_bounds_and_budget) {
    const auto q = quick_config(), f = full_config();
    EF_CHECK(validate_config(q) == ConfigVerdict::Ok);
    EF_CHECK(validate_config(f) == ConfigVerdict::Ok);
    EF_CHECK(planned_bytes(q) <= kMaxBytes && planned_bytes(f) <= kMaxBytes);
    EF_CHECK(planned_bytes(q) == 3ull * q.words * 4);
    EF_CHECK(q.time_budget_ms <= 5000 && f.time_budget_ms <= 15000);  // Hizli/Tam toplam sureye kucuk ek
    EF_CHECK(dispatch_groups(q.words) * kThreadsPerGroup == q.words);
    EF_CHECK(dispatch_groups(65) == 2);
    auto c = q;
    c.words = 0;
    EF_CHECK(validate_config(c) == ConfigVerdict::WordsInvalid);
    c.words = 100;  // grup katinda degil
    EF_CHECK(validate_config(c) == ConfigVerdict::WordsInvalid);
    c.words = kMaxWords + kThreadsPerGroup;
    EF_CHECK(validate_config(c) == ConfigVerdict::WordsInvalid);
    c = q;
    c.iterations = kMaxIterations + 1;
    EF_CHECK(validate_config(c) == ConfigVerdict::IterationsInvalid);
    c = q;
    c.max_repeats = c.min_repeats - 1;
    EF_CHECK(validate_config(c) == ConfigVerdict::RepeatsInvalid);
    c = q;
    c.min_repeats = 0;
    EF_CHECK(validate_config(c) == ConfigVerdict::RepeatsInvalid);
    c = q;
    c.time_budget_ms = 0;
    EF_CHECK(validate_config(c) == ConfigVerdict::TimeInvalid);
    c = q;
    c.wait_timeout_ms = 0;
    EF_CHECK(validate_config(c) == ConfigVerdict::TimeInvalid);
    // Butce: tekrar siniri ve sure tahmini.
    c = q;
    EF_CHECK(may_start_repeat(0, c, 0, 0));
    EF_CHECK(!may_start_repeat(c.max_repeats, c, 0, 1));
    EF_CHECK(may_start_repeat(3, c, 1000, 100));
    EF_CHECK(!may_start_repeat(3, c, c.time_budget_ms - 50.0, 100));  // bir sonraki tekrar butceyi asar
    EF_CHECK(!may_start_repeat(0, c, c.time_budget_ms + 1.0, 0));     // hazirlik butceyi bitirdi
}

EF_TEST(x22_gpu_invalid_config_runs_no_gpu_work) {
    // Gecersiz yapilandirma cihaz kurmadan Invalid doner (GPU'ya dokunmaz; birim testte guvenli).
    auto c = quick_config();
    c.words = 3;
    std::atomic<bool> cancel{false};
    const auto r = run(c, cancel);
    EF_CHECK(r.status == Status::Invalid);
    EF_CHECK(r.repeats_done == 0 && r.adapter.empty());
    EF_CHECK(verdict(r.status).effect == Effect::FailNoScore);
}

EF_TEST(x22_gpu_verdict_mapping_never_passes_without_evidence) {
    // Yalniz Ok skoru "dogrulandi" yapar; cihaz yok/kurulamadi gecti sayilmaz, uyusmazlik skor vermez.
    EF_CHECK(verdict(Status::Ok).effect == Effect::Pass && verdict(Status::Ok).reason.empty());
    EF_CHECK(verdict(Status::Mismatch).effect == Effect::FailNoScore && verdict(Status::Mismatch).reason == "gpu_compute_mismatch");
    EF_CHECK(verdict(Status::DeviceRemoved).effect == Effect::FailNoScore && verdict(Status::DeviceRemoved).reason == "device_removed");
    EF_CHECK(verdict(Status::GpuTimeout).effect == Effect::FailNoScore && verdict(Status::GpuTimeout).reason == "gpu_timeout");
    EF_CHECK(verdict(Status::ReadbackFailed).effect == Effect::FailNoScore);
    EF_CHECK(verdict(Status::Invalid).effect == Effect::FailNoScore);
    EF_CHECK(verdict(Status::TimeLimit).effect == Effect::Partial);
    EF_CHECK(verdict(Status::Cancelled).effect == Effect::Cancelled);
    EF_CHECK(verdict(Status::Unsupported).effect == Effect::NoClaim);
    EF_CHECK(verdict(Status::SetupFailed).effect == Effect::NoClaim);
    EF_CHECK(verdict(Status::NotRun).effect == Effect::NoClaim);
    // Uretilen her neden kodu katalogda (bench.reason.<kod>).
    for (auto s : {Status::Ok, Status::Mismatch, Status::Unsupported, Status::SetupFailed, Status::DeviceRemoved, Status::GpuTimeout,
                   Status::ReadbackFailed, Status::Cancelled, Status::TimeLimit, Status::Invalid, Status::NotRun}) {
        const auto v = verdict(s);
        EF_CHECK(!to_string(s).empty());
        if (!v.reason.empty()) EF_CHECK(i18n::id_of("bench.reason." + std::string(v.reason)).has_value());
    }
    EF_CHECK(i18n::id_of(std::string("bench.reason.") + config_reason(ConfigVerdict::MemoryOverBudget)).has_value());
}

namespace {
namespace bm = ef::benchmark;
bm::SubtestResult completed_scene(bm::BenchmarkReport& rep) {
    bm::SubtestResult r{"scene"};
    r.status = bm::SubtestStatus::Completed;
    r.metrics = {{"scene_avg_fps", 240.0}, {"scene_low1_fps", 180.0}};
    rep.scene_fps_series = {240.0, 239.0};
    rep.scene_measured = true;
    return r;
}
Result verify_result(Status s, std::uint32_t repeats, std::uint64_t mismatched) {
    Result v;
    v.status = s;
    v.words = 65536;
    v.repeats_done = repeats;
    v.mismatched_words = mismatched;
    return v;
}
bool has_metric(const bm::SubtestResult& r, const char* k) {
    for (const auto& [n, x] : r.metrics)
        if (n == k) return true;
    return false;
}
}  // namespace

EF_TEST(x22_gpu_runner_injected_mismatch_yields_failure_not_score) {
    // T01: hatali GPU hesabi -> sahne skoru silinir, durum Failed, neden gpu_compute_mismatch.
    bm::BenchmarkReport rep;
    auto r = completed_scene(rep);
    bm::apply_gpu_verify(r, rep, verify_result(Status::Mismatch, 4, 1), false);
    EF_CHECK(r.status == bm::SubtestStatus::Failed && r.reason == "gpu_compute_mismatch");
    EF_CHECK(!has_metric(r, "scene_avg_fps") && !has_metric(r, "scene_low1_fps"));
    EF_CHECK(has_metric(r, "scene_gpu_verify_mismatch_words"));
    EF_CHECK(rep.scene_fps_series.empty() && !rep.scene_measured);
    // Cihaz kaybi da skor vermez.
    bm::BenchmarkReport rep2;
    auto r2 = completed_scene(rep2);
    bm::apply_gpu_verify(r2, rep2, verify_result(Status::DeviceRemoved, 1, 0), false);
    EF_CHECK(r2.status == bm::SubtestStatus::Failed && r2.reason == "device_removed" && !has_metric(r2, "scene_avg_fps"));
}

EF_TEST(x22_gpu_runner_pass_unsupported_cancel) {
    bm::BenchmarkReport rep;
    auto r = completed_scene(rep);
    bm::apply_gpu_verify(r, rep, verify_result(Status::Ok, 32, 0), false);
    EF_CHECK(r.status == bm::SubtestStatus::Completed && r.reason.empty() && has_metric(r, "scene_avg_fps"));
    EF_CHECK(has_metric(r, "scene_gpu_verify_repeats") && rep.scene_measured);
    // Cihaz kurulamadi: skor kalir ama dogrulama metrigi yok (dogrulandi denmez).
    bm::BenchmarkReport rep2;
    auto r2 = completed_scene(rep2);
    bm::apply_gpu_verify(r2, rep2, verify_result(Status::Unsupported, 0, 0), false);
    EF_CHECK(r2.status == bm::SubtestStatus::Completed && has_metric(r2, "scene_avg_fps"));
    EF_CHECK(!has_metric(r2, "scene_gpu_verify_repeats"));
    // Iptal: skor silinir; bekci zaman asimi Partial "timeout".
    bm::BenchmarkReport rep3;
    auto r3 = completed_scene(rep3);
    bm::apply_gpu_verify(r3, rep3, verify_result(Status::Cancelled, 2, 0), false);
    EF_CHECK(r3.status == bm::SubtestStatus::Cancelled && r3.reason == "user_cancel" && r3.metrics.empty() && !rep3.scene_measured);
    bm::BenchmarkReport rep4;
    auto r4 = completed_scene(rep4);
    bm::apply_gpu_verify(r4, rep4, verify_result(Status::Cancelled, 2, 0), true);
    EF_CHECK(r4.status == bm::SubtestStatus::Partial && r4.reason == "timeout");
    // Sure siniri: kismi; dogrulandi denmez ama sayilar kayitta.
    bm::BenchmarkReport rep5;
    auto r5 = completed_scene(rep5);
    bm::apply_gpu_verify(r5, rep5, verify_result(Status::TimeLimit, 2, 0), false);
    EF_CHECK(r5.status == bm::SubtestStatus::Partial && r5.reason == "gpu_verify_incomplete");
}
