#include "core/adapters/windows/power.h"

#include <algorithm>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;
using contracts::SupportState;

bool is_known_power_mode(std::string_view g) noexcept {
    return g == kModeBestEfficiency || g == kModeBalanced || g == kModeBetterPerformance || g == kModeBestPerformance;
}

bool is_guid_text(std::string_view s) noexcept {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

// ---- W01 guc modu (AC ve DC/pil) ----
contracts::Result<std::string> PowerModeAdapter::get_mode(std::string_view target) {
    if (target == kTargetPowerModeAc) return api_.get_ac_mode();
    if (target == kTargetPowerModeDc) return api_.get_dc_mode();
    return contracts::Result<std::string>::fail(ErrorCode::ParseError, "power mode target");
}

Status PowerModeAdapter::set_mode(std::string_view target, const std::string& guid) {
    if (target == kTargetPowerModeAc) return api_.set_ac_mode(guid);
    if (target == kTargetPowerModeDc) return api_.set_dc_mode(guid);
    return Status::fail(ErrorCode::ParseError, "power mode target");
}

contracts::Result<recovery::Snapshot> PowerModeAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto m = get_mode(op.target);
    if (!m.ok()) return R(m.error());
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = m.value();
    s.raw_type = "guid";
    return s;
}

Status PowerModeAdapter::apply(const contracts::SettingOperation& op) {
    if (op.target != kTargetPowerModeAc && op.target != kTargetPowerModeDc)
        return Status::fail(ErrorCode::ParseError, "power mode target");
    if (!is_known_power_mode(op.new_value)) return Status::fail(ErrorCode::Denied, "unknown power mode GUID");
    return set_mode(op.target, op.new_value);
}

contracts::Result<std::optional<std::string>> PowerModeAdapter::read_back(const contracts::SettingOperation& op) {
    auto m = get_mode(op.target);
    if (!m.ok()) return contracts::Result<std::optional<std::string>>(m.error());
    return std::optional<std::string>{m.value()};
}

Status PowerModeAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    if (!is_known_power_mode(snap.old_value)) return Status::fail(ErrorCode::ParseError, "snapshot power mode");
    return set_mode(op.target, snap.old_value);
}

contracts::Capability PowerModeAdapter::capability() {
    contracts::Capability c{"W01", "system", SupportState::Unknown, "PowerSetUserConfiguredACPowerMode", "", "", contracts::Privilege::User,
                            contracts::EvidenceLevel::Unit};
    auto m = api_.get_ac_mode();
    if (!m.ok()) {
        c.state = m.code() == ErrorCode::Unsupported ? SupportState::Unsupported : SupportState::Unknown;
        c.reason = m.error().detail;
        return c;
    }
    auto sch = api_.get_active_scheme();
    if (sch.ok() && sch.value() != kSchemeBalanced) {
        c.state = SupportState::Unsupported;
        c.reason = "power mode applies only while the Balanced plan is active";
        return c;
    }
    c.state = SupportState::Supported;
    return c;
}

// ---- W02 etkin plan ----
contracts::Result<recovery::Snapshot> PowerSchemeAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (op.target != "power:scheme:active") return R::fail(ErrorCode::ParseError, "power scheme target");
    auto s0 = api_.get_active_scheme();
    if (!s0.ok()) return R(s0.error());
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = s0.value();
    s.raw_type = "guid";
    return s;
}

Status PowerSchemeAdapter::apply(const contracts::SettingOperation& op) {
    if (op.target != "power:scheme:active") return Status::fail(ErrorCode::ParseError, "power scheme target");
    if (!is_guid_text(op.new_value)) return Status::fail(ErrorCode::ParseError, "scheme GUID");
    auto list = api_.list_schemes();
    if (!list.ok()) return Status(list.error());
    // Plan olusturulmaz: yalniz mevcut planlardan birine gecilir.
    if (std::find(list.value().begin(), list.value().end(), op.new_value) == list.value().end())
        return Status::fail(ErrorCode::Unsupported, "power plan does not exist on this system");
    return api_.set_active_scheme(op.new_value);
}

contracts::Result<std::optional<std::string>> PowerSchemeAdapter::read_back(const contracts::SettingOperation&) {
    auto s = api_.get_active_scheme();
    if (!s.ok()) return contracts::Result<std::optional<std::string>>(s.error());
    return std::optional<std::string>{s.value()};
}

Status PowerSchemeAdapter::restore(const contracts::SettingOperation&, const recovery::Snapshot& snap) {
    if (!is_guid_text(snap.old_value)) return Status::fail(ErrorCode::ParseError, "snapshot scheme");
    return api_.set_active_scheme(snap.old_value);
}

contracts::Capability PowerSchemeAdapter::capability() {
    contracts::Capability c{"W02", "system", SupportState::Unknown, "PowerSetActiveScheme", "", "", contracts::Privilege::User,
                            contracts::EvidenceLevel::Unit};
    auto s = api_.get_active_scheme();
    auto l = api_.list_schemes();
    if (!s.ok() || !l.ok()) {
        c.reason = !s.ok() ? s.error().detail : l.error().detail;
        c.state = (!s.ok() ? s.code() : l.code()) == ErrorCode::Denied ? SupportState::Denied : SupportState::Unknown;
        return c;
    }
    c.state = l.value().size() > 1 ? SupportState::Supported : SupportState::Unsupported;
    if (c.state == SupportState::Unsupported) c.reason = "only one power plan available (modern standby / OEM restriction)";
    return c;
}

// ---- Fake ----
contracts::Result<std::string> FakePowerApi::get_ac_mode() {
    if (!modes_supported) return contracts::Result<std::string>::fail(ErrorCode::Unsupported, "power modes need Windows 11");
    return mode;
}
Status FakePowerApi::set_ac_mode(const std::string& g) {
    if (!modes_supported) return Status::fail(ErrorCode::Unsupported, "power modes need Windows 11");
    mode = g;
    return contracts::ok_status();
}
contracts::Result<std::string> FakePowerApi::get_dc_mode() {
    if (!modes_supported || !dc_supported)
        return contracts::Result<std::string>::fail(ErrorCode::Unsupported, "DC power mode not available");
    if (dc_read_error) return contracts::Result<std::string>::fail(*dc_read_error, "DC power mode read failed");
    return dc_mode;
}
Status FakePowerApi::set_dc_mode(const std::string& g) {
    if (!modes_supported || !dc_supported) return Status::fail(ErrorCode::Unsupported, "DC power mode not available");
    dc_mode = g;
    return contracts::ok_status();
}
contracts::Result<bool> FakePowerApi::has_battery() {
    if (!battery_readable) return contracts::Result<bool>::fail(ErrorCode::IoError, "battery status unreadable");
    return battery;
}
Status FakePowerApi::set_active_scheme(const std::string& g) {
    if (std::find(schemes.begin(), schemes.end(), g) == schemes.end()) return Status::fail(ErrorCode::MissingValue, "no such scheme");
    scheme = g;
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
