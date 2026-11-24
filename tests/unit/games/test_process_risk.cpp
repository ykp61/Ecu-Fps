#include <string>
#include <vector>

#include "core/games/process_risk.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {
bool has(const std::vector<ConfigRisk>& v, std::string_view code) {
    for (const auto& r : v)
        if (r.code == code) return true;
    return false;
}
}  // namespace

EF_TEST(process_risk_realtime_and_high_priority_flagged) {
    ProcessState s;
    s.known = true;
    s.priority_class = 0x100;
    auto r = scan_process_state(s);
    EF_CHECK(r.size() == 1 && has(r, "risk.process.realtime"));
    s.priority_class = 0x80;
    r = scan_process_state(s);
    EF_CHECK(r.size() == 1 && has(r, "risk.process.high"));
}

EF_TEST(process_risk_normal_priority_is_clean) {
    ProcessState s;
    s.known = true;
    s.priority_class = 0x20;  // NORMAL_PRIORITY_CLASS
    EF_CHECK(scan_process_state(s).empty());
    s.priority_class = 0x8000;  // ABOVE_NORMAL
    EF_CHECK(scan_process_state(s).empty());
}

EF_TEST(process_risk_affinity_only_when_strict_subset) {
    ProcessState s;
    s.known = true;
    s.system_mask = 0xFF;
    s.affinity_mask = 0xFF;
    EF_CHECK(scan_process_state(s).empty());           // tam maske: risk yok
    s.affinity_mask = 0x0F;
    EF_CHECK(has(scan_process_state(s), "risk.process.affinity_fixed"));
    s.affinity_mask = 0x1FF;                            // sistem maskesinde olmayan bit: yorumlanmaz
    EF_CHECK(scan_process_state(s).empty());
    s.system_mask = 0;                                  // bilinmiyor: risk yok
    s.affinity_mask = 0x0F;
    EF_CHECK(scan_process_state(s).empty());
}

EF_TEST(process_risk_unknown_state_never_claims_clean) {
    ProcessState s;
    EF_CHECK(scan_process_state(s).empty());
    EF_CHECK(!s.known);
}
