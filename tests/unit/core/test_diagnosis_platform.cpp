// DIAG-04: platform isi/guc/PCIe/arka plan kurallari.
#include <algorithm>

#include "core/diagnosis/platform.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;

namespace {
bool has(const std::vector<PlatformEvidence>& v, PlatformEvidence e) {
    return std::find(v.begin(), v.end(), e) != v.end();
}
PcieSample link(double load, std::uint32_t gen, std::uint32_t width) {
    return {load, gen, width, 4u, 16u, 4u, 16u};  // kart ve yuva: Gen4 x16
}
LimitSample lim(double load, double mhz, bool thermal, bool power = false, bool voltage = false) {
    return {PerfCapFlags{thermal, power, voltage}, load, mhz};
}
}  // namespace

EF_TEST(platform_pcie_idle_downclock_is_not_bottleneck) {
    // Bosta Gen1 x16: guc tasarrufu. Yuk alti gozlem yok -> karar yok, link dusuk ilan edilmez.
    auto f = evaluate_pcie({link(5, 1, 16), link(8, 1, 16)});
    EF_CHECK(f.cls == PlatformClass::Insufficient && f.reason == PlatformConfidence::NoLoadObserved);
    EF_CHECK(has(f.counter, PlatformEvidence::IdleDownclockIgnored));
    // Bosta Gen1, yukte Gen4 x16: sinirlayici degil.
    f = evaluate_pcie({link(5, 1, 16), link(97, 4, 16), link(95, 4, 16)});
    EF_CHECK(f.cls == PlatformClass::NotLimiting && has(f.counter, PlatformEvidence::LinkAtMax));
    EF_CHECK(has(f.counter, PlatformEvidence::IdleDownclockIgnored));
}

EF_TEST(platform_pcie_reduced_under_load) {
    auto f = evaluate_pcie({link(96, 4, 4), link(98, 4, 4)});  // x4 yuk altinda, kart/yuva x16
    EF_CHECK(f.cls == PlatformClass::PcieLinkReduced && f.reason == PlatformConfidence::DirectLinkReading);
    EF_CHECK(has(f.supporting, PlatformEvidence::GpuUnderLoad) && has(f.supporting, PlatformEvidence::LinkBelowCardMax) &&
              has(f.supporting, PlatformEvidence::LinkBelowSlotMax));
    EF_CHECK(!f.hardware_fault());
    // Yuva x8, kart x16, link x8: yuva siniri; egitim dususu degil.
    PcieSample s{97.0, 4u, 8u, 4u, 16u, 4u, 8u};
    f = evaluate_pcie({s});
    EF_CHECK(f.cls == PlatformClass::NotLimiting && has(f.counter, PlatformEvidence::LinkLimitedBySlot));
}

EF_TEST(platform_pcie_unknown_max_is_insufficient) {
    PcieSample s{97.0, 3u, 16u, std::nullopt, std::nullopt, 4u, 16u};
    auto f = evaluate_pcie({s});
    EF_CHECK(f.cls == PlatformClass::Insufficient && has(f.counter, PlatformEvidence::LinkMaxUnknown));
    f = evaluate_pcie({PcieSample{std::nullopt, 1u, 16u, 4u, 16u, 4u, 16u}});
    EF_CHECK(f.cls == PlatformClass::Insufficient && f.reason == PlatformConfidence::MissingData);
    EF_CHECK(evaluate_pcie({}).cls == PlatformClass::Insufficient);
}

EF_TEST(platform_limits_vendor_flags_and_clock_drop) {
    // Isi bayragi + yuk alti frekans dususu: ayni yonde.
    auto f = evaluate_limits({lim(98, 1500, true), lim(98, 1480, true), lim(97, 1900, false)}, 1950.0);
    EF_CHECK(f.cls == PlatformClass::ThermalLimit && f.reason == PlatformConfidence::SignalsAgree);
    EF_CHECK(has(f.supporting, PlatformEvidence::PerfCapThermal) && has(f.supporting, PlatformEvidence::ClockDropUnderLoad));
    // Guc bayragi, frekans bilinmiyor: yalniz uretici bayragi.
    f = evaluate_limits({lim(99, 0, false, true), lim(99, 0, false, true)}, std::nullopt);
    EF_CHECK(f.cls == PlatformClass::PowerLimit && f.reason == PlatformConfidence::VendorFlagOnly);
    EF_CHECK(has(f.counter, PlatformEvidence::ClockUnknown));
    // Guvenilirlik voltaji.
    f = evaluate_limits({lim(95, 1900, false, false, true), lim(95, 1900, false, false, true)}, 1950.0);
    EF_CHECK(f.cls == PlatformClass::VoltageLimit && has(f.counter, PlatformEvidence::ClockStable));
    // Bayrak yok, frekans sabit: sinirlayici degil.
    f = evaluate_limits({lim(98, 1940, false), lim(98, 1930, false)}, 1950.0);
    EF_CHECK(f.cls == PlatformClass::NotLimiting && has(f.counter, PlatformEvidence::PerfCapNone));
    // Frekans dustu ama bayrak yok: atfedilmez.
    f = evaluate_limits({lim(98, 1200, false), lim(98, 1250, false)}, 1950.0);
    EF_CHECK(f.cls == PlatformClass::Insufficient && f.reason == PlatformConfidence::ConflictingSignals);
}

