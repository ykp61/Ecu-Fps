#include "core/adapters/startup/startup.h"

#include <cstdio>

namespace ef::core::adapters::startup {

using contracts::ErrorCode;
using contracts::Status;

std::string_view to_string(Location l) noexcept {
    switch (l) {
        case Location::UserRun: return "user_run";
        case Location::MachineRun64: return "machine_run64";
        case Location::MachineRun32: return "machine_run32";
        case Location::UserFolder: return "user_folder";
        case Location::CommonFolder: return "common_folder";
    }
    return "?";
}

std::optional<Location> parse_location(std::string_view s) noexcept {
    for (auto l : {Location::UserRun, Location::MachineRun64, Location::MachineRun32, Location::UserFolder, Location::CommonFolder})
        if (to_string(l) == s) return l;
    return std::nullopt;
}

bool requires_admin(Location l) noexcept { return l != Location::UserRun && l != Location::UserFolder; }

Approval approval_from_raw(std::string_view hex) noexcept {
    if (hex.size() < 2) return Approval::Enabled;
    const char lo = hex[1];
    const int nib = (lo >= '0' && lo <= '9') ? lo - '0' : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : 0;
    return (nib & 1) ? Approval::Disabled : Approval::Enabled;
}

std::string make_raw(Approval a, std::uint64_t ft) {
    std::string s = a == Approval::Disabled ? "03" : "02";
    s += "000000";
    char b[3];
    for (int i = 0; i < 8; ++i) {  // FILETIME little-endian
        const unsigned v = a == Approval::Disabled ? static_cast<unsigned>((ft >> (8 * i)) & 0xFF) : 0u;
        std::snprintf(b, sizeof b, "%02x", v);
        s += b;
    }
    return s;
}

std::string format_target(Location l, std::string_view name) {
    static constexpr char hx[] = "0123456789abcdef";
    std::string h;
    for (unsigned char c : name) {
        h += hx[c >> 4];
        h += hx[c & 15];
    }
    return "startup:" + std::string(to_string(l)) + ":" + h;
}

contracts::Result<Target> parse_target(std::string_view s) {
    using R = contracts::Result<Target>;
    if (s.rfind("startup:", 0) != 0) return R::fail(ErrorCode::ParseError, "startup target");
    s.remove_prefix(8);
    const auto c = s.find(':');
    if (c == std::string_view::npos) return R::fail(ErrorCode::ParseError, "startup target");
    const auto loc = parse_location(s.substr(0, c));
    if (!loc) return R::fail(ErrorCode::InvalidEnum, "startup location");
    const auto hex = s.substr(c + 1);
    if (hex.empty() || hex.size() % 2 || hex.size() > 2 * 255) return R::fail(ErrorCode::ParseError, "startup name");
    std::string name;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto nib = [](char x) -> int {
            if (x >= '0' && x <= '9') return x - '0';
            if (x >= 'a' && x <= 'f') return x - 'a' + 10;
            return -1;
        };
        const int a = nib(hex[i]), b = nib(hex[i + 1]);
        if (a < 0 || b < 0) return R::fail(ErrorCode::ParseError, "startup name hex");
        name += static_cast<char>((a << 4) | b);
    }
    if (name.find('\0') != std::string::npos || name.find('\\') != std::string::npos || name.find('/') != std::string::npos)
        return R::fail(ErrorCode::ParseError, "startup name characters");
    return Target{*loc, name};
}

namespace {
using OptStr = std::optional<std::string>;
}

contracts::Result<recovery::Snapshot> StartupAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto t = parse_target(op.target);
    if (!t.ok()) return R(t.error());
    auto ex = api_.exists(t.value().location, t.value().name);
    if (!ex.ok()) return R(ex.error());
    if (!ex.value()) return R::fail(ErrorCode::MissingValue, "startup entry not found");
    auto raw = api_.get_raw(t.value().location, t.value().name);
    if (!raw.ok()) return R(raw.error());
    recovery::Snapshot s;
    s.existed = raw.value().has_value();
    s.raw_type = "startup_approved";
    if (s.existed) {
        s.raw = *raw.value();
        s.old_value = approval_from_raw(s.raw) == Approval::Disabled ? "disabled" : "enabled";
    }
    return s;
}

Status StartupAdapter::apply(const contracts::SettingOperation& op) {
    auto t = parse_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (op.new_value != "enabled" && op.new_value != "disabled") return Status::fail(ErrorCode::ParseError, "approval value");
    const auto a = op.new_value == "disabled" ? Approval::Disabled : Approval::Enabled;
    return api_.set_raw(t.value().location, t.value().name, make_raw(a, api_.now_filetime()));
}

contracts::Result<OptStr> StartupAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<OptStr>;
    auto t = parse_target(op.target);
    if (!t.ok()) return R(t.error());
    auto raw = api_.get_raw(t.value().location, t.value().name);
    if (!raw.ok()) return R(raw.error());
    if (!raw.value()) return OptStr{};
    return OptStr{approval_from_raw(*raw.value()) == Approval::Disabled ? "disabled" : "enabled"};
}

Status StartupAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto t = parse_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (!snap.existed) return api_.remove_raw(t.value().location, t.value().name);  // onceden hic yoktu
    if (snap.raw.size() != 24) return Status::fail(ErrorCode::ParseError, "snapshot raw");
    return api_.set_raw(t.value().location, t.value().name, snap.raw);  // birebir eski bayt
}

// ---- Fake ----
contracts::Result<std::vector<Entry>> FakeStartupApi::list(Location l) {
    std::vector<Entry> out;
    for (const auto& [k, cmd] : entries) {
        if (k.first != static_cast<int>(l)) continue;
        const auto it = approved.find(k);
        out.push_back({l, k.second, cmd, it == approved.end() ? Approval::Enabled : approval_from_raw(it->second)});
    }
    return out;
}
contracts::Result<bool> FakeStartupApi::exists(Location l, const std::string& n) { return entries.count({static_cast<int>(l), n}) > 0; }
contracts::Result<std::optional<std::string>> FakeStartupApi::get_raw(Location l, const std::string& n) {
    const auto it = approved.find({static_cast<int>(l), n});
    if (it == approved.end()) return std::optional<std::string>{};
    return std::optional<std::string>{it->second};
}
Status FakeStartupApi::set_raw(Location l, const std::string& n, const std::string& raw) {
    if (requires_admin(l) && !elevated) return Status::fail(ErrorCode::Denied, "machine startup needs administrator");
    approved[{static_cast<int>(l), n}] = raw;
    return contracts::ok_status();
}
Status FakeStartupApi::remove_raw(Location l, const std::string& n) {
    if (requires_admin(l) && !elevated) return Status::fail(ErrorCode::Denied, "machine startup needs administrator");
    approved.erase({static_cast<int>(l), n});
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::startup
