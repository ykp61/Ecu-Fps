// T10: DNS karsilastirma satirlari. Yalniz kullanici "DNS karsilastir" dediginde yapilan gercek olcumun sonucunu yazar;
// olcum yoksa sayi yoktur. DNS hizi alan adi cozumlemeyi etkiler, oyun ici ping'i degil (metinler bunu soyler).
#include <cmath>
#include <cstdio>
#include <string>

#include "core/adapters/windows/dns.h"
#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

std::string fmt(double v, const char* f) {
    char buf[32];
    if (!std::isfinite(v)) v = 0;
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Network;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

}  // namespace

std::vector<Check> dns_bench_checks(bool measuring, const std::optional<net::DnsBenchReport>& report, const std::vector<Check>& existing) {
    namespace aw = adapters::windows;
    std::vector<Check> out;
    // Arayuz ve ilke durumu: mevcut DNS satirindan (o satir ilke engelinde veya okunamadiysa eylem sunulmaz).
    std::string guid;
    bool fix_allowed = false;
    for (const auto& c : existing) {
        if (c.id.rfind("network.dns.", 0) != 0) continue;
        guid = c.id.substr(12);
        fix_allowed = c.text_key == "chk.network.dns_auto" || c.text_key == "chk.network.dns_manual" || c.text_key == "chk.network.dns_preset";
        break;
    }

    if (measuring) {
        out.push_back(make("network.dns_bench", Level::Info, "chk.network.dns_bench.measuring"));  // dugme yok
        return out;
    }
    Check head = make("network.dns_bench", Level::Info, report ? "chk.network.dns_bench.done" : "chk.network.dns_bench");
    head.action = ActionKind::RunDnsBench;
    if (!report) {
        out.push_back(std::move(head));
        return out;
    }
    head.args["n"] = std::to_string(report->stats.empty() ? 0 : report->stats.front().total);
    out.push_back(std::move(head));

    bool has_adguard = false;
    for (const auto& s : report->stats) {
        const bool cur = s.id == net::kDnsBenchCurrentId;
        const aw::DnsPreset* p = cur ? nullptr : aw::find_dns_preset(s.id);
        if (!cur && !p) continue;  // bilinmeyen kimlik gosterilmez
        has_adguard = has_adguard || (p && p->id == "adguard");
        std::string key = "chk.network.dns_bench.row";
        if (cur) key = report->current_modem ? "chk.network.dns_bench.row_modem" : "chk.network.dns_bench.row_current";
        if (!s.ok) key += "_na";
        Check c = make("network.dns_bench.row." + s.id, Level::Info, key);
        c.args["name"] = cur ? report->current_name : std::string(p->name);
        c.args["ms"] = fmt(s.median_ms, "%.1f");
        c.args["fail"] = fmt(s.fail_pct, "%.0f");
        out.push_back(std::move(c));
    }

    const auto& v = report->verdict;
    if (v.best_id.empty()) {
        out.push_back(make("network.dns_bench.verdict", Level::Info, "chk.network.dns_bench.insufficient"));
    } else {
        const net::ResolverStat* cur = nullptr;
        const net::ResolverStat* best = nullptr;
        for (const auto& s : report->stats) {
            if (s.id == net::kDnsBenchCurrentId) cur = &s;
            if (s.id == v.best_id) best = &s;
        }
        if (!cur || !cur->ok) {
            out.push_back(make("network.dns_bench.verdict", Level::Info, "chk.network.dns_bench.no_baseline"));
        } else if (v.meaningful && best) {
            const auto* p = aw::find_dns_preset(v.best_id);
            Check c = make(guid.empty() || !fix_allowed ? "network.dns_bench.verdict" : "network.dns_bench.fix." + guid,
                           Level::Info, "chk.network.dns_bench.better");
            c.args["name"] = p ? std::string(p->name) : v.best_id;
            c.args["gain"] = fmt(cur->median_ms - best->median_ms, "%.1f");
            c.args["pct"] = fmt(100.0 * (cur->median_ms - best->median_ms) / cur->median_ms, "%.0f");
            if (p && !guid.empty() && fix_allowed) {
                c.level = Level::Advice;
                c.action = ActionKind::AutoFix;
                c.action_arg = "fix.dns";  // mevcut Evet/Hayir + UAC akisi; hazir liste onceden secili gelir
                c.args["preselect"] = v.best_id;
            }
            out.push_back(std::move(c));
        } else {
            out.push_back(make("network.dns_bench.verdict", Level::Info, "chk.network.dns_bench.same"));
        }
    }
    if (has_adguard) out.push_back(make("network.dns_bench.adguard", Level::Info, "chk.network.dns_bench.adguard_note"));
    return out;
}

}  // namespace ef::core::readiness
