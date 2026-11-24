#include "core/adapters/windows/power_subs.h"
#include "core/broker/admin.h"
#include "core/perf/admin_plan.h"
#include "core/perf/controller.h"
#include "ef_test.h"

#include <string>

using namespace ef::core::adapters::windows;
using ef::core::perf::power_subs_plan_items;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
const std::string kBalanced = "381b4222-f694-41f0-9685-ff5bb260df2e";
const std::string kHigh = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";

SettingOperation op(std::string_view target = kPowerBoostAcTarget, const char* v = "1") {
    return SettingOperation{OperationId::PowerSettingSet, OperationScope::Machine, std::string(target), v, false, false};
}

FakePowerSubApi seeded() {
    FakePowerSubApi api;
    api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] = 4;
    api.schemes[kBalanced][std::string(kPowerParkingAcTarget)] = 50;
    api.schemes[kBalanced][std::string(kPowerAspmAcTarget)] = 2;
    api.schemes[kHigh] = api.schemes[kBalanced];
    return api;
}
}  // namespace

EF_TEST(power_subs_apply_readback_restore_captured_scheme) {
    auto api = seeded();
    PowerSubAdapter a(api);
    auto snap = a.capture(op());
    EF_CHECK(snap.ok() && snap.value().existed);
    EF_CHECK(snap.value().old_value == "4" && snap.value().raw == kBalanced);
    EF_CHECK(snap.value().raw_type == "power_scheme_ac");
    EF_CHECK(a.apply(op()).ok());
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] == 1);
    EF_CHECK(a.read_back(op()).value() == std::optional<std::string>("1"));
    // Kullanici bu arada baska plana gectiyse geri alma yine YAKALANAN plana yazar.
    api.active = kHigh;
    api.schemes[kHigh][std::string(kPowerBoostAcTarget)] = 1;
    EF_CHECK(a.restore(op(), snap.value()).ok());
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] == 4);
    EF_CHECK(api.schemes[kHigh][std::string(kPowerBoostAcTarget)] == 1);
}

EF_TEST(power_subs_reject_wrong_target_value_and_bad_snapshot) {
    auto api = seeded();
    PowerSubAdapter a(api);
    EF_CHECK(!a.capture(op("power:other:ac")).ok());
    EF_CHECK(!a.apply(op(kPowerBoostAcTarget, "7")).ok());     // 0-6 disi
    EF_CHECK(!a.apply(op(kPowerParkingAcTarget, "101")).ok()); // 0-100 disi
    EF_CHECK(!a.apply(op(kPowerAspmAcTarget, "3")).ok());      // 0-2 disi
    auto wrong_id = op();
    wrong_id.id = OperationId::FastStartupSet;
    EF_CHECK(!a.capture(wrong_id).ok() && !a.apply(wrong_id).ok());
    ef::core::recovery::Snapshot bad;
    bad.old_value = "4";
    bad.raw = "not-a-guid";  // serbest plan GUID'i kabul edilmez
    EF_CHECK(!a.restore(op(), bad).ok());
    bad.raw = kBalanced;
    bad.old_value = "x";
    EF_CHECK(!a.restore(op(), bad).ok());
    api.fail_write = true;
    EF_CHECK(!a.apply(op()).ok());
}

EF_TEST(power_subs_plan_items_only_documented_direction) {
    auto api = seeded();
    auto items = power_subs_plan_items(api, false);
    EF_CHECK(items.size() == 3);  // boost 4->1, parking 50->100, aspm 2->0
    auto find = [&](std::string_view t) -> const ef::core::perf::PlanItem* {
        for (const auto& i : items)
            if (i.op.target == t) return &i;
        return nullptr;
    };
    const auto* boost = find(kPowerBoostAcTarget);
    EF_CHECK(boost && boost->op.new_value == "1" && boost->group_key == "plan.power_cpu" && boost->needs_admin);
    const auto* parking = find(kPowerParkingAcTarget);
    EF_CHECK(parking && parking->op.new_value == "100" && !parking->op.needs_reboot);
    const auto* aspm = find(kPowerAspmAcTarget);
    EF_CHECK(aspm && aspm->op.new_value == "0" && aspm->group_key == "plan.power_pcie");
    // Plan degisikligi ayni islemdeyse alt ayarlar uygulanmaz (snapshot/yazma plani ayni olmali).
    EF_CHECK(power_subs_plan_items(api, true).empty());
    // Boost zaten Enabled/Aggressive/Disabled ise ezilmez; parking/ASPM hedefteyse is yok.
    for (std::uint32_t v : {0u, 1u, 2u, 5u, 6u}) {
        api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] = v;
        api.schemes[kBalanced][std::string(kPowerParkingAcTarget)] = 100;
        api.schemes[kBalanced][std::string(kPowerAspmAcTarget)] = 0;
        EF_CHECK(power_subs_plan_items(api, false).empty());
    }
    // Okunamayan ayar plana girmez (uydurma yok).
    api.fail_read = true;
    EF_CHECK(power_subs_plan_items(api, false).empty());
}

// ---- B1: dorduncu AC alt ayari, USB secici askiya alma (power:usbss:ac). MOCK: FakePowerSubApi, gercek guc API'si yok. ----

namespace {
FakePowerSubApi seeded_with_usb(std::uint32_t usb) {
    auto api = seeded();
    api.schemes[kBalanced][std::string(kPowerUsbSelectiveSuspendAcTarget)] = usb;
    api.schemes[kHigh] = api.schemes[kBalanced];
    return api;
}
}  // namespace

