// Denge testi: adim istatistigi ve denge_v1 kurali (spec 2026-11-12 A2/A3). Her esik: hemen alti / tam / hemen ustu.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/diagnosis/balance.h"
#include "core/insights/history.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

StepStats ok(double fps, std::optional<double> busy) {
    StepStats s;
    s.state = StepState::Valid;
    s.fps = fps;
    s.frames = 1000;
    s.seconds = 8;
    s.cv = 0.01;
    s.gpu_busy_ratio = busy;
    return s;
}

StepStats invalid(double fps) {
    StepStats s = ok(fps, 0.5);
    s.state = StepState::Invalid;
    s.cv = 0.2;
    s.reason = balance_reason::kUnstable;
    return s;
}

// Temiz kosu: S1/T1 CPU kapasitesi dogrulanmis (GPU bos), S/T2 GPU kapasitesi dogrulanmis (GPU tam mesgul),
// K adimi min(cpu, gpu) hizinda ve hukumle uyumlu GPU mesguliyetinde, karistirici yok.
BalanceInputs clean(double cpu, std::array<double, 3> gpu) {
    BalanceInputs in;
    in.cpu = ok(cpu, 0.30);
    in.cpu_repeat = ok(cpu, 0.30);
    for (std::size_t i = 0; i < kBalanceRows; ++i) {
        in.gpu[i] = ok(gpu[i], 0.97);
        in.coupled[i] = ok(std::min(cpu, gpu[i]), gpu[i] < cpu ? 0.97 : 0.40);
    }
    in.gpu_repeat = ok(gpu[0], 0.97);
    in.confounders.other_cpu_pct = 2.0;
    in.confounders.other_gpu_pct = 0.0;  // olculdu: test bagdastiricisini baska surec kullanmiyor
    return in;
}

bool row_is(const BalanceResult& r, std::size_t i, BalanceVerdict v, std::string_view reason = {}) {
    return r.rows[i].verdict == v && r.rows[i].reason == reason;
}

bool has(const std::vector<std::string_view>& v, std::string_view code) { return std::find(v.begin(), v.end(), code) != v.end(); }

std::vector<double> steady(std::size_t n, double ms) { return std::vector<double>(n, ms); }

}  // namespace

// Esikler spec A2/A3 degerleridir; biri degisirse kural surumu (denge_v1) de degismelidir.
static_assert(kBalanceCpuLimitedRatio == 1.15 && kBalanceGpuLimitedRatio == 0.87);
static_assert(kBalanceCoupledMinRatio == 0.85 && kBalanceCoupledMaxRatio == 1.10);
static_assert(kBalanceCpuStepMaxGpuBusy == 0.60 && kBalanceGpuStepMinGpuBusy == 0.90);
static_assert(kBalanceCoupledCpuMaxGpuBusy == 0.90);
static_assert(kBalanceDriftTolerance == 0.03 && 1.0 - kBalanceDriftTolerance == 0.97);
static_assert(kBalanceStepMinFrames == 30 && kBalanceStepMaxCv == 0.05 && kBalanceStepSecondsSlack == 1.0);
static_assert(kBalanceOtherCpuMaxPct == 10.0 && kBalanceRows == 3);
static_assert(kBalanceOtherGpuMaxPct == 10.0);
static_assert(kBalanceStepMaxAttempts == 2 && kBalanceRunMaxRetries == 2);

EF_TEST(balance_rule_version_and_row_keys) {
    EF_CHECK(kBalanceRuleVersion == "denge_v4");  // 2026-11-17 CPU adimi disjoint (balance.h)
    EF_CHECK(kBalanceRowKeys[0] == "1080" && kBalanceRowKeys[1] == "1440" && kBalanceRowKeys[2] == "2160");
    EF_CHECK(evaluate_balance(BalanceInputs{}).rule == kBalanceRuleVersion);
}

// ---- Adim istatistigi ----

EF_TEST(balance_per_second_fps_steady_and_fractional) {
    const auto a = per_second_fps(steady(800, 10.0));  // 8 sn, 100 FPS
    EF_CHECK(a.size() == 8);
    for (const double v : a) EF_CHECK(near(v, 100.0));
    // Dusuk FPS'te kuantalama yok: 250 ms'lik kareler tam 4 FPS verir; eksik son saniye atilir.
    const auto b = per_second_fps(steady(10, 250.0));  // 2,5 sn
    EF_CHECK(b.size() == 2 && near(b[0], 4.0) && near(b[1], 4.0));
    // Sinir asan kare sureyle orantili paylastirilir (toplam kare korunur).
    const auto c = per_second_fps(std::vector<double>{1500.0, 500.0});
    EF_CHECK(c.size() == 2 && near(c[0], 1000.0 / 1500.0) && near(c[1], 500.0 / 1500.0 + 1.0));
    EF_CHECK(near(c[0] + c[1], 2.0));
    // 1 sn'den kisa pencere: ornek yok (sahte deger yok).
    EF_CHECK(per_second_fps(steady(99, 10.0)).empty());
    EF_CHECK(per_second_fps(steady(1999, 1.0)).size() == 1);
    EF_CHECK(per_second_fps({}).empty());
    // Gecersiz araliklar atlanir.
    std::vector<double> d = steady(200, 10.0);
    d.insert(d.begin() + 50, {0.0, -3.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()});
    const auto e = per_second_fps(d);
    EF_CHECK(e.size() == 2 && near(e[0], 100.0) && near(e[1], 100.0));
}

EF_TEST(balance_median_and_cv) {
    EF_CHECK(median_of({}) == 0.0);
    EF_CHECK(median_of({3.0, 1.0, 2.0}) == 2.0);
    EF_CHECK(median_of({4.0, 1.0, 3.0, 2.0}) == 2.5);  // cift: ortadaki ikisinin ortalamasi
    EF_CHECK(median_of({7.0}) == 7.0);
    // Medyan tek bir kotu saniyeden etkilenmez.
    EF_CHECK(median_of({100.0, 100.0, 20.0, 100.0, 100.0}) == 100.0);

    const std::vector<double> flat{100.0, 100.0, 100.0};
    EF_CHECK(coefficient_of_variation(flat).value_or(-1) == 0.0);
    const std::vector<double> two{90.0, 110.0};  // ortalama 100, ornek sapmasi sqrt(200)
    EF_CHECK(near(coefficient_of_variation(two).value_or(-1), std::sqrt(200.0) / 100.0));
    const std::vector<double> one{100.0};
    EF_CHECK(!coefficient_of_variation(one));
    EF_CHECK(!coefficient_of_variation({}));
    const std::vector<double> zero{0.0, 0.0};
    EF_CHECK(!coefficient_of_variation(zero));
}

EF_TEST(balance_warmup_discard_window) {
    const auto f = steady(100, 100.0);  // 10 sn
    auto w = measured_window(f, 2.0, 8.0);
    EF_CHECK(w.first == 20 && w.last == 100 && w.size() == 80);  // tam sinirda baslayan kare olcumdur
    w = measured_window(f, 2.0, 4.0);
    EF_CHECK(w.first == 20 && w.last == 60);
    w = measured_window(f, 0.0, 0.0);  // isinma yok, sinir yok
    EF_CHECK(w.first == 0 && w.last == 100);
    w = measured_window(f, 1.0, 0.0);
    EF_CHECK(w.first == 10 && w.last == 100);
    // Isinma bitmeden baslayan kare atilir: 300 ms'lik karelerde 1800'de baslayan isinmadir, 2100 olcumdur.
    const auto g = steady(40, 300.0);
    w = measured_window(g, 2.0, 3.0);
    EF_CHECK(w.first == 7);
    EF_CHECK(w.last == 17);  // 5100 >= 5000: ilk olcum disi kare
    // Veri isinmadan kisa: bos pencere.
    w = measured_window(steady(5, 100.0), 2.0, 8.0);
    EF_CHECK(w.size() == 0);
    EF_CHECK(measured_window({}, 2.0, 8.0).size() == 0);
}

