#pragma once
// OPT-02 / OPT-10 (plan v3.5): yonetici kapsamli yurutme yolu. Tek "Evet" -> tek UAC -> broker yonetici
// grubunu (HAGS + NVIDIA DRS global profil) makine journal'i ile uygular.
//
// Degismezler:
//  - Serbest komut/yol yok: yalniz admin_operation_allowed() izin listesindeki islemler (tipli SettingOperation).
//  - Broker tarafinda her islem SEC-03 Dispatcher kontrollerinden (kapsam, yetki, makine kilidi) gecer.
//  - Her yazma once kalici journal baslangic kaydi, sonra uygulama, geri okuma; hata -> ayni adimda geri alma.
//  - Restore (Kapali) yalniz journal'daki ETKIN islemi anlik goruntuye birebir dondurur.
//  - UAC reddi/hata: broker hic baslamaz, hicbir yonetici adimi yazilmaz (run_elevated_request).
// Bu dosya platformdan bagimsizdir; gercek pipe/UAC platform/windows/broker_channel.* icindedir.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/drivers/driver_install.h"
#include "core/drivers/vendor_install.h"
#include "core/ipc/protocol.h"
#include "core/recovery/engine.h"
#include "core/maintenance/image_health.h"
#include "core/maintenance/standby_purge.h"
#include "core/recovery/restore_point.h"
#include "core/recovery/reconcile.h"
#include "core/recovery/restore_report.h"
#include "core/roles.h"
#include "core/security/dispatch.h"

