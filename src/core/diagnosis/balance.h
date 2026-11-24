#pragma once
// Denge testi (spec 2026-11-12 A2/A3): adim istatistigi ve "denge_v1" sonuc kurali (saf; birim testlenir).
//  - Girdi yalniz OLCULEN adim sonuclaridir (S1 CPU kapasitesi, S2-S4 GPU kapasitesi, K1-K3 birlikte, T1/T2
//    tekrar). Bu modul D3D, saat veya sistem cagrisi icermez.
//  - Kural dogrudan neden-sonuc olcumudur: istatistiksel siniflandiricinin (compute.h) kalibrasyon kapisina
//    bagli DEGILDIR ve kendi surumunu (kBalanceRuleVersion) tasir. Esikler burada adlandirilmis sabittir;
//    compute.h sabitleri degisse de denge_v1 sessizce degismez.
//  - Sonuc yalniz "bu test sahnesinde" gecerlidir. Yuzde/"darbogaz orani" uretilmez; model adina gore
//    yargi yoktur. Her cekimserlik TEK somut neden anahtari tasir (UI katalog anahtarina cevrilir).
//  - Olculen her FPS, ilgili tarafin kapasitesinin alt siniridir. Sinir dogrulamasi (gpu_busy_ratio) tutmazsa
//    deger "en az" isaretlenir ve yalniz hukmu guclendirdigi yonde kullanilir.
//  - Kaynaklar: research/lb2.md bolum 2-3 (gurultu tabani %3; bantlar gurultunun disinda secildi).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/diagnosis/platform.h"

