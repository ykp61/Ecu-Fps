// CHK-06 / NET-01: ag kurallari ve saf yardimcilar (Win32 cagrisi yok).
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/readiness/readiness.h"
#include "ef_test.h"
#include "platform/windows/readiness/network_reader.h"

using namespace ef::core::readiness;
namespace pr = ef::platform::readiness;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// Deger dondurur: gecici vektorden isaretci tutulmaz.
std::optional<Check> find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return c;
    return std::nullopt;
}

PingStats ps(const std::string& target, int sent, int received, double avg, double jitter, double p95 = 0) {
    PingStats p;
    p.target = target;
    p.sent = sent;
    p.received = received;
    p.avg_ms = avg;
    p.jitter_ms = jitter;
    p.p95_ms = p95 ? p95 : avg;
    return p;
}

NetworkInfo ethernet() {
    NetworkInfo n;
    n.adapter_kind = "ethernet";
    n.link_bps = 1000000000ull;
    n.background_rx_mbps = 0.2;
    return n;
}

NetworkInfo wifi(std::optional<int> sig, std::optional<double> band) {
    NetworkInfo n;
    n.adapter_kind = "wifi";
    n.wifi_signal_pct = sig;
    n.wifi_band_ghz = band;
    n.background_rx_mbps = 0.1;
    return n;
}

std::vector<PingStats> clean_pings() {
    return {ps("gateway", 30, 30, 1.2, 0.3), ps("1.1.1.1", 30, 30, 12, 1.5), ps("8.8.8.8", 30, 30, 18, 2.0)};
}

}  // namespace

// ---- ping_stats ----
EF_TEST(network_ping_stats_basic) {
    const auto s = ping_stats("1.1.1.1", {10.0, 12.0, 11.0, 15.0});
    EF_CHECK(s.target == "1.1.1.1");
    EF_CHECK(s.sent == 4 && s.received == 4);
    EF_CHECK(near(s.avg_ms, 12.0));
    EF_CHECK(near(s.jitter_ms, (2.0 + 1.0 + 4.0) / 3.0));
    EF_CHECK(near(s.p95_ms, 15.0));  // ceil(0.95*4)=4 -> en buyuk
    EF_CHECK(near(s.loss_pct(), 0.0));
}

EF_TEST(network_ping_stats_loss_skips_lost_in_jitter) {
    const auto s = ping_stats("gateway", {1.0, std::nullopt, 3.0, std::nullopt});
    EF_CHECK(s.sent == 4 && s.received == 2);
    EF_CHECK(near(s.loss_pct(), 50.0));
    EF_CHECK(near(s.avg_ms, 2.0));
    EF_CHECK(near(s.jitter_ms, 2.0));  // ardisik ALINAN: |3-1|
}

EF_TEST(network_ping_stats_p95_nearest_rank) {
    std::vector<std::optional<double>> v;
    for (int i = 1; i <= 20; ++i) v.push_back(static_cast<double>(i));
    const auto s = ping_stats("x", v);
    EF_CHECK(near(s.p95_ms, 19.0));  // ceil(0.95*20)=19
    v.push_back(21.0);
    EF_CHECK(near(ping_stats("x", v).p95_ms, 20.0));  // ceil(0.95*21)=20 -> 1..21 icinde 20
}

EF_TEST(network_ping_stats_empty_and_all_lost) {
    const auto e = ping_stats("x", {});
    EF_CHECK(e.sent == 0 && e.received == 0 && near(e.loss_pct(), 0.0));
    EF_CHECK(near(e.avg_ms, 0.0) && near(e.jitter_ms, 0.0) && near(e.p95_ms, 0.0));
    const auto l = ping_stats("x", {std::nullopt, std::nullopt});
    EF_CHECK(l.sent == 2 && l.received == 0 && near(l.loss_pct(), 100.0));
    const auto one = ping_stats("x", {5.0});
    EF_CHECK(one.received == 1 && near(one.jitter_ms, 0.0) && near(one.p95_ms, 5.0));
}

EF_TEST(network_ping_stats_invalid_samples_are_lost) {
    const auto s = ping_stats("x", {-1.0, std::nan(""), 4.0});
    EF_CHECK(s.sent == 3 && s.received == 1 && near(s.avg_ms, 4.0));
}

