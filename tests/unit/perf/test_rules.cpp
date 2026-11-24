#include "core/perf/rules.h"
#include "ef_test.h"

using namespace ef::core::perf;
using namespace ef::contracts;

namespace {
Capability cap(const char* id, SupportState s) {
    Capability c;
    c.feature_id = id;
    c.state = s;
    if (s != SupportState::Supported) c.reason = "test";
    return c;
}
Rule rule(const char* id, const char* feature, const char* title) {
    Rule r;
    r.id = id;
    r.feature_id = feature;
    r.item.title_key = title;
    r.item.op = SettingOperation{OperationId::PowerModeSet, OperationScope::CurrentUser, "t", "v", false, false};
    return r;
}
std::optional<Exclusion> why(const RuleSelection& s, const std::string& id) {
    for (const auto& d : s.decisions)
        if (d.rule_id == id) return d.excluded;
    return Exclusion::CapabilityUnknown;
}
}  // namespace

EF_TEST(rules_unknown_or_irreversible_excluded) {
    RuleContext ctx;
    ctx.caps = {{"W01", cap("W01", SupportState::Supported)}, {"G10", cap("G10", SupportState::Unknown)}, {"A01", cap("A01", SupportState::Unsupported)}};
    auto r1 = rule("power", "W01", "plan.power_mode");
    auto r2 = rule("vendor", "G10", "plan.x");
    auto r3 = rule("amd", "A01", "plan.y");  // GPU-03 yokken AMD: unsupported (ADR-020)
    auto r4 = rule("noundo", "W01", "plan.z");
    r4.reversible = false;
    auto r5 = rule("nocap", "ZZ", "plan.q");
    const auto s = select_rules({r1, r2, r3, r4, r5}, ctx);
    EF_CHECK(s.plan.items.size() == 1 && s.plan.items[0].title_key == "plan.power_mode");
    EF_CHECK(why(s, "vendor") == Exclusion::CapabilityUnknown && why(s, "amd") == Exclusion::CapabilityNotSupported);
    EF_CHECK(why(s, "noundo") == Exclusion::NotReversible && why(s, "nocap") == Exclusion::CapabilityUnknown);
}

EF_TEST(rules_zero_ops_is_nothing_to_do) {
    RuleContext ctx;
    const auto s = select_rules({rule("a", "W01", "plan.a")}, ctx);  // yetenek yok
    EF_CHECK(s.nothing_to_do() && s.decisions.size() == 1 && s.decisions[0].excluded);
}

EF_TEST(rules_exclusive_quality_os_game_and_dependency) {
    RuleContext ctx;
    ctx.caps = {{"W01", cap("W01", SupportState::Supported)}};
    ctx.os_build = "26300";
    ctx.game_id = "steam:730";
    auto hi = rule("cap-high", "W01", "plan.hi");
    hi.exclusive_group = "fps_cap", hi.priority = 2;
    auto lo = rule("cap-low", "W01", "plan.lo");
    lo.exclusive_group = "fps_cap", lo.priority = 1;
    auto q = rule("lower-res", "W01", "plan.res");
    q.lowers_quality = true;
    auto newos = rule("needs-new-os", "W01", "plan.os");
    newos.min_os_build = "99999";
    auto other = rule("other-game", "W01", "plan.g");
    other.game_id = "epic:Fortnite";
    auto dep = rule("needs-res", "W01", "plan.dep");
    dep.requires_rules = {"lower-res"};
    auto s = select_rules({hi, lo, q, newos, other, dep}, ctx);
    EF_CHECK(s.plan.items.size() == 1 && s.plan.items[0].title_key == "plan.hi");
    EF_CHECK(why(s, "cap-low") == Exclusion::LostExclusiveGroup && why(s, "lower-res") == Exclusion::QualityNotApproved);
    EF_CHECK(why(s, "needs-new-os") == Exclusion::OsTooOld && why(s, "other-game") == Exclusion::OtherGame);
    EF_CHECK(why(s, "needs-res") == Exclusion::MissingDependency);
    ctx.quality_tradeoff_approved = true;  // kullanici kalite takasini onayladi
    s = select_rules({hi, lo, q, dep}, ctx);
    EF_CHECK(s.plan.items.size() == 3);
}
