// BENCH-06: tam Uygulama Testi akisi (gercek GPU/CPU/RAM; depolama %TEMP% birimi) + iptal yayilimi.
#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>

#include "benchmark/runner/runner.h"

using namespace ef::benchmark;

namespace {
void print(const BenchmarkReport& r) {
    std::printf("plan: %s cancelled=%d\n", r.plan_fingerprint.c_str(), r.cancelled);
    for (const auto& s : r.subtests) {
        std::printf("  %-9s %-11s %6.2fs %s\n", s.id.c_str(), std::string(to_string(s.status)).c_str(), s.seconds, s.reason.c_str());
        for (const auto& [k, v] : s.metrics) std::printf("      %s=%.3f\n", k.c_str(), v);
    }
}
std::wstring temp_volume() {
    wchar_t t[MAX_PATH]{};
    GetTempPathW(MAX_PATH, t);
    return std::wstring(t, 3);  // "C:\"
}
bool orphan_left(const std::wstring& vol) {
    // Windows klasor silmeyi acik tutamaclar kapaninca tamamlar (bekleyen silme): kisa sure yeniden dene.
    for (int i = 0; i < 20; ++i) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((vol + L"EcuFps-bench-*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        FindClose(h);
        Sleep(100);
    }
    std::wprintf(L"orphan: %ls\n", vol.c_str());
    return true;
}
}  // namespace

int main() {
    RunnerOptions o;
    o.storage_volume = temp_volume();
    // EF_REAL_FULL=1: uygulamadaki gercek Tam test (~5 dk, Plan v3.5 "full" on ayarlari).
    char envf[8] = {};
    const bool real_full = GetEnvironmentVariableA("EF_REAL_FULL", envf, sizeof envf) > 0 && envf[0] == '1';
    o.full = real_full;
    o.subtest_timeout_s = real_full ? 300 : 120;  // app.cpp ile ayni bekci
    std::atomic<bool> cancel{false};
    const auto plan = make_plan(o, cancel);
    LockedPlan p = plan;
    // EF_REAL_QUICK=1: uygulamadaki gercek Hizli test (on ayarlar, sure tabanli sahne); aksi halde kisaltilmis.
    char env[8] = {};
    const bool real = real_full || (GetEnvironmentVariableA("EF_REAL_QUICK", env, sizeof env) > 0 && env[0] == '1');
    std::printf("benchmark_flow: mode=%s\n", real_full ? "full" : (real ? "quick" : "short"));
    if (!real) p.cpu_seconds_per_variant = 0.5, p.scene.measure_s = 1.0, p.scene.warmup_s = 0.5;  // test suresi kisa
    const auto t_all = GetTickCount64();
    const auto rep = run_benchmark(p, o, cancel);
    print(rep);
    std::printf("benchmark_flow: total %.1f s (real_quick=%d)\n", static_cast<double>(GetTickCount64() - t_all) / 1000.0, real ? 1 : 0);
    if (real) return 0;  // gercek kosu: yalniz olcum ciktisi (iptal turu kisaltilmis modda)
    if (rep.subtests.size() != 5) return 1;
    for (const auto& s : rep.subtests)
        if (s.status == SubtestStatus::Failed) return 1;
    if (orphan_left(o.storage_volume)) {
        std::printf("orphan storage folder left\n");
        return 1;
    }
    // Ayni plan -> ayni parmak izi (sessiz workload degisimi yok)
    if (p.fingerprint() != rep.plan_fingerprint) return 1;
    // Iptal: 1,5 sn sonra -> kalan alt testler calismaz, gecici dosya kalmaz
    std::atomic<bool> stop{false};
    std::thread t([&] {
        Sleep(1500);
        stop = true;
    });
    const auto c = run_benchmark(p, o, stop);
    t.join();
    print(c);
    bool any_cancel = false;
    for (const auto& s : c.subtests) any_cancel |= s.status == SubtestStatus::Cancelled;
    if (!c.cancelled || !any_cancel || c.subtests.size() >= 5) return 1;
    if (orphan_left(o.storage_volume)) return 1;
    std::printf("benchmark_flow: OK\n");
    return 0;
}
