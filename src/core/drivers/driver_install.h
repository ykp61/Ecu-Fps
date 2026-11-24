#pragma once
// Spec 2026-11-12 H2 (ADR-031): Windows Update surucu kurulumu. Yalniz yukseltilmis yardimci (broker) calistirir; tek
// izinli islem OperationId::DriverInstall, hedef "wuinst:<UpdateID>:<RevisionNumber>", deger "install".
//  - Yardimci arayuzden gelen veriye guvenmez: guncellemeyi UpdateID + RevisionNumber ile Windows Update'te YENIDEN arar
//    ve yalniz tam o surucu guncellemesini kabul eder (tek eslesme, tur Driver, kaynak Windows Update, gizli degil,
//    kullanici girdisi / lisans onayi istemiyor). Firmware sinifi (is_firmware) her zaman reddedilir.
//  - Sira: kalici journal baslangic kaydi -> geri yukleme noktasi istegi (olusmadiysa sonucta acikca yazilir) -> oyun
//    kapisi -> indirme -> oyun kapisi -> kurulum -> sonuc kodu + yeniden baslatma geregi geri okunur -> journal kapanir.
//    Kayit yazilamazsa hicbir sey indirilmez / kurulmaz. Iptal kurulum BASLAMADAN once uygulanir; baslamis kurulum
//    yarida kesilmez (surucu yarim birakilmaz).
//  - Windows Update Agent cagrilari DriverInstallApi arabiriminin arkasindadir (platform/windows/wua_install/); birim
//    testleri sahte arabirim kullanir. Bu dosyada platform cagrisi yoktur.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/drivers/driver_updates.h"
#include "core/recovery/journal.h"

