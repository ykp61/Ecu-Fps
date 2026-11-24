// OPT-03 gercek cihaz: A = Guc tasarrufu, B = Yuksek performans, ABBA sirasiyla 6+6 sahne kosusu (scene_v1 Low,
// 600 kare). Opt-in (EF_ALLOW_MUTATION=1): guc plani degisir ve sonunda OZGUN plan geri yuklenir.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>

#include "benchmark/scene/d3d_renderer.h"
#include "core/perf/experiment.h"

namespace {
std::string active_scheme() {
    char buf[512]{};
    FILE* f = _popen("powercfg /getactivescheme", "r");
    if (!f) return {};
    std::string s;
    while (fgets(buf, sizeof buf, f)) s += buf;
    _pclose(f);
    const auto p = s.find("GUID: ");
    return p == std::string::npos ? std::string{} : s.substr(p + 6, 36);
}
void set_scheme(const std::string& g) { std::system(("powercfg /setactive " + g).c_str()); }
}  // namespace

int main() {
    char* env = nullptr;
    std::size_t n = 0;
    _dupenv_s(&env, &n, "EF_ALLOW_MUTATION");
    const bool allow = env && std::string(env) == "1";
    free(env);
    if (!allow) {
        std::printf("experiment_probe: skipped (set EF_ALLOW_MUTATION=1)\n");
        return 0;
    }
    using namespace ef::core::perf;
    const std::string orig = active_scheme();
    const std::string saver = "a1841308-3541-4fab-bc81-f71556f20b4a", high = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
    std::atomic<bool> cancel{false};
    ef::benchmark::SceneConfig c;
    c.tier = ef::benchmark::Tier::Low;
    c.frames = 600;
    std::vector<RunOutcome> runs;
    for (const auto& slot : abba_schedule(6, 1)) {
        const Arm arm = slot.value_or(Arm::A);
        set_scheme(arm == Arm::A ? saver : high);
        Sleep(500);
        const auto r = ef::benchmark::run_scene(c, cancel);
        if (!slot) continue;  // isinma
        const double sum = std::accumulate(r.frame_ms.begin(), r.frame_ms.end(), 0.0);
        const double fps = r.ok && sum > 0 ? 1000.0 * r.frame_ms.size() / sum : 0;
        runs.push_back({arm, "scene_v1|low|600|" + std::to_string(r.workload_hash), r.ok, fps});
        std::printf("  run %c fps=%.1f\n", arm == Arm::A ? 'A' : 'B', fps);
    }
    set_scheme(orig);
    const auto e = evaluate_experiment(runs);
    std::printf("experiment_probe: verdict=%s valid_a=%zu valid_b=%zu mean_a=%.1f mean_b=%.1f ci=[%.1f, %.1f] restored=%d\n",
                std::string(to_string(e.verdict)).c_str(), e.valid_a, e.valid_b, e.fps.mean_before.value_or(0), e.fps.mean_after.value_or(0),
                e.fps.ci_low.value_or(0), e.fps.ci_high.value_or(0), active_scheme() == orig);
    return active_scheme() == orig ? 0 : 1;
}
