#include "core/recovery/uninstall.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
class FakeRemover final : public DataRemover {
public:
    Status remove_autostart() override { ++calls; autostart = false; return ok_status(); }
    Status remove_user_data() override { ++calls; user = false; return ok_status(); }
    Status remove_machine_data() override {
        ++calls;
        if (machine_denied) return Status::fail(ErrorCode::Denied, "needs admin");
        machine = false;
        return ok_status();
    }
    bool autostart = true, user = true, machine = true, machine_denied = false;
    int calls = 0;
};

struct Rig {
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    FakeAdapter fake;
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() {
        reg.add(OperationId::GameModeSet, &fake);
        fake.values = {{"a", "old-a"}};
        JournalTx tx;
        tx.id = "0123456789abcdef0123456789abcdef";
        tx.target_sid = "S-1-5-21-1-2-3-1001";
        JournalStep s;
        s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, "a", "new-a", false, false};
        tx.steps.push_back(s);
        (void)engine.apply(tx);
    }
    std::vector<JournalScope> scopes() { return {{"user", &store, &engine}}; }
};
}  // namespace

EF_TEST(uninstall_restores_then_removes_everything) {
    Rig r;
    EF_CHECK(r.fake.values["a"] == "new-a");
    FakeRemover rm;
    const auto res = run_uninstall(r.scopes(), rm);
    EF_CHECK(res.restored_all && res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::Done);
    EF_CHECK(r.fake.values["a"] == "old-a");
    EF_CHECK(!rm.autostart && !rm.user && !rm.machine);
}

EF_TEST(uninstall_partial_failure_keeps_data) {
    Rig r;
    r.fake.fail_restore_target = "a";
    FakeRemover rm;
    const auto res = run_uninstall(r.scopes(), rm);
    EF_CHECK(!res.restored_all && !res.data_removed);
    EF_CHECK(!res.unrestored.empty());
    EF_CHECK(rm.calls == 0);                // hicbir veri silinmedi
    EF_CHECK(rm.user && rm.autostart && rm.machine);
    EF_CHECK(r.store.scan().transactions.size() == 1);  // journal tekrar denemek icin duruyor
}

EF_TEST(uninstall_reports_machine_data_needing_admin) {
    Rig r;
    FakeRemover rm;
    rm.machine_denied = true;
    const auto res = run_uninstall(r.scopes(), rm);
    EF_CHECK(res.restored_all && res.data_removed);
    EF_CHECK(rm.machine);                   // makine verisi yerinde ...
    EF_CHECK(!res.notes.empty());           // ... ve acikca bildirildi
}

EF_TEST(uninstall_with_corrupt_journal_keeps_data) {
    Rig r;
    r.fs.files.emplace_back("tx-ffffffffffffffffffffffffffffffff.json", "{broken");
    FakeRemover rm;
    const auto res = run_uninstall(r.scopes(), rm);
    EF_CHECK(!res.data_removed && rm.calls == 0);
}
