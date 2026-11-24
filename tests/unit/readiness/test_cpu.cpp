// F-41: hibrit islemci hazirlik kurallari (saf; Win32 cagrisi yok). Topoloji bilinmiyorsa bulgu
// uretilmez; E cekirdek kapatma onerisi yoktur; APO/DTT etkinligi iddia edilmez.
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;

namespace {

std::optional<Check> find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return c;
    return std::nullopt;
}

CpuHybridInfo intel_win11() {
    CpuHybridInfo c;
    c.vendor = "intel";
    c.physical_cores = 16;
    c.logical_processors = 24;
    c.performance_cores = 8;
    c.efficiency_cores = 8;
    c.hybrid = true;
    c.windows_build = 26100;
    return c;
}

}  // namespace

EF_TEST(cpu_hybrid_unknown_makes_no_claim) {
    EF_CHECK(cpu_hybrid_checks(CpuHybridInfo{}).empty());
}

EF_TEST(cpu_hybrid_false_makes_no_claim) {
    CpuHybridInfo c = intel_win11();
    c.hybrid = false;
    EF_CHECK(cpu_hybrid_checks(c).empty());
}

EF_TEST(cpu_hybrid_intel_win11_reports_topology_and_apo) {
    const auto v = cpu_hybrid_checks(intel_win11());
    EF_CHECK(v.size() == 2);
    const auto h = find(v, "cpu.hybrid");
    EF_CHECK(h && h->level == Level::Info && h->section == Section::Hardware);
    EF_CHECK(h && h->text_key == "chk.cpu.hybrid");
    EF_CHECK(h && h->args.at("perf") == "8" && h->args.at("eff") == "8" && h->args.at("threads") == "24");
    EF_CHECK(h && h->action == ActionKind::None);
    const auto a = find(v, "cpu.intel_apo");
    EF_CHECK(a && a->level == Level::Info && a->text_key == "chk.cpu.intel_apo");
    EF_CHECK(a && a->action == ActionKind::OpenUrl && action_uri_allowed(a->action_arg));
    EF_CHECK(a && a->action_arg.find("intel.com") != std::string::npos);
    EF_CHECK(ef::core::i18n::id_of("chk.cpu.hybrid").has_value());
    EF_CHECK(ef::core::i18n::id_of("chk.cpu.intel_apo").has_value());
}

EF_TEST(cpu_hybrid_intel_win10_advises_upgrade_without_apo) {
    CpuHybridInfo c = intel_win11();
    c.windows_build = 19045;
    const auto v = cpu_hybrid_checks(c);
    EF_CHECK(find(v, "cpu.hybrid"));
    EF_CHECK(!find(v, "cpu.intel_apo"));
    const auto w = find(v, "cpu.hybrid_win10");
    EF_CHECK(w && w->level == Level::Advice && w->text_key == "chk.cpu.hybrid_win10");
    EF_CHECK(w && w->action == ActionKind::None);
    EF_CHECK(ef::core::i18n::id_of("chk.cpu.hybrid_win10").has_value());
}

EF_TEST(cpu_hybrid_build_unknown_reports_topology_only) {
    CpuHybridInfo c = intel_win11();
    c.windows_build.reset();
    const auto v = cpu_hybrid_checks(c);
    EF_CHECK(v.size() == 1);
    EF_CHECK(find(v, "cpu.hybrid"));
    EF_CHECK(!find(v, "cpu.hybrid_win10"));
    EF_CHECK(!find(v, "cpu.intel_apo"));
}

EF_TEST(cpu_hybrid_amd_win11_has_no_intel_apo) {
    CpuHybridInfo c = intel_win11();
    c.vendor = "amd";
    const auto v = cpu_hybrid_checks(c);
    EF_CHECK(find(v, "cpu.hybrid"));
    EF_CHECK(!find(v, "cpu.intel_apo"));
    EF_CHECK(!find(v, "cpu.hybrid_win10"));
}

EF_TEST(cpu_hybrid_missing_counts_makes_no_claim) {
    CpuHybridInfo c = intel_win11();
    c.performance_cores.reset();
    EF_CHECK(cpu_hybrid_checks(c).empty());
    c = intel_win11();
    c.efficiency_cores.reset();
    EF_CHECK(cpu_hybrid_checks(c).empty());
    c = intel_win11();
    c.logical_processors.reset();
    EF_CHECK(cpu_hybrid_checks(c).empty());
}

EF_TEST(cpu_hybrid_never_produces_autofix) {
    for (const auto& c : {intel_win11(), CpuHybridInfo{}}) {
        for (const auto& chk : cpu_hybrid_checks(c)) EF_CHECK(chk.action != ActionKind::AutoFix);
    }
}

// F-34: CPUID yalniz destek bilgisi verir; gercek BIOS/MSR durumu okunamaz ve yazilmaz.
EF_TEST(cpu_power_no_claim_without_cpuid_or_intel) {
    EF_CHECK(cpu_power_checks(CpuPowerInfo{}).empty());
    CpuPowerInfo amd;
    amd.vendor = "amd";
    amd.cpuid_ok = true;
    amd.speed_shift = true;
    amd.eist = true;
    EF_CHECK(cpu_power_checks(amd).empty());  // bu kontroller Intel'e ozgu
    CpuPowerInfo other;
    other.vendor = "other";
    other.cpuid_ok = true;
    other.eist = true;
    EF_CHECK(cpu_power_checks(other).empty());
}

