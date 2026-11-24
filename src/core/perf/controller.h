#pragma once
// UI-02: tek Performans anahtarinin durum makinesi (saf mantik, birim testlenir).
// Degismezler:
//  - Kullanici modu yalniz Acik/Kapali; profil/mod yok.
//  - Plan once gosterilir; Evet olmadan hicbir sey uygulanmaz. Hayir hicbir sey degistirmez.
//  - "Acik" yalniz arka uc islemi Active olarak dogruladiktan sonra gosterilir.
//  - "Kapali" yalniz geri yukleme dogrulandiktan sonra gosterilir; dogrulanamazsa kurtarma durumu.
//  - Ilerleme yuzdesi gercek adim sayisindan gelir (sahte yuzde yok).
// Uzun isler Executor ile arka planda calisir; sonuc UI is parcacigina postalanir.

#include <algorithm>
#include <atomic>
#include <map>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/perf/device_class.h"
#include "core/perf/drift.h"
#include "core/recovery/engine.h"
#include "core/recovery/restore_report.h"

namespace ef::core::perf {

// P2: onayda/panelde gorunen olculmus satir (dinamik: program adi + CPU + RAM). Katalog sablonu "plan.field.background".
struct FieldDetail {
    std::string name;
    double cpu_pct = 0.0;
    std::uint64_t ram_bytes = 0;
    bool operator==(const FieldDetail& o) const { return name == o.name; }  // ayni program tek satir
};

struct PlanItem {
    contracts::SettingOperation op;
    std::string title_key;  // katalog anahtari (ornek "plan.power_mode")
    std::string group_key;  // UI-10: kullanicinin Evet/Hayir sectigi grup (bos = title_key)
    const std::string& group() const { return group_key.empty() ? title_key : group_key; }
    bool needs_admin = false;
    bool changes_startup = false;
    std::optional<FieldDetail> detail;  // P2: arka plan programi satiri (yalniz olculmus degerler)
    std::string field_key;  // EF-X25 T03: onayda gorunen alan + kapsam + yeni deger (bos = grup basligi yeter)
};

// A3 (P12 karar 7): plana GIRMEYEN bir parcanin kaydi. "Desteklenmeyen" ile "veri yetersiz" ayri tutulur;
// sessiz dusme yok: neden kayitlidir (UI sonradan gosterebilir; bu yapi plan ogesi degildir, onaya girmez).
enum class OmissionKind { Unsupported, Insufficient };
struct PlanOmission {
    std::string group_key;   // ornek "plan.nvidia"
    OmissionKind kind = OmissionKind::Insufficient;
    std::string reason_key;  // katalog anahtari (ornek "plan.nvidia.gate.hybrid_target_unknown")
};

struct Plan {
    std::vector<PlanItem> items;
    std::vector<PlanOmission> omitted;  // plana girmeyen parcalar ve nedenleri (bos olabilir)
    bool affects_quality() const;
    bool needs_reboot() const;
    bool needs_admin() const;
    std::size_t startup_changes() const;
    bool empty() const { return items.empty(); }
};

enum class OutcomeKind {
    Active,            // tum adimlar uygulandi ve dogrulandi
    NothingToDo,       // plan bos (sifir is)
    Restored,          // geri yukleme dogrulandi
    FailedRestored,    // uygulama basarisiz/iptal; onceki degerler geri yuklendi
    RecoveryRequired,  // geri alma dogrulanamadi; kayit korunuyor
    Denied,            // UAC reddedildi / yetki yok: hicbir sey degismedi
    GateBlocked,       // islem sinirinda oyun/algilama kapisi reddetti: yeni adim baslamadi
};

struct Outcome {
    OutcomeKind kind = OutcomeKind::NothingToDo;
    std::size_t changes = 0;
    std::string detail;  // teknik ayrinti (log; kullanici metni degil)
    bool pending_reboot = false;  // OPT-10: HAGS yazildi/geri alindi; etkin durum yeniden baslatmadan sonra
    bool admin_skipped = false;   // OPT-10: UAC reddedildi/basarisiz -> yonetici gruplari uygulanmadi, gerisi uygulandi
    bool gate_blocked = false;    // UAC sonrasi kapi kapandi: kalan yonetici adimlari uygulanmadi, gerisi uygulandi
    std::string gate_reason;      // gate.* (GateBlocked / gate_blocked iken)
    // T08: otomatik geri yukleme noktasi sonucu. NotRun = planda yoktu; Skipped = Windows siklik siniri nedeniyle atladi
    // (hata degil); Failed = olusturulamadi (plan yine de uygulandi; ayarlar journal ile geri alinabilir).
    enum class RestorePoint { NotRun, Created, Skipped, Failed };
    RestorePoint restore_point = RestorePoint::NotRun;
};

using ProgressFn = std::function<void(const recovery::Progress&)>;

class PerfBackend {
public:
    virtual ~PerfBackend() = default;
    virtual contracts::Result<Plan> plan() = 0;
    virtual Outcome apply(const Plan& plan, const ProgressFn& progress, const std::atomic<bool>& cancel) = 0;
    virtual Outcome restore(const ProgressFn& progress) = 0;
    // Baslangic durumu (journal): etkin islem sayisi ve kurtarma gereksinimi.
    virtual std::size_t active_changes() = 0;
    virtual bool recovery_needed() = 0;
    // R-F1: onceki surumun geri alinmamis kayitlari bu surume aktarilamadi (recovery_needed de true doner).
    virtual bool legacy_unmigrated() { return false; }
    // Geri alma dis degisiklik nedeniyle duruyor mu; "Mevcut degeri koru" kaydi ayar yazmadan kapatir.
    virtual bool external_conflict() { return false; }
    virtual Outcome keep_external() { return {OutcomeKind::NothingToDo, 0, {}}; }
    // Saha 2026-11-15: Performans kapsamlarinda (kullanici + makine) geri alinamamis adimlar (salt okuma).
    virtual std::vector<recovery::FailedRestoreItem> failed_items() { return {}; }
    // Acilista uzlastirma (salt okuma): kayitta Active olan her kullanici adiminin GERCEK hedefi okunur; yazdigimiz
    // degerde olmayan (baska program degistirdi: reason External; okunamadi: NotVerified) adimlar doner. Bos = hepsi hala etkin.
    virtual std::vector<recovery::FailedRestoreItem> check_live() { return {}; }
    // Yeniden Acik: gercekte kapanmis adimlar KeptExternal olarak kalici kapatilir (ayar yazilmaz/geri alinmaz; kullanicinin
    // mevcut degeri kalir) ki yeni islem onlari yeniden yazabilsin ve sonraki Kapali'da catisma cikmasin.
    virtual void retire_off(const std::vector<recovery::FailedRestoreItem>&) {}
    // Koruma Modu (T07, salt okuma): journal'da etkin kullanici adimlarindan gercekte sapmis olanlar (Windows guncellemesi vb.).
    // Okunamayan ve oturumla biten adimlar sayilmaz; yonetici (broker) adimlari UAC olmadan okunamaz ve burada yoktur.
    virtual std::vector<DriftItem> drift() { return {}; }
    // Sapan adimlari ORIJINAL yazilan degerle yeniden yazar (ayni adaptorler), geri okuyarak dogrular. Onceki-deger
    // anlik goruntusune dokunmaz: Kapali yine gercek ilk degerleri geri yukler. Basarisizsa kayit oldugu gibi kalir.
    virtual Outcome reapply_drift(const std::vector<DriftItem>&, const ProgressFn&) { return {OutcomeKind::NothingToDo, 0, {}}; }
};

enum class Phase { Off, Planning, Confirming, Applying, On, ConfirmRestore, Restoring, RecoveryNeeded };
std::string_view to_string(Phase p) noexcept;

// Son islem hakkinda kullaniciya gosterilecek kisa not.
enum class Notice { None, NoChanges, Denied, FailedRestored, PlanFailed, RestoreFailed, NoneSelected, PlanChanged, PartialApplied, AdminSkipped,
                    AdminBusy, GameBlocked, SomeOff,
                    DriftFailed,
                    LegacyUnmigrated,  // R-F1: onceki surumun etkin kayitlari aktarilamadi (kurtarma durumu; eski kayit korunuyor)
                    RestorePointFailed };  // RestorePointFailed (T08): geri yukleme noktasi olusmadi; ayarlar yine de uygulandi ve journal ile geri alinabilir  // DriftFailed: Koruma Modu yeniden uygulamasi tam dogrulanamadi (kayit degismedi)  // GameBlocked: islem sinirinda oyun/algilama engeli; AdminBusy: baska yonetici islemi (Oyun Hazirligi UAC/broker) suruyor; hicbir sey baslatilmadi

// OPT-02: onaylanan islem setinin parmak izi (islem kimligi + hedef + yeni deger, sirali). Uygulamadan hemen
// once plan yeniden kurulur; iz degistiyse eski onay gecersizdir (bayat onayla yazma yok).
std::string plan_fingerprint(const Plan& p);

// UI-10: anahtarin degistirebilecegi ayar gruplari (sabit katalog; plan hangilerinin degisecegini soyler).
// EF-X25 T04: grup basina durust etiketler. Olculdu = uygulama kendi olcumuyle gosterir; Belgeli = uretici/Microsoft
// belgesi var; Tartismali = etki topluluk iddiasi, olcum garantisi yok. Risk: geri alinabilirlik ve yan etki acisindan.
enum class Evidence { Measured, Documented, Disputed };
enum class Risk { Low, Medium, High };
std::string_view evidence_key(Evidence e) noexcept;  // "plan.evidence.measured" ...
std::string_view risk_key(Risk r) noexcept;          // "plan.risk.low" ...

struct PlanGroup {
    std::string_view key;       // secim ve plan grubu ("plan.power_mode")
    std::string_view desc_key;  // aciklama (etki dahil, durust)
    bool default_on = true;     // secim kaydi yokken Evet mi (P2 arka plan grubu: kullanici acikca secmeden Hayir)
    Evidence evidence = Evidence::Documented;
    Risk risk = Risk::Low;
    // ADR-040: false = grup bu sistemde ogesi yokken panelin Ayrintilar listesinde de satir almaz (Panel 0.9.2
    // gorunumu degismez; yeni gruplar ancak plan ogesi uretince gorunur). Mevcut gruplar true (eski davranis).
    bool listed_when_empty = true;
};
const std::vector<PlanGroup>& plan_groups();
// Panel/onay listesinde satir alir mi: planda ogesi var ya da grup ogesizken de listelenir.
inline bool group_listed(const PlanGroup& g, bool has_items) noexcept { return has_items || g.listed_when_empty; }
// Gelismis: varsayilani Hayir olan ve kullanicinin acikca Evet demedigi grup (onay ekraninda katlanir).
inline bool group_is_advanced(const PlanGroup& g, bool selected) noexcept { return !g.default_on && !selected; }
// Onay ekrani gorunumu: planda ogesi olan (bos gruplar yok) gruplar, katalog sirasiyla. selection yoksa varsayilan.
struct GroupView {
    std::string_view key;
    std::string_view desc_key;
    Evidence evidence = Evidence::Documented;
    Risk risk = Risk::Low;
    bool advanced = false;
};
std::vector<GroupView> visible_groups(const Plan& p, const std::map<std::string, bool>& selection, const DeviceClass& dc = {});
// EF-X25 T03: grubun bu planda uygulanacak alanlari (sirali, tekrarsiz, bos alan atlanir). Yalniz metin; islem seti degismez.
std::vector<std::string> group_field_keys(const Plan& p, std::string_view group);
// P2: grubun olculmus program satirlari (sirali, program basina tek).
std::vector<FieldDetail> group_field_details(const Plan& p, std::string_view group);
// P1: kanit deneyinin olcum kapsami. Yonetici (UAC) / yeniden baslatma isteyen ogeler ve arka plan grubu (surec kimligi
// her gecista degisir; parmak izi kararsiz) deneye GIRMEZ. Saf; ayar yazmaz.
// admin_ok: yardimci hizmet hazir (UAC'siz yol); yalniz hizmetin yurutebildigi yonetici ogeler (service_handles_op) kapsama girer.
bool experiment_scope_item(const PlanItem& i, bool admin_ok = false);

// work: arka planda calisacak is; done: UI is parcaciginda calisacak devam. Testte senkron.
using Executor = std::function<void(std::function<void()> work)>;
using Poster = std::function<void(std::function<void()> on_ui)>;

class PerfController {
public:
    PerfController(PerfBackend& backend, Executor run_background, Poster post_ui);

