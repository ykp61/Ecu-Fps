#include "core/adapters/windows/background_process.h"
#include "core/perf/background_plan.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace cw = ef::core::adapters::windows;
namespace contracts = ef::contracts;

namespace {
BgCandidate cand(std::uint32_t pid, const char* name, double cpu) {
    BgCandidate c;
    c.rule_id = "discord";
    c.display_name = name;
    c.cpu_pct = cpu;
    c.working_set_bytes = 400ull << 20;
    c.processes.push_back({pid, 5000 + pid, "C:\\Users\\x\\AppData\\Local\\Discord\\Discord.exe"});
    return c;
}
}  // namespace

EF_TEST(bgplan_items_are_low_priority_process_ops_in_background_group) {
    auto c = cand(10, "Discord", 2.0);
    c.processes.push_back({11, 5011, "C:\\Users\\x\\AppData\\Local\\Discord\\Discord.exe"});
    const auto items = background_plan_items({c});
    EF_CHECK(items.size() == 2);
    for (const auto& i : items) {
        EF_CHECK(i.op.id == contracts::OperationId::BackgroundProcessLower);
        EF_CHECK(i.op.scope == contracts::OperationScope::Process);
        EF_CHECK(i.op.new_value == "low");
        EF_CHECK(i.group() == kBackgroundGroup && !i.needs_admin && !i.changes_startup);
        EF_CHECK(!i.op.affects_quality && !i.op.needs_reboot);
        EF_CHECK(cw::parse_bg_process_target(i.op.target).ok());
        EF_CHECK(i.detail && i.detail->name == "Discord");
    }
    // Iki surec, tek program satiri.
    Plan p;
    p.items = items;
    const auto d = group_field_details(p, kBackgroundGroup);
    EF_CHECK(d.size() == 1 && d[0].ram_bytes == (400ull << 20));
}

EF_TEST(bgplan_skips_incomplete_identity_and_caps_size) {
    BgCandidate c = cand(10, "Discord", 2.0);
    c.processes.push_back({0, 1, "C:\\x.exe"});
    c.processes.push_back({12, 0, "C:\\x.exe"});
    c.processes.push_back({13, 5013, ""});
    EF_CHECK(background_plan_items({c}).size() == 1);
    BgCandidate big;
    big.display_name = "Many";
    for (std::uint32_t i = 1; i <= 40; ++i) big.processes.push_back({i, 9000 + i, "C:\\x.exe"});
    EF_CHECK(background_plan_items({big}).size() == kBackgroundMaxProcesses);
    EF_CHECK(background_plan_items({}).empty());
}

EF_TEST(bgplan_group_defaults_to_not_selected_until_user_chooses) {
    // Arka plan grubu kullanici acikca Evet demeden secili degildir; diger gruplar varsayilan Evet kalir.
    bool found = false;
    for (const auto& g : plan_groups())
        if (g.key == kBackgroundGroup) {
            found = true;
            EF_CHECK(!g.default_on);
        } else if (g.key == "plan.privacy_system") {  // T14: HKLM gizlilik; ek UAC acmamak icin varsayilan Hayir
            EF_CHECK(!g.default_on);
        } else if (g.key == "plan.win11_cleanup" || g.key == "plan.input" || g.key == "plan.widgets") {  // T21 + R-C3: Gelismis; yalniz acikca Evet
            EF_CHECK(!g.default_on);
        } else if (g.key == "plan.update_off") {  // T03: yuksek riskli ikinci acik-secim grubu
            EF_CHECK(!g.default_on);
        } else if (g.key == "plan.defender") {  // T11: guvenlik odunu; yalniz acikca Evet
            EF_CHECK(!g.default_on);
        } else if (g.key == "plan.game_priority") {  // T09: Gelismis; yalniz acikca Evet
            EF_CHECK(!g.default_on);
        } else if (g.key.rfind("plan.debloat.", 0) == 0) {  // T13: Gelismis; yalniz acikca Evet
            EF_CHECK(!g.default_on);
        } else if (g.key.rfind("plan.feature.", 0) == 0) {  // T12: ozellik sihirbazi sorulari (Kullanmiyorsaniz Evet deyin)
            EF_CHECK(!g.default_on);
        } else if (g.key == "plan.power_usb" || g.key == "plan.foreground_boost" || g.key == "plan.svchost_split" ||
                   g.key == "plan.boot_tick" ||
                   g.key == "plan.nvidia_extra") {  // ADR-040 + B1 karari (S109 karsi kanit): yalniz acikca Evet
            EF_CHECK(!g.default_on);
        } else {
            EF_CHECK(g.default_on);
        }
    EF_CHECK(found);
}
