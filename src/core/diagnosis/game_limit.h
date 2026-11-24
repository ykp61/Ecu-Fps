#pragma once
// Spec 2026-11-12 B (D5): Oyunda Test "bu oyunda ne sinirladi" ozeti (saf; birim testlenir). Kural: game_limit_v1.
//  - Girdi: yakalanan render araliklari + ayni karelerin GPU mesgul suresi (PresentData GPUDuration; kare basi).
//    Ikisi bire bir hizalidir; GPU serisi bossa GPU suresi OLCULMEDI demektir (0 yazilmaz, hukum verilmez).
//  - Pencere: ardisik kareler ~1 sn'lik pencerelere toplanir; pencere orani = toplam GPU mesgul / toplam kare
//    suresi. Tek karenin orani kullanilmaz: GPU isi kare tamamlanma olayina gore atfedilir, kuyruklu karelerde tek
//    kare 0,7 / 1,3 gibi salinabilir; pencere toplami bunu dengeler. Esikler compute.h ile aynidir
//    (kGpuBusyShortRatio 0,60 = GPU bekledi; kGpuBusyFullRatio 0,90 = GPU tam mesgul).
//  - "Karelerin %N'i": orani esigi gecen pencerelerdeki kare sayisi / toplam kare. Olculen bir paydir.
//    Hic GPU paketi gorulmeyen pencere (GPU mesgul toplami 0) "bekledi" sayilmaz: kare sunan oyunda bu bir izleme
//    boslugudur; kareleri toplamda kalir ama hicbir paya yazilmaz.
//  - Kare siniri kontrolu ONCE gelir: ortalama FPS, ekran yenileme hizinin ya da yaygin sinirlarin
//    (30/60/72/90/120/144/165/240) %3 yakinindaysa "kare siniri olabilir" -> cekimser (hangi parcanin sinirladigi
//    soylenmez; sinirda calisan oyunda GPU'nun beklemesi CPU kaniti degildir).
//  - Baskinlik payi classify_run ile aynidir (%60): ne "tam mesgul" ne "bekledi" baskin degilse Belirlenemedi.
//  - HAGS acikken GPU paket olaylari yaklasiktir (PresentMon notu): hukum degismez, "GPU suresi yaklasik" notu tasinir.
//  - Capraz denetim: oyun surecinin kendi 3D motor kullanimi (PDH, LUID bazinda) verildiyse ve GPU suresi hukmuyle
//    CELISIYORSA hukum verilmez (Belirlenemedi / conflict). Esikler compute.h ile aynidir: "GPU bekledi" + kullanim
//    >= kGpuHighPct (%90; classify_window'daki gpu_high + busy_short celiskisi) ya da "GPU tam mesgul" + kosu geneli
//    GPU suresi orani >= 0,90 iken kullanim < kGpuLowPct (%70). Iki olcum ayni seyi (surecin GPU'da calistigi sure
//    payi) farkli kaynaktan okur; tutmuyorsa olcumlerden biri guvenilmezdir.
//  - Cikti metin degil kod tasir; her hukum "bu oyunda, bu ayarlarda" kapsamiyla sunulur (UI katmani).

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "core/diagnosis/compute.h"

