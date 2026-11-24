#include "core/security/dispatch.h"

#include <algorithm>

#include "core/serialization/operation_codec.h"

namespace ef::core::security {

using contracts::ErrorCode;
using contracts::OperationScope;
using contracts::Status;

bool looks_like_user_sid(const std::string& s) noexcept {
    if (!(s.rfind("S-1-5-21-", 0) == 0 || s.rfind("S-1-12-1-", 0) == 0)) return false;
    if (s.size() > 184) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || c == '-' || c == 'S')) return false;
    return true;
}

Status Dispatcher::register_handler(contracts::OperationId id, HandlerSpec spec) {
    if (handlers_.count(id)) return Status::fail(ErrorCode::ParseError, "duplicate handler");
    if (!spec.fn || spec.scopes.empty()) return Status::fail(ErrorCode::ParseError, "incomplete handler");
    handlers_.emplace(id, std::move(spec));
    return contracts::ok_status();
}

Status Dispatcher::dispatch(const contracts::SettingOperation& op, const DispatchContext& ctx) const {
    const auto it = handlers_.find(op.id);
    if (it == handlers_.end()) return Status::fail(ErrorCode::Unsupported, "operation not in allowlist");
    const auto& spec = it->second;
    if (std::find(spec.scopes.begin(), spec.scopes.end(), op.scope) == spec.scopes.end())
        return Status::fail(ErrorCode::Denied, "scope not allowed for operation");
    if (!json::is_safe_target(op.target)) return Status::fail(ErrorCode::ParseError, "unsafe target");
    if (spec.privilege == contracts::Privilege::Admin && !ctx.elevated)
        return Status::fail(ErrorCode::Denied, "operation requires elevation");
    if (op.scope == OperationScope::CurrentUser) {
        // Yukseltme baska yonetici hesabiyla yapilmis olabilir: hedef her zaman oyuncu SID'i.
        if (!looks_like_user_sid(ctx.target_user_sid)) return Status::fail(ErrorCode::Denied, "target user unknown");
    }
    if (op.scope == OperationScope::Machine && !ctx.machine_lock_held)
        return Status::fail(ErrorCode::Denied, "machine operation lock not held");
    return spec.fn(op, ctx);
}

}  // namespace ef::core::security