namespace ef::core::broker {

// NVIDIA "Shader disk cache maximum size" hedefi (birim MB; NVIDIA Denetim Masasi "10 GB" secenegi).
// NVAPI basligindaki varsayilan 0x4000 = 16384 -> MB ile 16 GB; bu nedenle 10240 hedefi guncel surucude
// "artirma" degil, KUCULTME olurdu. Yalniz mevcut sinir bundan KUCUKSE yukseltilir; daha buyuk/sinirsiz
// deger asla dusurulmez (G07). Birim resmi DRS belgesiyle tekrar dogrulanana kadar oge kapali (M3).
inline constexpr std::uint32_t kShaderCacheTargetMb = 10240;
// M3: birim (MB?) ve 0 degerinin anlami resmi NVIDIA belgesiyle DOGRULANMADI. Dogrulanana kadar oge plana
// girmez ve broker yeni Apply/Fix istegini reddeder (kod korunur; journal'da kalmis eski kayit yine geri alinir).
// Acmadan once: tasks/backlog.json OPT-10 notu (resmi NVAPI/DRS belgesiyle birim + 0 anlami).
inline constexpr bool kShaderCacheItemEnabled = false;

// B6 (ADR-040 m.8, grup plan.nvidia_extra, Gelismis, varsayilan Hayir): kimlik/degerler NVAPI SDK basligiyla dogrulandi
// ama gercek NVIDIA GPU'da urunun Evet/Hayir akisiyla yaz -> geri oku -> Kapali turu YAPILMADI. Bayrak kapaliyken oge
// plana girmez ve broker yeni Apply/Fix istegini reddeder; journal'daki eski kaydin GERI ALINMASI her zaman izinlidir
// (bayrak yalniz yeni yazmayi kapatir). Acma kosulu: artifacts/evidence/R-B6.md (needs: owner + device).
inline constexpr std::uint32_t kThreadedOptimizationTarget = 1;  // OGL_THREAD_CONTROL_ENABLE
inline constexpr std::uint32_t kFxaaOffTarget = 0;               // FXAA_ENABLE_OFF
inline constexpr bool kThreadedOptimizationItemEnabled = false;
inline constexpr bool kFxaaOffItemEnabled = false;
// Test ve ileride acma icin: bayraklar disaridan verilir (urun yolu yukaridaki sabitleri kullanir).
struct DrsExtraFlags {
    bool threaded_optimization = kThreadedOptimizationItemEnabled;
    bool fxaa_off = kFxaaOffItemEnabled;
};

// Izin listesi: HagsSet (Machine, "hags:hwschmode", yalniz "2" = acik), FastStartupSet (Machine,
// "faststartup:hiberboot", yalniz "0" = kapali), PowerSettingSet (Machine, "power:boost:ac" = "1",
// "power:parking:ac" = "100", "power:aspm:ac" = "0") ve VendorProfileValue (Machine,
// "drs:global:<PowerMode|MaxPrerendered|ShaderCacheSize|ThreadedOptimization|FxaaEnable>", sabit hedef deger), BootOptionSet (ADR-040: yalniz
// boot_options.h tablosundaki cift; bayrak kapaliyken yeni yazma yok). Baska her sey reddedilir.
// Geri alma (Restore/UndoFix/Recover) journal'daki kaydi kullanir; bu liste yalniz istegin bicimini sinirlar.
bool admin_operation_allowed(const contracts::SettingOperation& op) noexcept;
// Yeni yazma (Apply/Fix) icin: izin listesi + dogrulanmamis ogeler kapali (kShaderCacheItemEnabled).
bool admin_apply_allowed(const contracts::SettingOperation& op) noexcept;
// Ayni kural, B6 bayraklari disaridan (test). admin_apply_allowed(op) == admin_apply_allowed(op, DrsExtraFlags{}).
bool admin_apply_allowed(const contracts::SettingOperation& op, const DrsExtraFlags& extra) noexcept;
// ADR-040: islem yeniden baslatmadan sonra mi etkin (journal adimi needs_reboot, sonuc pending_reboot). Tablodan karar
// verilir (HagsSet, BootOptionSet, needs_reboot'lu katalog girdisi); istemcinin op.needs_reboot alanina guvenilmez.
bool admin_needs_reboot(const contracts::SettingOperation& op) noexcept;

// M1: yarim kalmis (Prepared/Applying/Applied/Verifying/RollingBack) kayit; broker oturumu basinda uzlastirilir.
bool tx_unreconciled(contracts::TxState s) noexcept;
// Journal listesinden (salt okuma) izin listesindeki islemler, hedef basina tek kayit:
//  journal_active_ops       : Active (+ istenirse yarim kalmis; Kapali bunlari da broker'a gonderir, broker once uzlastirir)
//  journal_unreconciled_ops : yarim kalmis (+ istenirse RecoveryRequired). Acilis sorusu ikisini de kullanir
//                             (dogrulanamamis geri alma sessizce takili kalmaz; Hayir -> sonraki acilista yine sorulur).
std::vector<contracts::SettingOperation> journal_active_ops(const recovery::JournalStore::Listing& l, bool include_unreconciled);
std::vector<contracts::SettingOperation> journal_unreconciled_ops(const recovery::JournalStore::Listing& l, bool include_recovery_required);
// Oyun Hazirligi satir durumu icin (izin listesinden bagimsiz; kullanici ve makine fixes journal'i): hedefler
// Active -> active, RecoveryRequired veya yarim kalmis -> recovery. Restored atlanir. Tekrarsiz eklenir.
void journal_fix_targets(const recovery::JournalStore::Listing& l, std::vector<std::string>& active, std::vector<std::string>& recovery);
// Ayrintili satir durumu (durum metni durust olsun diye ayrilir):
//  failed      : RecoveryRequired (onceki geri alma denendi, dogrulanamadi; dis degisiklik DEGIL)
//  interrupted : yarim kalmis (Prepared/Applying/... ; geri alma henuz denenmedi)
//  conflict    : RecoveryRequired + dis degisiklik (kExternalChange) -> "Mevcut degeri koru" sunulur
void journal_fix_targets(const recovery::JournalStore::Listing& l, std::vector<std::string>& active, std::vector<std::string>& failed,
                         std::vector<std::string>& interrupted, std::vector<std::string>& conflict);
// Makine fixes journal'inda dis degisiklik bekleyen (RecoveryRequired + kExternalChange) izin listesindeki islemler.
std::vector<contracts::SettingOperation> journal_conflict_ops(const recovery::JournalStore::Listing& l);

// Blocked: yeni yazma (Apply/Fix) reddedildi cunku hedefte henuz geri alinmamis kayit var (Active, RecoveryRequired
// veya yarim kalmis; iki makine journal'inda da). Hicbir sey yazilmadi; kullaniciya ozel bildirim gosterilir.
// Kept: "Mevcut degeri koru" -> kayit kapandi, HICBIR ayar yazilmadi (dis degisiklik oldugu gibi kaldi).
// GateBlocked: UAC/broker bekleyisinden sonra ilk yazimdan once guncel oyun/algilama kapisi reddetti; yazilmadi.
enum class StepState { Applied, Restored, NothingToDo, Failed, RecoveryRequired, Rejected, Blocked, Kept, GateBlocked };
std::string_view to_string(StepState s) noexcept;

struct StepResult {
    std::string target;
    StepState state = StepState::Failed;
    bool pending_reboot = false;  // HAGS: etkin durum yeniden baslatmadan sonra degisir
    std::string detail;           // teknik ayrinti (log; kullanici metni degil)
    // Istemci tarafi: istek gonderildi ama gecerli yanit alinamadi -> broker'da ne oldugu BILINMIYOR (Failed ile
    // birlikte; "degismedi" denmez). Sonraki broker oturumu yarim kalmis kaydi uzlastirir.
    bool unknown = false;
};

std::string encode_result(std::string_view request_id, const StepResult& r);

// Journal'da hedefi eslesen, Restored OLMAYAN (Active, RecoveryRequired, yarim kalmis) islemleri en yeniden eskiye
// motorla geri alir (gercek hedef okunur; dis degisiklik ezilmez; geri okumayla dogrulanir). Hic yoksa NothingToDo,
// hepsi Restored ise Restored (HAGS: pending_reboot), aksi halde RecoveryRequired. Broker UndoFix/Restore ve UI'nin
// kullanici kapsamli "Geri al / Yeniden dene" yolu ayni kurali kullanir.
StepResult restore_matching(recovery::JournalStore* store, recovery::TransactionEngine* engine,
                            const std::function<bool(const contracts::SettingOperation&)>& match, std::string_view target);
contracts::Result<StepResult> decode_result(std::string_view payload);
// "Mevcut degeri koru": hedefi eslesen, dis degisiklik bekleyen (RecoveryRequired + kExternalChange) kayitlarin
// catisan adimlari KeptExternal olarak kapatilir; ayar okunmaz/yazilmaz. Catisma yoksa NothingToDo; hepsi kapandiysa
// Kept; aksi halde (baska dogrulanamamis adim kaldi) RecoveryRequired. Broker KeepCurrent ve kullanici kapsami ayni kural.
StepResult keep_matching(recovery::JournalStore* store, const std::function<bool(const contracts::SettingOperation&)>& match,
                         std::string_view target);

// ---- Broker tarafi ----
// Broker oturumu basinda iki makine journal'inin uzlastirma sonucu (REC-03: geri al / RecoveryRequired).
struct SessionReconcile {
    bool ran = false;  // yukseltilmis + makine kilidi yoksa calismaz
    bool deferred = false;  // kapi kapali (oyun/algilama): yarim kayitlar korundu, sonraki oturuma kaldi
    recovery::ReconcileReport perf, fixes;
};

struct ExecutorDeps {
    recovery::JournalStore* perf_store = nullptr;      // Performans makine journal'i (Kapali geri alir)
    recovery::TransactionEngine* perf_engine = nullptr;
    recovery::JournalStore* fixes_store = nullptr;     // Oyun Hazirligi duzeltmeleri (Kapali geri almaz)
    recovery::TransactionEngine* fixes_engine = nullptr;
    std::function<std::string()> new_tx_id;            // 32 hex
    std::function<std::int64_t()> now_unix_ms;
    security::DispatchContext ctx;                     // elevated + machine_lock_held broker'da dogrulanir
    std::string journal_error;                         // CORE-01: makine journal dizini guvenlik reddi tanilamasi
    // Broker'in kendi bagimsiz izni (kendi surec listesi + hedef kullanicinin kitapligi). UI'ya guvenmez.
    recovery::WriteGuard permit;
    // EF-X16: serbest arguman yok; aciklama broker'da sabittir.
    std::function<recovery::RestorePointRun()> create_restore_point;
    // EF-X02: sabit kimlikli bakim isi (kontrol / yerel kaynakla onarim). Serbest arguman yok.
    std::function<maintenance::MaintRun(std::string_view target)> maintenance;
    // Spec H2: Windows Update surucu kurulumu (drivers::run_driver_install; hedef "wuinst:<UpdateID>:<rev>"). Yardimci
    // ogeyi yeniden arar, yalniz tam o surucu guncellemesini kabul eder; firmware ve Windows Update disi kaynak reddedilir.
    std::function<drivers::InstallRun(std::string_view target)> driver_install;
    // ADR-032: imzasi dogrulanan resmi ekran karti paketi (drivers::run_vendor_install; hedef "vdrv:<uretici>:<16 hex>").
    // Yardimci dosyayi yalniz kendi klasorunden alir, makine klasorune kopyalar, imzayi + imzalayani orada yeniden dogrular.
    std::function<drivers::VendorRun(std::string_view target)> vendor_install;
};

class AdminExecutor {
public:
    explicit AdminExecutor(ExecutorDeps deps);
    // M1: oturum basinda (istemci dogrulandiktan sonra, ilk istekten once) cagrilir. Yarim kalmis kayitlar gercek
    // hedef okunarak geri alinir; dogrulanamayan RecoveryRequired kalir. Active kayitlara dokunulmaz.
    SessionReconcile reconcile_on_start();
    StepResult handle(ipc::RequestAction action, const contracts::SettingOperation& op);
    // ServerSession isleyicisi: sonuc JSON'u.
    ipc::ServerSession::Handler handler();

private:
    StepResult apply_in(recovery::JournalStore* store, recovery::TransactionEngine* engine, const contracts::SettingOperation& op);
    StepResult restore_in(recovery::JournalStore* store, recovery::TransactionEngine* engine, const contracts::SettingOperation& op);
    StepResult recover(const contracts::SettingOperation& op);
    StepResult keep_current(const contracts::SettingOperation& op);
    // Bu oturumun basinda reconcile_on_start'in geri aldigi islem kimlikleri: UndoFix/Recover hedefi oturum basinda
    // zaten geri alindiysa "bulunamadi" degil, geri alindi bildirilir.
    bool restored_this_session(recovery::JournalStore* store, const contracts::SettingOperation& op);

