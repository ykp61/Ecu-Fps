// EF-X22 T01: islemci hesap dogrulamasi (degisken yuk + CPUID etiketli SIMD varyantlari), dogrulama durum
// kodlarinin gecmise yazimi ve Uygulama Testi sonucundaki dogrulama karti.
#include <atomic>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "benchmark/cpu/cpu_verify.h"
#include "benchmark/runner/runner.h"
#include "core/i18n/catalog.h"
#include "core/measure/bench_cards.h"
#include "core/measure/verify_state.h"
#include "ef_test.h"

namespace cv = ef::benchmark::cpu_verify;
namespace bm = ef::benchmark;
namespace cm = ef::core::measure;
namespace i18n = ef::core::i18n;

namespace {
// Bagimsiz referans: ayni tanim std::rotl ile ve tur sabiti ayri formulle (urun kodunu cagirmadan).
std::uint32_t independent_word(std::uint32_t x, std::uint32_t seed, std::uint32_t rounds) {
    for (std::uint32_t r = 0; r < rounds; ++r) {
        x = x ^ (x << 13);
        x = x ^ (x >> 17);
        x = x ^ (x << 5);
        const std::uint32_t k = static_cast<std::uint32_t>(static_cast<std::uint64_t>(seed) * 2654435769ull +
                                                           static_cast<std::uint64_t>(r) * 1663821227ull + 2135587861ull);
        x = std::rotl(static_cast<std::uint32_t>(x + k), 7);
    }
    return x;
}
std::optional<double> metric(const bm::SubtestResult& r, const std::string& k) {
    for (const auto& [n, v] : r.metrics)
        if (n == k) return v;
    return std::nullopt;
}
bm::SubtestResult completed_cpu() {
    bm::SubtestResult r{"cpu"};
    r.status = bm::SubtestStatus::Completed;
    r.metrics = {{"cpu_st_units_per_s", 861.0}, {"cpu_mt_units_per_s", 5235.0}};
    return r;
}
cv::Config small_config() {
    cv::Config c;
    c.threads = 2;
    c.time_budget_ms = 240;
    c.slice_ms = 20;
    c.min_blocks_per_isa = 1;
    c.rounds = 16;
    return c;
}
cv::CpuidBits all_bits() {
    cv::CpuidBits b;
    b.cpuid_ok = b.sse2 = b.osxsave = b.avx = b.avx2 = b.avx512f = true;
    b.xcr0 = 0xE7;
    return b;
}
}  // namespace

EF_TEST(x22_cpu_reference_matches_independent_and_is_input_dependent) {
    std::vector<std::uint32_t> in(cv::kWordsPerBlock);
    cv::fill_input(in.data(), 0x1234u, 3);
    for (std::uint32_t i = 0; i < cv::kWordsPerBlock; i += 37)
        EF_CHECK(cv::reference_word(in[i], 0x1234u, 64) == independent_word(in[i], 0x1234u, 64));
    EF_CHECK(cv::reference_word(5, 0, 0) == 5);  // 0 tur: kimlik
    std::size_t same = 0;
    for (std::uint32_t i = 1; i < cv::kWordsPerBlock; ++i) same += in[i] == in[i - 1];
    EF_CHECK(same == 0);
    std::vector<std::uint32_t> other(cv::kWordsPerBlock);
    cv::fill_input(other.data(), 0x1234u, 4);
    EF_CHECK(other[0] != in[0]);
    EF_CHECK(cv::reference_word(in[0], 1, 8) != cv::reference_word(in[0], 2, 8));
}

EF_TEST(x22_cpu_every_supported_isa_matches_reference_on_this_cpu) {
    // Gercek islemci: CPUID + OS destegi raporlanan her varyant referansla bit-ayni. Desteklenmeyen calistirilmaz.
    const auto bits = cv::read_cpuid();
    std::vector<std::uint32_t> in(cv::kWordsPerBlock), out(cv::kWordsPerBlock);
    int ran = 0;
    for (std::uint32_t k = 0; k < cv::kIsaCount; ++k) {
        const auto isa = static_cast<cv::Isa>(k);
        if (!cv::isa_supported(isa, bits)) continue;
        ++ran;
        for (std::uint64_t b : {0ull, 1ull, 977ull}) {
            cv::fill_input(in.data(), 0xC0FFEEu, b);
            cv::transform(isa, in.data(), out.data(), 0xC0FFEEu, 48);
            std::uint32_t bad = 0;
            for (std::uint32_t i = 0; i < cv::kWordsPerBlock; ++i) bad += out[i] != cv::reference_word(in[i], 0xC0FFEEu, 48);
            EF_CHECK(bad == 0);
        }
    }
    EF_CHECK(ran >= 1);  // skaler her zaman
#if defined(_M_X64)
    EF_CHECK(bits.cpuid_ok && cv::isa_supported(cv::Isa::Sse2, bits));  // x64 taban
#endif
}

