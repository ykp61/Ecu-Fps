#pragma once
// WIN-07 platform: ekran surucusu surum/tarih/saglayici okuma (SetupAPI DEVPKEY). Kurulum yok.

#include <vector>

#include "core/adapters/windows/update_check.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

std::vector<core::adapters::windows::DriverInfo> read_display_drivers();
core::json::Value update_check_json();

}  // namespace ef::platform::inventory
