#include "core/adapters/intel/igcl3d.h"

namespace ef::core::adapters::intel {

using contracts::ErrorCode;
using contracts::Status;
using contracts::SupportState;

std::string_view to_string(Igcl3dFeature f) noexcept {
    switch (f) {
        case Igcl3dFeature::LowLatency: return "low_latency";
        case Igcl3dFeature::EnduranceGaming: return "endurance_gaming";
    }
    return "?";
}

contracts::Result<Igcl3dTarget> parse_igcl_target(std::string_view s) {
    using R = contracts::Result<Igcl3dTarget>;
    if (s.rfind("igcl:gpu", 0) != 0) return R::fail(ErrorCode::ParseError, "not an igcl target");
    s.remove_prefix(8);
    const auto c = s.find(':');
    if (c == std::string_view::npos || c == 0 || c > 2) return R::fail(ErrorCode::ParseError, "gpu index");
    Igcl3dTarget t;
    for (std::size_t i = 0; i < c; ++i) {
        if (s[i] < '0' || s[i] > '9') return R::fail(ErrorCode::ParseError, "gpu index");
        t.gpu = t.gpu * 10 + static_cast<std::uint32_t>(s[i] - '0');
    }
    const auto f = s.substr(c + 1);
    if (f == "low_latency") t.feature = Igcl3dFeature::LowLatency;
    else if (f == "endurance_gaming") t.feature = Igcl3dFeature::EnduranceGaming;
    else return R::fail(ErrorCode::InvalidEnum, "feature");
    return t;
}

Status igcl_status(IgclResult r, std::string_view what) {
    switch (r) {
        case IgclResult::Success: return contracts::ok_status();
        case IgclResult::Unsupported: return Status::fail(ErrorCode::Unsupported, std::string(what) + ": unsupported");
        case IgclResult::InsufficientPermissions: return Status::fail(ErrorCode::Denied, std::string(what) + ": insufficient permissions");
        case IgclResult::DeviceLost: return Status::fail(ErrorCode::IoError, std::string(what) + ": device lost");
        case IgclResult::Other: break;
    }
    return Status::fail(ErrorCode::IoError, std::string(what) + ": driver error");
}

namespace {
std::optional<bool> decode(std::string_view v) {
    if (v == "1") return true;
    if (v == "0") return false;
    return std::nullopt;
}
}  // namespace

contracts::Result<recovery::Snapshot> Igcl3dAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto t = parse_igcl_target(op.target);
    if (!t.ok()) return R(t.error());
    bool sup = false;
    if (auto st = igcl_status(api_.is_supported(t.value(), sup), "is_supported"); !st.ok()) return R(st.error());
    if (!sup) return R::fail(ErrorCode::Unsupported, std::string(to_string(t.value().feature)) + " not supported on this GPU");
    bool on = false;
    if (auto st = igcl_status(api_.is_enabled(t.value(), on), "is_enabled"); !st.ok()) return R(st.error());
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = on ? "1" : "0";
    s.raw_type = "igcl_bool";
    return s;
}

Status Igcl3dAdapter::apply(const contracts::SettingOperation& op) {
    auto t = parse_igcl_target(op.target);
    if (!t.ok()) return Status(t.error());
    const auto v = decode(op.new_value);
    if (!v) return Status::fail(ErrorCode::ParseError, "value must be 0/1");
    return igcl_status(api_.set_enabled(t.value(), *v), "set_enabled");
}

contracts::Result<std::optional<std::string>> Igcl3dAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    auto t = parse_igcl_target(op.target);
    if (!t.ok()) return R(t.error());
    bool on = false;
    if (auto st = igcl_status(api_.is_enabled(t.value(), on), "is_enabled"); !st.ok()) return R(st.error());
    return std::optional<std::string>{on ? "1" : "0"};
}

Status Igcl3dAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto t = parse_igcl_target(op.target);
    if (!t.ok()) return Status(t.error());
    const auto v = decode(snap.old_value);
    if (!v) return Status::fail(ErrorCode::ParseError, "snapshot value");
    return igcl_status(api_.set_enabled(t.value(), *v), "set_enabled");
}

contracts::Capability Igcl3dAdapter::capability(const Igcl3dTarget& t, std::string provider_version) {
    contracts::Capability c;
    c.feature_id = "igcl." + std::string(to_string(t.feature));
    c.device_scope = "gpu:" + std::to_string(t.gpu);
    c.provider = "IGCL";
    c.provider_version = std::move(provider_version);
    c.evidence = contracts::EvidenceLevel::Mock;  // gercek Intel GPU'da dogrulanmadi (B-004)
    bool sup = false;
    const auto r = api_.is_supported(t, sup);
    if (r == IgclResult::InsufficientPermissions) c.state = SupportState::Denied, c.reason = "insufficient permissions";
    else if (r == IgclResult::Unsupported || (r == IgclResult::Success && !sup)) c.state = SupportState::Unsupported, c.reason = "reported unsupported";
    else if (r != IgclResult::Success) c.state = SupportState::Unknown, c.reason = "support query failed";
    else c.state = SupportState::Supported;
    return c;
}

IgclResult FakeIgcl3d::is_supported(const Igcl3dTarget& t, bool& out) {
    if (support_result != IgclResult::Success) return support_result;
    const auto it = supported.find(static_cast<int>(t.feature));
    out = it != supported.end() && it->second;
    return IgclResult::Success;
}
IgclResult FakeIgcl3d::is_enabled(const Igcl3dTarget& t, bool& out) {
    out = enabled[static_cast<int>(t.feature)];
    return IgclResult::Success;
}
IgclResult FakeIgcl3d::set_enabled(const Igcl3dTarget& t, bool on) {
    ++writes;
    if (write_result != IgclResult::Success) return write_result;
    enabled[static_cast<int>(t.feature)] = on;
    return IgclResult::Success;
}

}  // namespace ef::core::adapters::intel
