#include <algorithm>

#include "core/i18n/catalog.h"
#include "core/inventory/power_security.h"
#include "ef_test.h"

using namespace ef::core::inventory;
using ef::contracts::Field;
using ef::contracts::Provenance;

namespace {
bool has(const std::vector<PowerNote>& v, PowerNote n) { return std::find(v.begin(), v.end(), n) != v.end(); }
}  // namespace

EF_TEST(power_guid_mapping) {
    EF_CHECK(overlay_from_guid("961cc777-2547-4f9d-8174-7d86181b8a7a") == PowerOverlay::BestEfficiency);
    EF_CHECK(overlay_from_guid("ded574b5-45a0-4f42-8737-46345c09c238") == PowerOverlay::BestPerformance);
    EF_CHECK(overlay_from_guid("00000000-0000-0000-0000-000000000000") == PowerOverlay::Balanced);
    EF_CHECK(plan_from_guid("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") == PowerPlan::HighPerformance);
    EF_CHECK(plan_from_guid("e9a42b02-d5df-448d-aa00-03f14749eb61") == PowerPlan::Ultimate);
    EF_CHECK(plan_from_guid("deadbeef-0000-0000-0000-000000000000") == PowerPlan::Custom);
}

EF_TEST(laptop_on_battery_notes) {
    PowerSecurity s;
    s.has_battery = Field<bool>::known(true, Provenance::Api);
    s.on_ac = Field<bool>::known(false, Provenance::Api);
    s.battery_saver = Field<bool>::known(true, Provenance::Api);
    s.overlay_guid = Field<std::string>::known("961cc777-2547-4f9d-8174-7d86181b8a7a", Provenance::Api);
    const auto n = power_security_notes(s);
    EF_CHECK(has(n, PowerNote::OnBattery));
    EF_CHECK(has(n, PowerNote::BatterySaverOn));
    EF_CHECK(has(n, PowerNote::EfficiencyOverlay));
}

EF_TEST(desktop_without_battery_has_no_battery_notes) {
    PowerSecurity s;
    s.has_battery = Field<bool>::known(false, Provenance::Api);
    s.on_ac = Field<bool>::known(true, Provenance::Api);
    s.battery_saver = Field<bool>::known(false, Provenance::Api);
    s.hvci_running = Field<bool>::known(true, Provenance::Wmi);
    const auto n = power_security_notes(s);
    EF_CHECK(!has(n, PowerNote::OnBattery));
    EF_CHECK(!has(n, PowerNote::BatteryStatusUnknown));
    EF_CHECK(has(n, PowerNote::HvciRunning));
}

EF_TEST(unknown_battery_differs_from_unsupported) {
    PowerSecurity s;
    s.has_battery = Field<bool>::known(true, Provenance::Api);
    s.on_ac = Field<bool>::unknown("AC status unknown");
    EF_CHECK(has(power_security_notes(s), PowerNote::BatteryStatusUnknown));
    // OEM modu: standart API yok -> nota cevrilmez, unknown+neden olarak kalir
    s.oem_mode = Field<std::string>::unknown("no standard Windows API for OEM performance modes");
    EF_CHECK(!s.oem_mode.has());
}

EF_TEST(every_note_has_catalog_text) {
    for (auto l : {ef::core::i18n::Locale::TrTR, ef::core::i18n::Locale::EnUS}) {
        auto c = ef::core::i18n::Catalog::embedded(l);
        EF_CHECK(c.ok());
        for (auto n : {PowerNote::OnBattery, PowerNote::BatterySaverOn, PowerNote::EfficiencyOverlay, PowerNote::PowerSaverPlan,
                       PowerNote::HvciRunning, PowerNote::VbsRunning, PowerNote::BatteryStatusUnknown}) {
            bool found = false;
            for (std::size_t i = 0; i < ef::core::i18n::kStringCount; ++i)
                if (ef::core::i18n::key_of(static_cast<ef::core::i18n::StringId>(i)) == note_key(n)) found = true;
            EF_CHECK(found);
        }
    }
}
