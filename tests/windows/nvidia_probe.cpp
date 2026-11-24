// GPU-02 Windows testi (salt okuma): NVAPI acilir, surucu surumu, telemetri ve global DRS degerleri.
// Cikti: "driver=<v> temp=<c> util=<p> power=<hex>@<loc> vsync=<hex>@<loc>"; runtime yoksa exit 3.
#include <cstdio>

#include "core/adapters/nvidia/drs.h"
#include "platform/windows/nvidia/nvapi_drs.h"

using namespace ef;
using core::adapters::nvidia::DrsSetting;

int wmain() {
    auto nv = platform::nvidia::Nvapi::open();
    if (!nv.ok()) {
        std::printf("unavailable: %s\n", nv.error().detail.c_str());
        return 3;
    }
    const auto t = nv.value()->telemetry();
    std::printf("name=%s driver=%s temp=%d util=%d", t.name.c_str(), nv.value()->driver_version().c_str(),
                t.temperature_c.value_or(-1), t.gpu_utilization_pct.value_or(-1));
    // F-61: surucu perf-decrease maskesi ve grafik saati (desteklenmiyorsa n/a; uydurma yok).
    if (t.perf_decrease_mask) std::printf(" perfcap=%u", *t.perf_decrease_mask);
    else std::printf(" perfcap=n/a");
    if (t.graphics_clock_mhz) std::printf(" clock=%.0fMHz", *t.graphics_clock_mhz);
    else std::printf(" clock=n/a");
    auto drs = nv.value()->drs();
    if (!drs) {
        std::printf(" drs=unavailable\n");
        return 4;
    }
    for (auto s : {DrsSetting::PowerMode, DrsSetting::VSync, DrsSetting::FrameRateLimit}) {
        core::adapters::nvidia::DrsTarget tg{true, {}, static_cast<std::uint32_t>(s)};
        auto v = drs->get(tg);
        if (v.ok())
            std::printf(" %s=%08X@%s", core::adapters::nvidia::catalog_id(s).data(), v.value().value,
                        core::adapters::nvidia::to_string(v.value().location).data());
        else
            std::printf(" %s=error(%s)", core::adapters::nvidia::catalog_id(s).data(), v.error().detail.c_str());
    }
    std::printf("\n");
    return (t.temperature_c && t.gpu_utilization_pct) ? 0 : 5;
}