    // Baslangicta journal'dan durum okunur (Acik kalan islem veya kurtarma gereksinimi).
    void init();
    // UI-10: "neleri degistirir" paneli icin plan onizlemesi (arka planda; asamayi degistirmez).
    void refresh_preview();
    const Plan& preview() const { return preview_; }
    bool preview_ready() const { return preview_ready_; }
    // Grup secimi (Evet/Hayir). Yalniz Kapali veya onay asamasinda degistirilebilir.
    void set_selected(const std::string& group, bool on);
    bool selected(const std::string& group) const;
    // T18: cihaz sinifi (RAM/cekirdek/dizustu). Yalniz kayitli secimi olmayan gruplarin varsayilanini etkiler.
    void set_device_class(const DeviceClass& dc) {
        device_ = dc;
        if (on_change) on_change();
    }
    const DeviceClass& device_class() const { return device_; }
    // T18: onay ekraninda "Bu bilgisayar icin uyarlandi" notu: cihaz sinifi, planda olan ve kullanicinin kaydi olmayan bir
    // grubun varsayilanini degistirdi.
    bool tailored() const;
    // T18: son basarili uygulamada geri okunarak dogrulanan ayar sayisi (Outcome'dan; tahmin yok). 0 = bu oturumda uygulanmadi.
    std::size_t verified_count() const { return verified_count_; }
    std::function<void(const std::string&, bool)> on_selection_changed;  // kalicilik (preferences)
    void load_selection(const std::map<std::string, bool>& m) { selection_ = m; }
    // P4: bu PC'deki kanit deneyinde "Fark yok" hukmune giren gruplar (preferences). Yalniz gorunum: secim/plan degismez.
    void set_no_effect_groups(std::vector<std::string> g) { no_effect_ = std::move(g); }
    bool no_effect(const std::string& group) const { return std::find(no_effect_.begin(), no_effect_.end(), group) != no_effect_.end(); }
    // Anahtara basildi: Kapali -> plan hazirla; Acik -> geri yukleme onayi; kurtarma -> tekrar dene onayi.
    void toggle();
    // Onay penceresi cevabi (Evet / Hayir).
    void answer(bool yes);
    // Baska bir yonetici islemi suruyor (ikinci UAC/broker acilmaz): asama degismez, yalniz bildirim gosterilir.
    // Is suren asamada (Planning/Applying/Restoring) yok sayilir.
    void notify_busy();
    // OPT-06: onayli oyun basladiginda / kapandiginda (oyun basina "Izin ver" kullanicinin onayidir).
    // auto_apply: yalniz Kapali iken; secili gruplari planlayip uygular. auto_restore: yalniz otomatik
    // acildiysa ve Acik iken geri alir. Elle acilmis Performans otomatik kapatilmaz.
    void auto_apply();
    void auto_restore();
    bool auto_active() const { return auto_active_; }
    // Cokme/yeniden baslatma sonrasi: Acik durum otomatik moddan geldiyse isaret geri yuklenir.
    void resume_auto() {
        if (phase_ == Phase::On) auto_active_ = true;
    }
    void cancel() { cancel_ = true; }
    // P1: deney kapsami acikken start_apply yalniz experiment_scope_item ogelerini uygular (kullanicinin kayitli secimi degismez).
    // admin_ok: hizmet yolu hazirken yonetici ogeler de kapsamda (UAC acilmaz).
    void set_experiment_scope(bool on, bool admin_ok = false) {
        experiment_scope_ = on;
        experiment_admin_ok_ = on && admin_ok;
    }
    bool experiment_scope() const { return experiment_scope_; }
    // Kurtarma: dis degisiklik bekleyen kayitlar mevcut degerle kapatilir (ayar yazilmaz).
    void keep_external();
    bool external_conflict() const { return external_conflict_; }
    // Koruma Modu (T07): acilista sapmis ayarlar. "Yeniden uygula" Evet/Hayir onayina gider (sessiz uygulama yok);
    // "Boyle kalsin" mevcut degeri korur (retire_off: ayar yazilmaz). Oyun calisirken arka uc WriteGuard'i engeller.
    const std::vector<DriftItem>& drift_items() const { return drift_items_; }
    // Windows yapi numarasi son acilistan beri degisti (metin "guncelleme sonrasi" olur); yalniz sunum.
    void set_os_updated(bool v) { os_updated_ = v; }
    bool os_updated() const { return os_updated_; }
    bool drift_confirm() const { return drift_confirm_; }
    void reapply_drift();
    void keep_drift();
    // Saha 2026-11-15: kurtarma asamasinda geri alinamamis ayarlar (asama RecoveryNeeded olunca journal'dan okunur).
    const std::vector<recovery::FailedRestoreItem>& failed_items() const { return failed_items_; }
    // Acilis uzlastirmasi: etkin gorunen ama gercekte artik uygulanmamis ayarlar (Acik/Kapali asamasinda; Notice::SomeOff).
    const std::vector<recovery::FailedRestoreItem>& off_items() const { return off_items_; }

