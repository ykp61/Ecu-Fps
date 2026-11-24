#include "platform/windows/readiness/network_reader.h"

// winsock2 windows.h'den once gelmeli (WIN32_LEAN_AND_MEAN altinda).
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <objbase.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netioapi.h>
#include <wlanapi.h>  // yalniz tipler; DLL calisma aninda yuklenir (WlanApi)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <system_error>
#include <thread>

namespace ef::platform::readiness {

namespace cr = core::readiness;

// ---------------- Saf yardimcilar ----------------

std::optional<std::uint32_t> parse_ipv4(std::string_view s) {
    std::uint32_t out = 0;
    int part = 0;
    std::size_t i = 0;
    while (part < 4) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return std::nullopt;
        unsigned v = 0;
        int digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + static_cast<unsigned>(s[i] - '0');
            if (++digits > 3 || v > 255) return std::nullopt;
            ++i;
        }
        out |= static_cast<std::uint32_t>(v) << (8 * part);
        ++part;
        if (part < 4) {
            if (i >= s.size() || s[i] != '.') return std::nullopt;
            ++i;
        }
    }
    if (i != s.size()) return std::nullopt;
    return out;
}

std::string format_ipv4(std::uint32_t a) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", a & 0xffu, (a >> 8) & 0xffu, (a >> 16) & 0xffu, (a >> 24) & 0xffu);
    return buf;
}

std::optional<double> wifi_band_from_freq_mhz(unsigned mhz) {
    if (mhz >= 2400 && mhz <= 2500) return 2.4;
    if (mhz >= 5150 && mhz <= 5895) return 5.0;
    if (mhz >= 5925 && mhz <= 7125) return 6.0;
    return std::nullopt;
}

std::optional<double> wifi_band_from_channel(unsigned ch, bool may_be_6ghz) {
    const bool in24 = ch >= 1 && ch <= 14;
    const bool in5 = ch >= 32 && ch <= 177;
    // 6 GHz kanallari: 2 ve 1,5,9,...,233 (ch % 4 == 1).
    const bool six_possible = may_be_6ghz && ch >= 1 && ch <= 233 && (ch == 2 || ch % 4 == 1);
    if (six_possible) {
        if (in24 || in5) return std::nullopt;  // cakisan kanal: tahmin yok
        return 6.0;
    }
    if (in24) return 2.4;
    if (in5) return 5.0;
    return std::nullopt;
}

int wifi_signal_pct_from_rssi(long rssi_dbm) {
    const long pct = 2 * (rssi_dbm + 100);
    return static_cast<int>(std::clamp(pct, 0L, 100L));
}

std::optional<double> rx_mbps(std::uint64_t before, std::uint64_t after, double seconds) {
    if (!(seconds > 0.0) || after < before) return std::nullopt;
    return static_cast<double>(after - before) * 8.0 / seconds / 1e6;
}

std::string adapter_kind_from_type(std::uint32_t if_type, std::uint32_t physical_medium, bool hardware) {
    constexpr std::uint32_t kMedium80211 = 9;  // NdisPhysicalMediumNative802_11
    constexpr std::uint32_t kMedium8023 = 14;  // NdisPhysicalMedium802_3
    if (if_type == 71 /* IF_TYPE_IEEE80211 */) return "wifi";
    if (if_type == 6 /* IF_TYPE_ETHERNET_CSMACD */) {
        if (physical_medium == kMedium80211) return "wifi";
        if (hardware || physical_medium == kMedium8023) return "ethernet";
    }
    return "other";  // VPN/sanal bagdastirici, mobil genis bant ...
}

// ---------------- Win32 okuyucu ----------------

namespace {

std::string utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// wlanapi.dll calisma aninda System32'den yuklenir (statik baglanti yok): Wireless LAN hizmeti
// kurulu olmayan sistemlerde (or. Windows Server) DLL bulunmayabilir; o zaman Wi-Fi alanlari nullopt.
// wlanapi.h yalniz tipler ve decltype icin kullanilir; fonksiyonlar dogrudan cagrilmaz.
class WlanApi {
public:
    decltype(&WlanOpenHandle) open = nullptr;
    decltype(&WlanCloseHandle) close = nullptr;
    decltype(&WlanFreeMemory) free_memory = nullptr;
    decltype(&WlanQueryInterface) query = nullptr;
    decltype(&WlanGetInterfaceCapability) get_capability = nullptr;

