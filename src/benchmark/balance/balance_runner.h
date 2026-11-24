#pragma once
// Denge testi kosucusu (spec 2026-11-12 A2): "Arena v1" sahnesini tek cihazda adim adim kosar ve
// core::diagnosis::evaluate_balance girdilerini (BalanceInputs) uretir. Hukmu bu modul VERMEZ; karistirici
// girdileri (pil, guc plani, arka plan CPU'su, uretici siniri) cagiran doldurur ve kurali o cagirir.
//
// Adimlar (sabit sira):
//   S1  CPU kapasitesi   320x180            C    + Gmin   -> cpu_fps
//   S2-S4 GPU kapasitesi 1080p/1440p/2160p  Cmin + G      -> gpu_fps_r
//   K1-K3 birlikte       ayni uc cozunurluk C    + G      -> coupled_fps_r
//   T1, T2 tekrar        S1 ve S2 ile ayni ayar           -> suruklenme
// Tam: adim = 2 sn atilan isinma + 8 sn olcum (~90 sn). Hizli: 1 sn + 4 sn (~45 sn).
// Olcum penceresi kMeasurePadS kadar uzun acilir: istatistik yalniz TAM saniyeleri kullanir (eksik son saniye
// atilir) ve pencere tam 8 / 4 sn acilirsa son saniye kare zamanlamasina gore bir dolar bir dolmaz. Pay, 8 / 4
// tam saniyelik ornegi garanti eder; fazlasi atilir. Gecerlilik kurali nominal sureyle (8 / 4 sn) uygulanir.
//
//  - Her adim kendi zaman butcesiyle ayri alt testtir (ortak Watchdog; butce = isinma + olcum + hazirlik payi).
//    Butceyi asan adim "olculemedi (timeout)" olur, ayni cozunurlugun sonraki adimlari kosulmaz, digerleri surer.
//    S1 butceyi asarsa kosu "timeout" ile biter (hicbir satir hukum alamaz; kalan adimlar da asardi).
//  - Olcum ekran disidir (Flush, Present yok; mevcut GPU zaman damgasi zinciri). Adim FPS'i saniyelik FPS
//    medyanidir; gecerlilik kurali core::diagnosis::summarize_step'tedir. Seri: zaman damgalari gecerliyse GPU
//    tamamlama araligi, degilse gonderim araligi (StepResult.frame_source).
//  - Sinir dogrulamasi girdisi: gpu_busy_ratio = ort. GPU gecen suresi / ort. kare araligi. S1/T1 icin <= 0,60,
//    S2-S4/T2 icin >= 0,90 beklenir (StepResult.bound_confirmed); tutmayan deger kuralda "en az" sayilir.
//  - Cozunurluk hedefleri video bellegi butcesine sigmazsa ya da olusturulamazsa o adim "olculemedi" olur
//    (vram_budget / target_failed); diger adimlar surer. Butce asimi GPU siniri hukmu degildir.
//  - Kararsiz adim (Measured + Invalid, neden "unstable") BIR KEZ daha kosulur; kosu basina en cok
//    kBalanceRunMaxRetries tekrar (karar: core::diagnosis::should_retry_step). Her deneme kendi zaman butcesindedir,
//    iptal her an gecerlidir. SON deneme oldugu gibi kullanilir (iyisi secilmez); gecerlilik esigi degismez.
//    Deneme sayisi StepResult.attempts'ta, toplam tekrar BalanceReport.retries'ta (kosu kosulu) durur.
//  - Ilk adimdan once `before_steps` cagrilir (bagdastirici biliniyor, henuz yuk yok): cagiran burada test
//    bagdastiricisindaki BASKA sureclerin GPU yukunu ornekler (arka plan GPU karistiricisi).
//  - Iptal: kismi adim atilir, kalan adimlar kosulmaz (report.cancelled). Cihaz kaybi / GPU zaman asimi /
//    simulasyon saglamasi hatasi: yarim adim atilir, kosu report.error ile biter (hukum uretilmez).
//  - Yazilim bagdastiricisi (WARP) puanlanmaz: allow_software verilmedikce kosu "no_hardware_adapter" ile biter.
//  - Canli test ekrani (A4): FrameTap adim kimligi + canli sayilarla, PreviewCallback onizleme kareleriyle beslenir.
//    Onizleme acik/kapali durumu rapora yazilir (kosul); farkli durumlar karsilastirilmaz.
// Sayilar bu test sahnesinin olcumudur; oyun FPS tahmini degildir.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/scene/arena.h"
#include "benchmark/scene/d3d_renderer.h"
#include "benchmark/scene/scene.h"
#include "core/diagnosis/balance.h"

