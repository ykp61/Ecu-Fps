// ADR-040 m.4 / B0 + B4: onyukleme secenegi (dinamik tick). Gercek BCD okunmaz/yazilmaz; tum davranis [MOCK]
// FakeBootApi ile sinanir (sahte arka uc, gercek cihaz kaniti DEGILDIR). Bayrak kBootOptionItemEnabled = false kalir.
#include <optional>
#include <string>

#include "core/adapters/windows/boot_options.h"
#include "core/broker/admin.h"
#include "core/perf/boot_plan.h"
#include "core/readiness/readiness_interrupts.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"
#include "ef_test.h"

using ef::contracts::ErrorCode;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::Result;
using ef::contracts::SettingOperation;
using ef::contracts::Status;
namespace cw = ef::core::adapters::windows;
namespace rec = ef::core::recovery;

namespace {
SettingOperation dyn_tick_off() {
    return SettingOperation{OperationId::BootOptionSet, OperationScope::Machine, std::string(cw::kBootDynamicTickTarget),
                            std::string(cw::kBootDynamicTickOff), true, false};
}
SettingOperation boot_op(const char* target, const char* value, OperationScope sc = OperationScope::Machine) {
    return SettingOperation{OperationId::BootOptionSet, sc, target, value, true, false};
}

// [MOCK] sahte BCD: oge yok (nullopt) | "on" | "off". Gercek BCD deposu degildir.
struct FakeBootApi final : cw::BootOptionApi {
    std::optional<std::string> value;
    bool fail_read = false;
    bool fail_write = false;
    int writes = 0, removes = 0, reads = 0;
    Result<std::optional<std::string>> read(cw::BootElement) override {
        ++reads;
        if (fail_read) return Result<std::optional<std::string>>::fail(ErrorCode::IoError, "[MOCK] read failure");
        return value;
    }
    Status write(cw::BootElement, std::string_view v) override {
        if (fail_write) return Status::fail(ErrorCode::Denied, "[MOCK] write failure");
        ++writes;
        value = std::string(v);
        return ef::contracts::ok_status();
    }
    Status remove(cw::BootElement) override {
        if (fail_write) return Status::fail(ErrorCode::Denied, "[MOCK] write failure");
        ++removes;
        value.reset();
        return ef::contracts::ok_status();
    }
};

const std::string kTx = "0123456789abcdef0123456789abcdef";
rec::JournalTx one_step_tx() {
    rec::JournalTx t;
    t.id = kTx;
    rec::JournalStep s;
    s.op = dyn_tick_off();
    t.steps.push_back(s);
    return t;
}
}  // namespace

EF_TEST(boot_option_skeleton_adapter_fails_closed) {
    cw::UnavailableBootOptionApi api;  // dogrulanmis okuma yolu yok: her cagri Unsupported
    cw::BootOptionAdapter a{api, true};
    const auto op = dyn_tick_off();
    EF_CHECK(!a.capture(op).ok());
    EF_CHECK(!a.apply(op).ok());
    EF_CHECK(!a.read_back(op).ok());
    rec::Snapshot snap;
    EF_CHECK(!a.restore(op, snap).ok());
    auto bad = op;
    bad.target = "boot:hpet";
    EF_CHECK(!a.apply(bad).ok());
    EF_CHECK(!cw::is_boot_option_target("boot:hpet") && cw::is_boot_option_target(cw::kBootDynamicTickTarget));
}

EF_TEST(boot_admin_allows_only_fixed_pair) {
    // Bicim: yalniz ("boot:dynamictick","off"), Machine, BootOptionSet (bayrak acik derleme sabiti yerine parametre).
    EF_CHECK(cw::boot_option_pair_allowed(cw::kBootDynamicTickTarget, cw::kBootDynamicTickOff));
    EF_CHECK(cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "off"), true));
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "on"), true));
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:dynamictick", ""), true));
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "OFF"), true));
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:hpet", "off"), true));
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "off", OperationScope::CurrentUser), true));
    auto wrong_id = boot_op("boot:dynamictick", "off");
    wrong_id.id = OperationId::HagsSet;
    EF_CHECK(!cw::boot_option_admin_allowed(wrong_id, true));
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "off"), false));  // bayrak kapali
}

EF_TEST(boot_flag_off_blocks_plan_and_apply_but_not_restore) {
    static_assert(!cw::kBootOptionItemEnabled, "ADR-040 m.4: VM tam tur kaniti gelene kadar kapali");
    // Broker: yeni yazma reddedilir, kayitli geri alma istegi (ayni tipli islem) bicimden gecer.
    EF_CHECK(!ef::core::broker::admin_apply_allowed(dyn_tick_off()));
    EF_CHECK(ef::core::broker::admin_operation_allowed(dyn_tick_off()));
    EF_CHECK(ef::core::broker::admin_needs_reboot(dyn_tick_off()));
    // Plan: oge yok (bayrak kapali), gerekce kayitli.
    FakeBootApi api;
    api.value = std::nullopt;
    ef::core::perf::BootPlanInputs in;
    in.bcd_readable = true;
    in.bitlocker_system_on = false;
    in.on_battery_device = false;
    std::vector<ef::core::perf::PlanOmission> om;
    EF_CHECK(ef::core::perf::boot_plan_items(api, in, &om).empty());
    // Adaptor (savunma derinligi): varsayilan kurucu bayragi izler -> apply reddedilir, yazma yok; restore calisir.
    cw::BootOptionAdapter a{api};
    api.value = std::string("on");
    auto snap = a.capture(dyn_tick_off());
    EF_CHECK(snap.ok());
    EF_CHECK(!a.apply(dyn_tick_off()).ok() && api.writes == 0);
    api.value = std::string("off");  // onceki surumde uygulanmis kayit
    EF_CHECK(a.restore(dyn_tick_off(), snap.value()).ok());
    EF_CHECK(api.value == std::optional<std::string>("on") && api.writes == 1);
}

