// F-65/F-67: sistem sagligi kurallari (saf; Win32 cagrisi yok). Okunamayan deger Unknown kalir,
// sifir veya "sorun yok" varsayilmaz; hicbir ayar yazilmaz.
#include <cstdint>
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

SystemHealthInfo clean() {
    SystemHealthInfo s;
    s.uptime_s = 3600;
    s.commit_used_pct = 40.0;
    s.pagefile_configured = true;
    s.pagefile_usage_pct = 10.0;
    s.standby_mib = 100.0;
    s.nonpaged_pool_mib = 100.0;
    s.compressed_mib = 100.0;
    s.system_drive_free_ratio = 0.5;
    s.trim_enabled = true;
    return s;
}

}  // namespace

EF_TEST(system_health_empty_info_only_reports_unknown_trim) {
    const auto v = system_health_checks(SystemHealthInfo{});
    EF_CHECK(v.size() == 1);
    EF_CHECK(v[0].id == "system.trim" && v[0].level == Level::Unknown);
    EF_CHECK(v[0].text_key == "chk.system.trim_unknown");
    EF_CHECK(v[0].section == Section::Hardware);
}

EF_TEST(system_health_clean_has_no_findings) {
    const auto v = system_health_checks(clean());
    EF_CHECK(v.empty());  // TRIM acik: satir uretilmez
}

EF_TEST(system_health_uptime_advice_at_seven_days) {
    SystemHealthInfo s = clean();
    s.uptime_s = 7ull * 86400ull - 1ull;
    EF_CHECK(!find(system_health_checks(s), "system.uptime"));
    s.uptime_s = 7ull * 86400ull;
    const auto u = find(system_health_checks(s), "system.uptime");
    EF_CHECK(u && u->level == Level::Advice && u->text_key == "chk.system.uptime_long");
    EF_CHECK(u && u->args.at("days") == "7");
    s.uptime_s = 15ull * 86400ull + 3600ull;
    EF_CHECK(find(system_health_checks(s), "system.uptime")->args.at("days") == "15");
}

EF_TEST(system_health_pagefile_missing_needs_commit_pressure) {
    SystemHealthInfo s = clean();
    s.pagefile_configured = false;
    s.pagefile_usage_pct.reset();
    EF_CHECK(!find(system_health_checks(s), "system.pagefile"));
    s.commit_used_pct = 85.0;
    const auto p = find(system_health_checks(s), "system.pagefile");
    EF_CHECK(p && p->level == Level::Problem && p->text_key == "chk.system.pagefile_missing");
    EF_CHECK(p && p->args.at("commit_pct") == "85.0");
    // Sayfa dosyasi yok ama commit basinci yok: sorun denmez.
    s.commit_used_pct = 84.9;
    EF_CHECK(!find(system_health_checks(s), "system.pagefile"));
}

EF_TEST(system_health_pagefile_usage_advice) {
    SystemHealthInfo s = clean();
    s.pagefile_usage_pct = 80.0;
    const auto p = find(system_health_checks(s), "system.pagefile.usage");
    EF_CHECK(p && p->level == Level::Advice && p->text_key == "chk.system.pagefile_high");
    EF_CHECK(p && p->args.at("usage_pct") == "80.0");
    s.pagefile_usage_pct = 79.9;
    EF_CHECK(!find(system_health_checks(s), "system.pagefile.usage"));
    // Yapilandirilmamis sayfa dosyasinda kullanim satiri anlamsiz: yalniz eksiklik kurali.
    s.pagefile_configured = false;
    s.pagefile_usage_pct = 95.0;
    s.commit_used_pct = 40.0;
    EF_CHECK(!find(system_health_checks(s), "system.pagefile.usage"));
}