EF_TEST(x22_cpu_isa_support_requires_cpuid_and_os_state) {
    auto b = all_bits();
    for (std::uint32_t k = 0; k < cv::kIsaCount; ++k) EF_CHECK(cv::isa_supported(static_cast<cv::Isa>(k), b));
    auto no_os = b;
    no_os.osxsave = false;  // CPU destekler ama isletim sistemi YMM/ZMM durumunu saklamiyor
    EF_CHECK(!cv::isa_supported(cv::Isa::Avx2, no_os) && !cv::isa_supported(cv::Isa::Avx512, no_os));
    EF_CHECK(cv::isa_supported(cv::Isa::Sse2, no_os));
    auto ymm_only = b;
    ymm_only.xcr0 = 0x7;  // ZMM durumu kapali
    EF_CHECK(cv::isa_supported(cv::Isa::Avx2, ymm_only) && !cv::isa_supported(cv::Isa::Avx512, ymm_only));
    auto no_avx2 = b;
    no_avx2.avx2 = false;
    EF_CHECK(!cv::isa_supported(cv::Isa::Avx2, no_avx2));
    cv::CpuidBits none;  // CPUID yok: yalniz skaler
    EF_CHECK(cv::isa_supported(cv::Isa::Scalar, none) && !cv::isa_supported(cv::Isa::Sse2, none));
    EF_CHECK(cv::isa_name(cv::Isa::Avx512) == "avx512" && cv::isa_name(cv::Isa::Scalar) == "scalar");
}

EF_TEST(x22_cpu_variable_load_phases_and_config_bounds) {
    EF_CHECK(cv::phase_at(0, 4000) == 0 && cv::phase_at(999, 4000) == 0 && cv::phase_at(1000, 4000) == 1);
    EF_CHECK(cv::phase_at(3999, 4000) == 3 && cv::phase_at(4000, 4000) == 4 && cv::phase_at(-1, 4000) == 4);
    EF_CHECK(cv::kPhaseDutyPercent[0] == 100 && cv::kPhaseDutyPercent[1] == 25);
    EF_CHECK(cv::busy_at(39, 40, 100) && cv::busy_at(9, 40, 25) && !cv::busy_at(10, 40, 25) && cv::busy_at(41, 40, 25));
    EF_CHECK(!cv::busy_at(5, 40, 0));
    EF_CHECK(cv::validate_config(cv::quick_config()) == cv::ConfigVerdict::Ok);
    EF_CHECK(cv::validate_config(cv::full_config()) == cv::ConfigVerdict::Ok);
    auto c = cv::quick_config();
    c.rounds = 0;
    EF_CHECK(cv::validate_config(c) == cv::ConfigVerdict::RoundsInvalid);
    c = cv::quick_config();
    c.threads = cv::kMaxThreads + 1;
    EF_CHECK(cv::validate_config(c) == cv::ConfigVerdict::ThreadsInvalid);
    c = cv::quick_config();
    c.time_budget_ms = cv::kMaxTimeBudgetMs + 1;
    EF_CHECK(cv::validate_config(c) == cv::ConfigVerdict::TimeInvalid);
    c = cv::quick_config();
    c.isa_mask = 0;
    EF_CHECK(cv::validate_config(c) == cv::ConfigVerdict::NoIsa);
    EF_CHECK(std::string(cv::config_reason(cv::ConfigVerdict::SliceInvalid)) == "cpu_verify_invalid");
    // Gecersiz yapilandirma hic is parcacigi baslatmaz.
    std::atomic<bool> no{false};
    c = small_config();
    c.rounds = cv::kMaxRounds + 1;
    const auto r = cv::run(c, no);
    EF_CHECK(r.status == cv::Status::Invalid && r.blocks == 0 && r.threads == 0);
}