EF_TEST(boot_capture_absent_then_restore_deletes) {
    FakeBootApi api;  // [MOCK] oge yok
    cw::BootOptionAdapter a{api, true};
    auto snap = a.capture(dyn_tick_off());
    EF_CHECK(snap.ok() && !snap.value().existed && snap.value().old_value.empty());
    EF_CHECK(a.apply(dyn_tick_off()).ok());
    EF_CHECK(a.read_back(dyn_tick_off()).value() == std::optional<std::string>("off"));
    EF_CHECK(a.restore(dyn_tick_off(), snap.value()).ok());
    EF_CHECK(!api.value.has_value() && api.removes == 1);  // deletevalue karsiligi; "on" uydurulmadi
    EF_CHECK(a.matches_snapshot(dyn_tick_off(), snap.value(), a.read_back(dyn_tick_off()).value()));
}

EF_TEST(boot_capture_present_restores_old) {
    FakeBootApi api;
    api.value = std::string("on");  // [MOCK] dinamik tick acikca acik (disabledynamictick no)
    cw::BootOptionAdapter a{api, true};
    auto snap = a.capture(dyn_tick_off());
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "on");
    EF_CHECK(a.apply(dyn_tick_off()).ok() && api.value == std::optional<std::string>("off"));
    EF_CHECK(a.restore(dyn_tick_off(), snap.value()).ok());
    EF_CHECK(api.value == std::optional<std::string>("on") && api.removes == 0);
}

EF_TEST(boot_adapter_rejects_bad_ops_and_snapshots) {
    FakeBootApi api;
    cw::BootOptionAdapter a{api, true};
    EF_CHECK(!a.capture(boot_op("boot:hpet", "off")).ok());
    EF_CHECK(!a.apply(boot_op("boot:dynamictick", "on")).ok());  // istemci "on" yazdiramaz
    EF_CHECK(!a.apply(boot_op("boot:dynamictick", "off", OperationScope::CurrentUser)).ok());
    EF_CHECK(api.writes == 0 && api.reads == 0);
    rec::Snapshot bad;
    bad.existed = true;
    bad.old_value = "maybe";  // bozuk anlik goruntu: tahminle yazilmaz
    EF_CHECK(!a.restore(dyn_tick_off(), bad).ok() && api.writes == 0);
    api.fail_read = true;
    EF_CHECK(!a.capture(dyn_tick_off()).ok());  // okunamayan BCD: islem baslamaz
}

EF_TEST(boot_apply_fails_closed_on_readback_mismatch) {
    // [MOCK] oge yok; yazma basarili ama yazmadan sonraki ilk okuma farkli doner. Akis: capture (yok) -> apply ->
    // read_back ("on" != "off") -> Failed -> geri alma (gercek deger "off" = bizim) -> sil -> dogrula (yok) -> Restored.
    FakeBootApi api;
    rec::MemoryJournalFs mem;
    rec::JournalStore store(mem, "m");
    struct LieAfterWrite final : cw::BootOptionApi {
        FakeBootApi& in;
        bool armed = false;
        explicit LieAfterWrite(FakeBootApi& f) : in(f) {}
        Result<std::optional<std::string>> read(cw::BootElement e) override {
            if (armed) {
                armed = false;
                return std::optional<std::string>{"on"};
            }
            return in.read(e);
        }
        Status write(cw::BootElement e, std::string_view v) override {
            armed = true;
            return in.write(e, v);
        }
        Status remove(cw::BootElement e) override { return in.remove(e); }
    } lying{api};
    cw::BootOptionAdapter la{lying, true};
    rec::AdapterRegistry reg;
    reg.add(OperationId::BootOptionSet, &la);
    rec::TransactionEngine eng(store, reg);
    auto r = eng.apply(one_step_tx());
    EF_CHECK(!r.ok());                                   // Failed (geri alindi)
    EF_CHECK(api.writes == 1 && api.removes == 1);       // restore cagrildi: oge yok durumuna silindi
    EF_CHECK(!api.value.has_value());
    auto j = store.load(kTx);
    EF_CHECK(j.ok() && j.value().state == ef::contracts::TxState::Restored);
}

EF_TEST(interrupt_checks_are_read_only) {
    std::vector<ef::core::readiness::InterruptDeviceInfo> devs(1);
    devs[0].role = ef::core::readiness::InterruptDeviceRole::GameGpu;
    devs[0].readable = true;
    devs[0].msi_supported = 1;
    const auto v = ef::core::readiness::interrupt_checks(devs);  // B5: yalniz durum; yazma/Duzelt hicbir zaman yok
    EF_CHECK(v.size() == 1 && v[0].action == ef::core::readiness::ActionKind::None);
}
