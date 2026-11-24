#include <stdexcept>
#include <string>

#include "core/recovery/reconcile.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct ProcessCrash : std::runtime_error {
    ProcessCrash() : std::runtime_error("simulated process death") {}
};

// N. yazimda "surec olur": o yazim diske ulasmaz ve akis aninda kesilir.
class CrashingFs final : public JournalFs {
public:
    explicit CrashingFs(MemoryJournalFs& inner) : in(inner) {}
    ef::contracts::Status write_atomic(const std::string& n, const std::string& b) override {
        if (crash_at >= 0 && ++count > crash_at) throw ProcessCrash();
        return in.write_atomic(n, b);
    }
    ef::contracts::Result<std::string> read(const std::string& n) override { return in.read(n); }
    ef::contracts::Result<std::vector<std::string>> list() override { return in.list(); }
    ef::contracts::Status remove(const std::string& n) override { return in.remove(n); }
    MemoryJournalFs& in;
    int crash_at = -1;
    int count = 0;
};

// Uygulama yazdiktan HEMEN sonra (sonuc kaydindan once) olen adaptor.
class CrashAfterWrite final : public SettingAdapter {
public:
    explicit CrashAfterWrite(FakeAdapter& f) : fake(f) {}
    ef::contracts::Result<Snapshot> capture(const SettingOperation& op) override { return fake.capture(op); }
    ef::contracts::Status apply(const SettingOperation& op) override {
        auto s = fake.apply(op);
        if (op.target == crash_target) throw ProcessCrash();
        return s;
    }
    ef::contracts::Result<std::optional<std::string>> read_back(const SettingOperation& op) override { return fake.read_back(op); }
    ef::contracts::Status restore(const SettingOperation& op, const Snapshot& s) override { return fake.restore(op, s); }
    FakeAdapter& fake;
    std::string crash_target;
};

JournalTx tx3() {
    JournalTx tx;
    tx.id = "0123456789abcdef0123456789abcdef";
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    for (const char* t : {"a", "b", "c"}) {
        JournalStep s;
        s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, t, std::string("new-") + t, false, false};
        tx.steps.push_back(s);
    }
    return tx;
}
const std::map<std::string, std::string> kOriginal{{"a", "old-a"}, {"b", "old-b"}};  // c yok
}  // namespace

EF_TEST(crash_at_every_journal_write_then_reconcile_restores_everything) {
    // Mutlu yoldaki yazim sayisini olc
    int total_writes = 0;
    {
        MemoryJournalFs mem;
        JournalStore st(mem, "m");
        FakeAdapter f;
        f.values = kOriginal;
        AdapterRegistry reg;
        reg.add(OperationId::GameModeSet, &f);
        TransactionEngine eng(st, reg);
        EF_CHECK(eng.apply(tx3()).ok());
        total_writes = mem.writes;
    }
    EF_CHECK(total_writes >= 8);
    for (int crash = 0; crash < total_writes; ++crash) {
        MemoryJournalFs mem;
        CrashingFs fs(mem);
        fs.crash_at = crash;
        FakeAdapter f;
        f.values = kOriginal;
        AdapterRegistry reg;
        reg.add(OperationId::GameModeSet, &f);
        {
            JournalStore st(fs, "m");
            TransactionEngine eng(st, reg);
            bool crashed = false;
            try { (void)eng.apply(tx3()); } catch (const ProcessCrash&) { crashed = true; }
            EF_CHECK(crashed);
        }
        // Yeniden baslatma: ayni disk, ayni gercek hedefler
        fs.crash_at = -1;
        JournalStore st2(fs, "m");
        TransactionEngine eng2(st2, reg);
        const auto rep = reconcile(st2, eng2);
        EF_CHECK(rep.recovery_required.empty());
        EF_CHECK(rep.corrupt.empty());
        EF_CHECK(f.values == kOriginal);  // her cokme noktasinda tam geri donus
    }
}

EF_TEST(write_before_result_crash_uses_real_target) {
    MemoryJournalFs mem;
    FakeAdapter f;
    f.values = kOriginal;
    CrashAfterWrite crashing(f);
    crashing.crash_target = "b";  // b yazildi, sonucu kaydedilmeden oldu (journal'da Intent)
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &crashing);
    {
        JournalStore st(mem, "m");
        TransactionEngine eng(st, reg);
        bool crashed = false;
        try { (void)eng.apply(tx3()); } catch (const ProcessCrash&) { crashed = true; }
        EF_CHECK(crashed);
    }
    EF_CHECK(f.values["b"] == "new-b");  // gercek hedef degismis
    JournalStore st2(mem, "m");
    const auto pre = st2.scan();
    EF_CHECK(pre.transactions.size() == 1 && pre.transactions[0].steps[1].state == StepState::Intent);
    const int restores_before = f.restores;
    TransactionEngine eng2(st2, reg);
    const auto rep = reconcile(st2, eng2);
    EF_CHECK(rep.restored.size() == 1);
    EF_CHECK(f.values == kOriginal);
    // c'ye hic yazilmamisti: zaten anlik goruntude (yok) -> yazma yapilmadi; yalniz a ve b geri yuklendi
    EF_CHECK(f.restores - restores_before == 2);
}

EF_TEST(corrupt_journal_is_not_deleted_or_reported_as_success) {
    MemoryJournalFs mem;
    mem.files.emplace_back("tx-fedcba9876543210fedcba9876543210.json", "{\"schema_version\":1,\"sha256\":\"00\",\"tx\":{}}");
    JournalStore st(mem, "m");
    FakeAdapter f;
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    TransactionEngine eng(st, reg);
    const auto rep = reconcile(st, eng);
    EF_CHECK(rep.corrupt.size() == 1);
    EF_CHECK(!rep.clean());
    EF_CHECK(mem.files.size() == 1);  // silinmedi
}

EF_TEST(external_change_is_not_overwritten_during_recovery) {
    MemoryJournalFs mem;
    FakeAdapter f;
    f.values = kOriginal;
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    JournalStore st(mem, "m");
    TransactionEngine eng(st, reg);
    auto active = eng.apply(tx3());
    EF_CHECK(active.ok());
    f.values["b"] = "user-changed";  // kullanici sonradan degistirdi
    auto rb = eng.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::RecoveryRequired);
    EF_CHECK(f.values["b"] == "user-changed");   // ezilmedi
    EF_CHECK(f.values["a"] == "old-a");
    EF_CHECK(rb.value().steps[1].error == std::string(kExternalChange));
}

EF_TEST(active_transactions_survive_restart_for_ui_decision) {
    MemoryJournalFs mem;
    FakeAdapter f;
    f.values = kOriginal;
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    JournalStore st(mem, "m");
    TransactionEngine eng(st, reg);
    EF_CHECK(eng.apply(tx3()).ok());
    const auto rep = reconcile(st, eng);
    EF_CHECK(rep.active.size() == 1);
    EF_CHECK(f.values["a"] == "new-a");  // kendiliginden geri alinmaz; UI karar verir
}