// ---- network_checks ----
EF_TEST(network_no_adapter_is_problem_only) {
    NetworkInfo n;
    const auto v = network_checks(n, clean_pings());
    EF_CHECK(v.size() == 1);
    EF_CHECK(v[0].id == "network.adapter" && v[0].level == Level::Problem);
    EF_CHECK(v[0].text_key == "chk.network.no_adapter");
    EF_CHECK(v[0].section == Section::Network);
    EF_CHECK(v[0].action == ActionKind::OpenSettings && v[0].action_arg.rfind("ms-settings:", 0) == 0);
}

EF_TEST(network_ethernet_all_clean_is_good) {
    const auto v = network_checks(ethernet(), clean_pings());
    std::optional<Check> a = find(v, "network.adapter");
    EF_CHECK(a && a->level == Level::Good && a->text_key == "chk.network.ethernet");
    EF_CHECK(a && a->args.at("speed") == "1000.0");
    std::optional<Check> b = find(v, "network.background");
    EF_CHECK(b && b->level == Level::Good);
    std::optional<Check> l = find(v, "network.latency");
    EF_CHECK(l && l->level == Level::Good && l->text_key == "chk.network.latency_ok");
    EF_CHECK(l && l->args.at("target") == "1.1.1.1" && l->args.at("avg") == "12.0");
    EF_CHECK(l && l->args.at("gateway_avg") == "1.2");
    for (const auto& c : v) {
        EF_CHECK(c.level == Level::Good);
        EF_CHECK(c.section == Section::Network);
        EF_CHECK(c.text_key.rfind("chk.network.", 0) == 0);
    }
}

EF_TEST(network_not_measured_offers_test) {
    const auto v = network_checks(ethernet(), {});
    std::optional<Check> l = find(v, "network.latency");
    EF_CHECK(l && l->level == Level::Unknown && l->text_key == "chk.network.not_measured");
    EF_CHECK(l && l->action == ActionKind::RunNetworkTest);
    // Gonderilmemis (sent=0) olcum de olculmedi sayilir.
    const auto v2 = network_checks(ethernet(), {ps("1.1.1.1", 0, 0, 0, 0)});
    std::optional<Check> l2 = find(v2, "network.latency");
    EF_CHECK(l2 && l2->text_key == "chk.network.not_measured");
}

EF_TEST(network_wifi_prefers_cable_unless_proven_stable) {
    const auto unmeasured = network_checks(wifi(80, 5.0), {});
    std::optional<Check> a = find(unmeasured, "network.adapter");
    EF_CHECK(a && a->level == Level::Advice && a->text_key == "chk.network.wifi_prefer_cable");

    const auto stable = network_checks(wifi(80, 5.0), clean_pings());
    a = find(stable, "network.adapter");
    EF_CHECK(a && a->level == Level::Info && a->text_key == "chk.network.wifi_stable");

    // Gecit jitter 5 ms ve uzeri: kanitlanmis kararli degil.
    auto p = clean_pings();
    p[0].jitter_ms = 5.0;
    a = find(network_checks(wifi(80, 5.0), p), "network.adapter");
    EF_CHECK(a && a->level == Level::Advice);

    // Tek kayip: kararli degil.
    p = clean_pings();
    p[0].received = 29;
    a = find(network_checks(wifi(80, 5.0), p), "network.adapter");
    EF_CHECK(a && a->level == Level::Advice);

    // Gecit yok: internet hedefleri temiz ise kararli sayilir.
    std::vector<PingStats> inet{ps("1.1.1.1", 30, 30, 12, 1.0), ps("8.8.8.8", 30, 30, 15, 2.0)};
    a = find(network_checks(wifi(80, 5.0), inet), "network.adapter");
    EF_CHECK(a && a->level == Level::Info);
}

