// F-41 + F-34: hibrit islemci ve guc ozelligi hazirlik kurallari (saf; Win32 yok, birim testlenir).
// Kaynak: platform/windows/readiness/cpu_reader (salt okuma; hicbir ayar yazilmaz).
// Ilkeler:
//  - Topoloji bilinmiyorsa veya hibrit degilse bulgu uretilmez ("P/E var" iddia edilmez).
//  - E cekirdeklerini kapatma onerisi YOKTUR; tersine kapatmama gerekcesi yazilir.
//  - Thread Director/APO/DTT etkinligi genel API ile okunamaz; metin kosullu ve resmi
//    destek listesine yonlendiren bilgi duzeyindedir. Kazanc iddiası yoktur.
#include <cmath>
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

constexpr std::uint32_t kWin11FirstBuild = 22000;
constexpr std::string_view kIntelApoUrl =
    "https://www.intel.com/content/www/us/en/support/articles/000095419/processors.html";

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Hardware;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

}  // namespace

std::vector<Check> cpu_hybrid_checks(const CpuHybridInfo& c) {
    std::vector<Check> out;
    if (!c.hybrid.has_value() || !*c.hybrid) return out;
    // Hibrit bilgisi tek kaynaktan turetilir; sayilar eksikse kismi iddia yerine hic bulgu yok.
    if (!c.performance_cores.has_value() || !c.efficiency_cores.has_value() || !c.logical_processors.has_value())
        return out;

    Check hybrid = make("cpu.hybrid", Level::Info, "chk.cpu.hybrid");
    hybrid.args["perf"] = std::to_string(*c.performance_cores);
    hybrid.args["eff"] = std::to_string(*c.efficiency_cores);
    hybrid.args["threads"] = std::to_string(*c.logical_processors);
    out.push_back(std::move(hybrid));

    const bool build_known = c.windows_build.has_value();
    const bool win11 = build_known && *c.windows_build >= kWin11FirstBuild;
    if (build_known && !win11) out.push_back(make("cpu.hybrid_win10", Level::Advice, "chk.cpu.hybrid_win10"));

    // APO/DTT yalniz Intel'de ve Windows 11'de anlamlidir; yuklu/etkin oldugu iddia edilmez.
    if (win11 && c.vendor == "intel") {
        Check apo = make("cpu.intel_apo", Level::Info, "chk.cpu.intel_apo");
        apo.action = ActionKind::OpenUrl;
        apo.action_arg = std::string(kIntelApoUrl);
        out.push_back(std::move(apo));
    }
    return out;
}

// F-34: CPUID yalniz ozellik DESTEGINI verir. Gercek HWP/EIST etkinligi BIOS/MSR'dedir ve ring0
// olmadan okunamaz; burada "acik/kapali" iddiasi uretilmez, hicbir ayar yazilmaz.
std::vector<Check> cpu_power_checks(const CpuPowerInfo& p) {
    std::vector<Check> out;
    if (!p.cpuid_ok || p.vendor != "intel") return out;
    if (p.speed_shift.has_value() && *p.speed_shift)
        out.push_back(make("cpu.speed_shift", Level::Info, "chk.cpu.speed_shift"));
    if (p.eist.has_value() && *p.eist)
        out.push_back(make("cpu.eist", Level::Info, "chk.cpu.eist"));
    return out;
}

std::vector<Check> cpu_speed_limit_checks(const CpuSpeedLimitInfo& s) {
    const bool limit_ok = s.performance_limit_pct && std::isfinite(*s.performance_limit_pct) && *s.performance_limit_pct >= 0.0 &&
                          *s.performance_limit_pct <= 100.0;
    Check c = make("cpu.speed_limit", Level::Unknown, "chk.cpu.speed_limit_unknown");
    if (limit_ok && *s.performance_limit_pct < 100.0) {
        c.level = Level::Advice;
        c.text_key = "chk.cpu.speed_limit_capped";
        c.args["limit"] = std::to_string(static_cast<int>(std::floor(*s.performance_limit_pct)));
    } else if (s.firmware_limit_events && *s.firmware_limit_events > 0) {
        // Gecmis kayit: su an sinirlandigini, suresini ya da nedenini gostermez ve kullanicinin burada yapabilecegi bir
        // islem yoktur -> bilgi (hazirlik skorunu dusurmez). Yuk altindaki gercek dusus Denge testinde olculur.
        c.level = Level::Info;
        c.text_key = "chk.cpu.speed_limit_events";
        c.args["count"] = std::to_string(*s.firmware_limit_events) + (s.events_capped ? "+" : "");
    } else if (limit_ok && s.firmware_limit_events) {
        c.level = Level::Good;
        c.text_key = "chk.cpu.speed_limit_ok";
    }
    return {std::move(c)};
}

}  // namespace ef::core::readiness
