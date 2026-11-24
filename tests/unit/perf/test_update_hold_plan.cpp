// T03: Windows Update tutma plani (sahte kovanlar; gercek kayit defterine ve Windows Update'e dokunulmaz).
#include <vector>

#include "core/adapters/windows/tweak_adapter.h"
#include "core/perf/update_hold_plan.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using namespace ef::core::perf;
using ef::contracts::Field;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::Provenance;
using ef::contracts::SettingOperation;

namespace {
UpdateStatus status(bool managed) {
    UpdateStatus s;
    s.managed_by_policy = Field<bool>::known(managed, Provenance::Registry);
    return s;
}
UpdateStatus status_unknown() {
    UpdateStatus s;
    s.managed_by_policy = Field<bool>::unknown("registry unreadable");
    return s;
}
std::size_t count_group(const std::vector<PlanItem>& v, std::string_view g) {
    std::size_t n = 0;
    for (const auto& i : v)
        if (i.group() == g) ++n;
    return n;
}
constexpr const wchar_t* kAu = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
}  // namespace

EF_TEST(update_hold_managed_by_policy_yields_nothing) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    EF_CHECK(update_hold_plan_items(status(true), a).empty());
}

EF_TEST(update_hold_unknown_policy_state_yields_nothing) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    EF_CHECK(update_hold_plan_items(status_unknown(), a).empty());
    UpdateStatus never_read;  // alan hic okunmadi
    EF_CHECK(update_hold_plan_items(never_read, a).empty());
}

EF_TEST(update_hold_both_groups_need_admin_machine_scope) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto items = update_hold_plan_items(status(false), a);
    EF_CHECK(count_group(items, "plan.update_hold") == 3 && count_group(items, "plan.update_off") == 2);
    for (const auto& i : items) {
        EF_CHECK(i.needs_admin && i.op.id == OperationId::TweakValueSet && i.op.scope == OperationScope::Machine);
        EF_CHECK(i.op.target.rfind("tweak:update.", 0) == 0 && !i.op.needs_reboot);
    }
}

EF_TEST(update_hold_values_already_set_are_skipped) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.values[FakeMachineRegistry::key(kAu, L"NoAutoRebootWithLoggedOnUsers")] = dword_raw(1);
    m.values[FakeMachineRegistry::key(kAu, L"AUOptions")] = dword_raw(2);
    m.values[FakeMachineRegistry::key(L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", L"DODownloadMode")] = dword_raw(0);
    m.values[FakeMachineRegistry::key(kAu, L"NoAutoUpdate")] = dword_raw(1);
    EF_CHECK(update_hold_plan_items(status(false), a).empty());
}

EF_TEST(update_hold_machine_unreadable_yields_nothing) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.fail_read = true;
    EF_CHECK(update_hold_plan_items(status(false), a).empty());
    TweakAdapter none(u, nullptr);
    EF_CHECK(update_hold_plan_items(status(false), none).empty());
}

EF_TEST(update_hold_own_values_are_not_treated_as_organisation_policy) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    // Kendi yazdigimiz deger anahtari "Present" yapti (managed=true), ama journal'da etkin: plan kurulabilir.
    m.values[FakeMachineRegistry::key(kAu, L"AUOptions")] = dword_raw(2);
    const SettingOperation own{OperationId::TweakValueSet, OperationScope::Machine, "tweak:update.notify", "2", false, false};
    const SettingOperation other{OperationId::TweakValueSet, OperationScope::Machine, "tweak:no.such", "1", false, false};
    const SettingOperation wrong_id{OperationId::GamingToggleSet, OperationScope::Machine, "tweak:update.notify", "2", false, false};
    std::vector<SettingOperation> act{other, wrong_id};
    EF_CHECK(!update_hold_own_active(act));
    act.push_back(own);
    EF_CHECK(update_hold_own_active(act));
    const auto items = update_hold_plan_items(status(true), a, true);
    EF_CHECK(!items.empty());  // eksik kalanlar (ornegin NoAutoReboot) yeniden planlanabilir
    for (const auto& i : items) EF_CHECK(i.op.target != "tweak:update.notify");  // zaten hedefte
    EF_CHECK(update_hold_plan_items(status(true), a, false).empty());
}

EF_TEST(update_groups_registered_with_honest_labels) {
    const PlanGroup* hold = nullptr;
    const PlanGroup* off = nullptr;
    for (const auto& g : plan_groups()) {
        if (g.key == "plan.update_hold") hold = &g;
        if (g.key == "plan.update_off") off = &g;
    }
    EF_CHECK(hold && off);
    if (!hold || !off) return;
    EF_CHECK(hold->default_on && hold->risk == Risk::Low && hold->evidence == Evidence::Documented);
    EF_CHECK(!off->default_on && off->risk == Risk::High && off->evidence == Evidence::Documented);  // acikca secilmeden uygulanmaz
}