EF_TEST(network_wifi_signal_rules) {
    std::optional<Check> s = find(network_checks(wifi(49, 5.0), {}), "network.wifi.signal");
    EF_CHECK(s && s->level == Level::Advice && s->text_key == "chk.network.wifi_signal_weak" && s->args.at("pct") == "49");
    s = find(network_checks(wifi(50, 5.0), {}), "network.wifi.signal");
    EF_CHECK(s && s->level == Level::Good && s->text_key == "chk.network.wifi_signal_ok");
    s = find(network_checks(wifi(std::nullopt, 5.0), {}), "network.wifi.signal");
    EF_CHECK(s && s->level == Level::Unknown && s->text_key == "chk.network.wifi_signal_unknown");
    s = find(network_checks(wifi(150, 5.0), {}), "network.wifi.signal");
    EF_CHECK(s && s->args.at("pct") == "100");
}

EF_TEST(network_wifi_band_rules) {
    std::optional<Check> b = find(network_checks(wifi(80, 2.4), {}), "network.wifi.band");
    EF_CHECK(b && b->level == Level::Advice && b->text_key == "chk.network.wifi_band_24");
    b = find(network_checks(wifi(80, 5.0), {}), "network.wifi.band");
    EF_CHECK(b && b->level == Level::Good && b->args.at("band") == "5");
    b = find(network_checks(wifi(80, 6.0), {}), "network.wifi.band");
    EF_CHECK(b && b->level == Level::Good && b->args.at("band") == "6");
    b = find(network_checks(wifi(80, std::nullopt), {}), "network.wifi.band");
    EF_CHECK(b && b->level == Level::Unknown && b->text_key == "chk.network.wifi_band_unknown");
    // Ethernet'te Wi-Fi satiri yok.
    EF_CHECK(!find(network_checks(ethernet(), {}), "network.wifi.band"));
    EF_CHECK(!find(network_checks(ethernet(), {}), "network.wifi.signal"));
}

EF_TEST(network_other_adapter_is_info) {
    NetworkInfo n = ethernet();
    n.adapter_kind = "other";
    std::optional<Check> a = find(network_checks(n, {}), "network.adapter");
    EF_CHECK(a && a->level == Level::Info && a->text_key == "chk.network.other_adapter");
}

EF_TEST(network_background_download_rules) {
    NetworkInfo n = ethernet();
    n.background_rx_mbps = 5.5;
    std::optional<Check> b = find(network_checks(n, {}), "network.background");
    EF_CHECK(b && b->level == Level::Advice && b->text_key == "chk.network.background_download");
    EF_CHECK(b && b->args.at("mbps") == "5.5" && b->action == ActionKind::OpenSettings);
    n.background_rx_mbps = 5.0;
    b = find(network_checks(n, {}), "network.background");
    EF_CHECK(b && b->level == Level::Good);
    n.background_rx_mbps.reset();
    b = find(network_checks(n, {}), "network.background");
    EF_CHECK(b && b->level == Level::Unknown && b->text_key == "chk.network.background_unknown");
}

EF_TEST(network_power_saving_never_reported) {
    // Okunabilir kaynagi ve ayar sayfasi olmadigi icin bulgu uretilmez.
    NetworkInfo n = ethernet();
    EF_CHECK(!find(network_checks(n, {}), "network.power_saving"));
    n.adapter_power_saving = true;
    EF_CHECK(!find(network_checks(n, {}), "network.power_saving"));
}

EF_TEST(network_unknown_adapter_is_unknown_not_offline) {
    NetworkInfo n;
    n.adapter_kind = "unknown";
    const auto v = network_checks(n, clean_pings());
    std::optional<Check> a = find(v, "network.adapter");
    EF_CHECK(a && a->level == Level::Unknown && a->text_key == "chk.network.adapter_unknown");
    for (const auto& c : v) EF_CHECK(c.text_key != "chk.network.no_adapter");
    // Diger olcumler yine degerlendirilir.
    EF_CHECK(find(v, "network.background"));
    std::optional<Check> l = find(v, "network.latency");
    EF_CHECK(l && l->level == Level::Good);
    // Taninmayan deger de Unknown.
    n.adapter_kind = "bogus";
    a = find(network_checks(n, {}), "network.adapter");
    EF_CHECK(a && a->level == Level::Unknown);
}