EF_TEST(balance_step_validity_edges) {
    using namespace balance_reason;
    // Kare sayisi: 29 / 30 / 31.
    EF_CHECK(step_invalid_reason(29, 8, 0.01, 0, 8.0, true) == kTooFewFrames);
    EF_CHECK(step_invalid_reason(30, 8, 0.01, 0, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(31, 8, 0.01, 0, 8.0, true).empty());
    // Saniyelik ornek >= olcum suresi - 1: Tam (8 sn) icin 6 / 7 / 8, Hizli (4 sn) icin 2 / 3 / 4.
    EF_CHECK(step_invalid_reason(1000, 6, 0.01, 0, 8.0, true) == kTooFewSeconds);
    EF_CHECK(step_invalid_reason(1000, 7, 0.01, 0, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 0, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 2, 0.01, 0, 4.0, true) == kTooFewSeconds);
    EF_CHECK(step_invalid_reason(1000, 3, 0.01, 0, 4.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 4, 0.01, 0, 4.0, true).empty());
    // CV icin en az iki ornek: cok kisa olcumde de 1 ornek yetmez.
    EF_CHECK(step_invalid_reason(1000, 1, 0.0, 0, 2.0, true) == kTooFewSeconds);
    EF_CHECK(step_invalid_reason(1000, 2, 0.0, 0, 2.0, true).empty());
    // CV: 0,0499 / 0,05 / 0,0501.
    EF_CHECK(step_invalid_reason(1000, 8, 0.0499, 0, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 8, 0.05, 0, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 8, 0.0501, 0, 8.0, true) == kUnstable);
    EF_CHECK(step_invalid_reason(1000, 8, std::numeric_limits<double>::quiet_NaN(), 0, 8.0, true) == kUnstable);
    // denge_v2: zaman damgasi gecersiz kare payi %1'e kadar kabul (tam %1 dahil), ustu gecersiz.
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 0, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 1, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 10, 8.0, true).empty());
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 11, 8.0, true) == kTimestampDisjoint);
    EF_CHECK(step_invalid_reason(40, 8, 0.01, 1, 8.0, true) == kTimestampDisjoint);  // az karede tek kare de %1'i asar
}

EF_TEST(balance_gpu_busy_ratio) {
    const auto frames = steady(100, 10.0);
    EF_CHECK(near(gpu_busy_ratio(steady(100, 6.0), frames).value_or(-1), 0.6));
    EF_CHECK(near(gpu_busy_ratio(steady(100, 9.5), frames).value_or(-1), 0.95));
    EF_CHECK(gpu_busy_ratio(steady(100, 12.0), frames).value_or(-1) == 1.0);  // [0,1]'e kirpilir
    EF_CHECK(!gpu_busy_ratio({}, frames));   // GPU suresi olculmedi: bilinmiyor (0 degil)
    EF_CHECK(!gpu_busy_ratio(steady(10, 5.0), {}));
    const std::vector<double> bad{std::numeric_limits<double>::quiet_NaN(), -1.0};
    EF_CHECK(!gpu_busy_ratio(bad, frames));
}

EF_TEST(balance_summarize_step) {
    // 8 sn, 5 ms'lik sabit kareler, GPU kare basina 4,6 ms calisti.
    const auto s = summarize_step(steady(1600, 5.0), steady(1600, 4.6), 8.0, 0, true);
    EF_CHECK(s.state == StepState::Valid && s.reason.empty());
    EF_CHECK(s.frames == 1600 && s.seconds == 8 && near(s.fps, 200.0) && near(s.cv, 0.0));
    EF_CHECK(near(s.gpu_busy_ratio.value_or(-1), 0.92));
    // Saniyeler arasinda %20 oynama: kararsiz.
    std::vector<double> wobble;
    for (int sec = 0; sec < 8; ++sec) {
        const auto part = sec % 2 ? steady(80, 12.5) : steady(100, 10.0);
        wobble.insert(wobble.end(), part.begin(), part.end());
    }
    const auto u = summarize_step(wobble, {}, 8.0, 0, true);
    EF_CHECK(u.state == StepState::Invalid && u.reason == balance_reason::kUnstable && u.cv > kBalanceStepMaxCv);
    EF_CHECK(!u.gpu_busy_ratio);
    // Kisa kosu ve disjoint kare.
    const auto t = summarize_step(steady(500, 10.0), {}, 8.0, 0, true);
    EF_CHECK(t.state == StepState::Invalid && t.reason == balance_reason::kTooFewSeconds && t.seconds == 5);
    const auto d = summarize_step(steady(1600, 5.0), steady(1600, 4.6), 8.0, 17, true);  // %1,06
    EF_CHECK(d.state == StepState::Invalid && d.reason == balance_reason::kTimestampDisjoint && d.disjoint_frames == 17);
    const auto d2 = summarize_step(steady(1600, 5.0), steady(1600, 4.6), 8.0, 2, true);  // %0,125: gecerli (denge_v2)
    EF_CHECK(d2.state == StepState::Valid && d2.disjoint_frames == 2);
    const auto e = summarize_step({}, {}, 8.0, 0, true);
    EF_CHECK(e.state == StepState::Invalid && e.reason == balance_reason::kTooFewFrames && e.fps == 0.0 && e.seconds == 0);
    // Yavas ama kararli adim (4 FPS, 32 kare): gecerli; kuantalama CV'yi sisirmez.
    const auto slow = summarize_step(steady(32, 250.0), {}, 8.0, 0, true);
    EF_CHECK(slow.state == StepState::Valid && near(slow.fps, 4.0) && slow.seconds == 8);
}

// denge_v4: zaman damgasi gecersiz kare payi yalniz GPU suresine dayanan adimlari gecersiz kilar (S1/T1 haric).
EF_TEST(balance_disjoint_applies_only_to_gpu_time_steps) {
    using namespace balance_reason;
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 11, 8.0, false).empty());  // gpu_time_required=false: %1,1 gecersiz kare tolere
    EF_CHECK(step_invalid_reason(1000, 8, 0.01, 11, 8.0, true) == kTimestampDisjoint);
    // Diger gecerlilik kosullari CPU adiminda da gecerli kalir.
    EF_CHECK(step_invalid_reason(29, 8, 0.01, 500, 8.0, false) == kTooFewFrames);
    EF_CHECK(step_invalid_reason(1000, 8, 0.0501, 500, 8.0, false) == kUnstable);
    // ~%2 gecersiz kare (32 / 1600): S1/T1 (false) gecerli, oran bos; GPU suresine dayanan adim (true) gecersiz.
    const auto cpu = summarize_step(steady(1600, 5.0), steady(1600, 1.0), 8.0, 32, false);
    EF_CHECK(cpu.state == StepState::Valid && cpu.reason.empty() && cpu.disjoint_frames == 32);
    EF_CHECK(near(cpu.fps, 200.0) && !cpu.gpu_busy_ratio);
    const auto gpu = summarize_step(steady(1600, 5.0), steady(1600, 4.6), 8.0, 32, true);
    EF_CHECK(gpu.state == StepState::Invalid && gpu.reason == kTimestampDisjoint);
    // Esik asilmadiysa (%0,125) CPU adiminda oran korunur.
    const auto cpu_ok = summarize_step(steady(1600, 5.0), steady(1600, 1.0), 8.0, 2, false);
    EF_CHECK(cpu_ok.state == StepState::Valid && near(cpu_ok.gpu_busy_ratio.value_or(-1), 0.20));
    // evaluate_balance: oran bos -> sinir dogrulanmaz -> cpu_lower_bound ("en az"); CPU adimi gecerli oldugu icin genel neden yok.
    auto in = clean(100.0, {50.0, 100.0, 200.0});
    in.cpu = cpu;
    in.cpu.fps = 100.0;
    in.cpu_repeat = ok(100.0, 0.30);
    const auto r = evaluate_balance(in);
    EF_CHECK(r.cpu_lower_bound && r.cpu_fps.value_or(0) == 100.0 && r.global_reasons.empty());
    EF_CHECK(r.cpu_step_reason.empty() && r.cpu_repeat_reason.empty());
    // Gecersiz CPU adimi asil nedenini tasir; tekrar adimi icin ayri.
    auto bad = clean(100.0, {50.0, 100.0, 200.0});
    bad.cpu = summarize_step(steady(500, 10.0), {}, 8.0, 0, false);  // too_few_seconds
    const auto rb = evaluate_balance(bad);
    EF_CHECK(rb.global_reason == kCpuStepInvalid && rb.cpu_step_reason == kTooFewSeconds && rb.cpu_repeat_reason.empty());
    auto bad2 = clean(100.0, {50.0, 100.0, 200.0});
    bad2.cpu_repeat = summarize_step(steady(1600, 5.0), {}, 8.0, 0, false);  // gecerli degil: gpu_busy bos ama FPS 200 (sapma ayri)
    bad2.cpu_repeat.state = StepState::Invalid;
    bad2.cpu_repeat.reason = kUnstable;
    const auto rb2 = evaluate_balance(bad2);
    EF_CHECK(rb2.global_reason == kCpuRepeatInvalid && rb2.cpu_repeat_reason == kUnstable && rb2.cpu_step_reason.empty());
}

