#pragma once
// DIAG-01: teshis icin kanit zaman cizgisi (saf; birim testlenir). Kare, sayac ve G/C olaylari tek bir
// guvenilir zaman ekseninde pencerelere bolunur. Her pencere kaynak kosuyu (run id) ve pencereyi tasir.
//  - Saat alani (clock domain) eslesmeyen kaynak pencereye baglanmaz; o kaynakla teshis kesinlesmez.
//  - Gecersiz/eksik ornek "eksik" olarak kalir (0 degil); test kapsami disindaki pencereler (menu/yukleme,
//    isinma) filtrelenir.

#include <optional>
#include <string>
#include <vector>

#include "core/measure/frame_stats.h"
#include "core/measure/telemetry.h"

namespace ef::core::diagnosis {

enum class ClockDomain { Qpc, SteadyClock, Unknown };

struct SourceInfo {
    std::string id;  // "frames", "pdh", "etw_io"...
    ClockDomain domain = ClockDomain::Unknown;
    double offset_ms = 0;  // ortak eksene donusum (dogrulanmis ise)
    bool offset_verified = false;
};

enum class WindowScope { Measured, Warmup, MenuOrLoading, Excluded };

struct EvidenceWindow {
    std::string run_id;
    double start_ms = 0, end_ms = 0;
    WindowScope scope = WindowScope::Measured;
    measure::FrameStats frames;
    std::vector<measure::WindowContext> metrics;  // ayni pencereye denk gelen sayaclar
    std::vector<std::string> unaligned_sources;   // zaman eslenemeyen kaynaklar
    bool valid = false;                           // kare istatistigi yeterli ve kapsam Measured
};

struct Timeline {
    std::string run_id;
    ClockDomain frame_domain = ClockDomain::Qpc;
    std::vector<EvidenceWindow> windows;
    // Teshis yalniz gecerli ve hizali pencerelerden; yoksa "veri yetersiz".
    std::vector<const EvidenceWindow*> usable() const;
};

// Kaynak cerceve saatiyle ayni alandaysa (veya ofset dogrulanmissa) hizalidir.
bool aligned(const SourceInfo& s, ClockDomain frame_domain) noexcept;

// Kare araliklarini [start, end) pencerelere boler (window_ms), her pencereye hizali kaynaklarin
// orneklerinden baglam ekler. scopes: (bas, bit, kapsam) — kapsam disi pencereler gecersiz isaretlenir.
struct ScopeRange {
    double start_ms, end_ms;
    WindowScope scope;
};
struct FramePoint {
    double t_ms;         // sunum zamani
    double interval_ms;  // onceki kareden aralik
};
Timeline build_timeline(std::string run_id, ClockDomain frame_domain, const std::vector<FramePoint>& frames,
                        const std::vector<std::pair<SourceInfo, std::vector<measure::TelemetrySample>>>& sources,
                        const std::vector<std::string>& metric_names, const std::vector<ScopeRange>& scopes, double window_ms);

}  // namespace ef::core::diagnosis
