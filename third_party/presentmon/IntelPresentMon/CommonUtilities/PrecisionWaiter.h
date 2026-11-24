// Ecu Fps shim: Intel PresentMon CommonUtilities/PrecisionWaiter.h yerine.
// Arayuz Intel PresentMon v2.6.0'dan turetilmistir (Copyright (C) 2022 Intel Corporation, MIT).
// win::Handle yerine ham HANDLE kullanilir; davranis upstream ile aynidir
// (yuksek cozunurluklu bekleme zamanlayicisi + QPC spin, basarisizlikta Sleep).
#pragma once
#include "Qpc.h"

namespace pmon::util
{
    class PrecisionWaiter
    {
    public:
        static constexpr double standardWaitBuffer = 0.001;

        PrecisionWaiter(double defaultWaitBuffer = standardWaitBuffer) noexcept;
        PrecisionWaiter(const PrecisionWaiter&) = delete;
        PrecisionWaiter& operator=(const PrecisionWaiter&) = delete;
        PrecisionWaiter(PrecisionWaiter&&) = delete;
        PrecisionWaiter& operator=(PrecisionWaiter&&) = delete;
        ~PrecisionWaiter();
        double Wait(double seconds, bool alertable = false) noexcept;
        double WaitUnbuffered(double seconds, bool alertable = false) noexcept;
        double WaitWithBuffer(double seconds, double buffer, bool alertable = false) noexcept;
    private:
        double defaultWaitBuffer_;
        QpcTimer qpcTimer_;
        void* waitableTimer_ = nullptr;
    };
}
