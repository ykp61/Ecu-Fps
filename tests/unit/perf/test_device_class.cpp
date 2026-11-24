// T18: cihaz sinifi ve varsayilan uyarlama + uygulama ilerlemesi satiri.
#include <deque>
#include <map>
#include <string>

#include "core/perf/apply_progress.h"
#include "core/perf/controller.h"
#include "core/perf/device_class.h"
#include "ef_test.h"
#include "ui/shell/view_model.h"

using namespace ef::core::perf;
namespace rec = ef::core::recovery;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
constexpr std::uint64_t kGiB = 1ull << 30;

const PlanGroup& group(std::string_view key) {
    for (const auto& g : plan_groups())
        if (g.key == key) return g;
    static const PlanGroup none{};
    return none;
}

PlanItem item(const char* target, const char* grp) {
    PlanItem i;
    i.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, target, "1", false, false};
    i.title_key = grp;
    i.group_key = grp;
    return i;
}

struct FakeBackend final : PerfBackend {
    Plan next_plan;
    std::vector<rec::Progress> script;  // apply sirasinda yayinlanacak ilerleme
    std::size_t changes = 0;
    ef::contracts::Result<Plan> plan() override { return next_plan; }
    Outcome apply(const Plan& p, const ProgressFn& progress, const std::atomic<bool>&) override {
        for (const auto& s : script) progress(s);
        return {OutcomeKind::Active, changes ? changes : p.items.size(), {}};
    }
    Outcome restore(const ProgressFn&) override { return {OutcomeKind::Restored, 0, {}}; }
    std::size_t active_changes() override { return 0; }
    bool recovery_needed() override { return false; }
};

struct Loop {
    std::deque<std::function<void()>> q;
    Executor exec() {
        return [this](std::function<void()> f) { q.push_back(std::move(f)); };
    }
    Poster post() {
        return [this](std::function<void()> f) { q.push_back(std::move(f)); };
    }
    void drain() {
        while (!q.empty()) {
            auto f = std::move(q.front());
            q.pop_front();
            f();
        }
    }
};
}  // namespace

EF_TEST(device_class_low_end_by_ram_or_cores) {
    EF_CHECK(classify({false, 8 * kGiB, 16}).low_end);      // RAM <= 8 GiB
    EF_CHECK(classify({false, 16 * kGiB, 4}).low_end);      // cekirdek <= 4
    EF_CHECK(!classify({false, 8 * kGiB + 1, 6}).low_end);  // sinirin hemen ustu
    EF_CHECK(!classify({false, 32 * kGiB, 12}).low_end);
    EF_CHECK(classify({true, 32 * kGiB, 12}).laptop && !classify({true, 32 * kGiB, 12}).low_end);
}

EF_TEST(device_class_unknown_facts_set_no_flags) {
    const auto c = classify({});
    EF_CHECK(!c.laptop && !c.low_end);
    EF_CHECK(!classify({std::nullopt, std::nullopt, 12}).low_end);  // yalniz bilinen deger karar verir
    EF_CHECK(!classify({std::nullopt, 32 * kGiB, std::nullopt}).low_end);
    EF_CHECK(classify({std::nullopt, 4 * kGiB, std::nullopt}).low_end);  // bilinen tek olgu yeter
}

EF_TEST(default_on_for_only_background_flips_on_low_end) {
    const DeviceClass normal, low{false, true};
    for (const auto& g : plan_groups()) {
        EF_CHECK(default_on_for(g, normal) == g.default_on);  // normal cihaz: katalog varsayilani
        if (g.key == "plan.background") {
            EF_CHECK(!g.default_on && g.evidence == Evidence::Disputed);  // bilincli istisna: Tartismali ama dusuk onceliklendirme
            EF_CHECK(default_on_for(g, low));
        } else {
            EF_CHECK(default_on_for(g, low) == g.default_on);  // baska hicbir grup cevrilmez (Tartismali/Yuksek risk dahil)
        }
    }
    EF_CHECK(default_on_for(group("plan.vfx"), low));  // vfx zaten Evet kalir
    EF_CHECK(default_on_for(group("plan.vfx"), normal));
    EF_CHECK(default_on_for(group("plan.hags"), low) == group("plan.hags").default_on);
}

EF_TEST(controller_low_end_background_default_but_saved_choice_wins) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.power_mode"), item("b", "plan.background")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    EF_CHECK(!c.selected("plan.background"));  // cihaz bilgisi yok: varsayilan Hayir
    c.set_device_class({false, true});
    EF_CHECK(c.selected("plan.background"));
    EF_CHECK(c.selected("plan.power_mode"));
    c.load_selection({{"plan.background", false}});  // kayitli kullanici secimi kazanir
    EF_CHECK(!c.selected("plan.background"));
    // gorunum: cihaza gore varsayilani Evet olan grup Gelismis'e dusmez; kullanici Hayir dese de satir yerinde kalir (varsayilani Evet gruplar gibi)
    Plan p;
    p.items = b.next_plan.items;
    for (const auto& g : visible_groups(p, {}, DeviceClass{false, true}))
        if (g.key == "plan.background") EF_CHECK(!g.advanced);
    for (const auto& g : visible_groups(p, {{"plan.background", false}}, DeviceClass{false, false}))
        if (g.key == "plan.background") EF_CHECK(g.advanced);  // normal cihaz: varsayilan Hayir -> Gelismis
}

