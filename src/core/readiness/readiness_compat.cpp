// F-50 / F-68: sanallastirma isaretleri ve goruntu surucusu sagligi (saf; Win32 yok).
// Kaynak: platform/windows/readiness/virtualization_reader + driver_health_reader.
// Ilkeler:
//  - Hizmet/dosya varligi istege bagli ozelligin KESIN durumu degildir; metin "isaret" der.
//  - Birden fazla surucu paketi tek basina kalinti kaniti degildir; yalniz bilgi verilir.
//  - Hicbir ayar/ozellik/surucu yazilmaz, kaldirilmaz; AutoFix yoktur.
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Windows;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (const auto& s : v) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

}  // namespace

std::vector<Check> virtualization_checks(const VirtualizationInfo& v) {
    std::vector<Check> out;
    std::vector<std::string> signals;
    if (v.hyperv_service) signals.emplace_back("Hyper-V");
    if (v.vmp_service) signals.emplace_back("Sanal Makine Platformu");
    if (v.whp_dll) signals.emplace_back("Windows Hypervisor Platform");
    if (v.wsl_exe) signals.emplace_back("WSL");
    if (signals.empty()) return out;  // yalniz CPUID hipervizor bayragi: VBS/HVCI zaten ayri raporlanir

    Check c = make("system.virtualization", v.wsl_default_v2 ? Level::Advice : Level::Info,
                   v.wsl_default_v2 ? "chk.virtualization.wsl2" : "chk.virtualization.detected");
    c.args["signals"] = join(signals, ", ");
    out.push_back(std::move(c));
    return out;
}

std::vector<Check> display_driver_checks(const DisplayDriverHealthInfo& d) {
    std::vector<Check> out;
    if (!d.enumeration_ok) return out;  // liste alinamadi: sorun yok denmez

    if (!d.problem_devices.empty()) {
        Check c = make("driver.device_problem", Level::Advice, "chk.driver.device_problem");
        c.args["count"] = std::to_string(d.problem_devices.size());
        c.args["names"] = join(d.problem_devices, ", ");
        out.push_back(std::move(c));
    }

    std::vector<std::string> parts;
    if (d.nvidia_packages && *d.nvidia_packages > 1)
        parts.push_back("NVIDIA " + std::to_string(*d.nvidia_packages));
    if (d.amd_packages && *d.amd_packages > 1)
        parts.push_back("AMD " + std::to_string(*d.amd_packages));
    if (d.intel_packages && *d.intel_packages > 1)
        parts.push_back("Intel " + std::to_string(*d.intel_packages));
    if (!parts.empty()) {
        Check c = make("driver.multiple_packages", Level::Info, "chk.driver.multiple_packages");
        c.args["vendors"] = join(parts, ", ");
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Check> problem_device_checks(const inventory::ProblemDeviceReport& r) {
    std::vector<Check> out;
    if (!r.enumerated) {
        Check c = make("hardware.problem_devices", Level::Unknown, "chk.hardware.problem_devices.unknown");
        c.section = Section::Hardware;
        out.push_back(std::move(c));
        return out;
    }
    const std::size_t faults = inventory::problem_fault_count(r);
    if (faults == 0) {
        Check c = make("hardware.problem_devices", Level::Good, "chk.hardware.problem_devices.ok");
        c.section = Section::Hardware;
        out.push_back(std::move(c));
        return out;
    }
    std::vector<std::string> names;
    for (const auto& d : r.devices) {
        if (inventory::problem_is_informational(d.problem_code)) continue;
        if (names.size() >= 3) break;
        names.push_back(d.name.empty() ? (d.device_class.empty() ? d.instance_id : d.device_class) : d.name);
    }
    Check c = make("hardware.problem_devices", Level::Advice, "chk.hardware.problem_devices.found");
    c.section = Section::Hardware;
    c.args["count"] = std::to_string(faults) + (r.truncated ? "+" : "");
    c.args["names"] = join(names, ", ");
    c.action = ActionKind::OpenSettings;
    c.action_arg = "devmgmt.msc";
    out.push_back(std::move(c));
    return out;
}

}  // namespace ef::core::readiness
