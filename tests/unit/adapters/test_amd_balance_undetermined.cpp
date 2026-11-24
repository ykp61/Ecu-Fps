// P9-B8: AMD ekran kartinda Denge testi "belirlenemedi" yolu (saha raporu 2026-11-17, denge_v4). Fixture, gercek AMD
// kartin GPU zaman damgasi sorununu (gecersiz/disjoint kareler) ve surucu sinir API'sinin yoklugunu (ADLX baglanmadi)
// saf kurala besler. Gercek AMD cihaz sonucu degildir (fixture); amac: belirlenemeyen durumda sayi/hukum UYDURULMAZ,
// neden anahtari sabit kalir, API yoklugu hukmu engellemez ama not olarak gorunur.
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#include "core/diagnosis/balance.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;

namespace {

constexpr std::uint32_t kPciAmd = 0x1002, kPciIntel = 0x8086, kPciNvidia = 0x10DE;

bool has(const std::vector<std::string_view>& v, std::string_view code) { return std::find(v.begin(), v.end(), code) != v.end(); }

// `seconds` saniyelik sabit kare akisi (frame_ms araliginda).
std::vector<double> frames(double frame_ms, double seconds) {
    return std::vector<double>(static_cast<std::size_t>(seconds * 1000.0 / frame_ms), frame_ms);
}
std::vector<double> busy(std::size_t n, double ms) { return std::vector<double>(n, ms); }

constexpr double kMeasureS = 6.0;
constexpr double kRunS = 8.0;  // isinma atilmis pencere + payi

// CPU kapasitesi adimi (S1/T1): GPU zaman damgasi guvenilmez olabilir (disjoint), FPS CPU ritminden gelir.
StepStats cpu_step(double fps, std::uint32_t disjoint) {
    const auto f = frames(1000.0 / fps, kRunS);
    return summarize_step(f, busy(f.size(), 1.0), kMeasureS, disjoint, /*gpu_time_required=*/false);
}
// GPU kapasitesi adimi: GPU suresine dayanir; GPU tam mesgul (0,97).
StepStats gpu_step(double fps, std::uint32_t disjoint) {
    const auto f = frames(1000.0 / fps, kRunS);
    return summarize_step(f, busy(f.size(), 0.97 * 1000.0 / fps), kMeasureS, disjoint, /*gpu_time_required=*/true);
}
// Birlikte adim: hiz min(cpu,gpu); GPU mesguliyeti verilir.
StepStats coupled_step(double fps, double busy_ratio, std::uint32_t disjoint) {
    const auto f = frames(1000.0 / fps, kRunS);
    return summarize_step(f, busy(f.size(), busy_ratio * 1000.0 / fps), kMeasureS, disjoint, true);
}

BalanceInputs amd_inputs(std::uint32_t cpu_disjoint, std::uint32_t gpu_disjoint) {
    BalanceInputs in;
    in.cpu = cpu_step(200.0, cpu_disjoint);
    in.cpu_repeat = cpu_step(200.0, cpu_disjoint);
    for (std::size_t i = 0; i < kBalanceRows; ++i) {
        in.gpu[i] = gpu_step(100.0, gpu_disjoint);       // oran 0,5: GPU bandi olurdu
        in.coupled[i] = coupled_step(100.0, 0.97, gpu_disjoint);
    }
    in.gpu_repeat = gpu_step(100.0, gpu_disjoint);
    in.confounders.other_cpu_pct = 2.0;
    in.confounders.other_gpu_pct = 0.0;
    in.confounders.vendor_limit_no_api = vendor_limit_api_missing(kPciAmd);
    return in;
}

}  // namespace

EF_TEST(amd_denge_vendor_limit_api_is_missing_only_for_amd_and_intel) {
    EF_CHECK(vendor_limit_api_missing(kPciAmd) && vendor_limit_api_missing(kPciIntel));
    EF_CHECK(!vendor_limit_api_missing(kPciNvidia));
    EF_CHECK(!vendor_limit_api_missing(0) && !vendor_limit_api_missing(0x1414));  // bilinmeyen uretici hakkinda iddia yok
}

EF_TEST(amd_denge_fixture_clean_run_is_decided_and_api_absence_is_only_a_note) {
    // Saha raporu duzeltmesi (denge_v4) sonrasi temiz kosu: CPU ve GPU adimlari gecerli, AMD'de ADLX yok.
    const auto in = amd_inputs(0, 0);
    EF_CHECK(in.cpu.state == StepState::Valid && in.gpu[0].state == StepState::Valid);
    const auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason.empty() && r.global_reasons.empty());  // API yoklugu tum satirlari cekimser YAPMAZ
    EF_CHECK(has(r.notes, balance_note::kVendorLimitNoApi));          // ama gorunur not
    EF_CHECK(r.rows[0].verdict == BalanceVerdict::GpuLimited);        // GPU 100 fps < CPU 200 fps, GPU mesgul
    // NVIDIA'da ayni girdi not uretmez.
    auto nv = in;
    nv.confounders.vendor_limit_no_api = vendor_limit_api_missing(kPciNvidia);
    EF_CHECK(!has(evaluate_balance(nv).notes, balance_note::kVendorLimitNoApi));
}

