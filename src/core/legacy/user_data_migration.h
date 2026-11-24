#pragma once
// R-F1: marka degisikligi sonrasi KULLANICI verisi gocu (eski %LOCALAPPDATA%\<eski ad> -> yeni veri klasoru).
// Yeni uygulamanin ilk acilisinda, journal deposu / adaptorler / tercih okuyuculari yeni klasoru acmadan ONCE bir kez
// calisir. Kurallar (ADR-042, docs/04 "Veri klasoru gocu"):
//  - Eski klasore ASLA yazilmaz: silme, tasima, isaret dosyasi yok (eski surume donus calismaya devam eder).
//  - KOPYALA, tasima: dosyalar once "<yeni>.migrating" hazirlik klasorune kopyalanir, her biri boyut + SHA-256 ile
//    kaynakla karsilastirilir; hepsi dogruysa hazirlik klasoru tek adimda yeni klasor olur (yoksa) ya da bos/yarim
//    kalan yeni klasore uzerine yazmadan birlestirilir. Basari isareti (migration.json) en son yazilir.
//  - Herhangi bir hata: hazirlik klasoru silinir, eski veri dokunulmadan kalir, yeni klasorde kucuk hata kaydi
//    (migration-failed.json) birakilir; sonraki acilis yeniden dener. Eski journal'da hala etkin / kurtarma bekleyen /
//    okunamayan kayit varsa sonuc "legacy_pending" olur: Performans karti temiz "Kapali" gostermez (RecoveryNeeded +
//    durust bildirim).
//  - Journal icerigi yeniden yazilmaz (bayt bayt kopya); sema/surum alanlari degismez.
// Saf ve platformdan bagimsiz: dosya islemleri MigrationFs uzerinden (testte MemoryMigrationFs, uretimde Win32).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::legacy {

// ---- Dosya sistemi soyutlamasi (yollar UTF-8, ayirici '\\'; testte bellek ici) ----
enum class FsCode { Ok, NotFound, Exists, Busy, Denied, DiskFull, Io };
std::string_view to_string(FsCode c) noexcept;

struct FsStatus {
    FsCode code = FsCode::Ok;
    std::string detail;
    bool ok() const noexcept { return code == FsCode::Ok; }
    static FsStatus fail(FsCode c, std::string d) { return {c, std::move(d)}; }
};

struct FileInfo {
    std::string rel;          // kok altinda goreli yol, ayirici '/'
    std::uint64_t size = 0;
    bool read_only = false;
};

struct FileDigest {
    std::uint64_t size = 0;
    std::string sha256;  // kucuk harf hex
};

class MigrationFs {
public:
    virtual ~MigrationFs() = default;
    enum class Kind { Missing, Dir, File, Other };
    virtual Kind kind(const std::string& path) = 0;
    // dir altindaki tum duzenli dosyalar (ozyineli). Yeniden ayristirma noktasi (junction/symlink) izlenmez; bulunursa
    // Denied doner (baska bir agaci kopyalamak yok).
    virtual FsStatus list_files(const std::string& dir, std::vector<FileInfo>& out) = 0;
    virtual FsStatus hash_file(const std::string& path, FileDigest& out) = 0;
    // to yoksa olusturulur (ust klasorler dahil), varsa Exists. Kalici yazim (flush). Hedefin salt okunur ozniteligi
    // kopyalanmaz (hazirlik klasoru bizim; geri alinabilir kalmali).
    virtual FsStatus copy_file(const std::string& from, const std::string& to) = 0;
    virtual FsStatus read_file(const std::string& path, std::size_t max_bytes, std::string& out) = 0;
    // Atomik yaz (gecici + yeniden adlandir); ust klasorler olusturulur.
    virtual FsStatus write_file(const std::string& path, const std::string& bytes) = 0;
    virtual FsStatus make_dirs(const std::string& path) = 0;
    virtual FsStatus rename_dir(const std::string& from, const std::string& to) = 0;  // to varsa Exists
    virtual FsStatus move_file(const std::string& from, const std::string& to) = 0;   // to varsa Exists
    virtual FsStatus remove_file(const std::string& path) = 0;                        // yoksa Ok
    virtual FsStatus remove_tree(const std::string& path) = 0;                        // yoksa Ok
    // path'in bulundugu birimde bos alan; bilinmiyorsa nullopt (on kontrol atlanir, gercek yazim hatasi yine yakalanir).
    virtual std::optional<std::uint64_t> free_bytes(const std::string&) { return std::nullopt; }
};

