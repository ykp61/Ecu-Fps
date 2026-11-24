// EF-X01: gercek olay gunlugu salt okuma. Hicbir sey yazmaz; bulunan sayilar bu makinenin gercek sonucudur.
#include <cstdio>

#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/readiness/storage_reader.h"
#include "platform/windows/readiness/network_reader.h"
#include "platform/windows/adapters/win_dns.h"
#include "platform/windows/readiness/security_reader.h"

int main() {
    namespace cd = ef::core::diagnostics;
    const auto read = ef::platform::diagnostics::read_system_incidents(30);
    std::printf("status=%d events=%zu rejected=%zu truncated=%d detail=%s\n", static_cast<int>(read.status), read.events.size(), read.rejected,
                read.truncated ? 1 : 0, read.detail.c_str());
    const auto rep = cd::analyze_incidents(read, 30);
    for (const auto& f : rep.findings)
        std::printf("kind=%d count=%zu code=%s payload_invalid=%d\n", static_cast<int>(f.kind), f.count, f.bugcheck.c_str(), f.payload_invalid ? 1 : 0);
    // EF-X23: gercek disk sagligi okumasi (yalniz okuma).
    for (const auto& d : ef::platform::readiness::read_storage_health("").disks)
        std::printf("disk=%u support=%d used=%d spare=%d warn=%d media=%llu hours=%llu\n", d.disk, static_cast<int>(d.support),
                    d.nvme ? d.nvme->percent_used : -1, d.nvme ? d.nvme->available_spare : -1, d.nvme ? d.nvme->critical_warning : -1,
                    d.nvme ? static_cast<unsigned long long>(d.nvme->media_errors) : 0ull, d.nvme ? static_cast<unsigned long long>(d.nvme->power_on_hours) : 0ull);
    {   // EF-X14: salt okuma; DNS degistirilmez.
        const auto net = ef::platform::readiness::read_network(0);
        ef::platform::adapters::WinDnsApi dns;
        const auto cur = net.interface_guid.empty() ? ef::contracts::Result<std::string>::fail(ef::contracts::ErrorCode::MissingValue) : dns.read(net.interface_guid, false);
        const auto f = ef::platform::adapters::read_dns_policy_facts();
        std::printf("dns guid_known=%d read_ok=%d auto=%d policy=%d vpn=%d domain=%d nrpt=%d doh=%d\n", net.interface_guid.empty() ? 0 : 1, cur.ok() ? 1 : 0,
                    cur.ok() && cur.value().empty() ? 1 : 0, static_cast<int>(ef::core::adapters::windows::dns_policy(f)),
                    f.vpn_active ? static_cast<int>(*f.vpn_active) : -1, f.domain_joined ? static_cast<int>(*f.domain_joined) : -1,
                    f.nrpt_rules ? static_cast<int>(*f.nrpt_rules) : -1, f.doh_policy ? static_cast<int>(*f.doh_policy) : -1);
    }
    {   // EF-X09/X10: salt okuma.
        const auto sec = ef::platform::readiness::read_platform_security();
        const auto mpo = ef::platform::readiness::read_mpo_state();
        std::printf("security tpm=%d secure_boot=%d bitlocker=%d mpo_read=%d mpo_mode=%d\n", sec.tpm_version ? *sec.tpm_version : -1,
                    sec.secure_boot ? static_cast<int>(*sec.secure_boot) : -1, sec.bitlocker_on ? static_cast<int>(*sec.bitlocker_on) : -1,
                    mpo.read_ok ? 1 : 0, mpo.overlay_test_mode ? static_cast<int>(*mpo.overlay_test_mode) : -1);
    }
    // Sorgu sozdizimi reddedilirse (ERROR_EVT_INVALID_QUERY) test kalir: okuyucu gercekte calismiyor demektir.
    if (read.status == cd::ReadStatus::ReadError) return 1;
    return 0;
}
