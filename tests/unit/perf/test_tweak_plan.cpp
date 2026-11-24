// ADR-035 / T02: katalogdan plan ogeleri (sahte kovanlar; gercek kayit defteri yok).
#include <vector>

#include "core/adapters/windows/tweak_adapter.h"
#include "core/perf/tweak_plan.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using namespace ef::core::perf;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
constexpr TweakEntry kM{"p.machine", TweakHive::Machine, L"SOFTWARE\\EfTest\\P", L"A", kTweakRegDword, "1", "plan.t"};
constexpr TweakEntry kU{"p.user", TweakHive::User, L"Software\\EfTest", L"B", kTweakRegDword, "0", "plan.t"};
constexpr TweakEntry kS{"p.strict", TweakHive::Machine, L"SOFTWARE\\EfTest\\P", L"C", kTweakRegDword, "1", "plan.t",
                        TweakSkip::WhenCurrentLowerOrEqual};

std::vector<const TweakEntry*> all() { return {&kM, &kU, &kS}; }
std::vector<PlanItem> plan(TweakAdapter& a, const std::vector<const TweakEntry*>& v) {
    return tweak_plan_items_for(std::span<const TweakEntry* const>(v.data(), v.size()), a);
}
}  // namespace

EF_TEST(tweak_plan_includes_differing_and_absent_with_admin_flag) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.values[FakeMachineRegistry::key(L"SOFTWARE\\EfTest\\P", L"A")] = dword_raw(0);
    const auto items = plan(a, {&kM, &kU});
    EF_CHECK(items.size() == 2);
    if (items.size() != 2) return;
    EF_CHECK(items[0].op.id == OperationId::TweakValueSet && items[0].op.scope == OperationScope::Machine);
    EF_CHECK(items[0].op.target == "tweak:p.machine" && items[0].op.new_value == "1");
    EF_CHECK(items[0].needs_admin && items[0].field_key == "tweak.p.machine" && items[0].group() == "plan.t");
    EF_CHECK(items[1].op.scope == OperationScope::CurrentUser && !items[1].needs_admin && items[1].op.new_value == "0");
    EF_CHECK(!items[0].op.needs_reboot && !items[1].op.needs_reboot);
}

EF_TEST(tweak_plan_skips_already_set) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.values[FakeMachineRegistry::key(L"SOFTWARE\\EfTest\\P", L"A")] = dword_raw(1);
    u.values[FakeUserRegistry::key(L"Software\\EfTest", L"B")] = dword_raw(0);
    EF_CHECK(plan(a, {&kM, &kU}).empty());
}

EF_TEST(tweak_plan_skips_unreadable_and_machine_without_access) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.fail_read = true;
    const auto items = plan(a, all());
    EF_CHECK(items.size() == 1 && items[0].op.target == "tweak:p.user");  // HKLM okunamadi: yalniz HKCU girdisi
    TweakAdapter none(u, nullptr);
    const auto items2 = plan(none, all());
    EF_CHECK(items2.size() == 1 && items2[0].op.target == "tweak:p.user");
}

EF_TEST(tweak_plan_honors_skip_if_so_stricter_value_is_not_weakened) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto key = FakeMachineRegistry::key(L"SOFTWARE\\EfTest\\P", L"C");
    m.values[key] = dword_raw(0);  // daha siki
    EF_CHECK(plan(a, {&kS}).empty());
    m.values[key] = dword_raw(3);  // daha gevsek: plana girer
    EF_CHECK(plan(a, {&kS}).size() == 1);
    m.values.erase(key);           // yok: plana girer
    EF_CHECK(plan(a, {&kS}).size() == 1);
}

EF_TEST(tweak_plan_empty_catalog_yields_nothing) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    EF_CHECK(tweak_plan_items("plan.no_such_group", a).empty());
}

// ---- T14: gizlilik gruplari (plan.privacy HKCU, plan.privacy_system HKLM) ----
namespace {
constexpr const wchar_t* kDc = L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection";
}

EF_TEST(t14_privacy_group_is_user_scope_without_admin) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto items = tweak_plan_items("plan.privacy", a);
    EF_CHECK(items.size() == 5);
    for (const auto& i : items) EF_CHECK(!i.needs_admin && i.op.scope == OperationScope::CurrentUser && i.op.new_value == "0");
}

EF_TEST(t14_privacy_already_set_entries_are_skipped) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    u.values[FakeUserRegistry::key(L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled")] = dword_raw(0);
    const auto items = tweak_plan_items("plan.privacy", a);
    EF_CHECK(items.size() == 4);
    for (const auto& i : items) EF_CHECK(i.op.target != "tweak:privacy.ad_id");
}

EF_TEST(t14_privacy_system_needs_admin_and_never_writes_telemetry_zero) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto items = tweak_plan_items("plan.privacy_system", a);
    EF_CHECK(items.size() == 2);
    for (const auto& i : items) {
        EF_CHECK(i.needs_admin && i.op.scope == OperationScope::Machine);
        if (i.op.target == "tweak:privacy.telemetry") EF_CHECK(i.op.new_value == "1");
    }
}

// ---- T21: plan.win11_cleanup / plan.input (HKCU, UAC yok) ----
EF_TEST(t21_groups_are_user_scope_and_skip_already_set) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    EF_CHECK(tweak_plan_items("plan.win11_cleanup", a).size() == 4);
    auto in = tweak_plan_items("plan.input", a);
    EF_CHECK(in.size() == 3);
    for (const auto& i : in) EF_CHECK(!i.needs_admin && i.op.scope == OperationScope::CurrentUser && i.op.new_value == "0");
    u.values[FakeUserRegistry::key(L"Control Panel\\Mouse", L"MouseSpeed")] = sz_raw(L"0");
    in = tweak_plan_items("plan.input", a);
    EF_CHECK(in.size() == 2);
    for (const auto& i : in) EF_CHECK(i.op.target != "tweak:input.mouse_speed");
}