    ExecutorDeps d_;
    std::vector<std::string> session_restored_;
    bool reconcile_deferred_ = false;
    security::Dispatcher dispatcher_;
};

// ---- Istemci (UI) tarafi ----
class FrameTransport {
public:
    virtual ~FrameTransport() = default;
    virtual contracts::Status send(const ipc::Frame& f) = 0;
    virtual contracts::Result<ipc::Frame> receive() = 0;
};
// Broker basladiktan sonra cagrilir (gercekte: broker PID'i dogrulanmis pipe istemcisi).
using Connector = std::function<contracts::Result<std::unique_ptr<FrameTransport>>()>;

struct BatchResult {
    ElevationOutcome elevation = ElevationOutcome::Failed;
    std::vector<StepResult> steps;  // istek sirasiyla; yanit alinamayan islem Failed
    std::string detail;
    std::size_t count(StepState s) const;
    bool any_pending_reboot() const;
};

// Oyun Hazirligi / acilis kurtarmasi sonuc bildirimi (katalog anahtari; bos = bildirim yok, yeniden tarama yeterli).
// Fix: chk.notice.fix_denied | fix_blocked | fix_failed | fix_recovery | fix_reboot | unknown.
// UndoFix: chk.notice.undo_denied | undo_done | undo_reboot | undo_failed | nothing | unknown.
// KeepCurrent: chk.notice.keep_denied | keep_done | undo_done (oturum basi uzlastirma geri aldi) | keep_nothing | keep_failed | unknown.
// Recover: recovery.machine_denied | machine_done | machine_active | machine_failed (| chk.notice.unknown).
//   machine_active: kayitlar kapandi ama hedefte hala ETKIN bir degisiklik var (Performans Acik / duzeltme) -> "geri
//   yuklendi" denmez.
// Broker baslatilamadiysa (UAC reddi degil): chk.notice.admin_start_failed.
std::string_view admin_notice_key(ipc::RequestAction action, const BatchResult& b);
// A1.1: Oyunlar sayfasi NVIDIA oyun profili sonucu (katalog anahtari).
std::string_view game_profile_notice_key(bool undo, const BatchResult& b);
std::string_view restore_point_notice_key(const BatchResult& b);
std::string_view maintenance_notice_key(const BatchResult& b);
std::string_view maintenance_outcome_key(const BatchResult& b);  // "repairable" ...; bos = bilinmiyor
// Spec H2: surucu kurulumu sonucu (Guncellemeler sayfasi). key: updates.install.* katalog anahtari. Yalniz gecerli
// yanitta reboot / no_restore_point / hresult doludur (yanit yoksa sonuc iddia edilmez: "unknown").
struct DriverInstallNotice {
    std::string_view key;
    bool warn = true;               // dogrulanmis basari disindaki her sonuc
    bool reboot = false;            // kurulum yeniden baslatma istiyor
    bool no_restore_point = false;  // geri yukleme noktasi olusmadi (kurulum yine de yapildi / denendi)
    bool previous_interrupted = false;
    bool installed = false;         // surucu kuruldu (liste yeniden denetlenir)
    std::int32_t hresult = 0;       // basarisizlikta Windows Update kodu (0 = yok)
};
DriverInstallNotice driver_install_notice(const BatchResult& b);
// ADR-032: resmi paket kurulumu sonucu (updates.local.* / updates.install.* katalog anahtari). version: geri okunan kurulu
// surum (bos = okunamadi). Yanit yoksa sonuc iddia edilmez ("unknown").
struct VendorInstallNotice {
    std::string_view key;
    bool warn = true;
    bool reboot = false;
    bool no_restore_point = false;
    bool previous_interrupted = false;
    bool installed = false;
    std::uint32_t exit_code = 0;
    std::string version;
};
VendorInstallNotice vendor_install_notice(const BatchResult& b);
// Oyun Hazirligi bildirimi uyari renginde mi (RDY-06): dogrulanmis basari disindaki her sonuc (nothing/keep_nothing dahil).
bool admin_notice_warning(std::string_view key);

// Tek UAC: broker baslatilir, el sikisma (nonce + PID), her islem icin bir istek, Bye. UAC reddi -> steps bos.
// Izin listesi disi islem istemcide de gonderilmez (Rejected).
// Toplu istekte islem basina gonderilen eylem: Apply icindeki RestorePointCreate RestorePoint olarak gider (tek UAC, tek oturum).
ipc::RequestAction request_action_for(ipc::RequestAction batch_action, const contracts::SettingOperation& op) noexcept;
BatchResult run_admin_batch(ElevationLauncher& launcher, std::uint32_t self_pid, const std::string& nonce, const Connector& connect,
                            ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops);

// Performans arka ucunun gordugu kanal (testte sahte; uretimde platform BrokerChannel).
class AdminChannel {
public:
    virtual ~AdminChannel() = default;
    virtual BatchResult run(ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops) = 0;
    // Makine Performans journal'indaki ETKIN islemler (salt okuma; yukseltme gerekmez).
    virtual std::vector<contracts::SettingOperation> active_ops() = 0;
    // Makine Performans journal'inda dogrulanamamis geri alma veya yarim kalmis kayit var mi.
    virtual bool recovery_needed() { return false; }
    // Makine Performans journal'inda geri alinmasi dogrulanamamis/yarim kayitlar (Kapali yeniden denemesi bunlari da gonderir).
    virtual std::vector<contracts::SettingOperation> perf_recovery_ops() { return {}; }
    // Makine Performans journal'inda dis degisiklik bekleyen islemler ("Mevcut degeri koru").
    virtual std::vector<contracts::SettingOperation> perf_conflict_ops() { return {}; }
    // Saha 2026-11-15: iki makine journal'inda (scope "machine" = Performans, "machine_fixes" = duzeltmeler) geri alinamamis
    // adimlar (salt okuma; recovery::failed_restore_items). Kurtarma ekrani ve destek paketi "hangi ayar" bilgisini buradan alir.
    virtual std::vector<recovery::FailedRestoreItem> machine_failed_items() { return {}; }
    // Makine "fixes" journal'indaki ETKIN duzeltmeler (Oyun Hazirligi "Geri al").
    virtual std::vector<contracts::SettingOperation> fix_active_ops() { return {}; }
    // Makine "fixes" journal'inda geri alinmasi dogrulanamamis (RecoveryRequired) veya yarim kalmis duzeltmeler
    // (Oyun Hazirligi "Geri al / Yeniden dene"; broker UndoFix ile yeniden dener).
    virtual std::vector<contracts::SettingOperation> fix_recovery_ops() { return {}; }
    // Iki makine journal'inda yarim kalmis VE geri alinmasi dogrulanamamis (RecoveryRequired) islemler (acilista
    // Evet/Hayir ile yukseltilmis kurtarma; RequestAction::Recover; broker oturum basinda uzlastirip yeniden dener).
    virtual std::vector<contracts::SettingOperation> unreconciled_ops() { return {}; }
    // Makine fixes journal'inda dis degisiklik bekleyen duzeltmeler ("Mevcut degeri koru"; RequestAction::KeepCurrent).
    virtual std::vector<contracts::SettingOperation> fix_conflict_ops() { return {}; }
    // Makine fixes journal'i satir durumlari (journal_fix_targets 5'li ayrim; salt okuma). Varsayilan: eski iki liste.
    virtual void fix_target_lists(std::vector<std::string>& active, std::vector<std::string>& failed, std::vector<std::string>& interrupted,
                                  std::vector<std::string>& conflict) {
        (void)interrupted;
        (void)conflict;
        for (const auto& op : fix_active_ops()) active.push_back(op.target);
        for (const auto& op : fix_recovery_ops()) failed.push_back(op.target);
    }
};

}  // namespace ef::core::broker
