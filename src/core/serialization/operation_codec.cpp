#include "core/serialization/operation_codec.h"

namespace ef::core::json {

using contracts::ErrorCode;
using contracts::Result;
using contracts::SettingOperation;

bool is_safe_target(std::string_view s) noexcept {
    if (s.empty() || s.size() > kMaxTargetLen) return false;
    if (s.find("..") != std::string_view::npos) return false;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '.' || c == ':' || c == '-' || c == '{' || c == '}';
        if (!ok) return false;
    }
    // "C:" gibi surucu harfi ile baslayan yol bicimi reddedilir.
    if (s.size() >= 2 && s[1] == ':' && ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return false;
    return true;
}

Result<SettingOperation> decode_operation(const Value& v) {
    using R = Result<SettingOperation>;
    if (!v.is(Value::Type::Object)) return R::fail(ErrorCode::ParseError, "operation not object");
    // Bilinmeyen alanlar reddedilir: sozlesme disi alan sessizce yok sayilmaz.
    for (const auto& k : v.keys()) {
        if (k != "op" && k != "scope" && k != "target" && k != "value" && k != "needs_reboot" && k != "affects_quality")
            return R::fail(ErrorCode::ParseError, "unknown field: " + k);
    }
    auto op = get_string(v, "op", 64);
    if (!op.ok()) return R(op.error());
    auto id = contracts::parse_operation_id(op.value());
    if (!id) return R::fail(ErrorCode::InvalidEnum, "op");

    auto scope = get_string(v, "scope", 32);
    if (!scope.ok()) return R(scope.error());
    auto sc = contracts::parse_operation_scope(scope.value());
    if (!sc) return R::fail(ErrorCode::InvalidEnum, "scope");

    auto target = get_string(v, "target", kMaxTargetLen);
    if (!target.ok()) return R(target.error());
    if (!is_safe_target(target.value())) return R::fail(ErrorCode::ParseError, "unsafe target");

    auto value = get_string(v, "value", kMaxValueLen);
    if (!value.ok()) return R(value.error());

    SettingOperation out{*id, *sc, target.value(), value.value(), false, false};
    if (v.find("needs_reboot")) {
        auto b = get_bool(v, "needs_reboot");
        if (!b.ok()) return R(b.error());
        out.needs_reboot = b.value();
    }
    if (v.find("affects_quality")) {
        auto b = get_bool(v, "affects_quality");
        if (!b.ok()) return R(b.error());
        out.affects_quality = b.value();
    }
    return out;
}

Value encode_operation(const SettingOperation& op) {
    Value v = Value::object();
    v.set("op", Value::string(std::string(contracts::to_string(op.id))));
    v.set("scope", Value::string(std::string(contracts::to_string(op.scope))));
    v.set("target", Value::string(op.target));
    v.set("value", Value::string(op.new_value));
    v.set("needs_reboot", Value::boolean(op.needs_reboot));
    v.set("affects_quality", Value::boolean(op.affects_quality));
    return v;
}

}  // namespace ef::core::json