EF_TEST(network_gateway_jitter_is_advice_loss_is_problem) {
    auto p = clean_pings();
    p[0].jitter_ms = 10.6;
    auto v = network_checks(ethernet(), p);
    std::optional<Check> g = find(v, "network.gateway");
    EF_CHECK(g && g->level == Level::Advice && g->text_key == "chk.network.local_unstable");
    EF_CHECK(g && g->args.at("jitter") == "10.6" && g->args.at("target") == "gateway");
    EF_CHECK(!find(v, "network.latency"));  // temiz denmez

    p = clean_pings();
    p[0].jitter_ms = 10.0;  // sinir dahil temiz
    EF_CHECK(!find(network_checks(ethernet(), p), "network.gateway"));

    p = clean_pings();
    p[0].received = 28;  // 2 kayip, %6.7
    g = find(network_checks(ethernet(), p), "network.gateway");
    EF_CHECK(g && g->level == Level::Problem && g->args.at("loss") == "6.7");

    // Hem yerel hem internet kotu: yalniz yerel sorun bildirilir.
    p = clean_pings();
    p[0].jitter_ms = 20;
    p[1].jitter_ms = 40;
    v = network_checks(ethernet(), p);
    EF_CHECK(find(v, "network.gateway") && !find(v, "network.internet"));
}

EF_TEST(network_loss_needs_two_packets_and_five_percent) {
    auto p = clean_pings();
    p[0].received = 29;  // tek kayip: gurultu
    EF_CHECK(!find(network_checks(ethernet(), p), "network.gateway"));
    p[0] = ps("gateway", 50, 48, 1.2, 0.3);  // 2 kayip ama %4
    EF_CHECK(!find(network_checks(ethernet(), p), "network.gateway"));
    p[0] = ps("gateway", 40, 38, 1.2, 0.3);  // 2 kayip, %5 (sinir dahil)
    std::optional<Check> g = find(network_checks(ethernet(), p), "network.gateway");
    EF_CHECK(g && g->level == Level::Problem && g->args.at("loss") == "5.0");
    // Internet: tek kayip temiz sayilir.
    p = clean_pings();
    p[1].received = 29;
    EF_CHECK(!find(network_checks(ethernet(), p), "network.internet"));
}

EF_TEST(network_too_few_samples_is_unknown) {
    // Gecit 10 ornekte kotu: karar yok; "kararli" da denmez.
    std::vector<PingStats> p{ps("gateway", 10, 7, 2, 15), ps("1.1.1.1", 30, 30, 12, 1.5)};
    auto v = network_checks(ethernet(), p);
    std::optional<Check> g = find(v, "network.gateway");
    EF_CHECK(g && g->level == Level::Unknown && g->text_key == "chk.network.too_few_samples");
    EF_CHECK(g && g->args.at("sent") == "10" && g->action == ActionKind::RunNetworkTest);
    EF_CHECK(!find(v, "network.latency"));
    // Internet 19 ornekte kotu: Unknown, Advice degil.
    p = {ps("gateway", 30, 30, 1, 0.2), ps("1.1.1.1", 19, 19, 20, 30)};
    std::optional<Check> i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->level == Level::Unknown && i->text_key == "chk.network.too_few_samples");
    // 20 ornek yeterli.
    p[1] = ps("1.1.1.1", 20, 20, 20, 30);
    i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->level == Level::Advice);
    // Temiz ama kisa olcum: "kararli" (Good) denmez; Wi-Fi "kararli" kaniti da olmaz.
    p = {ps("gateway", 10, 10, 1, 0.2), ps("1.1.1.1", 10, 10, 12, 1)};
    auto v2 = network_checks(ethernet(), p);
    g = find(v2, "network.gateway");
    EF_CHECK(g && g->level == Level::Unknown && g->text_key == "chk.network.too_few_samples");
    EF_CHECK(g && g->args.at("min") == "20");
    EF_CHECK(!find(v2, "network.latency"));
    for (const auto& c : v2) EF_CHECK(c.level != Level::Good || c.id != "network.latency");
    std::optional<Check> a = find(network_checks(wifi(80, 5.0), p), "network.adapter");
    EF_CHECK(a && a->level == Level::Advice && a->text_key == "chk.network.wifi_prefer_cable");
}

