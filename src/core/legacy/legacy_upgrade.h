#pragma once
// R-F2 (ADR-043): eski adli kurulumdan yeni markaya gozetimsiz yukseltme (saf durum makinesi). Isletim sistemi
// islemleri UpgradeSystem (SCM, kayit defteri, kisayol, ACL, yeniden baslatmada silme) ve MigrationFs arkasindadir;
// birim testleri yalniz sahte kullanir. Platform uygulamasi (WinUpgradeSystem) ve service_host/app kancalari ayri
// gorevdir (gercek Windows yukseltme turu).
//
// Iki rol ayni fonksiyonu calistirir; ilerleme kalici kayitta (<yeni ProgramData>\setup\legacy-upgrade.json):
//  LegacyHost : yeni ikili ESKI yoldan, ESKI hizmet adiyla (--service, SYSTEM). Sira:
//     Begun (kayit yazilamazsa HICBIR SEY yapilmaz, eski EXE geri gelir)
//     -> NewFiles  (yeni kurulum klasoru + ACL, kendi goruntusu -> yeni EXE, SHA-256 dogrulama)
//     -> Journals  (makine journal'i gocu, korumali ACL)
//     -> NewService(yeni ad ile hizmet olustur/baslat, saglik: Running + kendi pipe'i yanit verir)
//     -> Registered(yeni ARP, yeni Baslat kisayolu; masaustu yalniz eskisi bizimse)
//     -> Healthy   (TEK silme kapisi: bu kayit kalici yazilmadan eski hicbir sey silinmez) -> cikis 0
//     Herhangi bir hata Healthy'den once: bu denemenin olusturdugu yeni artefaktlar ters sirayla geri alinir, eski
//     EXE (exe.old) geri getirilir, cikis 5 (SCM eski ikiliyi baslatir). Geri getirilemezse cikis 6 (yeniden dener).
//  NewService : yeni hizmet ilk acilisinda. Kayit Healthy ise eski artefaktlari temizler: eski hizmet durdur + sil,
//     eski ARP (InstallLocation eski klasorse), eski kisayollar (yalniz hedefi eski EXE olan .lnk), eski kurulum
//     klasorunde yalniz bizim adlarimiz (yeniden deneme, sonra yeniden baslatmada silme), bos klasor. Eski
//     ProgramData / LocalAppData yedek olarak KALIR. Eksik kalan adim sonraki acilista yeniden denenir (idempotent).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/legacy/machine_journal_migration.h"
#include "core/legacy/user_data_migration.h"
#include "core/setup/layout.h"

