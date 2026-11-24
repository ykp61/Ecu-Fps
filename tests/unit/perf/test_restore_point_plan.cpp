// T08: otomatik geri yukleme noktasi plan kurallari (saf; gercek SRSetRestorePoint calistirilmaz).
#include "core/perf/admin_plan.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using namespace ef::core::perf;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
PlanItem admin_item(const char* group) {
    PlanItem i;
    i.op = SettingOperation{OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", true, false};
    i.title_key = i.group_key = group;
    i.needs_admin = true;
    return i;
}
PlanItem user_item() {
    PlanItem i;
    i.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, "anim", "1", false, false};
    i.title_key = i.group_key = "plan.vfx";
    return i;
}
}  // namespace

EF_TEST(t08_no_admin_items_no_restore_point) {
    EF_CHECK(!restore_point_plan_item(false, std::nullopt, std::nullopt));
    EF_CHECK(!restore_point_plan_item(false, true, 100000));
}

EF_TEST(t08_recent_point_no_item_old_or_unknown_item) {
    EF_CHECK(!restore_point_plan_item(true, true, 3600));
    EF_CHECK(!restore_point_plan_item(true, true, kRestorePointMinAgeSeconds - 1));
    EF_CHECK(restore_point_plan_item(true, true, kRestorePointMinAgeSeconds).has_value());
    EF_CHECK(restore_point_plan_item(true, true, 3 * kRestorePointMinAgeSeconds).has_value());
    EF_CHECK(restore_point_plan_item(true, std::nullopt, std::nullopt).has_value());  // yas bilinmiyor: Windows siniri uygular
}

EF_TEST(t08_system_restore_off_no_item) {
    EF_CHECK(!restore_point_plan_item(true, false, std::nullopt));
    EF_CHECK(!restore_point_plan_item(true, false, 10 * kRestorePointMinAgeSeconds));
}

EF_TEST(t08_item_shape_and_group_catalog) {
    const auto it = restore_point_plan_item(true, true, std::nullopt);
    EF_CHECK(it.has_value());
    if (!it) return;
    EF_CHECK(it->op.id == OperationId::RestorePointCreate && it->needs_admin && it->group() == "plan.restore_point");
    EF_CHECK(is_restore_point_item(*it));
    bool found = false;
    for (const auto& g : plan_groups())
        if (g.key == "plan.restore_point") {
            found = true;
            EF_CHECK(g.default_on && g.evidence == Evidence::Documented && g.risk == Risk::Low);
        }
    EF_CHECK(found);
}

EF_TEST(t08_ordering_first_among_admin_items) {
    std::vector<PlanItem> admin{admin_item("plan.hags"), admin_item("plan.nvidia"), *restore_point_plan_item(true, true, std::nullopt)};
    restore_point_first(admin);
    EF_CHECK(admin.size() == 3 && is_restore_point_item(admin[0]));
    EF_CHECK(admin[1].group() == "plan.hags" && admin[2].group() == "plan.nvidia");  // digerlerinin sirasi korunur
}

EF_TEST(t08_lone_restore_point_is_dropped_when_no_other_admin_item_selected) {
    std::vector<PlanItem> items{user_item(), *restore_point_plan_item(true, true, std::nullopt)};
    drop_lone_restore_point(items);
    EF_CHECK(items.size() == 1 && !is_restore_point_item(items[0]));
    std::vector<PlanItem> with_admin{user_item(), admin_item("plan.hags"), *restore_point_plan_item(true, true, std::nullopt)};
    drop_lone_restore_point(with_admin);
    EF_CHECK(with_admin.size() == 3);
}
