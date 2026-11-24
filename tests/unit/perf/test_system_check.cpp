#include <deque>

#include "core/perf/system_check.h"
#include "ef_test.h"

using namespace ef::core::perf;
using namespace ef::contracts;

namespace {
struct Q {
    std::deque<std::function<void()>> q;
    void drain() {
        while (!q.empty()) {
            auto f = std::move(q.front());
            q.pop_front();
            f();
        }
    }
};
PlanItem pi(const char* t, bool admin = false) {
    PlanItem p;
    p.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, t, "1", false, false};
    p.title_key = t;
    p.needs_admin = admin;
    return p;
}
std::vector<CheckFinding> scan() {
    return {{"vfx.animations_on", FixKind::Auto, pi("vfx"), {}},
            {"power.balanced", FixKind::Auto, pi("power"), {}},
            {"health.xmp_off", FixKind::GuidedBios, std::nullopt, {"guide.bios_xmp_1", "guide.bios_xmp_2"}},
            {"driver.old", FixKind::GuidedDriver, std::nullopt, {"guide.driver_1"}},
            {"svc.spooler", FixKind::Auto, pi("svc", true), {}}};  // yonetici: rehberliye duser
}
}  // namespace

EF_TEST(system_check_split_auto_guided_and_no_bios_writes) {
    Q q;
    int applied_items = 0;
    CheckDeps d;
    d.scan = scan;
    d.apply = [&](const Plan& p, const std::function<void(std::size_t, std::size_t)>& prog) {
        applied_items = static_cast<int>(p.items.size());
        for (std::size_t i = 0; i <= p.items.size(); ++i) prog(i, p.items.size());
        return Outcome{OutcomeKind::Active, p.items.size(), {}};
    };
    d.measure_ab = [] { return std::optional<double>(1.5); };
    d.run_background = [&](std::function<void()> f) { q.q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.q.push_back(std::move(f)); };
    SystemCheck c(d);
    c.start();
    q.drain();
    EF_CHECK(c.phase() == CheckPhase::Review && c.findings()[4].kind == FixKind::GuidedAdmin);
    c.set_selected(1, false);  // kullanici guc planina Hayir
    c.set_selected(2, true);   // rehberli: secilemez (yok sayilir)
    c.answer(true, true);
    q.drain();
    EF_CHECK(c.phase() == CheckPhase::Report && applied_items == 1);  // yalniz vfx; BIOS/surucu/yonetici yazilmadi
    const auto& r = *c.report();
    EF_CHECK(r.auto_applied == 1 && r.auto_skipped_by_user == 1 && r.guided.size() == 3 && r.ab_delta == 1.5);
    EF_CHECK(r.guided[0].step_keys.size() == 2);  // adim adim metin
    EF_CHECK(c.progress().first == c.progress().second);
}

EF_TEST(system_check_no_answer_writes_nothing) {
    Q q;
    bool applied = false;
    CheckDeps d;
    d.scan = scan;
    d.apply = [&](const Plan&, const std::function<void(std::size_t, std::size_t)>&) {
        applied = true;
        return Outcome{OutcomeKind::Active, 0, {}};
    };
    d.run_background = [&](std::function<void()> f) { q.q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.q.push_back(std::move(f)); };
    SystemCheck c(d);
    c.start();
    q.drain();
    c.answer(false, false);
    q.drain();
    EF_CHECK(!applied && c.phase() == CheckPhase::Report && c.report()->outcome == OutcomeKind::NothingToDo);
    EF_CHECK(c.report()->auto_skipped_by_user == 2);
}

EF_TEST(system_check_group_extra_items_applied_together) {
    Q q;
    std::size_t n = 0;
    CheckDeps d;
    d.scan = [] {
        CheckFinding f{"vfx", FixKind::Auto, pi("vfx.1"), {}};
        f.extra_items = {pi("vfx.2"), pi("vfx.3")};
        return std::vector<CheckFinding>{f};
    };
    d.apply = [&](const Plan& p, const std::function<void(std::size_t, std::size_t)>&) {
        n = p.items.size();
        return Outcome{OutcomeKind::Active, p.items.size(), {}};
    };
    d.run_background = [&](std::function<void()> f) { q.q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.q.push_back(std::move(f)); };
    SystemCheck c(d);
    c.start();
    q.drain();
    EF_CHECK(c.findings().size() == 1);
    c.answer(true, false);
    q.drain();
    EF_CHECK(n == 3 && c.report()->auto_applied == 3);
}
