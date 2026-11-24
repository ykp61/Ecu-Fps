#pragma once
// Canli test ekrani (spec 2026-11-12 A4): saf veri modeli + cizim is parcacigindan UI is parcacigina onizleme karesi
// posta kutusu. Platform cagrisi yok; birim testlenir.
//  - LiveSample: UI'nin bir karede cizdigi her sey (en yeni onizleme pikselleri + FrameTap'ten okunan canli sayilar).
//    Sayilar olculmus degerlerdir; pencere kapanmadiysa (has_numbers = false) hicbir sayi gosterilmez, uydurulmaz.
//  - Yuk cubuklari: "CPU kare hazirlama" = kare basi CPU suresi / kare araligi; "GPU cizim" = kare basi GPU gecen
//    suresi / kare araligi (denge kuralindaki gpu_busy_ratio ile ayni oran). Sistem geneli kullanim yuzdesi DEGILDIR.
//  - PreviewMailbox: "en yeni kazanir". Uretici (cizim is parcacigi) hicbir zaman beklemez: kutu istenmiyorsa ya da
//    tuketici o an kutuyu tutuyorsa kare dusurulur. Kareler degismez (const) tamponlardir; birakilan tampon yeniden
//    kullanilir (kare basina bellek ayirma yok).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "core/games/game_icon.h"

namespace ef::core::measure {

// Onizleme en fazla 960x540'tir (benchmark::kPreviewMaxWidth / Height); cizim katmani bu kenara kadar kabul eder.
inline constexpr std::uint32_t kLivePreviewMaxSide = 960;
inline constexpr std::size_t kLiveRingSize = 32;  // benchmark::kTapRingSize ile ayni (kova basina bir FPS degeri)

enum class LivePhase { Idle, Prepare, Warmup, Measure };

struct LiveSample {
    games::ImageRef preview;  // en yeni onizleme (on-carpilmis BGRA, alfa 255); henuz kare yoksa bos
    LivePhase phase = LivePhase::Idle;
    std::uint32_t step_index = 0, step_total = 0;  // 1'den; 0 = adim yok (hazirlik)
    std::uint32_t step_code = 0;                   // denge kosucusu adim kodu (StepId + 1); 0 = yok
    std::uint32_t width = 0, height = 0;           // adimin cizim cozunurlugu
    bool has_numbers = false;      // en az bir olcum kovasi kapandi (asagidaki sayilar olculdu)
    double fps = 0;                // son 1 sn
    double cpu_ms = 0;             // son 1 sn: kare basi CPU suresi (simulasyon + ayiklama + kayit / gonderim)
    std::optional<double> gpu_ms;  // son 1 sn: kare basi GPU gecen suresi (zaman damgasi yoksa bos)
    std::array<float, kLiveRingSize> fps_ring{};  // eskiden yeniye; ilk ring_count oge gecerli
    std::size_t ring_count = 0;
};

// Kare suresi (ms) = 1000 / FPS; sayi yoksa bos.
std::optional<double> live_frame_ms(const LiveSample& s) noexcept;
// Yuk yuzdeleri [0, 100]; sayi (ya da GPU zaman damgasi) yoksa bos.
std::optional<double> live_cpu_load_pct(const LiveSample& s) noexcept;
std::optional<double> live_gpu_load_pct(const LiveSample& s) noexcept;
// Adimin yuk ayari (adim kodundan): S1 / T1 CPU tam + GPU en az; S2-S4 / T2 GPU tam + CPU en az; K1-K3 ikisi de tam.
enum class LiveLoad { Unknown, CpuHeavy, GpuHeavy, Both };
LiveLoad live_load(std::uint32_t step_code) noexcept;
// Kare suresi cubuklari (ms; eskiden yeniye): halkadaki gecerli (> 0, sonlu) kova FPS'lerinden 1000 / FPS. En fazla
// max_bars (en yeniler). Gecersiz kova atlanir; deger uydurulmaz.
std::vector<double> live_frame_bars(const LiveSample& s, std::size_t max_bars);
// Tamamlanan adim orani [0, 1]: adim n suruyorsa (n - 1) / toplam. Adim yoksa bos (sahte yuzde yok).
std::optional<double> live_step_fraction(const LiveSample& s) noexcept;

class PreviewMailbox {
public:
    PreviewMailbox();
    // Tuketici onizleme istiyor mu (pencere gorunur ve canli gorunum acik). false iken offer() kopyalamadan doner.
    void set_wanted(bool on) noexcept { wanted_.store(on, std::memory_order_relaxed); }
    bool wanted() const noexcept { return wanted_.load(std::memory_order_relaxed); }
    // Uretici (cizim is parcacigi): pikselleri (BGRA8, ustten alta, satir = stride bayt) kopyalar ve kutuya koyar;
    // okunmamis onceki karenin yerini alir. Beklemez. false: kare dusuruldu (istenmiyor, gecersiz ya da kutu mesgul).
    bool offer(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride);
    // Tuketici (UI): en yeni kare; yeni kare yoksa bos.
    games::ImageRef take();
    // Bekleyen kareyi ve yedek tamponlari birakir (kosu bitti). Tuketicinin elinde kalan kareler de birakildiklarinda
    // silinir (havuza donmez): kosu disinda onizleme tamponu tutulmaz. Sonraki offer() yeniden kullanimi acar.
    void clear();
    // Denetim / test sayaclari.
    std::size_t spare_buffers() const;  // havuzda bekleyen yedek tampon sayisi
    std::uint64_t offered() const noexcept { return offered_.load(std::memory_order_relaxed); }
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    struct Pool;
    std::shared_ptr<Pool> pool_;  // kareler kutudan uzun yasayabilir: havuz paylasimli tutulur
    std::mutex mu_;
    games::ImageRef latest_;
    std::atomic<bool> wanted_{false};
    std::atomic<std::uint64_t> offered_{0}, dropped_{0};
};

}  // namespace ef::core::measure