EF_TEST(cpu_power_intel_supported_features_reported_without_state_claim) {
    CpuPowerInfo p;
    p.vendor = "intel";
    p.cpuid_ok = true;
    p.speed_shift = true;
    p.eist = true;
    const auto v = cpu_power_checks(p);
    EF_CHECK(v.size() == 2);
    const auto s = find(v, "cpu.speed_shift");
    EF_CHECK(s && s->level == Level::Info && s->section == Section::Hardware);
    EF_CHECK(s && s->text_key == "chk.cpu.speed_shift" && s->action == ActionKind::None);
    const auto e = find(v, "cpu.eist");
    EF_CHECK(e && e->level == Level::Info && e->text_key == "chk.cpu.eist");
    EF_CHECK(e && e->action == ActionKind::None);
    EF_CHECK(ef::core::i18n::id_of("chk.cpu.speed_shift").has_value());
    EF_CHECK(ef::core::i18n::id_of("chk.cpu.eist").has_value());
    for (const auto& chk : v) EF_CHECK(chk.action != ActionKind::AutoFix);
}

EF_TEST(cpu_power_unsupported_or_unknown_bits_make_no_claim) {
    CpuPowerInfo p;
    p.vendor = "intel";
    p.cpuid_ok = true;
    p.speed_shift = false;
    p.eist = false;
    EF_CHECK(cpu_power_checks(p).empty());
    p.speed_shift.reset();
    p.eist.reset();
    EF_CHECK(cpu_power_checks(p).empty());  // bilinmiyor: "destekliyor" denmez
    p.speed_shift = true;
    const auto v = cpu_power_checks(p);
    EF_CHECK(v.size() == 1 && find(v, "cpu.speed_shift"));
}

// Spec 2026-11-12 C1 (D6): CPU hiz siniri satiri (salt okuma; sicaklik / neden iddiasi yok, eylem yok).
EF_TEST(cpu_speed_limit_row_levels_and_no_action) {
    namespace i18n = ef::core::i18n;
    const auto one = [](const CpuSpeedLimitInfo& s) {
        const auto v = cpu_speed_limit_checks(s);
        EF_CHECK(v.size() == 1);
        return v.empty() ? Check{} : v.front();
    };
    // Hicbir sey okunamadi: bilinmiyor (temiz denmez).
    Check c = one(CpuSpeedLimitInfo{});
    EF_CHECK(c.id == "cpu.speed_limit" && c.section == Section::Hardware && c.level == Level::Unknown && c.text_key == "chk.cpu.speed_limit_unknown");
    // Sinir 100 + kayit 0: temiz.
    CpuSpeedLimitInfo s;
    s.performance_limit_pct = 100.0;
    s.firmware_limit_events = 0;
    c = one(s);
    EF_CHECK(c.level == Level::Good && c.text_key == "chk.cpu.speed_limit_ok" && c.args.empty());
    // Sinir 99,9: sinirlama (asagi yuvarlanir; "%100" yazilmaz).
    s.performance_limit_pct = 99.9;
    c = one(s);
    EF_CHECK(c.level == Level::Advice && c.text_key == "chk.cpu.speed_limit_capped" && c.args.at("limit") == "99");
    s.performance_limit_pct = 62.0;
    EF_CHECK(one(s).args.at("limit") == "62");
    // Su an sinirli + kayit var: olculen guncel durum once gelir.
    s.firmware_limit_events = 16;
    c = one(s);
    EF_CHECK(c.level == Level::Advice && c.text_key == "chk.cpu.speed_limit_capped");
    // Kayit var (sinir temiz): gecmis kayit bilgidir (sure / neden tasimaz) + sayi; ust sinirda kesildiyse "+".
    s.performance_limit_pct = 100.0;
    c = one(s);
    EF_CHECK(c.level == Level::Info && c.text_key == "chk.cpu.speed_limit_events" && c.args.at("count") == "16");
    s.firmware_limit_events = 512;
    s.events_capped = true;
    EF_CHECK(one(s).args.at("count") == "512+");
    // Sinir sayaci okunamadi ama kayit var: kayit yine bildirilir.
    s = CpuSpeedLimitInfo{};
    s.firmware_limit_events = 3;
    EF_CHECK(one(s).text_key == "chk.cpu.speed_limit_events");
    // Biri okunamadi ve bulgu yok: temiz denmez.
    s = CpuSpeedLimitInfo{};
    s.performance_limit_pct = 100.0;
    EF_CHECK(one(s).level == Level::Unknown);
    s = CpuSpeedLimitInfo{};
    s.firmware_limit_events = 0;
    EF_CHECK(one(s).level == Level::Unknown);
    // Aralik disi / sonlu olmayan sinir okunmamis sayilir.
    s.performance_limit_pct = 250.0;
    EF_CHECK(one(s).level == Level::Unknown);
    s.performance_limit_pct = std::numeric_limits<double>::quiet_NaN();
    EF_CHECK(one(s).level == Level::Unknown);
    // Hicbir durumda eylem / otomatik duzeltme yok; butun metinler katalogda ve sicaklik iddiasi tasimaz.
    for (const auto& info : {CpuSpeedLimitInfo{}, CpuSpeedLimitInfo{62.0, 0u, false}, CpuSpeedLimitInfo{100.0, 4u, false},
                             CpuSpeedLimitInfo{100.0, 0u, false}}) {
        const Check k = one(info);
        EF_CHECK(k.action == ActionKind::None && k.action_arg.empty());
        EF_CHECK(i18n::id_of(k.text_key).has_value());
    }
}