    WlanApi() {
        mod_ = LoadLibraryExW(L"wlanapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!mod_) return;
        load(open, "WlanOpenHandle");
        load(close, "WlanCloseHandle");
        load(free_memory, "WlanFreeMemory");
        load(query, "WlanQueryInterface");
        load(get_capability, "WlanGetInterfaceCapability");
    }
    ~WlanApi() {
        if (mod_) FreeLibrary(mod_);
    }
    WlanApi(const WlanApi&) = delete;
    WlanApi& operator=(const WlanApi&) = delete;

    bool loaded() const { return mod_ != nullptr; }
    bool ok() const { return open && close && free_memory && query && get_capability; }

private:
    template <class F>
    void load(F& fn, const char* name) {
        fn = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(mod_, name)));
    }
    HMODULE mod_ = nullptr;
};

// RAII sarmalayicilar; WlanApi'den once yok edilmeleri icin read_wifi icinde api'den sonra tanimlanir.
struct WlanHandle {
    const WlanApi& api;
    HANDLE h = nullptr;
    explicit WlanHandle(const WlanApi& a) : api(a) {}
    ~WlanHandle() {
        if (h) api.close(h, nullptr);
    }
    WlanHandle(const WlanHandle&) = delete;
    WlanHandle& operator=(const WlanHandle&) = delete;
};

template <class T>
struct WlanMem {
    const WlanApi& api;
    T* p = nullptr;
    explicit WlanMem(const WlanApi& a) : api(a) {}
    ~WlanMem() {
        if (p) api.free_memory(p);
    }
    WlanMem(const WlanMem&) = delete;
    WlanMem& operator=(const WlanMem&) = delete;
};

