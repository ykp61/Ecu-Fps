#pragma once
// REC-03: cokme sonrasi uzlastirma. Sonraki acilista journal'daki tamamlanmamis islemler GERCEK hedef
// okunarak geri alinir; bozuk/yabanci/yeni sema kayitlari silinmez ve basari sayilmaz.

#include <string>
#include <vector>

#include "core/recovery/engine.h"
#include "core/recovery/journal.h"

namespace ef::core::recovery {

struct ReconcileReport {
    std::vector<std::string> restored;          // tamamlanmamis -> geri alindi
    std::vector<std::string> recovery_required; // geri alma dogrulanamadi / dis degisiklik
    std::vector<std::string> active;            // Active: performans acik kalmis (UI sorar)
    std::vector<std::string> corrupt;           // bozuk kayit: dokunulmadi, kullaniciya gosterilir
    std::vector<std::string> foreign;           // baska makine: uygulanmadi
    std::vector<std::string> newer;             // bilinmeyen yeni sema: yazma durduruldu
    bool clean() const noexcept {
        return recovery_required.empty() && corrupt.empty() && newer.empty();
    }
    bool writes_blocked() const noexcept { return !newer.empty(); }
};

ReconcileReport reconcile(JournalStore& store, TransactionEngine& engine);

}  // namespace ef::core::recovery
