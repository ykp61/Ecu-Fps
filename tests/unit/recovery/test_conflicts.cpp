#include "core/recovery/conflicts.h"
#include "core/recovery/history_view.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Rig {
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    FakeAdapter fake;
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() { reg.add(OperationId::GameModeSet, &fake); }
};
JournalTx tx2() {
    JournalTx tx;
    tx.id = "0123456789abcdef0123456789abcdef";
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    for (const char* t : {"a", "b"}) {
        JournalStep s;
        s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, t, std::string("new-") + t, false, false};
        tx.steps.push_back(s);
    }
    return tx;
}
}  // namespace

EF_TEST(user_says_no_keeps_value_and_reports_partial) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}};
    auto active = r.engine.apply(tx2());
    r.fake.values["b"] = "user";
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::RecoveryRequired);
    const auto cs = find_conflicts(rb.value(), r.reg);
    EF_CHECK(cs.size() == 1 && cs[0].target == "b" && cs[0].current == std::optional<std::string>("user"));
    EF_CHECK(cs[0].written == "new-b" && cs[0].original.old_value == "old-b");
    auto res = resolve_conflict(rb.value(), cs[0].step_index, ConflictDecision::KeepCurrent, r.reg, r.store);
    EF_CHECK(res.ok());
    EF_CHECK(res.value().state == TxState::Restored);
    EF_CHECK(restore_outcome(res.value()) == RestoreOutcome::PartialKeptUserChanges);  // tam restore uydurulmadi
    EF_CHECK(r.fake.values["b"] == "user");
    EF_CHECK(r.fake.values["a"] == "old-a");
    // Tekrar rollback no-op
    auto again = r.engine.rollback(res.value());
    EF_CHECK(again.value().state == TxState::Restored && r.fake.values["b"] == "user");
}

EF_TEST(user_says_yes_restores_original) {
    Rig r;
    r.fake.values = {{"a", "old-a"}};  // b onceden yok
    auto active = r.engine.apply(tx2());
    r.fake.values["b"] = "user";
    auto rb = r.engine.rollback(active.value());
    auto res = resolve_conflict(rb.value(), 1, ConflictDecision::RestoreOriginal, r.reg, r.store);
    EF_CHECK(res.ok() && res.value().state == TxState::Restored);
    EF_CHECK(restore_outcome(res.value()) == RestoreOutcome::Full);
    EF_CHECK(r.fake.values.count("b") == 0);  // onceden yoktu: silindi
    EF_CHECK(!resolve_conflict(res.value(), 1, ConflictDecision::KeepCurrent, r.reg, r.store).ok());  // tekrar cozulmez
}

EF_TEST(inherited_value_restore_removes_override) {
    Rig r;
    // NVIDIA profil benzeri: deger global'den miras; uygulama acik override yazar
    auto tx = tx2();
    tx.steps.pop_back();
    auto active = r.engine.apply(tx);
    EF_CHECK(active.ok());
    EF_CHECK(active.value().steps[0].snapshot.existed == false);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK(r.fake.values.count("a") == 0);  // varsayilan deger yazilmadi; override kaldirildi
}

