#pragma once
// REC-05 platform: gercek journal dizinleriyle kurtarma kapsamlarini kurar ve calistirir.

#include <memory>
#include <string>

#include "core/recovery/service.h"

namespace ef::platform {

struct RecoveryHost {
    std::unique_ptr<core::recovery::JournalFs> user_fs;
    std::unique_ptr<core::recovery::JournalStore> user_store;
    std::unique_ptr<core::recovery::TransactionEngine> user_engine;
    // WIN-09 (ADR-019): baslangic girdisi degisiklikleri ayri kapsam; Performans Kapali bunlari geri almaz.
    std::unique_ptr<core::recovery::JournalFs> startup_fs;
    std::unique_ptr<core::recovery::JournalStore> startup_store;
    std::unique_ptr<core::recovery::TransactionEngine> startup_engine;
    // CHK: Oyun Hazirligi "Duzelt" islemleri (kalici duzeltme; Performans Kapali geri almaz, Islem Gecmisi'nde gorunur).
    std::unique_ptr<core::recovery::JournalFs> fixes_fs;
    std::unique_ptr<core::recovery::JournalStore> fixes_store;
    std::unique_ptr<core::recovery::TransactionEngine> fixes_engine;
    // Makine journal'i ve makine "fixes" journal'i (HAGS duzeltmesi; broker yazar, burada salt okuma). Dizinleri
    // broker ilk yazimda olusturur: acilista yoksa refresh_machine_scopes() sonradan acar (Islem Gecmisi yeniden
    // baslatma beklemeden gorur). Yazma/geri alma yalniz yukseltilmis broker'da.
    core::recovery::LazyScope machine, machine_fixes;
    // Tum kapsamlar: Islem Gecmisi, kaldirma (run_uninstall) ve --recovery rolu (makine kilidiyle).
    std::vector<core::recovery::JournalScope> scopes() const;
    // Yukseltilmemis UI acilis uzlastirmasi: yalniz kullanici kapsamlari. Makine kayitlari sessizce ele alinmaz;
    // UI yarim kalmis makine kaydini Evet/Hayir ile sorar ve broker'da uzlastirir (M2).
    std::vector<core::recovery::JournalScope> user_scopes() const;
    // Acilmamis makine kapsamlarini (dizin artik varsa) acar; acik olanlara dokunmaz. Salt okuma; dizin olusturmaz.
    // Donus: yeni bir kapsam acildi mi.
    bool refresh_machine_scopes();

    const core::recovery::AdapterRegistry* adapters = nullptr;
    std::string id;
};

// Kullanici journal'i her zaman; makine journal'i varsa (okuma icin yukseltme gerekmez).
std::unique_ptr<RecoveryHost> open_recovery_host(const core::recovery::AdapterRegistry& adapters);

}  // namespace ef::platform