std::string join_path(std::string_view dir, std::string_view rel);  // '/' -> '\\'

// ---- Goc ----
inline constexpr int kMigrationSchema = 1;
inline constexpr std::string_view kMarkerFile = "migration.json";
inline constexpr std::string_view kFailedFile = "migration-failed.json";
inline constexpr std::string_view kStagingSentinel = ".migration-staging";
inline constexpr std::string_view kStagingSuffix = ".migrating";

struct MigrationPaths {
    std::string old_dir;  // eski kullanici veri klasoru (yalniz okunur)
    std::string new_dir;  // yeni kullanici veri klasoru; hazirlik = new_dir + kStagingSuffix
};

struct MigrationEnv {
    std::int64_t now_unix_ms = 0;  // enjekte saat (isaret icerigi testte sabit)
    std::string app_version;       // "0.9.6"
    std::string to_name;           // yeni klasor adi (isarete yazilir)
    std::string from_name;         // eski klasor adi (isarete yazilir; tam yol / kullanici adi YAZILMAZ)
    std::string machine_id;        // eski journal'in durum taramasi icin (baska makine kaydi sayilmaz)
    bool old_app_running = false;  // eski surum su an acik: kopya tutarsiz olabilir -> bu acilista denenmez
    int busy_retries = 4;          // paylasim/kilit ihlalinde dosya basina ek deneme
    std::function<void(int attempt)> sleep;  // denemeler arasi bekleme (testte bos)
};

enum class MigrationOutcome {
    AlreadyMigrated,  // yeni klasorde isaret var: hicbir sey yapilmadi
    NoLegacyData,     // eski klasor yok/bos: yalniz "no_legacy_data" isareti
    NewDirInUse,      // yeni klasor zaten kullanici verisi tasiyor (isaretsiz): dokunulmadi
    Migrated,         // kopyalandi + dogrulandi + isaret yazildi
    Failed,           // geri alindi; eski veri dokunulmadi; hata kaydi yazildi (mumkunse)
    Skipped,          // test kipi / klasor cozulemedi: hic calismadi (platform kancasi)
};
std::string_view to_string(MigrationOutcome o) noexcept;

// Eski journal'in (journal, journal\startup, journal\fixes) salt okuma durum ozeti.
struct LegacyJournalState {
    bool scanned = false;
    std::size_t transactions = 0;  // bu makineye ait gecerli kayit
    std::size_t unresolved = 0;    // terminal olmayan (Active/Applying/...) veya RecoveryRequired
    std::size_t corrupt = 0;       // okunamayan / bozuk / bilinmeyen yeni sema
    bool needs_attention() const noexcept { return unresolved > 0 || corrupt > 0; }
};

struct MigrationReport {
    MigrationOutcome outcome = MigrationOutcome::Skipped;
    std::string stage;   // hata asamasi: "preflight", "copy", "verify", "commit", ...
    std::string error;   // kisa, yol icermeyen (goreli ad) aciklama
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    std::size_t skipped_existing = 0;  // birlestirmede yeni klasorde zaten farkli icerikle olan (journal disi) dosyalar
    bool merged = false;               // var olan yeni klasore birlestirildi (yeniden adlandirma yerine)
    bool marker_written = false;
    std::string marker;                // yazilan isaret icerigi (test/tanilama)
    LegacyJournalState legacy;
    // Eski MAKINE journal'i (%ProgramData%\<eski ad>, yonetici adimlari). Kullanici sureci yalniz OKUR; gocu yukseltilmis
    // yardimci gerektirir (ayri gorev). Bekleyen kayit varsa yeni surumun makine journal'inda gorunmez -> durust bildirim.
    LegacyJournalState machine_legacy;
    // Eski veride geri alinmamis degisiklik var ve yeni klasore aktarilamadi: UI durust kurtarma durumunu gostermeli.
    bool legacy_pending() const noexcept {
        return ((outcome == MigrationOutcome::Failed || outcome == MigrationOutcome::NewDirInUse) && legacy.needs_attention()) ||
               machine_legacy.needs_attention();
    }
};

