#include "core/adapters/windows/hags.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;

namespace {
Status check_op(const contracts::SettingOperation& op) {
    if (op.id != contracts::OperationId::HagsSet || op.target != kHagsTarget)
        return Status::fail(ErrorCode::Denied, "hags: target not in allowlist");
    return contracts::ok_status();
}
}  // namespace

contracts::Result<recovery::Snapshot> HagsAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    auto v = api_.read_pref();
    if (!v.ok()) return R(v.error());
    recovery::Snapshot snap;
    snap.raw_type = "REG_DWORD";
    snap.existed = v.value().has_value();
    snap.old_value = snap.existed ? std::to_string(*v.value()) : std::string();
    if (const auto sup = api_.supported()) snap.source_version = *sup ? "hags_supported" : "hags_unsupported";
    return snap;
}

Status HagsAdapter::apply(const contracts::SettingOperation& op) {
    if (auto s = check_op(op); !s.ok()) return s;
    if (!valid_value(op.new_value)) return Status::fail(ErrorCode::OutOfRange, "hags value");
    // Desteklenmeyen GPU'da tercih yazilmaz (Windows yok sayar; sahte "acik" sonucu uretmeyelim).
    if (op.new_value == "2" && api_.supported() == std::optional<bool>(false)) return Status::fail(ErrorCode::Unsupported, "hags unsupported");
    return api_.write_pref(op.new_value == "2" ? kHagsOn : kHagsOff);
}

contracts::Result<std::optional<std::string>> HagsAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    if (auto s = check_op(op); !s.ok()) return R(s.error());
    auto v = api_.read_pref();
    if (!v.ok()) return R(v.error());
    if (!v.value()) return std::optional<std::string>{};
    return std::optional<std::string>{std::to_string(*v.value())};
}

Status HagsAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    if (auto s = check_op(op); !s.ok()) return s;
    if (!snap.existed) return api_.remove_pref();
    std::uint32_t v = 0;
    if (snap.old_value.empty() || snap.old_value.size() > 10) return Status::fail(ErrorCode::ParseError, "hags snapshot");
    for (char c : snap.old_value) {
        if (c < '0' || c > '9') return Status::fail(ErrorCode::ParseError, "hags snapshot");
        v = v * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return api_.write_pref(v);  // eski deger (1/2 disinda olsa bile) birebir geri yazilir
}

contracts::Result<std::optional<std::uint32_t>> FakeHags::read_pref() { return value; }

Status FakeHags::write_pref(std::uint32_t v) {
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    ++writes;
    value = v;
    return contracts::ok_status();
}

Status FakeHags::remove_pref() {
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    ++removes;
    value.reset();
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
