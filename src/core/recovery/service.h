#pragma once
// REC-05: kurtarma servisi. Acilista (ve --recovery rolunde) kullanici + makine journal'larini
// uzlastirir; "Kapali" ve tam cikista tum aktif degisiklikleri geri alir. Ag/lisans gerektirmez.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/recovery/reconcile.h"

namespace ef::core::recovery {

struct JournalScope {
    std::string name;  // "user" / "machine"
    JournalStore* store = nullptr;
    TransactionEngine* engine = nullptr;
};

struct RecoverySummary {
    std::map<std::string, ReconcileReport> scopes;
    bool writes_blocked = false;  // bilinmeyen yeni sema: yeni degisiklik yasak
    bool needs_user = false;      // catisma/bozuk kayit: kullaniciya gosterilmeli
    std::size_t restored = 0, active = 0;
};

RecoverySummary run_startup_recovery(const std::vector<JournalScope>& scopes);

// --recovery rolu: makine kapsamlarina ("machine", "machine_*") yalniz makine islemleri kilidi tutulurken dokunulur
// (broker ile es zamanli uzlastirma yok). machine_allowed=false -> yalniz kullanici kapsamlari.
bool is_machine_scope(std::string_view name) noexcept;
std::vector<JournalScope> scopes_for_role(const std::vector<JournalScope>& all, bool machine_allowed);

// Dizini sonradan olusabilen kapsam (ornek: makine "fixes" journal'i broker ilk yazimda olusturur). ensure() acik
// degilse make_fs ile acmayi dener (nullptr = dizin yok; kapsam kapali kalir, sonraki cagri yeniden dener).
// Acik kapsam yeniden olusturulmaz. Donus: bu cagrida yeni acildi mi.
struct LazyScope {
    std::unique_ptr<JournalFs> fs;
    std::unique_ptr<JournalStore> store;
    std::unique_ptr<TransactionEngine> engine;
    bool ensure(const std::function<std::unique_ptr<JournalFs>()>& make_fs, const std::string& machine_id, const AdapterRegistry& adapters);
};

// Tum Active islemleri geri alir (Performans Kapali / tam cikis). Geri alinamayanlar listelenir.
struct RestoreAllResult {
    std::size_t restored = 0;
    std::vector<std::string> failed;  // RecoveryRequired kalanlar
};
RestoreAllResult restore_all_active(const std::vector<JournalScope>& scopes);

// ---- Sema gocu ----
// Eski surum journal belgesini (zarfin "tx" kismi) guncel surume tasir. Kayitli adimlar sirayla
// uygulanir; bilinmeyen eski surum veya gelecek surum hata. (v1 tek surum; cerceve gelecege hazir.)
using Migration = std::function<contracts::Status(json::Value& tx)>;
class Migrator {
public:
    void add(int from_version, Migration m) { steps_[from_version] = std::move(m); }
    contracts::Result<int> migrate(json::Value& tx, int from_version, int target_version) const;

private:
    std::map<int, Migration> steps_;
};

}  // namespace ef::core::recovery