EF_TEST(amd_denge_fixture_gpu_timestamps_unusable_rows_stay_undetermined_without_invented_numbers) {
    // Her adimda karelerin %6'sinda zaman damgasi gecersiz (AMD sahadaki durum). GPU suresine dayanan adimlar gecersiz;
    // CPU adimi gecerli kalir (denge_v4) ama GPU mesguliyeti bos ve CPU degeri "en az".
    const auto in = amd_inputs(/*cpu_disjoint=*/80, /*gpu_disjoint=*/80);
    EF_CHECK(in.cpu.state == StepState::Valid && !in.cpu.gpu_busy_ratio.has_value());
    EF_CHECK(in.gpu[0].state == StepState::Invalid && in.gpu[0].reason == balance_reason::kTimestampDisjoint);
    const auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason.empty());       // genel neden yok: CPU adimi gecerli
    EF_CHECK(r.cpu_fps.has_value() && r.cpu_lower_bound);  // "en az" olarak isaretli
    for (const auto& row : r.rows) {
        EF_CHECK(row.verdict == BalanceVerdict::Undetermined);
        EF_CHECK(row.reason == balance_reason::kGpuStepInvalid);  // tek, somut neden anahtari
        EF_CHECK(!row.gpu_fps && !row.ratio && !row.coupled_ratio);  // gecersiz adimin degeri gosterilmez
    }
    EF_CHECK(has(r.notes, balance_note::kVendorLimitNoApi));
    // Gecmise yalniz olculen degerler yazilir; GPU FPS yok, her satir hukum kodu 0 (belirlenemedi).
    const auto metrics = balance_history_metrics(r);
    for (const auto& [key, value] : metrics) {
        EF_CHECK(key.rfind("balance.gpu_fps_", 0) != 0);
        EF_CHECK(key.rfind("balance.coupled_fps_", 0) != 0);
        if (key.rfind("balance.verdict_", 0) == 0) EF_CHECK(value == 0.0);
    }
}

EF_TEST(amd_denge_fixture_cpu_step_invalid_reports_the_specific_cause) {
    // CPU adimi bu kez yetersiz kare (10 sn degil 0,2 sn): genel neden cpu_step_invalid + adimin kendi nedeni.
    auto in = amd_inputs(0, 0);
    const auto few = frames(5.0, 0.1);  // 20 kare
    in.cpu = summarize_step(few, busy(few.size(), 1.0), kMeasureS, 0, false);
    EF_CHECK(in.cpu.state == StepState::Invalid);
    const auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kCpuStepInvalid);
    EF_CHECK(r.cpu_step_reason == balance_reason::kTooFewFrames);
    for (const auto& row : r.rows) EF_CHECK(row.verdict == BalanceVerdict::Undetermined && row.reason == balance_reason::kCpuStepInvalid);
    EF_CHECK(!r.cpu_fps.has_value());
}

EF_TEST(amd_denge_fixture_lower_bound_cpu_blocks_balanced_and_cpu_verdicts_but_not_gpu) {
    // GPU zaman damgasi bozuk -> CPU degeri "en az". Yalniz hukmu GUCLENDIREN yonde (GPU bandi) kullanilir.
    auto in = amd_inputs(0, 0);
    in.cpu = cpu_step(200.0, 80);
    in.cpu_repeat = cpu_step(200.0, 80);
    EF_CHECK(!in.cpu.gpu_busy_ratio.has_value());
    // 1080p: GPU 100 fps -> oran 0,5 -> GPU bandi: karar cikar.
    const auto gpu_band = evaluate_balance(in);
    EF_CHECK(gpu_band.cpu_lower_bound);
    EF_CHECK(gpu_band.rows[0].verdict == BalanceVerdict::GpuLimited);
    // GPU de 200 fps (oran 1,0 -> dengeli bandi): CPU alt sinir oldugu icin hukum kurulamaz.
    auto bal = in;
    for (std::size_t i = 0; i < kBalanceRows; ++i) {
        bal.gpu[i] = gpu_step(200.0, 0);
        bal.coupled[i] = coupled_step(200.0, 0.50, 0);
    }
    bal.gpu_repeat = gpu_step(200.0, 0);
    const auto r = evaluate_balance(bal);
    EF_CHECK(r.cpu_lower_bound);
    for (const auto& row : r.rows) EF_CHECK(row.verdict == BalanceVerdict::Undetermined && row.reason == balance_reason::kCpuLowerBound);
}

EF_TEST(amd_denge_fixture_integrated_gpu_run_is_undetermined_for_all_rows) {
    // Hibrit sistemde (AMD iGPU + ayrik kart) test iGPU'da kosarsa sonuc bilgisayarin oyun gucunu gostermez.
    auto in = amd_inputs(0, 0);
    in.confounders.ran_on_integrated_gpu = true;
    const auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kRanOnIntegratedGpu);
    for (const auto& row : r.rows) EF_CHECK(row.verdict == BalanceVerdict::Undetermined && row.reason == balance_reason::kRanOnIntegratedGpu);
    EF_CHECK(has(r.notes, balance_note::kVendorLimitNoApi));
}