EF_TEST(balance_warmup_is_discarded_before_statistics) {
    // 2 sn isinma 20 ms'lik karelerle (50 FPS), sonra 8 sn 10 ms (100 FPS).
    std::vector<double> all = steady(100, 20.0);
    const auto rest = steady(800, 10.0);
    all.insert(all.end(), rest.begin(), rest.end());
    const auto w = measured_window(all, 2.0, 8.0);
    EF_CHECK(w.first == 100 && w.last == 900);
    const std::span<const double> measured(all.data() + w.first, w.size());
    const auto s = summarize_step(measured, {}, 8.0, 0, true);
    EF_CHECK(s.state == StepState::Valid && near(s.fps, 100.0) && s.seconds == 8);
    // Isinma atilmasaydi adim kararsiz gorunurdu.
    const auto raw = summarize_step(all, {}, 10.0, 0, true);
    EF_CHECK(raw.state == StepState::Invalid && raw.reason == balance_reason::kUnstable);
    const auto um = unmeasurable_step(balance_reason::kVramBudget);
    EF_CHECK(um.state == StepState::Unmeasurable && um.reason == balance_reason::kVramBudget && um.fps == 0.0);
    EF_CHECK(unmeasurable_step({}).reason == balance_reason::kNotRun);
}

// ---- Sinir dogrulamasi ----

EF_TEST(balance_bound_confirmation_edges) {
    // S1/T1: GPU mesgul orani <= 0,60.
    EF_CHECK(bound_confirmed(StepRole::CpuCapacity, 0.59));
    EF_CHECK(bound_confirmed(StepRole::CpuCapacity, 0.60));
    EF_CHECK(!bound_confirmed(StepRole::CpuCapacity, 0.61));
    // S2-S4/T2: GPU mesgul orani >= 0,90.
    EF_CHECK(!bound_confirmed(StepRole::GpuCapacity, 0.89));
    EF_CHECK(bound_confirmed(StepRole::GpuCapacity, 0.90));
    EF_CHECK(bound_confirmed(StepRole::GpuCapacity, 0.91));
    // Oran bilinmiyorsa dogrulanmaz (deger "en az" kalir).
    EF_CHECK(!bound_confirmed(StepRole::CpuCapacity, std::nullopt));
    EF_CHECK(!bound_confirmed(StepRole::GpuCapacity, std::nullopt));
    EF_CHECK(!bound_confirmed(StepRole::CpuCapacity, std::numeric_limits<double>::quiet_NaN()));
}

// ---- denge_v1 kurali ----

EF_TEST(balance_band_edges) {
    // oran = gpu_fps / cpu_fps; cpu 100. CPU bandi: 1,1499 / 1,15 / 1,1501.
    auto r = evaluate_balance(clean(100.0, {114.99, 115.0, 115.01}));
    EF_CHECK(row_is(r, 0, BalanceVerdict::Balanced));
    EF_CHECK(row_is(r, 1, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(r, 2, BalanceVerdict::CpuLimited));
    EF_CHECK(r.rows[1].ratio.value_or(0) == 1.15);
    // GPU bandi: 0,8701 / 0,87 / 0,8699.
    r = evaluate_balance(clean(100.0, {87.01, 87.0, 86.99}));
    EF_CHECK(row_is(r, 0, BalanceVerdict::Balanced));
    EF_CHECK(row_is(r, 1, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(r, 2, BalanceVerdict::GpuLimited));
    EF_CHECK(r.rows[1].ratio.value_or(0) == 0.87);
    // Tipik karisik sonuc: 1080p CPU, 1440p dengeli, 4K GPU.
    r = evaluate_balance(clean(120.0, {210.0, 125.0, 60.0}));
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 1, BalanceVerdict::Balanced) &&
              row_is(r, 2, BalanceVerdict::GpuLimited));
    EF_CHECK(r.global_reason.empty() && r.global_reasons.empty() && r.rule == "denge_v4");
    EF_CHECK(r.cpu_fps.value_or(0) == 120.0 && !r.cpu_lower_bound && !r.cpu_drift && !r.gpu_drift);
    EF_CHECK(r.rows[2].gpu_fps.value_or(0) == 60.0 && !r.rows[2].gpu_lower_bound && r.rows[2].coupled_fps.value_or(0) == 60.0);
    EF_CHECK(!r.cpu_fps_lo && !r.cpu_fps_hi && !r.rows[0].gpu_fps_lo && !r.rows[0].gpu_fps_hi);
}

