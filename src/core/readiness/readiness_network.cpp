// CHK-06 / NET-01: ag hazirlik kurallari (saf; Win32 yok, birim testlenir).
// Kaynak: platform/windows/readiness/network_reader (salt okuma + kullanici tetikli ping).
// Ilkeler:
//  - Okunamayan deger -> Unknown (dogrulanamadi); tahmin yok.
//  - Ping olculmediyse "olculmedi" + RunNetworkTest eylemi; ping kendiliginden calismaz.
//  - Kayit defteri TCP/Nagle ince ayari ONERILMEZ: oyunlar cogunlukla UDP kullanir, etkisi yoktur.
//  - Yerel ag (modem/router'a kadar) ile internet (servis saglayici/router kuyrugu) ayri degerlendirilir.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

constexpr double kWifiStableJitterMs = 5.0;
constexpr int kWifiWeakSignalPct = 50;
constexpr double kBackgroundRxMbps = 5.0;
constexpr double kGatewayJitterMs = 10.0;
constexpr double kInternetJitterMs = 15.0;
// Kayip ancak en az kMinLost paket VE en az %kLossPct ise sayilir (tek kayip gurultudur).
constexpr int kMinLost = 2;
constexpr double kLossPct = 5.0;
// Advice/Problem icin en az bu kadar gonderilmis ornek gerekir; azsa sonuc Unknown.
constexpr int kMinSamples = 20;
constexpr const char* kBufferbloatUrl = "https://www.waveform.com/tools/bufferbloat";

