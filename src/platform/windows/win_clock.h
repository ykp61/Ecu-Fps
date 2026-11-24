#pragma once
// Gercek Windows saati: QueryPerformanceCounter (monoton) + GetSystemTimePreciseAsFileTime (UTC).

#include "core/time/time.h"

namespace ef::platform {

class WinClock final : public core::Clock {
public:
    WinClock() noexcept;
    core::MonoTime mono_now() noexcept override;
    core::UtcTime utc_now() noexcept override;

private:
    long long freq_;
};

}  // namespace ef::platform
