#include "core/recovery/history_view.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
JournalTx tx(const char* id, std::int64_t t, TxState st, std::vector<StepState> steps) {
    JournalTx x;
    x.id = id;
    x.created_unix_ms = t;
    x.state = st;
    for (auto s : steps) {
        JournalStep js;
        js.op = SettingOperation{OperationId::PowerSchemeSet, OperationScope::CurrentUser, "scheme", "hp", false, false};
        js.snapshot.existed = true;
        js.snapshot.old_value = "balanced";
        js.last_written = s == StepState::Planned ? "" : "hp";
        js.state = s;
        x.steps.push_back(js);
    }
    return x;
}
}  // namespace

EF_TEST(history_sorted_newest_first_with_real_values_and_status) {
    JournalStore::Listing user, startup;
    user.transactions = {tx("a", 100, TxState::Restored, {StepState::RolledBack}),
                         tx("b", 300, TxState::Active, {StepState::Verified})};
    user.corrupt = {"tx-bad.json"};
    startup.transactions = {tx("c", 200, TxState::Restored, {StepState::RolledBack, StepState::KeptExternal})};
    startup.foreign = {"tx-other.json"};
    const auto v = build_history({{"user", user}, {"startup", startup}});
    EF_CHECK(v.rows.size() == 3 && v.total == 3);
    EF_CHECK(v.rows[0].id == "b" && v.rows[0].status == HistoryStatus::Active && v.rows[0].scope == "user");
    EF_CHECK(v.rows[1].id == "c" && v.rows[1].status == HistoryStatus::PartlyRestored);  // dis deger korundu
    EF_CHECK(v.rows[2].id == "a" && v.rows[2].status == HistoryStatus::Restored);
    EF_CHECK(v.rows[0].steps[0].old_value == "balanced" && v.rows[0].steps[0].written == "hp" && v.rows[0].steps[0].existed);
    EF_CHECK(v.corrupt == 1 && v.foreign == 1 && v.newer == 0);  // bozuk kayit sayilir, gizlenmez
}

EF_TEST(history_incomplete_and_recovery_states_and_row_cap) {
    JournalStore::Listing l;
    l.transactions = {tx("p", 1, TxState::Applying, {StepState::Intent}), tx("r", 2, TxState::RecoveryRequired, {StepState::Failed})};
    for (int i = 0; i < 10; ++i) l.transactions.push_back(tx("z", 10 + i, TxState::Restored, {}));
    const auto v = build_history({{"user", l}}, 5);
    EF_CHECK(v.rows.size() == 5 && v.total == 12);  // ust sinir; toplam ayrica
    const auto all = build_history({{"user", l}});
    EF_CHECK(all.rows[10].status == HistoryStatus::RecoveryRequired && all.rows[11].status == HistoryStatus::Incomplete);
    EF_CHECK(all.rows[11].steps[0].state == StepState::Intent);  // adim durumu oldugu gibi
}

EF_TEST(history_kept_current_is_not_partly_restored) {
    const auto kept = tx("k", 1, TxState::Restored, {StepState::KeptExternal});
    const auto mixed = tx("m", 2, TxState::Restored, {StepState::RolledBack, StepState::KeptExternal});
    EF_CHECK(status_of(kept) == HistoryStatus::KeptCurrent);  // hicbir sey geri alinmadi: kullanici degeri korudu
    EF_CHECK(status_of(mixed) == HistoryStatus::PartlyRestored);
    EF_CHECK(status_of(tx("r", 3, TxState::Restored, {StepState::RolledBack, StepState::Planned})) == HistoryStatus::Restored);
}