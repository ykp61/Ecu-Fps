#include "platform/windows/metrics/net_rate_sampler.h"

// winsock2 windows.h'den once gelmeli (yalniz tipler; ws2_32 baglanmaz).
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include "platform/windows/readiness/network_reader.h"

namespace ef::platform::metrics {

using contracts::Measurement;
using contracts::MissingReason;
using contracts::Unit;
namespace cm = core::metrics;

namespace {
constexpr std::int64_t kIfRefreshMs = 10'000;

void mark_all(cm::LiveSnapshot& s, MissingReason r) {
    s.net_down_mbps = s.net_up_mbps = s.net_link_mbps = Measurement::missing(Unit::Mbps, r);
}
}  // namespace

void NetRateSampler::sample(cm::LiveSnapshot& s, std::int64_t mono_ms) {
    if (if_ms_ < 0 || mono_ms - if_ms_ >= kIfRefreshMs || mono_ms < if_ms_) {
        if_ms_ = mono_ms;
        DWORD best = 0;
        const auto probe = readiness::parse_ipv4("1.1.1.1");
        const DWORD br = probe ? GetBestInterface(static_cast<IPAddr>(*probe), &best) : ERROR_INVALID_PARAMETER;
        if_index_ = (br == NO_ERROR) ? static_cast<std::uint32_t>(best) : 0;
    }
    if (if_index_ == 0) {
        prev_.reset();
        mark_all(s, MissingReason::SourceError);
        return;
    }
    MIB_IF_ROW2 row{};
    row.InterfaceIndex = if_index_;
    if (GetIfEntry2(&row) != NO_ERROR) {
        prev_.reset();
        if_ms_ = -1;  // bagdastirici kayboldu: sonraki ornekte yeniden sec
        mark_all(s, MissingReason::SourceError);
        return;
    }
    cm::NetCounters cur;
    cur.if_index = if_index_;
    cur.in_octets = row.InOctets;
    cur.out_octets = row.OutOctets;
    cur.link_bps = row.ReceiveLinkSpeed;
    cur.mono_ms = mono_ms;
    const auto r = cm::net_rates(prev_, cur);
    s.net_down_mbps = r.down;
    s.net_up_mbps = r.up;
    s.net_link_mbps = r.link;
    prev_ = cur;
}

}  // namespace ef::platform::metrics
