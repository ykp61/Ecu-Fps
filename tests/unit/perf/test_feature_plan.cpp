// T12 (ADR-035): ozellik sihirbazi plani. Sahte SCM + sahte kayit defteri; gercek hizmet/ayar degismez.
#include <string>
#include <vector>

#include "core/adapters/windows/service_start_type.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/broker/admin.h"
#include "core/perf/feature_plan.h"
#include "ef_test.h"

using namespace ef::core::perf;
using namespace ef::core::adapters::windows;
namespace st = ef::core::adapters::startup;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
st::ServiceInfo win_svc(const std::string& name, const std::string& path, bool running = true, st::StartType start = st::StartType::Automatic) {
    st::ServiceInfo s;
    s.name = name;
    s.display_name = name;
    s.image_path = path;
    s.own_process = false;
    s.microsoft = true;
    s.start = start;
    s.running = running;
    return s;
}
const std::string kSvchost = "c:\\windows\\system32\\svchost.exe -k netsvcs";

struct Rig {
    st::FakeServiceApi api;
    FakeServiceBusyProbe probe;
    FakeUserRegistry user;
    FakeMachineRegistry machine;
    TweakAdapter tweak{user, &machine};
    Rig() {
        api.services["bthserv"] = win_svc("bthserv", kSvchost);
        api.services["BTAGService"] = win_svc("BTAGService", kSvchost);
        api.services["PrintNotify"] = win_svc("PrintNotify", kSvchost);
        api.services["Spooler"] = win_svc("Spooler", "c:\\windows\\system32\\spoolsv.exe");
        api.services["XblAuthManager"] = win_svc("XblAuthManager", kSvchost);
        api.services["XblGameSave"] = win_svc("XblGameSave", kSvchost);
        api.services["XboxNetApiSvc"] = win_svc("XboxNetApiSvc", kSvchost);
        api.services["XboxGipSvc"] = win_svc("XboxGipSvc", kSvchost);
        api.services["WSearch"] = win_svc("WSearch", "c:\\windows\\system32\\searchindexer.exe /embedding");
        api.services["XblAuthManager"].dependents = {"XblGameSave", "XboxNetApiSvc"};
    }
    std::vector<PlanItem> plan(const FeatureUsage& u, const std::vector<PlanItem>& planned = {}) {
        return feature_plan_items(u, api, probe, tweak, std::span<const PlanItem>(planned.data(), planned.size()));
    }
};
FeatureUsage none_in_use() {
    FeatureUsage u;
    u.bluetooth_devices = false;
    u.printers = false;
    u.xbox_games = false;
    return u;
}
std::size_t count_group(const std::vector<PlanItem>& v, std::string_view g) {
    std::size_t n = 0;
    for (const auto& i : v) n += i.group() == g ? 1 : 0;
    return n;
}
}  // namespace

EF_TEST(feature_all_groups_default_off_and_medium_risk) {
    std::size_t found = 0;
    for (const auto& g : plan_groups())
        if (g.key.rfind("plan.feature.", 0) == 0) {
            ++found;
            EF_CHECK(!g.default_on && g.risk == Risk::Medium && g.evidence == Evidence::Documented);
        }
    EF_CHECK(found == 5);
}

EF_TEST(feature_group_absent_when_in_use) {
    Rig r;
    FeatureUsage u = none_in_use();
    u.bluetooth_devices = true;
    u.printers = true;
    u.xbox_games = true;
    const auto items = r.plan(u);
    EF_CHECK(count_group(items, kFeatureBluetoothGroup) == 0 && count_group(items, kFeaturePrintGroup) == 0 &&
              count_group(items, kFeatureXboxGroup) == 0);
    EF_CHECK(count_group(items, kFeatureSearchGroup) == 1);  // arama/magaza icin kullanim denetimi yok
    EF_CHECK(count_group(items, kFeatureStoreGroup) == 1);
}

EF_TEST(feature_group_absent_when_usage_unknown) {
    Rig r;
    const auto items = r.plan(FeatureUsage{});  // hicbir olgu okunamadi
    EF_CHECK(count_group(items, kFeatureBluetoothGroup) == 0 && count_group(items, kFeaturePrintGroup) == 0 &&
              count_group(items, kFeatureXboxGroup) == 0);
    EF_CHECK(!feature_group_offered(kFeatureBluetoothGroup, FeatureUsage{}));
}

EF_TEST(feature_items_when_not_in_use_are_admin_machine_ops_allowed_by_broker) {
    Rig r;
    const auto items = r.plan(none_in_use());
    EF_CHECK(count_group(items, kFeatureBluetoothGroup) == 2 && count_group(items, kFeaturePrintGroup) == 2 &&
              count_group(items, kFeatureXboxGroup) == 4 && count_group(items, kFeatureSearchGroup) == 1 &&
              count_group(items, kFeatureStoreGroup) == 1);
    for (const auto& i : items) {
        EF_CHECK(i.needs_admin && i.op.scope == OperationScope::Machine && !i.field_key.empty());
        EF_CHECK(ef::core::broker::admin_operation_allowed(i.op));
        if (i.op.id == OperationId::ServiceStartTypeSet) EF_CHECK(i.op.new_value == "disabled" && i.op.target.rfind("svcstart:", 0) == 0);
    }
}

