#pragma once
// EF-X14: Get/SetInterfaceDnsSettings (iphlpapi, Windows 10 2004+; yoksa Unsupported) ve DNS ilke gercekleri.
#include "core/adapters/windows/dns.h"

namespace ef::platform::adapters {

class WinDnsApi final : public core::adapters::windows::DnsApi {
public:
    contracts::Result<std::string> read(const std::string& guid, bool v6) override;
    contracts::Status write(const std::string& guid, bool v6, const std::string& servers) override;
};

// Salt okuma. Okunamayan alan nullopt kalir (cagiran degisikligi onermez).
core::adapters::windows::DnsPolicyFacts read_dns_policy_facts();

}  // namespace ef::platform::adapters
