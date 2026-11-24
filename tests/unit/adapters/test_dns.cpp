// EF-X14: DNS adaptoru, hedef kimligi ve ilke karari (sahte API; gercek NIC degistirilmez).
#include "core/adapters/windows/dns.h"
#include "core/broker/admin.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using namespace ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;

namespace {
DnsPolicyFacts allowed() { return {false, false, false, false}; }
ef::contracts::SettingOperation op(const std::string& guid, std::string value) {
    return {OperationId::DnsServersSet, OperationScope::Machine, dns_target(guid), std::move(value), false, false};
}
}  // namespace

EF_TEST(x14_t01_dhcp_to_manual_back_to_dhcp) {
    FakeDns api;  // otomatik (DHCP)
    DnsAdapter a(api, allowed);
    const auto o = op(api.guid, std::string(find_dns_preset("cloudflare")->servers));
    const auto snap = a.capture(o);
    EF_CHECK(snap.ok() && snap.value().old_value == "auto|auto");
    EF_CHECK(a.apply(o).ok() && a.read_back(o).value() == std::optional<std::string>(std::string(find_dns_preset("cloudflare")->servers)));
    EF_CHECK(!api.servers6.empty());  // IPv6 de ayarlandi: eski IPv6 cozucu kullanilmaya devam etmez
    EF_CHECK(a.restore(o, snap.value()).ok() && api.servers.empty() && api.servers6.empty());  // statik listeye cevrilmedi
    api.servers = "192.168.1.5";  // onceden elle ayarli
    const auto manual = a.capture(o);
    EF_CHECK(a.apply(o).ok() && a.restore(o, manual.value()).ok() && api.servers == "192.168.1.5" && api.servers6.empty());
}

EF_TEST(x14_t02_target_is_interface_guid_not_index) {
    EF_CHECK(parse_dns_target("dns:{0a1b2c3d-0000-1111-2222-333344445555}") == std::optional<std::string>("{0A1B2C3D-0000-1111-2222-333344445555}"));
    EF_CHECK(!parse_dns_target("dns:12"));             // arayuz numarasi kimlik degil
    EF_CHECK(!parse_dns_target("dns:{0a1b2c3d-0000-1111-2222-33334444555}"));
    EF_CHECK(!parse_dns_target("hags:x"));
    FakeDns api;
    DnsAdapter a(api, allowed);
    api.gone = true;  // arayuz kalkti: baska NIC'e yazilmaz, geri alma no-op
    EF_CHECK(a.target_gone(op(api.guid, "auto")) && !a.apply(op(api.guid, std::string(find_dns_preset("cloudflare")->servers))).ok() && api.writes == 0);
}

EF_TEST(x14_only_presets_reach_broker) {
    const std::string g = "{0A1B2C3D-0000-1111-2222-333344445555}";
    EF_CHECK(ef::core::broker::admin_operation_allowed(op(g, "1.1.1.1,1.0.0.1|2606:4700:4700::1111,2606:4700:4700::1001")));
    EF_CHECK(!ef::core::broker::admin_operation_allowed(op(g, "9.9.9.9,149.112.112.112")));  // yalniz IPv4: eksik, IPv6 cozucu kalirdi
    EF_CHECK(!ef::core::broker::admin_operation_allowed(op(g, "6.6.6.6")));  // serbest adres yok
    EF_CHECK(!ef::core::broker::admin_operation_allowed(op(g, "auto")));     // yeni yazma otomatik degil; geri alma snapshot ile
    auto bad = op("12", std::string(find_dns_preset("cloudflare")->servers));
    EF_CHECK(!ef::core::broker::admin_operation_allowed(bad));
    FakeDns api;
    DnsAdapter a(api, allowed);
    EF_CHECK(!a.apply(op(api.guid, "6.6.6.6")).ok() && api.writes == 0);
}

EF_TEST(x14_policy_conflicts_and_unknown_block) {
    DnsPolicyFacts f{false, false, false, false};
    EF_CHECK(dns_policy(f) == DnsPolicy::Allowed);
    f.vpn_active = true;
    EF_CHECK(dns_policy(f) == DnsPolicy::Vpn);
    f.vpn_active = false;
    f.nrpt_rules = std::nullopt;
    EF_CHECK(dns_policy(f) == DnsPolicy::Unknown);
    f.nrpt_rules = false;
    f.doh_policy = true;
    EF_CHECK(dns_policy(f) == DnsPolicy::DohPolicy);
    f.doh_policy = false;
    f.domain_joined = true;
    EF_CHECK(dns_policy(f) == DnsPolicy::Domain);
}

