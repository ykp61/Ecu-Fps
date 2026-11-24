// Ecu Fps shim: PrecisionWaiter gerceklemesi (upstream mantigi, gunluk/istisna yok).
#include "PrecisionWaiter.h"
#include "win/WinAPI.h"
#include <algorithm>

namespace pmon::util
{
    PrecisionWaiter::PrecisionWaiter(double waitBuffer) noexcept
        : defaultWaitBuffer_{ waitBuffer }
    {
        waitableTimer_ = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    PrecisionWaiter::~PrecisionWaiter()
    {
        if (waitableTimer_ != nullptr) {
            CloseHandle(waitableTimer_);
        }
    }
    double PrecisionWaiter::Wait(double seconds, bool alertable) noexcept
    {
        return WaitWithBuffer(seconds, defaultWaitBuffer_, alertable);
    }
    double PrecisionWaiter::WaitUnbuffered(double seconds, bool alertable) noexcept
    {
        return WaitWithBuffer(seconds, 0., alertable);
    }
    double PrecisionWaiter::WaitWithBuffer(double seconds, double buffer, bool) noexcept
    {
        if (waitableTimer_ != nullptr) {
            if (buffer > 0.) {
                qpcTimer_.Mark();
                if (seconds <= buffer) {
                    return qpcTimer_.SpinWaitUntil(seconds);
                }
            }
            LARGE_INTEGER waitTime100ns{};
            waitTime100ns.QuadPart = -LONGLONG(std::max(seconds - buffer, 0.) * 10'000'000.);
            if (SetWaitableTimerEx(waitableTimer_, &waitTime100ns, 0, nullptr, nullptr, nullptr, 0) &&
                WaitForSingleObject(waitableTimer_, INFINITE) == WAIT_OBJECT_0) {
                if (buffer > 0.) {
                    return qpcTimer_.SpinWaitUntil(seconds);
                }
                return 0.;
            }
            // Zamanlayici hatasi: sonraki cagrilar Sleep yedegini kullanir.
            CloseHandle(waitableTimer_);
            waitableTimer_ = nullptr;
        }
        Sleep(DWORD(seconds * 1000.));
        return 0.;
    }
}
