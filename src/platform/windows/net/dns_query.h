#pragma once
// T10: DNS cozucu yanit suresi olcumu. Salt okuma: yalniz "A" sorgusu gonderir (onbellek ve hosts dosyasi atlanir);
// hicbir ayar yazilmaz. Yalniz kullanici "DNS karsilastir" dediginde calisir. Alan adlari/sure/tur core sabitlerindendir
// (core/net/dns_bench.h); bu klasorde adres/host metni yazilmaz.
#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "core/net/dns_bench.h"

namespace ef::platform::net {

struct BenchResolver {
    std::string id;    // core::net::kDnsBenchCurrentId | hazir liste kimligi
    std::string ipv4;  // sorgunun gidecegi tek IPv4 sunucu
};

// DnsQueryEx (UDP, 53) ile belirtilen sunucuya tek A sorgusu; QPC ile ms. Basarisiz/zaman asimi/iptal: nullopt.
std::optional<double> dns_query_ms(const std::string& server_ipv4, const std::string& host, unsigned timeout_ms,
                                   const std::atomic<bool>* cancel);

// Arayuz GUID'ine ("{...}") gore Windows'un o an kullandigi IPv4 DNS sunuculari (DHCP'den gelenler dahil; salt okuma).
std::vector<std::string> system_dns_ipv4(const std::string& interface_guid);

// Tur x alan adi x cozucu, araya serpistirilmis (zamana bagli sapma tum cozuculere esit dagilir). cancel gelirse false.
bool run_dns_bench(const std::vector<BenchResolver>& resolvers, const std::atomic<bool>& cancel,
                   std::vector<core::net::ResolverSamples>& out);

}  // namespace ef::platform::net
