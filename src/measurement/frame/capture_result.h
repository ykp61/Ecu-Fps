#pragma once
// INGAME-CAPTURE: yukseltilmis kare yakalama yardimcisinin sonucunu istemciye aktaran saf codec.
//  - 1. IPC cercevesi: JSON baslik (v1) = hedef kimligi, start durumu, ozet/istatistik/manifest alanlari,
//    dropped_presents, interval_count ve toplam bayt. 64 KB IPC sinirinin altinda kalir.
//  - Sonraki cerceveler: once render_intervals_ms, sonra (v4) display_intervals_ms; float32 LE parcalar (<= 60 KB).
//    Bir parca iki seriyi karistirmaz; her seri kendi sayi/bayt planiyla dogrulanir.
//  - v5 (spec 2026-11-12 B): ucuncu seri = kare basi GPU mesgul suresi (ms), render araliklariyla bire bir hizali.
//    Sayi ya 0'dir (GPU izlenmedi) ya da interval_count'a esittir; degerler sonlu ve >= 0'dir (0 gecerli olcumdur:
//    o karede GPU paketi gorulmedi). Aralik parcalarindan ayri kodlanir (ChunkKind::GpuBusy).
//  - Istemci toplam bayt ve sayiyi dogrular; eksik/fazla/bozuk veri kabul edilmez.
//  - Tum alanlar beyaz listeyle okunur; bilinmeyen alan, asiri sayi/bayt, NaN/sonsuz ve pozitif olmayan
//    aralik reddedilir. Platformdan bagimsizdir (Win32 cagrisi yok), birim testlenir.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/measure/frame_stats.h"
#include "core/measure/frames.h"
#include "core/measure/run_manifest.h"
#include "measurement/frame/game_capture.h"

namespace ef::measurement {

// v4: ekran araliklari + yakalama sonucu/hedef cikisi (EF-X18). v5: kare basi GPU mesgul serisi. Tam surum eslesmesi.
inline constexpr int kCaptureResultVersion = 5;
// 60 KB: tek Response cercevesinin yuku (64 KB IPC sinirinin altinda guvenli pay).
inline constexpr std::size_t kMaxChunkBytes = 60 * 1024;
inline constexpr std::size_t kMaxIntervalCount = 1'000'000;
inline constexpr std::size_t kMaxTotalBytes = kMaxIntervalCount * sizeof(float);
inline constexpr std::size_t kMaxChunkValues = kMaxChunkBytes / sizeof(float);

// Kapali protokol: yalniz sonlu ve > 0 kare araligi tasinabilir (frame_stats ile ayni dislama kurali).
bool is_valid_interval(double ms) noexcept;
// v5: GPU mesgul suresi sonlu ve >= 0 olmalidir (ust sinir: bir gun; anlamsiz buyuk deger reddedilir).
bool is_valid_gpu_busy(double ms) noexcept;
// Parca turu: Interval (> 0) | GpuBusy (>= 0).
enum class ChunkKind { Interval, GpuBusy };

struct CaptureResultHeader {
    // Hedef kimligi: yardimcinin isletim sisteminden okudugu deger; istemci kendi onayli kimligiyle karsilastirir.
    std::uint32_t target_pid = 0;
    std::uint64_t target_create_time = 0;
    std::string target_image;
    core::measure::TargetError target_error = core::measure::TargetError::NoCandidate;

    // ETW oturumu baslangici
    bool start_ok = false;
    FrameSessionError start_error = FrameSessionError::Other;
    std::uint32_t start_win32 = 0;
    std::string start_detail;

    // FrameSummary (render_intervals_ms ayri parcalarda tasinir)
    std::uint64_t summary_swapchain = 0;
    std::size_t summary_other_swapchains = 0;
    std::size_t summary_render_frames = 0;
    std::size_t summary_displayed_frames = 0;
    std::size_t summary_generated_frames = 0;
    core::measure::Runtime summary_runtime = core::measure::Runtime::Unknown;
    core::measure::Validity summary_validity = core::measure::Validity::Invalid;
    std::vector<std::string> summary_reasons;

