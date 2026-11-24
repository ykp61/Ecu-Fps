#include "core/insights/update_compare.h"

namespace ef::core::insights {

std::string_view to_string(Attribution a) noexcept {
    switch (a) {
        case Attribution::NoChangeInContext: return "no_change_in_context";
        case Attribution::SingleChangeCoincides: return "single_change_coincides";
        case Attribution::ConfoundedMultipleChanges: return "confounded_multiple_changes";
        case Attribution::ContextMissing: return "context_missing";
    }
    return "context_missing";
}

ContextDiff diff_context(const Context& a, const Context& b) {
    ContextDiff d;
    for (const auto& [k, v] : a) {
        const auto it = b.find(k);
        if (it == b.end()) d.missing.push_back(k);
        else if (it->second != v) d.changed.push_back(k);
    }
    for (const auto& [k, v] : b)
        if (!a.count(k)) d.missing.push_back(k);
    return d;
}

UpdateComparison compare_update(const Context& before, const Context& after, const std::vector<double>& bf, const std::vector<double>& af) {
    UpdateComparison u;
    u.diff = diff_context(before, after);
    u.fps = perf::compare_metric("avg_fps", perf::Direction::HigherIsBetter, bf, af);
    if (before.empty() || after.empty() || !u.diff.missing.empty()) u.attribution = Attribution::ContextMissing;
    else if (u.diff.changed.empty()) u.attribution = Attribution::NoChangeInContext;
    else if (u.diff.changed.size() == 1) u.attribution = Attribution::SingleChangeCoincides;  // neden degil, denk gelme
    else u.attribution = Attribution::ConfoundedMultipleChanges;
    return u;
}

}  // namespace ef::core::insights
