#include "core/measure/run_marker.h"

#include "core/serialization/json.h"

namespace ef::core::measure {

std::string encode_run_marker(const RunMarker& m) {
    auto o = json::Value::object();
    o.set("source", json::Value::string(m.source));
    o.set("started_unix_ms", json::Value::integer(m.started_unix_ms));
    return json::write(o);
}

std::optional<RunMarker> decode_run_marker(std::string_view text) {
    const auto doc = json::parse(text);
    if (!doc.ok()) return std::nullopt;
    auto src = json::get_string(doc.value(), "source", 16);
    auto when = json::get_int(doc.value(), "started_unix_ms", 0, 0x7FFFFFFFFFFFFFFFll);
    if (!src.ok() || !when.ok()) return std::nullopt;
    if (src.value() != "builtin" && src.value() != "in_game" && src.value() != "endurance" && src.value() != "balance") return std::nullopt;
    return RunMarker{src.value(), when.value()};
}

insights::HistoryEntry interrupted_entry(const RunMarker& m) {
    insights::HistoryEntry e;
    e.when_unix_ms = m.started_unix_ms;
    e.source = m.source;
    // Baslik bos: gorunen metin dislama nedeninden gelir (ham ic kimlik gosterilmez).
    e.run_valid = false;  // fingerprint bos: hicbir kayitla karsilastirilmaz
    e.exclusions.push_back("run.interrupted");
    return e;
}

}  // namespace ef::core::measure