EF_TEST(platform_limits_no_universal_temperature_and_unsupported) {
    // Bayrak desteklenmiyor: "ok" degil, acikca Unsupported (frekans dususu yalniz bilgi).
    std::vector<LimitSample> w{{std::nullopt, 98.0, 1300.0}, {std::nullopt, 98.0, 1300.0}};
    auto f = evaluate_limits(w, 1950.0);
    EF_CHECK(f.cls == PlatformClass::Unsupported && f.reason == PlatformConfidence::UnsupportedSensor);
    EF_CHECK(f.cls != PlatformClass::NotLimiting && has(f.supporting, PlatformEvidence::PerfCapUnsupported));
    // Yuk yok: bostaki dusuk saat sinir degil.
    f = evaluate_limits({lim(5, 300, false)}, 1950.0);
    EF_CHECK(f.cls == PlatformClass::Insufficient && f.reason == PlatformConfidence::NoLoadObserved);
    f = evaluate_limits({{PerfCapFlags{}, std::nullopt, 1900.0}}, 1950.0);
    EF_CHECK(f.cls == PlatformClass::Insufficient && f.reason == PlatformConfidence::MissingData);
}

EF_TEST(platform_perf_caps_from_mask_only_maps_known_thermal_power) {
    // 0 = surucu "dusus yok" diyor: destekli ve sinirlayici degil.
    auto c = perf_caps_from_mask(0);
    EF_CHECK(c.has_value() && !c->thermal && !c->power && !c->voltage);
    c = perf_caps_from_mask(0x1u | 0x2u);
    EF_CHECK(c.has_value() && c->thermal && c->power);
    // UNKNOWN veya taninmayan bit: "limit yok" denmez.
    EF_CHECK(!perf_caps_from_mask(0x80000000u).has_value());
    EF_CHECK(!perf_caps_from_mask(0x100u).has_value());
    // AC_BATT/API_TRIGGERED/INSUFFICIENT_POWER isi/guc limiti sayilmaz; maskede taninir.
    c = perf_caps_from_mask(0x4u | 0x8u | 0x10u);
    EF_CHECK(c.has_value() && !c->thermal && !c->power && !c->voltage);
}

EF_TEST(platform_background_contention_excludes_game_and_self) {
    const std::uint32_t game = 4000, self = 5000;
    ProcessWindow w{true,
                    {{0, "Idle", 70.0, 0.0},
                     {game, "cs2.exe", 60.0, 95.0},
                     {self, "EcuFps.exe", 30.0, 0.0},
                     {700, "obs64.exe", 18.0, 22.0},
                     {800, "explorer.exe", 1.0, 0.0}}};
    auto f = evaluate_background({w, w}, game, self);
    EF_CHECK(f.cls == PlatformClass::BackgroundContention && f.reason == PlatformConfidence::MeasuredShare);
    EF_CHECK(f.processes.size() == 1 && f.processes[0] == "obs64.exe");
    EF_CHECK(has(f.supporting, PlatformEvidence::CompetingCpuShare) && has(f.supporting, PlatformEvidence::CompetingGpuShare));
    // Dusuk pay: neden degil (oyun ve biz yuksek olsa bile).
    ProcessWindow low{true, {{game, "cs2.exe", 80.0, 95.0}, {self, "EcuFps.exe", 40.0, 0.0}, {800, "explorer.exe", 2.0, 0.5}}};
    f = evaluate_background({low}, game, self);
    EF_CHECK(f.cls == PlatformClass::NotLimiting && has(f.counter, PlatformEvidence::CompetingShareLow));
    EF_CHECK(f.processes.empty());
}

EF_TEST(platform_background_missing_data_is_insufficient) {
    EF_CHECK(evaluate_background({}, 1, 2).cls == PlatformClass::Insufficient);
    EF_CHECK(evaluate_background({ProcessWindow{false, {}}}, 1, 2).cls == PlatformClass::Insufficient);
    ProcessWindow nodata{true, {{900, "x.exe", std::nullopt, std::nullopt}}};
    const auto f = evaluate_background({nodata}, 1, 2);
    EF_CHECK(f.cls == PlatformClass::Insufficient && has(f.counter, PlatformEvidence::ProcessDataMissing));
}

EF_TEST(platform_never_declares_hardware_fault) {
    // Hicbir sinif PSU/VRM/anakart arizasi degildir.
    for (int i = 0; i < kPlatformClassCount; ++i) {
        const auto c = static_cast<PlatformClass>(i);
        EF_CHECK(!is_hardware_fault(c));
        const auto name = to_string(c);
        EF_CHECK(name != "unknown");
        EF_CHECK(name.find("psu") == std::string_view::npos && name.find("vrm") == std::string_view::npos &&
                  name.find("motherboard") == std::string_view::npos && name.find("fault") == std::string_view::npos);
    }
    static_assert(!is_hardware_fault(PlatformClass::PowerLimit));
    const auto f = evaluate_limits({lim(99, 1200, true, true, true)}, 1950.0);
    EF_CHECK(!f.hardware_fault());
}
