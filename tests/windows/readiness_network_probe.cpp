// CHK-06 / NET-01 Windows probu (SALT OKUMA; hicbir ag ayari degismez, yonetici gerekmez, pencere acmaz).
// Bu PC'de okunan gercek degerleri ve kural sonuclarini yazar. Varsayilan olarak HICBIR paket gondermez;
// yalniz "--ping" verilirse gecit + 1.1.1.1 + 8.8.8.8'e ICMP yanki istegi gonderir ("--samples N",
// varsayilan 20 = kurallarin karar icin istedigi en az ornek).
// ctest'e EKLENMEZ: ag ortamina bagli ve --ping ile paket gonderir.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#include "core/readiness/readiness.h"
#include "platform/windows/readiness/network_reader.h"

using namespace ef;
namespace cr = core::readiness;
namespace pr = platform::readiness;

namespace {
const char* level_name(cr::Level l) {
    switch (l) {
        case cr::Level::Good: return "Good";
        case cr::Level::Advice: return "Advice";
        case cr::Level::Problem: return "Problem";
        case cr::Level::Unknown: return "Unknown";
        case cr::Level::Info: return "Info";
    }
    return "?";
}
const char* ping_error_name(pr::PingError e) {
    switch (e) {
        case pr::PingError::None: return "none";
        case pr::PingError::InvalidTarget: return "invalid_target";
        case pr::PingError::IcmpOpenFailed: return "icmp_open_failed";
        case pr::PingError::Internal: return "internal";
    }
    return "?";
}
const char* action_name(cr::ActionKind a) {
    switch (a) {
        case cr::ActionKind::None: return "none";
        case cr::ActionKind::AutoFix: return "autofix";
        case cr::ActionKind::OpenSettings: return "settings";
        case cr::ActionKind::OpenUrl: return "url";
        case cr::ActionKind::RunNetworkTest: return "run_network_test";
    }
    return "?";
}
template <class T>
std::string opt(const std::optional<T>& v) {
    if (!v) return "nullopt";
    if constexpr (std::is_same_v<T, std::string>) return *v;
    else return std::to_string(*v);
}
void print_checks(const std::vector<cr::Check>& v) {
    for (const auto& c : v) {
        std::printf("  [%-7s] %-22s %-38s", level_name(c.level), c.id.c_str(), c.text_key.c_str());
        for (const auto& [k, val] : c.args) std::printf(" %s=%s", k.c_str(), val.c_str());
        std::printf(" | action=%s %s\n", action_name(c.action), c.action_arg.c_str());
    }
}
}  // namespace

int main(int argc, char** argv) {
    bool ping = false;
    int samples = 20;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ping") == 0) ping = true;
        else if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc) samples = std::atoi(argv[++i]);
    }
    if (samples < 1 || samples > 200) samples = 20;

    const auto s = pr::read_network(1000);
    std::printf("adapter: kind='%s' if_index=%u if_type=%u name='%s' desc='%s' read_note='%s'\n",
                s.info.adapter_kind.c_str(), s.if_index, s.if_type, s.adapter_name.c_str(), s.adapter_description.c_str(),
                s.read_note.c_str());
    std::printf("link_bps=%s gateway=%s background_rx_mbps=%s power_saving=%s\n", opt(s.info.link_bps).c_str(),
                opt(s.gateway_ipv4).c_str(), opt(s.info.background_rx_mbps).c_str(),
                s.info.adapter_power_saving ? (*s.info.adapter_power_saving ? "true" : "false") : "nullopt");
    if (s.info.adapter_kind == "wifi")
        std::printf("wifi: signal_pct=%s band_ghz=%s channel=%s freq_mhz=%s rssi=%s note='%s'\n",
                    opt(s.info.wifi_signal_pct).c_str(), opt(s.info.wifi_band_ghz).c_str(), opt(s.wifi_channel).c_str(),
                    opt(s.wifi_freq_mhz).c_str(), opt(s.wifi_rssi_dbm).c_str(), s.wifi_note.c_str());

    std::printf("checks (no ping):\n");
    auto pre = cr::network_checks(s.info, {});
    cr::sort_checks(pre);
    print_checks(pre);

    if (!ping) {
        std::printf("(ping not sent; pass --ping to run the ICMP test)\n");
        return 0;
    }
    const auto targets = pr::default_ping_targets(s);
    const auto runs = pr::run_ping_test(targets, samples, 200);
    const auto stats = pr::ping_test_stats(targets, runs);
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const auto& p = stats[i];
        std::printf("ping %-8s (%s): sent=%d recv=%d loss=%.1f%% avg=%.3f jitter=%.3f p95=%.3f ms err=%s/%u failed=%d | raw:",
                    p.target.c_str(), targets[i].ipv4.c_str(), p.sent, p.received, p.loss_pct(), p.avg_ms, p.jitter_ms,
                    p.p95_ms, ping_error_name(runs[i].error), runs[i].win32_error, p.failed ? 1 : 0);
        for (const auto& r : runs[i].rtt_ms) r ? std::printf(" %.2f", *r) : std::printf(" lost");
        std::printf("\n");
    }
    std::printf("checks (with ping):\n");
    auto post = cr::network_checks(s.info, stats);
    cr::sort_checks(post);
    print_checks(post);
    return 0;
}
