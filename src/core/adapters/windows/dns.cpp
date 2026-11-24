#include "core/adapters/windows/dns.h"

#include <cctype>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Result;
using contracts::Status;

namespace {

std::string one(const std::string& s) { return s.empty() ? std::string(kDnsAuto) : s; }
std::string decode(std::string_view v) { return v == kDnsAuto ? std::string() : std::string(v); }
// "v4|v6" -> iki parca; ayirac yoksa bicim hatasi.
std::optional<std::pair<std::string, std::string>> split(std::string_view v) {
    const auto p = v.find('|');
    if (p == std::string_view::npos || v.find('|', p + 1) != std::string_view::npos) return std::nullopt;
    return std::make_pair(decode(v.substr(0, p)), decode(v.substr(p + 1)));
}
Result<std::string> read_both(DnsApi& api, const std::string& g) {
    const auto a = api.read(g, false);
    if (!a.ok()) return a;
    const auto b = api.read(g, true);
    if (!b.ok()) return b;
    return one(a.value()) + "|" + one(b.value());
}

}  // namespace

const DnsPreset* find_dns_preset(std::string_view id) noexcept {
    for (const auto& p : kDnsPresets)
        if (p.id == id) return &p;
    return nullptr;
}

bool dns_preset_value(std::string_view servers) noexcept {
    for (const auto& p : kDnsPresets)
        if (p.servers == servers) return true;
    return false;
}

const DnsPreset* dns_current_preset(std::string_view v4, std::string_view v6) noexcept {
    if (v4.empty() || v6.empty()) return nullptr;  // bir aile otomatikse hazir liste sayilmaz
    for (const auto& p : kDnsPresets) {
        const auto bar = p.servers.find('|');
        if (p.servers.substr(0, bar) == v4 && p.servers.substr(bar + 1) == v6) return &p;
    }
    return nullptr;
}

std::optional<contracts::SettingOperation> dns_preset_operation(std::string_view target, std::string_view preset_id) {
    const auto* p = find_dns_preset(preset_id);
    if (!p || !parse_dns_target(target)) return std::nullopt;
    return contracts::SettingOperation{contracts::OperationId::DnsServersSet, contracts::OperationScope::Machine,
                                       std::string(target), std::string(p->servers), false, false};
}

std::string dns_preset_label(const DnsPreset& p) {
    auto list = [](std::string_view v) {
        std::string s(v);
        for (std::size_t i = 0; (i = s.find(',', i)) != std::string::npos; i += 2) s.insert(i + 1, " ");
        return s;
    };
    const auto bar = p.servers.find('|');
    return std::string(p.name) + " - " + list(p.servers.substr(0, bar)) + "; IPv6 " + list(p.servers.substr(bar + 1));
}

std::optional<std::string> parse_dns_target(std::string_view target) {
    if (target.substr(0, kDnsPrefix.size()) != kDnsPrefix) return std::nullopt;
    const auto g = target.substr(kDnsPrefix.size());
    if (g.size() != 38 || g.front() != '{' || g.back() != '}') return std::nullopt;
    for (std::size_t i = 1; i < 37; ++i) {
        const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? g[i] != '-' : !std::isxdigit(static_cast<unsigned char>(g[i]))) return std::nullopt;
    }
    std::string out(g);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string dns_target(std::string_view guid) { return std::string(kDnsPrefix) + std::string(guid); }

Result<recovery::Snapshot> DnsAdapter::capture(const contracts::SettingOperation& op) {
    const auto g = parse_dns_target(op.target);
    if (!g) return Result<recovery::Snapshot>::fail(ErrorCode::Denied, "dns target");
    const auto cur = read_both(api_, *g);
    if (!cur.ok()) return Result<recovery::Snapshot>::fail(cur.code(), cur.error().detail);
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = cur.value();
    s.raw_type = "dns_v4_v6";
    return s;
}

Status DnsAdapter::apply(const contracts::SettingOperation& op) {
    const auto g = parse_dns_target(op.target);
    if (!g || !dns_preset_value(op.new_value)) return Status::fail(ErrorCode::Denied, "dns value");
    if (!policy_ || dns_policy(policy_()) != DnsPolicy::Allowed) return Status::fail(ErrorCode::Denied, "dns policy conflict");
    const auto both = split(op.new_value);
    if (!both) return Status::fail(ErrorCode::ParseError, "dns value");
    if (auto s = api_.write(*g, false, both->first); !s.ok()) return s;
    return api_.write(*g, true, both->second);
}

Result<std::optional<std::string>> DnsAdapter::read_back(const contracts::SettingOperation& op) {
    const auto g = parse_dns_target(op.target);
    if (!g) return Result<std::optional<std::string>>::fail(ErrorCode::Denied, "dns target");
    const auto cur = read_both(api_, *g);
    if (!cur.ok()) return Result<std::optional<std::string>>::fail(cur.code(), cur.error().detail);
    return std::optional<std::string>(cur.value());
}

Status DnsAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    const auto g = parse_dns_target(op.target);
    if (!g) return Status::fail(ErrorCode::Denied, "dns target");
    const auto both = split(snap.old_value);
    if (!both) return Status::fail(ErrorCode::ParseError, "dns snapshot");
    if (auto s = api_.write(*g, false, both->first); !s.ok()) return s;  // "auto" -> otomatik; statik listeye cevrilmez
    return api_.write(*g, true, both->second);
}

bool DnsAdapter::target_gone(const contracts::SettingOperation& op) {
    const auto g = parse_dns_target(op.target);
    if (!g) return false;
    const auto cur = api_.read(*g, false);
    return !cur.ok() && cur.code() == ErrorCode::MissingValue;
}

DnsPolicy dns_policy(const DnsPolicyFacts& f) noexcept {
    if (!f.vpn_active || !f.domain_joined || !f.nrpt_rules || !f.doh_policy) return DnsPolicy::Unknown;
    if (*f.vpn_active) return DnsPolicy::Vpn;
    if (*f.domain_joined) return DnsPolicy::Domain;
    if (*f.nrpt_rules) return DnsPolicy::Nrpt;
    if (*f.doh_policy) return DnsPolicy::DohPolicy;
    return DnsPolicy::Allowed;
}

Result<std::string> FakeDns::read(const std::string& g, bool v6) {
    if (gone || g != guid) return Result<std::string>::fail(ErrorCode::MissingValue, "interface not found");
    return v6 ? servers6 : servers;
}

Status FakeDns::write(const std::string& g, bool v6, const std::string& s) {
    if (gone || g != guid) return Status::fail(ErrorCode::MissingValue, "interface not found");
    ++writes;
    (v6 ? servers6 : servers) = s;
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
