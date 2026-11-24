#include "core/recovery/service.h"

namespace ef::core::recovery {

using contracts::ErrorCode;
using contracts::TxState;

bool is_machine_scope(std::string_view name) noexcept { return name == "machine" || name.substr(0, 8) == "machine_"; }

std::vector<JournalScope> scopes_for_role(const std::vector<JournalScope>& all, bool machine_allowed) {
    std::vector<JournalScope> out;
    for (const auto& sc : all)
        if (machine_allowed || !is_machine_scope(sc.name)) out.push_back(sc);
    return out;
}

bool LazyScope::ensure(const std::function<std::unique_ptr<JournalFs>()>& make_fs, const std::string& machine_id, const AdapterRegistry& adapters) {
    if (store || !make_fs) return false;
    auto f = make_fs();
    if (!f) return false;
    fs = std::move(f);
    store = std::make_unique<JournalStore>(*fs, machine_id);
    engine = std::make_unique<TransactionEngine>(*store, adapters);
    return true;
}

RecoverySummary run_startup_recovery(const std::vector<JournalScope>& scopes) {
    RecoverySummary s;
    for (const auto& sc : scopes) {
        if (!sc.store || !sc.engine) continue;
        auto rep = reconcile(*sc.store, *sc.engine);
        s.writes_blocked = s.writes_blocked || rep.writes_blocked();
        s.needs_user = s.needs_user || !rep.clean();
        s.restored += rep.restored.size();
        s.active += rep.active.size();
        s.scopes[sc.name] = std::move(rep);
    }
    return s;
}

RestoreAllResult restore_all_active(const std::vector<JournalScope>& scopes) {
    RestoreAllResult r;
    for (const auto& sc : scopes) {
        if (!sc.store || !sc.engine) continue;
        auto l = sc.store->scan();
        // En yeniden eskiye: ust uste binen islemlerde son uygulanan once geri alinir.
        for (auto it = l.transactions.rbegin(); it != l.transactions.rend(); ++it) {
            if (it->state != TxState::Active && it->state != TxState::RecoveryRequired) continue;
            auto out = sc.engine->rollback(*it);
            if (out.ok() && out.value().state == TxState::Restored) ++r.restored;
            else r.failed.push_back(it->id);
        }
    }
    return r;
}

contracts::Result<int> Migrator::migrate(json::Value& tx, int from, int target) const {
    using R = contracts::Result<int>;
    if (from > target) return R::fail(ErrorCode::SchemaTooNew, "journal newer than this build");
    int v = from;
    while (v < target) {
        const auto it = steps_.find(v);
        if (it == steps_.end()) return R::fail(ErrorCode::Unsupported, "no migration from version " + std::to_string(v));
        if (auto s = it->second(tx); !s.ok()) return R(s.error());
        ++v;
    }
    return v;
}

}  // namespace ef::core::recovery