EF_TEST(balance_coupled_ratio_edges) {
    // CPU sinirliyor (min = cpu = 100): birlikte hiz 84,99 / 85 / 85,01 ve 109,99 / 110 / 110,01.
    const auto run = [](double coupled) {
        auto in = clean(100.0, {200.0, 200.0, 200.0});
        in.coupled[1].fps = coupled;
        return evaluate_balance(in);
    };
    EF_CHECK(row_is(run(84.99), 1, BalanceVerdict::Undetermined, balance_reason::kCoupledMismatch));
    EF_CHECK(row_is(run(85.0), 1, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(run(85.01), 1, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(run(109.99), 1, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(run(110.0), 1, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(run(110.01), 1, BalanceVerdict::Undetermined, balance_reason::kCoupledMismatch));
    // Uyumsuzluk yalniz o satiri etkiler.
    const auto mixed = run(60.0);
    EF_CHECK(row_is(mixed, 0, BalanceVerdict::CpuLimited) && row_is(mixed, 2, BalanceVerdict::CpuLimited));
    EF_CHECK(near(mixed.rows[1].coupled_ratio.value_or(0), 0.6));
    // GPU sinirliyor (min = gpu = 50): 42,49 / 42,5 ve 55 / 55,01.
    const auto gpu_run = [](double coupled) {
        auto in = clean(100.0, {50.0, 50.0, 50.0});
        in.coupled[2].fps = coupled;
        return evaluate_balance(in);
    };
    EF_CHECK(row_is(gpu_run(42.49), 2, BalanceVerdict::Undetermined, balance_reason::kCoupledMismatch));
    EF_CHECK(row_is(gpu_run(42.5), 2, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(gpu_run(42.51), 2, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(gpu_run(54.99), 2, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(gpu_run(55.0), 2, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(gpu_run(55.01), 2, BalanceVerdict::Undetermined, balance_reason::kCoupledMismatch));
    // Dengeli satirda da birlikte hiz denetlenir.
    auto bal = clean(100.0, {100.0, 100.0, 100.0});
    bal.coupled[0].fps = 70.0;
    EF_CHECK(row_is(evaluate_balance(bal), 0, BalanceVerdict::Undetermined, balance_reason::kCoupledMismatch));
}

EF_TEST(balance_coupled_gpu_busy_edges) {
    // "GPU sinirliyor" icin K adiminda GPU mesgul orani >= 0,90: 0,89 / 0,90 / 0,91.
    const auto gpu_run = [](std::optional<double> busy) {
        auto in = clean(100.0, {50.0, 50.0, 50.0});
        in.coupled[0].gpu_busy_ratio = busy;
        return evaluate_balance(in);
    };
    EF_CHECK(row_is(gpu_run(0.89), 0, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyMismatch));
    EF_CHECK(row_is(gpu_run(0.90), 0, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(gpu_run(0.91), 0, BalanceVerdict::GpuLimited));
    EF_CHECK(row_is(gpu_run(std::nullopt), 0, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyUnknown));
    // "CPU sinirliyor" icin K adiminda GPU doymamis olmali: < 0,90 (uc haric). Kapasite adimi esigi (0,60) burada
    // gecerli degildir: CPU bandinda beklenen GPU mesgul orani ~ 1 / oran'dir (oran 1,30 -> ~0,77).
    const auto cpu_run = [](std::optional<double> busy) {
        auto in = clean(100.0, {200.0, 200.0, 200.0});
        in.coupled[2].gpu_busy_ratio = busy;
        return evaluate_balance(in);
    };
    EF_CHECK(row_is(cpu_run(0.59), 2, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(cpu_run(0.60), 2, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(cpu_run(0.61), 2, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(cpu_run(0.74), 2, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(cpu_run(0.89), 2, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(cpu_run(0.8999), 2, BalanceVerdict::CpuLimited));
    EF_CHECK(row_is(cpu_run(0.90), 2, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyMismatch));
    EF_CHECK(row_is(cpu_run(0.91), 2, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyMismatch));
    EF_CHECK(row_is(cpu_run(1.0), 2, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyMismatch));
    EF_CHECK(row_is(cpu_run(std::nullopt), 2, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyUnknown));
    // Kapasite adimi (S1 / T1) esigi degismedi: 0,60 tutar, 0,61 tutmaz.
    EF_CHECK(bound_confirmed(StepRole::CpuCapacity, 0.60) && !bound_confirmed(StepRole::CpuCapacity, 0.61));
    // "Dengeli" hukmu GPU mesgul oranina bagli degildir.
    auto bal = clean(100.0, {100.0, 100.0, 100.0});
    bal.coupled[1].gpu_busy_ratio = std::nullopt;
    bal.coupled[2].gpu_busy_ratio = 0.75;
    const auto r = evaluate_balance(bal);
    EF_CHECK(row_is(r, 1, BalanceVerdict::Balanced) && row_is(r, 2, BalanceVerdict::Balanced));
}

// Gerileme (gercek cihaz, RTX 5080, Hizli kosu): S1 cpu_fps ~1194, S2 gpu_fps ~1553 (oran 1,30: CPU bandi), K1 birlikte
// ~1170 (birlikte oran 0,98), K1 GPU mesgul orani 0,73. Eski "<= 0,60" denetimi satiri "Belirlenemedi
// (coupled_busy_mismatch)" yapiyordu; beklenen mesgul oran ~ 1 / 1,30 = 0,77 oldugu icin bu satir "CPU sinirliyor"dur.
EF_TEST(balance_coupled_cpu_band_accepts_unsaturated_gpu_regression) {
    BalanceInputs in = clean(1194.0, {1553.0, 911.0, 416.0});
    in.cpu.gpu_busy_ratio = 0.086;
    in.cpu_repeat = in.cpu;
    in.gpu[0].gpu_busy_ratio = 0.994;
    in.gpu_repeat = in.gpu[0];
    in.coupled[0].fps = 1170.0;
    in.coupled[0].gpu_busy_ratio = 0.73;
    in.coupled[1].fps = 957.0;
    in.coupled[1].gpu_busy_ratio = 0.993;
    in.coupled[2].fps = 442.0;
    in.coupled[2].gpu_busy_ratio = 0.997;
    const auto r = evaluate_balance(in);
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited));
    EF_CHECK(near(r.rows[0].ratio.value_or(0), 1553.0 / 1194.0) && near(r.rows[0].coupled_ratio.value_or(0), 1170.0 / 1194.0));
    EF_CHECK(row_is(r, 1, BalanceVerdict::GpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    // Ayni satirda GPU birlikte adimda doymus olsaydi (>= 0,90) "CPU sinirliyor" denmezdi.
    in.coupled[0].gpu_busy_ratio = 0.95;
    EF_CHECK(row_is(evaluate_balance(in), 0, BalanceVerdict::Undetermined, balance_reason::kCoupledBusyMismatch));
}

EF_TEST(balance_cpu_lower_bound_logic) {
    // S1'de GPU bos degildi (0,61 > 0,60): cpu_fps "en az". Satirlar: GPU / dengeli / CPU.
    auto in = clean(100.0, {50.0, 100.0, 200.0});
    in.cpu.gpu_busy_ratio = 0.61;
    auto r = evaluate_balance(in);
    EF_CHECK(r.cpu_lower_bound && r.cpu_fps.value_or(0) == 100.0);
    EF_CHECK(row_is(r, 0, BalanceVerdict::GpuLimited));  // gercek CPU hizi daha yuksekse hukum guclenir
    EF_CHECK(row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kCpuLowerBound));
    EF_CHECK(row_is(r, 2, BalanceVerdict::Undetermined, balance_reason::kCpuLowerBound));
    // Sinirda (0,60) alt sinir isareti yok.
    in.cpu.gpu_busy_ratio = 0.60;
    r = evaluate_balance(in);
    EF_CHECK(!r.cpu_lower_bound && row_is(r, 1, BalanceVerdict::Balanced) && row_is(r, 2, BalanceVerdict::CpuLimited));
    // Yalniz tekrar (T1) dogrulanmadiysa da "en az".
    in.cpu_repeat.gpu_busy_ratio = 0.80;
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_lower_bound && row_is(r, 0, BalanceVerdict::GpuLimited) &&
              row_is(r, 2, BalanceVerdict::Undetermined, balance_reason::kCpuLowerBound));
    // GPU suresi olculemedi: dogrulanamaz, "en az".
    in = clean(100.0, {50.0, 100.0, 200.0});
    in.cpu.gpu_busy_ratio = std::nullopt;
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_lower_bound && row_is(r, 0, BalanceVerdict::GpuLimited) &&
              row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kCpuLowerBound));
}

EF_TEST(balance_gpu_lower_bound_logic) {
    // GPU adiminda GPU tam mesgul degildi (0,89 < 0,90): gpu_fps "en az". Satirlar: CPU / dengeli / GPU.
    auto in = clean(100.0, {200.0, 100.0, 50.0});
    for (auto& g : in.gpu) g.gpu_busy_ratio = 0.89;
    auto r = evaluate_balance(in);
    EF_CHECK(r.rows[0].gpu_lower_bound && r.rows[1].gpu_lower_bound && r.rows[2].gpu_lower_bound && !r.cpu_lower_bound);
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited));  // gercek GPU hizi daha yuksekse hukum guclenir
    EF_CHECK(row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kGpuLowerBound));
    EF_CHECK(row_is(r, 2, BalanceVerdict::Undetermined, balance_reason::kGpuLowerBound));
    // Sinirda (0,90) isaret yok; isaret yalniz kendi satirini etkiler.
    in = clean(100.0, {200.0, 100.0, 50.0});
    in.gpu[1].gpu_busy_ratio = 0.90;
    in.gpu[2].gpu_busy_ratio = 0.50;
    r = evaluate_balance(in);
    EF_CHECK(!r.rows[0].gpu_lower_bound && !r.rows[1].gpu_lower_bound && r.rows[2].gpu_lower_bound);
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 1, BalanceVerdict::Balanced) &&
              row_is(r, 2, BalanceVerdict::Undetermined, balance_reason::kGpuLowerBound));
    // T2 dogrulanmadiysa yalniz 1080p satiri "en az".
    in = clean(100.0, {50.0, 50.0, 50.0});
    in.gpu_repeat.gpu_busy_ratio = 0.70;
    r = evaluate_balance(in);
    EF_CHECK(r.rows[0].gpu_lower_bound && !r.rows[1].gpu_lower_bound);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kGpuLowerBound) && row_is(r, 1, BalanceVerdict::GpuLimited));
    // Iki taraf da "en az": hicbir hukum guclenmez.
    in = clean(100.0, {200.0, 100.0, 50.0});
    in.cpu.gpu_busy_ratio = 0.95;
    for (auto& g : in.gpu) g.gpu_busy_ratio = 0.30;
    r = evaluate_balance(in);
    for (std::size_t i = 0; i < kBalanceRows; ++i) EF_CHECK(r.rows[i].verdict == BalanceVerdict::Undetermined);
    EF_CHECK(r.rows[0].reason == balance_reason::kCpuLowerBound && r.rows[2].reason == balance_reason::kGpuLowerBound);
}

EF_TEST(balance_drift_threshold_edges) {
    // T1 ile S1 farki: tam %3 aralik degildir; %3,1 araliktir (iki yonde).
    auto in = clean(100.0, {200.0, 200.0, 200.0});
    in.cpu_repeat.fps = 97.0;
    auto r = evaluate_balance(in);
    EF_CHECK(!r.cpu_drift && !r.cpu_fps_lo && !r.cpu_fps_hi && !has(r.notes, balance_note::kCpuDrift));
    in.cpu_repeat.fps = 97.01;
    EF_CHECK(!evaluate_balance(in).cpu_drift);
    in.cpu_repeat.fps = 96.9;
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_drift && r.cpu_fps_lo.value_or(0) == 96.9 && r.cpu_fps_hi.value_or(0) == 100.0);
    EF_CHECK(has(r.notes, balance_note::kCpuDrift) && r.cpu_fps.value_or(0) == 100.0);
    in.cpu_repeat.fps = 103.0;  // 100/103 = 0,9709: aralik degil
    EF_CHECK(!evaluate_balance(in).cpu_drift);
    in.cpu_repeat.fps = 103.2;  // 100/103,2 = 0,9690: aralik
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_drift && r.cpu_fps_lo.value_or(0) == 100.0 && r.cpu_fps_hi.value_or(0) == 103.2);
    // Aralik bandin icinde kaliyorsa hukum degismez.
    for (std::size_t i = 0; i < kBalanceRows; ++i) EF_CHECK(row_is(r, i, BalanceVerdict::CpuLimited));
    // T2 ile S2: ayni esik.
    in = clean(100.0, {50.0, 50.0, 50.0});
    in.gpu_repeat.fps = 48.5;  // 0,97
    r = evaluate_balance(in);
    EF_CHECK(!r.gpu_drift && !r.rows[0].gpu_fps_lo);
    in.gpu_repeat.fps = 48.4;
    r = evaluate_balance(in);
    EF_CHECK(r.gpu_drift && r.rows[0].gpu_fps_lo.value_or(0) == 48.4 && r.rows[0].gpu_fps_hi.value_or(0) == 50.0);
    EF_CHECK(has(r.notes, balance_note::kGpuDrift) && !r.rows[1].gpu_fps_lo);
    for (std::size_t i = 0; i < kBalanceRows; ++i) EF_CHECK(row_is(r, i, BalanceVerdict::GpuLimited));
}

EF_TEST(balance_drift_range_crossing_band) {
    // CPU suruklenmesi %4 (100 -> 96). 112/100 = 1,12 dengeli; 112/96 = 1,167 CPU: iki uc farkli bantta.
    auto in = clean(100.0, {112.0, 200.0, 50.0});
    in.cpu_repeat.fps = 96.0;
    auto r = evaluate_balance(in);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kDrift));
    EF_CHECK(near(r.rows[0].ratio_lo.value_or(0), 1.12) && near(r.rows[0].ratio_hi.value_or(0), 112.0 / 96.0));
    // Banttan uzak satirlar hukmunu korur.
    EF_CHECK(row_is(r, 1, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    // Ayni fark %3'un icindeyken (100 -> 97) aralik yoktur; hukum ilk olcumden verilir.
    in.cpu_repeat.fps = 97.0;
    EF_CHECK(row_is(evaluate_balance(in), 0, BalanceVerdict::Balanced));
    // GPU bandinin kenari: 90/100 = 0,90 dengeli; tekrar daha hizli (104) -> 90/104 = 0,865 GPU.
    in = clean(100.0, {90.0, 90.0, 200.0});
    in.cpu_repeat.fps = 104.0;
    r = evaluate_balance(in);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kDrift) &&
              row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kDrift) && row_is(r, 2, BalanceVerdict::CpuLimited));
    // GPU suruklenmesi (S2 112, T2 118): 1080p satiri olculen aralikla, diger satirlar ayni oranla degerlendirilir.
    in = clean(100.0, {112.0, 113.0, 50.0});
    in.gpu_repeat.fps = 118.0;
    r = evaluate_balance(in);
    EF_CHECK(r.gpu_drift && r.rows[0].gpu_fps_lo.value_or(0) == 112.0 && r.rows[0].gpu_fps_hi.value_or(0) == 118.0);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kDrift));
    EF_CHECK(row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kDrift));
    EF_CHECK(near(r.rows[1].ratio_lo.value_or(0), 1.13 * 112.0 / 118.0) && near(r.rows[1].ratio_hi.value_or(0), 1.13 * 118.0 / 112.0));
    EF_CHECK(!r.rows[1].gpu_fps_lo && !r.rows[1].gpu_fps_hi);  // olculmeyen aralik gosterilmez
    EF_CHECK(row_is(r, 2, BalanceVerdict::GpuLimited));
    // Iki taraf birden suruklenirse araliklar birlesir.
    in = clean(100.0, {106.0, 200.0, 50.0});
    in.cpu_repeat.fps = 96.5;
    in.gpu_repeat.fps = 112.0;
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_drift && r.gpu_drift && near(r.rows[0].ratio_lo.value_or(0), 1.06) &&
              near(r.rows[0].ratio_hi.value_or(0), 112.0 / 96.5));
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kDrift));  // 1,06 dengeli .. 1,16 CPU
    EF_CHECK(row_is(r, 1, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    // Tek tarafin araligi bandi asmiyorsa (1,06 .. 1,098) hukum korunur.
    in.gpu_repeat.fps = 106.0;
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_drift && !r.gpu_drift && row_is(r, 0, BalanceVerdict::Balanced));
}

