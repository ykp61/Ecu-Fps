#include <vector>

#include "ef_test.h"
#include "ui/lifecycle/activity.h"

using namespace ef::ui;

EF_TEST(ui_activity_notifies_and_gates_sampling) {
    Activity a;
    std::vector<Visibility> seen;
    const int id = a.subscribe([&](Visibility v) { seen.push_back(v); });
    a.set(Visibility::Visible);
    a.set(Visibility::Visible);  // ayni durum: bildirim yok
    a.set(Visibility::Minimized);
    a.unsubscribe(id);
    a.set(Visibility::Hidden);
    EF_CHECK(seen.size() == 2 && seen[0] == Visibility::Visible && seen[1] == Visibility::Minimized);
    EF_CHECK(Activity::should_sample_idle(Visibility::Visible));
    for (auto v : {Visibility::Minimized, Visibility::Hidden, Visibility::Closed}) {
        EF_CHECK(!Activity::should_sample_idle(v));
        EF_CHECK(Activity::ui_refresh_ms(v) == 0);
    }
    EF_CHECK(Activity::ui_refresh_ms(Visibility::Visible) >= 1000);  // UI cizimi en fazla 1 Hz
}
