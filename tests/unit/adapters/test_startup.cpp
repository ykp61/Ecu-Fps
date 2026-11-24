#include "core/adapters/startup/startup.h"
#include "ef_test.h"

using namespace ef::core::adapters::startup;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Rig {
    FakeStartupApi api;
    StartupAdapter adapter{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    Rig() { reg.add(OperationId::StartupRunEntry, &adapter); }
    JournalTx tx(Location l, const std::string& name, const char* v) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = SettingOperation{OperationId::StartupRunEntry, OperationScope::CurrentUser, format_target(l, name), v, false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(startup_raw_format) {
    EF_CHECK(approval_from_raw("020000000000000000000000") == Approval::Enabled);
    EF_CHECK(approval_from_raw("0300000011223344aabbccdd") == Approval::Disabled);
    EF_CHECK(approval_from_raw("060000000000000000000000") == Approval::Enabled);
    EF_CHECK(approval_from_raw("070000000000000000000000") == Approval::Disabled);
    EF_CHECK(make_raw(Approval::Disabled, 0x0807060504030201ull) == "030000000102030405060708");
    EF_CHECK(make_raw(Approval::Enabled, 0x0807060504030201ull) == "020000000000000000000000");
}

EF_TEST(startup_unicode_name_target_round_trip) {
    const std::string name = "Discord Güncelleyici İşlem";
    const auto t = format_target(Location::UserRun, name);
    auto back = parse_target(t);
    EF_CHECK(back.ok() && back.value().name == name && back.value().location == Location::UserRun);
    EF_CHECK(!parse_target("startup:user_run:zz").ok());
    EF_CHECK(!parse_target("startup:runonce:6162").ok());  // RunOnce konumu yok
    EF_CHECK(!parse_target(format_target(Location::UserFolder, "..\\x.lnk")).ok());
}

EF_TEST(startup_disable_then_restore_absent_value) {
    Rig r;
    r.api.entries[{static_cast<int>(Location::UserRun), "Spotify"}] = "C:\\Users\\x\\Spotify.exe /minimized";
    auto active = r.engine.apply(r.tx(Location::UserRun, "Spotify", "disabled"));
    EF_CHECK(active.ok());
    EF_CHECK(r.api.list(Location::UserRun).value()[0].approval == Approval::Disabled);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK(r.api.approved.empty());  // onceden StartupApproved yoktu -> silindi, "02" uydurulmadi
    EF_CHECK(r.api.entries.size() == 1);  // Run kaydi hic silinmedi
}

EF_TEST(startup_restore_exact_previous_bytes) {
    Rig r;
    r.api.entries[{static_cast<int>(Location::UserFolder), "Steam.lnk"}] = "Steam.lnk";
    r.api.approved[{static_cast<int>(Location::UserFolder), "Steam.lnk"}] = "060000000000000000000000";  // kullanici acik (06)
    auto active = r.engine.apply(r.tx(Location::UserFolder, "Steam.lnk", "disabled"));
    EF_CHECK(active.ok());
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK((r.api.approved[{static_cast<int>(Location::UserFolder), "Steam.lnk"}] == "060000000000000000000000"));
}

EF_TEST(startup_machine_32_64_views_are_distinct_and_need_admin) {
    Rig r;
    r.api.entries[{static_cast<int>(Location::MachineRun64), "Agent"}] = "a";
    r.api.entries[{static_cast<int>(Location::MachineRun32), "Agent"}] = "b";
    auto denied = r.engine.apply(r.tx(Location::MachineRun32, "Agent", "disabled"));
    EF_CHECK(!denied.ok());  // yukseltme yok: Denied
    r.api.elevated = true;
    auto active = r.engine.apply(r.tx(Location::MachineRun32, "Agent", "disabled"));
    EF_CHECK(active.ok());
    EF_CHECK(r.api.list(Location::MachineRun32).value()[0].approval == Approval::Disabled);
    EF_CHECK(r.api.list(Location::MachineRun64).value()[0].approval == Approval::Enabled);  // 64 bit etkilenmedi
    EF_CHECK(requires_admin(Location::MachineRun64) && !requires_admin(Location::UserRun));
}

EF_TEST(startup_missing_entry_not_created) {
    Rig r;
    auto res = r.engine.apply(r.tx(Location::UserRun, "Ghost", "disabled"));
    EF_CHECK(!res.ok() && res.code() == ErrorCode::MissingValue);
    EF_CHECK(r.api.approved.empty());
}
