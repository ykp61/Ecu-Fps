#include "core/recovery/history_view.h"

#include <algorithm>

namespace ef::core::recovery {

HistoryStatus status_of(const JournalTx& tx) noexcept {
    using contracts::TxState;
    switch (tx.state) {
        case TxState::Active: return HistoryStatus::Active;
        case TxState::RecoveryRequired: return HistoryStatus::RecoveryRequired;
        case TxState::Restored: {
            bool kept = false, rolled = false;
            for (const auto& s : tx.steps) {
                kept = kept || s.state == StepState::KeptExternal;
                rolled = rolled || s.state == StepState::RolledBack;
            }
            if (kept) return rolled ? HistoryStatus::PartlyRestored : HistoryStatus::KeptCurrent;
            return HistoryStatus::Restored;
        }
        default: return HistoryStatus::Incomplete;  // yarim kaldi: acilista uzlastirilir
    }
}

HistoryView build_history(const std::vector<std::pair<std::string, JournalStore::Listing>>& listings, std::size_t max_rows) {
    HistoryView v;
    for (const auto& [scope, l] : listings) {
        v.corrupt += l.corrupt.size();
        v.foreign += l.foreign.size();
        v.newer += l.newer.size();
        for (const auto& tx : l.transactions) {
            HistoryTxRow r;
            r.scope = scope;
            r.id = tx.id;
            r.created_unix_ms = tx.created_unix_ms;
            r.status = status_of(tx);
            for (const auto& s : tx.steps) {
                HistoryStepRow sr;
                sr.op = s.op.id;
                sr.target = s.op.target;
                sr.existed = s.snapshot.existed;
                sr.old_value = s.snapshot.old_value;
                sr.written = s.last_written;
                sr.state = s.state;
                sr.has_error = !s.error.empty();
                r.steps.push_back(std::move(sr));
            }
            v.rows.push_back(std::move(r));
        }
    }
    v.total = v.rows.size();
    std::stable_sort(v.rows.begin(), v.rows.end(), [](const HistoryTxRow& a, const HistoryTxRow& b) {
        if (a.created_unix_ms != b.created_unix_ms) return a.created_unix_ms > b.created_unix_ms;
        return a.id > b.id;
    });
    if (v.rows.size() > max_rows) v.rows.resize(max_rows);
    return v;
}

}  // namespace ef::core::recovery
