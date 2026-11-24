#pragma once
// Metrik ornegi ve akis gecerliligi: kaynak, zaman alani, kayip ve kesinti alanlari.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "contracts/measure.h"
#include "contracts/models.h"
#include "contracts/result.h"
#include "core/time/time.h"

namespace ef::contracts {

// Ornek zamaninin hangi saatte olculdugu. Farkli alanlar eslenmeden karistirilmaz.
enum class TimeDomain { AppMonotonic, SourceQpc, SourceEtw, SourceDriver };

struct MetricSample {
    std::string source;      // ornek: "presentmon", "nvapi", "pdh"
    TimeDomain domain = TimeDomain::AppMonotonic;
    core::MonoTime t;        // domain saatinde zaman
    std::string metric_id;   // ornek: "gpu.busy"
    Measurement value = Measurement::missing(Unit::Count, MissingReason::NotSampled);
    core::Duration interval; // beklenen ornekleme araligi
};

// Kaynak saatinden uygulama monoton saatine bilinen eslem (sabit ofset).
struct ClockMapping {
    TimeDomain from;
    std::int64_t offset_ns;  // app = source + offset
};

// Eslem yoksa NotAligned; teshis bu durumda abstain olmalidir.
Result<core::MonoTime> align(const MetricSample& s, const std::vector<ClockMapping>& mappings);

// Hizalama basarisizsa teshis guvenini abstain'e cevirir.
Confidence confidence_after_alignment(bool aligned, Confidence wanted) noexcept;

// Tek metrik akisi: sira disi ornek kesinti, araliktaki bosluk kayip sayilir.
class SampleStream {
public:
    explicit SampleStream(core::Duration expected_interval) : interval_(expected_interval) {}

    // Geriye giden zaman: ornek reddedilir ve kesinti sayaci artar.
    Status add(const MetricSample& s);

    std::size_t accepted() const noexcept { return samples_.size(); }
    std::uint64_t lost_estimate() const noexcept { return lost_; }
    std::uint64_t discontinuities() const noexcept { return discontinuities_; }
    const std::vector<MetricSample>& samples() const noexcept { return samples_; }

    // Kesinti varsa kosu gecerli sayilmaz.
    RunValidity validity(std::size_t min_samples) const noexcept;

private:
    core::Duration interval_;
    std::vector<MetricSample> samples_;
    std::uint64_t lost_ = 0;
    std::uint64_t discontinuities_ = 0;
};

}  // namespace ef::contracts