    Phase phase() const { return phase_; }
    Notice notice() const { return notice_; }
    const Plan& plan() const { return plan_; }
    const recovery::Progress& progress() const { return progress_; }
    std::size_t active_changes() const { return active_changes_; }
    // OPT-10: son uygulama/geri alma yeniden baslatma bekleyen bir ayara (HAGS) dokundu.
    bool reboot_pending() const { return reboot_pending_; }
    // Anahtar gorsel konumu: yalniz dogrulanmis Active durumda acik.
    bool switch_on() const { return phase_ == Phase::On || phase_ == Phase::ConfirmRestore || phase_ == Phase::Restoring; }
    bool busy() const { return phase_ == Phase::Planning || phase_ == Phase::Applying || phase_ == Phase::Restoring; }

    std::function<void()> on_change;

private:
    void set(Phase p, Notice n);
    void start_apply();
    void start_plan();
    void start_restore();
    // Onay/plan reddedilince donulecek asama: kayitta hala etkin ayar varsa Acik, yoksa Kapali.
    Phase idle_phase() const { return (!off_items_.empty() && active_changes_ > 0) ? Phase::On : Phase::Off; }

    PerfBackend& backend_;
    Executor run_;
    Poster post_;
    Phase phase_ = Phase::Off;
    Notice notice_ = Notice::None;
    Plan plan_;
    bool auto_active_ = false;
    bool auto_pending_ = false;
    bool experiment_scope_ = false;
    bool experiment_admin_ok_ = false;
    Plan preview_;
    bool preview_ready_ = false;
    std::map<std::string, bool> selection_;  // yoksa secili
    std::vector<std::string> no_effect_;     // P4: yalniz gorunum vurgusu
    recovery::Progress progress_;
    std::size_t active_changes_ = 0;
    DeviceClass device_;
    std::size_t verified_count_ = 0;
    bool reboot_pending_ = false;
    bool external_conflict_ = false;
    std::vector<DriftItem> drift_items_;
    bool os_updated_ = false;
    bool drift_confirm_ = false;  // Confirming asamasi Koruma Modu yeniden uygulamasi icin (plan_ = sapan adimlar)
    void start_drift_apply();
    std::vector<recovery::FailedRestoreItem> failed_items_;
    std::vector<recovery::FailedRestoreItem> off_items_;
    std::atomic<bool> cancel_{false};
};

}  // namespace ef::core::perf
