#pragma once
// MEAS-03: sensor/sayac orneklerini kare zaman cizgisine baglama (saf; birim testlenir).
//  - Her ornek kaynak, ortak monoton zaman (ms) ve ornekleme araligi tasir; eksik deger nullopt + neden.
//  - Coznurluk kurali: araligi T ms olan sayac, T'den kisa bir olayin (ornek 40 ms'lik takilma) kok
//    nedeni olarak kullanilamaz; yalniz "ayni pencerede yuksekti" baglami verir.
//  - VRAM kaynagi tiplidir: bu uygulamanin kendi DXGI butcesi (QueryVideoMemoryInfo) dis oyunun
//    butcesi/kullanimi DEGILDIR. Oyun icin yalniz PID'ye bagli surec sayaci (PDH GPU Process Memory)
//    veya adaptor toplami (baglam) kabul edilir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::measure {

enum class MetricSource { Pdh, Dxgi, VendorApi, Etw, Unknown };

struct TelemetrySample {
    std::string metric;            // ornek "cpu_pct", "gpu_engine_3d_pct", "vram_dedicated_bytes"
    MetricSource source = MetricSource::Unknown;
    double t_ms = 0;               // ortak monoton eksen (kare olaylariyla ayni koken)
    double interval_ms = 1000;     // ornekleme araligi
    std::optional<double> value;   // olculemediyse nullopt
    std::string missing_reason;    // value yoksa dolu
};

// Bir olay penceresini (ornek takilma) aciklayabilecek coznurluk var mi?
// Kural: interval_ms <= olay suresi / 2 (Nyquist benzeri temkinli sinir).
bool can_attribute(double interval_ms, double event_duration_ms) noexcept;

enum class Attribution { RootCandidate, ContextOnly, NoData };
struct WindowContext {
    std::string metric;
    std::optional<double> value;
    Attribution attribution = Attribution::NoData;
};

// [start_ms, end_ms] penceresini kapsayan en yakin ornegi bulur; coznurluk yetmiyorsa ContextOnly.
WindowContext context_for_window(const std::vector<TelemetrySample>& samples, std::string_view metric, double start_ms, double end_ms);

// VRAM kaynagi ve kime ait oldugu.
enum class VramScope { OwnProcessDxgiBudget, ProcessUsage, AdapterTotal };
struct VramReading {
    VramScope scope = VramScope::AdapterTotal;
    std::uint32_t pid = 0;  // ProcessUsage icin olculen surec; OwnProcess icin bu uygulamanin PID'i
    std::optional<std::uint64_t> bytes;
};
// Hedef oyun icin kullanilabilir mi? Kendi DXGI butcesi asla; ProcessUsage yalniz ayni PID.
bool vram_valid_for_game(const VramReading& r, std::uint32_t game_pid) noexcept;

}  // namespace ef::core::measure