EF_TEST(x14_readiness_row_maps_to_undo_target) {
    EF_CHECK(ef::core::readiness::fix_target_for_check("network.dns.{0A1B2C3D-0000-1111-2222-333344445555}") ==
              "dns:{0A1B2C3D-0000-1111-2222-333344445555}");
    EF_CHECK(ef::core::readiness::fix_target_for_check("network.dns.x").empty());
}

EF_TEST(x14_policy_rechecked_at_write_time) {
    FakeDns api;
    bool vpn = false;
    DnsAdapter a(api, [&vpn] { return DnsPolicyFacts{vpn, false, false, false}; });
    const auto o = op(api.guid, std::string(find_dns_preset("cloudflare")->servers));
    const auto snap = a.capture(o);
    vpn = true;  // tarama sonrasi VPN baglandi
    EF_CHECK(!a.apply(o).ok() && api.writes == 0);
    EF_CHECK(a.restore(o, snap.value()).ok());  // geri alma ilkeye takilmaz
    DnsAdapter none(api, {});
    EF_CHECK(!none.apply(o).ok());  // ilke okuyucusu yoksa yeni yazma yok
}

// EF-X14-T01 (her hazir liste): DHCP -> secilen liste -> geri al: IPv4 ve IPv6 gercekten otomatige doner.
EF_TEST(x14_t01_every_preset_restores_dhcp) {
    for (const auto& p : kDnsPresets) {
        FakeDns api;
        DnsAdapter a(api, allowed);
        const auto o = dns_preset_operation(dns_target(api.guid), p.id);
        EF_CHECK(o.has_value());
        if (!o) continue;
        const auto snap = a.capture(*o);
        EF_CHECK(snap.ok());
        if (!snap.ok()) continue;
        EF_CHECK(snap.value().old_value == "auto|auto");
        EF_CHECK(a.apply(*o).ok() && !api.servers.empty() && !api.servers6.empty());
        EF_CHECK(a.read_back(*o).value() == std::optional<std::string>(std::string(p.servers)));
        EF_CHECK(a.restore(*o, snap.value()).ok() && api.servers.empty() && api.servers6.empty());
    }
}

// Secim dogrulama + geri alma hedefi (T02): yalniz derlenmis kimlik ve GUID hedefi; islem broker izin listesinden
// gecer ve hedefi satirin geri alma hedefiyle ayni.
EF_TEST(x14_resolver_choice_only_compiled_presets) {
    const auto target = ef::core::readiness::fix_target_for_check("network.dns.{0A1B2C3D-0000-1111-2222-333344445555}");
    EF_CHECK(target == "dns:{0A1B2C3D-0000-1111-2222-333344445555}");
    for (const char* id : {"cloudflare", "google", "quad9", "adguard"}) {
        const auto o = dns_preset_operation(target, id);
        EF_CHECK(o && o->id == OperationId::DnsServersSet && o->scope == OperationScope::Machine);
        EF_CHECK(o && o->target == target);
        EF_CHECK(o && o->new_value == find_dns_preset(id)->servers);
        EF_CHECK(o && ef::core::broker::admin_operation_allowed(*o));
    }
    for (const char* id : {"", "Cloudflare", "opendns", "auto", "1.1.1.1"}) EF_CHECK(!dns_preset_operation(target, id));
    EF_CHECK(!dns_preset_operation("dns:12", "google"));  // arayuz numarasi hedef olamaz
    EF_CHECK(!dns_preset_operation("hags:hwschmode", "google"));
    static_assert(std::size(kDnsPresets) == 4);  // yeni saglayici yalniz belge/ADR ile (ADR-035: AdGuard)
}

// Satir karari: IPv4 ve IPv6 birlikte birebir eslesirse hazir liste; yarim eslesme degisiklik onerir.
EF_TEST(x14_current_preset_needs_both_families) {
    const auto* cf = dns_current_preset("1.1.1.1,1.0.0.1", "2606:4700:4700::1111,2606:4700:4700::1001");
    EF_CHECK(cf && cf->id == "cloudflare" && cf->name == "Cloudflare");
    const auto* q9 = dns_current_preset("9.9.9.9,149.112.112.112", "2620:fe::fe,2620:fe::9");
    EF_CHECK(q9 && q9->id == "quad9");
    EF_CHECK(!dns_current_preset("1.1.1.1,1.0.0.1", ""));  // IPv6 otomatik: eski IPv6 cozucu kalir
    EF_CHECK(!dns_current_preset("", ""));                 // otomatik (DHCP)
    EF_CHECK(!dns_current_preset("8.8.8.8,8.8.4.4", "2606:4700:4700::1111,2606:4700:4700::1001"));
    EF_CHECK(!dns_current_preset("1.0.0.1,1.1.1.1", "2606:4700:4700::1111,2606:4700:4700::1001"));  // sira farkli
}

