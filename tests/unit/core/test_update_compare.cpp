// INS-02: guncelleme oncesi/sonrasi (update-comparison).
#include "core/insights/update_compare.h"
#include "ef_test.h"

using namespace ef::core::insights;

EF_TEST(update_compare_driver_not_blamed) {
    const Context a{{"driver", "621.01"}, {"os_build", "26300"}, {"game_version", "26393695"}};
    auto b = a;
    b["driver"] = "622.00";
    const std::vector<double> before{200, 201, 199, 200, 202}, after{180, 181, 179, 180, 182};
    auto u = compare_update(a, b, before, after);
    EF_CHECK(u.attribution == Attribution::SingleChangeCoincides && u.diff.changed.size() == 1 && u.diff.changed[0] == "driver");
    EF_CHECK(u.fps.verdict == ef::core::perf::Verdict::Significant);  // fark gercek; ama "surucu yuzunden" denmez
    b["game_version"] = "26400000";
    u = compare_update(a, b, before, after);
    EF_CHECK(u.attribution == Attribution::ConfoundedMultipleChanges && u.diff.changed.size() == 2);
}

EF_TEST(update_compare_no_change_and_missing_context) {
    const Context a{{"driver", "621.01"}};
    auto u = compare_update(a, a, {100, 101, 99}, {100, 100.5, 99.5});
    EF_CHECK(u.attribution == Attribution::NoChangeInContext && u.fps.verdict == ef::core::perf::Verdict::NoSignificantDifference);
    u = compare_update(a, {{"driver", "621.01"}, {"os_build", "26300"}}, {1, 2, 3}, {1, 2, 3});
    EF_CHECK(u.attribution == Attribution::ContextMissing && u.diff.missing.size() == 1);
    u = compare_update({}, a, {}, {});
    EF_CHECK(u.attribution == Attribution::ContextMissing && u.fps.verdict == ef::core::perf::Verdict::Unsupported);
}
