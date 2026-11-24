#pragma once
// R-F2 (ADR-043): eski MAKINE journal'inin (%ProgramData%\<eski ad>\journal: perf koku + fixes, restore_point,
// maintenance, drivers, vendor_drivers alt dizinleri) yeni makine klasorune gocu. Yalniz yukseltilmis baglamda
// (eski yoldan --service olarak calisan yeni ikili) calisir. R-F1 (user_data_migration) ile ayni kural:
//  - Eski klasore ASLA yazilmaz; updates\ (degisim hazirligi) ve setup\ kopyalanmaz (journal\ disinda).
//  - Yeni kok once SYSTEM + Administrators korumali ACL ile hazirlanir (apply_machine_acl); hazirlik klasoru
//    <yeni>\journal.migrating bu ACL'i miras alir. Dosyalar kopyalanir, boyut + SHA-256 kaynakla karsilastirilir,
//    tek adimda <yeni>\journal olur (yoksa) ya da UZERINE YAZMADAN birlestirilir (ayni icerik = zaten tasinmis;
//    farkli icerik = cakisma, hata). Devirden sonra journal klasoru ve her alt klasorun ACL'i geri okunur.
//  - Isaret <yeni>\machine-migration.json journal klasorunun DISINDA (JournalStore *.json'u islem sanmasin) ve dosya
//    listesini (goreli ad, boyut, SHA-256) tasir: geri alma (undo) yalniz hala ayni olan KOPYALARI siler.
//  - Journal baytlari yeniden yazilmaz: Etkin islemler yeni surumde "Kapali" ile ayni baytlardan geri alinir.

#include <string>
#include <vector>

#include "core/legacy/user_data_migration.h"

namespace ef::core::legacy {

inline constexpr std::string_view kMachineMarkerFile = "machine-migration.json";
inline constexpr std::string_view kMachineJournalDir = "journal";

// Korumali makine klasoru ACL'i (platform: kMachineDirSddl + verify_machine_journal_dir; testte kayit).
class MachineAcl {
public:
    virtual ~MachineAcl() = default;
    virtual FsStatus apply_machine_acl(const std::string& dir) = 0;   // yoksa olusturur; varsa korumali DACL yeniden uygulanir
    virtual FsStatus verify_machine_acl(const std::string& dir) = 0;  // sahip SY/BA, kullaniciya yazma yok, reparse yok
};

struct MachineMigrationPaths {
    std::string old_root;  // %ProgramData%\<eski ad> (yalniz okunur)
    std::string new_root;  // %ProgramData%\<yeni ad>
};

struct MachineMigrationReport {
    MigrationOutcome outcome = MigrationOutcome::Skipped;
    std::string stage, error;
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    bool merged = false;
    bool acl_verified = false;
    LegacyJournalState legacy;  // eski makine journal'i durum ozeti (salt okuma)
};

MachineMigrationReport migrate_machine_journals(MigrationFs& fs, MachineAcl& acl, const MachineMigrationPaths& p, const MigrationEnv& env);

// Yukseltme geri alinirken: isarette listelenen ve hala ayni ozetteki kopyalar + isaret silinir; degismis dosya
// (yeni surum yazdi) korunur ve *kept artar. Eski klasore dokunulmaz.
FsStatus undo_machine_migration(MigrationFs& fs, const MachineMigrationPaths& p, std::size_t* kept = nullptr);

// Kullanici sureci (R-F1 bildirimi) icin: yeni kokte gecerli isaret varsa eski makine journal'i aktarilmistir (temiz
// ozet); yoksa eski journal salt okunur taranir (bekleyen kayit -> durust RecoveryNeeded).
LegacyJournalState machine_legacy_after_upgrade(MigrationFs& fs, const MachineMigrationPaths& p, const std::string& machine_id);

}  // namespace ef::core::legacy
