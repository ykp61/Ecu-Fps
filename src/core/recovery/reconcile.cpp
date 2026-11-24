#include "core/recovery/reconcile.h"

namespace ef::core::recovery {

using contracts::TxState;

ReconcileReport reconcile(JournalStore& store, TransactionEngine& engine) {
    ReconcileReport rep;
    auto l = store.scan();
    rep.corrupt = l.corrupt;
    rep.foreign = l.foreign;
    rep.newer = l.newer;
    for (auto& tx : l.transactions) {
        switch (tx.state) {
            case TxState::Restored: break;
            case TxState::Active: rep.active.push_back(tx.id); break;
            default: {
                // Prepared/Applying/Applied/Verifying/RollingBack/RecoveryRequired: geri al.
                // Cagiran (acilis/broker oturumu) kapiyi kendi dogrular; yarim islem kurtarmasi motorda ikinci kez reddedilmez.
                EngineOptions opt;
                opt.bypass_guard = true;
                auto r = engine.rollback(tx, opt);
                if (r.ok() && r.value().state == TxState::Restored) rep.restored.push_back(tx.id);
                else rep.recovery_required.push_back(tx.id);
            }
        }
    }
    return rep;
}

}  // namespace ef::core::recovery