namespace ef::benchmark::balance {

// Kosu sirasi. Deger = sira indeksi (FrameTap.step_code = deger + 1).
enum class StepId : std::uint32_t { S1, S2, S3, S4, K1, K2, K3, T1, T2 };
inline constexpr std::size_t kStepCount = 9;
enum class StepKind { CpuCapacity, GpuCapacity, Coupled, CpuRepeat, GpuRepeat };
std::string_view to_string(StepId id) noexcept;      // "S1" ... "T2"
std::string_view to_string(StepKind kind) noexcept;  // "cpu_capacity" | "gpu_capacity" | "coupled" | "cpu_repeat" | "gpu_repeat"

struct StepTiming {
    double warmup_s = 0, measure_s = 0;
};
inline constexpr StepTiming kFullTiming{2.0, 8.0};   // Tam
inline constexpr StepTiming kQuickTiming{1.0, 4.0};  // Hizli
// Olcum penceresi payi (yukariya bakin): SceneConfig.measure_s = measure_s + kMeasurePadS.
inline constexpr double kMeasurePadS = 0.25;
// Adim zaman butcesi (bekci): isinma + olcum + pay + hazirlik / son bosaltma payi.
inline constexpr double kStepOverheadBudgetS = 20.0;
double step_budget_s(const StepTiming& t) noexcept;
// Bir adimin planlanan suresi (isinma + olcum + pay; hazirlik haric) ve dokuz adimin toplami. UI ilerleme / sure
// tahmini icin.
double planned_step_seconds(const StepTiming& t) noexcept;
double planned_seconds(const StepTiming& t) noexcept;

struct Resolution {
    std::uint32_t width = 0, height = 0;
};
inline constexpr Resolution kCpuStepResolution{320, 180};
inline constexpr std::array<Resolution, core::diagnosis::kBalanceRows> kRowResolutions{{{1920, 1080}, {2560, 1440}, {3840, 2160}}};

struct StepPlan {
    StepId id = StepId::S1;
    StepKind kind = StepKind::CpuCapacity;
    const ArenaCpuProfile* cpu = nullptr;
    const ArenaGpuProfile* gpu = nullptr;
    int row = -1;        // cozunurluk satiri (0: 1080p, 1: 1440p, 2: 2160p); S1/T1 icin -1
    SceneConfig config;  // profil cifti + cozunurluk + pencere (isinma / olcum + pay) + tohum
};

struct StepProgress;

struct BalanceOptions {
    bool quick = false;                    // Hizli (1 + 4 sn) / Tam (2 + 8 sn)
    std::uint64_t seed = SceneConfig{}.seed;
    std::uint32_t logical_cores = 0;       // 0 -> std::thread::hardware_concurrency()
    FrameTap* frame_tap = nullptr;         // canli sayilar (A4); nullptr = yok
    const PreviewCallback* preview = nullptr;  // canli onizleme (A4); nullptr = uretilmez
    // Adim basinda (finished = false) ve sonunda (finished = true, result dolu) kosucu is parcacigindan cagrilir.
    std::function<void(const StepProgress&)> progress;
    // Cihaz acildiktan sonra, ilk adimdan ONCE kosucu is parcacigindan bir kez cagrilir (yazilim bagdastiricisi
    // reddedildiyse cagrilmaz). Uzun surmemelidir (~1 sn); `cancel` kalkarsa hemen donmelidir.
    std::function<void(const AdapterInfo&, const std::atomic<bool>& cancel)> before_steps;
    // Uyumluluk B3 (bosluk 25): olculmus her denemeden sonra kosucu is parcacigindan [olcum basi, olcum sonu] ile
    // cagrilir; true -> pencere bu aralikta simge durumundaydi ve deneme Invalid(window_minimized) sayilir (tekrar
    // edilmez). Bos -> denetim yok.
    std::function<bool(std::chrono::steady_clock::time_point, std::chrono::steady_clock::time_point)> window_minimized_during;
    // P1 (ABBA kanit deneyi): doluysa YALNIZ bu adim kosulur (digerleri NotRun kalir, kosu yine ok sayilir); plan parmak izi
    // dokuz adimin tamamina bakar, bu yuzden ayni secenekle her kosu ayni kimligi tasir. Bos = dokuz adim (urun varsayilani).
    std::optional<StepId> only_step;
    // P1: dolu ise olculen adimin sureli kare araliklari StepResult::scored_ms'e kopyalanir (%1 dusuk / p99 icin). Varsayilan kapali.
    bool keep_scored_frames = false;
    bool prefer_hardware = true;
    // ---- Yalniz test / deneme (urun kodu bunlari vermez) ----
    // Uyumluluk B9 (bosluk 24): >= 0 -> yuksek performans sirasindaki o donanim bagdastiricisi (real_gpu ctest her
    // bagdastiricida kosar). -1 = urun secimi.
    int hardware_adapter_index = -1;
    bool allow_software = false;                      // WARP ile kos (puanlanmaz; report.software = true)
    std::optional<StepTiming> timing_override;        // kisa adimlar
    std::optional<Resolution> cpu_resolution_override;
    std::optional<std::array<Resolution, core::diagnosis::kBalanceRows>> row_resolution_override;
    std::uint64_t vram_budget_limit = 0;              // 0 = yok; butce ust siniri (bayt)
    int vram_limit_row = -1;                          // >= 0: sinir yalniz bu satirin adimlarinda uygulanir
    std::optional<double> step_budget_override_s;     // adim zaman butcesi (sn); bos = step_budget_s(pencere)
    int step_budget_row = -1;                         // >= 0: butce gecersiz kilma yalniz bu satirin adimlarinda
    // Tekrar yolunun islev denetimi (WARP): neden ne olursa olsun Invalid adim tekrar edilir (ayni ust sinirlarla).
    bool retry_any_invalid = false;
};

// P1: bu adim secili mi (only_step bos ise hepsi).
bool step_selected(const BalanceOptions& o, StepId id) noexcept;
// Secenekler -> gecerli pencere (override varsa o).
StepTiming effective_timing(const BalanceOptions& o) noexcept;
// Dokuz adimin plani (saf; birim testlenir). logical_cores 0 ise 1 sayilir (cagiran cozmelidir).
std::array<StepPlan, kStepCount> balance_plan(const BalanceOptions& o, std::uint32_t logical_cores) noexcept;
// Plan kimligi: sahne + simulasyon surumu, kip, pencere ve dokuz adimin is yuku ozetlerinin katlanmasi. Ayni
// secenek + ayni cekirdek sayisi -> ayni dize; profil, cozunurluk, pencere ya da tohum degisirse degisir.
std::string plan_fingerprint(const BalanceOptions& o, std::uint32_t logical_cores);

enum class StepStatus {
    NotRun,        // kosu daha once bitti (iptal / hata) ya da ayni cozunurlugun onceki adimi zaman asimina ugradi
    Measured,      // kosuldu; stats Valid ya da Invalid (neden anahtariyla)
    Unmeasurable,  // hedefler kurulamadi / video bellegi butcesi yetmedi
    TimedOut,      // adim zaman butcesini asti
    Cancelled,     // kullanici iptali (kismi adim atildi)
    Failed,        // cihaz kaybi / GPU zaman asimi / dogrulama hatasi: kosu burada biter
};
std::string_view to_string(StepStatus s) noexcept;

struct StepResult {
    StepPlan plan;
    StepStatus status = StepStatus::NotRun;
    std::string error;                    // renderer hata anahtari (status Measured degilse)
    core::diagnosis::StepStats stats;     // kural girdisi (NotRun / Unmeasurable / Invalid / Valid)
    // Sinir dogrulamasi (yalniz S ve T adimlari; K adimlarinda bos): GPU mesgul orani beklenen tarafta mi.
    std::optional<bool> bound_confirmed;
    double seconds = 0;                   // adimin duvar suresi (hazirlik dahil; tekrar varsa denemelerin toplami)
    std::uint32_t attempts = 0;           // kac kez kosuldu (0: kosulmadi; 2: kararsiz cikti ve bir kez tekrarlandi)
    // ---- Olcum ayrintisi (status == Measured) ----
    FrameSource frame_source = FrameSource::SubmitCadence;  // FPS hangi seriden
    std::uint32_t warmup_frames = 0, measured_frames = 0;
    double warmup_s = 0, measure_s = 0;   // gerceklesen pencereler
    double frame_ms = 0;                  // ort. kare araligi (gonderimden gonderime)
    double gpu_ms = 0;                    // ort. GPU gecen suresi (zaman damgasi yoksa 0)
    double wait_ms = 0, sim_ms = 0, cull_ms = 0, submit_ms = 0;  // kare basi ortalamalar
    std::optional<double> wait_share;     // GPU'yu bekleme / kare araligi, [0,1]
    std::uint32_t sim_workers = 0;
    std::uint32_t object_draws = 0;       // kare basina cizilen nesne (olculen karelerde en cok)
    std::uint64_t workload_hash = 0, pixel_checksum = 0;
    bool gpu_timestamps = false;
    bool preview = false;                 // bu adimda onizleme uretildi mi
    std::uint32_t preview_frames = 0;
    double preview_ms = 0;                // olcum penceresinde onizleme okuma + geri cagri toplam suresi
    std::vector<double> scored_ms;        // BalanceOptions::keep_scored_frames: olcum penceresi kare araliklari (ms)
    std::vector<double> second_fps;       // tam saniye basina FPS (stats.fps bunlarin medyani; tani / kanit icin)
    std::chrono::steady_clock::time_point measure_begin{}, measure_end{};  // dis ornekleyiciler icin
};

struct StepProgress {
    StepId id = StepId::S1;
    std::uint32_t index = 0, total = 0;  // 1'den
    Resolution resolution;
    bool finished = false;
    const StepResult* result = nullptr;  // finished iken dolu; yalniz geri cagri suresince gecerli
};

struct BalanceReport {
    bool ok = false;         // dokuz adimin hepsi denendi (tek tek olculemeyen / gecersiz adim olabilir)
    bool cancelled = false;
    std::string error;       // kosuyu bitiren hata ("no_hardware_adapter", "device_removed", "gpu_timeout", ...)
    AdapterInfo adapter;     // cizen bagdastirici (not olarak gosterilir)
    bool software = false;   // WARP / yazilim bagdastiricisi: PUANLANMAZ
    bool quick = false;
    StepTiming timing;
    std::uint32_t logical_cores = 0;
    bool preview = false;    // kosul: en az bir adimda onizleme uretildi
    std::uint32_t retries = 0;  // kosul: kararsiz adim tekrar sayisi (0..kBalanceRunMaxRetries)
    std::string fingerprint;
    std::array<StepResult, kStepCount> steps;
    core::diagnosis::BalanceInputs inputs;  // karistiricilar varsayilan (cagiran doldurur)
    double seconds = 0;
    const StepResult& step(StepId id) const noexcept { return steps[static_cast<std::size_t>(id)]; }
};

// Bir adimin sahne sonucundan kural girdisi (saf; birim testlenir): olculduyse summarize_step, hedef / butce
// hatasinda unmeasurable_step, olcum karesi yoksa Invalid(too_few_frames).
// `measure_s` NOMINAL olcum suresidir (pay haric).
// `needs_gpu_time`: adimin degeri GPU suresine dayaniyorsa true (zaman damgasi gecersiz kare payi adimi gecersiz kilar);
// S1/T1 (CPU kapasitesi) icin false (denge_v4).
core::diagnosis::StepStats step_stats_from(const SceneRunResult& r, double measure_s, bool needs_gpu_time);
// S1/T1 disindaki adimlar GPU suresine dayanir.
bool needs_gpu_time(StepKind kind) noexcept;
// Adim sonuclarindan kural girdileri (saf): S1 -> cpu, S2-S4 -> gpu, K1-K3 -> coupled, T1/T2 -> tekrar,
// S1 kare basi CPU sureleri -> cpu_split. Karistiricilar ve cpu_speed_drop varsayilan kalir.
core::diagnosis::BalanceInputs build_inputs(const std::array<StepResult, kStepCount>& steps);

// Tum diziyi kosar. Kosucu is parcaciginda cagrilir (UI is parcaciginda degil); `cancel` her an kaldirilabilir.
BalanceReport run_balance(const BalanceOptions& o, const std::atomic<bool>& cancel);

}  // namespace ef::benchmark::balance
