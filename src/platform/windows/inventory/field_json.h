#pragma once
// Field<T> -> JSON: {"value":..,"source":".."} veya {"unknown":"<neden>"}

#include <cstdint>
#include <string>

#include "contracts/inventory.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

inline core::json::Value jv(const std::string& s) { return core::json::Value::string(s); }
inline core::json::Value jv(bool b) { return core::json::Value::boolean(b); }
inline core::json::Value jv(int i) { return core::json::Value::integer(i); }
inline core::json::Value jv(std::uint32_t i) { return core::json::Value::integer(i); }
inline core::json::Value jv(std::int64_t i) { return core::json::Value::integer(i); }
inline core::json::Value jv(std::uint64_t i) { return core::json::Value::integer(static_cast<std::int64_t>(i)); }
inline core::json::Value jv(double d) { return core::json::Value::number(d); }

template <typename T>
core::json::Value field_json(const contracts::Field<T>& f) {
    auto o = core::json::Value::object();
    if (f.has()) {
        o.set("value", jv(*f.value));
        o.set("source", core::json::Value::string(std::string(contracts::to_string(f.source))));
    } else {
        o.set("unknown", core::json::Value::string(f.reason.empty() ? "not read" : f.reason));
    }
    return o;
}

}  // namespace ef::platform::inventory