EF_TEST(network_short_run_never_good_even_with_loss_below_threshold) {
    // Iptal edilmis olcum: 2 gonderildi 1 alindi (%50 kayip, ama tek paket < kMinLost). Eskiden Good + "%50".
    std::vector<PingStats> p{ps("1.1.1.1", 2, 1, 12, 0)};
    auto v = network_checks(ethernet(), p);
    std::optional<Check> l = find(v, "network.latency");
    EF_CHECK(l && l->level == Level::Unknown && l->text_key == "chk.network.too_few_samples");
    EF_CHECK(l && l->args.at("sent") == "2" && l->args.at("target") == "1.1.1.1" && l->args.at("min") == "20");
    EF_CHECK(l && l->action == ActionKind::RunNetworkTest);
    for (const auto& c : v) EF_CHECK(!(c.id == "network.latency" && c.level == Level::Good));
    // 3 gonderildi 1 alindi (2 kayip = esik asildi): internet satiri Unknown, Good yok.
    p = {ps("gateway", 30, 30, 1, 0.2), ps("1.1.1.1", 3, 1, 12, 0)};
    v = network_checks(ethernet(), p);
    std::optional<Check> i = find(v, "network.internet");
    EF_CHECK(i && i->level == Level::Unknown && i->text_key == "chk.network.too_few_samples");
    EF_CHECK(!find(v, "network.latency"));
    // Bir hedef yeterli, digeri kisa: yine karar yok (kisa olan raporlanir).
    p = {ps("gateway", 30, 30, 1, 0.2), ps("1.1.1.1", 30, 30, 12, 1), ps("8.8.8.8", 5, 5, 10, 1)};
    l = find(network_checks(ethernet(), p), "network.latency");
    EF_CHECK(l && l->level == Level::Unknown && l->args.at("target") == "8.8.8.8");
    // Tam olcum: Good.
    p = {ps("gateway", 20, 20, 1, 0.2), ps("1.1.1.1", 20, 20, 12, 1)};
    l = find(network_checks(ethernet(), p), "network.latency");
    EF_CHECK(l && l->level == Level::Good && l->text_key == "chk.network.latency_ok");
}

EF_TEST(network_short_clean_gateway_does_not_claim_isp_queue) {
    // Gecit kisa olcumde temiz gorunuyor: yerel taraf kanitlanmadi -> internet sorunu "ISP kuyrugu" denmez.
    std::vector<PingStats> p{ps("gateway", 5, 5, 1, 0.2), ps("1.1.1.1", 30, 30, 20, 30)};
    const auto v = network_checks(ethernet(), p);
    std::optional<Check> g = find(v, "network.gateway");
    EF_CHECK(g && g->level == Level::Unknown && g->text_key == "chk.network.too_few_samples");
    std::optional<Check> i = find(v, "network.internet");
    EF_CHECK(i && i->level == Level::Advice && i->text_key == "chk.network.internet_unstable");
}

EF_TEST(network_worst_prefers_bad_target_on_badness_tie) {
    // Ilk hedef: jitter tam 15.0 (esik dahil temiz) -> badness 1.0. Ikinci: 2 kayip %5 -> badness 1.0 ve "kotu".
    std::vector<PingStats> p{ps("gateway", 40, 40, 1, 0.2), ps("1.1.1.1", 40, 40, 12, 15.0), ps("8.8.8.8", 40, 38, 15, 1)};
    std::optional<Check> i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->level == Level::Advice && i->args.at("target") == "8.8.8.8" && i->args.at("loss") == "5.0");
}