EF_TEST(balance_confounders_force_undetermined) {
    const auto all_undetermined = [](const BalanceResult& r, std::string_view reason) {
        bool okay = r.global_reason == reason && !r.global_reasons.empty() && r.global_reasons.front() == reason;
        for (const auto& row : r.rows) okay = okay && row.verdict == BalanceVerdict::Undetermined && row.reason == reason;
        return okay;
    };
    const auto base = clean(120.0, {210.0, 125.0, 60.0});
    auto in = base;
    in.confounders.on_battery = true;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kOnBattery));
    in = base;
    in.confounders.battery_saver = true;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kBatterySaver));
    in = base;
    in.confounders.power_saver_plan = true;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kPowerSaverPlan));
    in = base;
    in.confounders.vendor_throttle = true;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kVendorThrottle));
    // Baska sureclerin CPU kullanimi: 9,9 / 10 / 10,1.
    in = base;
    in.confounders.other_cpu_pct = 9.9;
    auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason.empty() && row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    in.confounders.other_cpu_pct = 10.0;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kBackgroundCpu));
    in.confounders.other_cpu_pct = 10.1;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kBackgroundCpu));
    // Okunamadiysa iddia yok: hukum verilir, not dusulur.
    in.confounders.other_cpu_pct = std::nullopt;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason.empty() && row_is(r, 0, BalanceVerdict::CpuLimited) && has(r.notes, balance_note::kBackgroundCpuUnknown));
    EF_CHECK(!has(evaluate_balance(base).notes, balance_note::kBackgroundCpuUnknown));
    // Olculen degerler cekimserlikte de tasinir (uydurma degil, olcum).
    in = base;
    in.confounders.on_battery = true;
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_fps.value_or(0) == 120.0 && r.rows[0].gpu_fps.value_or(0) == 210.0 && near(r.rows[0].ratio.value_or(0), 1.75));
}

EF_TEST(balance_invalid_cpu_step_and_repeat) {
    const auto base = clean(120.0, {210.0, 125.0, 60.0});
    // Gecersiz S1.
    auto in = base;
    in.cpu = invalid(120.0);
    auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kCpuStepInvalid && !r.cpu_fps);
    for (const auto& row : r.rows)
        EF_CHECK(row.verdict == BalanceVerdict::Undetermined && row.reason == balance_reason::kCpuStepInvalid && !row.ratio);
    EF_CHECK(r.rows[0].gpu_fps.value_or(0) == 210.0);
    // S1 hic kosmadi / FPS 0.
    in = base;
    in.cpu = StepStats{};
    EF_CHECK(evaluate_balance(in).global_reason == balance_reason::kCpuStepInvalid);
    in = base;
    in.cpu.fps = 0.0;
    EF_CHECK(evaluate_balance(in).global_reason == balance_reason::kCpuStepInvalid);
    // T1 gecersiz: suruklenme denetlenemedi.
    in = base;
    in.cpu_repeat = invalid(118.0);
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kCpuRepeatInvalid && r.cpu_fps.value_or(0) == 120.0);
    for (const auto& row : r.rows) EF_CHECK(row.verdict == BalanceVerdict::Undetermined);
    // Oncelik: ortam nedeni gecersiz S1'den once gosterilir; hepsi listelenir.
    in = base;
    in.cpu = invalid(120.0);
    in.confounders.on_battery = true;
    in.confounders.other_cpu_pct = 35.0;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kOnBattery && r.global_reasons.size() == 3);
    EF_CHECK(has(r.global_reasons, balance_reason::kBackgroundCpu) && has(r.global_reasons, balance_reason::kCpuStepInvalid));
    // Genel cekimserlikte olculemeyen satir da "Belirlenemedi" olur (spec A3: tum satirlar).
    in = base;
    in.gpu[2] = unmeasurable_step(balance_reason::kVramBudget);
    in.confounders.power_saver_plan = true;
    EF_CHECK(row_is(evaluate_balance(in), 2, BalanceVerdict::Undetermined, balance_reason::kPowerSaverPlan));
}

