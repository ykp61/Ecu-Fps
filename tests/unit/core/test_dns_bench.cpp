// T10: DNS karsilastirma (saf cekirdek): siralama, basarisizlik, yetersiz veri, "belirgin fark" esigi, mevcut zaten en iyi.
#include <optional>
#include <string>
#include <vector>

#include "core/adapters/windows/dns.h"
#include "core/net/dns_bench.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::net;
namespace cr = ef::core::readiness;

namespace {
// n ornek: ilk `fails` tanesi basarisiz, kalani sabit ms.
ResolverSamples rs(std::string id, double ms, int n = 15, int fails = 0) {
    ResolverSamples s{std::move(id), {}};
    for (int i = 0; i < n; ++i) s.ms.push_back(i < fails ? std::nullopt : std::optional<double>(ms));
    return s;
}
}  // namespace

EF_TEST(dns_bench_summarize_median_p95_fail) {
    ResolverSamples s{"x", {}};
    for (double v : {10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0}) s.ms.push_back(v);
    s.ms.push_back(std::nullopt);
    s.ms.push_back(std::nullopt);
    const auto st = summarize({s}).front();
    EF_CHECK(st.total == 12 && st.ok_count == 10 && st.ok);
    EF_CHECK(st.median_ms == 55.0 && st.p95_ms == 100.0);
    EF_CHECK(st.fail_pct > 16.6 && st.fail_pct < 16.7);
}

EF_TEST(dns_bench_insufficient_samples_not_ok) {
    const auto st = summarize({rs("a", 10.0, 15, 6)}).front();  // 9 basarili < 10
    EF_CHECK(st.ok_count == 9 && !st.ok);
    EF_CHECK(summarize({rs("a", 10.0, 15, 5)}).front().ok);  // tam 10: yeterli
    EF_CHECK(!summarize({ResolverSamples{"e", {}}}).front().ok);
    // Hicbir cozucu yeterli veri vermedi: en iyi yok, anlamli degil.
    const auto v = verdict(summarize({rs("current", 40.0, 15, 10), rs("google", 10.0, 15, 10)}));
    EF_CHECK(v.best_id.empty() && !v.meaningful);
}

EF_TEST(dns_bench_ranking_and_meaningful_threshold) {
    // 40 ms -> 20 ms: 20 ms (%50) kazanc, 0 hata: anlamli.
    auto v = verdict(summarize({rs("current", 40.0), rs("cloudflare", 20.0), rs("google", 25.0)}));
    EF_CHECK(v.best_id == "cloudflare" && v.meaningful);
    // Mutlak esik: 4 ms fark (%40) anlamsiz; 5 ms fark (%25) anlamli.
    v = verdict(summarize({rs("current", 10.0), rs("cloudflare", 6.0)}));
    EF_CHECK(v.best_id == "cloudflare" && !v.meaningful);
    v = verdict(summarize({rs("current", 20.0), rs("cloudflare", 15.0)}));
    EF_CHECK(v.meaningful);
    // Oransal esik: 6 ms fark ama %12 -> anlamsiz.
    v = verdict(summarize({rs("current", 50.0), rs("cloudflare", 44.0)}));
    EF_CHECK(v.best_id == "cloudflare" && !v.meaningful);
}

EF_TEST(dns_bench_failures_block_meaningful) {
    // En iyi cozucunun tek basarisiz sorgusu bile "belirgin fark" vermez.
    auto v = verdict(summarize({rs("current", 40.0), rs("cloudflare", 10.0, 15, 1)}));
    EF_CHECK(v.best_id == "cloudflare" && !v.meaningful);
    // Mevcut cozucu yetersiz veri verdiyse taban yok: anlamli sayilmaz.
    v = verdict(summarize({rs("current", 40.0, 15, 10), rs("cloudflare", 10.0)}));
    EF_CHECK(v.best_id == "cloudflare" && !v.meaningful);
    // Mevcut yok (okunamadi): anlamli degil.
    v = verdict(summarize({rs("cloudflare", 10.0), rs("google", 12.0)}));
    EF_CHECK(!v.meaningful);
}

EF_TEST(dns_bench_current_already_best) {
    const auto v = verdict(summarize({rs("current", 12.0), rs("cloudflare", 30.0), rs("quad9", 40.0)}));
    EF_CHECK(v.best_id == "current" && !v.meaningful);
    // Esitlikte mevcut kalir.
    EF_CHECK(verdict(summarize({rs("current", 20.0), rs("google", 20.0)})).best_id == "current");
}