EF_TEST(game_config_unrelated_fields_preserved) {
    const std::string original =
        "// CS2 user config\r\nfps_max \"0\"\r\nsensitivity \"1.2\"\r\nmat_vsync 1\r\nvolume=0.5\r\n";
    // Uygulama fps_max ve mat_vsync'i degistirdi, yeni bir alan ekledi
    std::string applied = write_field(original, "fps_max", "300");
    applied = write_field(applied, "mat_vsync", "0");
    applied = write_field(applied, "r_dynamic", "0");
    // Sonra kullanici oyunda hassasiyeti ve sesi degistirdi (ilgisiz alanlar) ve mat_vsync'i elle 1 yapti
    std::string user = write_field(applied, "sensitivity", "2.5");
    user = write_field(user, "volume", "0.8");
    user = write_field(user, "mat_vsync", "1");
    const std::vector<FieldChange> ours{{"fps_max", "0", "300"}, {"mat_vsync", "1", "0"}, {"r_dynamic", std::nullopt, "0"}};
    const auto m = restore_fields(user, ours);
    EF_CHECK(read_field(m.text, "fps_max") == std::optional<std::string>("0"));
    EF_CHECK(!read_field(m.text, "r_dynamic").has_value());          // bizim ekledigimiz kaldirildi
    EF_CHECK(read_field(m.text, "sensitivity") == std::optional<std::string>("2.5"));  // ilgisiz alan korundu
    EF_CHECK(read_field(m.text, "volume") == std::optional<std::string>("0.8"));
    EF_CHECK(read_field(m.text, "mat_vsync") == std::optional<std::string>("1"));      // kullanici zaten orijinale dondurmus
    EF_CHECK(m.conflicts.empty());
    EF_CHECK(m.text.find("// CS2 user config") == 0);                 // yorum korundu
    EF_CHECK(m.text.find("fps_max \"0\"\r") != std::string::npos);   // tirnak ve CRLF bicimi korundu
    // Kullanici bizim alanimizi baska bir degere cevirdiyse catisma, ezilmez
    const auto m2 = restore_fields(write_field(applied, "fps_max", "144"), ours);
    EF_CHECK(m2.conflicts.size() == 1 && m2.conflicts[0] == "fps_max");
    EF_CHECK(read_field(m2.text, "fps_max") == std::optional<std::string>("144"));
}

// "Mevcut degeri koru": catisan adim(lar) KeptExternal; ayar okunmaz/yazilmaz; tum adimlar cozulunce Restored.
EF_TEST(keep_external_values_closes_without_touching_target) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}};
    auto active = r.engine.apply(tx2());
    r.fake.values["a"] = "user-a";
    r.fake.values["b"] = "user-b";
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::RecoveryRequired && has_external_conflict(rb.value()));
    const int restores = r.fake.restores, applies = r.fake.applies;
    // Yalniz "b" eslesir: "a" catismasi kalir, islem kapanmaz
    auto part = keep_external_values(rb.value(), r.store, [](const SettingOperation& o) { return o.target == "b"; });
    EF_CHECK(part.ok() && part.value().state == TxState::RecoveryRequired && has_external_conflict(part.value()));
    auto all = keep_external_values(part.value(), r.store);
    EF_CHECK(all.ok() && all.value().state == TxState::Restored);
    EF_CHECK(all.value().steps[0].state == StepState::KeptExternal && all.value().steps[1].state == StepState::KeptExternal);
    EF_CHECK(r.fake.restores == restores && r.fake.applies == applies);  // hicbir yazma yok
    EF_CHECK(r.fake.values["a"] == "user-a" && r.fake.values["b"] == "user-b");
    EF_CHECK(status_of(all.value()) == HistoryStatus::KeptCurrent);  // "kismen geri alindi" denmez
    EF_CHECK(restore_outcome(all.value()) == RestoreOutcome::PartialKeptUserChanges);
    // Kalici: diskteki kayit da kapandi
    EF_CHECK(r.store.scan().transactions.size() == 1 && r.store.scan().transactions[0].state == TxState::Restored);
    // Catisma yoksa reddedilir (yazma yok)
    EF_CHECK(!keep_external_values(all.value(), r.store).ok());
}

EF_TEST(keep_external_values_refuses_non_conflict_recovery) {
    Rig r;
    r.fake.values = {{"a", "old-a"}, {"b", "old-b"}};
    auto active = r.engine.apply(tx2());
    r.fake.fail_restore_target = "b";  // geri alma hatasi (dis degisiklik degil)
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::RecoveryRequired && !has_external_conflict(rb.value()));
    EF_CHECK(!keep_external_values(rb.value(), r.store).ok());
    EF_CHECK(!has_external_conflict(active.value()));  // Active: catisma yok
}