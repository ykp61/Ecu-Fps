#pragma once
// INV-01: Windows ve kullanici baglami (normal izinlerle, salt okuma).

#include "contracts/inventory.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

contracts::OsContext read_os();
contracts::UserContext read_user();
contracts::PowerContext read_power();
contracts::PolicyContext read_policy();

core::json::Value to_json(const contracts::OsContext& v);
core::json::Value to_json(const contracts::UserContext& v);
core::json::Value to_json(const contracts::PowerContext& v);
core::json::Value to_json(const contracts::PolicyContext& v);

}  // namespace ef::platform::inventory