// Yalniz konum izni gerektirmeyen opcode'lar (bkz. baslik).
// DIKKAT: bu Wi-Fi okuma yolu henuz gercek Wi-Fi donaniminda DOGRULANMADI (gelistirme PC'si kablolu);
// struct duzeni ve opcode davranisi Microsoft belgelerine gore yazildi. Saha dogrulamasi gerekir.
void read_wifi(NetworkSnapshot& s, const GUID& guid) {
    const WlanApi api;
    if (!api.loaded()) {
        s.wifi_note = "wlanapi.dll=" + std::to_string(GetLastError());
        return;
    }
    if (!api.ok()) {
        s.wifi_note = "wlanapi.dll missing export";
        return;
    }

    WlanHandle wh(api);
    DWORD ver = 0;
    const DWORD open = api.open(2, nullptr, &ver, &wh.h);
    if (open != ERROR_SUCCESS) {
        s.wifi_note = "WlanOpenHandle=" + std::to_string(open);
        wh.h = nullptr;
        return;
    }

    // 1) Gercek zamanli baglanti kalitesi (Windows 11 24H2+): frekans + kalite.
    {
        DWORD size = 0;
        WlanMem<WLAN_REALTIME_CONNECTION_QUALITY> q(api);
        const DWORD r = api.query(wh.h, &guid, wlan_intf_opcode_realtime_connection_quality, nullptr, &size,
                                  reinterpret_cast<PVOID*>(&q.p), nullptr);
        if (r == ERROR_SUCCESS && q.p && size >= sizeof(WLAN_REALTIME_CONNECTION_QUALITY)) {
            if (q.p->ulLinkQuality <= 100) s.info.wifi_signal_pct = static_cast<int>(q.p->ulLinkQuality);
            const ULONG links = q.p->ulNumLinks;
            const std::size_t room =
                (size - offsetof(WLAN_REALTIME_CONNECTION_QUALITY, linksInfo)) / sizeof(WLAN_REALTIME_CONNECTION_QUALITY_LINK_INFO);
            const std::size_t count = std::min<std::size_t>(links, room);
            for (std::size_t i = 0; i < count; ++i) {
                const auto& li = q.p->linksInfo[i];
                const auto band = wifi_band_from_freq_mhz(li.ulChannelCenterFrequencyMhz);
                // MLO: birden cok baglanti varsa en yuksek bant raporlanir.
                if (band && (!s.info.wifi_band_ghz || *band > *s.info.wifi_band_ghz)) {
                    s.info.wifi_band_ghz = band;
                    s.wifi_freq_mhz = li.ulChannelCenterFrequencyMhz;
                }
                if (i == 0 && li.lRssi < 0) s.wifi_rssi_dbm = li.lRssi;
            }
            s.wifi_note = "realtime_quality";
        } else {
            s.wifi_note = "realtime_quality=" + std::to_string(r);
        }
    }

    // 2) RSSI -> sinyal yuzdesi (kalite okunamadiysa).
    if (!s.wifi_rssi_dbm || !s.info.wifi_signal_pct) {
        DWORD size = 0;
        WlanMem<LONG> rssi(api);
        if (api.query(wh.h, &guid, wlan_intf_opcode_rssi, nullptr, &size, reinterpret_cast<PVOID*>(&rssi.p), nullptr) ==
                ERROR_SUCCESS &&
            rssi.p && size >= sizeof(LONG) && *rssi.p < 0) {
            s.wifi_rssi_dbm = *rssi.p;
            if (!s.info.wifi_signal_pct) s.info.wifi_signal_pct = wifi_signal_pct_from_rssi(*rssi.p);
            s.wifi_note += " rssi";
        }
    }

    // 3) Kanal -> bant (frekans okunamadiysa). 6 GHz belirsizligi icin bagdastirici PHY yetenegi.
    {
        DWORD size = 0;
        WlanMem<ULONG> ch(api);
        if (api.query(wh.h, &guid, wlan_intf_opcode_channel_number, nullptr, &size, reinterpret_cast<PVOID*>(&ch.p),
                      nullptr) == ERROR_SUCCESS &&
            ch.p && size >= sizeof(ULONG) && *ch.p > 0) {
            s.wifi_channel = static_cast<unsigned>(*ch.p);
        }
    }
    if (!s.info.wifi_band_ghz && s.wifi_channel) {
        bool may_be_6 = true;  // bilinmiyorsa temkinli
        WlanMem<WLAN_INTERFACE_CAPABILITY> cap(api);
        if (api.get_capability(wh.h, &guid, nullptr, &cap.p) == ERROR_SUCCESS && cap.p) {
            may_be_6 = false;
            const DWORD n = std::min<DWORD>(cap.p->dwNumberOfSupportedPhys, WLAN_MAX_PHY_INDEX);
            for (DWORD i = 0; i < n; ++i) {
                const auto t = static_cast<int>(cap.p->dot11PhyTypes[i]);
                if (t == 10 /* he (11ax) */ || t == 11 /* eht (11be) */) may_be_6 = true;
            }
        }
        s.info.wifi_band_ghz = wifi_band_from_channel(*s.wifi_channel, may_be_6);
        s.wifi_note += may_be_6 ? " channel(6ghz-capable)" : " channel";
    }
}

// Okuma hatasi: baglanti var/yok bilinmiyor (bos adapter_kind "baglanti yok" demektir, ondan ayri).
void mark_unknown(NetworkSnapshot& s, const char* step, unsigned long err) {
    s.info.adapter_kind = "unknown";
    s.read_note = std::string(step) + "=" + std::to_string(err);
}

}  // namespace

