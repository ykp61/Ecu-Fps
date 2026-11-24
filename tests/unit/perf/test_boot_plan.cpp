// ADR-040 m.4 / B4: "plan.boot_tick" kapilari. [MOCK] sahte BCD okuyucu; gercek BCD/BitLocker/guc okumasi yok.
#include <optional>
#include <string>
#include <vector>

#include "core/adapters/windows/boot_options.h"
#include "core/perf/boot_plan.h"
#include "ef_test.h"

using ef::contracts::ErrorCode;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::Result;
using ef::contracts::Status;
namespace cw = ef::core::adapters::windows;
namespace perf = ef::core::perf;

namespace {
struct FakeBootRead final : cw::BootOptionApi {  // [MOCK]
    std::optional<std::string> value;
    bool fail_read = false;
    int writes = 0;
    Result<std::optional<std::string>> read(cw::BootElement) override {
        if (fail_read) return Result<std::optional<std::string>>::fail(ErrorCode::IoError, "[MOCK] read failure");
        return value;
    }
    Status write(cw::BootElement, std::string_view) override {
        ++writes;
        return Status::fail(ErrorCode::Denied, "[MOCK] plan must not write");
    }
    Status remove(cw::BootElement) override {
        ++writes;
        return Status::fail(ErrorCode::Denied, "[MOCK] plan must not write");
    }
};

perf::BootPlanInputs ok_inputs() {
    perf::BootPlanInputs in;
    in.bcd_readable = true;
    in.bitlocker_system_on = false;
    in.on_battery_device = false;
    return in;
}

// Tek gerekce bekler: oge yok + dogru grup/tur/anahtar.
bool omitted_only(FakeBootRead& api, const perf::BootPlanInputs& in, perf::OmissionKind kind, const char* key) {
    std::vector<perf::PlanOmission> om;
    const auto items = perf::boot_plan_items(api, in, &om, true);
    return items.empty() && om.size() == 1 && om[0].group_key == perf::kBootTickGroup && om[0].kind == kind && om[0].reason_key == key;
}
}  // namespace

EF_TEST(boot_plan_item_when_all_gates_pass) {
    FakeBootRead api;  // oge yok (Windows varsayilani)
    std::vector<perf::PlanOmission> om;
    const auto items = perf::boot_plan_items(api, ok_inputs(), &om, true);
    EF_CHECK(items.size() == 1 && om.empty());
    if (items.size() != 1) return;
    const auto& i = items[0];
    EF_CHECK(i.op.id == OperationId::BootOptionSet && i.op.scope == OperationScope::Machine);
    EF_CHECK(i.op.target == cw::kBootDynamicTickTarget && i.op.new_value == cw::kBootDynamicTickOff);
    EF_CHECK(i.op.needs_reboot && i.needs_admin);  // onay ekraninda yeniden baslatma uyarisi
    EF_CHECK(i.group() == perf::kBootTickGroup && i.title_key == perf::kBootTickGroup);
    EF_CHECK(cw::boot_option_admin_allowed(i.op, true));
    api.value = std::string("on");  // acikca acik: yine plana girer
    EF_CHECK(perf::boot_plan_items(api, ok_inputs(), nullptr, true).size() == 1);
    EF_CHECK(api.writes == 0);
}

EF_TEST(boot_plan_flag_off_is_empty) {
    FakeBootRead api;
    std::vector<perf::PlanOmission> om;
    EF_CHECK(perf::boot_plan_items(api, ok_inputs(), &om).empty());  // varsayilan = kBootOptionItemEnabled (false)
    EF_CHECK(om.size() == 1 && om[0].reason_key == "plan.boot_tick.gate.not_verified" &&
              om[0].kind == perf::OmissionKind::Unsupported);
    EF_CHECK(perf::boot_plan_items(api, ok_inputs(), nullptr).empty());  // nullptr guvenli
}

EF_TEST(boot_gate_bitlocker) {
    FakeBootRead api;
    auto in = ok_inputs();
    in.bitlocker_system_on = true;
    EF_CHECK(omitted_only(api, in, perf::OmissionKind::Unsupported, "plan.boot_tick.gate.bitlocker_on"));
    in.bitlocker_system_on = std::nullopt;
    EF_CHECK(omitted_only(api, in, perf::OmissionKind::Insufficient, "plan.boot_tick.gate.bitlocker_unread"));
}

EF_TEST(boot_gate_battery) {
    FakeBootRead api;
    auto in = ok_inputs();
    in.on_battery_device = true;
    EF_CHECK(omitted_only(api, in, perf::OmissionKind::Unsupported, "plan.boot_tick.gate.battery"));
    in.on_battery_device = std::nullopt;
    EF_CHECK(omitted_only(api, in, perf::OmissionKind::Insufficient, "plan.boot_tick.gate.power_unread"));
}

EF_TEST(boot_gate_unreadable) {
    FakeBootRead api;
    auto in = ok_inputs();
    in.bcd_readable = false;
    EF_CHECK(omitted_only(api, in, perf::OmissionKind::Insufficient, "plan.boot_tick.gate.bcd_unread"));
    api.fail_read = true;  // girdi okunabilir dese de adaptor okuyamazsa oge yok
    EF_CHECK(omitted_only(api, ok_inputs(), perf::OmissionKind::Insufficient, "plan.boot_tick.gate.bcd_unread"));
    api.fail_read = false;
    api.value = std::string("garbage");  // tanimsiz deger: varsayilan uydurulmaz
    EF_CHECK(omitted_only(api, ok_inputs(), perf::OmissionKind::Insufficient, "plan.boot_tick.gate.bcd_unread"));
}

EF_TEST(boot_plan_skips_when_already_off) {
    FakeBootRead api;
    api.value = std::string("off");
    std::vector<perf::PlanOmission> om;
    EF_CHECK(perf::boot_plan_items(api, ok_inputs(), &om, true).empty() && om.empty());  // degisiklik yok
}

EF_TEST(boot_plan_unavailable_api_never_plans) {
    cw::UnavailableBootOptionApi api;  // urundeki gercek okuma yolu dogrulanmadi: oge hic cikmaz
    std::vector<perf::PlanOmission> om;
    EF_CHECK(perf::boot_plan_items(api, ok_inputs(), &om, true).empty());
    EF_CHECK(om.size() == 1 && om[0].reason_key == "plan.boot_tick.gate.bcd_unread");
}
