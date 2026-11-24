#include "core/adapters/windows/power.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Rig {
    FakePowerApi api;
    PowerModeAdapter mode{api};
    PowerSchemeAdapter scheme{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() {
        reg.add(OperationId::PowerModeSet, &mode);
        reg.add(OperationId::PowerSchemeSet, &scheme);
    }
    int n = 0;
    JournalTx tx(OperationId id, const char* target, std::string v) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = SettingOperation{id, OperationScope::Machine, target, std::move(v), false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(power_mode_apply_readback_restore) {
    Rig r;
    auto active = r.engine.apply(r.tx(OperationId::PowerModeSet, "power:mode:ac", std::string(kModeBestPerformance)));
    EF_CHECK(active.ok());
    EF_CHECK(r.api.mode == kModeBestPerformance);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK(r.api.mode == kModeBalanced);
}

EF_TEST(power_mode_ac_and_dc_journaled_and_restored_together) {
    // Gap 11: pilli cihazda AC ve DC ayri adimlar; baslangic kaydi ikisinin eski degerini tasir,
    // geri okuma ikisini dogrular, geri alma ikisini birlikte eski haline getirir.
    Rig r;
    r.api.battery = true;
    r.api.mode = std::string(kModeBetterPerformance);
    r.api.dc_mode = std::string(kModeBestEfficiency);
    JournalTx t = r.tx(OperationId::PowerModeSet, "power:mode:ac", std::string(kModeBestPerformance));
    JournalStep dc;
    dc.op = SettingOperation{OperationId::PowerModeSet, OperationScope::Machine, "power:mode:dc", std::string(kModeBestPerformance),
                             false, false};
    t.steps.push_back(dc);
    auto active = r.engine.apply(t);
    EF_CHECK(active.ok());
    EF_CHECK(r.api.mode == kModeBestPerformance && r.api.dc_mode == kModeBestPerformance);
    EF_CHECK(active.value().steps.size() == 2);
    EF_CHECK(active.value().steps[0].snapshot.old_value == kModeBetterPerformance);
    EF_CHECK(active.value().steps[1].snapshot.old_value == kModeBestEfficiency);
    EF_CHECK(!r.fs.files.empty() && r.fs.files.back().second.find(std::string(kModeBestEfficiency)) != std::string::npos);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.mode == kModeBetterPerformance && r.api.dc_mode == kModeBestEfficiency);
}

EF_TEST(power_mode_dc_unsupported_writes_nothing) {
    Rig r;
    r.api.battery = true;
    r.api.dc_supported = false;
    auto res = r.engine.apply(r.tx(OperationId::PowerModeSet, "power:mode:dc", std::string(kModeBestPerformance)));
    EF_CHECK(res.code() == ErrorCode::Unsupported);
    EF_CHECK(r.fs.files.empty());
    EF_CHECK(r.api.mode == kModeBalanced);  // AC'ye dokunulmadi
}

EF_TEST(power_mode_dc_does_not_touch_ac) {
    Rig r;
    r.api.battery = true;
    auto active = r.engine.apply(r.tx(OperationId::PowerModeSet, "power:mode:dc", std::string(kModeBestPerformance)));
    EF_CHECK(active.ok());
    EF_CHECK(r.api.dc_mode == kModeBestPerformance && r.api.mode == kModeBalanced);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.api.dc_mode == kModeBalanced);
}

EF_TEST(power_mode_unknown_target_refused) {
    Rig r;
    auto res = r.engine.apply(r.tx(OperationId::PowerModeSet, "power:mode:xx", std::string(kModeBestPerformance)));
    EF_CHECK(!res.ok());
    EF_CHECK(r.api.mode == kModeBalanced && r.api.dc_mode == kModeBalanced);
}

EF_TEST(power_mode_unknown_guid_refused) {
    Rig r;
    auto res = r.engine.apply(r.tx(OperationId::PowerModeSet, "power:mode:ac", "11111111-2222-3333-4444-555555555555"));
    EF_CHECK(!res.ok());
    EF_CHECK(r.api.mode == kModeBalanced);
}

EF_TEST(power_modes_unsupported_on_windows10_not_forced) {
    Rig r;
    r.api.modes_supported = false;
    auto res = r.engine.apply(r.tx(OperationId::PowerModeSet, "power:mode:ac", std::string(kModeBestPerformance)));
    EF_CHECK(res.code() == ErrorCode::Unsupported);
    EF_CHECK(r.fs.files.empty());
    EF_CHECK(r.mode.capability().state == SupportState::Unsupported);
}

EF_TEST(power_mode_capability_requires_balanced_plan) {
    Rig r;
    EF_CHECK(r.mode.capability().state == SupportState::Supported);
    r.api.scheme = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";  // Yuksek performans plani etkin
    const auto c = r.mode.capability();
    EF_CHECK(c.state == SupportState::Unsupported);
    EF_CHECK(c.reason.find("Balanced") != std::string::npos);
}

EF_TEST(power_scheme_only_switches_to_existing_plans) {
    Rig r;
    auto bad = r.engine.apply(r.tx(OperationId::PowerSchemeSet, "power:scheme:active", "e9a42b02-d5df-448d-aa00-03f14749eb61"));
    EF_CHECK(!bad.ok());  // Nihai performans yok: olusturulmaz
    EF_CHECK(r.api.schemes.size() == 2 && r.api.scheme == kSchemeBalanced);
    auto ok = r.engine.apply(r.tx(OperationId::PowerSchemeSet, "power:scheme:active", "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"));
    EF_CHECK(ok.ok());
    auto rb = r.engine.rollback(ok.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.api.scheme == kSchemeBalanced);
}

EF_TEST(guid_text_validation) {
    EF_CHECK(is_guid_text("381b4222-f694-41f0-9685-ff5bb260df2e"));
    EF_CHECK(!is_guid_text("381B4222-F694-41F0-9685-FF5BB260DF2E"));
    EF_CHECK(!is_guid_text("{381b4222-f694-41f0-9685-ff5bb260df2e}"));
    EF_CHECK(!is_guid_text("381b4222f69441f09685ff5bb260df2e"));
}