EF_TEST(t14_stricter_or_org_telemetry_value_is_not_overwritten) {
    for (const std::uint32_t existing : {0u, 2u, 3u}) {
        FakeUserRegistry u;
        FakeMachineRegistry m;
        TweakAdapter a(u, &m);
        m.values[FakeMachineRegistry::key(kDc, L"AllowTelemetry")] = dword_raw(existing);
        const auto items = tweak_plan_items("plan.privacy_system", a);
        EF_CHECK(items.size() == 1 && items[0].op.target == "tweak:privacy.activities");
        EF_CHECK(m.values[FakeMachineRegistry::key(kDc, L"AllowTelemetry")] == dword_raw(existing));  // plan okumasi yazmaz
    }
}

// ---- B2: sched.priority_separation (MOCK sahte kovan; gercek kayit defteri yok) ----
EF_TEST(b2_priority_separation_plan_item_is_admin_when_enabled_and_absent_when_closed) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto key = FakeMachineRegistry::key(L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation");
    m.values[key] = dword_raw(24);
    // Kapali bayrak (varsayilan): grup yoluyla oge uretilmez.
    EF_CHECK(tweak_plan_items("plan.foreground_boost", a).empty());
    // Girdinin kendisi (bayrak acik varsayimiyla, yalniz test): mevcut 24 -> tek yonetici ogesi.
    const auto* e = find_tweak("sched.priority_separation");
    EF_CHECK(e != nullptr);
    if (!e) return;
    const std::vector<const TweakEntry*> v{e};
    const auto items = plan(a, v);
    EF_CHECK(items.size() == 1);
    if (items.size() != 1) return;
    EF_CHECK(items[0].needs_admin && items[0].op.scope == OperationScope::Machine && items[0].op.id == OperationId::TweakValueSet);
    EF_CHECK(items[0].op.target == "tweak:sched.priority_separation" && items[0].op.new_value == "38");
    EF_CHECK(items[0].group() == "plan.foreground_boost" && items[0].field_key == "tweak.sched.priority_separation" && !items[0].op.needs_reboot);
    EF_CHECK(m.writes == 0);  // plan okumasi yazmaz
    // Zaten 38: plana girmez. Deger yok: plana girer (okunabilir, yok).
    m.values[key] = dword_raw(38);
    EF_CHECK(plan(a, v).empty());
    m.values.erase(key);
    EF_CHECK(plan(a, v).size() == 1);
    // Atlama kurali yok: mevcut 2 (istemci varsayilani) denklik dogrulanmadigindan atlanmaz.
    m.values[key] = dword_raw(2);
    EF_CHECK(plan(a, v).size() == 1);
    // HKLM okunamiyorsa plana girmez.
    m.fail_read = true;
    EF_CHECK(plan(a, v).empty());
}

// ---- B3: hesaplanan deger (InstalledRamKb), katalog disi test girdisi (MOCK runtime + sahte kovan) ----
EF_TEST(b3_svchost_plan_item_from_runtime_needs_reboot_and_absent_without_ram) {
    constexpr const wchar_t* kCtl = L"SYSTEM\\CurrentControlSet\\Control";
    constexpr TweakEntry kSvc{"t.svc_split", TweakHive::Machine, kCtl, L"SvcHostSplitThresholdInKB", kTweakRegDword, "",
                              "plan.svchost_split", TweakSkip::WhenCurrentHigherOrEqual, true, TweakValueSource::InstalledRamKb};
    const auto key = FakeMachineRegistry::key(kCtl, L"SvcHostSplitThresholdInKB");
    const std::vector<const TweakEntry*> v{&kSvc};
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    // Runtime yok (RAM okunamadi): oge yok.
    EF_CHECK(plan(a, v).empty());
    TweakRuntime rt;
    rt.installed_ram_kb = 16777216;
    a.set_runtime(rt);
    m.values[key] = dword_raw(3670016);
    auto items = plan(a, v);
    EF_CHECK(items.size() == 1);
    if (items.size() == 1) {
        EF_CHECK(items[0].op.new_value == "16777216" && items[0].op.needs_reboot && items[0].needs_admin);
        EF_CHECK(items[0].op.scope == OperationScope::Machine && items[0].group() == "plan.svchost_split");
    }
    EF_CHECK(m.writes == 0);
    // Zaten hedefte veya daha yuksek: oge yok. Deger yok: oge var.
    m.values[key] = dword_raw(16777216);
    EF_CHECK(plan(a, v).empty());
    m.values[key] = dword_raw(33554432);
    EF_CHECK(plan(a, v).empty());
    m.values.erase(key);
    EF_CHECK(plan(a, v).size() == 1);
    // DWORD'a sigmayan RAM: oge yok. HKLM okunamazsa: oge yok.
    rt.installed_ram_kb = 0x100000000ull;
    a.set_runtime(rt);
    EF_CHECK(plan(a, v).empty());
    rt.installed_ram_kb = 16777216;
    a.set_runtime(rt);
    m.fail_read = true;
    EF_CHECK(plan(a, v).empty());
    // Grup yolu: katalogda girdi yok (adim 0 dogrulanmadi) -> oge yok.
    m.fail_read = false;
    EF_CHECK(tweak_plan_items("plan.svchost_split", a).empty());
}