EF_TEST(usbss_target_known) {
    EF_CHECK(is_power_sub_target("power:usbss:ac"));
    EF_CHECK(kPowerUsbSelectiveSuspendAcTarget == "power:usbss:ac");
    EF_CHECK(valid_power_sub_index("power:usbss:ac", 0));
    EF_CHECK(valid_power_sub_index("power:usbss:ac", 1));
    EF_CHECK(!valid_power_sub_index("power:usbss:ac", 2));
    EF_CHECK(valid_power_sub_value("power:usbss:ac", "0") && !valid_power_sub_value("power:usbss:ac", "2"));
    // DC (pil) hedefi yoktur: yalniz AC yazilir.
    EF_CHECK(!is_power_sub_target("power:usbss:dc"));
    EF_CHECK(!valid_power_sub_index("power:usbss:dc", 0));
}

EF_TEST(usbss_admin_allows_only_zero) {
    EF_CHECK(admin_power_sub_value_allowed("power:usbss:ac", "0"));
    EF_CHECK(!admin_power_sub_value_allowed("power:usbss:ac", "1"));
    EF_CHECK(!admin_power_sub_value_allowed("power:usbss:ac", "00"));
    EF_CHECK(!admin_power_sub_value_allowed("power:usbss:ac", ""));
    EF_CHECK(!admin_power_sub_value_allowed("power:usbss:dc", "0"));
    // Broker izin satiri: yalniz Machine kapsami, yalniz "0".
    auto usb = op(kPowerUsbSelectiveSuspendAcTarget, "0");
    EF_CHECK(ef::core::broker::admin_operation_allowed(usb));
    usb.new_value = "1";
    EF_CHECK(!ef::core::broker::admin_operation_allowed(usb));
    usb.new_value = "0";
    usb.scope = OperationScope::CurrentUser;
    EF_CHECK(!ef::core::broker::admin_operation_allowed(usb));
}

EF_TEST(usbss_apply_readback_restore_captured_scheme) {
    auto api = seeded_with_usb(1);
    PowerSubAdapter a(api);
    const auto usb = op(kPowerUsbSelectiveSuspendAcTarget, "0");
    auto snap = a.capture(usb);
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "1" && snap.value().raw == kBalanced);
    EF_CHECK(a.apply(usb).ok());
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerUsbSelectiveSuspendAcTarget)] == 0);
    EF_CHECK(a.read_back(usb).value() == std::optional<std::string>("0"));
    // Diger AC alt ayarlari ve baska plan etkilenmez.
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] == 4);
    EF_CHECK(api.schemes[kHigh][std::string(kPowerUsbSelectiveSuspendAcTarget)] == 1);
    // Kullanici baska plana gectiyse geri alma yine YAKALANAN plana yazar, onceki degerle.
    api.active = kHigh;
    EF_CHECK(a.restore(usb, snap.value()).ok());
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerUsbSelectiveSuspendAcTarget)] == 1);
    EF_CHECK(a.read_for_restore(usb, snap.value()).value() == std::optional<std::string>("1"));
    // Gecersiz indeks ve DC hedefi reddedilir.
    EF_CHECK(!a.apply(op(kPowerUsbSelectiveSuspendAcTarget, "2")).ok());
    EF_CHECK(!a.apply(op("power:usbss:dc", "0")).ok() && !a.capture(op("power:usbss:dc", "0")).ok());
}

EF_TEST(usbss_capture_fails_when_unreadable) {
    auto api = seeded();  // usbss ayari planda yok
    PowerSubAdapter a(api);
    EF_CHECK(!a.capture(op(kPowerUsbSelectiveSuspendAcTarget, "0")).ok());
    EF_CHECK(api.writes == 0);
}

EF_TEST(usbss_plan_item_when_enabled) {
    auto api = seeded_with_usb(1);
    // seeded() boost/parking/aspm de degisecek; yalniz usbss ogesine bak.
    auto items = power_subs_plan_items(api, false);
    const ef::core::perf::PlanItem* usb = nullptr;
    for (const auto& i : items)
        if (i.op.target == kPowerUsbSelectiveSuspendAcTarget) usb = &i;
    EF_CHECK(items.size() == 4 && usb != nullptr);
    if (usb) {
        EF_CHECK(usb->group_key == "plan.power_usb" && usb->needs_admin);
        EF_CHECK(usb->op.id == OperationId::PowerSettingSet && usb->op.scope == OperationScope::Machine);
        EF_CHECK(usb->op.new_value == "0" && !usb->op.needs_reboot);
    }
    // Mevcut 0 -> yapilacak is yok.
    api.schemes[kBalanced][std::string(kPowerUsbSelectiveSuspendAcTarget)] = 0;
    for (const auto& i : power_subs_plan_items(api, false)) EF_CHECK(i.op.target != kPowerUsbSelectiveSuspendAcTarget);
    // Plan degisikligi ayni islemdeyse hic oge yok.
    api.schemes[kBalanced][std::string(kPowerUsbSelectiveSuspendAcTarget)] = 1;
    EF_CHECK(power_subs_plan_items(api, true).empty());
    // Okuma hatasi -> oge yok (uydurma yok).
    api.fail_read = true;
    EF_CHECK(power_subs_plan_items(api, false).empty());
}

EF_TEST(usbss_group_default_is_no) {
    // Karar (B1): Microsoft surucu belgesi kapatmamayi onerir (karsi kanit) -> Hayir (Gelismis), Tartismali.
    const ef::core::perf::PlanGroup* g = nullptr;
    for (const auto& x : ef::core::perf::plan_groups())
        if (x.key == "plan.power_usb") g = &x;
    EF_CHECK(g != nullptr);
    if (g) {
        EF_CHECK(!g->default_on);
        EF_CHECK(g->evidence == ef::core::perf::Evidence::Disputed);
    }
}