EF_TEST(network_worst_prefers_bad_target_with_enough_samples) {
    // 1.1.1.1: 5 ornek, cok yuksek jitter (daha "kotu" ama yetersiz ornek). 8.8.8.8: 30 ornek, jitter 22 (gecerli bulgu).
    // Tam olculmus hedefin bulgusu gizlenmemeli: too_few_samples yerine isp_queue (8.8.8.8).
    std::vector<PingStats> p{ps("gateway", 30, 30, 1, 0.2), ps("1.1.1.1", 5, 5, 20, 90), ps("8.8.8.8", 30, 30, 18, 22)};
    std::optional<Check> i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->level == Level::Advice && i->text_key == "chk.network.isp_queue");
    EF_CHECK(i && i->args.at("target") == "8.8.8.8" && i->args.at("jitter") == "22.0");
    // Sira tersine de ayni sonuc.
    std::vector<PingStats> q{ps("gateway", 30, 30, 1, 0.2), ps("8.8.8.8", 30, 30, 18, 22), ps("1.1.1.1", 5, 5, 20, 90)};
    i = find(network_checks(ethernet(), q), "network.internet");
    EF_CHECK(i && i->text_key == "chk.network.isp_queue" && i->args.at("target") == "8.8.8.8");
    // Yalniz az ornekli kotu hedef varsa (digeri temiz): yine Unknown too_few_samples, "kararli" denmez.
    std::vector<PingStats> r{ps("gateway", 30, 30, 1, 0.2), ps("1.1.1.1", 5, 5, 20, 90), ps("8.8.8.8", 30, 30, 18, 2)};
    const auto v = network_checks(ethernet(), r);
    i = find(v, "network.internet");
    EF_CHECK(i && i->level == Level::Unknown && i->text_key == "chk.network.too_few_samples" && i->args.at("target") == "1.1.1.1");
    EF_CHECK(!find(v, "network.latency"));
}

EF_TEST(network_test_failed_is_distinct_from_not_measured) {
    PingStats f;
    f.target = "1.1.1.1";
    f.failed = true;
    std::optional<Check> l = find(network_checks(ethernet(), {f}), "network.latency");
    EF_CHECK(l && l->level == Level::Unknown && l->text_key == "chk.network.test_failed");
    EF_CHECK(l && l->action == ActionKind::RunNetworkTest);
}

EF_TEST(network_numbers_one_decimal) {
    NetworkInfo n = ethernet();
    n.link_bps = 2500000000ull;
    n.background_rx_mbps = 12.34;
    const auto v = network_checks(n, {ps("gateway", 30, 30, 0.04, 0.0), ps("1.1.1.1", 30, 30, 123.46, 1)});
    EF_CHECK(find(v, "network.adapter")->args.at("speed") == "2500.0");
    EF_CHECK(find(v, "network.background")->args.at("mbps") == "12.3");
    std::optional<Check> l = find(v, "network.latency");
    EF_CHECK(l && l->args.at("avg") == "123.5" && l->args.at("jitter") == "1.0" && l->args.at("loss") == "0.0");
    EF_CHECK(l && l->args.at("gateway_avg") == "0.0");
}

EF_TEST(network_internet_jitter_with_clean_gateway_is_isp_queue) {
    auto p = clean_pings();
    p[2].jitter_ms = 22;
    const auto v = network_checks(ethernet(), p);
    std::optional<Check> i = find(v, "network.internet");
    EF_CHECK(i && i->level == Level::Advice && i->text_key == "chk.network.isp_queue");
    EF_CHECK(i && i->args.at("target") == "8.8.8.8" && i->args.at("jitter") == "22.0");
    EF_CHECK(i && i->action == ActionKind::OpenUrl && i->action_arg == "https://www.waveform.com/tools/bufferbloat");
    EF_CHECK(!find(v, "network.latency"));

    p = clean_pings();
    p[1].received = 28;  // internet kaybi (2 paket, %6.7)
    i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->text_key == "chk.network.isp_queue" && i->args.at("target") == "1.1.1.1");

    p = clean_pings();
    p[1].jitter_ms = 15.0;  // sinir dahil temiz
    EF_CHECK(!find(network_checks(ethernet(), p), "network.internet"));
}

EF_TEST(network_internet_bad_without_gateway_is_generic) {
    std::vector<PingStats> p{ps("1.1.1.1", 30, 27, 30, 25)};
    std::optional<Check> i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->level == Level::Advice && i->text_key == "chk.network.internet_unstable");
    // Gecit yanit vermedi (Unknown) ama internet kotu: genel oneri, yerel sorun degil.
    p.insert(p.begin(), ps("gateway", 30, 0, 0, 0));
    const auto v = network_checks(ethernet(), p);
    std::optional<Check> g = find(v, "network.gateway");
    EF_CHECK(g && g->level == Level::Unknown && g->text_key == "chk.network.gateway_no_reply");
    i = find(v, "network.internet");
    EF_CHECK(i && i->text_key == "chk.network.internet_unstable");
}

