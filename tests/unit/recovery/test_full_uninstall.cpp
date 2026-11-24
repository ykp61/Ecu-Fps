// ADR-033 / spec 8: kurulu programin tam kaldirilmasi (run_full_uninstall). Sahte veri ve program kaldiricilari; hicbir
// gercek dosya, hizmet veya kayit defteri yazilmaz.
#include <string>
#include <vector>

#include "core/recovery/uninstall.h"
#include "ef_test.h"
#include "platform/windows/install/program_remover.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Log {
    std::vector<std::string> calls;
};

class FakeData final : public DataRemover {
public:
    explicit FakeData(Log& l) : log(l) {}
    Status remove_autostart() override { log.calls.push_back("autostart"); return ok_status(); }
    Status remove_user_data() override {
        log.calls.push_back("user");
        if (fail_user) return Status::fail(ErrorCode::IoError, "locked");
        return ok_status();
    }
    Status remove_machine_data() override { log.calls.push_back("machine"); return ok_status(); }
    Log& log;
    bool fail_user = false;
};

class FakeProgram final : public ProgramRemover {
public:
    explicit FakeProgram(Log& l) : log(l) {}
    Outcome remove_program() override {
        log.calls.push_back("program");
        return out;
    }
    Log& log;
    Outcome out{true, true, false, false, {}};
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

bool has_note(const UninstallResult& r, const char* n) {
    for (const auto& x : r.notes)
        if (x == n) return true;
    return false;
}
}  // namespace

EF_TEST(full_uninstall_installed_happy_path_order) {
    Rig r;
    Log log;
    FakeData d(log);
    FakeProgram p(log);
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(res.restored_all && res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::Done);
    EF_CHECK(r.fake.values["a"] == "old-a");  // once geri yukleme
    // Makine verisini program kaldirici (isci) siler; burada ayrica cagrilmaz.
    EF_CHECK((log.calls == std::vector<std::string>{"program", "autostart", "user"}));
    EF_CHECK(!res.reboot_pending && res.notes.empty());
}

EF_TEST(full_uninstall_restore_incomplete_calls_nothing) {
    Rig r;
    r.fake.fail_restore_target = "a";
    Log log;
    FakeData d(log);
    FakeProgram p(log);
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(!res.restored_all && !res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::Restore);
    EF_CHECK(log.calls.empty());
    EF_CHECK(r.store.scan().transactions.size() == 1);
}

EF_TEST(full_uninstall_program_denied_keeps_autostart_and_user_data) {
    Rig r;
    Log log;
    FakeData d(log);
    FakeProgram p(log);
    p.out = {false, false, false, true, "uac denied"};
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(res.restored_all && !res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::RemoveProgram);
    EF_CHECK((log.calls == std::vector<std::string>{"program"}));
    EF_CHECK(has_note(res, kNoteProgramDenied));
}

EF_TEST(full_uninstall_program_not_started_counts_as_failed_not_denied) {
    Rig r;
    Log log;
    FakeData d(log);
    FakeProgram p(log);
    p.out = {false, false, false, false, "launch failed"};
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(!res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::RemoveProgram);
    EF_CHECK((log.calls == std::vector<std::string>{"program"}));
    // UAC reddi degil: kullaniciya "izin verilmedi" denmez.
    EF_CHECK(has_note(res, kNoteProgramFailed) && !has_note(res, kNoteProgramDenied));
}

EF_TEST(full_uninstall_program_failed_keeps_autostart_and_user_data) {
    Rig r;
    Log log;
    FakeData d(log);
    FakeProgram p(log);
    p.out = {true, false, false, false, "rolled back"};
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(res.restored_all && !res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::RemoveProgram);
    EF_CHECK((log.calls == std::vector<std::string>{"program"}));
    EF_CHECK(has_note(res, kNoteProgramFailed));
}

EF_TEST(full_uninstall_propagates_reboot_pending) {
    Rig r;
    Log log;
    FakeData d(log);
    FakeProgram p(log);
    p.out = {true, true, true, false, {}};
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(res.data_removed && res.reboot_pending);
    EF_CHECK(has_note(res, kNoteProgramRebootPending));
}

EF_TEST(full_uninstall_user_data_failure_after_program_stops) {
    Rig r;
    Log log;
    FakeData d(log);
    d.fail_user = true;
    FakeProgram p(log);
    const auto res = run_full_uninstall(r.scopes(), d, &p);
    EF_CHECK(!res.data_removed);
    EF_CHECK(res.stopped_at == UninstallStage::RemoveUserData);
}

EF_TEST(program_outcome_maps_worker_results) {
    using ef::platform::install::program_outcome;
    using ef::platform::install::SetupRun;
    using ef::core::ElevationOutcome;
    SetupRun denied;
    denied.elevation = ElevationOutcome::Denied;
    auto o = program_outcome(denied);
    EF_CHECK(o.denied && !o.attempted && !o.removed);

    SetupRun failed;  // baslatilamadi
    o = program_outcome(failed);
    EF_CHECK(!o.denied && !o.attempted && !o.removed);

    SetupRun no_reply;
    no_reply.elevation = ElevationOutcome::Started;
    o = program_outcome(no_reply);  // sonuc bilinmiyor: basari varsayilmaz
    EF_CHECK(o.attempted && !o.removed);

    SetupRun busy;
    busy.elevation = ElevationOutcome::Started;
    busy.reply = ef::core::setup::SetupReply{};
    busy.reply->result.ok = true;
    busy.reply->error = "busy";
    o = program_outcome(busy);
    EF_CHECK(o.attempted && !o.removed);

    SetupRun rolled_back;
    rolled_back.elevation = ElevationOutcome::Started;
    rolled_back.reply = ef::core::setup::SetupReply{};
    rolled_back.reply->result.reboot_pending = true;  // ok degilken yok sayilir
    o = program_outcome(rolled_back);
    EF_CHECK(o.attempted && !o.removed && !o.reboot_pending);

    SetupRun ok;
    ok.elevation = ElevationOutcome::Started;
    ok.reply = ef::core::setup::SetupReply{};
    ok.reply->result.ok = true;
    ok.reply->result.reboot_pending = true;
    o = program_outcome(ok);
    EF_CHECK(o.attempted && o.removed && o.reboot_pending && !o.denied);
}

EF_TEST(full_uninstall_portable_equals_old_behaviour) {
    Rig r1, r2;
    Log l1, l2;
    FakeData d1(l1), d2(l2);
    const auto a = run_full_uninstall(r1.scopes(), d1, nullptr);
    const auto b = run_uninstall(r2.scopes(), d2);
    EF_CHECK(a.restored_all == b.restored_all && a.data_removed == b.data_removed);
    EF_CHECK(a.stopped_at == b.stopped_at && a.notes == b.notes);
    EF_CHECK((l1.calls == std::vector<std::string>{"autostart", "machine", "user"}));
    EF_CHECK(l1.calls == l2.calls);
    EF_CHECK(!a.reboot_pending);
}
