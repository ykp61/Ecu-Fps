#pragma once
// Zorunlu guncelleme istemcisi (spec 2026-11-11-zorunlu-guncelleme-design.md §2/§5, ADR-028).
//  - Yalniz UI rolunde, surec basina bir kez acilista (manifest) ve kullanici "Simdi guncelle" deyince (EXE) cagrilir.
//  - Adresler core::update sabitlerinden (kManifestUrl) veya imzali manifestteki url'den gelir; bu klasor adres yazmaz.
//  - Yonlendirmeler elle izlenir (en fazla kMaxRedirects); her adim core::update::update_host_allowed ile denetlenir,
//    izin disi host = Redirect hatasi. Yalniz HTTPS/443, TLS dogrulamasi acik, sistem proxy'si, genel User-Agent.
//  - Bayt siniri (manifest 64 KiB, EXE 64 MiB) ve toplam sure siniri; iptal her okuma (<= 64 KiB) arasinda denetlenir.
// Bu baslik Win32 tiplerini disariya acmaz.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ef::platform::net {

inline constexpr int kMaxRedirects = 5;

enum class FetchFail { None, Offline, Timeout, Tls, Http, Redirect, TooLarge, Cancelled };

struct FetchResult {
    FetchFail fail = FetchFail::None;
    std::string body;
    bool ok() const { return fail == FetchFail::None; }
};

// done: okunan govde baytlari; total: sunucunun bildirdigi boy (bilinmiyorsa 0). En fazla 10 Hz.
using FetchProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

// deadline_ms <= 0: toplam sure siniri yok (yalniz baglanti/okuma zaman asimlari).
FetchResult update_get(std::string_view url, std::uint64_t max_bytes, int deadline_ms, const std::atomic<bool>& cancel,
                       const FetchProgress& progress = {});

// Kapanis: cagirandan once `cancel` ayarlanmis olmali. Su an bekleyen istek tutamacini kapatir; engelleyen WinHTTP
// cagrisi (gonderim/yanit/okuma) zaman asimini beklemeden hata ile doner ve update_get Cancelled dondurur.
void abort_update_requests();

}  // namespace ef::platform::net
