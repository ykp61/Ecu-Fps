#include "core/adapters/windows/boot_options.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::Result;
using contracts::SettingOperation;
using contracts::Status;

namespace {
constexpr const char* kUnverified = "boot option: BCD read path not verified (ADR-040 m.4, R-B4 adim 0)";
constexpr const char* kRawType = "BCD_BOOLEAN";

// Adaptor seviyesinde de yalniz tablodaki oge + Machine + BootOptionSet (bicim; deger ayrica denetlenir).
Result<BootElement> check_op(const SettingOperation& op) {
    if (op.id != OperationId::BootOptionSet || op.scope != OperationScope::Machine)
        return Result<BootElement>::fail(ErrorCode::Denied, "boot option: operation not in allowlist");
    const auto e = boot_element_of(op.target);
    if (!e) return Result<BootElement>::fail(ErrorCode::Denied, "boot option: target not in allowlist");
    return *e;
}

// Okunan/kayitli deger yalniz "on" | "off" olabilir; baska her sey bozuk sayilir (tahminle yazilmaz).
Result<std::optional<std::string>> checked(Result<std::optional<std::string>> v) {
    if (!v.ok()) return v;
    if (v.value() && !is_boot_option_value(*v.value()))
        return Result<std::optional<std::string>>::fail(ErrorCode::ParseError, "boot option: unexpected BCD value");
    return v;
}
}  // namespace

bool is_boot_option_target(std::string_view target) noexcept { return target == kBootDynamicTickTarget; }

std::optional<BootElement> boot_element_of(std::string_view target) noexcept {
    if (target == kBootDynamicTickTarget) return BootElement::DisableDynamicTick;
    return std::nullopt;
}

bool is_boot_option_value(std::string_view v) noexcept { return v == kBootDynamicTickOff || v == kBootDynamicTickOn; }

bool boot_option_pair_allowed(std::string_view target, std::string_view value) noexcept {
    // ADR-040 m.4: tek hedef, tek deger. "on" istemciden hicbir zaman kabul edilmez (yalniz geri alma snapshot'indan).
    return target == kBootDynamicTickTarget && value == kBootDynamicTickOff;
}

bool boot_option_admin_allowed(const SettingOperation& op, bool item_enabled) noexcept {
    return item_enabled && op.id == OperationId::BootOptionSet && op.scope == OperationScope::Machine &&
           boot_option_pair_allowed(op.target, op.new_value);
}

Result<std::optional<std::string>> UnavailableBootOptionApi::read(BootElement) {
    return Result<std::optional<std::string>>::fail(ErrorCode::Unsupported, kUnverified);
}
Status UnavailableBootOptionApi::write(BootElement, std::string_view) { return Status::fail(ErrorCode::Unsupported, kUnverified); }
Status UnavailableBootOptionApi::remove(BootElement) { return Status::fail(ErrorCode::Unsupported, kUnverified); }

Result<recovery::Snapshot> BootOptionAdapter::capture(const SettingOperation& op) {
    using R = Result<recovery::Snapshot>;
    const auto e = check_op(op);
    if (!e.ok()) return R(e.error());
    auto v = checked(api_.read(e.value()));
    if (!v.ok()) return R(v.error());  // okunamayan BCD: islem baslamaz
    recovery::Snapshot snap;
    snap.raw_type = kRawType;
    snap.existed = v.value().has_value();
    snap.old_value = snap.existed ? *v.value() : std::string();
    return snap;
}

Status BootOptionAdapter::apply(const SettingOperation& op) {
    const auto e = check_op(op);
    if (!e.ok()) return Status(e.error());
    // Apply-only kapi: bayrak kapaliyken yeni yazma yok (restore bu kapiya bakmaz).
    if (!apply_enabled_) return Status::fail(ErrorCode::Denied, "boot option: item disabled (ADR-040 m.4)");
    if (!boot_option_pair_allowed(op.target, op.new_value)) return Status::fail(ErrorCode::OutOfRange, "boot option: value not allowed");
    return api_.write(e.value(), op.new_value);
}

Result<std::optional<std::string>> BootOptionAdapter::read_back(const SettingOperation& op) {
    using R = Result<std::optional<std::string>>;
    const auto e = check_op(op);
    if (!e.ok()) return R(e.error());
    return checked(api_.read(e.value()));
}

Status BootOptionAdapter::restore(const SettingOperation& op, const recovery::Snapshot& snap) {
    const auto e = check_op(op);
    if (!e.ok()) return Status(e.error());
    if (!snap.existed) return api_.remove(e.value());  // oge yoktu: silinir (deletevalue karsiligi)
    if (!is_boot_option_value(snap.old_value)) return Status::fail(ErrorCode::ParseError, "boot option snapshot");
    return api_.write(e.value(), snap.old_value);  // eski deger birebir
}

}  // namespace ef::core::adapters::windows
