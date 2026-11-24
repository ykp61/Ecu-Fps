#pragma once
// Olcum degeri: birimi acik, eksik deger 0 degil nullopt + neden.

#include <optional>
#include <string_view>

#include "contracts/result.h"

namespace ef::contracts {

enum class Unit { Fps, Milliseconds, Percent, MiB, Celsius, Watt, MHz, BytesPerSecond, Count, MBps, Mbps };
// MBps = megabayt/saniye (1e6 B/s, disk akisi); Mbps = megabit/saniye (1e6 bit/s, ag hizi).
std::string_view to_string(Unit u) noexcept;
std::optional<Unit> parse_unit(std::string_view s) noexcept;

enum class MissingReason {
    None,            // deger mevcut
    NotSupported,    // kaynak bu metrigi vermiyor
    PermissionDenied,
    NotSampled,      // henuz ornek yok
    SourceError,
    Invalid,         // ornek gecersiz isaretlendi
};
std::string_view to_string(MissingReason r) noexcept;
std::optional<MissingReason> parse_missing_reason(std::string_view s) noexcept;

class Measurement {
public:
    static Measurement of(double value, Unit unit) noexcept { return Measurement(value, unit); }
    static Measurement missing(Unit unit, MissingReason reason) noexcept {
        return Measurement(unit, reason == MissingReason::None ? MissingReason::NotSampled : reason);
    }

    bool has_value() const noexcept { return value_.has_value(); }
    std::optional<double> value() const noexcept { return value_; }
    Unit unit() const noexcept { return unit_; }
    MissingReason missing_reason() const noexcept { return reason_; }

    // Ayni birim ve iki deger de mevcut olmali. Aksi halde hata; eksik deger 0 sayilmaz.
    Result<double> difference(const Measurement& other) const;
    Result<int> compare(const Measurement& other) const;  // -1, 0, 1

private:
    Measurement(double v, Unit u) noexcept : value_(v), unit_(u), reason_(MissingReason::None) {}
    Measurement(Unit u, MissingReason r) noexcept : unit_(u), reason_(r) {}

    std::optional<double> value_;
    Unit unit_;
    MissingReason reason_;
};

}  // namespace ef::contracts
