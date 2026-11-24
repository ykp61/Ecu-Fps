#include "core/adapters/windows/fast_startup.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;

namespace {
Status check_op(const contracts::SettingOperation& op) {
    if (op.id != contracts::OperationId::FastStartupSet || op.target != kFastStartupTarget)
        return Status::fail(ErrorCode::Denied, "fast startup: target not in allowlist");
    return contracts::ok_status();
}

contracts::Status parse_old(std::string_view s, std::uint32_t& out) {
    if (s.empty() || s.size() > 10) return Status::fail(ErrorCode::ParseError, "fast startup snapshot");
    std::uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return Status::fail(ErrorCode::ParseError, "fast startup snapshot");
        v = v * 10 + static_cast<std::uint32_t>(c - '0');
    }
    out = v;
    return contracts::ok_status();
}
}  // namespace

contracts::Result<recovery::Snapshot> FastStartupAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    auto v = api_.read_pref();
    if (!v.ok()) return R(v.error());
    recovery::Snapshot snap;
    snap.raw_type = "REG_DWORD";
    snap.existed = v.value().has_value();
    snap.old_value = snap.existed ? std::to_string(*v.value()) : std::string();
    return snap;
}

Status FastStartupAdapter::apply(const contracts::SettingOperation& op) {
    if (auto s = check_op(op); !s.ok()) return s;
    if (!valid_value(op.new_value)) return Status::fail(ErrorCode::OutOfRange, "fast startup value must be 0/1");
    return api_.write_pref(op.new_value == "1" ? 1u : 0u);
}

contracts::Result<std::optional<std::string>> FastStartupAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    auto v = api_.read_pref();
    if (!v.ok()) return R(v.error());
    if (!v.value()) return std::optional<std::string>{};
    return std::optional<std::string>{std::to_string(*v.value())};
}

Status FastStartupAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    if (auto s = check_op(op); !s.ok()) return s;
    if (!snap.existed) return api_.remove_pref();
    std::uint32_t v = 0;
    if (auto s = parse_old(snap.old_value, v); !s.ok()) return s;
    return api_.write_pref(v);  // eski deger (0/1 disinda olsa bile) birebir geri yazilir
}

contracts::Result<std::optional<std::uint32_t>> FakeFastStartup::read_pref() { return value; }

Status FakeFastStartup::write_pref(std::uint32_t v) {
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    ++writes;
    value = v;
    return contracts::ok_status();
}

Status FakeFastStartup::remove_pref() {
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    ++removes;
    value.reset();
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