EF_TEST(system_health_disk_free_bands) {
    SystemHealthInfo s = clean();
    s.system_drive_free_ratio = 0.09;
    const auto low = find(system_health_checks(s), "system.disk");
    EF_CHECK(low && low->level == Level::Problem && low->text_key == "chk.system.disk_low");
    EF_CHECK(low && low->args.at("free_pct") == "9.0");
    s.system_drive_free_ratio = 0.10;
    const auto tight = find(system_health_checks(s), "system.disk");
    EF_CHECK(tight && tight->level == Level::Advice && tight->text_key == "chk.system.disk_tight");
    EF_CHECK(tight && tight->args.at("free_pct") == "10.0");
    s.system_drive_free_ratio = 0.20;
    EF_CHECK(!find(system_health_checks(s), "system.disk"));
    s.system_drive_free_ratio.reset();
    EF_CHECK(!find(system_health_checks(s), "system.disk"));
}

EF_TEST(system_health_trim_off_and_unknown) {
    SystemHealthInfo s = clean();
    s.trim_enabled = false;
    const auto off = find(system_health_checks(s), "system.trim");
    EF_CHECK(off && off->level == Level::Advice && off->text_key == "chk.system.trim_off");
    s.trim_enabled.reset();
    const auto unknown = find(system_health_checks(s), "system.trim");
    EF_CHECK(unknown && unknown->level == Level::Unknown && unknown->text_key == "chk.system.trim_unknown");
}

EF_TEST(system_health_nonpaged_pool_bands) {
    SystemHealthInfo s = clean();
    s.nonpaged_pool_mib = 1023.9;
    EF_CHECK(!find(system_health_checks(s), "system.pool"));
    s.nonpaged_pool_mib = 1024.0;
    const auto advice = find(system_health_checks(s), "system.pool");
    EF_CHECK(advice && advice->level == Level::Advice && advice->text_key == "chk.system.pool_high");
    EF_CHECK(advice && advice->args.at("pool_gib") == "1.0");
    s.nonpaged_pool_mib = 4096.0;
    const auto problem = find(system_health_checks(s), "system.pool");
    EF_CHECK(problem && problem->level == Level::Problem);
    EF_CHECK(problem && problem->args.at("pool_gib") == "4.0");
}

EF_TEST(system_health_compression_needs_both_memory_and_commit) {
    SystemHealthInfo s = clean();
    s.compressed_mib = 2048.0;
    s.commit_used_pct = 85.0;
    const auto c = find(system_health_checks(s), "system.compression");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.system.compressed_high");
    EF_CHECK(c && c->args.at("compressed_gib") == "2.0");
    s.commit_used_pct = 84.9;
    EF_CHECK(!find(system_health_checks(s), "system.compression"));
    s.commit_used_pct = 99.0;
    s.compressed_mib = 2047.9;
    EF_CHECK(!find(system_health_checks(s), "system.compression"));
    s.compressed_mib.reset();
    EF_CHECK(!find(system_health_checks(s), "system.compression"));
}

EF_TEST(system_health_standby_is_info_only) {
    SystemHealthInfo s = clean();
    s.standby_mib = 8191.9;
    EF_CHECK(!find(system_health_checks(s), "system.standby"));
    s.standby_mib = 8192.0;
    const auto b = find(system_health_checks(s), "system.standby");
    EF_CHECK(b && b->level == Level::Info && b->text_key == "chk.system.standby_info");
    EF_CHECK(b && b->args.at("standby_gib") == "8.0");
}

EF_TEST(system_health_all_problem_ids_unique_and_keyed) {
    SystemHealthInfo s;
    s.uptime_s = 30ull * 86400ull;
    s.commit_used_pct = 99.0;
    s.pagefile_configured = false;
    s.nonpaged_pool_mib = 5000.0;
    s.compressed_mib = 3000.0;
    s.standby_mib = 9000.0;
    s.system_drive_free_ratio = 0.05;
    s.trim_enabled = false;
    const auto v = system_health_checks(s);
    EF_CHECK(v.size() == 7);
    for (std::size_t i = 0; i < v.size(); ++i) {
        EF_CHECK(v[i].section == Section::Hardware);
        EF_CHECK(v[i].text_key.rfind("chk.system.", 0) == 0);
        EF_CHECK(ef::core::i18n::id_of(v[i].text_key).has_value());
        for (std::size_t j = i + 1; j < v.size(); ++j) EF_CHECK(v[i].id != v[j].id);
    }
}
