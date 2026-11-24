#pragma once
// DIAG-07: bilesen darbogaz raporu (saf; birim testlenir). Her bilesen icin kanitli sonuc veya cekimser
// (abstain). Kanitsiz kesin ifade yok; "darbogaz puani" yok; donanim arizasi iddiasi yok.
//  - CPU/GPU: yalniz calisma ani teshisinden (DIAG-02) gelir; teshis yoksa cekimser.
//  - Anakart: PCIe genislik/nesil dusumu (yuk altinda olculmus olmali), tek kanal supheli bellek.
//  - RAM: XMP/EXPO kapali gorunumu, kapasite yetersizligi (oyunun onerisine gore; oneri yoksa cekimser).
//  - Depolama: oyun HDD'de, dolu disk.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/platform.h"
#include "core/diagnosis/release_gate.h"
#include "core/insights/history.h"
#include "core/inventory/health.h"

namespace ef::core::diagnosis {

enum class Component { Cpu, Gpu, Motherboard, Ram, Storage };
std::string_view to_string(Component c) noexcept;

enum class Verdict { NoIssueFound, Limiting, Abstain };
std::string_view to_string(Verdict v) noexcept;

struct ComponentFinding {
    Component component;
    Verdict verdict = Verdict::Abstain;
    std::vector<std::string> evidence;  // kanit referanslari (bulgu kodu + olculen deger)
    std::string abstain_reason;         // cekimserse neden
    bool fixable_by_setting = false;    // BIOS/ayar ile duzeltilebilir (XMP, ReBAR) - fiziksel sinir degil
};

// Kullaniciya gosterilecek karar metni (diag.verdict_*). "Bu testte sinir gozlenmedi" yalniz calisma aninda olculduyse;
// envanter bulgusu ayri yazilir. "Olculmedi" (calisma verisi yok) ile "Belirlenemedi" (veri karara yetmiyor) ayridir.
std::string_view verdict_key(const ComponentFinding& f) noexcept;

// Calisma ani teshis ozeti (DIAG-02/03 ciktisinin indirgenmis hali). nullopt = calisma verisi yok.
enum class RuntimeClass { GpuBound, CpuOrEngineBound, FrameCap, Mixed, Healthy, Insufficient };

struct GameStorageInput {
    std::string game;
    bool on_hdd = false;
    std::optional<double> volume_free_pct;
};

struct ComponentInput {
    inventory::HealthInput inventory;                     // envanter (INV-06)
    std::vector<inventory::HealthFinding> health;         // evaluate_health ciktisi
    std::optional<RuntimeClass> runtime;                  // yuk altinda teshis
    // B10: yuk altinda gercek olcum var (en yeni sahne kanitli Uygulama Testi kaydi) ama sinif yok (yayin kapisi
    // kapali). CPU/GPU satiri "Olculmedi" yerine "olculdu, karar yok" + "diagnosis_calibration_unverified" der.
    bool runtime_measured = false;
    // BM-02: teshis yayin kapisi. Kapaliysa calisma ani teshisi CPU/GPU hukmune cevrilmez (cekimser +
    // "diagnosis_calibration_unverified" nedeni).
    bool diagnosis_release_ready = false;
    bool pcie_measured_under_load = false;                // PCIe degeri yuk altinda mi okundu
    // Spec 2026-11-12 C2: denge testinin S3 adiminda (GPU yuk altinda) okunan link ornegi; evaluate_pcie'ye verilir.
    // Varsa hukum BU ornekten gelir (bostaki envanter okumasi yuk alti olcumun yerine gecmez); `pcie_load_text`
    // dilden bagimsiz link metnidir ("x8 / x16, Gen 3 / Gen 4") ve kanit degeri olarak gosterilir. Yuva azamisi
    // okunamadiysa (ornekte bos) kural hukum vermez: yalniz yuk altinda kart azamisinin altinda olculen GENISLIK
    // sinirlama kanitidir (mevcut genislik kurali); hiz nesli tek basina sayilmaz.
    std::optional<PcieSample> pcie_load;
    std::string pcie_load_text;
    std::optional<std::uint64_t> ram_total_gib;
    std::optional<std::uint64_t> game_recommended_ram_gib;  // oyunun resmi onerisi (yoksa nullopt)
    std::vector<GameStorageInput> games;
};

std::vector<ComponentFinding> component_report(const ComponentInput& in);

// --- DIAG-11: Uygulama Testi sahnesi -> calisma ani sinifi ---
// Bilinen yuk (ekran disi sahne) altinda GPU/CPU kullanimini ve kare istatistigini tasir. Kare araliklari
// saklanmaz; kosucunun skorladigi ortalama/p99 yeterlidir (siniflandirici bunlari kullanir). Eksik alan
// "0" sayilmaz: siniflandirici o kaniti yok sayar ve gerekli veri yoksa cekimser kalir.
// MEAS-03: sayac ornekleme araligi > kMaxBenchmarkSampleIntervalMs ise kullanim degerleri kok neden
// kaniti sayilmaz (ortalama 1 Hz kacirmalarla dolar); siniflandirici cekimser kalir.
inline constexpr double kMaxBenchmarkSampleIntervalMs = 250.0;

struct BenchmarkSceneEvidence {
    std::optional<double> avg_fps;          // sahne skoru (zorunlu; yoksa cekimser)
    std::optional<double> p99_ms;           // kare suresi p99 (kararlilik kaniti)
    std::optional<double> gpu_util_pct;     // sahne penceresi 3D motor kullanimi (PDH, ~20 Hz)
    std::optional<double> cpu_max_core_pct; // sahne penceresi en yuklu cekirdek (PDH, ~20 Hz)
    std::optional<double> sample_interval_ms;  // penceredeki ortalama PDH ornek araligi (coznurluk kapisi)
    // DIAG-10: GPU zaman damgasi tabanli kare basi komut araligi (kendi isimizin ust siniri; yabanci
    // GPU isi dahil). Kare olcekli oldugu icin PDH coznurluk kapisina tabi degildir; yoksa kanit sayilmaz.
    std::optional<double> gpu_busy_ms;
};

struct RuntimeEvidenceResult {
    RuntimeClass cls = RuntimeClass::Insufficient;
    std::vector<std::string> supporting;  // kanit kodlari (compute ile ayni sozluk)
    std::vector<std::string> counter;     // karsit/eksik kanit kodlari
};

// Gecmis metriklerinden kanit cikarir (anahtarlar: scene_avg_fps, scene_p99_ms, scene_gpu_pct,
// scene_cpu_max_core_pct, scene_sample_interval_ms, scene_gpu_busy_ms). scene_avg_fps yoksa nullopt
// (eski/kosmayan kayit).
std::optional<BenchmarkSceneEvidence> benchmark_scene_evidence_from_metrics(const std::map<std::string, double>& metrics);

// B10: bilesen raporunun Uygulama Testi calisma kaniti (saf). Gecmis en yeniden eskiye siralidir. Sahne kaniti olan
// (benchmark_scene_evidence_from_metrics dolu) EN YENI "builtin" kaydi esastir; sahne kaniti olmayan kayitlar (eski
// kayit, sahne kosmadi / basarisiz, simge durumunda olculdu) atlanir. measured: boyle bir kayit var. cls: yalniz
// siniflandirici sinif verdiyse (yayin kapisi acik ve kanit yeterli); kapi kapaliyken her zaman bos (karar uydurulmaz).
// B10 inceleme: kayit BUGUNKU donanimla alinmis olmalidir (kosul "cpu", "gpu" ve "gpu_driver" bugunku envanterle ayni;
// kosul degerleri ilk kRuntimeConditionBytes bayttir). cpu/gpu bugun bilinmiyorsa ya da kayitta yoksa kayit kullanilmaz
// (baska islemci / ekran karti / surucuyle alinmis bayat kanit bugunun raporuna girmez).
struct BenchmarkRuntime {
    bool measured = false;
    std::optional<RuntimeClass> cls;
    friend bool operator==(const BenchmarkRuntime&, const BenchmarkRuntime&) = default;
};
inline constexpr std::size_t kRuntimeConditionBytes = 160;  // App::run_conditions kosul uzunlugu ile ayni
struct RuntimeHardware {
    std::string cpu, gpu, gpu_driver;
};
BenchmarkRuntime benchmark_runtime_from_history(const std::vector<insights::HistoryEntry>& newest_first, const ReleaseStatus& gate,
                                                const RuntimeHardware& current);
// B10 inceleme: olcum durumu ya da sinif degistiyse bilesen raporu yeniden toplanmalidir (ornek: ilk Uygulama Testi
// "Olculmedi" satirini "Olculdu, karar yok" yapar; gecmis sinirindan dusen kayit geri "Olculmedi" yapar).
inline bool benchmark_runtime_changed(const BenchmarkRuntime& before, const BenchmarkRuntime& after) { return !(before == after); }

// Sahne kanitini calisma ani sinifina cevirir. Yayin kapisi kapaliysa siniflandirici Insufficient
// dondurur; bu "sorun yok" demek degildir. FPS siniri yoktur (ekran disi sahne, VSync kapali).
RuntimeEvidenceResult runtime_class_from_benchmark_scene(const BenchmarkSceneEvidence& e, const ReleaseStatus& gate);

}  // namespace ef::core::diagnosis
