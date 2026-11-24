#include "core/adapters/amd/amd3d.h"

namespace ef::core::adapters::amd {

using contracts::ErrorCode;
using contracts::Status;
using contracts::SupportState;

std::string_view to_string(Amd3dFeature f) noexcept {
    switch (f) {
        case Amd3dFeature::AntiLag: return "antilag";
        case Amd3dFeature::Chill: return "chill";
        case Amd3dFeature::Boost: return "boost";
    }
    return "?";
}

std::string_view catalog_id(Amd3dFeature f) noexcept {
    switch (f) {
        case Amd3dFeature::AntiLag: return "G03";
        case Amd3dFeature::Chill: return "G08";
        case Amd3dFeature::Boost: return "G09";
    }
    return "?";
}

contracts::Result<Amd3dTarget> parse_amd_target(std::string_view s) {
    using R = contracts::Result<Amd3dTarget>;
    if (s.rfind("adlx:gpu", 0) != 0) return R::fail(ErrorCode::ParseError, "not an adlx target");
    s.remove_prefix(8);
    const auto c = s.find(':');
    if (c == std::string_view::npos || c == 0 || c > 2) return R::fail(ErrorCode::ParseError, "gpu index");
    Amd3dTarget t;
    for (std::size_t i = 0; i < c; ++i) {
        if (s[i] < '0' || s[i] > '9') return R::fail(ErrorCode::ParseError, "gpu index");
        t.gpu = t.gpu * 10 + static_cast<std::uint32_t>(s[i] - '0');
    }
    const auto f = s.substr(c + 1);
    if (f == "antilag") t.feature = Amd3dFeature::AntiLag;
    else if (f == "chill") t.feature = Amd3dFeature::Chill;
    else if (f == "boost") t.feature = Amd3dFeature::Boost;
    else return R::fail(ErrorCode::InvalidEnum, "feature");
    return t;
}

namespace {
std::optional<bool> decode(std::string_view v) {
    if (v == "1") return true;
    if (v == "0") return false;
    return std::nullopt;
}
}  // namespace

contracts::Result<recovery::Snapshot> Amd3dAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto t = parse_amd_target(op.target);
    if (!t.ok()) return R(t.error());
    auto sup = api_.is_supported(t.value());
    if (!sup.ok()) return R(sup.error());
    if (!sup.value()) return R::fail(ErrorCode::Unsupported, std::string(to_string(t.value().feature)) + " not supported on this GPU");
    auto en = api_.is_enabled(t.value());
    if (!en.ok()) return R(en.error());
    recovery::Snapshot s;
    s.existed = true;  // surucu ayari her zaman bir degere sahip
    s.old_value = en.value() ? "1" : "0";
    s.raw_type = "adlx_bool";
    return s;
}

Status Amd3dAdapter::apply(const contracts::SettingOperation& op) {
    auto t = parse_amd_target(op.target);
    if (!t.ok()) return Status(t.error());
    const auto v = decode(op.new_value);
    if (!v) return Status::fail(ErrorCode::ParseError, "value must be 0/1");
    return api_.set_enabled(t.value(), *v);
}

contracts::Result<std::optional<std::string>> Amd3dAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    auto t = parse_amd_target(op.target);
    if (!t.ok()) return R(t.error());
    auto en = api_.is_enabled(t.value());
    if (!en.ok()) return R(en.error());
    return std::optional<std::string>{en.value() ? "1" : "0"};
}

Status Amd3dAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto t = parse_amd_target(op.target);
    if (!t.ok()) return Status(t.error());
    const auto v = decode(snap.old_value);
    if (!v) return Status::fail(ErrorCode::ParseError, "snapshot value");
    return api_.set_enabled(t.value(), *v);
}

contracts::Capability Amd3dAdapter::capability(const Amd3dTarget& t, std::string provider_version) {
    contracts::Capability c;
    c.feature_id = std::string(catalog_id(t.feature));
    c.device_scope = "gpu:" + std::to_string(t.gpu);
    c.provider = "ADLX";
    c.provider_version = std::move(provider_version);
    c.evidence = contracts::EvidenceLevel::Mock;
    auto sup = api_.is_supported(t);
    if (!sup.ok()) {
        c.state = sup.code() == ErrorCode::Denied ? SupportState::Denied : SupportState::Unknown;
        c.reason = sup.error().detail;
    } else if (!sup.value()) {
        c.state = SupportState::Unsupported;
        c.reason = "IsSupported returned false";
    } else {
        c.state = SupportState::Supported;
    }
    return c;
}

contracts::Result<bool> FakeAmd3d::is_supported(const Amd3dTarget& t) {
    const auto it = supported.find(static_cast<int>(t.feature));
    return it != supported.end() && it->second;
}
contracts::Result<bool> FakeAmd3d::is_enabled(const Amd3dTarget& t) { return enabled[static_cast<int>(t.feature)]; }
Status FakeAmd3d::set_enabled(const Amd3dTarget& t, bool on) {
    if (deny_writes) return Status::fail(ErrorCode::Denied, "driver refused setting change");
    if (!supported[static_cast<int>(t.feature)]) return Status::fail(ErrorCode::Unsupported, "not supported");
    enabled[static_cast<int>(t.feature)] = on;
    return contracts::ok_status();
}

contracts::Capability amd_cpu_monitoring_capability() {
    contracts::Capability c;
    c.feature_id = "H06";
    c.device_scope = "cpu";
    c.state = SupportState::Unsupported;
    c.provider = "AMD Ryzen Master Monitoring SDK";
    c.reason = "SDK license not accepted by the product owner (B-006); read-only monitoring only, no PBO/voltage writes";
    return c;
}

}  // namespace ef::core::adapters::amd
