#pragma once
// Spec 2026-11-12 C: parca ve ayar uyumu denetimleri (saf; birim testlenir). Hepsi SALT OKUMA girdilerle calisir;
// bu modul sayac, olay gunlugu, aygit ya da D3D cagrisi icermez.
//  C1 CPU hiz siniri : denge testinin CPU yuklu adimlarinda (S1, K1-K3, T1) "% Processor Performance" ilk dilim ile
//                      son dilim arasinda > %15 dustuyse ya da "% Performance Limit" < 100 gorulduyse "CPU yuk altinda
//                      hiz dusurdu" notu. Sicaklik ya da neden iddiasi YOKTUR (bellenim Windows'a haber vermeden de
//                      sinirlayabilir; sayaclar nedeni tasimaz).
//  C2 PCIe yuk altinda: S3 (GPU kapasitesi, 1440p) sirasinda okunan link genisligi / hiz nesli azamiyle
//                      karsilastirilir ve mevcut evaluate_pcie kuralina verilir. Metin yalniz "x8 / x16, yuk
//                      altinda"dir; neden (yuva, riser, serit paylasimi) iddia edilmez.
//  C3 Oyun bagdastiricisi: Oyunda Test sirasinda oyun surecinin 3D yukunu tasiyan bagdastirici (LUID), yuksek
//                      performansli bagdastiriciyla karsilastirilir; farkliysa masaustunde oneri, dizustunde bilgi.
// Okunamayan girdi "sorun yok" sayilmaz: bulgu "olculmedi / bilinmiyor" kalir.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/diagnosis/platform.h"