EF_TEST(dns_bench_private_ipv4) {
    for (const char* ip : {"192.168.1.1", "10.0.0.2", "172.16.0.1", "172.31.255.1", "169.254.1.1"}) EF_CHECK(is_private_ipv4(ip));
    for (const char* ip : {"8.8.8.8", "172.32.0.1", "172.15.0.1", "1.1.1.1", "", "192.168.1", "192.168.1.256", "a.b.c.d", "192.168.1.1.1"})
        EF_CHECK(!is_private_ipv4(ip));
}

EF_TEST(dns_bench_constants_fixed) {
    static_assert(std::size(kDnsBenchHosts) == 5 && kDnsBenchRounds == 3 && kDnsBenchTimeoutMs == 2000);
    EF_CHECK(true);
}

// Satirlar: olcum yokken sayi yok; olcum suruyorken dugme yok; belirgin farkta mevcut fix.dns eylemi, secili liste ile.
EF_TEST(dns_bench_rows) {
    cr::Check dns_row;
    dns_row.id = "network.dns.{0A1B2C3D-0000-1111-2222-333344445555}";
    dns_row.text_key = "chk.network.dns_auto";
    const std::vector<cr::Check> existing{dns_row};

    const auto idle = cr::dns_bench_checks(false, std::nullopt, existing);
    EF_CHECK(idle.size() == 1 && idle[0].id == "network.dns_bench" && idle[0].action == cr::ActionKind::RunDnsBench && idle[0].args.empty());
    const auto busy = cr::dns_bench_checks(true, std::nullopt, existing);
    EF_CHECK(busy.size() == 1 && busy[0].action == cr::ActionKind::None && busy[0].text_key == "chk.network.dns_bench.measuring");

    DnsBenchReport rep;
    rep.current_modem = true;
    rep.current_name = "192.168.1.1";
    rep.stats = summarize({rs("current", 40.0), rs("cloudflare", 20.0), rs("google", 25.0, 15, 10), rs("adguard", 30.0)});
    rep.verdict = verdict(rep.stats);
    auto rows = cr::dns_bench_checks(false, rep, existing);
    auto find = [&](const std::string& id) -> const cr::Check* {
        for (const auto& c : rows)
            if (c.id == id) return &c;
        return nullptr;
    };
    EF_CHECK(find("network.dns_bench.row.current") && find("network.dns_bench.row.current")->text_key == "chk.network.dns_bench.row_modem");
    EF_CHECK(find("network.dns_bench.row.cloudflare")->args.at("ms") == "20.0" && find("network.dns_bench.row.cloudflare")->args.at("fail") == "0");
    EF_CHECK(find("network.dns_bench.row.google")->text_key == "chk.network.dns_bench.row_na");  // veri yetersiz: sayi yok
    EF_CHECK(find("network.dns_bench.adguard") != nullptr);                                        // AdGuard notu
    const auto* fix = find("network.dns_bench.fix.{0A1B2C3D-0000-1111-2222-333344445555}");
    EF_CHECK(fix && fix->action == cr::ActionKind::AutoFix && fix->action_arg == "fix.dns" && fix->args.at("preselect") == "cloudflare");
    EF_CHECK(fix && fix->text_key == "chk.network.dns_bench.better" && fix->args.at("name") == "Cloudflare");
    if (fix) EF_CHECK(cr::fix_target_for_check(fix->id) == "dns:{0A1B2C3D-0000-1111-2222-333344445555}");

    // Mevcut zaten en iyi: eylem yok, "fark anlamsiz".
    rep.stats = summarize({rs("current", 12.0), rs("cloudflare", 30.0)});
    rep.verdict = verdict(rep.stats);
    rows = cr::dns_bench_checks(false, rep, existing);
    EF_CHECK(find("network.dns_bench.verdict") && find("network.dns_bench.verdict")->text_key == "chk.network.dns_bench.same");
    for (const auto& c : rows) EF_CHECK(c.action != cr::ActionKind::AutoFix);

    // Politika engelli DNS satiri: belirgin fark olsa bile eylem sunulmaz.
    cr::Check blocked = dns_row;
    blocked.text_key = "chk.network.dns_policy";
    rep.stats = summarize({rs("current", 40.0), rs("cloudflare", 10.0)});
    rep.verdict = verdict(rep.stats);
    for (const auto& c : cr::dns_bench_checks(false, rep, {blocked})) EF_CHECK(c.action != cr::ActionKind::AutoFix);

    // Hicbir cozucu veri vermedi: sayi yok, "veri yetersiz".
    rep.stats = summarize({rs("current", 40.0, 15, 15), rs("cloudflare", 10.0, 15, 15)});
    rep.verdict = verdict(rep.stats);
    rows = cr::dns_bench_checks(false, rep, existing);
    EF_CHECK(find("network.dns_bench.verdict")->text_key == "chk.network.dns_bench.insufficient");
}