// Arguman bicimi: her zaman bir ondalik, "." ayirici ("12.0", "3.3", "1000.0").
// UI ondalik ayiriciyi dile gore yerellestirir (tr: "12,0").
std::string num(double v) {
    char buf[32];
    if (!std::isfinite(v)) v = 0;
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

std::string band_text(double ghz) {
    if (ghz < 3.0) return "2.4";
    if (ghz < 5.9) return "5";
    return "6";
}

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Network;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

bool measured(const PingStats& p) { return p.sent > 0; }
bool has_replies(const PingStats& p) { return p.received > 0; }
bool enough_samples(const PingStats& p) { return p.sent >= kMinSamples; }
bool significant_loss(const PingStats& p) {
    return p.sent - p.received >= kMinLost && p.loss_pct() >= kLossPct;
}
bool bad(const PingStats& p, double jitter_limit) {
    return has_replies(p) && (significant_loss(p) || p.jitter_ms > jitter_limit);
}
// Yalniz siralama icin: 1'den buyukse esik asilmis.
double badness(const PingStats& p, double jitter_limit) {
    const double loss = significant_loss(p) ? p.loss_pct() / kLossPct : 0.0;
    return std::max(loss, p.jitter_ms / jitter_limit);
}

const PingStats* find_gateway(const std::vector<PingStats>& pings) {
    for (const auto& p : pings)
        if (p.target == "gateway" && measured(p)) return &p;
    return nullptr;
}

void ping_args(Check& c, const PingStats& p) {
    c.args["target"] = p.target;
    c.args["avg"] = num(p.avg_ms);
    c.args["jitter"] = num(p.jitter_ms);
    c.args["p95"] = num(p.p95_ms);
    c.args["loss"] = num(p.loss_pct());
}

// Ornek sayisi yetersiz (test iptal edildi ya da az ornek istendi): ne sorun ne "kararli" karari verilir.
Check too_few(std::string id, const PingStats& p) {
    Check c = make(std::move(id), Level::Unknown, "chk.network.too_few_samples");
    c.args["target"] = p.target;
    c.args["sent"] = std::to_string(p.sent);
    c.args["min"] = std::to_string(kMinSamples);
    c.action = ActionKind::RunNetworkTest;
    return c;
}

// Wi-Fi "kablo daha kararli" onerisi yerine Info: olculen yerel baglanti temizse.
// Kanit icin yeterli ornek ve sifir kayip gerekir (siki kosul: "kararli" iddiasi).
bool wifi_link_proven_stable(const std::vector<PingStats>& pings) {
    std::vector<const PingStats*> basis;
    if (const PingStats* g = find_gateway(pings)) {
        basis.push_back(g);
    } else {
        for (const auto& p : pings)
            if (measured(p)) basis.push_back(&p);
    }
    if (basis.empty()) return false;
    for (const PingStats* p : basis)
        if (!enough_samples(*p) || !has_replies(*p) || p->received != p->sent ||
            p->jitter_ms >= kWifiStableJitterMs)
            return false;
    return true;
}

enum class Gateway { Absent, NoReply, Clean, Flagged, Inconclusive };

void latency_checks(std::vector<Check>& out, const std::vector<PingStats>& pings) {
    bool any = false;
    bool any_failed = false;
    for (const auto& p : pings) {
        any = any || measured(p);
        any_failed = any_failed || p.failed;
    }
    if (!any) {
        Check c = make("network.latency", Level::Unknown,
                       any_failed ? "chk.network.test_failed" : "chk.network.not_measured");
        c.action = ActionKind::RunNetworkTest;
        out.push_back(std::move(c));
        return;
    }

    const PingStats* gw = find_gateway(pings);
    Gateway gstate = Gateway::Absent;
    if (gw) {
        if (!has_replies(*gw)) {
            // Bazi modem/router'lar ping'e yanit vermez; sorun diye isaretlenmez.
            Check c = make("network.gateway", Level::Unknown, "chk.network.gateway_no_reply");
            c.action = ActionKind::RunNetworkTest;
            out.push_back(std::move(c));
            gstate = Gateway::NoReply;
        } else if (bad(*gw, kGatewayJitterMs)) {
            if (!enough_samples(*gw)) {
                out.push_back(too_few("network.gateway", *gw));
                gstate = Gateway::Inconclusive;
            } else {
                // Anlamli kayip yerel sorundur; yalniz dalgalanma (jitter) oneridir.
                Check c = make("network.gateway", significant_loss(*gw) ? Level::Problem : Level::Advice,
                               "chk.network.local_unstable");
                ping_args(c, *gw);
                c.action = ActionKind::RunNetworkTest;
                out.push_back(std::move(c));
                gstate = Gateway::Flagged;
            }
        } else if (!enough_samples(*gw)) {
            // Esik asilmadi ama kisa olcum yerel tarafin temiz oldugunu kanitlamaz.
            out.push_back(too_few("network.gateway", *gw));
            gstate = Gateway::Inconclusive;
        } else {
            gstate = Gateway::Clean;
        }
    }

    // Internet hedefleri: yanit alinanlar arasinda en kotu olan.
    const PingStats* worst = nullptr;
    const PingStats* best = nullptr;
    bool any_internet = false;
    bool any_internet_reply = false;
    for (const auto& p : pings) {
        if (!measured(p) || p.target == "gateway") continue;
        any_internet = true;
        if (!has_replies(p)) continue;
        any_internet_reply = true;
        // Siralama: (1) esigi asan hedef asmayana gore daima "daha kotu"; (2) esigi asanlar arasinda
        // yeterli ornekli hedef, az ornekli hedefe tercih edilir (az ornekli hedef daha kotu gorunse bile
        // tam olculmus hedefin gecerli bulgusu gizlenmez); (3) sonra badness; esitlikte ilk hedef kalir.
        if (!worst) {
            worst = &p;
        } else {
            const bool pb = bad(p, kInternetJitterMs), wb = bad(*worst, kInternetJitterMs);
            const bool pe = pb && enough_samples(p), we = wb && enough_samples(*worst);
            if ((pb && !wb) || (pb == wb && pe && !we) ||
                (pb == wb && pe == we && badness(p, kInternetJitterMs) > badness(*worst, kInternetJitterMs)))
                worst = &p;
        }
        if (!best || p.avg_ms < best->avg_ms) best = &p;
    }

    if (any_internet && !any_internet_reply) {
        Check c = make("network.internet", Level::Unknown, "chk.network.internet_no_reply");
        c.action = ActionKind::RunNetworkTest;
        out.push_back(std::move(c));
        return;
    }
    if (!any_internet) {
        // Yalniz gecit olculmus; internet yolu dogrulanamadi.
        if (gstate == Gateway::Clean) {
            Check c = make("network.internet", Level::Unknown, "chk.network.internet_not_measured");
            c.action = ActionKind::RunNetworkTest;
            out.push_back(std::move(c));
        }
        return;
    }

    if (worst && bad(*worst, kInternetJitterMs)) {
        if (gstate == Gateway::Flagged) return;  // yerel sorun zaten bildirildi; once o cozulmeli
        if (!enough_samples(*worst)) {
            out.push_back(too_few("network.internet", *worst));
            return;
        }
        Check c = make("network.internet", Level::Advice,
                       gstate == Gateway::Clean ? "chk.network.isp_queue" : "chk.network.internet_unstable");
        ping_args(c, *worst);
        c.action = ActionKind::OpenUrl;
        c.action_arg = kBufferbloatUrl;
        out.push_back(std::move(c));
        return;
    }

    // Yerel taraf isaretlendiyse veya karar verilemediyse "kararli" denmez.
    if (gstate == Gateway::Flagged || gstate == Gateway::Inconclusive) return;
    // "Kararli" iddiasi icin yanit veren her internet hedefinde yeterli ornek gerekir; aksi halde
    // (or. iptal edilmis 2 ornekli olcumde tek kayip "%50 kayip" ile Good gorunurdu) karar verilmez.
    for (const auto& p : pings) {
        if (!measured(p) || p.target == "gateway" || !has_replies(p)) continue;
        if (!enough_samples(p)) {
            out.push_back(too_few("network.latency", p));
            return;
        }
    }
    Check c = make("network.latency", Level::Good, "chk.network.latency_ok");
    ping_args(c, *best);
    if (gstate == Gateway::Clean) c.args["gateway_avg"] = num(gw->avg_ms);
    c.action = ActionKind::RunNetworkTest;  // istenirse yeniden olc
    out.push_back(std::move(c));
}

}  // namespace