// Kopyalanmayan eski klasor alt dizinleri: yeniden indirilen buyuk onbellekler (surucu paketi, kurulum dosyasi).
// Kullanici durumu / geri alma kaydi icermezler (docs/04 veri klasoru gocu).
bool excluded_from_migration(std::string_view rel) noexcept;

MigrationReport migrate_user_data(MigrationFs& fs, const MigrationPaths& paths, const MigrationEnv& env);

// Eski journal'i (salt okuma) tarar. fs uzerinde yazma yapmaz.
LegacyJournalState scan_legacy_journal(MigrationFs& fs, const std::string& data_dir, const std::string& machine_id);

// Kisa tek satir tanilama (yol icermez): logs/migration.log.
std::string migration_log_line(const MigrationReport& r);

// ---- Test icin bellek ici dosya sistemi + hata enjeksiyonu ----
class MemoryMigrationFs final : public MigrationFs {
public:
    struct File {
        std::string bytes;
        bool read_only = false;
    };
    Kind kind(const std::string& path) override;
    FsStatus list_files(const std::string& dir, std::vector<FileInfo>& out) override;
    FsStatus hash_file(const std::string& path, FileDigest& out) override;
    FsStatus copy_file(const std::string& from, const std::string& to) override;
    FsStatus read_file(const std::string& path, std::size_t max_bytes, std::string& out) override;
    FsStatus write_file(const std::string& path, const std::string& bytes) override;
    FsStatus make_dirs(const std::string& path) override;
    FsStatus rename_dir(const std::string& from, const std::string& to) override;
    FsStatus move_file(const std::string& from, const std::string& to) override;
    FsStatus remove_file(const std::string& path) override;
    FsStatus remove_tree(const std::string& path) override;
    std::optional<std::uint64_t> free_bytes(const std::string&) override { return free; }

    void put(const std::string& path, std::string bytes, bool read_only = false);  // ust klasorleri de ekler
    std::map<std::string, File> files;  // tam yol -> icerik
    std::set<std::string> dirs;         // tam yol
    std::set<std::string> reparse;      // junction gibi izlenmeyen klasorler

    // Hata enjeksiyonu
    std::string corrupt_copy_of;       // bu goreli ad ile biten kaynagin KOPYASINDA bir bayt bozulur
    std::string fail_copy_of;          // bu ad ile biten kaynagin kopyasi Io ile basarisiz
    bool fail_rename = false;
    std::string fail_write_of;         // bu ad ile biten yazim Io
    std::optional<std::uint64_t> capacity;  // toplam yazilabilir bayt (asilirsa DiskFull)
    std::optional<std::uint64_t> free;      // free_bytes() yaniti
    std::map<std::string, int> busy;   // ad sonu -> kalan Busy yanit sayisi (-1 = surekli kilitli)
    std::uint64_t written = 0;         // toplam yazilan bayt
    std::vector<std::string> mutations;  // degistirici islem gunlugu ("op path")
    // Kopya sirasinda kaynagi degistir (eski surum ayni anda yaziyor): ad sonu -> yeni icerik (tek sefer).
    std::map<std::string, std::string> mutate_source_during_copy;

private:
    FsStatus check_busy(const std::string& path);
    FsStatus account(std::uint64_t n, const std::string& path);
    void note(const char* op, const std::string& path) { mutations.push_back(std::string(op) + " " + path); }
};

}  // namespace ef::core::legacy
