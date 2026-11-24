#pragma once
// EF-X22: kosu-suruyor isareti. Test basinda kalici yazilir, bitince silinir. Sonraki acilista hala varsa
// kosu yarida kesilmistir (kapanma, cokme, guc kaybi): gecmise "kesildi" kaydi eklenir; test KENDILIGINDEN
// yeniden baslatilmaz ve kesik kosu basari/sonuc gibi gosterilmez.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/insights/history.h"
#include "core/measure/address_pattern.h"

namespace ef::core::measure {

inline constexpr char kRunMarkerFile[] = "run-in-progress.json";

struct RunMarker {
    std::string source;  // "builtin" / "in_game" / "endurance" / "balance"
    std::int64_t started_unix_ms = 0;
};

std::string encode_run_marker(const RunMarker& m);
std::optional<RunMarker> decode_run_marker(std::string_view text);
// Gecersiz kosu: fark/yuzde uretmez, dislama nedeni "run.interrupted".
insights::HistoryEntry interrupted_entry(const RunMarker& m);

}  // namespace ef::core::measure