PingStats ping_stats(const std::string& target, const std::vector<std::optional<double>>& rtt_ms) {
    PingStats s;
    s.target = target;
    s.sent = static_cast<int>(rtt_ms.size());
    std::vector<double> got;
    got.reserve(rtt_ms.size());
    for (const auto& r : rtt_ms)
        if (r && std::isfinite(*r) && *r >= 0.0) got.push_back(*r);
    s.received = static_cast<int>(got.size());
    if (got.empty()) return s;

    double sum = 0;
    for (double v : got) sum += v;
    s.avg_ms = sum / static_cast<double>(got.size());

    // Jitter: ardisik ALINAN RTT'ler arasindaki mutlak farklarin ortalamasi (kayiplar atlanir).
    if (got.size() >= 2) {
        double d = 0;
        for (std::size_t i = 1; i < got.size(); ++i) d += std::fabs(got[i] - got[i - 1]);
        s.jitter_ms = d / static_cast<double>(got.size() - 1);
    }

    // p95: en yakin sira (nearest-rank) yontemi.
    std::vector<double> sorted = got;
    std::sort(sorted.begin(), sorted.end());
    const auto n = sorted.size();
    // Tam sayi aritmetigi: ceil(95 n / 100) (kayan nokta yuvarlama hatasi yok).
    std::size_t rank = (95 * n + 99) / 100;
    if (rank < 1) rank = 1;
    if (rank > n) rank = n;
    s.p95_ms = sorted[rank - 1];
    return s;
}

std::vector<Check> network_checks(const NetworkInfo& n, const std::vector<PingStats>& pings) {
    std::vector<Check> out;

    if (n.adapter_kind.empty()) {
        Check c = make("network.adapter", Level::Problem, "chk.network.no_adapter");
        c.action = ActionKind::OpenSettings;
        c.action_arg = "ms-settings:network-status";
        out.push_back(std::move(c));
        return out;  // baglanti yokken diger olcumler anlamsiz
    }

    if (n.adapter_kind == "ethernet") {
        Check c = make("network.adapter", Level::Good, "chk.network.ethernet");
        if (n.link_bps && *n.link_bps > 0) c.args["speed"] = num(static_cast<double>(*n.link_bps) / 1e6);
        out.push_back(std::move(c));
    } else if (n.adapter_kind == "wifi") {
        const bool stable = wifi_link_proven_stable(pings);
        Check c = make("network.adapter", stable ? Level::Info : Level::Advice,
                       stable ? "chk.network.wifi_stable" : "chk.network.wifi_prefer_cable");
        out.push_back(std::move(c));

        if (!n.wifi_signal_pct) {
            out.push_back(make("network.wifi.signal", Level::Unknown, "chk.network.wifi_signal_unknown"));
        } else {
            const int pct = std::clamp(*n.wifi_signal_pct, 0, 100);
            Check s = make("network.wifi.signal", pct < kWifiWeakSignalPct ? Level::Advice : Level::Good,
                           pct < kWifiWeakSignalPct ? "chk.network.wifi_signal_weak" : "chk.network.wifi_signal_ok");
            s.args["pct"] = std::to_string(pct);
            out.push_back(std::move(s));
        }

        if (!n.wifi_band_ghz) {
            out.push_back(make("network.wifi.band", Level::Unknown, "chk.network.wifi_band_unknown"));
        } else if (*n.wifi_band_ghz < 3.0) {
            out.push_back(make("network.wifi.band", Level::Advice, "chk.network.wifi_band_24"));
        } else {
            Check b = make("network.wifi.band", Level::Good, "chk.network.wifi_band_ok");
            b.args["band"] = band_text(*n.wifi_band_ghz);
            out.push_back(std::move(b));
        }
    } else if (n.adapter_kind == "other") {
        // VPN, mobil genis bant, sanal ag anahtari (Hyper-V vEthernet: altinda kablo olabilir) ...
        out.push_back(make("network.adapter", Level::Info, "chk.network.other_adapter"));
    } else {
        // "unknown" (okuma hatasi) veya taninmayan deger: baglanti turu dogrulanamadi.
        out.push_back(make("network.adapter", Level::Unknown, "chk.network.adapter_unknown"));
    }

    // Bagdastirici guc tasarrufu: belgelenmis salt okuma API'si ve ilgili ms-settings sayfasi yok;
    // bu yuzden bulgu uretilmez (adapter_power_saving yok sayilir).

    if (!n.background_rx_mbps) {
        out.push_back(make("network.background", Level::Unknown, "chk.network.background_unknown"));
    } else if (*n.background_rx_mbps > kBackgroundRxMbps) {
        Check c = make("network.background", Level::Advice, "chk.network.background_download");
        c.args["mbps"] = num(*n.background_rx_mbps);
        c.action = ActionKind::OpenSettings;
        c.action_arg = "ms-settings:datausage";
        out.push_back(std::move(c));
    } else {
        out.push_back(make("network.background", Level::Good, "chk.network.background_quiet"));
    }

    latency_checks(out, pings);
    return out;
}

}  // namespace ef::core::readiness