EF_TEST(network_no_internet_replies_is_unknown) {
    std::vector<PingStats> p{ps("gateway", 30, 30, 1, 0.2), ps("1.1.1.1", 30, 0, 0, 0), ps("8.8.8.8", 30, 0, 0, 0)};
    const auto v = network_checks(ethernet(), p);
    std::optional<Check> i = find(v, "network.internet");
    EF_CHECK(i && i->level == Level::Unknown && i->text_key == "chk.network.internet_no_reply");
    EF_CHECK(!find(v, "network.latency"));
    // Biri yanit verdi: o degerlendirilir.
    p[2] = ps("8.8.8.8", 30, 30, 15, 1);
    std::optional<Check> l = find(network_checks(ethernet(), p), "network.latency");
    EF_CHECK(l && l->level == Level::Good && l->args.at("target") == "8.8.8.8");
}

EF_TEST(network_gateway_only_measured) {
    std::vector<PingStats> p{ps("gateway", 30, 30, 1, 0.2)};
    std::optional<Check> i = find(network_checks(ethernet(), p), "network.internet");
    EF_CHECK(i && i->level == Level::Unknown && i->text_key == "chk.network.internet_not_measured");
}

EF_TEST(network_check_ids_unique_and_sorted) {
    auto v = network_checks(wifi(30, 2.4), {ps("gateway", 30, 25, 3, 12), ps("1.1.1.1", 30, 30, 20, 2)});
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = i + 1; j < v.size(); ++j) EF_CHECK(v[i].id != v[j].id);
    sort_checks(v);
    EF_CHECK(!v.empty() && v.front().level == Level::Problem);
}

// ---- saf okuyucu yardimcilari ----
EF_TEST(network_reader_parse_ipv4) {
    EF_CHECK(pr::parse_ipv4("1.1.1.1") == 0x01010101u);
    EF_CHECK(pr::parse_ipv4("192.168.0.1") == (192u | (168u << 8) | (0u << 16) | (1u << 24)));
    EF_CHECK(pr::format_ipv4(*pr::parse_ipv4("192.168.0.1")) == "192.168.0.1");
    EF_CHECK(pr::format_ipv4(*pr::parse_ipv4("8.8.8.8")) == "8.8.8.8");
    EF_CHECK(!pr::parse_ipv4(""));
    EF_CHECK(!pr::parse_ipv4("1.1.1"));
    EF_CHECK(!pr::parse_ipv4("1.1.1.1.1"));
    EF_CHECK(!pr::parse_ipv4("256.1.1.1"));
    EF_CHECK(!pr::parse_ipv4("1..1.1"));
    EF_CHECK(!pr::parse_ipv4("1.1.1.1 "));
    EF_CHECK(!pr::parse_ipv4("a.b.c.d"));
    EF_CHECK(!pr::parse_ipv4("0001.1.1.1"));
}

EF_TEST(network_reader_wifi_band) {
    EF_CHECK(pr::wifi_band_from_freq_mhz(2437) == 2.4);
    EF_CHECK(pr::wifi_band_from_freq_mhz(5180) == 5.0);
    EF_CHECK(pr::wifi_band_from_freq_mhz(5955) == 6.0);
    EF_CHECK(!pr::wifi_band_from_freq_mhz(0));
    EF_CHECK(!pr::wifi_band_from_freq_mhz(60480));  // 60 GHz (11ad) desteklenmiyor

    EF_CHECK(pr::wifi_band_from_channel(1, false) == 2.4);
    EF_CHECK(pr::wifi_band_from_channel(14, false) == 2.4);
    EF_CHECK(pr::wifi_band_from_channel(36, false) == 5.0);
    EF_CHECK(pr::wifi_band_from_channel(149, false) == 5.0);
    EF_CHECK(pr::wifi_band_from_channel(177, false) == 5.0);
    EF_CHECK(!pr::wifi_band_from_channel(0, false));
    EF_CHECK(!pr::wifi_band_from_channel(20, false));
    EF_CHECK(!pr::wifi_band_from_channel(200, false));
    // 6 GHz olasi bagdastirici: cakisan kanallar belirsiz, digerleri kesin.
    EF_CHECK(!pr::wifi_band_from_channel(1, true));
    EF_CHECK(!pr::wifi_band_from_channel(2, true));
    EF_CHECK(pr::wifi_band_from_channel(6, true) == 2.4);
    EF_CHECK(pr::wifi_band_from_channel(11, true) == 2.4);
    EF_CHECK(pr::wifi_band_from_channel(36, true) == 5.0);
    EF_CHECK(!pr::wifi_band_from_channel(149, true));
    EF_CHECK(pr::wifi_band_from_channel(181, true) == 6.0);
    EF_CHECK(pr::wifi_band_from_channel(233, true) == 6.0);
    EF_CHECK(!pr::wifi_band_from_channel(234, true));
}

