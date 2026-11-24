// EF-X25 T04: onay ekrani grup gorunumu (ana / gelismis ayrimi) ve durust etiket anahtarlari.
#include <map>
#include <string>

#include "core/i18n/catalog.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using namespace ef::core::perf;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
PlanItem item(const char* group) {
    PlanItem i;
    i.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, "t", "1", false, false};
    i.title_key = group;
    i.group_key = group;
    return i;
}
const GroupView* find(const std::vector<GroupView>& v, std::string_view key) {
    for (const auto& g : v)
        if (g.key == key) return &g;
    return nullptr;
}
}  // namespace

EF_TEST(group_view_default_off_group_is_advanced) {
    Plan p;
    p.items = {item("plan.power_mode"), item("plan.background")};
    const auto v = visible_groups(p, {});
    EF_CHECK(v.size() == 2);
    EF_CHECK(find(v, "plan.power_mode") && !find(v, "plan.power_mode")->advanced);
    EF_CHECK(find(v, "plan.background") && find(v, "plan.background")->advanced);
}

EF_TEST(group_view_explicitly_selected_default_off_group_moves_to_main) {
    Plan p;
    p.items = {item("plan.background")};
    EF_CHECK(!find(visible_groups(p, {{"plan.background", true}}), "plan.background")->advanced);
    EF_CHECK(find(visible_groups(p, {{"plan.background", false}}), "plan.background")->advanced);  // acik Hayir: gelismis kalir
}

EF_TEST(group_view_default_on_group_stays_main_even_when_turned_off) {
    Plan p;
    p.items = {item("plan.vfx")};
    EF_CHECK(!find(visible_groups(p, {{"plan.vfx", false}}), "plan.vfx")->advanced);  // kullanici kapatti; satir yerinde kalir
}

EF_TEST(group_view_empty_groups_are_not_shown) {
    Plan p;
    p.items = {item("plan.vfx")};
    const auto v = visible_groups(p, {});
    EF_CHECK(v.size() == 1 && v[0].key == "plan.vfx");
    EF_CHECK(visible_groups(Plan{}, {}).empty());
}

EF_TEST(group_view_evidence_and_risk_table_matches_owner_amendments) {
    Plan p;
    for (const auto& g : plan_groups()) p.items.push_back(item(std::string(g.key).c_str()));
    // item() anahtari c_str kopyalar (title_key std::string): hepsi gorunur olmali.
    const auto v = visible_groups(p, {});
    EF_CHECK(v.size() == plan_groups().size());
    for (const auto& g : {"plan.power_mode", "plan.power_scheme", "plan.vfx", "plan.gaming", "plan.windowed", "plan.nvidia"}) {
        EF_CHECK(find(v, g)->evidence == Evidence::Documented && find(v, g)->risk == Risk::Low);
    }
    for (const auto& g : {"plan.power_cpu", "plan.power_pcie", "plan.hags", "plan.services"}) {
        EF_CHECK(find(v, g)->evidence == Evidence::Documented && find(v, g)->risk == Risk::Medium);
    }
    EF_CHECK(find(v, "plan.background")->evidence == Evidence::Disputed && find(v, "plan.background")->risk == Risk::Low);
}

EF_TEST(group_view_label_keys_are_distinct_and_resolve_in_both_locales) {
    namespace i18n = ef::core::i18n;
    const Evidence evs[] = {Evidence::Measured, Evidence::Documented, Evidence::Disputed};
    const Risk rks[] = {Risk::Low, Risk::Medium, Risk::High};
    std::map<std::string, int> seen;
    for (const auto e : evs) ++seen[std::string(evidence_key(e))];
    for (const auto r : rks) ++seen[std::string(risk_key(r))];
    EF_CHECK(seen.size() == 6);
    for (const auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const auto& [key, n] : seen) {
            const auto id = i18n::id_of(key);
            EF_CHECK(id.has_value());
            if (id) EF_CHECK(!cat.value().get(*id).empty());
        }
        EF_CHECK(cat.value().get(i18n::StringId::PlanAdvanced).find("{n}") != std::string_view::npos);
    }
}