namespace ef::core::legacy {

// UTF-8 yollar, ayirici '\\'.
struct UpgradePaths {
    std::string self_exe;  // calisan goruntu (LegacyHost'ta == old_exe)
    std::string old_dir, old_exe, old_machine_root, old_start_lnk, old_desktop_lnk;
    std::vector<std::string> old_owned_files;  // legacy_owned_files (EXE + degisim kardesleri)
    std::string new_dir, new_exe, new_machine_root, new_start_lnk, new_desktop_lnk;
    std::string record_file() const { return join_path(new_machine_root, "setup/legacy-upgrade.json"); }
};

struct UpgradeNames {
    std::wstring old_service, new_service, old_arp_key, new_arp_key;
};

enum class SvcState { Missing, Stopped, Running, Pending };

class UpgradeSystem : public MachineAcl {
public:
    virtual SvcState service_state(std::wstring_view name) = 0;
    virtual std::optional<std::string> service_exe(std::wstring_view name) = 0;  // ImagePath'teki EXE yolu
    // "\"<exe>\" --service", gecikmeli otomatik, required_failure_config; geri okunur.
    virtual FsStatus create_service(std::wstring_view name, const std::string& exe) = 0;
    virtual FsStatus start_service(std::wstring_view name) = 0;
    virtual FsStatus stop_service(std::wstring_view name) = 0;  // sinirli bekleme; hala calisiyorsa Busy
    virtual FsStatus delete_service(std::wstring_view name) = 0;
    virtual bool service_healthy(std::wstring_view name, std::int64_t timeout_ms) = 0;  // Running + pipe Hello yaniti
    virtual std::optional<std::string> arp_install_location(std::wstring_view key) = 0;  // nullopt: anahtar yok
    virtual FsStatus write_arp(std::wstring_view key) = 0;  // yeni yerlesim degerleri (make_arp_values); geri okunur
    virtual FsStatus delete_arp(std::wstring_view key) = 0;
    virtual bool shortcut_present(const std::string& lnk) = 0;
    virtual std::optional<std::string> shortcut_target(const std::string& lnk) = 0;  // okunamazsa nullopt
    virtual FsStatus write_shortcut(const std::string& lnk, const std::string& target) = 0;  // geri okunur
    virtual FsStatus delete_shortcut(const std::string& lnk) = 0;
    virtual FsStatus apply_install_acl(const std::string& dir) = 0;  // yonetici-yalniz DACL (WinSetupOps ile ayni)
    virtual FsStatus schedule_delete_on_reboot(const std::string& path) = 0;  // MoveFileEx(DELAY_UNTIL_REBOOT)
    virtual FsStatus restore_legacy_exe() = 0;  // eski degisim yollarinda recover_swap(false)
};

enum class UpgradePhase { None, Begun, NewFiles, Journals, NewService, Registered, Healthy, Done, RollingBack, RolledBack };
std::string_view to_string(UpgradePhase p) noexcept;

struct UpgradeRecord {
    UpgradePhase phase = UpgradePhase::None;
    int attempt = 0;
    int rollback_tries = 0;  // bu denemede geri alma kac kez denendi (artik kalirsa ust sinirdan sonra eski EXE yine gelir)
    std::string version;
    // Bu denemenin OLUSTURDUGU (onceden olmayan) yeni artefaktlar: geri alma yalniz bunlari kaldirir.
    bool created_dir = false, created_exe = false, created_service = false, created_arp = false, created_start_lnk = false,
         created_desktop_lnk = false, migrated_journals = false;
    bool reboot_pending = false;
    std::string stage, error;
    std::int64_t updated_unix_ms = 0;
    friend bool operator==(const UpgradeRecord&, const UpgradeRecord&) = default;
};
std::string encode_record(const UpgradeRecord& r);
std::optional<UpgradeRecord> decode_record(std::string_view json);

enum class UpgradeRole { LegacyHost, NewService };

struct UpgradeEnv {
    UpgradeRole role = UpgradeRole::LegacyHost;
    std::string version;  // calisan yeni surum
    std::int64_t now_unix_ms = 0;
    std::string machine_id;
    int busy_retries = 4;
    std::function<void(int attempt)> sleep;
    std::int64_t health_timeout_ms = 60000;
};

enum class UpgradeOutcome {
    NotApplicable,   // LegacyHost: eski yol degil / NewService: bekleyen yukseltme yok
    HandedOff,       // LegacyHost: yeni hizmet saglikli, kayit Healthy; eski hizmet durur
    Completed,       // NewService: eski artefaktlar temizlendi (yeniden baslatmada silinecekler olabilir)
    CleanupPending,  // NewService: bir adim tamamlanamadi (eski hizmet durmadi vb.); sonraki acilista yeniden
    RolledBack,      // LegacyHost: yeni artefaktlar geri alindi, eski EXE geri geldi
    RollbackPending, // LegacyHost: eski EXE geri getirilemedi (kilitli); SCM yeniden baslatinca tekrar denenir
    WaitingForHost,  // NewService: yukseltme LegacyHost'ta suruyor (Healthy degil): dokunulmaz
};
std::string_view to_string(UpgradeOutcome o) noexcept;

struct UpgradeResult {
    UpgradeOutcome outcome = UpgradeOutcome::NotApplicable;
    std::uint32_t exit_code = 0;  // LegacyHost: SCM'ye hizmete ozgu kod (legacy_install.h)
    std::string stage, error;
    bool reboot_pending = false;
    std::vector<std::string> scheduled_reboot_delete;
    MachineMigrationReport journals;
    UpgradeRecord record;  // son kalici kayit
};

// Bilinen kok klasorlerden eski + yeni yollar ve adlar (platform ve test ayni turetmeyi kullanir).
UpgradePaths make_upgrade_paths(const setup::KnownRoots& r, const std::filesystem::path& self_exe);
UpgradeNames default_upgrade_names();

UpgradeResult run_legacy_upgrade(MigrationFs& fs, UpgradeSystem& sys, const UpgradePaths& p, const UpgradeNames& n, const UpgradeEnv& env);

// Tek satir tanilama (yol icermez).
std::string upgrade_log_line(const UpgradeResult& r);

}  // namespace ef::core::legacy
