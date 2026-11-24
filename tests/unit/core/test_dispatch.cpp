#include "core/security/dispatch.h"
#include "ef_test.h"

using namespace ef::core::security;
using namespace ef::contracts;

namespace {
const std::string kGamer = "S-1-5-21-1111-2222-3333-1001";
const std::string kOtherAdmin = "S-1-5-21-1111-2222-3333-500";

SettingOperation op(OperationId id, OperationScope sc, std::string target = "t") {
    return SettingOperation{id, sc, std::move(target), "v", false, false};
}
}  // namespace

EF_TEST(dispatch_allowlist_scope_privilege) {
    Dispatcher d;
    std::string seen_sid;
    EF_CHECK(d.register_handler(OperationId::GameModeSet,
                                 {{OperationScope::CurrentUser}, Privilege::User,
                                  [&](const SettingOperation&, const DispatchContext& c) {
                                      seen_sid = c.target_user_sid;
                                      return ok_status();
                                  }})
                  .ok());
    EF_CHECK(d.register_handler(OperationId::PowerSchemeSet,
                                 {{OperationScope::Machine}, Privilege::Admin,
                                  [](const SettingOperation&, const DispatchContext&) { return ok_status(); }})
                  .ok());
    EF_CHECK(!d.register_handler(OperationId::GameModeSet, {{OperationScope::CurrentUser}, Privilege::User,
                                                             [](const SettingOperation&, const DispatchContext&) { return ok_status(); }})
                   .ok());  // ikinci kayit yok

    DispatchContext ctx{kGamer, kOtherAdmin, true, true};
    // Kayitsiz islem
    EF_CHECK(d.dispatch(op(OperationId::StartupTask, OperationScope::Machine), ctx).code() == ErrorCode::Unsupported);
    // Yanlis kapsam
    EF_CHECK(d.dispatch(op(OperationId::GameModeSet, OperationScope::Machine), ctx).code() == ErrorCode::Denied);
    // Yonetici gerektiren islem yukseltmesiz
    DispatchContext user{kGamer, kGamer, false, true};
    EF_CHECK(d.dispatch(op(OperationId::PowerSchemeSet, OperationScope::Machine), user).code() == ErrorCode::Denied);
    // Guvensiz hedef
    EF_CHECK(d.dispatch(op(OperationId::GameModeSet, OperationScope::CurrentUser, "C:\\x"), ctx).code() == ErrorCode::ParseError);
}

EF_TEST(other_admin_does_not_replace_gamer_hkcu) {
    Dispatcher d;
    std::string seen;
    EF_CHECK(d.register_handler(OperationId::GameModeSet, {{OperationScope::CurrentUser}, Privilege::User,
                                                            [&](const SettingOperation&, const DispatchContext& c) {
                                                                seen = c.target_user_sid;
                                                                return ok_status();
                                                            }})
                  .ok());
    DispatchContext ctx{kGamer, kOtherAdmin, true, false};
    EF_CHECK(d.dispatch(op(OperationId::GameModeSet, OperationScope::CurrentUser), ctx).ok());
    EF_CHECK(seen == kGamer);  // yukselten yonetici degil, oyuncu
    DispatchContext unknown{"", kOtherAdmin, true, false};
    EF_CHECK(d.dispatch(op(OperationId::GameModeSet, OperationScope::CurrentUser), unknown).code() == ErrorCode::Denied);
    DispatchContext system{"S-1-5-18", kOtherAdmin, true, false};  // SYSTEM hedef olamaz
    EF_CHECK(d.dispatch(op(OperationId::GameModeSet, OperationScope::CurrentUser), system).code() == ErrorCode::Denied);
}

EF_TEST(machine_ops_require_lock) {
    Dispatcher d;
    int writes = 0;
    EF_CHECK(d.register_handler(OperationId::PowerSchemeSet, {{OperationScope::Machine}, Privilege::Admin,
                                                               [&](const SettingOperation&, const DispatchContext&) {
                                                                   ++writes;
                                                                   return ok_status();
                                                               }})
                  .ok());
    DispatchContext nolock{kGamer, kGamer, true, false};
    EF_CHECK(d.dispatch(op(OperationId::PowerSchemeSet, OperationScope::Machine), nolock).code() == ErrorCode::Denied);
    EF_CHECK(writes == 0);
    DispatchContext lock{kGamer, kGamer, true, true};
    EF_CHECK(d.dispatch(op(OperationId::PowerSchemeSet, OperationScope::Machine), lock).ok());
    EF_CHECK(writes == 1);
}

EF_TEST(user_sid_shape) {
    EF_CHECK(looks_like_user_sid("S-1-5-21-3623811015-3361044348-30300820-1013"));
    EF_CHECK(looks_like_user_sid("S-1-12-1-111-222-333-444"));  // Entra ID
    EF_CHECK(!looks_like_user_sid("S-1-5-18"));
    EF_CHECK(!looks_like_user_sid("S-1-5-32-544"));
    EF_CHECK(!looks_like_user_sid("S-1-5-21-1\\..\\x"));
}
