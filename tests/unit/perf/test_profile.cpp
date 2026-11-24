#include "core/perf/profile.h"
#include "core/recovery/journal.h"
#include "ef_test.h"

using namespace ef::core::perf;

EF_TEST(profile_invalidation_and_general_fallback) {
    const std::map<std::string, bool> general{{"plan.power_mode", true}, {"plan.vfx", false}};
    EF_CHECK(profile_status(std::nullopt, "1", "621.01") == ProfileStatus::NoProfile);
    GameProfile p;
    p.game_id = "steam:730", p.game_version = "26393695", p.driver_version = "621.01", p.groups = {{"plan.vfx", true}};
    EF_CHECK(profile_status(p, "26393695", "621.01") == ProfileStatus::Valid);
    EF_CHECK(effective_groups(p, ProfileStatus::Valid, general).at("plan.vfx"));
    EF_CHECK(profile_status(p, "26400000", "621.01") == ProfileStatus::GameUpdated);
    EF_CHECK(profile_status(p, "26393695", "622.00") == ProfileStatus::DriverUpdated);
    // Gecersiz profil: genel secim (ilk acilista uzun benchmark zorunlu degil)
    EF_CHECK(!effective_groups(p, ProfileStatus::GameUpdated, general).at("plan.vfx"));
    const auto u = update_profile(p, {{"plan.vfx", false}}, "26400000", "621.01");
    EF_CHECK(u.revision == 2 && !u.measured && profile_status(u, "26400000", "621.01") == ProfileStatus::Valid);
}

EF_TEST(profile_no_infinite_write_race) {
    DriftGuard g;
    EF_CHECK(g.observe("powercfg:scheme", "high", "high") == DriftAction::None);
    EF_CHECK(g.observe("powercfg:scheme", "high", "balanced") == DriftAction::WarnOnce);  // baska arac degistirdi
    EF_CHECK(g.observe("powercfg:scheme", "high", "balanced") == DriftAction::Ceasefire);  // ikinci kez: biz birakiriz
    EF_CHECK(g.ceasefire("powercfg:scheme") && g.ceasefire_targets().size() == 1 && !g.ceasefire("vfx"));
}

EF_TEST(profile_update_does_not_touch_journal) {
    // Profil guncellemesi yalniz secimi degistirir; journal islemi kendi snapshot'ini tasir ve degismez.
    ef::core::recovery::JournalTx tx;
    tx.id = "0123456789abcdef0123456789abcdef";
    ef::core::recovery::JournalStep st;
    st.snapshot.old_value = "a1841308";
    tx.steps.push_back(st);
    const auto before = ef::core::json::write(ef::core::recovery::to_json(tx));
    GameProfile p;
    p.groups = {{"plan.power_scheme", true}};
    (void)update_profile(p, {{"plan.power_scheme", false}}, "2", "1");
    EF_CHECK(ef::core::json::write(ef::core::recovery::to_json(tx)) == before && tx.steps[0].snapshot.old_value == "a1841308");
}