EF_TEST(network_reader_rssi_and_rate) {
    EF_CHECK(pr::wifi_signal_pct_from_rssi(-100) == 0);
    EF_CHECK(pr::wifi_signal_pct_from_rssi(-120) == 0);
    EF_CHECK(pr::wifi_signal_pct_from_rssi(-75) == 50);
    EF_CHECK(pr::wifi_signal_pct_from_rssi(-50) == 100);
    EF_CHECK(pr::wifi_signal_pct_from_rssi(-30) == 100);

    const auto r = pr::rx_mbps(1000, 1000 + 1250000, 1.0);  // 1.25 MB/s = 10 Mbit/s
    EF_CHECK(r && near(*r, 10.0));
    EF_CHECK(!pr::rx_mbps(2000, 1000, 1.0));
    EF_CHECK(!pr::rx_mbps(1000, 2000, 0.0));
    EF_CHECK(pr::rx_mbps(5, 5, 1.0) == 0.0);
}

EF_TEST(network_reader_adapter_kind) {
    EF_CHECK(pr::adapter_kind_from_type(6, 14, true) == "ethernet");
    EF_CHECK(pr::adapter_kind_from_type(6, 0, true) == "ethernet");
    EF_CHECK(pr::adapter_kind_from_type(6, 0, false) == "other");  // Hyper-V/TAP sanal
    EF_CHECK(pr::adapter_kind_from_type(6, 9, true) == "wifi");
    EF_CHECK(pr::adapter_kind_from_type(71, 9, true) == "wifi");
    EF_CHECK(pr::adapter_kind_from_type(243, 8, true) == "other");  // WWAN
    EF_CHECK(pr::adapter_kind_from_type(53, 0, false) == "other");  // PROP_VIRTUAL
}

EF_TEST(network_reader_default_targets_and_stats) {
    pr::NetworkSnapshot s;
    auto t = pr::default_ping_targets(s);
    EF_CHECK(t.size() == 2 && t[0].label == "1.1.1.1" && t[1].label == "8.8.8.8");
    s.gateway_ipv4 = "192.168.0.1";
    t = pr::default_ping_targets(s);
    EF_CHECK(t.size() == 3 && t[0].label == "gateway" && t[0].ipv4 == "192.168.0.1");
    std::vector<pr::PingRun> runs(3);
    runs[0].rtt_ms = {1.0, 2.0};
    runs[1].rtt_ms = {std::nullopt};
    runs[2].error = pr::PingError::IcmpOpenFailed;
    runs[2].win32_error = 5;
    const auto st = pr::ping_test_stats(t, runs);
    EF_CHECK(st.size() == 3 && st[0].target == "gateway" && st[0].received == 2 && !st[0].failed);
    EF_CHECK(st[1].sent == 1 && st[1].received == 0 && !st[1].failed);
    EF_CHECK(st[2].sent == 0 && st[2].failed);
    // Gecersiz hedef / sifir ornek: paket gonderilmez.
    const auto none = pr::run_ping_test({{"bad", "not-an-ip"}}, 3, 0);
    EF_CHECK(none.size() == 1 && none[0].rtt_ms.empty() && none[0].error == pr::PingError::InvalidTarget);
    const auto zero = pr::run_ping_test({{"1.1.1.1", "1.1.1.1"}}, 0, 0);
    EF_CHECK(zero.size() == 1 && zero[0].rtt_ms.empty() && zero[0].error == pr::PingError::None);
}