    // FrameStats
    std::size_t stats_n = 0;
    std::size_t stats_excluded = 0;
    double stats_valid_seconds = 0;
    std::optional<double> avg_fps;
    std::optional<double> low1_fps;
    std::optional<double> low01_fps;
    std::optional<double> p95_ms;
    std::optional<double> p99_ms;
    std::optional<double> p99_9_ms;
    double stats_median_ms = 0;
    double stats_max_ms = 0;
    std::string stats_pattern;  // "insufficient" | "stable" | "micro_stutter" | "spiky"
    bool stats_sufficient = false;

    // RunManifest
    int manifest_schema_version = 1;
    std::string manifest_source;
    std::string manifest_app;
    std::map<std::string, std::string> manifest_context;
    std::string manifest_stats_definition;
    std::string manifest_presentmon_version;
    double manifest_valid_seconds = 0;
    std::size_t manifest_intervals = 0;
    std::size_t manifest_excluded_intervals = 0;
    std::uint32_t manifest_events_lost = 0;
    std::vector<core::measure::Exclusion> manifest_exclusions;
    bool manifest_valid = false;

    std::uint64_t dropped_presents = 0;
    // v2: kare zaman ekseninin QPC kokeni (ms). Bilinmiyorsa null; istemci o zaman hiza iddia etmez.
    std::optional<double> capture_origin_ms;

    // Kare plani: interval_count * 4 == total_bytes (parcalarin toplam yuku).
    std::size_t interval_count = 0;
    std::size_t total_bytes = 0;
    // v4: ekran araliklari plani + yakalama sonucu.
    std::size_t display_interval_count = 0;
    std::size_t display_total_bytes = 0;
    core::measure::CaptureOutcome run_outcome = core::measure::CaptureOutcome::NotStarted;
    std::optional<double> target_exited_after_s;  // yalniz run_outcome == GameExited iken dolu
    // v5: GPU mesgul serisi plani. gpu_busy_count == 0 (izlenmedi) ya da == interval_count.
    std::size_t gpu_busy_count = 0;
    std::size_t gpu_busy_total_bytes = 0;
};

// GameCaptureResult + OS'tan okunan hedef kimligi -> baslik. Kare plani sonuc araliklarindan hesaplanir.
CaptureResultHeader header_from(const GameCaptureResult& r, const core::measure::ProcessIdentity& target);
// Baslik + cozulmus araliklar -> GameCaptureResult. Sayi/bayt tutarsizligi veya gecersiz aralik hatasi.
contracts::Result<GameCaptureResult> result_from(const CaptureResultHeader& h, std::vector<double> intervals);
// v4: ekran araliklari ayri seri; plan (display_interval_count/bytes) ile birebir eslesmeli.
contracts::Result<GameCaptureResult> result_from(const CaptureResultHeader& h, std::vector<double> intervals,
                                                 std::vector<double> display_intervals);
// v5: GPU mesgul serisi; plan (gpu_busy_count/bytes) ile birebir eslesmeli, bos ya da render araliklariyla ayni boyda.
contracts::Result<GameCaptureResult> result_from(const CaptureResultHeader& h, std::vector<double> intervals,
                                                 std::vector<double> display_intervals, std::vector<double> gpu_busy);

// JSON baslik yaz/oku. Okuyucu bilinmeyen alan, eksik alan, aralik disi deger ve tutarsiz kare planini reddeder.
std::string encode_header(const CaptureResultHeader& h);
contracts::Result<CaptureResultHeader> decode_header(std::string_view payload);

// float32 LE parca. n * 4 <= kMaxChunkBytes; her deger turune gore gecerli olmali (Interval: is_valid_interval;
// GpuBusy: is_valid_gpu_busy).
contracts::Status encode_chunk(const double* values, std::size_t n, std::string& out, ChunkKind kind = ChunkKind::Interval);
// bytes: 4'un kati, bos olamaz, <= kMaxChunkBytes; en fazla max_values deger. NaN/sonsuz reddedilir; Interval icin
// <= 0, GpuBusy icin negatif (eksi sifir dahil) deger reddedilir.
contracts::Result<std::vector<double>> decode_chunk(std::string_view bytes, std::size_t max_values,
                                                    ChunkKind kind = ChunkKind::Interval);

}  // namespace ef::measurement