EF_TEST(balance_row_level_step_states) {
    const auto base = clean(120.0, {210.0, 125.0, 60.0});
    // 4K video bellegine sigmadi: o satir "olculemedi", digerleri surer. GPU siniri hukmu sayilmaz.
    auto in = base;
    in.gpu[2] = unmeasurable_step(balance_reason::kVramBudget);
    in.coupled[2] = unmeasurable_step(balance_reason::kVramBudget);
    auto r = evaluate_balance(in);
    EF_CHECK(row_is(r, 2, BalanceVerdict::Unmeasurable, balance_reason::kVramBudget) && !r.rows[2].gpu_fps && !r.rows[2].ratio);
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 1, BalanceVerdict::Balanced));
    in.gpu[2] = unmeasurable_step(balance_reason::kTargetFailed);
    EF_CHECK(row_is(evaluate_balance(in), 2, BalanceVerdict::Unmeasurable, balance_reason::kTargetFailed));
    in.gpu[2] = StepStats{};  // hic kosmadi
    EF_CHECK(row_is(evaluate_balance(in), 2, BalanceVerdict::Unmeasurable, balance_reason::kNotRun));
    // Hedef yalniz K adiminda kaybedildi (S3 olculdu, K2 olusturulamadi): satir yine "olculemedi"dir, "gecersiz
    // olcum" degil (spec A2). Olculen S degeri tasinir; diger satirlar surer.
    in = base;
    in.coupled[1] = unmeasurable_step(balance_reason::kTargetFailed);
    r = evaluate_balance(in);
    EF_CHECK(row_is(r, 1, BalanceVerdict::Unmeasurable, balance_reason::kTargetFailed));
    EF_CHECK(r.rows[1].gpu_fps.value_or(0) == 125.0 && !r.rows[1].coupled_fps && !r.rows[1].coupled_ratio);
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited) && r.global_reason.empty());
    in.coupled[1] = unmeasurable_step(balance_reason::kVramBudget);
    EF_CHECK(row_is(evaluate_balance(in), 1, BalanceVerdict::Unmeasurable, balance_reason::kVramBudget));
    in.coupled[1] = StepStats{};  // K adimi hic kosmadi
    EF_CHECK(row_is(evaluate_balance(in), 1, BalanceVerdict::Unmeasurable, balance_reason::kNotRun));
    // Iki adim da olculemediyse S adiminin nedeni gosterilir; S gecersiz + K olculemedi yine "olculemedi".
    in.gpu[1] = unmeasurable_step(balance_reason::kVramBudget);
    in.coupled[1] = unmeasurable_step(balance_reason::kTargetFailed);
    EF_CHECK(row_is(evaluate_balance(in), 1, BalanceVerdict::Unmeasurable, balance_reason::kVramBudget));
    in.gpu[1] = invalid(125.0);
    EF_CHECK(row_is(evaluate_balance(in), 1, BalanceVerdict::Unmeasurable, balance_reason::kTargetFailed));
    // Olculemeyen satir gecmise 4 koduyla yazilir; olculmeyen K degeri yazilmaz.
    in = base;
    in.coupled[1] = unmeasurable_step(balance_reason::kTargetFailed);
    bool code_ok = false, coupled_written = false;
    for (const auto& [k, v] : balance_history_metrics(evaluate_balance(in))) {
        if (k == "balance.verdict_1440") code_ok = v == 4.0;
        if (k == "balance.coupled_fps_1440") coupled_written = true;
    }
    EF_CHECK(code_ok && !coupled_written);
    // GPU adimi kararsiz.
    in = base;
    in.gpu[1] = invalid(125.0);
    r = evaluate_balance(in);
    EF_CHECK(row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kGpuStepInvalid) && !r.rows[1].gpu_fps);
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    // Birlikte adimi kararsiz.
    in = base;
    in.coupled[0] = invalid(118.0);
    r = evaluate_balance(in);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kCoupledStepInvalid) && !r.rows[0].coupled_fps);
    EF_CHECK(row_is(r, 1, BalanceVerdict::Balanced));
    // T2 gecersiz: GPU suruklenmesi denetlenemedi; hukum verilebilecek satirlar cekimser, olculemeyen ayni kalir.
    in = base;
    in.gpu_repeat = invalid(200.0);
    in.gpu[2] = unmeasurable_step(balance_reason::kVramBudget);
    r = evaluate_balance(in);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kGpuRepeatInvalid) &&
              row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kGpuRepeatInvalid));
    EF_CHECK(row_is(r, 2, BalanceVerdict::Unmeasurable, balance_reason::kVramBudget) && r.global_reason.empty());
    // S2 gecersiz, T2 gecerli: karsilastirilacak ilk olcum yok; 1080p kendi nedeniyle, digerleri suruklenme
    // denetlenemedigi icin cekimser (T2 degeri S2 yerine KULLANILMAZ).
    in = base;
    in.gpu[0] = invalid(210.0);
    r = evaluate_balance(in);
    EF_CHECK(row_is(r, 0, BalanceVerdict::Undetermined, balance_reason::kGpuStepInvalid) && !r.rows[0].gpu_fps && !r.gpu_drift);
    EF_CHECK(row_is(r, 1, BalanceVerdict::Undetermined, balance_reason::kGpuRepeatInvalid) &&
              row_is(r, 2, BalanceVerdict::Undetermined, balance_reason::kGpuRepeatInvalid));
}

EF_TEST(balance_notes_and_cpu_shares) {
    EF_CHECK(!cpu_shares(CpuFrameSplit{}));
    EF_CHECK(!cpu_shares(CpuFrameSplit{-1.0, 1.0, 1.0}));
    EF_CHECK(!cpu_shares(CpuFrameSplit{std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0}));
    const auto s = cpu_shares(CpuFrameSplit{2.0, 0.5, 1.5});
    EF_CHECK(s && near(s->sim, 0.5) && near(s->draw, 0.5));
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    in.cpu_split = CpuFrameSplit{1.0, 1.0, 2.0};
    in.cpu_speed_drop = true;
    const auto r = evaluate_balance(in);
    EF_CHECK(near(r.cpu_sim_share.value_or(0), 0.25) && near(r.cpu_draw_share.value_or(0), 0.75));
    EF_CHECK(has(r.notes, balance_note::kCpuSpeedDrop));
    // Notlar hukmu engellemez.
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    const auto plain = evaluate_balance(clean(120.0, {210.0, 125.0, 60.0}));
    EF_CHECK(plain.notes.empty() && !plain.cpu_sim_share && !plain.cpu_draw_share);
}

EF_TEST(balance_history_codes_and_keys) {
    // Kalici kodlar (gecmis dosyasinda saklanir; degismez).
    EF_CHECK(verdict_code(BalanceVerdict::Undetermined) == 0 && verdict_code(BalanceVerdict::CpuLimited) == 1);
    EF_CHECK(verdict_code(BalanceVerdict::Balanced) == 2 && verdict_code(BalanceVerdict::GpuLimited) == 3);
    EF_CHECK(verdict_code(BalanceVerdict::Unmeasurable) == 4);
    for (const auto v : {BalanceVerdict::Undetermined, BalanceVerdict::CpuLimited, BalanceVerdict::Balanced,
                         BalanceVerdict::GpuLimited, BalanceVerdict::Unmeasurable})
        EF_CHECK(verdict_from_code(verdict_code(v)) == v);
    EF_CHECK(!verdict_from_code(5) && !verdict_from_code(-1) && !verdict_from_code(1.5));
    EF_CHECK(to_string(BalanceVerdict::CpuLimited) == "cpu" && to_string(BalanceVerdict::GpuLimited) == "gpu");
    EF_CHECK(to_string(BalanceVerdict::Balanced) == "balanced" && to_string(BalanceVerdict::Undetermined) == "undetermined");
    EF_CHECK(to_string(BalanceVerdict::Unmeasurable) == "unmeasurable" && to_string(StepState::Valid) == "valid");

    auto in = clean(120.0, {210.0, 125.0, 60.0});
    in.gpu[2] = unmeasurable_step(balance_reason::kVramBudget);
    in.coupled[2] = unmeasurable_step(balance_reason::kVramBudget);
    in.gpu[0].gpu_busy_ratio = 0.5;  // 1080p "en az" (CPU hukmu yine gecerli)
    const auto m = balance_history_metrics(evaluate_balance(in));
    const auto get = [&](std::string_view key) -> std::optional<double> {
        for (const auto& [k, v] : m)
            if (k == key) return v;
        return std::nullopt;
    };
    for (const auto& [k, v] : m) EF_CHECK(ef::core::insights::safe_metric_key(k) && std::isfinite(v));
    EF_CHECK(get("balance.cpu_fps") == 120.0 && get("balance.cpu_lower_bound") == 0.0);
    EF_CHECK(get("balance.gpu_fps_1080") == 210.0 && get("balance.gpu_lower_bound_1080") == 1.0);
    EF_CHECK(get("balance.gpu_fps_1440") == 125.0 && get("balance.gpu_lower_bound_1440") == 0.0);
    EF_CHECK(get("balance.coupled_fps_1080") == 120.0 && get("balance.coupled_fps_1440") == 120.0);
    EF_CHECK(get("balance.verdict_1080") == 1.0 && get("balance.verdict_1440") == 2.0 && get("balance.verdict_2160") == 4.0);
    // Olculmeyen deger yazilmaz (0 olarak da yazilmaz).
    EF_CHECK(!get("balance.gpu_fps_2160") && !get("balance.coupled_fps_2160") && !get("balance.gpu_lower_bound_2160"));
    // Gecersiz S1: cpu_fps yok, hukum kodlari yine yazilir.
    in.cpu = invalid(120.0);
    const auto m2 = balance_history_metrics(evaluate_balance(in));
    bool cpu_written = false;
    int verdicts = 0;
    for (const auto& [k, v] : m2) {
        if (k == "balance.cpu_fps") cpu_written = true;
        if (k.rfind("balance.verdict_", 0) == 0) {
            ++verdicts;
            EF_CHECK(v == 0.0);
        }
    }
    EF_CHECK(!cpu_written && verdicts == 3);
}