EF_TEST(controller_tailored_note_only_when_default_changed_and_no_saved_choice) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.power_mode"), item("b", "plan.background")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.set_device_class({false, true});
    c.init();
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming && c.tailored());
    c.set_selected("plan.background", false);  // kullanici kendi secti: not kalmaz
    EF_CHECK(!c.tailored());
}

EF_TEST(controller_not_tailored_on_normal_device) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.power_mode"), item("b", "plan.background")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.set_device_class({true, false});  // dizustu ama dusuk degil
    c.init();
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming && !c.tailored() && !c.selected("plan.background"));
}

EF_TEST(apply_phase_follows_engine_progress) {
    rec::Progress p;
    p.phase = rec::Phase::Capturing;
    EF_CHECK(apply_phase(p) == ApplyPhase::Prepare);
    p.phase = rec::Phase::Applying, p.step = 1, p.steps = 3, p.group = "plan.power_mode";
    EF_CHECK(apply_phase(p) == ApplyPhase::Apply && has_step_line(p));
    p.group = "plan.restore_point";
    EF_CHECK(apply_phase(p) == ApplyPhase::RestorePoint);
    p.phase = rec::Phase::Verifying, p.step = 0;
    EF_CHECK(apply_phase(p) == ApplyPhase::Verify && !has_step_line(p));
    EF_CHECK(apply_phase_key(ApplyPhase::Verify) == "perf.progress.phase.verify");
    rec::Progress q;  // eski baslatici: adim/grup yok -> adim satiri yok
    q.phase = rec::Phase::Applying, q.done = 1, q.total = 3;
    EF_CHECK(!has_step_line(q));
}

EF_TEST(perf_view_shows_step_group_and_phase_while_applying) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.power_mode"), item("b", "plan.vfx")};
    rec::Progress p1;
    p1.phase = rec::Phase::Applying, p1.done = 1, p1.total = 6, p1.step = 2, p1.steps = 2, p1.group = "plan.vfx";
    b.script = {p1};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.toggle();
    loop.drain();
    c.answer(true);
    // Arka plan is'i (plan yenileme + apply) sirayla calisir; ilerleme postasi islenince sonuc postasi henuz kuyrukta.
    while (!loop.q.empty() && c.progress().step != 2) {
        auto f = std::move(loop.q.front());
        loop.q.pop_front();
        f();
    }
    EF_CHECK(c.phase() == Phase::Applying);
    const auto v = ef::ui::make_perf_view(c);
    EF_CHECK(v.progress_has_step && v.progress_step == 2 && v.progress_steps == 2 && v.progress_group == "plan.vfx");
    EF_CHECK(v.progress_phase == ApplyPhase::Apply);
    EF_CHECK(v.verified == 0);  // tamamlanmadan ozet yok
}

EF_TEST(perf_view_summary_uses_outcome_count_only) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.power_mode"), item("b", "plan.vfx"), item("c", "plan.vfx")};
    b.changes = 3;
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On);
    EF_CHECK(ef::ui::make_perf_view(c).verified == 3);
    c.toggle();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && ef::ui::make_perf_view(c).verified == 0);
}

EF_TEST(perf_view_tailored_flag_only_in_confirm) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.background")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.set_device_class({false, true});
    c.init();
    EF_CHECK(!ef::ui::make_perf_view(c).tailored);
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming && ef::ui::make_perf_view(c).tailored);
}

// P4: "Fark yok" kaydi yalniz gorunum vurgusudur: grup secili kalir, plana girer ve uygulanir; gorunumde no_effect isaretli.
EF_TEST(perf_view_no_effect_group_is_flagged_but_still_applied) {
    FakeBackend b;
    b.next_plan.items = {item("a", "plan.power_mode"), item("b", "plan.vfx")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.set_no_effect_groups({"plan.vfx"});
    c.init();
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming);
    const auto v = ef::ui::make_perf_view(c);
    bool seen = false;
    for (const auto& g : v.groups) {
        if (g.key == "plan.vfx") {
            seen = true;
            EF_CHECK(g.no_effect && g.selected && g.will_change);
        }
        if (g.key == "plan.power_mode") EF_CHECK(!g.no_effect);
    }
    EF_CHECK(seen);
    std::size_t vfx_items = 0;
    for (const auto& it : c.plan().items) vfx_items += it.group() == "plan.vfx" ? 1u : 0u;
    EF_CHECK(vfx_items == 1);  // plan degismedi
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.verified_count() == 2);  // her iki grup da uygulandi
}