namespace ef::core::diagnosis {

// Surum kurali: asagidaki esikler ve kural mantigi, "denge_v1" iceren ILK PAKETLENMIS (dagitilan) derlemeden itibaren
// dondurulur. O andan sonra herhangi bir esik ya da kural degisikligi surumu arttirir (denge_v2 ...); farkli surumle
// uretilmis kayitlar birbiriyle karsilastirilmaz. Ilk paketten ONCE (gelistirme / ilk gercek cihaz turu) yapilan
// duzeltmeler ad degistirmez: henuz hicbir kullanicida denge_v1 kaydi yoktur (bkz. kBalanceCoupledCpuMaxGpuBusy,
// 2026-11-12 duzeltmesi).
// denge_v2 (2026-11-14, saha raporu: Ryzen 5 5600 + RX 570 + 240 Hz ekranda her kosu "belirlenemedi"):
//  - karistirici CPU/GPU olcumunde Windows masaustu bilesimcisi (dwm.exe, kendi oturumumuz) ve donanim kesmesi /
//    DPC suresi "baska program" sayilmaz (testin kendisinin tetikledigi sistem isi; kullanicinin kapatabilecegi bir
//    program degil);
//  - zaman damgasi gecersiz kare: adim yalniz karelerin %1'inden fazlasi gecersizse gecersiz (onceden tek kare
//    yetiyordu) ve bu neden de bir kez tekrar denenir.
// denge_v3 (2026-11-14, uyumluluk B4):
//  - NVIDIA isi/guc bayragi tek ornekle tum kosuyu iptal etmez: drops.cpp ile ayni kural (bayrak yuk alti pencerelerin
//    >= %25'inde VE yuk alti saat referansin %90'inin altinda) tutarsa karistiricidir, yoksa yalniz not; yuk ya da
//    saat okunamadigi icin kural degerlendirilemezse ihtiyatli karistirici (kVendorLimitUnverified, denge_v2 gibi);
//  - Windows guc modu "en iyi guc verimliligi" (etkin overlay) ayri bir karistiricidir.
// denge_v4 (2026-11-17, saha raporu: AMD ekran kartli arkadaslarda "CPU kapasitesi adimi gecerli olcum vermedi"):
//  - zaman damgasi gecersiz kare kurali yalniz GPU suresine dayanan adimlara uygulanir. S1/T1 (CPU kapasitesi) FPS'ini
//    CPU tarafi gonderim ritminden alir; GPU zamani yalniz gpu_busy_ratio / sinir dogrulamasi icindir. Bu adimlarda
//    gecersiz kare payi esigi asarsa adim gecerli kalir, gpu_busy_ratio BOS birakilir (guvenilmeyen GPU zamani
//    kullanilmaz) ve CPU degeri "en az" (alt sinir) isaretlenir;
//  - CPU adimi gecersizse asil neden (cpu_step_reason / cpu_repeat_reason) kayda ve karta yazilir.
inline constexpr std::string_view kBalanceRuleVersion = "denge_v4";

// ---- Esikler (surumlenir; testler bunlara dayanir) ----
// Bant: oran = gpu_fps_r / cpu_fps.
inline constexpr double kBalanceCpuLimitedRatio = 1.15;  // oran >= : CPU sinirliyor
inline constexpr double kBalanceGpuLimitedRatio = 0.87;  // oran <= : GPU sinirliyor
// Birlikte dogrulama: coupled_fps_r / min(cpu_fps, gpu_fps_r) bu aralikta (uclar dahil) olmali.
inline constexpr double kBalanceCoupledMinRatio = 0.85;
inline constexpr double kBalanceCoupledMaxRatio = 1.10;
// Sinir dogrulamasi: gpu_busy_ratio = GPU calisma suresi / kare araligi.
inline constexpr double kBalanceCpuStepMaxGpuBusy = 0.60;  // S1/T1 (CPU kapasitesi adimi): <=
inline constexpr double kBalanceGpuStepMinGpuBusy = 0.90;  // S2-S4/T2 ve "GPU" hukmunde K adimi: >=
// "CPU" hukmunde K (birlikte) adimi: GPU DOYMAMIS olmali, yani gpu_busy_ratio < bu deger (uc haric). Kapasite adimi
// esigi (0,60) burada kullanilamaz: CPU bandinda GPU her kareyi 1 / gpu_fps surede cizer, kare araligi ise
// 1 / cpu_fps'tir; birlikte adimda beklenen GPU mesgul orani ~ 1 / oran olur. 1,15 <= oran < 1,67 icin bu 0,60..0,87
// araligidir ve "<= 0,60" hicbir zaman tutmazdi (gercek cihaz: oran 1,30, olculen 0,73..0,74 -> hukum cikmiyordu).
inline constexpr double kBalanceCoupledCpuMaxGpuBusy = 0.90;
// Suruklenme: tekrar ile ilk olcum arasindaki fark (1 - kucuk/buyuk) bundan BUYUKSE deger araliktir.
inline constexpr double kBalanceDriftTolerance = 0.03;
// Adim gecerliligi.
inline constexpr std::size_t kBalanceStepMinFrames = 30;
inline constexpr double kBalanceStepMaxCv = 0.05;           // saniyelik FPS degisim katsayisi <=
inline constexpr double kBalanceStepSecondsSlack = 1.0;     // saniyelik ornek >= olcum suresi - 1
inline constexpr std::size_t kBalanceStepMinSeconds = 2;    // CV icin en az iki saniyelik ornek gerekir
// denge_v2: zaman damgasi gecersiz (disjoint / okunamayan) kare payi bu yuzdeyi ASARSA adim gecersiz. Gecersiz kareler
// GPU mesgul oranina zaten girmez; tek bir surucu saat atlamasi tum kosuyu "belirlenemedi" yapmaz.
// denge_v4: kural yalniz GPU suresine dayanan adimlara (gpu_time_required) uygulanir; CPU kapasitesi adiminda (S1/T1)
// esik asilirsa adim gecerli kalir ve gpu_busy_ratio bos birakilir.
inline constexpr double kBalanceStepMaxDisjointPct = 1.0;
// Karistirici: olcum sirasinda baska sureclerin CPU kullanimi (yuzde) >= ise tum satirlar cekimser.
inline constexpr double kBalanceOtherCpuMaxPct = 10.0;
// Karistirici: ilk adimdan ONCE (~1 sn) test bagdastiricisinda BASKA sureclerin 3D motor kullanimi (yuzde) >= ise tum
// satirlar cekimser. Gercek cihaz (RTX 5080): baska bir surec GPU'yu surekli yuklerken S3/S4 yari hizda ve dusuk
// CV ile olculdu; gecerlilik kurali bunu yakalayamaz (kararli ama yanlis). Bu yuzden ayri karistiricidir.
inline constexpr double kBalanceOtherGpuMaxPct = 10.0;
// Kararsiz adim icin sinirli tekrar (kosucu): adim basina en cok kBalanceStepMaxAttempts deneme (ilk + 1 tekrar),
// kosu basina en cok kBalanceRunMaxRetries tekrar. Gecerlilik esigi (kBalanceStepMaxCv) DEGISMEZ; SON deneme
// oldugu gibi kullanilir (ikisinin iyisi secilmez). Gercek cihaz: derleme / paketleme hemen sonrasinda S1/K1 CV
// 0,054-0,086 ile gecersiz cikti; sakin sistemde ayni adimlar CV ~0,018 ve ayni sayilari verdi.
inline constexpr std::uint32_t kBalanceStepMaxAttempts = 2;
inline constexpr std::uint32_t kBalanceRunMaxRetries = 2;

// Cozunurluk satirlari (sabit sira: 1080p, 1440p, 2160p).
inline constexpr std::size_t kBalanceRows = 3;
inline constexpr std::array<std::string_view, kBalanceRows> kBalanceRowKeys = {"1080", "1440", "2160"};

// Neden anahtarlari (insan metni degil; UI bunlari katalog anahtarina cevirir).
namespace balance_reason {
// Adim gecersizligi.
inline constexpr std::string_view kTooFewFrames = "too_few_frames";
inline constexpr std::string_view kTooFewSeconds = "too_few_seconds";
inline constexpr std::string_view kUnstable = "unstable";
inline constexpr std::string_view kTimestampDisjoint = "timestamp_disjoint";
// Uyumluluk B3 (bosluk 25): olcum penceresinde uygulama penceresi simge durumundaydi (Windows sureci EcoQoS'a /
// verimlilik cekirdeklerine alabilir): adim gecersiz sayilir, sonuc uydurulmaz. Kosucu doldurur; tekrar edilmez.
inline constexpr std::string_view kWindowMinimized = "window_minimized";
// Adim olculemedi (kosucu doldurur).
inline constexpr std::string_view kVramBudget = "vram_budget";
inline constexpr std::string_view kTargetFailed = "target_failed";
inline constexpr std::string_view kTimeout = "timeout";  // adim zaman butcesini asti
inline constexpr std::string_view kNotRun = "not_run";
// Tum satirlar cekimser.
inline constexpr std::string_view kOnBattery = "on_battery";
inline constexpr std::string_view kBatterySaver = "battery_saver";
inline constexpr std::string_view kPowerSaverPlan = "power_saver_plan";
inline constexpr std::string_view kEfficiencyOverlay = "efficiency_overlay";  // denge_v3: guc modu "en iyi verimlilik"
inline constexpr std::string_view kVendorThrottle = "vendor_throttle";
// denge_v3 (inceleme B4): surucu isi/guc bayragi goruldu ama yuk ya da saat okunamadigi icin kosu kurali
// degerlendirilemedi. Saat dususu olmadigi iddia edilemez; ihtiyatli olarak tum satirlar cekimser (denge_v2 gibi).
inline constexpr std::string_view kVendorLimitUnverified = "vendor_limit_unverified";
// P6: test tumlesik (dusuk guclu) kartta calisti ve sistemde ayrik kart var: sonuc o bilgisayarin oyun gucunu gostermez.
inline constexpr std::string_view kRanOnIntegratedGpu = "ran_on_integrated_gpu";
inline constexpr std::string_view kBackgroundCpu = "background_cpu";
inline constexpr std::string_view kBackgroundGpu = "background_gpu";  // baska surecler test bagdastiricisini yukluyordu
inline constexpr std::string_view kCpuStepInvalid = "cpu_step_invalid";
inline constexpr std::string_view kCpuRepeatInvalid = "cpu_repeat_invalid";
// Satir cekimser.
inline constexpr std::string_view kGpuStepInvalid = "gpu_step_invalid";
inline constexpr std::string_view kCoupledStepInvalid = "coupled_step_invalid";
inline constexpr std::string_view kGpuRepeatInvalid = "gpu_repeat_invalid";
inline constexpr std::string_view kDrift = "drift";
inline constexpr std::string_view kCpuLowerBound = "cpu_lower_bound";
inline constexpr std::string_view kGpuLowerBound = "gpu_lower_bound";
inline constexpr std::string_view kCoupledMismatch = "coupled_mismatch";
inline constexpr std::string_view kCoupledBusyMismatch = "coupled_busy_mismatch";
inline constexpr std::string_view kCoupledBusyUnknown = "coupled_busy_unknown";
}  // namespace balance_reason

// Not anahtarlari (hukmu engellemez).
namespace balance_note {
inline constexpr std::string_view kCpuSpeedDrop = "cpu_speed_drop";            // C1: CPU yuk altinda hiz dusurdu
// C1 ek kanit: olay gunlugunde son acilistan beri "bellenim islemci hizini sinirliyor" kaydi (Kernel-Processor-Power
// 37) var. Yalniz kCpuSpeedDrop ile birlikte yazilir; tek basina yuk alti olcum degildir.
inline constexpr std::string_view kCpuFirmwareLimitLogged = "cpu_firmware_limit_logged";
// denge_v3: surucu isi/guc bayragi goruldu, yuk altinda saat okundu ama kosu duzeyi kurali (pencere payi + saat
// dususu) tutmadi. Yuk/saat okunamadiysa bu not yazilmaz (balance_reason::kVendorLimitUnverified).
inline constexpr std::string_view kVendorLimitFlag = "vendor_limit_flag";
// Uyumluluk B8 (bosluk 17): cizen kart AMD/Intel; bu ureticiler icin surucu isi/guc/saat siniri okuyan bir API
// baglanmadi (ADLX/IGCL yok). Sinir kaniti "yok" degil "desteklenmiyor"dur; hukmu engellemez.
inline constexpr std::string_view kVendorLimitNoApi = "vendor_limit_no_api";
inline constexpr std::string_view kBackgroundCpuUnknown = "background_cpu_unknown";  // diger surec CPU'su okunamadi
inline constexpr std::string_view kBackgroundGpuUnknown = "background_gpu_unknown";  // diger surec GPU'su okunamadi
inline constexpr std::string_view kCpuDrift = "cpu_drift";                     // T1/S1 farki > %3 (aralik)
inline constexpr std::string_view kGpuDrift = "gpu_drift";                     // T2/S2 farki > %3 (aralik)
}  // namespace balance_note

// ---- Adim istatistigi (A2) ----

// Saniyelik FPS: olcum penceresi [k, k+1) saniye dilimlerine bolunur; her dilimin degeri o dilime dusen kare
// sayisidir (sinir asan kare, sureyle orantili paylastirilir). Eksik son saniye atilir. Sabit kare akisinda
// kuantalama gurultusu uretmez (dusuk FPS'te tam sayi sayimi CV'yi sisirirdi). Gecersiz aralik (NaN, sonsuz,
// <= 0) atlanir.
std::vector<double> per_second_fps(std::span<const double> frame_ms);

// Medyan: cift sayida ornekte ortadaki iki degerin ortalamasi. Bos dizi -> 0.
double median_of(std::vector<double> values);
// Degisim katsayisi = ornek standart sapmasi (n-1) / ortalama. n < 2 veya ortalama <= 0 -> nullopt.
std::optional<double> coefficient_of_variation(std::span<const double> values) noexcept;

// Isinma atimi: adim basindan itibaren kare araliklari verilir; baslangici [warmup_s, warmup_s + measure_s)
// icinde olan kareler olcumdur (scene_phase ile ayni karar: kare GONDERILMEDEN once gecen sure).
// Donen aralik frame_ms icindeki [first, last) indeksleridir. measure_s <= 0 -> isinmadan sonraki tum kareler.
struct FrameWindow {
    std::size_t first = 0, last = 0;
    std::size_t size() const noexcept { return last - first; }
};
FrameWindow measured_window(std::span<const double> frame_ms, double warmup_s, double measure_s) noexcept;

enum class StepState { NotRun, Unmeasurable, Invalid, Valid };

struct StepStats {
    StepState state = StepState::NotRun;
    // Invalid/Unmeasurable/NotRun nedeni; Valid iken bos. Yalniz balance_reason sabitleri (statik omurlu)
    // atanir: gorunum BalanceRow/BalanceResult icine kopyalanir ve adimdan uzun yasar.
    std::string_view reason;
    std::size_t frames = 0;   // olcum penceresindeki gecerli kare araligi
    std::size_t seconds = 0;  // saniyelik ornek sayisi
    double fps = 0;           // saniyelik FPS medyani (seconds == 0 iken 0; sahte deger yok)
    double cv = 0;            // saniyelik FPS degisim katsayisi (seconds < 2 iken 0)
    std::optional<double> gpu_busy_ratio;  // GPU calisma suresi / kare araligi, [0,1]; olculmediyse bos
    std::uint32_t disjoint_frames = 0;     // olcum penceresinde zaman damgasi disjoint kare
};

// Gecerlilik kurali: >= 30 kare, >= max(2, olcum suresi - 1) saniyelik ornek, disjoint kare yok, CV <= 0,05.
// Ilk tutmayan kosulun neden anahtari doner; hepsi tutuyorsa bos.
// `gpu_time_required`: adimin degeri GPU suresine dayaniyorsa true (S2-S4/K/T2); CPU kapasitesi adiminda (S1/T1) false
// ve zaman damgasi gecersiz kare payi adimi gecersiz yapmaz.
std::string_view step_invalid_reason(std::size_t frames, std::size_t seconds, double cv, std::uint32_t disjoint_frames,
                                     double measure_s, bool gpu_time_required) noexcept;

// GPU mesgul orani = ort. GPU calisma suresi / ort. kare araligi, [0,1]'e kirpilir. Dizilerden biri bossa
// veya ortalama aralik <= 0 ise bilinmiyor.
std::optional<double> gpu_busy_ratio(std::span<const double> gpu_busy_ms, std::span<const double> frame_ms) noexcept;

// Olcum penceresinin (isinma ATILMIS) kare araliklari ve ayni penceredeki kare basi GPU calisma surelerinden
// adim ozeti. state Valid ya da Invalid doner (neden anahtariyla).
// `gpu_time_required` false iken disjoint payi esigi asarsa adim gecerli kalir, gpu_busy_ratio bos olur.
StepStats summarize_step(std::span<const double> measured_frame_ms, std::span<const double> gpu_busy_ms, double measure_s,
                         std::uint32_t disjoint_frames, bool gpu_time_required);
// Olculemeyen adim (video bellegi butcesi / hedef olusturulamadi). Bos neden -> kNotRun.
StepStats unmeasurable_step(std::string_view reason) noexcept;
// Olcum penceresinde pencere simge durumundaysa (Uyumluluk B3) olculmus adim Invalid(kWindowMinimized) olur; kare
// sayilari / FPS tani icin kalir ama kural Valid saymaz. Olculemeyen / kosulmayan adim degismez.
StepStats flag_window_minimized(StepStats stats, bool minimized) noexcept;

// Kararsiz adim tekrar karari (saf): adim Invalid + neden "unstable" ise, bu adim icin deneme sayisi
// kBalanceStepMaxAttempts'in ve kosudaki toplam tekrar kBalanceRunMaxRetries'in altindaysa adim BIR KEZ daha kosulur.
// Baska gecersizlik nedenleri (az kare, kisa pencere, disjoint) tekrar edilmez: onlar gurultu degil yapisal kosuldur.
// `attempts`: bu adim icin tamamlanan deneme sayisi (ilk kosudan sonra 1). `run_retries`: kosuda yapilmis tekrar.
bool should_retry_step(const StepStats& stats, std::uint32_t attempts, std::uint32_t run_retries) noexcept;

// ---- Sinir dogrulamasi (A2) ----

enum class StepRole { CpuCapacity, GpuCapacity };
// CpuCapacity (S1/T1): gpu_busy_ratio <= 0,60 ise kapasite dogrulanir. GpuCapacity (S2-S4/T2): >= 0,90.
// Oran bilinmiyorsa dogrulanmaz. Dogrulanmayan deger "en az" (alt sinir) isaretlenir.
bool bound_confirmed(StepRole role, const std::optional<double>& gpu_busy_ratio) noexcept;

// ---- Sonuc kurali (A3) ----

struct BalanceConfounders {
    bool on_battery = false;
    bool battery_saver = false;
    bool power_saver_plan = false;
    bool ran_on_integrated_gpu = false;   // P6: cizen kart tumlesik, sistemde ayrik kart da var (okunamadiysa false)
    bool efficiency_overlay = false;      // denge_v3: etkin guc modu "en iyi guc verimliligi" (okunamadiysa false)
    // denge_v3: kosu duzeyi kural (balance_vendor_limit == Throttle). Tek ornekteki bayrak bunu dolduramaz.
    bool vendor_throttle = false;
    bool vendor_limit_flag = false;       // bayrak goruldu ama kural tutmadi: yalniz not (balance_vendor_limit == FlagOnly)
    bool vendor_limit_unverified = false;  // bayrak goruldu, yuk/saat yetersiz: cekimser (balance_vendor_limit == Unverifiable)
    bool vendor_limit_no_api = false;      // Uyumluluk B8: cizen kartin ureticisi icin sinir API'si yok (yalniz not)
    std::optional<double> other_cpu_pct;  // olcum sirasinda baska sureclerin CPU yuzdesi; okunamadiysa bos
    // Ilk adimdan once, test bagdastiricisinda BASKA sureclerin 3D motor kullanimi (yuzde); okunamadiysa bos.
    std::optional<double> other_gpu_pct;
};

// denge_v3: olcum pencerelerine dusen NVIDIA surucu orneklerinden (bayrak + yuk + saat) kosu duzeyi karar. Kural
// drops.cpp analyze_gpu_limits ile aynidir (evaluate_limits): en az 2 yuk alti pencere (yuk >= kGpuLoadPct), isi
// ya da guc bayragi bu pencerelerin >= kLimitWindowShare'inde VE en az bir yuk alti pencerede saat, ayni kosuda yuk
// altinda gorulen en yuksek saatin kClockDropRatio'su altinda -> Throttle. Bayrak goruldu ama bu tutmadi -> FlagOnly
// (not). Hicbir ornekte isi/guc bayragi yoksa None. Bayrak goruldu ama kural degerlendirilemedi (yuk alti pencere < 2
// ya da saati okunan yuk alti pencere < 2) -> Unverifiable: saat dususu olmadigi iddia edilmez, karistirici sayilir.
enum class VendorLimit { None, FlagOnly, Throttle, Unverifiable };
// Uyumluluk B8 (bosluk 17): surucu sinir telemetrisi (isi/guc bayragi, saat) bu PCI ureticisi icin desteklenmiyor mu?
// Yalniz NVIDIA (NVAPI) bagli; AMD ve Intel icin uretici API'si yok. Diger / bilinmeyen uretici iddia edilmez (false).
constexpr bool vendor_limit_api_missing(std::uint32_t vendor_id) noexcept { return vendor_id == 0x1002 || vendor_id == 0x8086; }
VendorLimit balance_vendor_limit(const std::vector<LimitSample>& windows);

// S1'de kare basi ortalama CPU sureleri (not icin; hukme girmez).
struct CpuFrameSplit {
    double sim_ms = 0;     // isci is parcaciklari: simulasyon (cizim is parcaciginin bekledigi sure)
    double cull_ms = 0;    // cizim is parcacigi: ayiklama + siralama
    double submit_ms = 0;  // cizim is parcacigi: tampon guncelleme + cizim cagrilari
};

struct BalanceInputs {
    StepStats cpu;                                // S1
    std::array<StepStats, kBalanceRows> gpu;      // S2, S3, S4
    std::array<StepStats, kBalanceRows> coupled;  // K1, K2, K3
    StepStats cpu_repeat;                         // T1 (S1 ile ayni ayar)
    StepStats gpu_repeat;                         // T2 (S2 ile ayni ayar, 1080p)
    BalanceConfounders confounders;
    std::optional<CpuFrameSplit> cpu_split;
    bool cpu_speed_drop = false;  // C1 notu (salt okuma kontrolu doldurur)
    bool cpu_firmware_limit_logged = false;  // C1 ek kanit: olay gunlugu kaydi (yalniz cpu_speed_drop ile not olur)
};

enum class BalanceVerdict { Undetermined, CpuLimited, Balanced, GpuLimited, Unmeasurable };

struct BalanceRow {
    BalanceVerdict verdict = BalanceVerdict::Undetermined;
    std::string_view reason;             // Undetermined/Unmeasurable nedeni; hukum varsa bos
    std::optional<double> gpu_fps;       // S adimi gecerliyse
    bool gpu_lower_bound = false;        // "en az": GPU adiminda GPU tam mesgul degildi
    std::optional<double> gpu_fps_lo, gpu_fps_hi;  // yalniz tekrar olcumu olan satirda ve suruklenme varsa
    std::optional<double> coupled_fps;   // K adimi gecerliyse
    std::optional<double> coupled_gpu_busy;
    std::optional<double> ratio;         // gpu_fps / cpu_fps (ilk olcumler)
    std::optional<double> ratio_lo, ratio_hi;  // suruklenme araligi dahil karar araligi
    std::optional<double> coupled_ratio;       // coupled_fps / min(cpu_fps, gpu_fps)
};

struct BalanceResult {
    std::string_view rule = kBalanceRuleVersion;
    std::array<BalanceRow, kBalanceRows> rows;
    std::optional<double> cpu_fps;                 // S1 gecerliyse
    bool cpu_lower_bound = false;                  // "en az": CPU adiminda GPU bos degildi
    std::optional<double> cpu_fps_lo, cpu_fps_hi;  // suruklenme varsa S1/T1 araligi
    bool cpu_drift = false, gpu_drift = false;
    // denge_v4: CPU adimi / tekrar adimi gecersizse o adimin kendi nedeni (balance_reason; statik omurlu); aksi halde bos.
    std::string_view cpu_step_reason, cpu_repeat_reason;
    std::string_view global_reason;                // dolu: tum satirlar bu nedenle Belirlenemedi
    std::vector<std::string_view> global_reasons;  // gecerli tum genel nedenler (oncelik sirasinda)
    std::vector<std::string_view> notes;           // balance_note anahtarlari
    std::optional<double> cpu_sim_share, cpu_draw_share;  // S1 CPU suresinin simulasyon / cizim payi, [0,1]
};

BalanceResult evaluate_balance(const BalanceInputs& in);

// S1 CPU suresinin paylari. Toplam <= 0 veya gecersiz deger -> nullopt (pay uydurulmaz).
struct CpuShares {
    double sim = 0, draw = 0;
};
std::optional<CpuShares> cpu_shares(const CpuFrameSplit& s) noexcept;

// ---- Gecmis (A5) ----

std::string_view to_string(BalanceVerdict v) noexcept;  // "undetermined" | "cpu" | "balanced" | "gpu" | "unmeasurable"
std::string_view to_string(StepState s) noexcept;
// Kalici hukum kodu (gecmis metrigi). Degerler degismez: 0 belirlenemedi, 1 CPU, 2 dengeli, 3 GPU, 4 olculemedi.
int verdict_code(BalanceVerdict v) noexcept;
std::optional<BalanceVerdict> verdict_from_code(double code) noexcept;
// Duz metrik anahtarlari: balance.cpu_fps, balance.cpu_lower_bound, balance.gpu_fps_<r>,
// balance.gpu_lower_bound_<r>, balance.coupled_fps_<r>, balance.verdict_<r> (r: 1080/1440/2160).
// Yalniz olculen degerler yazilir (eksik deger 0 olarak yazilmaz); hukum kodu her satir icin yazilir.
std::vector<std::pair<std::string, double>> balance_history_metrics(const BalanceResult& r);

}  // namespace ef::core::diagnosis
