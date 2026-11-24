#include "core/adapters/windows/power_subs.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;

namespace {
Status check_op(const contracts::SettingOperation& op) {
    if (op.id != contracts::OperationId::PowerSettingSet || !is_power_sub_target(op.target))
        return Status::fail(ErrorCode::Denied, "power setting: target not in allowlist");
    return contracts::ok_status();
}

bool parse_uint(std::string_view s, std::uint32_t& out) noexcept {
    if (s.empty() || s.size() > 3) return false;
    std::uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint32_t>(c - '0');
    }
    out = v;
    return true;
}

bool guid_text_ok(std::string_view s) noexcept {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}
}  // namespace

bool is_power_sub_target(std::string_view target) noexcept {
    return target == kPowerBoostAcTarget || target == kPowerParkingAcTarget || target == kPowerAspmAcTarget ||
           target == kPowerUsbSelectiveSuspendAcTarget;
}

bool valid_power_sub_index(std::string_view target, std::uint32_t v) noexcept {
    if (target == kPowerBoostAcTarget) return v <= 6;  // Disabled / Enabled / Aggressive / Efficient*
    if (target == kPowerParkingAcTarget) return v <= 100;
    if (target == kPowerAspmAcTarget) return v <= 2;
    if (target == kPowerUsbSelectiveSuspendAcTarget) return v <= 1;
    return false;
}

bool valid_power_sub_value(std::string_view target, std::string_view v) noexcept {
    std::uint32_t n = 0;
    return parse_uint(v, n) && valid_power_sub_index(target, n);
}

bool admin_power_sub_value_allowed(std::string_view target, std::string_view new_value) noexcept {
    // Katalog hedefi: boost Enabled (1), core parking kapali (100), PCIe ASPM Off (0).
    if (target == kPowerBoostAcTarget) return new_value == "1";
    if (target == kPowerParkingAcTarget) return new_value == "100";
    if (target == kPowerAspmAcTarget) return new_value == "0";
    if (target == kPowerUsbSelectiveSuspendAcTarget) return new_value == "0";  // USB secici askiya alma Disabled
    return false;
}

contracts::Result<recovery::Snapshot> PowerSubAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    auto v = api_.read_active_ac(op.target);
    if (!v.ok()) return R(v.error());
    if (!guid_text_ok(v.value().scheme_guid)) return R::fail(ErrorCode::ParseError, "power scheme guid");
    recovery::Snapshot snap;
    snap.existed = true;
    snap.raw_type = "power_scheme_ac";
    snap.old_value = std::to_string(v.value().ac_index);
    snap.raw = v.value().scheme_guid;  // geri alma ayni plana yazar
    return snap;
}

Status PowerSubAdapter::apply(const contracts::SettingOperation& op) {
    if (auto s = check_op(op); !s.ok()) return s;
    std::uint32_t v = 0;
    if (!parse_uint(op.new_value, v) || !valid_power_sub_index(op.target, v))
        return Status::fail(ErrorCode::OutOfRange, "power setting index out of range");
    return api_.write_active_ac(op.target, v);
}

contracts::Result<std::optional<std::string>> PowerSubAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    auto v = api_.read_active_ac(op.target);
    if (!v.ok()) return R(v.error());
    return std::optional<std::string>{std::to_string(v.value().ac_index)};
}

Status PowerSubAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    if (auto s = check_op(op); !s.ok()) return s;
    std::uint32_t v = 0;
    if (!parse_uint(snap.old_value, v) || !valid_power_sub_index(op.target, v))
        return Status::fail(ErrorCode::ParseError, "power setting snapshot");
    if (!guid_text_ok(snap.raw)) return Status::fail(ErrorCode::ParseError, "power setting snapshot scheme");
    return api_.write_scheme_ac(snap.raw, op.target, v);
}

contracts::Result<std::optional<std::string>> PowerSubAdapter::read_for_restore(const contracts::SettingOperation& op,
                                                                                const recovery::Snapshot& snap) {
    using R = contracts::Result<std::optional<std::string>>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    if (!guid_text_ok(snap.raw)) return read_back(op);  // plan kimligi yoksa (beklenmez) etkin plan okunur
    auto v = api_.read_scheme_ac(snap.raw, op.target);
    if (!v.ok()) return R(v.error());
    return std::optional<std::string>{std::to_string(v.value())};
}

contracts::Result<PowerSubValue> FakePowerSubApi::read_active_ac(std::string_view target) {
    using R = contracts::Result<PowerSubValue>;
    if (fail_read) return R::fail(ErrorCode::Unsupported, "fake read failure");
    const auto s = schemes.find(active);
    if (s == schemes.end()) return R::fail(ErrorCode::Unsupported, "fake scheme not found");
    const auto t = s->second.find(std::string(target));
    if (t == s->second.end()) return R::fail(ErrorCode::Unsupported, "fake setting not found");
    return PowerSubValue{active, t->second};
}

Status FakePowerSubApi::write_active_ac(std::string_view target, std::uint32_t v) {
    ++writes;
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    schemes[active][std::string(target)] = v;
    return contracts::ok_status();
}

Status FakePowerSubApi::write_scheme_ac(const std::string& scheme_guid, std::string_view target, std::uint32_t v) {
    ++writes;
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    schemes[scheme_guid][std::string(target)] = v;
    return contracts::ok_status();
}

contracts::Result<std::uint32_t> FakePowerSubApi::read_scheme_ac(const std::string& scheme_guid, std::string_view target) {
    using R = contracts::Result<std::uint32_t>;
    if (fail_read) return R::fail(ErrorCode::Unsupported, "fake read failure");
    const auto s = schemes.find(scheme_guid);
    if (s == schemes.end()) return R::fail(ErrorCode::Unsupported, "fake scheme not found");
    const auto t = s->second.find(std::string(target));
    if (t == s->second.end()) return R::fail(ErrorCode::Unsupported, "fake setting not found");
    return t->second;
}

}  // namespace ef::core::adapters::windows
