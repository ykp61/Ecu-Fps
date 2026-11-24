#pragma once
// Monoton yakalama zamani ile UTC log zamani ayri tiplerdir; ortuk donusum yoktur.
// Sure hesabi yalniz MonoTime ile yapilir; UTC saat sicramasi sureyi etkilemez.

#include <compare>
#include <cstdint>

#include "contracts/result.h"

namespace ef::core {

struct Duration {
    std::int64_t ns = 0;
    static constexpr Duration from_ms(std::int64_t ms) noexcept { return {ms * 1'000'000}; }
    static constexpr Duration from_us(std::int64_t us) noexcept { return {us * 1'000}; }
    constexpr double ms() const noexcept { return static_cast<double>(ns) / 1e6; }
    constexpr double seconds() const noexcept { return static_cast<double>(ns) / 1e9; }
    constexpr auto operator<=>(const Duration&) const = default;
};

// Keyfi baslangicli monoton zaman (ns). Yalniz ayni oturum icinde anlamlidir.
struct MonoTime {
    std::int64_t ns = 0;
    constexpr auto operator<=>(const MonoTime&) const = default;
    constexpr MonoTime operator+(Duration d) const noexcept { return {ns + d.ns}; }
};

// UTC duvar saati (Unix epoch, ms). Yalniz kayit/gosterim icindir.
struct UtcTime {
    std::int64_t unix_ms = 0;
    constexpr auto operator<=>(const UtcTime&) const = default;
};

// to < from ise NegativeDuration hatasi; uydurma/negatif sure uretilmez.
inline contracts::Result<Duration> elapsed(MonoTime from, MonoTime to) {
    if (to < from) return contracts::Result<Duration>::fail(contracts::ErrorCode::NegativeDuration);
    return Duration{to.ns - from.ns};
}

class Clock {
public:
    virtual ~Clock() = default;
    virtual MonoTime mono_now() noexcept = 0;
    virtual UtcTime utc_now() noexcept = 0;
};

// Testler icin elle surulen saat. UTC bagimsiz olarak ileri/geri sicratilabilir.
class FakeClock final : public Clock {
public:
    MonoTime mono_now() noexcept override { return mono_; }
    UtcTime utc_now() noexcept override { return utc_; }
    void advance(Duration d) noexcept {
        mono_.ns += d.ns;
        utc_.unix_ms += d.ns / 1'000'000;
    }
    void jump_utc(std::int64_t delta_ms) noexcept { utc_.unix_ms += delta_ms; }

private:
    MonoTime mono_{1'000'000'000};
    UtcTime utc_{1'700'000'000'000};
};

}  // namespace ef::core