namespace ef::core::drivers {

inline constexpr std::string_view kInstallTargetPrefix = "wuinst:";
inline constexpr std::string_view kInstallValue = "install";

// Kurulacak guncellemenin kimligi (UpdateID kucuk harf, suslu parantezsiz GUID).
struct InstallRef {
    std::string update_id;
    int revision = 0;
    friend bool operator==(const InstallRef&, const InstallRef&) = default;
};
// "8-4-4-4-12" onaltilik GUID (suslu parantez yok). Buyuk/kucuk harf kabul edilir.
bool valid_update_id(std::string_view id);
// Gecersiz kimlik / revizyon (< 1) -> bos.
std::string install_target(std::string_view update_id, int revision);
// Kesin bicim: "wuinst:" + GUID + ":" + 1..9 haneli ondalik revizyon (bastaki sifir yok). Aksi halde nullopt.
std::optional<InstallRef> parse_install_target(std::string_view target);
// Yeniden arama olcutu: "UpdateID='<id>' and RevisionNumber=<n>" (IsInstalled kosulu yok: kurulu oge "zaten kurulu"
// diye bildirilir).
std::wstring install_criteria(const InstallRef& ref);

// Yeniden aramanin buldugu oge (alanlar WUA'dan; bilinmeyen alan false / bos).
struct InstallCandidate {
    DriverUpdate update;               // update_id + revision + sinif + donanim kimligi + baslik
    bool type_driver = false;          // IUpdate::Type == surucu
    bool from_windows_update = false;  // arama Windows Update hizmetine karsi yapildi (WSUS / baska hizmet degil)
    bool installed = false;
    bool hidden = false;
    bool eula_accepted = true;         // lisans onayi kullanici adina verilmez
    bool needs_user_input = false;     // kurulum sirasinda kullanici girdisi isteyebilir
};

struct FindOutcome {
    bool ok = false;
    bool cancelled = false;
    std::int32_t hresult = 0;
    std::vector<InstallCandidate> found;
};

// WUA OperationResultCode degerleri (0 NotStarted, 1 InProgress, 2 Succeeded, 3 SucceededWithErrors, 4 Failed, 5 Aborted).
inline constexpr int kWuaNotStarted = 0, kWuaInProgress = 1, kWuaSucceeded = 2, kWuaSucceededWithErrors = 3, kWuaFailed = 4,
                     kWuaAborted = 5;
struct StageOutcome {
    int result_code = kWuaNotStarted;
    std::int32_t hresult = 0;
    bool reboot_required = false;  // yalniz kurulum
};

enum class RestorePointState { NotAttempted, Created, Recent, Failed };
std::string_view to_string(RestorePointState s) noexcept;

// Platformda Windows Update Agent; testte sahte. find() kabul edilen ogeyi icinde tutar; download/install o ogeyi isler.
class DriverInstallApi {
public:
    virtual ~DriverInstallApi() = default;
    virtual FindOutcome find(const InstallRef& ref, const std::function<bool()>& stop) = 0;
    // Kabul edilen ogeyi secer (find'in dondurdugu listedeki indeks). Sonraki indirme / kurulum yalniz bu ogeyi isler.
    virtual bool select(std::size_t index) = 0;
    virtual RestorePointState restore_point() = 0;
    virtual StageOutcome download(const std::function<bool()>& stop) = 0;
    virtual StageOutcome install() = 0;
};

enum class InstallOutcome {
    Installed, InstalledWithErrors, AlreadyInstalled,
    // Red (hicbir sey indirilmedi / kurulmadi):
    BadTarget, NotFound, Ambiguous, NotDriver, Firmware, NotWindowsUpdate, Hidden, NeedsUserInput,
    // Baslamadi / basarisiz:
    SearchFailed, JournalFailed, GateBlocked, Cancelled, DownloadFailed, InstallFailed,
};
std::string_view to_string(InstallOutcome o) noexcept;
std::optional<InstallOutcome> parse_install_outcome(std::string_view s) noexcept;
// Red sonucu mu (kabul kurali; hicbir sey indirilmedi / kurulmadi).
bool is_rejection(InstallOutcome o) noexcept;

// Saf kabul kurali: tam bir eslesme (UpdateID buyuk/kucuk harf duyarsiz + ayni revizyon) ve kosullar. Kabul edilirse
// nullopt + *index secilen oge; aksi halde red / AlreadyInstalled.
std::optional<InstallOutcome> check_candidates(const InstallRef& ref, const std::vector<InstallCandidate>& found, std::size_t* index);
// Indirme sonucu: Succeeded / SucceededWithErrors -> nullopt (devam); Aborted -> Cancelled; digerleri DownloadFailed.
std::optional<InstallOutcome> map_download(const StageOutcome& s);
// Kurulum sonucu: Succeeded -> Installed, SucceededWithErrors -> InstalledWithErrors, Aborted -> Cancelled, digerleri
// InstallFailed.
InstallOutcome map_install(const StageOutcome& s);

struct InstallRun {
    InstallOutcome outcome = InstallOutcome::SearchFailed;
    int result_code = kWuaNotStarted;  // son calisan WUA asamasinin sonuc kodu
    std::int32_t hresult = 0;
    bool reboot_required = false;
    RestorePointState restore_point = RestorePointState::NotAttempted;
    bool previous_interrupted = false;  // onceki kurulum kaydi yarida kalmisti (bu calismada kapatildi)
    std::string title;                  // yeniden aramanin basligi (yardimcinin gordugu gercek oge)
};
// Sonuc metni (StepResult::detail; kullanici metni degil): "di:<outcome>:rc=<n>:hr=<8 hex>:rp=<state>[:reboot][:prev]".
std::string encode_install_detail(const InstallRun& r);
std::optional<InstallRun> decode_install_detail(std::string_view detail);

struct InstallDeps {
    DriverInstallApi* api = nullptr;
    recovery::JournalStore* journal = nullptr;     // makine "drivers" journal'i; yoksa kurulum baslamaz
    std::function<std::string()> new_tx_id;        // 32 hex
    std::function<std::int64_t()> now_unix_ms;
    std::function<bool()> cancel;                  // true: kurulum baslamadan birak
    std::function<bool()> gate_ok;                 // oyun kapisi (indirme ve kurulumdan hemen once yeniden sorulur)
    std::string actor_sid, target_sid;
};
InstallRun run_driver_install(const InstallDeps& d, std::string_view target);

}  // namespace ef::core::drivers
