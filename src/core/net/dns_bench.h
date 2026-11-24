#pragma once
// T10: DNS cozucu yanit suresi karsilastirmasi (saf; Win32 yok, birim testlenir).
// Olculen sey yalniz ALAN ADI COZUMLEME suresidir; oyun ici ping/gecikmeyi degistirmez ve bunun gibi sunulmaz.
// Karar: "belirgin fark" ancak en iyi cozucu mevcut sistem cozucusunden en az kMinGainMs VE en az %kMinGainPct hizliysa
// ve hic basarisiz sorgusu yoksa verilir; aksi halde "fark anlamsiz" (DNS degisikligi onerilmez).
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::net {

inline constexpr std::string_view kDnsBenchCurrentId = "current";  // sistemin o anki cozucusu
inline constexpr int kDnsBenchRounds = 3;
inline constexpr int kDnsBenchTimeoutMs = 2000;
inline constexpr int kDnsBenchMinSamples = 10;  // bir cozucu icin en az bu kadar basarili ornek yoksa "veri yetersiz"
inline constexpr double kDnsBenchMinGainMs = 5.0;
inline constexpr double kDnsBenchMinGainPct = 20.0;
// Sabit, tanidik 5 alan adi (onbellek atlanir: her sorgu cozucuye gider).
inline constexpr std::string_view kDnsBenchHosts[] = {"www.microsoft.com", "www.wikipedia.org", "store.steampowered.com",
                                                      "www.youtube.com", "www.amazon.com"};

struct ResolverSamples {
    std::string id;                       // "current" | hazir liste kimligi
    std::vector<std::optional<double>> ms;  // sorgu basina yanit suresi; nullopt = basarisiz/zaman asimi
};
struct ResolverStat {
    std::string id;
    int total = 0, ok_count = 0;
    double median_ms = 0, p95_ms = 0, fail_pct = 0;
    bool ok = false;  // false: veri yetersiz (kMinSamples'tan az basarili ornek)
};
std::vector<ResolverStat> summarize(const std::vector<ResolverSamples>& samples);

struct DnsBenchVerdict {
    std::string best_id;       // en dusuk ortancali (ok) cozucu; bos = hicbiri yeterli veri vermedi
    bool meaningful = false;   // yalniz esikler saglanirsa
};
DnsBenchVerdict verdict(const std::vector<ResolverStat>& stats, std::string_view current_id = kDnsBenchCurrentId);

// Gosterim icin: olcum raporu (bellekte; kalici iddia degil).
struct DnsBenchReport {
    std::vector<ResolverStat> stats;
    DnsBenchVerdict verdict;
    std::string current_name;   // sistem cozucusu bir hazir listeyse markasi, degilse adresi; bos = okunamadi
    bool current_modem = false;  // sistem cozucusu ozel (yerel) adres: genellikle modem/yonlendirici
};

// 10.x, 172.16-31.x, 192.168.x, 169.254.x (IPv4 metni). Gecersiz metin false.
bool is_private_ipv4(std::string_view ip) noexcept;

}  // namespace ef::core::net