// P6: test tumlesik kartta kostu ve sistemde ayrik kart var -> tum satirlar Belirlenemedi (acik neden).
EF_TEST(balance_ran_on_integrated_gpu_is_global_reason) {
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    EF_CHECK(evaluate_balance(in).global_reason.empty());
    in.confounders.ran_on_integrated_gpu = true;
    const auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kRanOnIntegratedGpu);
    EF_CHECK(std::all_of(r.rows.begin(), r.rows.end(), [](const BalanceRow& row) {
        return row.verdict == BalanceVerdict::Undetermined && row.reason == balance_reason::kRanOnIntegratedGpu;
    }));
}

// D6: arka plan GPU karistiricisi (ilk adimdan once, test bagdastiricisinda baska sureclerin 3D kullanimi).
EF_TEST(balance_background_gpu_confounder_edges) {
    const auto base = clean(120.0, {210.0, 125.0, 60.0});
    const auto all_undetermined = [](const BalanceResult& r, std::string_view reason) {
        return r.global_reason == reason && std::all_of(r.rows.begin(), r.rows.end(), [&](const BalanceRow& row) {
            return row.verdict == BalanceVerdict::Undetermined && row.reason == reason;
        });
    };
    // 9,9 / 10 / 10,1.
    auto in = base;
    in.confounders.other_gpu_pct = 9.9;
    auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason.empty() && r.global_reasons.empty() && row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    EF_CHECK(!has(r.notes, balance_note::kBackgroundGpuUnknown));
    in.confounders.other_gpu_pct = 10.0;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kBackgroundGpu));
    in.confounders.other_gpu_pct = 10.1;
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kBackgroundGpu));
    // Olculen degerler cekimserlikte de tasinir.
    r = evaluate_balance(in);
    EF_CHECK(r.cpu_fps.value_or(0) == 120.0 && r.rows[2].gpu_fps.value_or(0) == 60.0);
    // Okunamadi: hukum engellenmez, YALNIZ not dusulur.
    in.confounders.other_gpu_pct = std::nullopt;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reasons.empty() && row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    EF_CHECK(has(r.notes, balance_note::kBackgroundGpuUnknown) && !has(r.notes, balance_note::kBackgroundCpuUnknown));
    // Sonlu olmayan deger "dusuk" sayilmaz (NaN < 10 yanlistir): cekimser.
    in.confounders.other_gpu_pct = std::numeric_limits<double>::quiet_NaN();
    EF_CHECK(all_undetermined(evaluate_balance(in), balance_reason::kBackgroundGpu));
    // Oncelik grubu: ortam nedenleri > arka plan CPU > arka plan GPU > gecersiz S1; hepsi listelenir.
    in = base;
    in.confounders.other_cpu_pct = 35.0;
    in.confounders.other_gpu_pct = 48.0;
    in.cpu = invalid(120.0);
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kBackgroundCpu && r.global_reasons.size() == 3);
    EF_CHECK(r.global_reasons[1] == balance_reason::kBackgroundGpu && r.global_reasons[2] == balance_reason::kCpuStepInvalid);
    in.confounders.on_battery = true;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kOnBattery && has(r.global_reasons, balance_reason::kBackgroundGpu));
}

// D6: kararsiz adim icin sinirli tekrar karari (saf). Esik degismez; yalniz "unstable" tekrar edilir.
EF_TEST(balance_retry_decision_is_bounded_and_only_for_unstable) {
    const StepStats unstable = invalid(120.0);
    EF_CHECK(unstable.reason == balance_reason::kUnstable);
    // Ilk denemeden sonra (attempts = 1), kosuda henuz tekrar yokken: tekrar.
    EF_CHECK(should_retry_step(unstable, 1, 0));
    EF_CHECK(should_retry_step(unstable, 1, 1));   // kosuda ikinci tekrar hakki
    EF_CHECK(!should_retry_step(unstable, 1, 2));  // kosu ust siniri (2) doldu
    EF_CHECK(!should_retry_step(unstable, 2, 0));  // adim ust siniri: ilk + 1 tekrar
    EF_CHECK(!should_retry_step(unstable, 3, 0));
    EF_CHECK(!should_retry_step(unstable, 0, 0));  // henuz kosulmamis adim tekrar edilmez
    // Gecerli adim, olculemeyen adim, kosulmamis adim: tekrar yok.
    EF_CHECK(!should_retry_step(ok(120.0, 0.3), 1, 0));
    EF_CHECK(!should_retry_step(unmeasurable_step(balance_reason::kVramBudget), 1, 0));
    EF_CHECK(!should_retry_step(StepStats{}, 1, 0));
    // Baska gecersizlik nedenleri gurultu degil yapisal kosuldur: tekrar yok.
    // denge_v2: gecersiz zaman damgasi gecici olabilir: kararsizlik gibi bir kez tekrar.
    {
        StepStats s = invalid(120.0);
        s.reason = balance_reason::kTimestampDisjoint;
        EF_CHECK(should_retry_step(s, 1, 0) && !should_retry_step(s, 2, 0));
    }
    for (const auto reason : {balance_reason::kTooFewFrames, balance_reason::kTooFewSeconds}) {
        StepStats s = invalid(120.0);
        s.reason = reason;
        EF_CHECK(!should_retry_step(s, 1, 0));
    }
    // "unstable" nedeni Valid durumla gelirse (tutarsiz girdi) tekrar yok.
    StepStats odd = ok(120.0, 0.3);
    odd.reason = balance_reason::kUnstable;
    EF_CHECK(!should_retry_step(odd, 1, 0));
    // Olcumden gelen ozetle: saniyeler arasi oynama -> tekrar adayi; sabit / kisa / disjoint -> degil.
    std::vector<double> wobble;
    for (int sec = 0; sec < 8; ++sec) {
        const auto part = sec % 2 ? steady(80, 12.5) : steady(100, 10.0);
        wobble.insert(wobble.end(), part.begin(), part.end());
    }
    EF_CHECK(should_retry_step(summarize_step(wobble, {}, 8.0, 0, true), 1, 0));
    EF_CHECK(!should_retry_step(summarize_step(steady(1600, 5.0), {}, 8.0, 0, true), 1, 0));
    EF_CHECK(!should_retry_step(summarize_step(steady(500, 10.0), {}, 8.0, 0, true), 1, 0));
    EF_CHECK(!should_retry_step(summarize_step(steady(1600, 5.0), steady(1600, 4.6), 8.0, 2, true), 1, 0));
}

// D6 / C1: olay gunlugu kaydi tek basina not olmaz; yalniz yuk altinda olculen dususle birlikte ek kanittir.
EF_TEST(balance_firmware_limit_note_needs_measured_speed_drop) {
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    in.cpu_firmware_limit_logged = true;
    auto r = evaluate_balance(in);
    EF_CHECK(r.notes.empty());
    in.cpu_speed_drop = true;
    r = evaluate_balance(in);
    EF_CHECK(has(r.notes, balance_note::kCpuSpeedDrop) && has(r.notes, balance_note::kCpuFirmwareLimitLogged));
    EF_CHECK(row_is(r, 0, BalanceVerdict::CpuLimited) && r.global_reasons.empty());  // not hukmu engellemez
    in.cpu_firmware_limit_logged = false;
    r = evaluate_balance(in);
    EF_CHECK(has(r.notes, balance_note::kCpuSpeedDrop) && !has(r.notes, balance_note::kCpuFirmwareLimitLogged));
}

// Uyumluluk B3 (bosluk 25): olcum penceresinde simge durumundaki pencere adimi gecersiz kilar; sonuc uydurulmaz.
EF_TEST(balance_window_minimized_flags_measured_step) {
    const StepStats good = ok(120.0, 0.30);
    EF_CHECK(flag_window_minimized(good, false).state == StepState::Valid);
    const StepStats flagged = flag_window_minimized(good, true);
    EF_CHECK(flagged.state == StepState::Invalid && flagged.reason == balance_reason::kWindowMinimized);
    EF_CHECK(flagged.fps == 120.0 && flagged.frames == good.frames);  // tani icin kalir
    // Zaten gecersiz adimin nedeni simge durumu olur; olculemeyen / kosulmayan adim degismez.
    EF_CHECK(flag_window_minimized(invalid(90.0), true).reason == balance_reason::kWindowMinimized);
    const StepStats um = flag_window_minimized(unmeasurable_step(balance_reason::kVramBudget), true);
    EF_CHECK(um.state == StepState::Unmeasurable && um.reason == balance_reason::kVramBudget);
    EF_CHECK(flag_window_minimized(StepStats{}, true).state == StepState::NotRun);
    // Tekrar edilmez (gecici gurultu degil, kullanici durumu).
    EF_CHECK(!should_retry_step(flagged, 1, 0));
    // Kural: simge durumunda olculen S1 -> butun satirlar cekimser (CPU adimi gecersiz).
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    in.cpu = flagged;
    const auto r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kCpuStepInvalid && !r.cpu_fps);
}