EF_TEST(x22_cpu_run_real_passes_and_labels_isas) {
    std::atomic<bool> no{false};
    const auto r = cv::run(small_config(), no);
    EF_CHECK(r.status == cv::Status::Ok);
    EF_CHECK(r.mismatched_blocks == 0 && r.blocks > 0 && r.threads == 2);
    const auto bits = cv::read_cpuid();
    for (const auto& i : r.isas) {
        EF_CHECK(i.supported == cv::isa_supported(i.isa, bits));
        if (i.supported) EF_CHECK(i.blocks >= 1);
        else EF_CHECK(i.blocks == 0);  // desteklenmeyen varyant calistirilmadi
    }
    // Degisken yuk: en az iki fazda blok karsilastirildi (yuk %100 ve %25 dahil).
    int phases = 0;
    for (auto n : r.phase_blocks) phases += n > 0;
    EF_CHECK(phases >= 2);
}

EF_TEST(x22_cpu_injected_fault_is_mismatch_not_score) {
    // T01: hatali islemci hesabi -> skor yerine hesap tutarsizligi.
    std::atomic<bool> no{false};
    auto c = small_config();
    c.inject_fault_block = 2;
    const auto r = cv::run(c, no);
    EF_CHECK(r.status == cv::Status::Mismatch && r.mismatched_blocks == 1);
    EF_CHECK(r.first_mismatch_block && *r.first_mismatch_block == 2 && r.first_mismatch_isa.has_value());
    auto sub = completed_cpu();
    bm::apply_cpu_verify(sub, r, false);
    EF_CHECK(sub.status == bm::SubtestStatus::Failed && sub.reason == "cpu_compute_mismatch");
    EF_CHECK(!metric(sub, "cpu_st_units_per_s") && !metric(sub, "cpu_mt_units_per_s"));
    EF_CHECK(metric(sub, "cpu_verify_state") == static_cast<double>(cm::VerifyCode::Mismatch));
    EF_CHECK(metric(sub, "cpu_verify_mismatch_blocks") == 1.0);
    EF_CHECK(i18n::id_of("bench.reason.cpu_compute_mismatch").has_value());
}

EF_TEST(x22_cpu_unsupported_variant_is_not_failure) {
    // Yalniz skaler destek (CPUID yok benzetimi): SIMD varyantlari desteklenmiyor, sonuc yine gecti.
    std::atomic<bool> no{false};
    auto c = small_config();
    c.cpuid_override = cv::CpuidBits{};
    const auto r = cv::run(c, no);
    EF_CHECK(r.status == cv::Status::Ok);
    EF_CHECK(r.isas[0].supported && r.isas[0].blocks > 0);
    for (int k = 1; k < 4; ++k) EF_CHECK(!r.isas[k].supported && r.isas[k].blocks == 0);
    auto sub = completed_cpu();
    bm::apply_cpu_verify(sub, r, false);
    EF_CHECK(sub.status == bm::SubtestStatus::Completed && metric(sub, "cpu_st_units_per_s"));
    EF_CHECK(metric(sub, "cpu_verify_avx2_supported") == 0.0 && !metric(sub, "cpu_verify_avx2_blocks"));
    EF_CHECK(metric(sub, "cpu_verify_scalar_supported") == 1.0);
    // Maske yalniz desteklenmeyeni secerse dogrulama yok: Invalid ("gecti" denmez).
    c.isa_mask = cv::isa_bit(cv::Isa::Avx512);
    EF_CHECK(cv::run(c, no).status == cv::Status::Invalid);
}

EF_TEST(x22_cpu_cancel_budget_and_incomplete) {
    std::atomic<bool> yes{true};
    const auto pre = cv::run(small_config(), yes);
    EF_CHECK(pre.status == cv::Status::Cancelled && pre.blocks == 0);
    auto sub = completed_cpu();
    bm::apply_cpu_verify(sub, pre, false);
    EF_CHECK(sub.status == bm::SubtestStatus::Cancelled && sub.reason == "user_cancel" && sub.metrics.empty());
    auto sub2 = completed_cpu();
    bm::apply_cpu_verify(sub2, pre, true);
    EF_CHECK(sub2.status == bm::SubtestStatus::Partial && sub2.reason == "timeout" && sub2.metrics.empty());
    // Blok tavani asilmaz; desteklenen her varyant esige ulasamazsa Incomplete (skor kalir, dogrulandi denmez).
    std::atomic<bool> no{false};
    auto c = small_config();
    c.max_blocks = 1;
    c.min_blocks_per_isa = 2;
    const auto r = cv::run(c, no);
    EF_CHECK(r.blocks <= 1 && r.status == cv::Status::Incomplete);
    auto sub3 = completed_cpu();
    bm::apply_cpu_verify(sub3, r, false);
    EF_CHECK(sub3.status == bm::SubtestStatus::Partial && sub3.reason == "cpu_verify_incomplete" && metric(sub3, "cpu_st_units_per_s"));
    EF_CHECK(metric(sub3, "cpu_verify_state") == static_cast<double>(cm::VerifyCode::Incomplete));
    // Uyusmazlik iptalde de korunur.
    cv::Result m;
    m.mismatched_blocks = 1;
    EF_CHECK(cv::decide(m, c, true) == cv::Status::Mismatch);
}