NetworkSnapshot read_network(unsigned rx_sample_ms) {
    NetworkSnapshot s;

    // Varsayilan IPv4 yolu: GetBestInterface yalniz yonlendirme tablosuna bakar, paket gondermez.
    DWORD best = 0;
    const auto probe = parse_ipv4("1.1.1.1");
    const DWORD br = probe ? GetBestInterface(static_cast<IPAddr>(*probe), &best) : ERROR_INVALID_PARAMETER;
    if (br == ERROR_NETWORK_UNREACHABLE || br == ERROR_NOT_FOUND) {
        s.read_note = "GetBestInterface=" + std::to_string(br) + " (no route)";
        return s;  // adapter_kind bos = baglanti yok
    }
    if (br != NO_ERROR || best == 0) {
        mark_unknown(s, "GetBestInterface", br);
        return s;
    }
    s.if_index = best;

    MIB_IF_ROW2 row{};
    row.InterfaceIndex = best;
    const DWORD er = GetIfEntry2(&row);
    if (er != NO_ERROR) {
        mark_unknown(s, "GetIfEntry2", er);
        return s;
    }
    // Ortam kopuk VEYA arayuz calismiyor -> baglanti yok. MediaConnectStateUnknown (bazi tuneller)
    // tek basina kopuk sayilmaz; OperStatus Unknown/Testing ise karar verilemez -> unknown.
    const bool media_down = row.MediaConnectState == MediaConnectStateDisconnected;
    const bool oper_down = row.OperStatus == IfOperStatusDown || row.OperStatus == IfOperStatusNotPresent ||
                           row.OperStatus == IfOperStatusLowerLayerDown || row.OperStatus == IfOperStatusDormant;
    if (media_down || oper_down) {
        s.read_note = "disconnected";
        return s;
    }
    if (row.OperStatus != IfOperStatusUp) {
        mark_unknown(s, "OperStatus", static_cast<unsigned long>(row.OperStatus));
        return s;
    }

    s.if_type = row.Type;
    s.info.adapter_kind = adapter_kind_from_type(row.Type, static_cast<std::uint32_t>(row.PhysicalMediumType),
                                                 row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE);
    if (row.ReceiveLinkSpeed > 0 && row.ReceiveLinkSpeed != ~0ull) s.info.link_bps = row.ReceiveLinkSpeed;
    s.adapter_description = utf8(row.Description);
    s.adapter_name = utf8(row.Alias);

    // Varsayilan gecit (IPv4) ve kolay ad.
    ULONG len = 15 * 1024;
    std::unique_ptr<unsigned char[]> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buf.reset(new unsigned char[len]);
        rc = GetAdaptersAddresses(AF_INET,
                                  GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                      GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get()), &len);
    }
    if (rc == NO_ERROR) {
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get()); a; a = a->Next) {
            if (a->IfIndex != best) continue;
            if (a->FriendlyName && *a->FriendlyName) s.adapter_name = utf8(a->FriendlyName);
            for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
                const SOCKADDR* sa = g->Address.lpSockaddr;
                if (sa && sa->sa_family == AF_INET) {
                    const auto* in = reinterpret_cast<const sockaddr_in*>(sa);
                    const std::uint32_t addr = in->sin_addr.S_un.S_addr;
                    if (addr != 0) {
                        s.gateway_ipv4 = format_ipv4(addr);
                        break;
                    }
                }
            }
            break;
        }
    }

    {
        wchar_t g[64] = {};
        if (StringFromGUID2(row.InterfaceGuid, g, 64) > 0) s.interface_guid = utf8(g);
    }
    if (s.info.adapter_kind == "wifi") read_wifi(s, row.InterfaceGuid);

    // Arka plan alma hizi: iki InOctets ornegi. Sure, ILK ornegin alindigi andan baslar (yukaridaki
    // gecit/Wi-Fi okumalarinin suresi hesaba girmez); her iki zaman damgasi da GetIfEntry2 donusunde.
    if (rx_sample_ms > 0) {
        MIB_IF_ROW2 r0{};
        r0.InterfaceIndex = best;
        if (GetIfEntry2(&r0) == NO_ERROR) {
            const auto t0 = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(rx_sample_ms));
            MIB_IF_ROW2 r1{};
            r1.InterfaceIndex = best;
            if (GetIfEntry2(&r1) == NO_ERROR) {
                const auto t1 = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(t1 - t0).count();
                s.info.background_rx_mbps = rx_mbps(r0.InOctets, r1.InOctets, secs);
            }
        }
    }
    // adapter_power_saving: belgelenmis salt okuma API'si yok -> nullopt.
    return s;
}

std::vector<PingTarget> default_ping_targets(const NetworkSnapshot& s) {
    std::vector<PingTarget> t;
    if (s.gateway_ipv4) t.push_back({"gateway", *s.gateway_ipv4});
    t.push_back({"1.1.1.1", "1.1.1.1"});
    t.push_back({"8.8.8.8", "8.8.8.8"});
    return t;
}

