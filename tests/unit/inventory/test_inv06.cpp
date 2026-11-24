#include <algorithm>

#include "core/i18n/catalog.h"
#include "core/inventory/health.h"
#include "ef_test.h"

using namespace ef::core::inventory;

namespace {
bool has(const std::vector<HealthFinding>& v, HealthCode c) {
    return std::any_of(v.begin(), v.end(), [c](const HealthFinding& f) { return f.code == c; });
}
std::int64_t day(const char* d) { return *parse_bios_date_days(d); }
}  // namespace

EF_TEST(rated_speed_from_part_numbers) {
    EF_CHECK(rated_speed_from_part("F5-6000J3038F16G") == 6000u);
    EF_CHECK(rated_speed_from_part("KF560C36-16") == 5600u);
    EF_CHECK(rated_speed_from_part("CMK32GX5M2B6000C36") == 6000u);
    EF_CHECK(rated_speed_from_part("AX5U6000C3016G-B") == 6000u);
    EF_CHECK(!rated_speed_from_part("M425R1GB4BB0-CQKOL").has_value());
    EF_CHECK(!rated_speed_from_part("").has_value());
}

EF_TEST(bios_date_parsing) {
    EF_CHECK(day("01/01/1970") == 0);
    EF_CHECK(day("06/30/2026") - day("06/30/2025") == 365);
    EF_CHECK(!parse_bios_date_days("2026-06-30").has_value());
    EF_CHECK(!parse_bios_date_days("13/01/2026").has_value());
}

EF_TEST(xmp_off_detected_from_part_number) {
    HealthInput in;
    in.ram = {{4800, 4800, "F5-6000J3038F16G", "DDR5"}, {4800, 4800, "F5-6000J3038F16G", "DDR5"}};
    const auto f = evaluate_health(in);
    EF_CHECK(has(f, HealthCode::XmpLikelyOff));
    EF_CHECK(!has(f, HealthCode::SingleDimm));
    const auto it = std::find_if(f.begin(), f.end(), [](const HealthFinding& x) { return x.code == HealthCode::XmpLikelyOff; });
    EF_CHECK(it->fix == FixKind::GuidedBios);  // BIOS'a yazilmaz, rehber
    EF_CHECK(it->evidence.find("4800") != std::string::npos);
}

EF_TEST(xmp_on_or_unknown_has_no_finding) {
    HealthInput in;
    in.ram = {{6000, 6000, "", "DDR5"}, {6000, 6000, "", "DDR5"}};  // bu PC
    EF_CHECK(!has(evaluate_health(in), HealthCode::XmpLikelyOff));
    in.ram = {{std::nullopt, std::nullopt, "", "DDR5"}, {std::nullopt, std::nullopt, "", ""}};
    EF_CHECK(evaluate_health(in).empty());
}

EF_TEST(single_dimm_and_pcie_and_rebar) {
    HealthInput in;
    in.ram = {{3200, 3200, "", "DDR4"}};
    in.dgpu_pcie = PcieInput{8, 16, 1, 4};  // gen1 bostayken normal; genislik dusuk
    in.rebar_enabled = false;
    const auto f = evaluate_health(in);
    EF_CHECK(has(f, HealthCode::SingleDimm));
    EF_CHECK(has(f, HealthCode::PcieWidthReduced));
    EF_CHECK(has(f, HealthCode::ResizableBarOff));
    for (const auto& x : f) {  // kanit duzeyi iddiayla uyumlu: olculmeyen sey kesin yazilmaz
        if (x.code == HealthCode::SingleDimm) EF_CHECK(x.confidence != ef::contracts::Confidence::High && x.evidence.find("not measured") != std::string::npos);
        if (x.code == HealthCode::ResizableBarOff) EF_CHECK(x.confidence == ef::contracts::Confidence::Low && x.evidence.find("inferred") != std::string::npos);
    }
    in.dgpu_pcie = PcieInput{16, 16, 1, 5};  // yalniz hiz dusuk (bosta guc tasarrufu) -> bulgu yok
    in.rebar_enabled = std::nullopt;
    const auto g = evaluate_health(in);
    EF_CHECK(!has(g, HealthCode::PcieWidthReduced));
    EF_CHECK(!has(g, HealthCode::ResizableBarOff));
}

EF_TEST(bios_age_and_disk_space) {
    HealthInput in;
    in.today_unix_days = day("11/04/2026");
    in.bios_date = "06/30/2026";
    EF_CHECK(!has(evaluate_health(in), HealthCode::BiosOld));
    in.bios_date = "01/15/2024";
    EF_CHECK(has(evaluate_health(in), HealthCode::BiosOld));
    for (const auto& x : evaluate_health(in))
        if (x.code == HealthCode::BiosOld) EF_CHECK(x.confidence == ef::contracts::Confidence::Low && x.evidence.find("no newer version verified") != std::string::npos);
    in.volumes = {{"C:\\", 952, 649}, {"D:\\", 931, 50}, {"X:\\", 8, 0}};
    const auto f = evaluate_health(in);
    EF_CHECK(std::count_if(f.begin(), f.end(), [](const HealthFinding& x) { return x.code == HealthCode::LowDiskSpace; }) == 1);
}

EF_TEST(every_health_finding_has_catalog_text) {
    auto c = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(c.ok());
    for (auto code : {HealthCode::XmpLikelyOff, HealthCode::SingleDimm, HealthCode::PcieWidthReduced,
                      HealthCode::ResizableBarOff, HealthCode::BiosOld, HealthCode::LowDiskSpace}) {
        bool found = false;
        for (std::size_t i = 0; i < ef::core::i18n::kStringCount; ++i)
            if (ef::core::i18n::key_of(static_cast<ef::core::i18n::StringId>(i)) == text_key(code)) found = true;
        EF_CHECK(found);
    }
}