// Secim penceresi gercek adresleri gosterir (uydurma etiket yok).
EF_TEST(x14_preset_labels_show_real_servers) {
    EF_CHECK(dns_preset_label(*find_dns_preset("cloudflare")) ==
              "Cloudflare - 1.1.1.1, 1.0.0.1; IPv6 2606:4700:4700::1111, 2606:4700:4700::1001");
    for (const auto& p : kDnsPresets) {
        const auto l = dns_preset_label(p);
        EF_CHECK(l.rfind(std::string(p.name), 0) == 0 && l.find("IPv6 ") != std::string::npos);
    }
}

// Kablolama: sabit Cloudflare yolu kalmadi; satir "fix.dns" uretir, App secimi pencereden alip islemi kurar.
EF_TEST(x14_app_wiring_user_picks_preset) {
    auto read = [](const char* rel) {
        std::ifstream f(std::string(EF_SOURCE_ROOT) + "/" + rel);
        EF_CHECK(f.good());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    // Denetim 4.4: App uye islevleri app*.cpp dosyalarina bolundu; kablolama hangisindeyse orada aranir.
    std::string app;
    for (const char* f : {"src/app/app.cpp", "src/app/app_games.cpp", "src/app/app_readiness.cpp", "src/app/app_measure.cpp",
                          "src/app/app_drivers.cpp", "src/app/app_system.cpp", "src/app/app_internal.h"})
        app += read(f);
    const auto collect = read("src/app/readiness_collect.cpp");
    for (const std::string* s : {static_cast<const std::string*>(&app), &collect}) {
        EF_CHECK(s->find("fix.dns.cloudflare") == std::string::npos);
        EF_CHECK(s->find("find_dns_preset(\"cloudflare\")") == std::string::npos);
    }
    EF_CHECK(collect.find("c.action_arg = \"fix.dns\";") != std::string::npos);
    EF_CHECK(app.find("ui::ask_dns_preset(") != std::string::npos && app.find("dns_preset_operation(") != std::string::npos);
}

// T10 (ADR-035): AdGuard hazir listesi. IPv4 ve IPv6 birlikte, broker dogrulamasindan gecer; serbest adres yine yok.
EF_TEST(t10_adguard_preset) {
    const auto* ag = find_dns_preset("adguard");
    EF_CHECK(ag && ag->name == "AdGuard");
    EF_CHECK(ag && ag->servers == "94.140.14.14,94.140.15.15|2a10:50c0::ad1:ff,2a10:50c0::ad2:ff");
    EF_CHECK(dns_preset_value("94.140.14.14,94.140.15.15|2a10:50c0::ad1:ff,2a10:50c0::ad2:ff"));
    EF_CHECK(!dns_preset_value("94.140.14.14,94.140.15.15|"));          // yarim (yalniz IPv4) liste kabul edilmez
    EF_CHECK(!dns_preset_value("94.140.14.14|2a10:50c0::ad1:ff"));      // serbest/eksik adres yok
    const auto* cur = dns_current_preset("94.140.14.14,94.140.15.15", "2a10:50c0::ad1:ff,2a10:50c0::ad2:ff");
    EF_CHECK(cur && cur->id == "adguard");
    EF_CHECK(dns_preset_label(*ag) == "AdGuard - 94.140.14.14, 94.140.15.15; IPv6 2a10:50c0::ad1:ff, 2a10:50c0::ad2:ff");
    const auto target = dns_target("{0A1B2C3D-0000-1111-2222-333344445555}");
    const auto o = dns_preset_operation(target, "adguard");
    EF_CHECK(o && o->id == OperationId::DnsServersSet && o->scope == OperationScope::Machine && o->new_value == ag->servers);
    EF_CHECK(o && ef::core::broker::admin_operation_allowed(*o));
    // Journal'li yazma + geri okuma + geri alma (sahte API).
    FakeDns api;
    DnsAdapter a(api, allowed);
    const auto op2 = dns_preset_operation(dns_target(api.guid), "adguard");
    EF_CHECK(op2.has_value());
    if (!op2) return;
    const auto snap = a.capture(*op2);
    EF_CHECK(snap.ok() && a.apply(*op2).ok());
    EF_CHECK(a.read_back(*op2).value() == std::optional<std::string>(std::string(ag->servers)));
    EF_CHECK(a.restore(*op2, snap.value()).ok() && api.servers.empty() && api.servers6.empty());
    // Karsilastirma satirinin hedefi ayni arayuz hedefidir.
    EF_CHECK(ef::core::readiness::fix_target_for_check("network.dns_bench.fix.{0A1B2C3D-0000-1111-2222-333344445555}") == target);
    EF_CHECK(ef::core::readiness::fix_target_for_check("network.dns_bench.fix.12").empty());
}
