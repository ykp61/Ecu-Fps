#pragma once
// P1: "Performans etkisini olc" kanit deneyi (saf cekirdek; Win32 yok, birim testlenir).
// Tasarim: docs/superpowers/plans/2026-11-17-p1-abba-proof.md
//  - Sira: isinma (Kapali, puanlanmaz) + ABBA; A = Kapali, B = Acik. Kol basina en az 5 gecerli kosu.
//  - Birim kosudur (kare degil). Hukum yalniz ortalama FPS'in %95 Welch araligina bakar; aralik 0'i
//    iceriyorsa "fark yok". Esik UYDURULMAZ: gurultu, deneyin kendi kosu-ici yayilimidir.
//  - Gecersiz kosu / farkli kosul parmak izi -> hukum yok (neden yazilir). <5 gecerli kosu -> yetersiz veri.
//  - Iptal / hata -> hukum yok; Acik kaldiysa geri yukleme istenir (RestoreOriginal).
//  - Performansi YALNIZ cagiran (mevcut PerfController akisi) acar/kapatir; bu modul ayar yazmaz.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/perf/before_after.h"

namespace ef::core::perf {

enum class ProofArm { Off, On };  // Kapali, Acik

inline constexpr int kProofPairs = 5;  // ABBA cift sayisi -> kol basina 5 kosu
inline constexpr int kProofWarmup = 1;
inline constexpr std::size_t kProofMinRunsPerArm = 5;

// Tek puanlanan kosunun olculen degerleri. conditions: parmak izi alanlari (alan adi -> deger); tum kosularda ayni
// olmalidir (arena plani, adapter, surucu, cozunurluk, guc kaynagi, uygulama surumu, ...).
struct ProofRun {
    ProofArm arm = ProofArm::Off;
    bool valid = false;
    std::string invalid_reason;  // ASCII neden anahtari (valid == false iken)
    std::map<std::string, std::string> conditions;
    double avg_fps = 0;              // MEAS-04 ortalama FPS (olcum penceresi)
    std::optional<double> low1_fps;  // low1_v1
    std::optional<double> p99_ms;    // kare suresi p99
};

enum class ProofVerdict {
    Better,            // Acik, ortalama FPS'i anlamli artirdi (aralik 0'in ustunde)
    Worse,             // Acik, ortalama FPS'i anlamli dusurdu
    NoDifference,      // aralik 0'i iceriyor: "fark yok / anlamli fark olculmedi"
    Mixed,             // ortalama ve %1 dusuk FPS ters yonde anlamli: tek yonde iddia yok
    InsufficientData,  // kol basina < 5 gecerli kosu
    ConditionsDiffer,  // parmak izi farkli
    InvalidRun,        // puanlanan bir kosu gecersiz
    Aborted,           // iptal / kosu veya gecis hatasi: tamamlanmadi
    RestoreFailed,     // Kapali'ya donus dogrulanamadi: sonuc yok, kullanici bilgilendirilir
};
std::string_view to_string(ProofVerdict v) noexcept;
// Hukum verildi mi (Better/Worse/NoDifference/Mixed); diger durumlarda sayisal iddia yoktur.
bool has_verdict(ProofVerdict v) noexcept;

// Tek metrigin kol karsilastirmasi (fark = Acik - Kapali).
struct ProofMetric {
    std::string key;  // "avg_fps" | "low1_fps" | "p99_ms"
    bool higher_is_better = true;
    std::size_t n_off = 0, n_on = 0;
    std::optional<double> mean_off, mean_on, sd_off, sd_on;  // sd: kosudan kosuya ornek std sapmasi (n-1)
    std::optional<double> diff, ci_low, ci_high;
    Verdict stat = Verdict::Unsupported;  // before_after::Verdict (Significant / NoSignificantDifference / ...)
    std::optional<bool> improved;         // yalniz Significant iken
};

struct ProofResult {
    ProofVerdict verdict = ProofVerdict::InsufficientData;
    std::size_t valid_off = 0, valid_on = 0;
    ProofMetric avg_fps, low1_fps, p99_ms;
    bool low1_differs = false;      // verdict NoDifference iken %1 dusuk FPS anlamli fark gosterdi (not; hukum degismez)
    bool quality_tradeoff = false;  // plan gorsel kaliteyi degistirebilir (kalite takasi etiketi)
    std::string reason;             // InvalidRun: ilk gecersiz kosunun nedeni; ConditionsDiffer: farkli alanlar (",")
    std::size_t bad_run = 0;        // InvalidRun: 1'den baslayan puanlanan kosu sirasi
};

// Tamamlanmis puanlanan kosular (sirasiyla) -> sonuc. Saf.
ProofResult evaluate_proof(const std::vector<ProofRun>& runs, bool quality_tradeoff = false);

// ---- P4: "Fark yok" kaydi (bu PC, ayar grubu bazinda) ----
// Deneyin hukmu olculen alt kumenin TAMAMI icindir (gruplar birlikte olculur). Kural:
//  - NoDifference: kapsamdaki her grup "olculebilir fark gorulmedi" olarak isaretlenir.
//  - Better / Worse / Mixed: kapsamdaki gruplarin isareti kaldirilir (birlikte anlamli bir fark olculdu).
//  - Hukumsuz sonuc (yetersiz veri, iptal, kosul farki ...): kayit degismez.
// Yalniz gorunum vurgusudur (onay ekraninda "Ayrintilar" altinda); plan, secim ve uygulama degismez. Saf.
inline constexpr std::size_t kMaxNoEffectGroups = 64;
std::vector<std::string> update_no_effect_groups(std::vector<std::string> current, const std::vector<std::string>& scope_groups,
                                                 ProofVerdict verdict);

// ---- Baslama on kosullari ----
enum class ProofRefusal {
    None,
    GameRunning,        // oyun calisiyor / algilama guvenilmez
    OtherTestBusy,      // baska test / veri silme suruyor
    PerfNotOff,         // Performans baslangicta Kapali degil (Acik / kurtarma / mesgul)
    PlanNotReady,       // plan onizlemesi henuz hazir degil
    PlanEmpty,          // degistirilecek ayar yok
    PlanNeedsAdmin,     // secili ayarlarin HEPSI yonetici (UAC) istiyor: olculecek alt kume bos (karisik planda alt kume olculur)
    PlanNeedsReboot,    // olculecek alt kume bos ve kalanlar yeniden baslatma istiyor: deney etkisiz kalir
    PlanBackgroundOnly, // yalniz arka plan programlari grubu secili: surec kimligi her gecista degisir, olculemez
    NoHardwareAdapter,  // donanim cizim bagdastiricisi yok (WARP puanlanmaz): desteklenmiyor
};
std::string_view to_string(ProofRefusal r) noexcept;

struct ProofPreconditions {
    bool game_blocked = false;
    bool other_test_busy = false;
    bool perf_off = false;  // controller Phase::Off ve kurtarma gerekmiyor
    bool plan_ready = false;
    bool plan_empty = true;
    // Olculecek alt kume: secili gruplardan yonetici/yeniden baslatma istemeyen ve arka plan olmayanlar (experiment_scope_item).
    std::size_t scope_groups = 0;
    bool excluded_admin = false;       // secili ogelerden en az biri yonetici istiyor (alt kumeye girmez)
    bool excluded_reboot = false;      // secili ogelerden en az biri yeniden baslatma istiyor (alt kumeye girmez)
    bool hardware_adapter = true;  // bilinmiyorsa true (kosu sirasinda no_hardware_adapter ayrica yakalanir)
};
// Ilk tutmayan neden (sabit sira); hepsi tutuyorsa None.
ProofRefusal check_proof_preconditions(const ProofPreconditions& p) noexcept;

// ---- Sure tahmini ----
// Gercek cihaz kosusuna (0.8.3, 2026-11-17, Hizli sahne, 11 kosu + 6 gecis, 2 grup, yardimci hizmet) gore kalibre edildi:
// planlanan kosu suresi toplami 11 x 5,25 = 57,75 sn; gercek toplam ~85 sn (yoklama araligi nedeniyle 67-87 sn). Fark ~27 sn
// kosu hazirligi + gecis payidir. YALNIZ TOPLAM OLCULDU; iki sabit arasindaki bolusum varsayimdir (kosu basina 1 sn, gecis basina
// 3 sn -> 86,75 sn, yani 2 dk). Daha ayrintili zamanlama kaydi eklenirse bolusum guncellenmelidir.
inline constexpr double kProofRunOverheadS = 1.0;
inline constexpr double kProofToggleBudgetS = 3.0;
// Gecis sayisi (SetArm) ve toplam kosu (isinma dahil) ABBA dizisinden hesaplanir; bitis restore'u dahil.
struct ProofPlanSize {
    std::size_t runs_total = 0, runs_scored = 0, toggles = 0;
};
ProofPlanSize proof_plan_size(int pairs = kProofPairs, int warmup = kProofWarmup);
// planned_step_seconds: bir kosunun gercek isinma + olcum suresi (balance StepTiming'den).
double estimate_proof_seconds(double planned_step_seconds, int pairs = kProofPairs, int warmup = kProofWarmup);
// Kart dugmesi icin dakika (yukari yuvarlanir, en az 1).
int estimate_proof_minutes(double planned_step_seconds, int pairs = kProofPairs, int warmup = kProofWarmup);

// ---- Durum makinesi ----
enum class ProofPhase { Idle, AwaitConsent, Running, Restoring, Done, Aborted };
std::string_view to_string(ProofPhase p) noexcept;

enum class ProofActionKind { SetArm, MeasureRun, RestoreOriginal };
struct ProofAction {
    ProofActionKind kind = ProofActionKind::MeasureRun;
    ProofArm arm = ProofArm::Off;  // SetArm: hedef; MeasureRun: kosunun kolu
    bool scored = false;           // MeasureRun: false = isinma
    std::size_t run_index = 0;     // MeasureRun: isinma dahil 0'dan
};

// Gecis sonucu (controller): Applied = hedef durum geri okumayla dogrulandi; NotApplied = degisiklik yapilmadi/geri
// alindi (durum ONCEKI kol); Unverified = geri alma dogrulanamadi (kurtarma gerekir).
enum class ProofArmOutcome { Applied, NotApplied, Unverified };

struct ProofProgress {
    std::size_t scored_done = 0, scored_total = 0;
    std::size_t run_index = 0;  // siradaki / suren kosu (isinma dahil)
    bool in_warmup = false;
    ProofArm arm = ProofArm::Off;  // dogrulanmis mevcut kol
};

class ProofSession {
public:
    explicit ProofSession(int pairs = kProofPairs, int warmup = kProofWarmup, bool quality_tradeoff = false);
    // Idle -> AwaitConsent. On kosullar cagirandadir (check_proof_preconditions).
    void begin();
    // Tek Evet/Hayir. Hayir -> Idle (hicbir sey degismedi). Evet -> Running.
    void consent(bool yes);
    // Bekleyen eylem (cevap gelene kadar degismez). Terminal / onay beklerken nullopt.
    std::optional<ProofAction> next() const;
    void on_arm_set(ProofArmOutcome o);
    void on_run(ProofRun run);  // isinma kosusunda deger yok sayilir; yine de cagrilmalidir
    void on_restore(ProofArmOutcome o);
    // Iptal / kosu hatasi (oyun basladi vb.): kalan kosular atilir; Acik ise RestoreOriginal istenir.
    void cancel();
    // Cagiranin tespit ettigi hata (gecis kismi uygulandi, plan degisti vb.): cancel ile ayni yol, neden kayda yazilir.
    void abort(std::string reason);
    ProofPhase phase() const { return phase_; }
    bool terminal() const { return phase_ == ProofPhase::Done || phase_ == ProofPhase::Aborted; }
    ProofArm arm() const { return arm_; }
    ProofProgress progress() const;
    // Terminal olunca dolu.
    const std::optional<ProofResult>& result() const { return result_; }
    const std::vector<ProofRun>& runs() const { return runs_; }

private:
    void stop(int why);
    void settle();
    void finish_terminal();

    std::vector<std::optional<ProofArm>> schedule_;  // nullopt = isinma (Kapali)
    std::size_t idx_ = 0;
    ProofArm arm_ = ProofArm::Off;
    ProofPhase phase_ = ProofPhase::Idle;
    bool quality_tradeoff_ = false;
    std::string stop_reason_;
    int stop_ = 0;  // 0 yok, 1 iptal, 2 gecersiz kosu, 3 geri yukleme dogrulanamadi, 4 gecis hatasi
    std::vector<ProofRun> runs_;
    std::optional<ProofResult> result_;
};

}  // namespace ef::core::perf
