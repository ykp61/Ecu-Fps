// Task 12: GERCEK AG hiz testi probu (istege bagli). Varsayilan derlemede ve ctest'te YOKTUR (EXCLUDE_FROM_ALL,
// add_test yok). Ana bilgisayarda yalniz urun sahibi acikca isterse calistirilir:
//   cmake --build --preset x64-dist --target ef_speedtest_probe
//   ef_speedtest_probe --real-network
// En fazla ~256 MB veri ve ~60 sn. Ayar degistirmez; sonuc yalniz konsola yazilir.
#include <atomic>
#include <cstdio>
#include <cstring>

#include "platform/windows/net/speedtest_http.h"

using namespace ef;

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--real-network") != 0) {
        std::printf("speedtest_probe: gercek ag kullanir; calistirmak icin --real-network verin.\n");
        return 2;
    }
    std::atomic<bool> cancel{false};
    platform::net::SpeedTestRunner runner;
    const auto r = runner.run(cancel, [] { return false; }, [](core::net::SpeedPhase ph, double mbps, std::uint64_t bytes) {
        std::printf("  phase=%d live=%.1f Mbps bytes=%llu\n", static_cast<int>(ph), mbps, static_cast<unsigned long long>(bytes));
    });
    std::printf("phase=%d fail=%.*s down=%.1f up=%.1f latency=%.1f bytes=%llu\n", static_cast<int>(r.phase),
                static_cast<int>(core::net::speed_fail_key(r.fail).size()), core::net::speed_fail_key(r.fail).data(),
                r.down_mbps.value_or(-1), r.up_mbps.value_or(-1), r.latency_ms.value_or(-1), static_cast<unsigned long long>(r.bytes_used));
    const bool ok = r.phase == core::net::SpeedPhase::Done && r.down_mbps && r.up_mbps && r.latency_ms &&
                    r.bytes_used <= core::net::kSpeedMaxBytes;
    std::printf(ok ? "SPEEDTEST OK\n" : "SPEEDTEST FAIL\n");
    return ok ? 0 : 1;
}
