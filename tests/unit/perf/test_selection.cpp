#include <deque>
#include <functional>

#include "core/perf/controller.h"
#include "core/perf/journal_backend.h"
#include "core/perf/vfx_plan.h"
#include "core/recovery/journal.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace rec = ef::core::recovery;
namespace cw = ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
struct FakeVfx final : cw::VisualEffectsApi {
    std::map<cw::VisualEffect, bool> on;
    ef::contracts::Result<bool> get(cw::VisualEffect e) override { return on.count(e) ? on[e] : false; }
    ef::contracts::Status set(cw::VisualEffect e, bool v) override {
        on[e] = v;
        return ef::contracts::ok_status();
    }
};
PlanItem item(const char* target, const char* group) {
    PlanItem i;
    i.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, target, "1", false, false};
    i.title_key = group;
    i.group_key = group;
    return i;
}
}  // namespace

EF_TEST(perf_vfx_plan_only_enabled_effects) {
    FakeVfx api;
    api.on[cw::VisualEffect::MenuAnimation] = true;
    api.on[cw::VisualEffect::DropShadow] = true;
    api.on[cw::VisualEffect::CursorShadow] = false;
    const auto items = vfx_plan_items(api);
    EF_CHECK(items.size() == 2);
    for (const auto& i : items) EF_CHECK(i.group() == "plan.vfx" && i.op.new_value == "0" && i.op.target.rfind("vfx:", 0) == 0);
    FakeVfx none;
    EF_CHECK(vfx_plan_items(none).empty());
}

EF_TEST(perf_unselected_group_is_not_applied_nor_journaled) {
    rec::MemoryJournalFs fs;
    rec::JournalStore store(fs, "m");
    rec::FakeAdapter fake;
    fake.values["p1"] = "0";
    fake.values["v1"] = "0";
    fake.values["v2"] = "0";
    rec::AdapterRegistry reg;
    reg.add(OperationId::VisualEffectsSet, &fake);
    rec::TransactionEngine eng(store, reg);
    Plan plan;
    plan.items = {item("p1", "plan.power_mode"), item("v1", "plan.vfx"), item("v2", "plan.vfx")};
    JournalBackendDeps deps;
    deps.scope = {"user", &store, &eng};
    deps.build_plan = [&] { return ef::contracts::Result<Plan>(plan); };
    int n = 0;
    deps.new_tx_id = [&] { return std::string(31, '0') + std::to_string(++n % 10); };
    JournalBackend be(deps);
    std::deque<std::function<void()>> q;
    auto sync = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    PerfController c(be, sync, sync);
    auto drain = [&] { while (!q.empty()) { auto f = std::move(q.front()); q.pop_front(); f(); } };
    std::vector<std::pair<std::string, bool>> persisted;
    c.on_selection_changed = [&](const std::string& g, bool v) { persisted.push_back({g, v}); };
    c.init();
    c.refresh_preview();
    drain();
    EF_CHECK(c.preview().items.size() == 3 && c.phase() == Phase::Off);  // onizleme asamayi degistirmez
    c.set_selected("plan.vfx", false);  // kullanici animasyonlari kapatmak istemiyor
    EF_CHECK(!c.selected("plan.vfx") && c.selected("plan.power_mode") && persisted.size() == 1);
    c.toggle();
    drain();
    c.answer(true);
    drain();
    EF_CHECK(c.phase() == Phase::On && c.active_changes() == 1);
    EF_CHECK(fake.values["p1"] == "1" && fake.values["v1"] == "0" && fake.values["v2"] == "0");
    // Journal'da yalniz secilen oge var
    const auto l = store.scan();
    EF_CHECK(l.transactions.size() == 1 && l.transactions[0].steps.size() == 1 && l.transactions[0].steps[0].op.target == "p1");
    // Is surerken secim degismez
    c.toggle();
    c.answer(true);
    c.set_selected("plan.vfx", true);
    EF_CHECK(!c.selected("plan.vfx"));
    drain();
    EF_CHECK(c.phase() == Phase::Off && fake.values["p1"] == "0");
}

EF_TEST(perf_nothing_selected_changes_nothing) {
    rec::MemoryJournalFs fs;
    rec::JournalStore store(fs, "m");
    rec::FakeAdapter fake;
    fake.values["v1"] = "0";
    rec::AdapterRegistry reg;
    reg.add(OperationId::VisualEffectsSet, &fake);
    rec::TransactionEngine eng(store, reg);
    Plan plan;
    plan.items = {item("v1", "plan.vfx")};
    JournalBackendDeps deps;
    deps.scope = {"user", &store, &eng};
    deps.build_plan = [&] { return ef::contracts::Result<Plan>(plan); };
    deps.new_tx_id = [] { return std::string(32, 'b'); };
    JournalBackend be(deps);
    auto inline_exec = [](std::function<void()> f) { f(); };
    PerfController c(be, inline_exec, inline_exec);
    c.load_selection({{"plan.vfx", false}});
    c.init();
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == Phase::Off && c.notice() == Notice::NoneSelected && fake.applies == 0 && store.scan().transactions.empty());
}