EF_TEST(x22_gpu_unsupported_state_differs_from_not_run) {
    // Desteklenmiyor/kurulamadi: skor kalir, sayac yok, ama durum kodu gecmise yazilir. NotRun: hicbir anahtar yok.
    auto scene = [] {
        bm::SubtestResult r{"scene"};
        r.status = bm::SubtestStatus::Completed;
        r.metrics = {{"scene_avg_fps", 90.0}};
        return r;
    };
    namespace gv = ef::benchmark::gpu_verify;
    auto res = [](gv::Status s) {
        gv::Result v;
        v.status = s;
        return v;
    };
    bm::BenchmarkReport rep;
    auto a = scene();
    bm::apply_gpu_verify(a, rep, res(gv::Status::Unsupported), false);
    EF_CHECK(metric(a, "scene_gpu_verify_state") == static_cast<double>(cm::VerifyCode::Unsupported) && metric(a, "scene_avg_fps"));
    EF_CHECK(!metric(a, "scene_gpu_verify_repeats"));
    auto b = scene();
    bm::apply_gpu_verify(b, rep, res(gv::Status::SetupFailed), false);
    EF_CHECK(metric(b, "scene_gpu_verify_state") == static_cast<double>(cm::VerifyCode::SetupFailed));
    auto c = scene();
    bm::apply_gpu_verify(c, rep, res(gv::Status::NotRun), false);
    EF_CHECK(!metric(c, "scene_gpu_verify_state") && c.metrics.size() == 1);
    auto d = scene();
    auto ok = res(gv::Status::Ok);
    ok.repeats_done = 32;
    ok.words = 65536;
    bm::apply_gpu_verify(d, rep, ok, false);
    EF_CHECK(metric(d, "scene_gpu_verify_state") == static_cast<double>(cm::VerifyCode::Passed));
}

EF_TEST(x22_storage_verify_state_codes) {
    namespace st = ef::benchmark::storage;
    EF_CHECK(!bm::storage_verify_code(st::VerifyStatus::NotRun, "no_volume_selected"));
    EF_CHECK(bm::storage_verify_code(st::VerifyStatus::NotRun, "storage_verify_setup_failed") == cm::VerifyCode::SetupFailed);
    EF_CHECK(bm::storage_verify_code(st::VerifyStatus::Ok, "") == cm::VerifyCode::Passed);
    EF_CHECK(bm::storage_verify_code(st::VerifyStatus::Mismatch, "") == cm::VerifyCode::Mismatch);
    EF_CHECK(bm::storage_verify_code(st::VerifyStatus::IoError, "") == cm::VerifyCode::IoError);
    EF_CHECK(bm::storage_verify_code(st::VerifyStatus::TimeLimit, "") == cm::VerifyCode::Incomplete);
}

