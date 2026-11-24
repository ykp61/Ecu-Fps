// MEAS-02 gercek tur araci (ctest disi; yonetici PowerShell'de, oyun acikken calistirilir):
//   ef_game_capture.exe <exe_adi_veya_pid> [saniye=30]
// Ornek: ef_game_capture.exe cs2.exe 30   -> oyun surecini bulur, kimligi dogrular, ETW ile kare olcer.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "core/diagnosis/game_limit.h"
#include "core/product_info.h"
#include "measurement/frame/game_capture.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("kullanim: ef_game_capture <exe_adi|pid> [saniye]\n");
        return 2;
    }
    DWORD pid = static_cast<DWORD>(std::wcstoul(argv[1], nullptr, 10));
    if (pid == 0) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{sizeof(pe)};
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            if (_wcsicmp(pe.szExeFile, argv[1]) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        CloseHandle(snap);
    }
    if (pid == 0) {
        std::printf("surec bulunamadi\n");
        return 3;
    }
    const double secs = argc > 2 ? std::wcstod(argv[2], nullptr) : 30.0;
    const auto id = ef::measurement::query_identity(pid);
    std::printf("build_id=%s\n", std::string(ef::core::build_id()).c_str());  // kanit: hangi derleme olctu
    std::printf("hedef: pid=%lu path=%s\n", pid, id.image_path.c_str());
    std::atomic<bool> cancel{false};
    const auto r = ef::measurement::capture_game(id, secs, cancel, std::string(ef::core::measure::exe_name(id.image_path)), secs * 0.5);
    if (!r.start.ok()) {
        std::printf("etw: %s (win32=%lu) — yonetici PowerShell gerekir\n", r.start.error == ef::measurement::FrameSessionError::AccessDenied ? "access_denied" : r.start.detail.c_str(), r.start.win32);
        return 4;
    }
    std::printf("target=%s render_frames=%zu displayed=%zu generated=%zu other_swapchains=%zu validity=%s dropped=%llu\n",
                std::string(ef::core::measure::to_string(r.target)).c_str(), r.summary.render_frames, r.summary.displayed_frames,
                r.summary.generated_frames, r.summary.other_swapchains, std::string(ef::core::measure::to_string(r.summary.validity)).c_str(),
                static_cast<unsigned long long>(r.dropped_presents));
    if (r.stats.sufficient)
        std::printf("avg_fps=%.1f low1_fps=%.1f p95_ms=%.2f p99_ms=%.2f n=%zu (%s)\n", *r.stats.avg_fps, *r.stats.low1_fps, *r.stats.p95_ms, *r.stats.p99_ms,
                    r.stats.n, std::string(ef::core::measure::kLowDefinition).c_str());
    else
        std::printf("veri yetersiz (n=%zu)\n", r.stats.n);
    char exited[32] = "none";
    if (r.target_exited_after_s) std::snprintf(exited, sizeof exited, "%.2f", *r.target_exited_after_s);
    std::printf("outcome=%s exited_after_s=%s exited_after_window=%d api_confidence=%s display_intervals=%zu\n",
                std::string(ef::core::measure::to_string(r.outcome)).c_str(), exited, r.target_exited_after_window ? 1 : 0,
                std::string(ef::core::measure::to_string(ef::core::measure::api_confidence(r.summary.runtime))).c_str(),
                r.summary.display_intervals_ms.size());
    // Spec 2026-11-12 B (D5): kare basi GPU mesgul serisi ve "bu oyunda ne sinirladi" kurali. Bu arac ekran yenileme
    // hizini ve HAGS durumunu okumaz (urun okur): kare siniri yalniz yaygin sinirlara gore denenir. Olculmeyen "null".
    {
        ef::core::diagnosis::GameLimitInput gl;
        gl.intervals_ms = r.summary.render_intervals_ms;
        gl.gpu_busy_ms = r.summary.render_gpu_busy_ms;
        gl.avg_fps = r.stats.avg_fps;
        gl.run_valid = r.manifest.valid;
        const auto g = ef::core::diagnosis::evaluate_game_limit(gl);
        const auto num = [](const std::optional<double>& v, int decimals) {
            if (!v) return std::string("null");
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", decimals, *v);
            return std::string(b);
        };
        std::printf("game_limit rules=%s gpu_busy_frames=%zu windows=%zu full_windows=%zu waited_windows=%zu full_pct=%s waited_pct=%s "
                    "busy_ratio=%s verdict=%s reason=%s cap_fps=%s\n",
                    std::string(ef::core::diagnosis::kGameLimitRules).c_str(), r.summary.render_gpu_busy_ms.size(), g.windows, g.full_windows,
                    g.waited_windows, num(g.full_pct, 1).c_str(), num(g.waited_pct, 1).c_str(), num(g.busy_ratio, 3).c_str(),
                    std::string(ef::core::diagnosis::to_string(g.verdict)).c_str(), std::string(ef::core::diagnosis::to_string(g.reason)).c_str(),
                    num(g.cap_fps, 0).c_str());
    }
    // Dogrulama bulgusu 3: kayip sayaclari ayri (T02); manifest_loss_total yalniz gecerlilik toplamidir.
    std::printf("%s\n", ef::core::measure::capture_counters_line(ef::core::build_id(), r.loss).c_str());
    std::printf("manifest valid=%d exclusions=%zu\n", r.manifest.valid, r.manifest.exclusions.size());
    for (const auto& e : r.manifest.exclusions) std::printf("  - %s\n", std::string(ef::core::measure::to_string(e)).c_str());
    // BM-01: sonuc gorunumu ayni dusuk guven sinyalini tasir (UI ham kod gostermez; anahtarlar katalogda).
    std::printf("view run_valid=%d confidence=%s evidence=%zu\n", r.view.run_valid,
                std::string(ef::contracts::to_string(r.view.confidence)).c_str(), r.view.evidence_keys.size());
    for (const auto& k : r.view.evidence_keys) std::printf("  - %s\n", k.c_str());
    return 0;
}