EF_TEST(feature_xbox_dependents_come_before_auth_manager) {
    Rig r;
    const auto items = r.plan(none_in_use());
    std::size_t auth = 0, save = 0, net = 0, pos = 0;
    for (const auto& i : items) {
        if (i.group() != kFeatureXboxGroup) continue;
        if (i.op.target == "svcstart:XblAuthManager") auth = pos;
        if (i.op.target == "svcstart:XblGameSave") save = pos;
        if (i.op.target == "svcstart:XboxNetApiSvc") net = pos;
        ++pos;
    }
    EF_CHECK(save < auth && net < auth);
}

EF_TEST(feature_missing_disabled_and_unfit_services_are_skipped_silently) {
    Rig r;
    r.api.services.erase("BTAGService");                                 // bu Windows'ta yok
    r.api.services["PrintNotify"].start = st::StartType::Disabled;        // zaten kapali
    r.probe.busy_by_service["Spooler"] = true;                            // kuyrukta is var
    r.api.services["WSearch"].microsoft = false;                          // sahte ikili
    const auto items = r.plan(none_in_use());
    EF_CHECK(count_group(items, kFeatureBluetoothGroup) == 1 && items[0].op.target == "svcstart:bthserv");
    EF_CHECK(count_group(items, kFeaturePrintGroup) == 0);
    EF_CHECK(count_group(items, kFeatureSearchGroup) == 0);
}

EF_TEST(feature_running_dependent_outside_table_blocks_service) {
    Rig r;
    r.api.services["bthserv"].dependents = {"SomeOtherSvc"};
    r.api.services["SomeOtherSvc"] = win_svc("SomeOtherSvc", "c:\\program files\\x\\x.exe");
    EF_CHECK(count_group(r.plan(none_in_use()), kFeatureBluetoothGroup) == 1);  // yalniz BTAGService
    r.api.services["SomeOtherSvc"].running = false;
    EF_CHECK(count_group(r.plan(none_in_use()), kFeatureBluetoothGroup) == 2);
}

EF_TEST(feature_not_offered_when_session_stop_already_plans_same_service) {
    Rig r;
    std::vector<PlanItem> planned;
    PlanItem p;
    p.op = ef::contracts::SettingOperation{OperationId::ServiceSessionStop, OperationScope::Machine, "svcstop:Spooler", "stopped", false, false};
    p.group_key = "plan.services";
    planned.push_back(p);
    const auto items = r.plan(none_in_use(), planned);
    EF_CHECK(count_group(items, kFeaturePrintGroup) == 0);  // ayni hizmet hem oturumluk durdurulup hem kapatilmaz
    EF_CHECK(count_group(items, kFeatureBluetoothGroup) == 2);
}

EF_TEST(feature_store_uses_catalog_entry_and_skips_when_already_set) {
    Rig r;
    auto items = r.plan(none_in_use());
    std::size_t store = 0;
    for (const auto& i : items)
        if (i.group() == kFeatureStoreGroup) {
            ++store;
            EF_CHECK(i.op.id == OperationId::TweakValueSet && i.op.target == "tweak:feature.store_autodl" && i.op.new_value == "2");
            EF_CHECK(i.needs_admin && i.op.scope == OperationScope::Machine);
            EF_CHECK(ef::core::broker::admin_operation_allowed(i.op));
        }
    EF_CHECK(store == 1);
    r.machine.values[FakeMachineRegistry::key(L"SOFTWARE\\Policies\\Microsoft\\WindowsStore", L"AutoDownload")] = dword_raw(2);
    EF_CHECK(count_group(r.plan(none_in_use()), kFeatureStoreGroup) == 0);
}

EF_TEST(feature_combine_usage_rules) {
    using O = std::optional<bool>;
    EF_CHECK(combine_usage({O(false), O(false)}) == O(false));
    EF_CHECK(combine_usage({O(false), O(true)}) == O(true));
    EF_CHECK(combine_usage({O(false), std::nullopt}) == std::nullopt);
    EF_CHECK(combine_usage({std::nullopt, O(true)}) == O(true));  // bir kullanim kaniti belirsizligi yener
}

EF_TEST(feature_virtual_printers_do_not_count_as_in_use) {
    EF_CHECK(is_virtual_printer("Microsoft Print to PDF", "PORTPROMPT:"));
    EF_CHECK(is_virtual_printer("Microsoft XPS Document Writer", "nul:"));
    EF_CHECK(is_virtual_printer("OneNote (Desktop)", "nul:"));
    EF_CHECK(is_virtual_printer("Fax", "SHRFAX:"));
    EF_CHECK(!is_virtual_printer("HP LaserJet Pro", "USB001"));
    EF_CHECK(!is_virtual_printer("Office Printer", "192.168.1.20"));
    EF_CHECK(!is_virtual_printer("Office Printer", ""));  // port okunamadi: gercek sayilir (temkinli)
}
