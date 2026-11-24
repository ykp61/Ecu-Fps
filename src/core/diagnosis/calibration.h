#pragma once
// DIAG-06: kalibrasyon ve bekletme (holdout) dogrulamasi (saf). Etiketli ornekler kaynak bilgisi
// (sentetik fikstur / gercek donanim) tasir. Ayni ornek hem esik ayari hem bagimsiz basari iddiasi icin
// kullanilmaz: ornekler sabit kurala gore "tune" ve "holdout" kumelerine ayrilir. Gercek donanim orani
// ayri raporlanir; yetersizse teshis ozelligi yayinda kapali kalir.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/compute.h"
#include "core/diagnosis/release_gate.h"

namespace ef::core::diagnosis {

enum class Provenance { SyntheticFixture, RealHardware };
enum class Split { Tune, Holdout, Validation };  // Validation sona eklendi

// EF-X20: etiket yontemi. Yalniz kontrollu deney/uzman kapiya sayilir; "rule_derived" okuyucuda reddedilir.
enum class LabelMethod { Unspecified, ControlledExperiment, ExpertReview };

// EF-X20: genis etiket taksonomisi. ComputeClass degismez; ince etiket compute sinifina eslenir ya da
// baska modele aittir (compute tarafinda puanlanmaz).
enum class CalibrationLabel {
    Legacy,  // v1: label alani dogrudan ComputeClass
    GpuBound, CpuOrEngineBound, CpuMainThread, FrameCapOrPresentWait, FpsCap, Vsync, EngineFrameLimit,
    Mixed, Healthy, Insufficient, RamPressure, StorageWait, ThermalPower, Unknown
};
enum class LabelDomain { Compute, MemoryStorage, PlatformLimits, Unknown };
std::string_view to_string(CalibrationLabel l) noexcept;
std::optional<CalibrationLabel> calibration_label_from(std::string_view s) noexcept;  // Legacy asla donmez
std::optional<ComputeClass> expected_compute_class(CalibrationLabel l) noexcept;      // nullopt: compute disi
LabelDomain label_domain(CalibrationLabel l) noexcept;

inline constexpr std::size_t kComputeClassCount = 6;
static_assert(static_cast<std::size_t>(ComputeClass::Insufficient) + 1 == kComputeClassCount,
              "confusion matrisi ComputeClass sayisina bagli");

struct LabeledWindow {
    std::string id;
    Provenance provenance = Provenance::SyntheticFixture;
    ComputeClass label = ComputeClass::Insufficient;
    EvidenceWindow window;
    RunComputeInputs run;
    WindowComputeExtras extras;
    // EF-X20 (diagcal_v2). v1 orneklerinde bos/varsayilan.
    std::string machine_group;   // opak; holdout bu anahtarla ayrilir
    std::string run_group;       // opak; tune/validation bu anahtarla ayrilir
    // Opak donanim ozeti (seri/UUID degil). Okuyucuda istege bagli, ama EF-X20 dogrulama bulgusu 3: v2'de
    // kapiya uygunluk icin ZORUNLU; yoksa ornek uygun degildir (real_missing_digest ile raporlanir). Disa
    // aktarimda yalniz operatorun verdigi EF_CALIB_MACHINE_DIGEST yazilir; otomatik parmak izi toplanmaz.
    std::string machine_digest;
    LabelMethod label_method = LabelMethod::Unspecified;
    CalibrationLabel fine_label = CalibrationLabel::Legacy;
    DeviceFamily family;
};

// Kararli bolme: id'nin FNV ozeti tek -> holdout (yaklasik yari). Ornek sirasindan bagimsiz.
Split split_of(const std::string& id) noexcept;
// EF-X20: gruplu uc yonlu bolme. Grup yoksa (v1) split_of(id) (Tune/Holdout; kapiya uygun degil).
Split split_for(const LabeledWindow& x) noexcept;
// Uygun: gercek + iki grup + machine_digest dolu + label_method in {controlled_experiment, expert_review}.
bool gate_eligible(const LabeledWindow& x) noexcept;

struct CalibrationStats {
    std::size_t total = 0, correct = 0, false_positive = 0, false_negative = 0, abstained = 0, wrong_class = 0;
    std::size_t synthetic = 0, real = 0;
    // EF-X20 ek alanlar (eski toplamlar ayni anlamda).
    std::size_t real_ineligible = 0;    // gercek ama grupsuz / ozetsiz / etiket yontemi dogrulanmamis
    std::size_t real_missing_digest = 0;  // real_ineligible alt kumesi: gruplu ama machine_digest yok
    // EF-X20 dogrulama bulgusu 1: kapi oranlari YALNIZ uygun gercek orneklerden. Sentetik/uygun olmayan
    // ornekler yanlis pozitif oranini seyreltemez. Uygun toplam = real - real_ineligible.
    std::size_t eligible_correct = 0, eligible_false_positive = 0, eligible_abstained = 0;
    std::size_t out_of_scope = 0;       // ince etiket compute disi (total'a girmez)
    std::size_t distinct_machines = 0;  // uygun gercek orneklerde farkli machine_group
    std::size_t positives = 0;          // uygun gercek: sinirlayici beklenen sinif
    std::size_t negatives = 0;          // uygun gercek: healthy/insufficient
    std::uint32_t covered_families = 0; // >= kMinFamilyHoldoutSamples uygun ornek VE >= kMinFamilyHoldoutMachines
                                        // farkli machine_group olan aile bitleri (tek PC aileyi kapsamaz)
    std::array<std::array<std::size_t, kComputeClassCount>, kComputeClassCount> confusion{};  // [etiket][tahmin]
    // false_positive: etiket Healthy/Insufficient iken sinirlayici sinif dendi; false_negative: sinirlayici
    // etiket varken Healthy dendi; abstained: Insufficient/Mixed cevabi (yanlis sayilmaz, ayri sayilir).
};

CalibrationStats evaluate(const std::vector<LabeledWindow>& set, Split which);

// Raporlama yardimcilari (kapi degil): cekimserlik durust bir cevaptir; bu yuzden iki oran ayri verilir.
// accuracy_all: correct / total (cekimserler yanlis sayilir). accuracy_answered: correct / (total - abstained).
// Bos kume -> 0. Yayin kapisi yalniz yanlis pozitif oranini ve gercek ornek sayisini kullanir.
double accuracy_all(const CalibrationStats& s) noexcept;
double accuracy_answered(const CalibrationStats& s) noexcept;
double coverage(const CalibrationStats& s) noexcept;

// Yayin kapisi: gercek donanim holdout ornegi en az min_real, yanlis pozitif orani <= max_fp_rate.
inline constexpr std::size_t kMinRealHoldout = 30;
inline constexpr double kMaxFalsePositiveRate = 0.05;
inline constexpr std::size_t kMinHoldoutMachines = 3;       // docs/18: tek PC aileyi temsil etmez
inline constexpr std::size_t kMinHoldoutPerSide = 10;       // docs/18 kural 5: sinif dengesi
inline constexpr std::size_t kMinFamilyHoldoutSamples = 10; // aile kapsami
inline constexpr std::size_t kMinFamilyHoldoutMachines = 2; // aile kapsami: makine basina, ornek basina degil
// Uygun gercek holdout sayisi (kapi paydasi).
std::size_t eligible_real(const CalibrationStats& s) noexcept;

struct SplitIssue { std::string code; std::string id; };
// issues: engelleyici (ok=false, kapi split_leakage, arac cikis 5). notes: engellemeyen bilgi; ornek zaten
// kapiya uygun degildir (EF-X20 dogrulama bulgusu 4: v1/grupsuz gercek ornek sizinti DEGILDIR).
struct SplitIntegrity { bool ok = true; std::vector<SplitIssue> issues; std::vector<SplitIssue> notes; };
// T04: sizinti / yinelenmis (holdout) makine / karsi kumede veya baska makinede kopya pencere (issues);
// grupsuz gercek ornek (notes).
SplitIntegrity validate_split_integrity(const std::vector<LabeledWindow>& set);

struct ModelReport {
    std::string_view model;  // "compute" | "memory_storage" | "platform_limits"
    bool scored = false;     // false: girdiler veri setinde yok; sayi uretilmez
    std::size_t labeled = 0;
    CalibrationStats stats;
};
std::vector<ModelReport> evaluate_models(const std::vector<LabeledWindow>& set, Split which);
bool diagnosis_release_ready(const CalibrationStats& holdout) noexcept;

// BM-02: kapi durumu (hazir mi + neden anahtari). Sentetik-only holdout hazir sayilmaz.
ReleaseStatus release_status(const CalibrationStats& holdout);
ReleaseStatus release_status(const CalibrationStats& holdout, const SplitIntegrity& integrity);
// Uretim kapisi: etiketli gercek donanim holdout kumesi henuz toplanmadi; sentetik fikstur gercek
// dogruluk sayilmaz -> kapi kapali, siniflandirici yayinlanabilir sinif uretmez.
ReleaseStatus diagnosis_release_status();
inline constexpr std::string_view kComputeRulesVersion = "compute_v1";
// "compute_v1:uncalibrated" | "compute_v1:diagcal-<sha256 ilk 16>"; gecmis kayda yazilir.
std::string diagnosis_version_stamp();

// BM-02: uretim yolu — uretim kapisi kapaliyken calisma ani siniflandirmasi yapilmaz.
RunComputeVerdict classify_run_with_production_gate(const Timeline& tl, const RunComputeInputs& run,
                                                    const std::vector<WindowComputeExtras>& extras = {});

}  // namespace ef::core::diagnosis
