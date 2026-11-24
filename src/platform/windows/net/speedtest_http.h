#pragma once
// Internet hiz testi istemcisi (spec 2026-11-11 §7, ADR-027). Urunun tek HTTP istemcisi bu klasordedir.
//  - Yalniz kullanici Evet dedikten sonra, oyun kapisi acikken App'in calisma is parcaciginda cagrilir.
//  - Host/yollar core::net sabitlerinden (kSpeedHost, kDownPath, kUpPath); URL'de kullanici verisi yok.
//  - TLS dogrulamasi acik, sistem proxy'si, genel User-Agent "EcuFps", yonlendirme izlenmez.
//  - Butce: core::net::SpeedPlan (parca boyu, asama/toplam sure, toplam bayt). Iptal ve oyun kapisi
//    her okuma/yazma (<= 64 KiB) arasinda denetlenir.
//  - Done disindaki sonuc sayi tasimaz (core::net::sanitize_result).
// Bu baslik Win32 tiplerini disariya acmaz.

#include <atomic>
#include <cstdint>
#include <functional>

#include "core/net/speed_test.h"

namespace ef::platform::net {

class SpeedTestRunner {
public:
    // Asama, o anki parcanin olculen hizi (Mbit/s; Latency'de 0) ve toplam aktarilan bayt. En fazla 10 Hz
    // (asama degisiminde hemen) cagrilir; calisma is parcaciginda calisir.
    using Progress = std::function<void(core::net::SpeedPhase, double live_mbps, std::uint64_t bytes)>;
    core::net::SpeedResult run(const std::atomic<bool>& cancel, const std::function<bool()>& gate_blocked, Progress progress);
};

}  // namespace ef::platform::net
