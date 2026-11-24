// T13 (ADR-035): geri alinabilir temizlik plani. Sahte AppxApi ve sahte kayit; gercek sistem DEGISMEZ.
#include <string>

#include "core/adapters/windows/appx_remove.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/perf/controller.h"
#include "core/perf/debloat_plan.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace cw = ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
const std::string kNews = "Microsoft.BingNews_8wekyb3d8bbwe";
const std::string kWeather = "Microsoft.BingWeather_8wekyb3d8bbwe";

BrowserInfo chrome_default() {
    BrowserInfo b;
    b.read_ok = true;
    b.clients = {"Google Chrome", "Microsoft Edge", "IEXPLORE.EXE"};
    b.default_progid = "ChromeHTML";
    return b;
}

struct Rig {
    cw::FakeAppxApi appx;
    cw::FakeUserRegistry user;
    cw::FakeMachineRegistry machine;
    cw::TweakAdapter tweak{user, &machine};
    std::vector<PlanItem> plan(const BrowserInfo& b) { return debloat_plan_items(appx, b, tweak); }
};

std::size_t count_group(const std::vector<PlanItem>& v, std::string_view g) {
    std::size_t n = 0;
    for (const auto& i : v) n += i.group() == g ? 1 : 0;
    return n;
}
}  // namespace

EF_TEST(debloat_edge_group_absent_without_other_browser_or_when_edge_is_default_or_unknown) {
    Rig r;
    BrowserInfo only_edge;
    only_edge.read_ok = true;
    only_edge.clients = {"Microsoft Edge", "IEXPLORE.EXE"};
    only_edge.default_progid = "MSEdgeHTM";
    EF_CHECK(count_group(r.plan(only_edge), kDebloatEdgeGroup) == 0);
    BrowserInfo edge_default = chrome_default();
    edge_default.default_progid = "MSEdgeHTM";  // baska tarayici var ama varsayilan Edge
    EF_CHECK(!edge_debloat_offered(edge_default) && count_group(r.plan(edge_default), kDebloatEdgeGroup) == 0);
    BrowserInfo unknown_default = chrome_default();
    unknown_default.default_progid.clear();
    EF_CHECK(count_group(r.plan(unknown_default), kDebloatEdgeGroup) == 0);
    BrowserInfo unread;  // okunamadi
    EF_CHECK(count_group(r.plan(unread), kDebloatEdgeGroup) == 0);
    EF_CHECK(count_group(r.plan(chrome_default()), kDebloatEdgeGroup) == 2);
}

EF_TEST(debloat_edge_and_onedrive_items_are_machine_admin_policy_writes) {
    Rig r;
    const auto items = r.plan(chrome_default());
    EF_CHECK(count_group(items, kDebloatOneDriveGroup) == 1);
    for (const auto& i : items) {
        if (i.group() == kDebloatAppsGroup) continue;
        EF_CHECK(i.op.id == OperationId::TweakValueSet && i.op.scope == OperationScope::Machine && i.needs_admin);
        EF_CHECK(i.field_key.rfind("tweak.debloat.", 0) == 0);
    }
    const auto* od = cw::find_tweak("debloat.onedrive_sync");
    EF_CHECK(od && od->on_value == "1" && od->name == L"DisableFileSyncNGSC");
    const auto* sb = cw::find_tweak("debloat.edge_startup_boost");
    const auto* bg = cw::find_tweak("debloat.edge_background");
    EF_CHECK(sb && bg && sb->on_value == "0" && bg->on_value == "0");
    for (const auto* g : {"plan.debloat.edge", "plan.debloat.onedrive"})
        for (const auto* e : cw::tweaks_in_group(g)) EF_CHECK(cw::tweak_entry_valid(*e) && e->skip_if == cw::TweakSkip::WhenPresent);
}

EF_TEST(debloat_policy_values_already_set_or_org_managed_are_skipped) {
    Rig r;
    r.machine.values[cw::FakeMachineRegistry::key(L"SOFTWARE\\Policies\\Microsoft\\Edge", L"StartupBoostEnabled")] = cw::dword_raw(0);   // zaten hedef
    r.machine.values[cw::FakeMachineRegistry::key(L"SOFTWARE\\Policies\\Microsoft\\Edge", L"BackgroundModeEnabled")] = cw::dword_raw(1);  // kurum/baska deger
    r.machine.values[cw::FakeMachineRegistry::key(L"SOFTWARE\\Policies\\Microsoft\\Windows\\OneDrive", L"DisableFileSyncNGSC")] = cw::dword_raw(0);
    const auto items = r.plan(chrome_default());
    EF_CHECK(count_group(items, kDebloatEdgeGroup) == 0 && count_group(items, kDebloatOneDriveGroup) == 0);
}

EF_TEST(debloat_apps_only_installed_and_restorable_are_offered) {
    Rig r;
    r.appx.installed = {{kNews, "Microsoft.BingNews_4.55.1_x64__8wekyb3d8bbwe"}, {kWeather, "Microsoft.BingWeather_4.53.1_x64__8wekyb3d8bbwe"}};
    r.appx.provisioned = {kNews};  // Hava Durumu yalniz kullanici icin yuklu: geri kayit garanti degil -> sunulmaz
    const auto items = r.plan(chrome_default());
    EF_CHECK(count_group(items, kDebloatAppsGroup) == 1);
    for (const auto& i : items) {
        if (i.group() != kDebloatAppsGroup) continue;
        EF_CHECK(i.op.id == OperationId::AppxPackageRemove && i.op.scope == OperationScope::CurrentUser && !i.needs_admin);
        EF_CHECK(i.op.target == "appx:" + kNews && i.op.new_value == "removed");
        EF_CHECK(i.detail && i.detail->name == "Microsoft News");
    }
}

EF_TEST(debloat_apps_skips_not_installed_and_unreadable) {
    Rig r;
    r.appx.provisioned = {kNews, kWeather};  // hicbiri kurulu degil
    EF_CHECK(count_group(r.plan(chrome_default()), kDebloatAppsGroup) == 0);
    r.appx.installed = {{kNews, "Microsoft.BingNews_1_x64__8wekyb3d8bbwe"}};
    EF_CHECK(count_group(r.plan(chrome_default()), kDebloatAppsGroup) == 1);
    r.appx.fail_restorable = true;  // provisioned okunamadi: uydurma yok
    EF_CHECK(count_group(r.plan(chrome_default()), kDebloatAppsGroup) == 0);
    r.appx.fail_restorable = false;
    r.appx.fail_read = true;
    EF_CHECK(count_group(r.plan(chrome_default()), kDebloatAppsGroup) == 0);
}

EF_TEST(debloat_groups_are_advanced_default_no_with_evidence_and_risk) {
    int found = 0;
    for (const auto& g : plan_groups()) {
        if (g.key == kDebloatEdgeGroup || g.key == kDebloatOneDriveGroup || g.key == kDebloatAppsGroup) {
            ++found;
            EF_CHECK(!g.default_on && g.evidence == Evidence::Documented);
        }
    }
    EF_CHECK(found == 3);
}
