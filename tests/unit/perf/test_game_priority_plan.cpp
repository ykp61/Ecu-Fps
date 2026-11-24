// T09 (ADR-035): oyun onceligi plani. Sahte kayit defteri; gercek HKLM DEGISMEZ.
#include <string>
#include <vector>

#include "core/adapters/windows/ifeo_priority.h"
#include "core/broker/admin.h"
#include "core/perf/controller.h"
#include "core/perf/game_priority_plan.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace cw = ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
const std::wstring kCs2Perf = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\cs2.exe\\PerfOptions";

struct Rig {
    cw::FakeMachineRegistry reg;
    cw::IfeoPriorityAdapter adapter{&reg};
    GamePriorityInput in;
    Rig() {
        in.scan_trusted = true;
        in.faceit = FaceitState::Absent;
        in.games = {{"steam", "730", "", "Counter-Strike 2"}, {"steam", "570", "", "Dota 2"}, {"steam", "400", "", "Portal"}};
    }
    std::vector<PlanItem> plan() { return game_priority_plan_items(adapter, in); }
};
}  // namespace

EF_TEST(gpplan_offers_only_allowlisted_installed_games) {
    Rig r;
    const auto items = r.plan();
    EF_CHECK(items.size() == 2);  // Portal izin listesinde degil
    EF_CHECK(items[0].op.id == OperationId::GamePriorityHint && items[0].op.scope == OperationScope::Machine);
    EF_CHECK(items[0].op.target == "ifeoprio:cs2.exe" && items[0].op.new_value == "above_normal");
    EF_CHECK(items[1].op.target == "ifeoprio:dota2.exe");
    for (const auto& i : items) {
        EF_CHECK(i.needs_admin && i.group() == "plan.game_priority" && i.detail);
        EF_CHECK(ef::core::broker::admin_operation_allowed(i.op));
    }
    EF_CHECK(items[0].detail->name == "Counter-Strike 2");
}

EF_TEST(gpplan_none_when_not_installed_or_scan_untrusted) {
    Rig r;
    r.in.games.clear();  // oyun kurulu degil
    EF_CHECK(r.plan().empty());
    Rig u;
    u.in.scan_trusted = false;  // guvenilmez tarama
    EF_CHECK(u.plan().empty());
    Rig other;
    other.in.games = {{"steam", "400", "", "Portal"}, {"epic", "Fortnite", "FortniteClient-Win64-Shipping.exe", "Fortnite"}};
    EF_CHECK(other.plan().empty());
}

EF_TEST(gpplan_kernel_anticheat_games_are_skipped) {
    EF_CHECK(game_kernel_protected("1172470", FaceitState::Absent));   // Apex: EAC
    EF_CHECK(game_kernel_protected("578080", FaceitState::Absent));    // PUBG: BattlEye
    EF_CHECK(game_kernel_protected("valorant.live", FaceitState::Absent));
    EF_CHECK(game_kernel_protected("1938090", FaceitState::Absent));   // Ricochet
    EF_CHECK(!game_kernel_protected("570", FaceitState::Unknown));     // Dota 2: yalniz VAC (matriste yok)
    EF_CHECK(!game_kernel_protected("", FaceitState::Present));
    Rig r;
    // Izin listesindeki exe ipucu olsa bile korumali oyun kimligi (ornek EAC) elenir.
    r.in.games = {{"epic", "Fortnite", "dota2.exe", "Garip"}};
    EF_CHECK(r.plan().empty());
}

EF_TEST(gpplan_cs2_is_skipped_when_faceit_present_or_unknown) {
    Rig r;
    r.in.faceit = FaceitState::Present;
    auto items = r.plan();
    EF_CHECK(items.size() == 1 && items[0].op.target == "ifeoprio:dota2.exe");
    r.in.faceit = FaceitState::Unknown;  // servis yoneticisi okunamadi: fail-closed
    items = r.plan();
    EF_CHECK(items.size() == 1 && items[0].op.target == "ifeoprio:dota2.exe");
    r.in.faceit = FaceitState::Absent;
    EF_CHECK(r.plan().size() == 2);
}

EF_TEST(gpplan_existing_value_or_unreadable_registry_gives_no_item) {
    Rig r;
    r.reg.values[cw::FakeMachineRegistry::key(kCs2Perf, L"CpuPriorityClass")] = cw::dword_raw(3);  // kullanicinin kendi ayari: ezilmez
    auto items = r.plan();
    EF_CHECK(items.size() == 1 && items[0].op.target == "ifeoprio:dota2.exe");
    r.reg.fail_read = true;
    EF_CHECK(r.plan().empty());
    cw::IfeoPriorityAdapter none{nullptr};
    EF_CHECK(game_priority_plan_items(none, r.in).empty());
}

EF_TEST(gpplan_exe_hint_matches_case_insensitively_and_dedupes) {
    Rig r;
    r.in.games = {{"xbox", "Microsoft.MinecraftUWP", "C:\\XboxGames\\Minecraft\\Minecraft.Windows.exe", "Minecraft"},
                  {"xbox", "Microsoft.MinecraftUWP", "minecraft.windows.exe", "Minecraft kopya"}};
    const auto items = r.plan();
    EF_CHECK(items.size() == 1 && items[0].op.target == "ifeoprio:minecraft.windows.exe");
}

EF_TEST(gpplan_group_is_advanced_default_no_low_risk) {
    bool found = false;
    for (const auto& g : plan_groups())
        if (g.key == "plan.game_priority") {
            found = true;
            EF_CHECK(!g.default_on && g.risk == Risk::Low && g.evidence == Evidence::Disputed);
        }
    EF_CHECK(found);
}
