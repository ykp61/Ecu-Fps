#include "core/adapters/windows/update_check.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using ef::contracts::Field;
using ef::contracts::Provenance;

EF_TEST(nvidia_display_version_mapping) {
    EF_CHECK(nvidia_display_version("32.0.16.2101") == std::optional<std::string>("621.01"));
    EF_CHECK(nvidia_display_version("32.0.15.6094") == std::optional<std::string>("560.94"));
    EF_CHECK(nvidia_display_version("31.0.15.3623") == std::optional<std::string>("536.23"));
    EF_CHECK(!nvidia_display_version(""));
    EF_CHECK(!nvidia_display_version("32.0.16"));
    EF_CHECK(!nvidia_display_version("32.0.16.2101.5"));
    EF_CHECK(!nvidia_display_version("a.b.c.d"));
    EF_CHECK(!nvidia_display_version("32.0.16.99999"));
}

EF_TEST(official_urls_are_fixed_https_allowlist) {
    for (auto v : {GpuVendor::Nvidia, GpuVendor::Amd, GpuVendor::Intel}) {
        const auto u = official_driver_url(v);
        EF_CHECK(u.substr(0, 8) == "https://" && is_allowed_web_url(u));
    }
    EF_CHECK(official_driver_url(GpuVendor::Other).empty());
    EF_CHECK(!is_allowed_web_url(""));
    EF_CHECK(!is_allowed_web_url("https://example.com/driver.exe"));
    EF_CHECK(!is_allowed_web_url("http://www.nvidia.com/Download/index.aspx"));
    EF_CHECK(vendor_from_pci(0x10DE) == GpuVendor::Nvidia && vendor_from_pci(0x1002) == GpuVendor::Amd &&
              vendor_from_pci(0x8086) == GpuVendor::Intel && vendor_from_pci(0x1414) == GpuVendor::Other);
}

namespace {
DriverInfo drv(std::uint32_t ven, std::optional<std::int64_t> days) {
    DriverInfo d;
    d.device_name = "GPU";
    d.vendor_id = ven;
    d.version = Field<std::string>::known("32.0.16.2101", Provenance::Api);
    d.date_unix_days = days ? Field<std::int64_t>::known(*days, Provenance::Api) : Field<std::int64_t>::unknown("x");
    return d;
}
}  // namespace

EF_TEST(update_check_findings_and_no_install) {
    UpdateStatus s;
    s.reboot_pending = Field<bool>::known(false, Provenance::Api);
    const std::int64_t today = 20000;
    auto r = evaluate_update_check({drv(0x10DE, today - 30), drv(0x1002, today - 100)}, s, today);
    EF_CHECK(r.findings.empty() && !r.installs_anything && r.drivers.size() == 2);

    s.reboot_pending = Field<bool>::known(true, Provenance::Api);
    r = evaluate_update_check({drv(0x10DE, today - 400), drv(0x1414, today - 9000), drv(0x8086, std::nullopt)}, s, today);
    EF_CHECK(r.findings.size() == 3);
    EF_CHECK(r.findings[0].code == UpdateCheckCode::RebootPending && r.findings[0].url.empty());
    EF_CHECK(r.findings[1].code == UpdateCheckCode::DriverOld && is_allowed_web_url(r.findings[1].url));
    EF_CHECK(r.findings[2].code == UpdateCheckCode::DriverDateUnknown);
    EF_CHECK(!r.installs_anything);
}

EF_TEST(update_check_unknown_reboot_is_not_finding) {
    UpdateStatus s;
    s.reboot_pending = Field<bool>::unknown("WUA unavailable");
    EF_CHECK(evaluate_update_check({}, s, 20000).findings.empty());
}

EF_TEST(update_check_texts_exist) {
    for (auto c : {UpdateCheckCode::RebootPending, UpdateCheckCode::DriverOld, UpdateCheckCode::DriverDateUnknown}) {
        bool found = false;
        for (std::size_t i = 0; i < ef::core::i18n::kStringCount; ++i)
            if (ef::core::i18n::key_of(static_cast<ef::core::i18n::StringId>(i)) == text_key(c)) found = true;
        EF_CHECK(found);
    }
}
