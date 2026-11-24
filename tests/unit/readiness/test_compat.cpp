// F-50 / F-68: sanallastirma isaretleri ve surucu sagligi hazirlik kurallari (saf).
// Hizmet/dosya varligi kesin ozellik durumu degildir; sorun kodu/veri yoksa iddia uretilmez.
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

}  // namespace

EF_TEST(virtualization_makes_no_claim_without_service_or_file_signal) {
    EF_CHECK(virtualization_checks(VirtualizationInfo{}).empty());
    VirtualizationInfo v;
    v.hypervisor_present = true;  // yalniz CPUID: VBS/HVCI ayri raporlanir, burada iddia yok
    EF_CHECK(virtualization_checks(v).empty());
}

EF_TEST(virtualization_detected_is_info_with_signals) {
    VirtualizationInfo v;
    v.hyperv_service = true;
    v.whp_dll = true;
    const auto checks = virtualization_checks(v);
    const auto c = find(checks, "system.virtualization");
    EF_CHECK(c.has_value());
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->text_key == "chk.virtualization.detected");
    EF_CHECK(c->args.at("signals").find("Hyper-V") != std::string::npos);
    EF_CHECK(c->args.at("signals").find("Windows Hypervisor Platform") != std::string::npos);
    EF_CHECK(c->action == ActionKind::None);
}

EF_TEST(virtualization_wsl2_default_is_advice) {
    VirtualizationInfo v;
    v.wsl_exe = true;
    v.wsl_default_v2 = true;
    v.vmp_service = true;
    const auto c = find(virtualization_checks(v), "system.virtualization");
    EF_CHECK(c.has_value());
    EF_CHECK(c->level == Level::Advice);
    EF_CHECK(c->text_key == "chk.virtualization.wsl2");
    EF_CHECK(c->args.at("signals").find("WSL") != std::string::npos);
    EF_CHECK(c->args.at("signals").find("Sanal Makine Platformu") != std::string::npos);
}

EF_TEST(driver_health_makes_no_claim_without_enumeration) {
    EF_CHECK(display_driver_checks(DisplayDriverHealthInfo{}).empty());
    DisplayDriverHealthInfo d;
    d.enumeration_ok = true;
    d.nvidia_packages = 1;  // tek paket: kalinti iddiasi yok
    EF_CHECK(display_driver_checks(d).empty());
}

EF_TEST(driver_health_reports_problem_devices) {
    DisplayDriverHealthInfo d;
    d.enumeration_ok = true;
    d.problem_devices = {"NVIDIA GeForce RTX 4070", "Intel UHD Graphics"};
    const auto c = find(display_driver_checks(d), "driver.device_problem");
    EF_CHECK(c.has_value());
    EF_CHECK(c->level == Level::Advice);
    EF_CHECK(c->text_key == "chk.driver.device_problem");
    EF_CHECK(c->args.at("count") == "2");
    EF_CHECK(c->args.at("names").find("NVIDIA") != std::string::npos);
    EF_CHECK(c->action == ActionKind::None);
}

EF_TEST(driver_health_reports_multiple_packages_only_above_one) {
    DisplayDriverHealthInfo d;
    d.enumeration_ok = true;
    d.nvidia_packages = 2;
    d.amd_packages = 1;
    d.intel_packages = 3;
    const auto c = find(display_driver_checks(d), "driver.multiple_packages");
    EF_CHECK(c.has_value());
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->text_key == "chk.driver.multiple_packages");
    EF_CHECK(c->args.at("vendors").find("NVIDIA 2") != std::string::npos);
    EF_CHECK(c->args.at("vendors").find("AMD") == std::string::npos);  // 1 paket: kalinti iddiasi yok
    EF_CHECK(c->args.at("vendors").find("Intel 3") != std::string::npos);
}

EF_TEST(compat_keys_are_localized) {
    for (const char* key : {"chk.virtualization.detected", "chk.virtualization.wsl2",
                            "chk.driver.device_problem", "chk.driver.multiple_packages"}) {
        EF_CHECK(ef::core::i18n::id_of(key).has_value());
    }
}
