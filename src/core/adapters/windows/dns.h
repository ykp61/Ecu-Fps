#pragma once
// EF-X14: arayuz basina IPv4+IPv6 DNS sunuculari (Get/SetInterfaceDnsSettings; yalniz broker yazar).
//  - Hedef arayuz GUID'i ile tutulur ("dns:{GUID}"): arayuz yeniden numaralansa da baska NIC degismez.
//  - IPv4 ve IPv6 birlikte: "v4|v6", her biri "auto" (otomatik/DHCP) veya virgullu sunucu listesi. Geri alma "auto"yu otomatige dondurur;
//    DHCP'den gelen o anki adresler statik listeye CEVRILMEZ.
//  - Yeni yazma yalniz derlenmis hazir listelerden biri (serbest adres yok).
//  - VPN, etki alani, NRPT veya DoH ilkesi varsa ya da okunamiyorsa degisiklik onerilmez.
//  - Paket isleyici / surucu tabanli erisim modulu bu adaptorun kapsami DEGILDIR (ADD-ADR-02).

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "contracts/models.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kDnsPrefix = "dns:";
inline constexpr std::string_view kDnsAuto = "auto";

struct DnsPreset {
    std::string_view id;
    std::string_view name;     // gorunen ad (marka; cevrilmez)
    std::string_view servers;  // "v4|v6"
};
inline constexpr DnsPreset kDnsPresets[] = {
    {"cloudflare", "Cloudflare", "1.1.1.1,1.0.0.1|2606:4700:4700::1111,2606:4700:4700::1001"},
    {"google", "Google", "8.8.8.8,8.8.4.4|2001:4860:4860::8888,2001:4860:4860::8844"},
    {"quad9", "Quad9", "9.9.9.9,149.112.112.112|2620:fe::fe,2620:fe::9"},
    {"adguard", "AdGuard", "94.140.14.14,94.140.15.15|2a10:50c0::ad1:ff,2a10:50c0::ad2:ff"},  // reklam/izleyici alan adlarini engeller
};
const DnsPreset* find_dns_preset(std::string_view id) noexcept;
bool dns_preset_value(std::string_view servers) noexcept;
// O anki IPv4 ve IPv6 listesi (bos = otomatik) bir hazir listeyle birebir ayniysa o liste; degilse nullptr.
const DnsPreset* dns_current_preset(std::string_view v4, std::string_view v6) noexcept;
// Kullanici secimi -> broker islemi. Kimlik derlenmis listede degilse veya hedef "dns:{GUID}" degilse nullopt.
std::optional<contracts::SettingOperation> dns_preset_operation(std::string_view target, std::string_view preset_id);
// Secim penceresi metni: "Cloudflare - 1.1.1.1, 1.0.0.1; IPv6 2606:..., 2606:..." (gercek adresler).
std::string dns_preset_label(const DnsPreset& p);

// "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" bicimi; aksi halde nullopt.
std::optional<std::string> parse_dns_target(std::string_view target);
std::string dns_target(std::string_view guid);

class DnsApi {
public:
    virtual ~DnsApi() = default;
    // Statik sunucular (bos = otomatik). Arayuz yoksa ErrorCode::MissingValue.
    virtual contracts::Result<std::string> read(const std::string& guid, bool v6) = 0;
    // Bos servers: otomatige dondur.
    virtual contracts::Status write(const std::string& guid, bool v6, const std::string& servers) = 0;
};

struct DnsPolicyFacts;
class DnsAdapter final : public recovery::SettingAdapter {
public:
    // policy: yazma aninda ilke yeniden okunur (tarama sonrasi VPN baglanmis olabilir). Bossa yeni yazma reddedilir.
    DnsAdapter(DnsApi& api, std::function<DnsPolicyFacts()> policy) : api_(api), policy_(std::move(policy)) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    bool target_gone(const contracts::SettingOperation& op) override;

private:
    DnsApi& api_;
    std::function<DnsPolicyFacts()> policy_;
};

// Politika celiskisi: her alan gercekten okunmus ve false olmali. Bilinmeyen = engel.
struct DnsPolicyFacts {
    std::optional<bool> vpn_active, domain_joined, nrpt_rules, doh_policy;
};
enum class DnsPolicy { Allowed, Vpn, Domain, Nrpt, DohPolicy, Unknown };
DnsPolicy dns_policy(const DnsPolicyFacts& f) noexcept;

class FakeDns final : public DnsApi {
public:
    contracts::Result<std::string> read(const std::string& guid, bool v6) override;
    contracts::Status write(const std::string& guid, bool v6, const std::string& servers) override;
    std::string guid = "{00000000-0000-0000-0000-000000000001}";
    std::string servers, servers6;  // bos = otomatik
    bool gone = false;
    int writes = 0;
};

}  // namespace ef::core::adapters::windows
