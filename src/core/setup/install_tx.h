#pragma once
// ADR-033 / spec bolum 4 ve 9: islemsel kurulum ve program kaldirma motoru (saf). Her adim: kalici Started kaydi
// (yazilamazsa hicbir sey uygulanmaz) -> uygula (+ platformda geri okuma) -> Done. Hata: Started kaydi olan her adim
// (hatali adimin kismi etkisi dahil) ters sirayla geri alinir; her geri alma Undone / UndoFailed olarak yazilir.
// Isletim sistemi islemleri SetupOps arkasindadir; birim testleri yalniz sahte kullanir.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/setup/arp.h"

namespace ef::core::setup {

struct ServiceConfig {
    std::wstring image_path;
    bool auto_start_delayed = true;
    friend bool operator==(const ServiceConfig&, const ServiceConfig&) = default;
};

struct PriorState {
    bool dir_existed = false, exe_existed = false, shortcut_existed = false, service_existed = false, service_running = false,
         updates_existed = false;
    std::optional<std::wstring> shortcut_target;
    // Ortak (Public) masaustu kisayolu. 0.3.0 journal'larinda yoktur: kod cozmede eksikse "yoktu" sayilir.
    bool desktop_existed = false;
    std::optional<std::wstring> desktop_target;
    std::optional<ServiceConfig> service_config;
    std::optional<ArpValues> arp;
    friend bool operator==(const PriorState&, const PriorState&) = default;
};

// Ortak masaustu "Ecu Fps.lnk" kararlari (saf; platform ve sahte ayni kurali kullanir). present: ayni adli dosya var;
// current: okunabilen hedefi (okunamazsa yok). Kaldirma yalniz bizimkini (hedef = kurulu EXE) siler; ayni adli yabanci veya
// okunamayan kullanici dosyasina dokunmaz. Geri alma: onceki yoksa yerindekini siler (Probe'dan sonra yazildi); onceki hedef
// zaten yerindeyse yeniden yazmaz; onceki hedef okunamamissa ve yerindeki bizimki degilse dokunmaz, bizimkiyse Fail.
enum class LinkAction { Nothing, Delete, Write, Fail };
LinkAction desktop_remove_action(bool present, const std::optional<std::wstring>& current, const std::filesystem::path& exe);
LinkAction desktop_restore_action(const PriorState& p, bool present, const std::optional<std::wstring>& current,
                                  const std::filesystem::path& exe);

enum class SetupStep {
    Probe, CreateDir, CopyExe, Shortcut, ServiceCreate, ServiceStart, UpdatesDir, Arp, Commit,
    /*uninstall*/ ServiceStop, ArpRemove, ShortcutRemove, ServiceDelete, MachineDataRemove, FilesRemove,
    // Sona eklendi (journal adlari sabit): ortak masaustu kisayolu (kurulum: Shortcut'tan sonra; kaldirma:
    // ShortcutRemove'dan sonra; onarim yeniden yazar).
    DesktopShortcut, DesktopShortcutRemove
};
enum class StepRecord { Started, Done, Undone, UndoFailed, Committed };

std::string_view to_string(SetupStep s) noexcept;
std::string_view to_string(StepRecord r) noexcept;

struct JournalEntry {
    std::uint64_t seq = 0;
    std::string tx_id;
    SetupStep step{};
    StepRecord rec{};
    std::string detail;
    std::string prior_json;  // yalniz Probe/Done kaydinda: encode_prior(PriorState)
    friend bool operator==(const JournalEntry&, const JournalEntry&) = default;
};

// Satir basina bir JSON nesnesi (satir sonu icermez).
std::string encode_entry(const JournalEntry& e);
contracts::Result<JournalEntry> decode_entry(std::string_view line);
std::string encode_prior(const PriorState& p);
contracts::Result<PriorState> decode_prior(std::string_view json);

class SetupJournal {
public:
    virtual ~SetupJournal() = default;
    virtual contracts::Status append(const JournalEntry& e) = 0;  // donmeden once kalici (flush)
    virtual contracts::Result<std::vector<JournalEntry>> read() = 0;
    virtual contracts::Status clear() = 0;
};

class SetupOps {
public:
    virtual ~SetupOps() = default;
    virtual contracts::Result<PriorState> probe() = 0;
    virtual contracts::Status create_install_dir() = 0;  // yonetici-yalniz DACL + geri okuma; mevcut klasorde de yeniden uygular
    virtual contracts::Status remove_install_dir_if_empty() = 0;
    virtual contracts::Status stage_and_swap_exe() = 0;     // kendi goruntusu -> exe.new, SHA-256 dogrula, run_swap
    // recover_swap(false); onceki EXE yoksa (p.exe_existed=false, yedek yok) yalniz yeni kopya ve hazirlik dosyasi silinir.
    virtual contracts::Status restore_previous_exe(const PriorState& p) = 0;
    virtual contracts::Status commit_exe() = 0;             // recover_swap(true)
    virtual contracts::Status write_shortcut() = 0;
    virtual contracts::Status restore_shortcut(const PriorState& p) = 0;  // onceki yoksa siler; varsa onceki hedefle yazar
    virtual contracts::Status remove_shortcut() = 0;
    // Ortak masaustu "Ecu Fps.lnk" (FOLDERID_PublicDesktop): yaz + hedefi geri oku; geri alma Baslat menusu ile ayni.
    virtual contracts::Status write_desktop_shortcut() = 0;
    virtual contracts::Status restore_desktop_shortcut(const PriorState& p) = 0;  // onceki yoksa siler; varsa onceki hedef
    virtual contracts::Status remove_desktop_shortcut() = 0;
    virtual contracts::Status write_service() = 0;
    virtual contracts::Status restore_service(const PriorState& p) = 0;   // onceki yoksa siler; varsa onceki yapilandirma
    virtual contracts::Status delete_service() = 0;
    virtual contracts::Status start_service() = 0;
    virtual contracts::Status stop_service() = 0;
    virtual contracts::Status create_updates_dir() = 0;
    virtual contracts::Status remove_updates_dir() = 0;
    virtual contracts::Status write_arp(const ArpValues& v) = 0;
    virtual contracts::Status restore_arp(const PriorState& p) = 0;       // onceki yoksa anahtar silinir
    virtual contracts::Status remove_arp() = 0;
    virtual contracts::Result<bool> machine_journals_clean() = 0;  // makine perf/fixes journal'larinda geri alinmamis kayit yok
    virtual contracts::Status remove_machine_data() = 0;           // ProgramData\EcuFps (updates + setup journal dahil)
    // Dosyalari siler; calisan EXE ve klasor yeniden baslatmada silinmek uzere isaretlenir. Deger: reboot_pending.
    virtual contracts::Result<bool> remove_program_files() = 0;
};

struct TxResult {
    bool ok = false;
    std::optional<SetupStep> failed_at;
    bool rollback_clean = true;
    std::vector<SetupStep> undo_failed;
    bool reboot_pending = false;
    std::vector<SetupStep> done;
    bool machine_data_kept = false;  // kaldirmada makine journal'lari temiz degildi; veri korundu
};

// fail_after: test icin "guc kaybi" — o adimin Done kaydindan sonra geri alma YAPMADAN durur (Commit icin: Committed
// yazildiktan sonra, journal temizlenmeden).
TxResult run_install(SetupOps& ops, SetupJournal& journal, const ArpValues& arp, std::string tx_id,
                     std::optional<SetupStep> fail_after = {});
// P9/B4: onarim ARP tazelemesi. layout + running_version doluysa ve mevcut ARP kaydi bizimse (plan_display_version_refresh),
// son adim olarak journal'li Arp adimi yalniz DisplayVersion'u calisan surume ceker (geri alma: onceki ARP). Guncelleme
// sonrasi hizmet durmus kaldiginda (0.8.x vakasi) hizmet acilisindaki tazeleme hic calismadigi icin bu yol gereklidir.
struct RepairOptions {
    const InstallLayout* layout = nullptr;
    std::wstring running_version;
};
// ServiceCreate, ServiceStart, UpdatesDir, DesktopShortcut [, Arp]
TxResult run_repair(SetupOps& ops, SetupJournal& journal, std::string tx_id, const RepairOptions& opt = {});
TxResult run_uninstall_program(SetupOps& ops, SetupJournal& journal, std::string tx_id,
                               std::optional<SetupStep> fail_after = {});
// Son islem Committed degilse: Probe kaydindaki onceki durumla Started/Done adimlari ters sirayla geri alinir.
// Geri donulmez adim (MachineDataRemove/FilesRemove) baslamissa geri alma yapilmaz; journal korunur.
TxResult recover_interrupted(SetupOps& ops, SetupJournal& journal);

}  // namespace ef::core::setup