// denge_v3 (Uyumluluk B4 bosluk 13): Windows guc modu "en iyi guc verimliligi" tum satirlari cekimser birakir.
EF_TEST(balance_efficiency_overlay_confounder) {
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    auto r = evaluate_balance(in);
    EF_CHECK(r.global_reasons.empty() && row_is(r, 0, BalanceVerdict::CpuLimited));
    in.confounders.efficiency_overlay = true;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kEfficiencyOverlay);
    for (const auto& row : r.rows) EF_CHECK(row.verdict == BalanceVerdict::Undetermined && row.reason == balance_reason::kEfficiencyOverlay);
    EF_CHECK(r.cpu_fps.value_or(0) == 120.0);  // olculen deger tasinir
    // Ortam nedenleri sirasi: guc tasarrufu plani overlay'den once gelir; ikisi de listelenir.
    in.confounders.power_saver_plan = true;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kPowerSaverPlan && has(r.global_reasons, balance_reason::kEfficiencyOverlay));
}

// denge_v3 (bosluk 9): bayrak goruldu ama kosu kurali tutmadi -> yalniz not; hukum engellenmez.
EF_TEST(balance_vendor_limit_flag_is_note_only) {
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    in.confounders.vendor_limit_flag = true;
    auto r = evaluate_balance(in);
    EF_CHECK(r.global_reasons.empty() && row_is(r, 0, BalanceVerdict::CpuLimited) && row_is(r, 2, BalanceVerdict::GpuLimited));
    EF_CHECK(has(r.notes, balance_note::kVendorLimitFlag));
    // Kural tuttuysa karistirici; not tekrar yazilmaz.
    in.confounders.vendor_throttle = true;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kVendorThrottle && !has(r.notes, balance_note::kVendorLimitFlag));
    in = clean(120.0, {210.0, 125.0, 60.0});
    EF_CHECK(!has(evaluate_balance(in).notes, balance_note::kVendorLimitFlag));
}

// Inceleme B4: bayrak goruldu ama yuk/saat okunamadi -> "saat dusmedi" notu yazilmaz; ihtiyatli olarak cekimser.
EF_TEST(balance_vendor_limit_unverified_blocks_without_clock_claim) {
    const auto all_undetermined = [](const BalanceResult& r, std::string_view reason) {
        bool okay = r.global_reason == reason && !r.global_reasons.empty() && r.global_reasons.front() == reason;
        for (const auto& row : r.rows) okay = okay && row.verdict == BalanceVerdict::Undetermined && row.reason == reason;
        return okay;
    };
    auto in = clean(120.0, {210.0, 125.0, 60.0});
    in.confounders.vendor_limit_unverified = true;
    auto r = evaluate_balance(in);
    EF_CHECK(all_undetermined(r, balance_reason::kVendorLimitUnverified));
    EF_CHECK(!has(r.notes, balance_note::kVendorLimitFlag));
    // Uygulama esleme kurali ile ayni: Unverifiable -> yalniz unverified alani.
    const PerfCapFlags power{false, true, false};
    LimitSample load_missing;
    load_missing.caps = power;
    load_missing.clock_mhz = 1900;
    LimitSample clock_missing;
    clock_missing.caps = power;
    clock_missing.load_pct = 99;
    for (const auto& ws : {std::vector<LimitSample>{load_missing, load_missing}, std::vector<LimitSample>{clock_missing, clock_missing}}) {
        const auto v = balance_vendor_limit(ws);
        EF_CHECK(v == VendorLimit::Unverifiable);
        auto in2 = clean(120.0, {210.0, 125.0, 60.0});
        in2.confounders.vendor_throttle = v == VendorLimit::Throttle;
        in2.confounders.vendor_limit_flag = v == VendorLimit::FlagOnly;
        in2.confounders.vendor_limit_unverified = v == VendorLimit::Unverifiable;
        const auto r2 = evaluate_balance(in2);
        EF_CHECK(!has(r2.notes, balance_note::kVendorLimitFlag) && r2.global_reason == balance_reason::kVendorLimitUnverified);
    }
    // Throttle ile birlikte yalniz throttle nedeni yazilir.
    in.confounders.vendor_throttle = true;
    r = evaluate_balance(in);
    EF_CHECK(r.global_reason == balance_reason::kVendorThrottle && !has(r.global_reasons, balance_reason::kVendorLimitUnverified));
}

// denge_v3 (bosluk 9): drops.cpp ile ayni kosu duzeyi kural (>= %25 yuk alti pencerede bayrak VE saat < %90).
EF_TEST(balance_vendor_limit_rule_edges) {
    const PerfCapFlags none{}, power{false, true, false}, thermal{true, false, false};
    const auto w = [](std::optional<PerfCapFlags> caps, double load, std::optional<double> clock) {
        LimitSample s;
        s.caps = caps;
        s.load_pct = load;
        s.clock_mhz = clock;
        return s;
    };
    // Bayrak yok -> None (saat dussede).
    EF_CHECK(balance_vendor_limit({}) == VendorLimit::None);
    EF_CHECK(balance_vendor_limit({w(none, 99, 2000), w(none, 99, 1500), w(none, 99, 2000), w(none, 99, 2000)}) == VendorLimit::None);
    // Tek pencerede bayrak (4 yuk alti pencerenin %25'i) + saat dususu (1700 < 0,9 * 2000) -> Throttle.
    EF_CHECK(balance_vendor_limit({w(power, 99, 1700), w(none, 99, 2000), w(none, 99, 2000), w(none, 99, 2000)}) == VendorLimit::Throttle);
    // Isi bayragi da ayni kural.
    EF_CHECK(balance_vendor_limit({w(thermal, 99, 1700), w(none, 99, 2000), w(none, 99, 2000), w(none, 99, 2000)}) == VendorLimit::Throttle);
    // Pay esigin altinda: 5 pencerenin 1'i (%20) -> yalniz not.
    EF_CHECK(balance_vendor_limit({w(power, 99, 1700), w(none, 99, 2000), w(none, 99, 2000), w(none, 99, 2000), w(none, 99, 2000)}) ==
              VendorLimit::FlagOnly);
    // Bayrak her pencerede ama saat dusmedi (1801 >= 0,9 * 2000 = 1800) -> yalniz not. Tam %90 da dusus sayilmaz.
    EF_CHECK(balance_vendor_limit({w(power, 99, 1801), w(power, 99, 2000), w(power, 99, 1950)}) == VendorLimit::FlagOnly);
    EF_CHECK(balance_vendor_limit({w(power, 99, 1800), w(power, 99, 2000)}) == VendorLimit::FlagOnly);
    EF_CHECK(balance_vendor_limit({w(power, 99, 1799), w(power, 99, 2000)}) == VendorLimit::Throttle);
    // Inceleme B4: saat bilinmiyor -> kural degerlendirilemez ("saat dusmedi" denemez) -> Unverifiable.
    EF_CHECK(balance_vendor_limit({w(power, 99, std::nullopt), w(power, 99, std::nullopt)}) == VendorLimit::Unverifiable);
    // Saati okunan tek yuk alti pencere de yetmez (kendisiyle kiyaslanir).
    EF_CHECK(balance_vendor_limit({w(power, 99, 2000), w(power, 99, std::nullopt), w(none, 99, std::nullopt)}) == VendorLimit::Unverifiable);
    // Tek yuk alti pencere (en az 2 gerekir) -> Unverifiable; yuk alti olmayan (CPU adimi) pencere sayilmaz.
    EF_CHECK(balance_vendor_limit({w(power, 99, 1000), w(none, 40, 2000)}) == VendorLimit::Unverifiable);
    EF_CHECK(balance_vendor_limit({w(power, 30, 1000), w(power, 20, 900)}) == VendorLimit::Unverifiable);
    // Yuk okunamadi (surucu kullanim bildirmedi): yuk alti pencere yok -> Unverifiable.
    LimitSample no_load;
    no_load.caps = power;
    no_load.clock_mhz = 2000;
    EF_CHECK(balance_vendor_limit({no_load, no_load, no_load}) == VendorLimit::Unverifiable);
    // Bayrak yoksa yuk/saat eksikligi onemsiz -> None.
    EF_CHECK(balance_vendor_limit({w(none, 99, std::nullopt), w(none, 30, std::nullopt)}) == VendorLimit::None);
    // Desteklenmeyen bayrak (nullopt) bayrak sayilmaz.
    EF_CHECK(balance_vendor_limit({w(std::nullopt, 99, 1000), w(std::nullopt, 99, 2000)}) == VendorLimit::None);
}
