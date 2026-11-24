#pragma once
// SEC-03: tipli izin listesi dagitici. Yalniz kayitli OperationId calisir; her isleyici izinli kapsam
// ve gereken yetkiyi bildirir. CurrentUser kapsami DAIMA hedef oyuncu SID'i ile calisir.

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"
#include "contracts/support.h"

namespace ef::core::security {

struct DispatchContext {
    std::string target_user_sid;     // oturumdaki oyuncu (INV-01 resolve_target_user)
    std::string broker_user_sid;     // islemi yurutenin token kullanicisi (bilgi)
    bool elevated = false;
    bool machine_lock_held = false;  // Machine kapsami icin zorunlu
};

using Handler = std::function<contracts::Status(const contracts::SettingOperation& op, const DispatchContext& ctx)>;

struct HandlerSpec {
    std::vector<contracts::OperationScope> scopes;
    contracts::Privilege privilege = contracts::Privilege::User;
    Handler fn;
};

class Dispatcher {
public:
    contracts::Status register_handler(contracts::OperationId id, HandlerSpec spec);
    // Kontroller: kayit, kapsam, yetki, hedef SID, makine kilidi. Sonra isleyici.
    contracts::Status dispatch(const contracts::SettingOperation& op, const DispatchContext& ctx) const;
    bool is_registered(contracts::OperationId id) const { return handlers_.count(id) != 0; }

private:
    std::map<contracts::OperationId, HandlerSpec> handlers_;
};

bool looks_like_user_sid(const std::string& s) noexcept;  // S-1-5-21-... veya S-1-12-1-... (AAD)

}  // namespace ef::core::security
