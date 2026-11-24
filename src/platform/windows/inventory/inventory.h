#pragma once
// Tum salt okuma envanterini tek JSON belgesinde toplar (tanilama ve kanit icin).

#include "core/serialization/json.h"

namespace ef::platform::inventory {

core::json::Value collect_all_json();

}  // namespace ef::platform::inventory