namespace ef::core::diagnosis {

// ---- C1: CPU hiz siniri ----
inline constexpr double kCpuSpeedDropRatio = 0.15;  // (ilk - son) / ilk > : dusus
inline constexpr double kCpuSpeedSegmentS = 3.0;    // ilk / son dilim uzunlugu (pencere kisaysa pencerenin yarisi)
inline constexpr double kCpuPerfLimitFull = 100.0;  // "% Performance Limit" < : Windows / bellenim hizi sinirliyor

struct CpuSpeedSample {
    double t_s = 0;                         // ortak zaman ekseni (sn)
    std::optional<double> performance_pct;  // "% Processor Performance" (anma frekansina gore; 100'u asabilir)
    std::optional<double> limit_pct;        // "% Performance Limit"
};
struct CpuSpeedWindow {
    double begin_s = 0, end_s = 0;  // CPU yuklu bir adimin olcum penceresi
};
struct CpuSpeedFinding {
    bool measured = false;  // en az bir pencerede iki dilim de orneklendi YA DA bir sinir degeri okundu
    bool drop = false;      // bir pencerede ilk dilim -> son dilim dususu > %15
    bool limited = false;   // pencereler icinde "% Performance Limit" < 100 goruldu
    // En buyuk dususun (dusus yoksa en buyuk farkin) olculdugu pencerenin dilim ortalamalari; olculmediyse bos.
    std::optional<double> first_pct, last_pct;
    std::optional<double> drop_ratio;     // 1 - son / ilk (negatif: hiz artti)
    std::optional<double> min_limit_pct;  // pencerelerde gorulen en dusuk sinir
    bool noted() const noexcept { return drop || limited; }
};
// Ornekler zamana gore sirali olmak zorunda degildir. Sonlu olmayan / <= 0 performans degeri ve [0,100] disi sinir
// degeri atlanir. Pencere disindaki ornek kullanilmaz.
CpuSpeedFinding evaluate_cpu_speed(std::span<const CpuSpeedSample> samples, std::span<const CpuSpeedWindow> windows) noexcept;

// ---- C2: GPU PCIe linki yuk altinda ----
struct PcieLoadReading {
    std::optional<std::uint32_t> cur_width, max_width;  // serit sayisi (kartin azamisi)
    std::optional<std::uint32_t> cur_gen, max_gen;      // hiz nesli (1 = 2,5 GT/s ... 5 = 32 GT/s)
    std::optional<std::uint32_t> slot_max_width, slot_max_gen;  // ust koprunun (yuvanin) azamisi; okunamadiysa bos
    std::optional<double> gpu_load_pct;  // okuma anindaki GPU yuku (S3 gpu_busy_ratio * 100)
};
struct PcieLoadFinding {
    // Link yuk altinda (GPU yuku >= kGpuLoadPct) okundu ve en az bir boyutu kart azamisiyle karsilastirilabildi.
    // Kuralin sinifindan bagimsizdir (yuva azamisi okunamadiysa kural hukum vermez, okuma yine yuk alti olcumdur).
    bool measured_under_load = false;
    bool width_below_max = false;      // genislik kart azamisinin altinda (yuk altinda)
    bool gen_below_max = false;        // hiz nesli kart azamisinin altinda (yuk altinda)
    PlatformFinding platform;          // evaluate_pcie sonucu (PcieLinkReduced / NotLimiting / Insufficient)
    bool below_max() const noexcept { return measured_under_load && (width_below_max || gen_below_max); }
    // Kural "egitim dususu" dedi: link hem kart hem OKUNAN yuva azamisinin altinda. Yuva azamisi okunamadiysa false.
    bool reduced() const noexcept { return measured_under_load && platform.cls == PlatformClass::PcieLinkReduced; }
};
// Okumadan evaluate_pcie ornegi. Yuva azamisi okunan degerdir; okunamadiysa YALNIZ link kart azamisindeyken kart
// azamisi yazilir (o hizda calisan link yuvanin en az onu destekledigini kanitlar). Link kart azamisinin altinda ve
// yuva okunamadiysa yuva bos kalir: kural hukum vermez (Gen 5 kart + Gen 4 yuva olagandir; "dusuk link" denmez).
// Duz karsilastirma (below_max, "x16 / x16, Gen 4 / Gen 5, yuk altinda") her durumda kart azamisiyledir.
PcieSample pcie_sample_from(const PcieLoadReading& r) noexcept;
PcieLoadFinding evaluate_pcie_under_load(const PcieLoadReading& r);
// Dilden bagimsiz link metni: "x8 / x16, Gen 3 / Gen 4". Yalniz okunan boyutlar yazilir; hicbiri yoksa bos.
std::string pcie_link_text(const PcieLoadReading& r);

// ---- C3: oyun hangi ekran kartinda ----
inline constexpr double kGameAdapterMinLoadPct = 5.0;  // oyunun 3D yuku bundan dusukse bagdastirici iddia edilmez

// Yakalama boyunca okunan (bagdastirici, 3D yuzdesi) ornekleri -> oyunun cizdigi bagdastirici: yuzde toplami en
// buyuk LUID (esitlikte metni kucuk olan). mean_pct o LUID'in KENDI orneklerinin ortalamasidir. LUID'i bos ya da
// degeri sonlu olmayan / negatif ornek atlanir; ornek yoksa luid bos.
struct AdapterLoadSample {
    std::string luid;
    double pct = 0;
};
struct GameAdapterPick {
    std::string luid;
    std::optional<double> mean_pct;
    std::size_t samples = 0;
};
GameAdapterPick pick_game_adapter(std::span<const AdapterLoadSample> samples);

enum class GameAdapterClass { Unknown, OnHighPerformance, OnOtherAdapter };
enum class GameAdapterLevel { None, Info, Advice };
struct GameAdapterInput {
    std::string game_luid;              // oyunun 3D yukunu tasiyan bagdastirici ("hhhhhhhh:llllllll"); okunamadiysa bos
    std::optional<double> game_3d_pct;  // o bagdastiricidaki ortalama 3D motor kullanimi
    std::string high_perf_luid;         // DXGI yuksek performans tercihindeki ilk donanim bagdastiricisi; yoksa bos
    std::size_t hardware_adapters = 0;  // yazilim olmayan bagdastirici sayisi
    std::optional<bool> laptop;         // pil var mi (dizustu); okunamadiysa bos
};
struct GameAdapterFinding {
    GameAdapterClass cls = GameAdapterClass::Unknown;
    GameAdapterLevel level = GameAdapterLevel::None;
    std::string_view reason;  // Unknown nedeni: "no_game_adapter" | "low_load" | "no_high_perf" | "single_adapter"
};
// Farkli bagdastirici: masaustu (laptop == false) -> Advice; dizustu ya da bilinmiyor -> Info (hibrit dizustunde
// uygulama basina tercih okunmadan "yanlis" denmez).
GameAdapterFinding evaluate_game_adapter(const GameAdapterInput& in) noexcept;


// P6: Arena testi tumlesik kartta mi kostu? Yalniz DXGI iki ayri donanim karti bildirirse (guc tasarrufu tercihi ilk karti
// != yuksek performans tercihi ilk karti) VE test bagdastiricisi guc tasarrufu karti ise true. Herhangi bir LUID bos ya da
// tek donanim karti varsa false (iddia yok: "tumlesik" bilinmiyor). Mock/birim testi gercek cihaz sayilmaz.
bool ran_on_integrated_with_discrete(const std::string& ran_luid, const std::string& high_perf_luid, const std::string& min_power_luid,
                                     std::size_t hardware_adapters) noexcept;

}  // namespace ef::core::diagnosis
