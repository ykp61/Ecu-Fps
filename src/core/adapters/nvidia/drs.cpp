#include "core/adapters/nvidia/drs.h"

#include <cstdio>

namespace ef::core::adapters::nvidia {

using contracts::ErrorCode;
using contracts::Status;

bool is_allowed_setting(std::uint32_t id) noexcept {
    switch (static_cast<DrsSetting>(id)) {
        case DrsSetting::PowerMode:
        case DrsSetting::MaxPrerendered:
        case DrsSetting::VSync:
        case DrsSetting::FrameRateLimit:
        case DrsSetting::ShaderCacheSize:
        case DrsSetting::ThreadedOptimization:
        case DrsSetting::FxaaEnable: return true;
    }
    return false;
}

std::string_view catalog_id(DrsSetting s) noexcept {
    switch (s) {
        case DrsSetting::PowerMode: return "G01";
        case DrsSetting::MaxPrerendered: return "G02";
        case DrsSetting::VSync: return "G05";
        case DrsSetting::FrameRateLimit: return "G06";
        case DrsSetting::ShaderCacheSize: return "G07";
        case DrsSetting::ThreadedOptimization: return "G13";
        case DrsSetting::FxaaEnable: return "G14";
    }
    return "?";
}

std::string_view to_string(DrsLocation l) noexcept {
    switch (l) {
        case DrsLocation::CurrentProfile: return "current_profile";
        case DrsLocation::Global: return "global";
        case DrsLocation::Base: return "base";
        case DrsLocation::Default: return "default";
    }
    return "?";
}

namespace {
std::optional<std::uint32_t> hex32(std::string_view s) {
    if (s.empty() || s.size() > 8) return std::nullopt;
    std::uint32_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
        else return std::nullopt;
    }
    return v;
}
bool valid_exe(std::string_view s) {
    if (s.size() < 5 || s.size() > 64 || s.substr(s.size() - 4) != ".exe") return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return s.find("..") == std::string_view::npos;
}
}  // namespace

contracts::Result<DrsTarget> parse_drs_target(std::string_view t) {
    using R = contracts::Result<DrsTarget>;
    if (t.rfind("drs:", 0) != 0) return R::fail(ErrorCode::ParseError, "not a drs target");
    t.remove_prefix(4);
    DrsTarget out;
    if (t.rfind("global:", 0) == 0) {
        t.remove_prefix(7);
    } else if (t.rfind("app:", 0) == 0) {
        t.remove_prefix(4);
        const auto c = t.rfind(':');
        if (c == std::string_view::npos) return R::fail(ErrorCode::ParseError, "app target");
        if (!valid_exe(t.substr(0, c))) return R::fail(ErrorCode::ParseError, "app exe name");
        out.global = false;
        out.app = std::string(t.substr(0, c));
        t.remove_prefix(c + 1);
    } else {
        return R::fail(ErrorCode::ParseError, "drs scope");
    }
    const auto id = hex32(t);
    if (!id) return R::fail(ErrorCode::ParseError, "setting id");
    if (!is_allowed_setting(*id)) return R::fail(ErrorCode::Denied, "setting not in allowlist");
    out.setting = *id;
    return out;
}

std::string format_drs_target(const DrsTarget& t) {
    char id[16];
    std::snprintf(id, sizeof id, "%08X", t.setting);
    return t.global ? "drs:global:" + std::string(id) : "drs:app:" + t.app + ":" + id;
}

std::string DrsAdapter::encode(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}
std::optional<std::uint32_t> DrsAdapter::decode(std::string_view s) { return s.size() == 8 ? hex32(s) : std::nullopt; }

contracts::Result<recovery::Snapshot> DrsAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto t = parse_drs_target(op.target);
    if (!t.ok()) return R(t.error());
    auto v = api_.get(t.value());
    if (!v.ok()) return R(v.error());
    recovery::Snapshot s;
    s.existed = v.value().location == DrsLocation::CurrentProfile;
    s.inherited = !s.existed;
    s.old_value = s.existed ? encode(v.value().value) : std::string();
    s.raw_type = "drs_dword";
    s.source_version = std::string(to_string(v.value().location)) + "=" + encode(v.value().value);  // bilgi: miras edilen etkin deger
    return s;
}

Status DrsAdapter::apply(const contracts::SettingOperation& op) {
    auto t = parse_drs_target(op.target);
    if (!t.ok()) return Status(t.error());
    const auto v = decode(op.new_value);
    if (!v) return Status::fail(ErrorCode::ParseError, "drs value");
    if (auto s = api_.set(t.value(), *v); !s.ok()) return s;
    return api_.save();
}

contracts::Result<std::optional<std::string>> DrsAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    auto t = parse_drs_target(op.target);
    if (!t.ok()) return R(t.error());
    auto v = api_.get(t.value());
    if (!v.ok()) return R(v.error());
    if (v.value().location != DrsLocation::CurrentProfile) return std::optional<std::string>{};
    return std::optional<std::string>{encode(v.value().value)};
}

Status DrsAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto t = parse_drs_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (snap.existed) {
        const auto v = decode(snap.old_value);
        if (!v) return Status::fail(ErrorCode::ParseError, "snapshot value");
        if (auto s = api_.set(t.value(), *v); !s.ok()) return s;
    } else {
        if (auto s = api_.remove(t.value()); !s.ok()) return s;  // varsayilan YAZILMAZ; miras geri gelir
    }
    return api_.save();
}

// ---- FakeDrs: uygulama -> global -> varsayilan miras zinciri ----
contracts::Result<DrsValue> FakeDrs::get(const DrsTarget& t) {
    if (!t.global) {
        const auto app = apps.find(t.app);
        if (app == apps.end()) return contracts::Result<DrsValue>::fail(ErrorCode::MissingValue, "application profile not found");
        if (const auto it = app->second.find(t.setting); it != app->second.end()) return DrsValue{it->second, DrsLocation::CurrentProfile};
        if (const auto g = global.find(t.setting); g != global.end()) return DrsValue{g->second, DrsLocation::Global};
    } else {
        if (const auto g = global.find(t.setting); g != global.end()) return DrsValue{g->second, DrsLocation::CurrentProfile};
    }
    const auto d = defaults.find(t.setting);
    return DrsValue{d == defaults.end() ? 0u : d->second, DrsLocation::Default};
}

Status FakeDrs::set(const DrsTarget& t, std::uint32_t value) {
    if (t.global) global[t.setting] = value;
    else {
        const auto app = apps.find(t.app);
        if (app == apps.end()) return Status::fail(ErrorCode::MissingValue, "application profile not found");
        app->second[t.setting] = value;
    }
    return contracts::ok_status();
}

Status FakeDrs::remove(const DrsTarget& t) {
    if (t.global) global.erase(t.setting);
    else if (const auto app = apps.find(t.app); app != apps.end()) app->second.erase(t.setting);
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::nvidia
