// T11 (ADR-035): Defender dislama plani. Sahte DefenderApi; gercek Defender DEGISMEZ.
#include <string>
#include <vector>

#include "core/adapters/windows/defender_exclusion.h"
#include "core/broker/admin.h"
#include "core/perf/controller.h"
#include "core/perf/defender_plan.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace cw = ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
std::vector<DefenderFolder> folders(std::size_t n) {
    std::vector<DefenderFolder> v;
    for (std::size_t i = 0; i < n; ++i) v.push_back({"D:\\Games\\Game" + std::to_string(i), "Game" + std::to_string(i)});
    return v;
}
}  // namespace

EF_TEST(defender_plan_empty_when_not_usable_or_unknown) {
    cw::FakeDefenderApi api;
    api.usable_value = false;  // ucuncu taraf antivirus / ilke
    EF_CHECK(defender_plan_items(api, folders(3)).empty());
    api.usable_value = std::nullopt;
    EF_CHECK(defender_plan_items(api, folders(3)).empty());
    api.usable_value = true;
    EF_CHECK(defender_plan_items(api, folders(3)).size() == 3);
    EF_CHECK(defender_plan_items(api, {}).empty());
}

EF_TEST(defender_plan_items_are_machine_admin_and_pass_broker_rule) {
    cw::FakeDefenderApi api;
    const auto items = defender_plan_items(api, {{"D:\\SteamLibrary\\steamapps\\common\\Portal", "Portal"}});
    EF_CHECK(items.size() == 1);
    const auto& i = items[0];
    EF_CHECK(i.op.id == OperationId::DefenderExclusionSet && i.op.scope == OperationScope::Machine && i.op.new_value == "1");
    EF_CHECK(i.needs_admin && i.group() == "plan.defender" && i.detail && i.detail->name == "Portal");
    EF_CHECK(ef::core::broker::admin_operation_allowed(i.op));
}

EF_TEST(defender_plan_skips_already_excluded_when_list_is_readable) {
    cw::FakeDefenderApi api;
    api.paths = {"d:\\games\\GAME1"};
    const auto items = defender_plan_items(api, folders(3));
    EF_CHECK(items.size() == 2);
    // Liste okunamiyorsa (yonetici degil) tum uygun klasorler sunulur; onceden dislanmis olan broker'da yakalanir.
    api.deny_list = true;
    EF_CHECK(defender_plan_items(api, folders(3)).size() == 3);
}

EF_TEST(defender_plan_cap_dedup_and_unsafe_folders) {
    cw::FakeDefenderApi api;
    EF_CHECK(defender_plan_items(api, folders(40)).size() == kDefenderMaxItems);
    auto f = folders(2);
    f.push_back({"d:/games/GAME0/", "dup"});  // ayni klasor
    EF_CHECK(defender_plan_items(api, f).size() == 2);
    // Kok / sistem / profil / steamapps\common koku guvenilir listeden gelse de sunulmaz.
    const std::vector<DefenderFolder> bad{{"D:\\", "d"}, {"C:\\Windows", "w"}, {"C:\\Users\\Ali", "u"}, {"D:\\Lib\\steamapps\\common", "c"},
                                          {"C:\\Program Files", "p"}};
    EF_CHECK(defender_plan_items(api, bad).empty());
}

EF_TEST(defender_group_is_advanced_default_no_high_risk) {
    bool found = false;
    for (const auto& g : plan_groups())
        if (g.key == "plan.defender") {
            found = true;
            EF_CHECK(!g.default_on && g.risk == Risk::High && g.evidence == Evidence::Disputed);
        }
    EF_CHECK(found);
}
