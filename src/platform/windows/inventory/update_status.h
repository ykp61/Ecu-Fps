#pragma once
// WIN-06/WIN-07 platform: Windows Update Agent (wuapi) ve BITS durum okuma. Yazma yok.

#include "core/adapters/windows/update_guard.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

core::adapters::windows::UpdateStatus read_update_status();
core::json::Value update_status_json();

}  // namespace ef::platform::inventory
