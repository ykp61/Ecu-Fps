#include "core/adapters/windows/gpu_preference.h"
#include "core/perf/gpu_route_plan.h"
#include "ef_test.h"

using namespace ef::core::perf;
using ef::core::readiness::GameGpuPref;
using ef::core::readiness::GpuPrefInfo;
using ef::core::readiness::GpuPrefKind;
namespace contracts = ef::contracts;

namespace {
GameGpuPref game(const std::string& name, GpuPrefKind k) {
    GameGpuPref g;
    g.game = name;
    g.exe = "C:\\Games\\" + name;
    g.pref = k;
    return g;
}
GpuPrefInfo hybrid_laptop() {
    GpuPrefInfo i;
    i.laptop = true;
    i.hybrid = true;
    return i;
}
}  // namespace

EF_TEST(gpuroute_empty_when_not_hybrid_laptop) {
    GpuPrefInfo none;  // hazirlik taramasi yok: bos
    none.games.push_back(game("a.exe", GpuPrefKind::SystemDefault));
    EF_CHECK(gpu_route_plan_items(none).empty());
    auto desktop = hybrid_laptop();
    desktop.laptop = false;
    desktop.games.push_back(game("a.exe", GpuPrefKind::SystemDefault));
    EF_CHECK(gpu_route_plan_items(desktop).empty());
    auto not_hybrid = hybrid_laptop();
    not_hybrid.hybrid = false;
    not_hybrid.games.push_back(game("a.exe", GpuPrefKind::SystemDefault));
    EF_CHECK(gpu_route_plan_items(not_hybrid).empty());
    auto unknown_laptop = hybrid_laptop();
    unknown_laptop.laptop.reset();
    unknown_laptop.games.push_back(game("a.exe", GpuPrefKind::SystemDefault));
    EF_CHECK(gpu_route_plan_items(unknown_laptop).empty());
    auto unknown_hybrid = hybrid_laptop();
    unknown_hybrid.hybrid.reset();
    unknown_hybrid.games.push_back(game("a.exe", GpuPrefKind::PowerSaving));
    EF_CHECK(gpu_route_plan_items(unknown_hybrid).empty());
}

EF_TEST(gpuroute_only_default_and_power_saving) {
    auto i = hybrid_laptop();
    i.games.push_back(game("default.exe", GpuPrefKind::SystemDefault));
    i.games.push_back(game("saving.exe", GpuPrefKind::PowerSaving));
    i.games.push_back(game("high.exe", GpuPrefKind::HighPerformance));
    i.games.push_back(game("unknown.exe", GpuPrefKind::Unknown));
    const auto items = gpu_route_plan_items(i);
    EF_CHECK(items.size() == 2);
    for (const auto& it : items) {
        EF_CHECK(it.op.id == contracts::OperationId::GpuPreferenceSet);
        EF_CHECK(it.op.scope == contracts::OperationScope::CurrentUser);
        EF_CHECK(it.op.new_value == "2");
        EF_CHECK(it.group() == kGpuRouteGroup && !it.needs_admin && !it.changes_startup);
        EF_CHECK(ef::core::adapters::windows::gpu_pref_exe_from_target(it.op.target).has_value());
        EF_CHECK(it.detail.has_value());
    }
    EF_CHECK(items[0].detail->name == "default.exe" && items[1].detail->name == "saving.exe");
}

EF_TEST(gpuroute_cap_writable_filter_and_bad_path) {
    auto i = hybrid_laptop();
    for (int n = 0; n < 40; ++n) i.games.push_back(game("g" + std::to_string(n) + ".exe", GpuPrefKind::SystemDefault));
    EF_CHECK(gpu_route_plan_items(i).size() == kGpuRouteMaxItems);
    // Yazilamayan EXE plana girmez (apply reddederdi).
    auto j = hybrid_laptop();
    j.games.push_back(game("ok.exe", GpuPrefKind::SystemDefault));
    j.games.push_back(game("no.exe", GpuPrefKind::SystemDefault));
    const auto filtered = gpu_route_plan_items(j, [](std::string_view exe) { return exe.find("ok.exe") != std::string_view::npos; });
    EF_CHECK(filtered.size() == 1 && filtered[0].detail->name == "ok.exe");
    // Gecersiz yol (mutlak degil) hedef kurulamaz: oge yok.
    auto k = hybrid_laptop();
    GameGpuPref bad;
    bad.game = "x.exe";
    bad.exe = "x.exe";
    bad.pref = GpuPrefKind::SystemDefault;
    k.games.push_back(bad);
    EF_CHECK(gpu_route_plan_items(k).empty());
}
