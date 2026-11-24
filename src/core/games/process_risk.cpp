#include "core/games/process_risk.h"

#include <cstdio>

namespace ef::core::games {

namespace {
std::string hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}
}  // namespace

std::vector<ConfigRisk> scan_process_state(const ProcessState& s) {
    std::vector<ConfigRisk> out;
    if (!s.known) return out;
    if (s.priority_class) {
        // Win32 sabitleri: HIGH_PRIORITY_CLASS=0x80, REALTIME_PRIORITY_CLASS=0x100.
        if (*s.priority_class == 0x100u)
            out.push_back({"risk.process.realtime", RiskSeverity::Warning, hex(*s.priority_class)});
        else if (*s.priority_class == 0x80u)
            out.push_back({"risk.process.high", RiskSeverity::Caution, hex(*s.priority_class)});
    }
    // Affinity yalnız sistem maskesinin gerçek alt kümesiyse "elle daraltılmış" sayılır.
    if (s.affinity_mask && s.system_mask && *s.system_mask != 0 && *s.affinity_mask != *s.system_mask &&
        (*s.affinity_mask & ~*s.system_mask) == 0)
        out.push_back({"risk.process.affinity_fixed", RiskSeverity::Caution, hex(*s.affinity_mask)});
    return out;
}

}  // namespace ef::core::games
