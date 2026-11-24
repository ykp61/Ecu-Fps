#include "platform/windows/net/dns_query.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <windns.h>

#include <cstdint>
#include <cstring>
#include <memory>

namespace ef::platform::net {

namespace {

bool parse_ipv4(const std::string& s, std::uint8_t out[4]) {
    int idx = 0, val = 0, digits = 0;
    for (const char ch : s) {
        if (ch == '.') {
            if (digits == 0 || idx >= 3) return false;
            out[idx++] = static_cast<std::uint8_t>(val);
            val = 0, digits = 0;
        } else if (ch >= '0' && ch <= '9') {
            val = val * 10 + (ch - '0');
            if (++digits > 3 || val > 255) return false;
        } else {
            return false;
        }
    }
    if (idx != 3 || digits == 0) return false;
    out[3] = static_cast<std::uint8_t>(val);
    return true;
}

std::wstring widen(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (const char ch : s) w.push_back(static_cast<unsigned char>(ch));  // yalniz ASCII alan adlari
    return w;
}

struct Ctx {
    HANDLE ev = nullptr;
    LARGE_INTEGER done{};
    DWORD status = 0;
    PDNS_RECORD records = nullptr;
};

VOID WINAPI on_done(PVOID context, PDNS_QUERY_RESULT r) {
    auto* c = static_cast<Ctx*>(context);
    QueryPerformanceCounter(&c->done);
    c->status = static_cast<DWORD>(r->QueryStatus);
    c->records = r->pQueryRecords;
    SetEvent(c->ev);
}

}  // namespace

std::optional<double> dns_query_ms(const std::string& server_ipv4, const std::string& host, unsigned timeout_ms,
                                   const std::atomic<bool>* cancel) {
    std::uint8_t ip[4];
    if (!parse_ipv4(server_ipv4, ip) || host.empty()) return std::nullopt;
    if (cancel && cancel->load()) return std::nullopt;

    DNS_ADDR_ARRAY servers{};
    servers.MaxCount = 1;
    servers.AddrCount = 1;
    SOCKADDR_IN sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = static_cast<USHORT>((53 >> 8) | ((53 & 0xFF) << 8));  // htons(53)
    std::memcpy(&sa.sin_addr, ip, 4);
    static_assert(sizeof(SOCKADDR_IN) <= sizeof(servers.AddrArray[0].MaxSa));
    std::memcpy(servers.AddrArray[0].MaxSa, &sa, sizeof sa);

    // Zaman asiminda geri cagri hala yazabilir; baglam yalniz geri cagri kesin bittikten sonra silinir.
    auto* ctx = new Ctx;
    ctx->ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ctx->ev) {
        delete ctx;
        return std::nullopt;
    }
    const std::wstring name = widen(host);
    DNS_QUERY_REQUEST req{};
    req.Version = DNS_QUERY_REQUEST_VERSION1;
    req.QueryName = name.c_str();
    req.QueryType = DNS_TYPE_A;
    req.QueryOptions = DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_HOSTS_FILE | DNS_QUERY_NO_NETBT | DNS_QUERY_NO_MULTICAST;
    req.pDnsServerList = &servers;
    req.pQueryCompletionCallback = on_done;
    req.pQueryContext = ctx;
    DNS_QUERY_RESULT res{};
    res.Version = DNS_QUERY_RESULTS_VERSION1;
    DNS_QUERY_CANCEL cancel_handle{};

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    const DNS_STATUS st = DnsQueryEx(&req, &res, &cancel_handle);

    bool ok = false;
    if (st != DNS_REQUEST_PENDING) {
        QueryPerformanceCounter(&t1);  // es zamanli tamamlandi; geri cagri cagrilmaz
        ok = st == ERROR_SUCCESS && res.pQueryRecords != nullptr;
        if (res.pQueryRecords) DnsRecordListFree(res.pQueryRecords, DnsFreeRecordList);
        CloseHandle(ctx->ev);
        delete ctx;
    } else {
        const ULONGLONG start = GetTickCount64();
        bool finished = false;
        for (;;) {
            if (WaitForSingleObject(ctx->ev, 20) == WAIT_OBJECT_0) {
                finished = true;
                break;
            }
            if ((cancel && cancel->load()) || GetTickCount64() - start >= timeout_ms) break;
        }
        bool timed_out = false;
        if (!finished) {
            timed_out = true;
            (void)DnsCancelQuery(&cancel_handle);
            finished = WaitForSingleObject(ctx->ev, 5000) == WAIT_OBJECT_0;
        }
        if (!finished) return std::nullopt;  // geri cagri hic gelmedi: baglam bilerek sizdirilir (serbest sonrasi yazma riski yok)
        t1 = ctx->done;
        ok = !timed_out && ctx->status == ERROR_SUCCESS && ctx->records != nullptr;  // iptal edilen sorgu basarili sayilmaz
        if (ctx->records) DnsRecordListFree(ctx->records, DnsFreeRecordList);
        CloseHandle(ctx->ev);
        delete ctx;
    }
    if (!ok || freq.QuadPart <= 0) return std::nullopt;
    return 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
}

std::vector<std::string> system_dns_ipv4(const std::string& interface_guid) {
    std::vector<std::string> out;
    if (interface_guid.empty()) return out;
    ULONG len = 16 * 1024;
    std::unique_ptr<unsigned char[]> buf;
    constexpr ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 3 && rc == ERROR_BUFFER_OVERFLOW; ++i) {
        buf = std::make_unique<unsigned char[]>(len);
        rc = GetAdaptersAddresses(AF_INET, kFlags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.get()), &len);
    }
    if (rc != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.get()); a; a = a->Next) {
        if (!a->AdapterName || _stricmp(a->AdapterName, interface_guid.c_str()) != 0) continue;
        for (auto* d = a->FirstDnsServerAddress; d; d = d->Next) {
            if (!d->Address.lpSockaddr || d->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* in = reinterpret_cast<const SOCKADDR_IN*>(d->Address.lpSockaddr);
            const auto* b = reinterpret_cast<const unsigned char*>(&in->sin_addr);
            out.push_back(std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]));
        }
        break;
    }
    return out;
}

bool run_dns_bench(const std::vector<BenchResolver>& resolvers, const std::atomic<bool>& cancel,
                   std::vector<core::net::ResolverSamples>& out) {
    out.clear();
    for (const auto& r : resolvers) out.push_back({r.id, {}});
    for (int round = 0; round < core::net::kDnsBenchRounds; ++round) {
        for (const auto host : core::net::kDnsBenchHosts) {
            for (std::size_t i = 0; i < resolvers.size(); ++i) {
                if (cancel.load()) return false;
                out[i].ms.push_back(dns_query_ms(resolvers[i].ipv4, std::string(host), core::net::kDnsBenchTimeoutMs, &cancel));
            }
        }
    }
    return !cancel.load();
}

}  // namespace ef::platform::net
