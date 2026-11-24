#include "core/perf/rules.h"

#include <algorithm>
#include <set>

namespace ef::core::perf {

std::string_view to_string(Exclusion e) noexcept {
    switch (e) {
        case Exclusion::CapabilityNotSupported: return "capability_not_supported";
        case Exclusion::CapabilityUnknown: return "capability_unknown";
        case Exclusion::NotReversible: return "not_reversible";
        case Exclusion::OsTooOld: return "os_too_old";
        case Exclusion::OtherGame: return "other_game";
        case Exclusion::QualityNotApproved: return "quality_not_approved";
        case Exclusion::LostExclusiveGroup: return "lost_exclusive_group";
        case Exclusion::MissingDependency: return "missing_dependency";
    }
    return "capability_unknown";
}

namespace {
bool os_at_least(const std::string& have, const std::string& need) {
    if (need.empty()) return true;
    if (have.empty()) return false;  // bilinmeyen surumde kosullu kural uygulanmaz
    try {
        return std::stoll(have) >= std::stoll(need);
    } catch (...) {
        return false;
    }
}
}  // namespace

RuleSelection select_rules(const std::vector<Rule>& rules, const RuleContext& ctx) {
    RuleSelection out;
    std::vector<const Rule*> eligible;
    for (const auto& r : rules) {
        RuleDecision d{r.id, std::nullopt};
        const auto it = ctx.caps.find(r.feature_id);
        if (it == ctx.caps.end() || it->second.state == contracts::SupportState::Unknown) d.excluded = Exclusion::CapabilityUnknown;
        else if (it->second.state != contracts::SupportState::Supported) d.excluded = Exclusion::CapabilityNotSupported;
        else if (!r.reversible) d.excluded = Exclusion::NotReversible;
        else if (!os_at_least(ctx.os_build, r.min_os_build)) d.excluded = Exclusion::OsTooOld;
        else if (!r.game_id.empty() && r.game_id != ctx.game_id) d.excluded = Exclusion::OtherGame;
        else if (r.lowers_quality && !ctx.quality_tradeoff_approved) d.excluded = Exclusion::QualityNotApproved;
        if (!d.excluded) eligible.push_back(&r);
        out.decisions.push_back(d);
    }
    auto mark = [&](const std::string& id, Exclusion e) {
        for (auto& d : out.decisions)
            if (d.rule_id == id && !d.excluded) d.excluded = e;
    };
    // Karsilikli dislama: grup basina en yuksek oncelik (esitlikte kural sirasi).
    std::map<std::string, const Rule*> winner;
    for (const auto* r : eligible)
        if (!r->exclusive_group.empty()) {
            auto& w = winner[r->exclusive_group];
            if (!w || r->priority > w->priority) w = r;
        }
    std::vector<const Rule*> kept;
    for (const auto* r : eligible) {
        if (!r->exclusive_group.empty() && winner[r->exclusive_group] != r) {
            mark(r->id, Exclusion::LostExclusiveGroup);
            continue;
        }
        kept.push_back(r);
    }
    // Bagimliliklar: sabit noktaya kadar eksik bagimliligi olani cikar.
    bool changed = true;
    while (changed) {
        changed = false;
        std::set<std::string> in;
        for (const auto* r : kept) in.insert(r->id);
        for (auto it = kept.begin(); it != kept.end();) {
            const bool ok = std::all_of((*it)->requires_rules.begin(), (*it)->requires_rules.end(), [&](const std::string& q) { return in.count(q) > 0; });
            if (!ok) {
                mark((*it)->id, Exclusion::MissingDependency);
                it = kept.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    for (const auto* r : kept) out.plan.items.push_back(r->item);
    return out;
}

}  // namespace ef::core::perf