namespace ef::core::diagnosis {

// game_limit_v2 (Uyumluluk B4 bosluk 14/15): VRR otomatik kare siniri adaylari (yalniz dusuk kare araligi CV ile) ve
// capraz denetimde oyunun en yuklu GPU motor turu (Gorev Yoneticisi yontemi; 3D ikincil alan).
inline constexpr std::string_view kGameLimitRules = "game_limit_v2";
// Yaygin kare sinirlari (FPS). Ekran yenileme hizi ayrica denenir.
inline constexpr std::array<double, 8> kCommonFrameCapsFps{30.0, 60.0, 72.0, 90.0, 120.0, 144.0, 165.0, 240.0};
inline constexpr double kFrameCapTolerance = kCapTolerance;  // |fps - sinir| / sinir <= 0,03
// Uyumluluk B4 (bosluk 14): ekranin hemen altindaki kare sinirlari. G-SYNC/FreeSync + VSync/Reflex/ULL surucu otomatik
// siniri (yenileme - yenileme^2 / 3600: 144 Hz -> 138,24, 240 Hz -> 224) ve el ile "yenileme - 3" siniri. Bunlar yalniz
// kare araligi degisim katsayisi bilinip <= kLowVarianceCv iken (sabit kare akisi) ve yenileme hizi / yaygin sinir
// eslesmediginde denenir: GPU/CPU sinirli oyunun ortalamasi da tesadufen bu sayilara dusebilir.
inline constexpr double kVrrRefreshOffsetFps = 3.0;
// Surucu otomatik VRR siniri: yenileme - yenileme^2 / 3600 (tek tanim; detect_frame_cap ve classify_window kullanir).
inline constexpr double vrr_auto_cap_fps(double refresh_hz) noexcept { return refresh_hz - refresh_hz * refresh_hz / 3600.0; }
// VRR adayi yalniz GPU suresi siniri yalanlamiyorsa kabul edilir (inceleme B4): sinirda calisan oyunda GPU bosta
// bekler. GPU suresi olculup karelerin >= kGameLimitDominantShare'i tam mesgul pencerede ya da kosu geneli oran
// >= kGpuBusyFullRatio ise, ya da oyunun GPU kullanimi >= kGpuHighPct ise aday reddedilir (GPU sinirli oyun
// tesadufen bu FPS'e dusebilir). Yenileme hizi / yaygin sinir eslesmesi bu kapidan gecmez (onceki davranis).
inline constexpr double kGpuBusyWindowMs = 1000.0;
inline constexpr double kGameLimitDominantShare = 0.60;
inline constexpr std::size_t kGameLimitMinFrames = 30;
inline constexpr std::size_t kGameLimitMinWindows = 3;

struct FrameCapMatch {
    bool suspected = false;
    double cap_fps = 0;       // eslesen sinir (yenileme hizi ya da yaygin sinir)
    bool is_refresh = false;  // eslesen deger ekran yenileme hizi
    bool is_vrr = false;      // eslesen deger yenileme hizindan turetilmis VRR / "yenileme - 3" siniri
};
// En yakin sinir secilir; yenileme hizi ile yaygin sinir ayni uzakliktaysa yenileme hizi bildirilir. VRR adaylari
// yalniz bunlardan hicbiri eslesmezse denenir (is_vrr).
// Sonlu olmayan / <= 0 FPS ya da yenileme hizi yok sayilir (eslesme uretmez).
// interval_cv: kare araliklarinin degisim katsayisi (std / ortalama). Bilinmiyorsa ya da > kLowVarianceCv ise VRR
// adaylari denenmez (onceki davranis).
FrameCapMatch detect_frame_cap(std::optional<double> avg_fps, std::optional<double> refresh_hz,
                               std::optional<double> interval_cv = std::nullopt) noexcept;
// Ekran yenileme hizindan VRR sinir adaylari: { yenileme - yenileme^2 / 3600, yenileme - 3 } (pozitif olanlar).
std::vector<double> vrr_cap_candidates(double refresh_hz);
// Kare araliklarinin degisim katsayisi (orneklem std / ortalama). < 2 gecerli aralik -> nullopt.
std::optional<double> frame_interval_cv(const std::vector<double>& intervals_ms);

struct GpuBusyWindow {
    double start_ms = 0, end_ms = 0;  // kare araliklarinin birikimli ekseninde
    std::size_t frames = 0;
    double interval_ms = 0;  // penceredeki kare surelerinin toplami
    double busy_ms = 0;      // penceredeki GPU mesgul surelerinin toplami
    double ratio = 0;        // busy_ms / interval_ms
    double busy_per_frame_ms() const noexcept { return frames ? busy_ms / static_cast<double>(frames) : 0.0; }
};
// Boyutlar esit degilse ya da seri bossa bos doner. Gecersiz kare (sonlu olmayan, aralik <= 0, mesgul < 0) atlanir.
// Son pencere window_ms / 2'den kisaysa bir oncekine eklenir (tek basina kisa pencere hukum agirligi tasimaz).
std::vector<GpuBusyWindow> gpu_busy_windows(const std::vector<double>& intervals_ms, const std::vector<double>& gpu_busy_ms,
                                            double window_ms = kGpuBusyWindowMs);

enum class GameLimit { GpuFull, GpuWaited, FrameCapSuspected, Undetermined };
enum class GameLimitReason {
    None,          // hukum var
    RunInvalid,    // kosu gecersiz (kayip olay, kisa kosu ...)
    TooFewFrames,  // < kGameLimitMinFrames kare, ortalama FPS yok ya da < kGameLimitMinWindows pencere
    NoGpuTiming,   // GPU suresi olculmedi (seri bos ya da hic GPU paketi gorulmedi)
    Mixed,         // ne tam mesgul ne bekleme baskin
    Conflict,      // GPU suresi hukmu, oyunun kendi 3D motor kullanimiyla celisiyor
};
std::string_view to_string(GameLimit v) noexcept;        // "gpu_full" | "gpu_waited" | "frame_cap" | "undetermined"
std::string_view to_string(GameLimitReason r) noexcept;  // "none" | "run_invalid" | "too_few_frames" | "no_gpu_timing" | "mixed" | "conflict"

struct GameLimitInput {
    std::vector<double> intervals_ms;  // render araliklari (ms)
    std::vector<double> gpu_busy_ms;   // intervals_ms ile hizali; bos = olculmedi
    std::optional<double> avg_fps;     // kosu ortalamasi (frame_stats)
    std::optional<double> refresh_hz;  // oyunun bulundugu ekran; bilinmiyorsa yalniz yaygin sinirlar denenir
    std::optional<bool> hags_on;       // donanim hizlandirmali GPU zamanlamasi; bilinmiyorsa nullopt
    // Oyun surecinin kendi GPU kullanimi (0..100, yakalama boyunca ortalama); game_limit_v2: en yuklu motor turu (Gorev
    // Yoneticisi yontemi; async compute dahil). Okunamadiysa nullopt (denetim yok).
    std::optional<double> game_gpu_pct;
    bool run_valid = true;
};

struct GameLimitResult {
    GameLimit verdict = GameLimit::Undetermined;
    GameLimitReason reason = GameLimitReason::TooFewFrames;
    std::size_t frames = 0;  // pencerelere giren gecerli kare
    std::size_t windows = 0, full_windows = 0, waited_windows = 0;
    std::size_t full_frames = 0, waited_frames = 0;  // esigi gecen pencerelerdeki kareler
    // Yalniz GPU suresi olculduyse dolu (0..100 ve 0..): eksik deger 0 yazilmaz.
    std::optional<double> full_pct, waited_pct;
    std::optional<double> busy_ratio;  // toplam GPU mesgul / toplam kare suresi
    std::optional<double> avg_fps;     // girdideki ortalama (metin icin)
    std::optional<double> cap_fps;     // FrameCapSuspected: eslesen sinir
    bool cap_is_refresh = false;
    bool cap_is_vrr = false;  // eslesen sinir yenileme hizindan turetilmis VRR / "yenileme - 3" siniri
    bool gpu_time_approximate = false;  // HAGS acik ve GPU suresi olculdu
};

GameLimitResult evaluate_game_limit(const GameLimitInput& in);

// HAGS sistem geneli bir ayardir (display_reader ile ayni kural): destekleyen herhangi bir bagdastiricida acik
// gorunuyorsa acik; destekleyen var ama hicbirinde acik degilse kapali; okunamadiysa / destekleyen yoksa nullopt.
struct HagsAdapterState {
    std::optional<bool> supported, enabled;
};
std::optional<bool> hags_enabled_any(const std::vector<HagsAdapterState>& adapters) noexcept;

}  // namespace ef::core::diagnosis
