#pragma once
// R-C4: oyun sunucusu ping olcumu (saf cekirdek; ag kodu burada degildir).
//  - Hedef YALNIZ kullanicinin yazdigi sayili IPv4 adresidir (a.b.c.d). Alan adi cozulmez (DNS/soket API'si eklenmez);
//    adres uygulama icinde sabit degildir ve yalniz bu bilgisayarda (tercihler) saklanir.
//  - Olcum mevcut ICMP yanki yolunu (platform/windows/readiness/network_reader) kullanir. Yanit vermeyen sunucu
//    (ICMP kapali) "veri yetersiz"dir; uydurma deger yazilmaz. Ceviri/oyun ici ping vaadi yoktur.
#include <optional>
#include <string>
#include <string_view>

namespace ef::core::net {

inline constexpr int kServerPingSamples = 20;      // gonderilecek yanki sayisi
inline constexpr int kServerPingIntervalMs = 200;  // istekler arasi (toplam ~4 sn)
inline constexpr int kServerPingMinReceived = 10;  // bundan az yanit = veri yetersiz (ortalama yazilmaz)
inline constexpr std::size_t kServerAddrMaxLen = 15;

enum class ServerPingState { None, Running, Done, Insufficient, Failed, Cancelled };

// Kullanici metnini kanonik "a.b.c.d" IPv4'e cevirir (bastaki/sondaki bosluk atilir). Reddedilenler: bos, 4 sekizli
// olmayan, 0..255 disi, basinda gereksiz sifir ("01"), ilk sekizli 0 (gecersiz) / 127 (kendi bilgisayari) / 224 ve ustu
// (coklu yayin, ayrilmis, yayin). Ozel (LAN) adresler gecerlidir.
std::optional<std::string> normalize_server_ipv4(std::string_view text);

}  // namespace ef::core::net
