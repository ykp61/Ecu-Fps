#pragma once
// Internet hiz testi (Cloudflare) icin saf mantik: Win32/ag cagrisi yok, birim testlenir.
// Kurallar (spec 2026-11-11-zehir-yesili-redesign-design.md §7):
//  - Tek ag hostu kSpeedHost; yol yalniz kDownPath / kUpPath. Ag kodu yalniz
//    src/platform/windows/net/ altindadir (politika testi bu sabitleri tarar).
//  - Butce: en fazla kSpeedMaxBytes veri, en fazla kSpeedMaxSeconds sure; her yon kPhaseSeconds.
//  - Iptal/hata durumunda kismi sayi sonuc olarak sunulmaz (sanitize_result).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ef::core::net {

inline constexpr std::string_view kSpeedHost = "speed.cloudflare.com";
inline constexpr std::string_view kDownPath = "/__down";
inline constexpr std::string_view kUpPath = "/__up";

inline constexpr double kSpeedMaxSeconds = 60.0;
inline constexpr std::uint64_t kSpeedMaxBytes = 256ull << 20;
inline constexpr int kLatencyProbes = 10;
inline constexpr std::array<std::uint64_t, 4> kChunkBytes{100'000, 1'000'000, 10'000'000, 25'000'000};
inline constexpr double kPhaseSeconds = 10.0;

enum class SpeedPhase { Idle, Latency, Download, Upload, Done, Cancelled, Failed };
enum class SpeedFail { None, Offline, Timeout, Tls, Http, Budget, Gated };

struct ChunkSample {
    std::uint64_t bytes = 0;
    double seconds = 0.0;
};

// Tek parcanin aktarim hizi (Mbit/s). Sure <= 0, sonsuz/NaN veya bayt 0 ise nullopt.
std::optional<double> chunk_mbps(const ChunkSample& s) noexcept;

// Gecerli parca hizlarinin en-yakin-sira 90. yuzdeligi; gecerli parca yoksa nullopt.
std::optional<double> percentile90_mbps(std::span<const ChunkSample> chunks);

// Sonlu degerlerin medyani (cift sayida: iki ortanin ortalamasi); bos ise nullopt.
std::optional<double> median_ms(std::span<const double> values);

class SpeedPlan {
public:
    // Siradaki parca boyu; 0 = bu asama bitti. Yalniz Download/Upload icin parca uretir.
    // chunks_done: bu asamada tamamlanan parca sayisi (kademe dizini, son kademede sabit).
    // 0 doner: phase_elapsed_s >= kPhaseSeconds, total_elapsed_s >= kSpeedMaxSeconds
    // veya total_bytes + parca > kSpeedMaxBytes.
    std::uint64_t next_chunk(SpeedPhase phase, std::size_t chunks_done, double phase_elapsed_s,
                             std::uint64_t total_bytes, double total_elapsed_s) const noexcept;
};

// Kadran kesri: {0,5,10,50,100,250,500,750,1000} Mbit/s olcegi uzerinde parcali dogrusal, [0,1].
double gauge_fraction(double mbps) noexcept;

// Mbit/s -> MB/s (bolu 8).
double mbps_to_mbs(double mbps) noexcept;

// R-C1: yuk altinda gecikme (bufferbloat). Indirme/yukleme sirasinda ayni hosta yan baglantiyla kucuk istekler atilir
// (govdesi 0 bayt); bos hat gecikmesiyle karsilastirilir. Yalniz HAT gecikmesidir: oyun sunucusu ping'i degildir ve
// oyun ici gecikmeyi/FPS'i vaat etmez. Esikler urun kararidir (standart degil): artis < 30 ms Dusuk, < 100 ms Orta, aksi Yuksek.
inline constexpr std::size_t kLoadedMinSamples = 5;      // bir yonde en az bu kadar gecerli ornek yoksa "veri yetersiz"
inline constexpr int kLoadedProbeIntervalMs = 250;       // yan baglanti istek araligi
inline constexpr double kBloatLowMs = 30.0, kBloatMediumMs = 100.0;

enum class BloatGrade { Insufficient, Low, Medium, High };

// Yuk altinda medyan; kLoadedMinSamples'tan az sonlu ornek varsa nullopt.
std::optional<double> loaded_median_ms(std::span<const double> samples);

// Bos hat ve yuklu gecikme (ms). Not: en kotu yonun artisi (yuklu - bos) siniflanir; artis negatifse 0 sayilir.
// Bos hat yoksa veya iki yon de yoksa Insufficient.
BloatGrade bloat_grade(std::optional<double> idle_ms, std::optional<double> down_loaded_ms, std::optional<double> up_loaded_ms) noexcept;

// Metin anahtari: speed.bloat.low|medium|high|none (Insufficient).
std::string_view bloat_key(BloatGrade g) noexcept;

struct SpeedResult {
    SpeedPhase phase = SpeedPhase::Idle;
    SpeedFail fail = SpeedFail::None;
    std::optional<double> down_mbps, up_mbps, latency_ms;
    std::optional<double> down_loaded_ms, up_loaded_ms;  // R-C1: yuk altinda medyan gecikme (yetersiz ornekte yok)
    std::uint64_t bytes_used = 0;
};

// Done disindaki her sonuc (iptal, hata, yarim) sayi tasimaz; harcanan veri korunur.
SpeedResult sanitize_result(SpeedResult r) noexcept;

// Hata nedeni metin anahtari: speed.fail.offline|timeout|tls|http|budget|gated; None -> "".
std::string_view speed_fail_key(SpeedFail f) noexcept;

}  // namespace ef::core::net