EF_TEST(x22_verify_card_states_and_real_counts) {
    // Eski kayit: hicbir dogrulama anahtari yok -> kart yok (sahte "calismadi" satiri da yok).
    for (const auto& c : cm::component_cards({{"scene_avg_fps", 90.0}}, {})) EF_CHECK(c.id != "verify");
    const std::map<std::string, double> m{{"scene_gpu_verify_state", 3},
                                          {"cpu_verify_state", 2}, {"cpu_verify_blocks", 120}, {"cpu_verify_mismatch_blocks", 1},
                                          {"cpu_verify_sse2_supported", 1}, {"cpu_verify_avx2_supported", 1},
                                          {"cpu_verify_avx512_supported", 0}};
    const auto cards = cm::component_cards(m, {});
    const cm::ComponentCard* v = nullptr;
    for (const auto& c : cards)
        if (c.id == "verify") v = &c;
    EF_CHECK(v != nullptr);
    if (!v) return;
    EF_CHECK(v->title_key == "bench.card.verify" && v->metrics.size() == 3);
    const auto& gpu = v->metrics[0];
    const auto& cpu = v->metrics[1];
    const auto& sto = v->metrics[2];
    EF_CHECK(gpu.value_key == "bench.verify.unsupported" && gpu.note_key == "bench.reason.gpu_verify_unsupported");
    EF_CHECK(cpu.value_key == "bench.verify.mismatch_compute" && cpu.value_tone == "error");
    EF_CHECK(cpu.note_key == "bench.verify.note.cpu_unsupported" && cpu.note_args.at("blocks") == "120" &&
              cpu.note_args.at("bad") == "1" && cpu.note_args.at("isas") == "SSE2, AVX2" && cpu.note_args.at("unsupported") == "AVX-512");
    EF_CHECK(sto.value_key == "bench.verify.not_run" && sto.note_key.empty() && !sto.value);  // anahtar yok: calismadi, sayi yok
    // Gecti + gercek sayac.
    const auto pass = cm::component_cards({{"storage_verify_state", 1}, {"storage_verify_blocks", 64}, {"storage_verify_mismatch_blocks", 0}}, {});
    EF_CHECK(pass.back().id == "verify" && pass.back().metrics[2].value_key == "bench.verify.passed" &&
              pass.back().metrics[2].value_tone == "success" && pass.back().metrics[2].note_args.at("blocks") == "64");
    EF_CHECK(pass.back().metrics[0].value_key == "bench.verify.not_run");  // GPU anahtari yok: desteklenmiyor DEGIL
    // Gecersiz/bilinmeyen kod "gecti" sayilmaz.
    const auto bad = cm::component_cards({{"cpu_verify_state", 42}}, {});
    EF_CHECK(bad.back().metrics[1].value_key == "bench.verify.not_run");
    // Kartin urettigi tum anahtarlar katalogda.
    for (const auto* k : {"bench.card.verify", "bench.verify.gpu", "bench.verify.cpu", "bench.verify.storage", "bench.verify.passed",
                          "bench.verify.mismatch_compute", "bench.verify.mismatch_data", "bench.verify.unsupported", "bench.verify.incomplete",
                          "bench.verify.not_run", "bench.verify.note.gpu", "bench.verify.note.cpu", "bench.verify.note.cpu_unsupported",
                          "bench.verify.note.cpu_scalar", "bench.verify.note.storage", "bench.reason.cpu_verify_incomplete",
                          "bench.reason.cpu_verify_invalid", "bench.reason.storage_verify_io_error", "bench.reason.gpu_timeout",
                          "bench.reason.device_removed", "measure.mixed_load_warning"})
        EF_CHECK(i18n::id_of(k).has_value());
}

EF_TEST(x22_cpu_thread_failure_is_incomplete_not_crash) {
    // Inceleme bulgusu: is parcacigi baslatilamazsa veya calisirken istisna verirse surec sonlanmaz (std::terminate
    // yok), baslayanlar birlestirilir ve sonuc "gecti" degil Incomplete olur; CPU skoru kalir, dogrulandi denmez.
    std::atomic<bool> no{false};
    auto c = small_config();
    c.inject_thread_start_failure = 1;  // ilk is parcacigi basladi, ikincisi baslatilamadi
    const auto r = cv::run(c, no);
    EF_CHECK(r.worker_failed && r.status == cv::Status::Incomplete && r.mismatched_blocks == 0);
    EF_CHECK(r.blocks > 0);  // baslayan is parcacigi dogrulamaya devam etti ve birlestirildi
    auto sub = completed_cpu();
    bm::apply_cpu_verify(sub, r, false);
    EF_CHECK(sub.status == bm::SubtestStatus::Partial && sub.reason == "cpu_verify_incomplete" && metric(sub, "cpu_st_units_per_s"));
    EF_CHECK(metric(sub, "cpu_verify_state") == static_cast<double>(cm::VerifyCode::Incomplete));
    c = small_config();
    c.inject_thread_start_failure = 0;  // hic is parcacigi baslamadi
    const auto none = cv::run(c, no);
    EF_CHECK(none.worker_failed && none.blocks == 0 && none.status == cv::Status::Incomplete);
    c = small_config();
    c.inject_worker_throw = true;  // ilk is parcacigi bellek ayiramadi
    const auto w = cv::run(c, no);
    EF_CHECK(w.worker_failed && w.status == cv::Status::Incomplete);
    // Uyusmazlik istisnadan once gelir: hatali hesap varsa yine Mismatch.
    cv::Result m;
    m.mismatched_blocks = 1;
    m.worker_failed = true;
    EF_CHECK(cv::decide(m, small_config(), false) == cv::Status::Mismatch);
}
