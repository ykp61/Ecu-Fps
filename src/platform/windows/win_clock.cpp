#include "platform/windows/win_clock.h"

#include <windows.h>

namespace ef::platform {

WinClock::WinClock() noexcept {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);  // Windows XP+ her zaman basarili
    freq_ = f.QuadPart;
}

core::MonoTime WinClock::mono_now() noexcept {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    // Tasma olmadan ns'ye cevir: saniye ve kalan ayri hesaplanir.
    const long long sec = c.QuadPart / freq_;
    const long long rem = c.QuadPart % freq_;
    return core::MonoTime{sec * 1'000'000'000LL + rem * 1'000'000'000LL / freq_};
}

core::UtcTime WinClock::utc_now() noexcept {
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME: 1601'den beri 100 ns. Unix epoch farki 11644473600 sn.
    constexpr long long kEpochDiff100ns = 116444736000000000LL;
    return core::UtcTime{(static_cast<long long>(u.QuadPart) - kEpochDiff100ns) / 10'000};
}

}  // namespace ef::platform
