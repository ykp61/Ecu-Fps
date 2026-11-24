// EF-X22.02: karisik yuk isi/guc tanisi (saf) + yayin kapisi fail-closed + CPU FP tolerans politikasi.
#include <string>
#include <vector>

#include "benchmark/cpu/cpu_workload.h"
#include "core/diagnosis/calibration.h"
#include "core/diagnosis/combined_load.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
namespace i18n = ef::core::i18n;

namespace {

LimitSample win(double load, double clock, bool thermal = false, bool power = false) {
    LimitSample s;
    s.caps = PerfCapFlags{thermal, power, false};
    s.load_pct = load;
    s.clock_mhz = clock;
    return s;
}
// Yuk altinda, bayrakli ve saati dusen pencereler (Throttle).
std::vector<LimitSample> throttled(bool thermal, bool power) {
    return {win(95, 2600), win(95, 2000, thermal, power), win(95, 1900, thermal, power), win(95, 2000, thermal, power)};
}
std::vector<LimitSample> clean() { return {win(95, 2600), win(95, 2590), win(95, 2600), win(95, 2595)}; }

ReleaseStatus open_gate() {
    ReleaseStatus g;
    g.ready = true;
    g.synthetic_only = false;
    g.real_holdout = kMinRealHoldout;
    g.reason_key = "diag.gate.ready";
    return g;
}
ReleaseStatus closed_gate() {
    ReleaseStatus g;
    g.reason_key = "diag.gate.no_real_holdout";
    return g;
}

}  // namespace

EF_TEST(x22_combined_classes_with_open_gate) {
    const auto g = open_gate();
    auto r = diagnose_combined_limits({clean(), throttled(true, false), false}, g);
    EF_CHECK(r.candidate == CombinedLimit::CombinedOnly && r.published && r.text_key == "diag.combined.combined_only");
    EF_CHECK(r.thermal_seen && !r.power_seen && r.cpu_not_measured);
    r = diagnose_combined_limits({throttled(false, true), throttled(false, true), false}, g);
    EF_CHECK(r.candidate == CombinedLimit::AlsoSolo && !r.thermal_seen && r.power_seen);
    r = diagnose_combined_limits({clean(), clean(), false}, g);
    EF_CHECK(r.candidate == CombinedLimit::NoLimitSeen && r.published);
}

// Tek basina sicaklik/bayrak kisit sayilmaz: saat dusmediyse NoLimitSeen (FlagOnly), PSU hukmu yok.
EF_TEST(x22_flag_without_clock_drop_is_not_a_limit) {
    std::vector<LimitSample> flagged = {win(95, 2600, true), win(95, 2590, true), win(95, 2600, true)};
    const auto r = diagnose_combined_limits({clean(), flagged, false}, open_gate());
    EF_CHECK(r.candidate == CombinedLimit::NoLimitSeen && r.thermal_seen);
}

EF_TEST(x22_unsupported_and_insufficient_are_separate) {
    const auto g = open_gate();
    EF_CHECK(diagnose_combined_limits({clean(), throttled(true, true), true}, g).candidate == CombinedLimit::NotSupported);  // AMD/Intel GPU
    std::vector<LimitSample> no_caps = {LimitSample{std::nullopt, 95.0, 2000.0}, LimitSample{std::nullopt, 95.0, 1800.0}};
    EF_CHECK(diagnose_combined_limits({clean(), no_caps, false}, g).candidate == CombinedLimit::NotSupported);
    std::vector<LimitSample> light = {win(20, 1200), win(30, 1300, true)};  // yuk altinda pencere yok
    EF_CHECK(diagnose_combined_limits({clean(), light, false}, g).candidate == CombinedLimit::InsufficientData);
    EF_CHECK(diagnose_combined_limits({clean(), {win(95, 2000)}, false}, g).candidate == CombinedLimit::InsufficientData);  // tek pencere
    // Karisik yukte kisit var ama tek basina sahne tabani yok: "yalniz karisik yukte" denemez.
    EF_CHECK(diagnose_combined_limits({{}, throttled(true, false), false}, g).candidate == CombinedLimit::InsufficientData);
    EF_CHECK(diagnose_combined_limits({{}, {}, false}, g).candidate == CombinedLimit::NotSupported);
    // Cekimserlik kapi kapaliyken de gosterilir.
    const auto r = diagnose_combined_limits({clean(), light, false}, closed_gate());
    EF_CHECK(r.published && r.text_key == "diag.combined.insufficient" && r.gate_reason_key.empty());
}

// Fail-closed: 30 gercek holdout olmadan hukum yayimlanmaz; uretim kapisi bu dilimde kapali.
EF_TEST(x22_verdicts_withheld_while_gate_closed) {
    for (const auto& in : {CombinedLimitInput{clean(), throttled(true, false), false}, CombinedLimitInput{throttled(true, false), throttled(true, false), false},
                           CombinedLimitInput{clean(), clean(), false}}) {
        const auto r = diagnose_combined_limits(in, closed_gate());
        EF_CHECK(!r.published && r.text_key == "diag.combined.withheld" && r.gate_reason_key == "diag.gate.no_real_holdout");
        ReleaseStatus blank;  // nedeni bos kapali kapi da kapali sayilir
        EF_CHECK(!diagnose_combined_limits(in, blank).published);
    }
    const auto prod = release_status_from_validated(validated_calibration());
    EF_CHECK(!prod.ready);  // gomulu uretim ozeti: gercek holdout yok
    EF_CHECK(!diagnose_combined_limits({clean(), throttled(true, false), false}, prod).published);
}

EF_TEST(x22_cpu_fp_tolerance_is_exact) {
    static_assert(ef::benchmark::cpu::kCpuFpToleranceUlps == 0, "CPU FP tolerans 0 ULP (docs/05)");
    const volatile std::uint32_t ulps = ef::benchmark::cpu::kCpuFpToleranceUlps;
    EF_CHECK(ulps == 0u);
}

EF_TEST(x22_combined_keys_in_both_catalogs) {
    std::vector<std::string> keys = {"diag.combined.withheld", "diag.combined.cpu_not_measured", "diag.gate.no_real_holdout"};
    for (auto c : {CombinedLimit::NotSupported, CombinedLimit::InsufficientData, CombinedLimit::NoLimitSeen, CombinedLimit::CombinedOnly, CombinedLimit::AlsoSolo})
        keys.emplace_back(combined_text_key(c));
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const auto& k : keys) {
            const auto id = i18n::id_of(k);
            EF_CHECK(id.has_value() && !cat.value().get(*id).empty());
        }
    }
}
