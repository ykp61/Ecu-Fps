#include <atomic>
#include <vector>

#include "core/recovery/engine.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Rig {
    MemoryJournalFs fs;
    JournalStore store{fs, "m1"};
    FakeAdapter fake;
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() {
        reg.add(OperationId::GameModeSet, &fake);
        reg.add(OperationId::VisualEffectsSet, &fake);
        reg.add(OperationId::PowerModeSet, &fake);
    }
};
JournalTx tx3(const std::string& id = "0123456789abcdef0123456789abcdef") {
    JournalTx tx;
    tx.id = id;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    for (const char* t : {"a", "b", "c"}) {
        JournalStep s;
        s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, t, std::string("new-") + t, false, false};
        tx.steps.push_back(s);
    }
    return tx;
}
}  // namespace

EF_TEST(engine_happy_path_reaches_active_with_real_progress) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}};  // "c" yok
    std::vector<int> pct;
    EngineOptions opt;
    opt.on_progress = [&](const Progress& p) { pct.push_back(p.percent()); };
    auto res = r.engine.apply(tx3(), opt);
    EF_CHECK(res.ok());
    EF_CHECK(res.value().state == TxState::Active);
    EF_CHECK(r.fake.values["c"] == "new-c");
    for (const auto& s : res.value().steps) EF_CHECK(s.state == StepState::Verified);
    EF_CHECK(res.value().steps[2].snapshot.existed == false);
    // Ilerleme monoton, 100'de biter, 3 adim x 3 birim
    EF_CHECK(!pct.empty() && pct.back() == 100);
    for (std::size_t i = 1; i < pct.size(); ++i) EF_CHECK(pct[i] >= pct[i - 1]);
    EF_CHECK(r.store.load(res.value().id).value().state == TxState::Active);
}

EF_TEST(engine_partial_failure_compensates_completed_steps) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    r.fake.fail_apply_target = "c";
    auto res = r.engine.apply(tx3());
    EF_CHECK(!res.ok());  // Restored: hata olarak raporlanir ama tam geri alindi
    EF_CHECK(r.fake.values["a"] == "old-a");
    EF_CHECK(r.fake.values["b"] == "old-b");
    EF_CHECK(r.fake.values["c"] == "old-c");
    const auto l = r.store.scan();
    EF_CHECK(l.transactions.size() == 1 && l.transactions[0].state == TxState::Restored);
}

EF_TEST(engine_rollback_is_idempotent_and_does_not_create_override) {
    Rig r;
    r.fake.values = {{"a", "old-a"}};  // b ve c onceden yok
    auto active = r.engine.apply(tx3());
    EF_CHECK(active.ok());
    auto first = r.engine.rollback(active.value());
    EF_CHECK(first.ok() && first.value().state == TxState::Restored);
    EF_CHECK(r.fake.values.count("b") == 0);  // olmayan deger varsayilan yazilarak uydurulmadi
    EF_CHECK(r.fake.values.count("c") == 0);
    EF_CHECK(r.fake.values["a"] == "old-a");
    const int restores = r.fake.restores;
    auto second = r.engine.rollback(first.value());
    EF_CHECK(second.ok() && second.value().state == TxState::Restored);
    EF_CHECK(r.fake.restores == restores);  // ikinci geri alma hicbir sey yazmadi
}

EF_TEST(engine_readback_mismatch_rolls_back) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    r.fake.corrupt_readback_target = "b";
    auto res = r.engine.apply(tx3());
    EF_CHECK(!res.ok());
    EF_CHECK(r.fake.values["a"] == "old-a" && r.fake.values["b"] == "old-b");
    EF_CHECK(r.fake.values["c"] == "old-c");  // c'ye hic dokunulmadi
}

EF_TEST(engine_failed_restore_marks_recovery_required_and_keeps_journal) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    auto active = r.engine.apply(tx3());
    EF_CHECK(active.ok());
    r.fake.fail_restore_target = "b";
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok());
    EF_CHECK(rb.value().state == TxState::RecoveryRequired);  // basari uydurulmadi
    EF_CHECK(rb.value().steps[1].state == StepState::Failed);
    EF_CHECK(rb.value().steps[0].state == StepState::RolledBack);
    EF_CHECK(r.store.scan().transactions.size() == 1);  // kayit korunuyor
    r.fake.fail_restore_target.clear();
    auto again = r.engine.rollback(rb.value());           // tekrar denenebilir
    EF_CHECK(again.ok() && again.value().state == TxState::Restored);
    EF_CHECK(r.fake.values["b"] == "old-b");
}

