#include "contracts/metrics.h"

namespace ef::contracts {

Result<core::MonoTime> align(const MetricSample& s, const std::vector<ClockMapping>& mappings) {
    if (s.domain == TimeDomain::AppMonotonic) return s.t;
    for (const auto& m : mappings) {
        if (m.from == s.domain) return core::MonoTime{s.t.ns + m.offset_ns};
    }
    return Result<core::MonoTime>::fail(ErrorCode::NotAligned, s.source);
}

Confidence confidence_after_alignment(bool aligned, Confidence wanted) noexcept {
    return aligned ? wanted : Confidence::Abstain;
}

Status SampleStream::add(const MetricSample& s) {
    if (!samples_.empty()) {
        const auto& last = samples_.back();
        if (s.domain != last.domain) {
            ++discontinuities_;
            return Status::fail(ErrorCode::NotAligned, "time domain changed");
        }
        auto gap = core::elapsed(last.t, s.t);
        if (!gap.ok() || gap.value().ns == 0) {
            ++discontinuities_;
            return Status::fail(ErrorCode::NegativeDuration, s.source);
        }
        const std::int64_t g = gap.value().ns;
        // 1.5 araliktan buyuk bosluk: aradaki tahmini kayip ornek sayisi (en az 1).
        if (interval_.ns > 0 && g * 2 > interval_.ns * 3) {
            const std::int64_t slots = (g + interval_.ns / 2) / interval_.ns;
            lost_ += static_cast<std::uint64_t>(slots > 1 ? slots - 1 : 1);
        }
    }
    samples_.push_back(s);
    return ok_status();
}

RunValidity SampleStream::validity(std::size_t min_samples) const noexcept {
    if (discontinuities_ > 0) return RunValidity::InvalidClockDiscontinuity;
    if (samples_.size() < min_samples) return RunValidity::InvalidInsufficientSamples;
    return RunValidity::Valid;
}

}  // namespace ef::contracts
