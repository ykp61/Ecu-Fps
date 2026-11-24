#include "app/readiness_collect.h"

#include <windows.h>

#include <algorithm>
#include <ctime>

#include "core/diagnostics/incident_engine.h"
#include "platform/windows/diagnostics/event_reader.h"
#include "core/adapters/windows/dns.h"
#include "platform/windows/adapters/win_dns.h"
#include "core/readiness/platform_security.h"
#include "platform/windows/readiness/background_reader.h"
#include "platform/windows/readiness/security_reader.h"
#include "platform/windows/readiness/cpu_reader.h"
#include "platform/windows/readiness/display_reader.h"
#include "platform/windows/readiness/driver_health_reader.h"
#include "platform/windows/readiness/graphics_reader.h"
#include "platform/windows/readiness/interrupt_reader.h"
#include "platform/windows/readiness/memory_reader.h"
#include "platform/windows/readiness/network_reader.h"
#include "platform/windows/net/dns_query.h"
#include "platform/windows/readiness/storage_reader.h"
#include "platform/windows/readiness/system_reader.h"
#include "platform/windows/readiness/tweaks_reader.h"
#include "platform/windows/inventory/problem_devices_reader.h"
#include "platform/windows/readiness/virtualization_reader.h"

namespace ef::app {

namespace {
void append(std::vector<core::readiness::Check>& out, std::vector<core::readiness::Check> v) {
    for (auto& c : v) out.push_back(std::move(c));
}
}  // namespace

std::vector<core::readiness::Check> collect_readiness_checks(const std::vector<core::readiness::PingStats>& pings, int incident_days,
                                                             const std::vector<std::string>& game_exe_paths,
                                                             core::readiness::GpuPrefInfo* gpu_pref_out) {
    namespace pr = platform::readiness;
    namespace cr = core::readiness;
    std::vector<cr::Check> out;
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    const auto displays = pr::read_displays();
    const auto gaming = pr::read_windows_gaming();
    append(out, cr::display_checks(displays));
    append(out, cr::windows_checks(gaming));
    append(out, cr::frame_sync_checks(displays, gaming));  // F-05 salt okuma danismani
    {
        auto gpu_pref = pr::read_gpu_pref_info(game_exe_paths);  // P6 salt okuma (Windows Grafik tercihi)
        append(out, cr::gpu_preference_checks(gpu_pref));
        if (gpu_pref_out) *gpu_pref_out = std::move(gpu_pref);
    }
    append(out, cr::cpu_hybrid_checks(pr::read_cpu_hybrid()));  // F-41 salt okuma hibrit raporu
    append(out, cr::cpu_power_checks(pr::read_cpu_power()));    // F-34 CPUID destek raporu (yazma yok)
    append(out, cr::cpu_speed_limit_checks(pr::read_cpu_speed_limit()));  // spec 2026-11-12 C1 salt okuma
    append(out, cr::virtualization_checks(pr::read_virtualization()));  // F-50 salt okuma isaretleri
    auto g = pr::read_graphics();  // D3D12 cihazi + NVAPI: UI is parcacigi disinda (cagiran arka planda)
    g.now_unix = now;
    append(out, cr::graphics_checks(g));
    auto s = pr::read_software();
    s.now_unix = now;
    append(out, cr::software_checks(s));
    const auto net = pr::read_network(1000);  // paket gondermez; arka plan alma hizi 1 sn ornek
    append(out, cr::network_checks(net.info, pings));
    {   // EF-X13: islettigimiz test sunucusu yok: yuk testi calismaz, durum acikca yazilir.
        cr::Check c;
        c.id = "network.load_test";
        c.section = cr::Section::Network;
        c.level = cr::Level::Info;
        c.text_key = "chk.network.load_blocked";
        out.push_back(std::move(c));
    }
    if (!net.interface_guid.empty()) {  // EF-X14: etkin arayuzun DNS'i (salt okuma); degisiklik ancak ilke engeli yoksa onerilir
        namespace aw = core::adapters::windows;
        platform::adapters::WinDnsApi dns;
        const auto cur = dns.read(net.interface_guid, false);
        const auto cur6 = dns.read(net.interface_guid, true);
        cr::Check c;
        c.id = "network.dns." + net.interface_guid;
        c.section = cr::Section::Network;
        c.level = cr::Level::Info;
        c.args["adapter"] = net.adapter_name;
        if (!cur.ok() && cur.code() == contracts::ErrorCode::Unsupported) {
            c.text_key = "chk.network.dns_unsupported";  // desteklenmiyor: "okunamadi" ile ayni degil
        } else if (!cur.ok() || !cur6.ok()) {
            c.level = cr::Level::Unknown;
            c.text_key = "chk.network.dns_unknown";
        } else {
            c.text_key = cur.value().empty() ? "chk.network.dns_auto" : "chk.network.dns_manual";
            c.args["servers"] = cur.value();
            const auto policy = aw::dns_policy(platform::adapters::read_dns_policy_facts());
            if (policy != aw::DnsPolicy::Allowed) {
                c.text_key = policy == aw::DnsPolicy::Unknown ? "chk.network.dns_policy_unknown" : "chk.network.dns_policy";
            } else if (const auto* on = aw::dns_current_preset(cur.value(), cur6.value())) {
                c.text_key = "chk.network.dns_preset";  // zaten hazir listelerden birinde: degisiklik onerilmez
                c.args["preset"] = std::string(on->name);
            } else {
                c.action = cr::ActionKind::AutoFix;
                c.action_arg = "fix.dns";  // kullanici Evet/Hayir penceresinde hazir listeden secer; otomatik uygulanmaz
            }
        }
        out.push_back(std::move(c));
    }
    append(out, cr::system_health_checks(pr::read_system_health()));  // F-65/F-67 salt okuma
    append(out, cr::storage_health_checks(pr::read_storage_health()));  // F-81 salt okuma
    {
        // EF-X01 salt okuma; tarih kullanicinin yerel gunu ile yazilir.
        TIME_ZONE_INFORMATION tz{};
        const DWORD tzid = GetTimeZoneInformation(&tz);
        const long bias = tz.Bias + (tzid == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tzid == TIME_ZONE_ID_STANDARD ? tz.StandardBias : 0);
        const int days = core::diagnostics::normalize_incident_days(incident_days);
        const auto rep = core::diagnostics::analyze_incidents(platform::diagnostics::read_system_incidents(days), days, now * 1000);
        auto rows = core::diagnostics::incident_checks(rep, static_cast<int>(-bias));
        core::diagnostics::add_incident_window_action(rows, rep);
        append(out, std::move(rows));
    }
    {
        cr::Check rp;  // EF-X16: istege bagli; ayar kaydinin yerine gecmez
        rp.id = "windows.restore_point";
        rp.section = cr::Section::Windows;
        rp.level = cr::Level::Info;
        rp.text_key = "chk.rp.offer";
        rp.action = cr::ActionKind::AutoFix;
        rp.action_arg = "fix.restore_point";
        out.push_back(std::move(rp));
        cr::Check im;  // EF-X02: istege bagli goruntu kontrolu; onarim yalniz kontrol "onarilabilir" derse ayrica sorulur
        im.id = "windows.image_health";
        im.section = cr::Section::Windows;
        im.level = cr::Level::Info;
        im.text_key = "chk.maint.offer";
        im.action = cr::ActionKind::AutoFix;
        im.action_arg = "fix.image_check";
        out.push_back(std::move(im));
    }
    append(out, cr::platform_security_checks(pr::read_platform_security()));  // EF-X09 salt okuma
    {   // EF-X10: MPO yalniz ekran surucusu sifirlama kaydi varsa anlatilir
        const bool symptom = std::any_of(out.begin(), out.end(), [](const cr::Check& c) { return c.id == "hardware.incidents.display_reset"; });
        const auto mpo = pr::read_mpo_state();
        append(out, cr::mpo_checks(mpo.overlay_test_mode, symptom, mpo.read_ok));
    }
    append(out, cr::memory_layout_checks(pr::read_memory_layout()));  // F-62 salt okuma
    append(out, cr::standby_pressure_checks(pr::read_memory_pressure()));  // B5 / ADR-040 m.6 salt okuma; Duzelt = elle bakim isi
    cr::drop_superseded_standby_info(out);
    append(out, cr::interrupt_checks(pr::read_interrupt_devices()));  // B5 / ADR-040 m.5: MSI durumu YALNIZ okunur
    append(out, cr::system_tweak_checks(pr::read_system_tweaks()));   // F-52 salt okuma
    append(out, cr::background_stack_checks(
                    cr::classify_background_stack(pr::read_running_exe_names())));  // F-79 salt okuma
    append(out, cr::display_driver_checks(pr::read_display_driver_health()));  // F-68 salt okuma
    append(out, cr::problem_device_checks(platform::inventory::read_problem_devices()));  // T17 salt okuma
    cr::group_latency_checks(out);  // EF-X25
    return out;
}

std::vector<core::readiness::PingStats> run_network_test(const std::atomic<bool>& cancel) {
    namespace pr = platform::readiness;
    const auto net = pr::read_network(0);
    const auto targets = pr::default_ping_targets(net);
    const auto runs = pr::run_ping_test(targets, 30, 200, &cancel);  // yalniz kullanici dugmesiyle: ICMP yanki
    return pr::ping_test_stats(targets, runs);
}

std::optional<core::net::DnsBenchReport> run_dns_compare(const std::atomic<bool>& cancel) {
    namespace pn = platform::net;
    namespace aw = core::adapters::windows;
    const auto net = platform::readiness::read_network(0);
    const auto sys = pn::system_dns_ipv4(net.interface_guid);
    core::net::DnsBenchReport rep;
    std::vector<pn::BenchResolver> resolvers;
    if (!sys.empty()) {
        resolvers.push_back({std::string(core::net::kDnsBenchCurrentId), sys.front()});
        rep.current_name = sys.front();
        rep.current_modem = core::net::is_private_ipv4(sys.front());
    }
    for (const auto& p : aw::kDnsPresets) {
        const std::string_view v4 = p.servers.substr(0, p.servers.find('|'));
        bool same_as_current = false;  // sistem zaten bu listeyi kullaniyor: ayni cozucu iki kez olculmez
        for (const auto& s : sys) {
            for (std::size_t from = 0; from <= v4.size();) {
                const auto comma = v4.find(',', from);
                const auto item = v4.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from);
                if (item == s) same_as_current = true;
                if (comma == std::string_view::npos) break;
                from = comma + 1;
            }
        }
        if (same_as_current) {
            rep.current_name = std::string(p.name);
            rep.current_modem = false;
            continue;
        }
        resolvers.push_back({std::string(p.id), std::string(v4.substr(0, v4.find(',')))});  // her listenin ilk (birincil) IPv4 sunucusu
    }
    if (resolvers.empty()) return std::nullopt;
    std::vector<core::net::ResolverSamples> samples;
    if (!pn::run_dns_bench(resolvers, cancel, samples)) return std::nullopt;
    rep.stats = core::net::summarize(samples);
    rep.verdict = core::net::verdict(rep.stats);
    return rep;
}

}  // namespace ef::app
