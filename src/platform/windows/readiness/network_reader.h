#pragma once
// CHK-06 / NET-01: ag okuyucu. SALT OKUMA: hicbir ag ayari degismez, yonetici izni gerekmez.
//  - read_network(): varsayilan IPv4 yolunu tasiyan bagdastirici (GetBestInterface 1.1.1.1; paket
//    gonderilmez), turu, baglanti hizi, arka plan alma hizi (InOctets, iki ornek arasi rx_sample_ms).
//    Bu cagri rx_sample_ms kadar BLOKLAR; UI is parcaciginda cagrilmaz.
//  - Wi-Fi: yalniz konum izni GEREKTIRMEYEN wlanapi cagrilari kullanilir (Windows 11 24H2+ konum
//    gizliligi: wlan_intf_opcode_current_connection ve WlanGetNetworkBssList izin yoksa sistem
//    istemi acabilir / ERROR_ACCESS_DENIED doner; bu yuzden CAGRILMAZ). Kullanilan: realtime
//    connection quality (frekans + baglanti kalitesi), rssi, channel_number, interface capability.
//    Okunamayan deger nullopt kalir. wlanapi.dll calisma aninda System32'den yuklenir (statik baglanti
//    yok); DLL/fonksiyon yoksa Wi-Fi alanlari nullopt. Wi-Fi yolu gercek Wi-Fi donaniminda HENUZ
//    DOGRULANMADI (gelistirme PC'si kablolu).
//  - adapter_kind: "" yalniz rota yok (ERROR_NETWORK_UNREACHABLE / ERROR_NOT_FOUND) veya bagdastirici
//    kopuk ise; diger okuma hatalari "unknown" (baglanti var/yok bilinmiyor). Ayrinti read_note'ta.
//  - Bagdastirici guc tasarrufu: belgelenmis salt okuma API'si yok -> nullopt.
//  - run_ping_test(): ICMP yanki (echo) paketi GONDERIR. Yalniz kullanici "Ag testini calistir"
//    dugmesine bastiginda cagrilir; kurulumda/okuma sirasinda kendiliginden calismaz.
// Bu baslik Win32 tiplerini disariya acmaz (saf yardimcilar birim testlenir).

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

struct NetworkSnapshot {
    core::readiness::NetworkInfo info;
    std::uint32_t if_index = 0;               // 0 = varsayilan yol yok
    std::string interface_guid;               // "{...}"; DNS hedef kimligi (EF-X14)
    std::string adapter_name;                 // Windows'taki ad ("Ethernet", "Wi-Fi") UTF-8
    std::string adapter_description;          // surucu aciklamasi UTF-8
    std::uint32_t if_type = 0;                // IANA ifType (6 = ethernet, 71 = 802.11)
    std::optional<std::string> gateway_ipv4;  // varsayilan gecit ("192.168.0.1")
    std::optional<unsigned> wifi_channel;     // Wi-Fi kanal numarasi
    std::optional<unsigned> wifi_freq_mhz;    // Wi-Fi merkez frekansi (realtime quality)
    std::optional<long> wifi_rssi_dbm;
    std::string wifi_note;                    // tani: hangi kaynak okundu / neden okunamadi
    std::string read_note;                    // tani: "GetIfEntry2=87", "disconnected" ... (bos = sorunsuz)
};

// Varsayilan rotanin bagdastiricisini okur. rx_sample_ms = 0 ise arka plan hizi olculmez (nullopt).
NetworkSnapshot read_network(unsigned rx_sample_ms = 1000);

struct PingTarget {
    std::string label;  // "gateway" | "1.1.1.1" | "8.8.8.8" (PingStats::target olur)
    std::string ipv4;
};
// Gecit (biliniyorsa) + 1.1.1.1 + 8.8.8.8.
std::vector<PingTarget> default_ping_targets(const NetworkSnapshot& s);

enum class PingError {
    None,
    InvalidTarget,   // ipv4 metni gecersiz; paket gonderilmedi
    IcmpOpenFailed,  // IcmpCreateFile basarisiz (win32_error = GetLastError)
    Internal,        // beklenmeyen istisna (or. bellek)
};
struct PingRun {
    std::vector<std::optional<double>> rtt_ms;  // yalniz GONDERILEN ornekler; kayip/zaman asimi = nullopt
    PingError error = PingError::None;
    std::uint32_t win32_error = 0;
};

// Kullanici tetikli ICMP yanki testi (IcmpSendEcho2, IPv4). Hedefler paralel (std::jthread; is
// parcacigi acilamazsa o hedef sirayla olculur); her hedefe `samples` istek, `interval_ms` aralikla,
// istek basina `timeout_ms` zaman asimi. RTT QueryPerformanceCounter ile olculur. `cancel` true
// olunca kalan ornekler gonderilmez (sent dogru kalir). Kurallar >= 20 ornek olmadan Advice/Problem
// vermez; varsayilan 30.
std::vector<PingRun> run_ping_test(const std::vector<PingTarget>& targets, int samples = 30, int interval_ms = 200,
                                   const std::atomic<bool>* cancel = nullptr, unsigned timeout_ms = 1000);
// Hata ile hic ornek gonderilemeyen hedef: PingStats::failed = true, sent = 0.
std::vector<core::readiness::PingStats> ping_test_stats(const std::vector<PingTarget>& targets,
                                                        const std::vector<PingRun>& runs);

// ---- Saf yardimcilar (Win32 yok; birim testli) ----
// "a.b.c.d" -> IPAddr bellek duzeni (ilk oktet en dusuk bayt). Gecersizse nullopt.
std::optional<std::uint32_t> parse_ipv4(std::string_view s);
std::string format_ipv4(std::uint32_t addr);
// Merkez frekansindan bant: 2400-2500 -> 2.4, 5150-5895 -> 5, 5925-7125 -> 6; digeri nullopt.
std::optional<double> wifi_band_from_freq_mhz(unsigned mhz);
// Kanal numarasindan bant. 6 GHz kanallari (1,2,5,9,...,233) 2.4/5 GHz ile cakisir: bagdastirici
// 802.11ax/be destekliyorsa (may_be_6ghz) cakisan kanallar icin nullopt (tahmin yok).
std::optional<double> wifi_band_from_channel(unsigned channel, bool may_be_6ghz);
// WLAN_ASSOCIATION_ATTRIBUTES belgesindeki dogrusal eslesme: -100 dBm = 0, -50 dBm = 100.
int wifi_signal_pct_from_rssi(long rssi_dbm);
// Iki InOctets orneginden Mbit/s; sayac geri gittiyse/sure gecersizse nullopt.
std::optional<double> rx_mbps(std::uint64_t octets_before, std::uint64_t octets_after, double seconds);
// IANA ifType + fiziksel ortam -> "ethernet" | "wifi" | "other" (VPN, WWAN, Hyper-V vEthernet gibi
// sanal anahtarlar; bunlarin altinda kablolu baglanti olabilir).
std::string adapter_kind_from_type(std::uint32_t if_type, std::uint32_t physical_medium, bool hardware);

}  // namespace ef::platform::readiness