namespace {

PingRun ping_one(std::uint32_t addr, int samples, int interval_ms, const std::atomic<bool>* cancel,
                 unsigned timeout_ms) {
    PingRun run;
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) {
        run.error = PingError::IcmpOpenFailed;
        run.win32_error = GetLastError();
        return run;
    }
    run.rtt_ms.reserve(static_cast<std::size_t>(samples));  // dongude push_back ayirma yapmaz
    char payload[32];
    for (std::size_t i = 0; i < sizeof payload; ++i) payload[i] = static_cast<char>('a' + (i % 23));
    // Yanit tamponu: ICMP_ECHO_REPLY + veri + 8 bayt ICMP hata + IO_STATUS_BLOCK payi.
    alignas(8) unsigned char reply[sizeof(ICMP_ECHO_REPLY) + sizeof payload + 8 + 64];
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < samples; ++i) {
        if (cancel && cancel->load()) break;
        LARGE_INTEGER a{}, b{};
        QueryPerformanceCounter(&a);
        const DWORD n = IcmpSendEcho2(h, nullptr, nullptr, nullptr, static_cast<IPAddr>(addr), payload,
                                      static_cast<WORD>(sizeof payload), nullptr, reply, sizeof reply, timeout_ms);
        QueryPerformanceCounter(&b);
        std::optional<double> rtt;
        if (n > 0) {
            const auto* r = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply);
            if (r->Status == IP_SUCCESS && r->Address == addr && freq.QuadPart > 0)
                rtt = 1000.0 * static_cast<double>(b.QuadPart - a.QuadPart) / static_cast<double>(freq.QuadPart);
        }
        run.rtt_ms.push_back(rtt);
        // Sonraki ornege kadar bekle (iptal icin kisa dilimler).
        const auto next = start + std::chrono::milliseconds(static_cast<long long>(interval_ms) * (i + 1));
        while (std::chrono::steady_clock::now() < next) {
            if (cancel && cancel->load()) break;
            const auto left = next - std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(left, std::chrono::milliseconds(20)));
        }
    }
    IcmpCloseHandle(h);
    return run;
}

// Is parcacigi govdesi: istisna disari kacmaz (std::terminate yerine Internal hatasi).
PingRun ping_one_noexcept(std::uint32_t addr, int samples, int interval_ms, const std::atomic<bool>* cancel,
                          unsigned timeout_ms) noexcept {
    try {
        return ping_one(addr, samples, interval_ms, cancel, timeout_ms);
    } catch (...) {
        PingRun r;
        r.error = PingError::Internal;
        return r;
    }
}

}  // namespace

std::vector<PingRun> run_ping_test(const std::vector<PingTarget>& targets, int samples, int interval_ms,
                                   const std::atomic<bool>* cancel, unsigned timeout_ms) {
    std::vector<PingRun> out(targets.size());
    if (samples <= 0) return out;
    if (interval_ms < 0) interval_ms = 0;
    // out'tan SONRA tanimlanir: istisna halinde once is parcaciklari (jthread yikicisi join eder)
    // yok edilir, sonra out; hicbir is parcacigi yok edilmis bellege yazmaz.
    std::vector<std::jthread> workers;
    workers.reserve(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto addr = parse_ipv4(targets[i].ipv4);
        if (!addr) {
            out[i].error = PingError::InvalidTarget;
            continue;
        }
        PingRun* slot = &out[i];
        try {
            workers.emplace_back([slot, a = *addr, samples, interval_ms, cancel, timeout_ms] {
                *slot = ping_one_noexcept(a, samples, interval_ms, cancel, timeout_ms);
            });
        } catch (const std::system_error&) {
            // Is parcacigi olusturulamadi: bu hedef bu is parcaciginda sirayla olculur.
            *slot = ping_one_noexcept(*addr, samples, interval_ms, cancel, timeout_ms);
        }
    }
    workers.clear();  // tum is parcaciklarini bekle (join) -> out artik guvenle dondurulur
    return out;
}

std::vector<cr::PingStats> ping_test_stats(const std::vector<PingTarget>& targets, const std::vector<PingRun>& runs) {
    std::vector<cr::PingStats> v;
    for (std::size_t i = 0; i < targets.size() && i < runs.size(); ++i) {
        cr::PingStats st = cr::ping_stats(targets[i].label, runs[i].rtt_ms);
        st.failed = runs[i].error != PingError::None && runs[i].rtt_ms.empty();
        v.push_back(std::move(st));
    }
    return v;
}

}  // namespace ef::platform::readiness