EF_TEST(engine_cancel_before_next_step_rolls_back) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    std::atomic<bool> cancel{false};
    EngineOptions opt;
    opt.cancel = &cancel;
    opt.on_progress = [&](const Progress& p) {
        if (p.phase == Phase::Applying && p.done >= 4) cancel = true;  // ilk adim bitti
    };
    auto res = r.engine.apply(tx3(), opt);
    EF_CHECK(!res.ok());
    EF_CHECK(r.fake.values["a"] == "old-a");
    EF_CHECK(r.fake.applies == 1);
}

EF_TEST(engine_journal_failure_applies_nothing) {
    Rig r;
    r.fake.values = {{"a", "old-a"}};
    r.fs.fail_write_after = 0;
    auto res = r.engine.apply(tx3());
    EF_CHECK(!res.ok());
    EF_CHECK(r.fake.applies == 0);
    EF_CHECK(r.fake.values["a"] == "old-a");
}

// CORE-02: "Active" kalici yazimi basarisizsa islem sessizce Verifying'de BIRAKILMAZ; tam geri alinir.
// 3 adimli islem yazim sayisi: begin(1) + applying(2) + 3x(intent+verified)(3..8) + applied(9) + verifying(10)
// + active(11).
EF_TEST(engine_active_persist_failure_rolls_back_verified_changes) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    r.fs.fail_only_write = 11;  // yalniz Active yazimi bir kez basarisiz; sonrasi yazilabilir
    auto res = r.engine.apply(tx3());
    EF_CHECK(res.ok());
    EF_CHECK(res.value().state == TxState::Restored);  // hata gizlenmedi, ama durum da Verifying'de kalmadi
    EF_CHECK(r.fake.values["a"] == "old-a" && r.fake.values["b"] == "old-b" && r.fake.values["c"] == "old-c");
    EF_CHECK(r.store.load(res.value().id).value().state == TxState::Restored);
}

// CORE-02: journal tamamen yazilamaz hale gelirse (disk/ACL) tam geri alma kaydedilemez; hedef degerler
// journalsiz geri yuklenir ve sonuc "geri alindi" diye uydurulmadan hata olarak bildirilir.
EF_TEST(engine_active_persist_failure_with_dead_journal_restores_targets) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    r.fs.fail_write_after = 10;  // Verifying'e kadar yazildi; Active ve sonrasi yazilamaz
    auto res = r.engine.apply(tx3());
    EF_CHECK(!res.ok());
    EF_CHECK(res.error().detail.find("applied changes restored") != std::string::npos);
    EF_CHECK(r.fake.values["a"] == "old-a" && r.fake.values["b"] == "old-b" && r.fake.values["c"] == "old-c");
    // Disk kaydi "Active" degil: sonraki acilis onu uzlastirir (sessizce etkin sayilmaz).
    const auto l = r.store.scan();
    EF_CHECK(l.transactions.size() == 1 && l.transactions[0].state != TxState::Active);
}

// CORE-02: journalsiz geri yukleme de dogrulanamazsa basari uydurulmaz; journal RecoveryRequired kalir.
EF_TEST(engine_active_persist_failure_unverified_restore_marks_recovery_required) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}, {"c", "old-c"}};
    r.fs.fail_only_write = 11;  // Active yazimi bir kez basarisiz; rollback yazimlari calisir
    r.fake.fail_restore_target = "b";
    auto res = r.engine.apply(tx3());
    EF_CHECK(res.ok());
    EF_CHECK(res.value().state == TxState::RecoveryRequired);
    EF_CHECK(r.fake.values["b"] == "new-b");  // geri alinamayan hedef oldugu gibi birakildi
    EF_CHECK(r.store.load(res.value().id).value().state == TxState::RecoveryRequired);
}
