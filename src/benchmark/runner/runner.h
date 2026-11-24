#pragma once
// BENCH-06: Uygulama Testi orkestrasyonu.
//  - Kalibrasyon (calibrate_scene: Low -> Medium -> High, GPU tamamlama araligi gecerliyse o) -> tier
//    secimi -> tier/seed/cozunurluk KILITLENIR (LockedPlan).
//  - Isinma ayni kosuda atilir; kilitli kosu skorlanir. A/B arasinda plan sessiz degismez (parmak izi).
//  - Skor serisi scored_frame_ms / ScoredFrames: GPU tamamlama araligi ya da (yedek) gonderim araligi;
//    hangisi oldugu <onek>frame_source_gpu metrigiyle, dislanan kare ve kapsam ile birlikte yazilir.
//  - Alt testler: sahne (GPU), CPU, RAM, depolama (istege bagli birim), birlesik (sahne + akan okuma).
//  - Her alt test kendi durumunu tasir: completed / partial / skipped / unavailable / cancelled / failed.
//  - Iptal tum alt testlere yayilir; alt test zaman asimi bekci ile iptale cevrilir; gecici kaynaklar
//    her alt testin kendi RAII temizligiyle birakilir.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "benchmark/cpu/cpu_verify.h"
#include "benchmark/scene/gpu_verify.h"
#include "benchmark/storage/storage_rules.h"
#include "core/measure/verify_state.h"
#include "benchmark/scene/scene.h"

namespace ef::benchmark {

enum class SubtestStatus { Completed, Partial, Skipped, Unavailable, Cancelled, Failed };
std::string_view to_string(SubtestStatus s) noexcept;

// On olcum ortalama kare suresinden tier: hedef butce icinde kalan en yuksek tier (saf).
// Low on olcum kare suresi x tier orani (ornek sayisi orani) tahmini kullanilir; tahmin yalniz secim icindir.
Tier select_tier(double low_tier_avg_frame_ms, double target_frame_ms) noexcept;

struct LockedPlan {
    SceneConfig scene;
    std::uint32_t warmup_frames = 120;
    double cpu_seconds_per_variant = 2.0;
    std::wstring storage_volume;  // bos: depolama atlanir
    // Plan v3.5: Hizli (~1 dk) / Tam (~5 dk). Tam: uzun pencereler, CPU/RAM/disk "full" on ayarlari.
    bool full = false;
    std::string fingerprint() const;  // tier/seed/cozunurluk/kare/surumler
};

struct SubtestResult {
    std::string id;  // "scene", "cpu", "memory", "storage", "combined"
    SubtestStatus status = SubtestStatus::Skipped;
    std::string reason;
    double seconds = 0;
    std::vector<std::pair<std::string, double>> metrics;  // ad -> deger (birim adda)
    // Yalniz sonuc kartindaki metin parametreleri (gecmise metrik olarak yazilmaz). Bosluk 7: bos alan yuzunden
    // atlanan depolama testi core::measure::storage_free_space_args() ciktisini (kStorageNeedFreeBytesArg /
    // kStorageAvailFreeBytesArg; olculmus byte, ondalik metin) tasir.
    std::vector<std::pair<std::string, std::string>> args;
};

struct BenchmarkReport {
    LockedPlan plan;
    std::string plan_fingerprint;
    std::vector<SubtestResult> subtests;
    bool cancelled = false;
    // DIAG-11: sahne olcum penceresi (harici PDH ornekleyicisinin zamanlamasi icin). Yalniz sahne
    // gercekten olculduyse gecerlidir; aksi halde begin == end (bos).
    std::chrono::steady_clock::time_point scene_measure_begin{}, scene_measure_end{};
    bool scene_measured = false;
    std::vector<double> scene_fps_series;  // saniye basina gercek FPS; sahne olculmediyse bos
};

struct RunnerOptions {
    double target_frame_ms = 4.0;     // tier secim butcesi (ekran disi, 240 FPS)
    double subtest_timeout_s = 120;   // bekci: asilirsa iptal
    std::wstring storage_volume;
    bool full = false;                // Plan v3.5: Tam test
    std::function<void(const std::string& stage, double fraction)> progress;  // gercek asama orani
    FrameTap* frame_tap = nullptr;  // EF-X15: yalniz sahne alt testine baglanir; skor/plan degismez
    // Canli test ekrani (spec 2026-11-12 A4): sahne alt testinin onizleme kareleri. nullptr = uretilmez (varsayilan;
    // davranis aynen). Aciksa kosu "onizleme acik" kosuluyla isaretlenmelidir (cagiran); plan / parmak izi degismez.
    const PreviewCallback* preview = nullptr;
    // EF-X22: sahne olcumunden sonra GPU compute/geri okuma dogrulamasi (Hizli ~3 sn, Tam ~10 sn ust sinir).
    // Uyusmazlik sahne skorunu siler (gpu_compute_mismatch); yalniz testlerde kapatilir.
    bool gpu_verify = true;
    // EF-X22: CPU olcumunden sonra degisken yuk + CPUID etiketli SIMD dogrulamasi (Hizli ~3 sn, Tam ~8 sn).
    // Uyusmazlik CPU skorunu siler (cpu_compute_mismatch); yalniz testlerde kapatilir.
    bool cpu_verify = true;
};

// Sahne alt testinin tek kosu yapilandirmasi (saf): kare kipinde plan.warmup_frames ayni cihazda isinma
// olarak eklenir (ayri isinma kosusu yok; warmup_frames 0/1 ise olcum yine plan.scene.frames kadardir).
SceneConfig scene_run_config(const LockedPlan& plan);

// EF-X22: tamamlanmis sahne alt testine GPU dogrulama sonucunu uygular (saf). Pass: dogrulama metrikleri eklenir.
// NoClaim (cihaz yok/kurulamadi): skor kalir, dogrulama metrigi YAZILMAZ. FailNoScore: sahne metrikleri ve FPS serisi
// silinir, durum Failed (gpu_compute_mismatch / device_removed / ...). Partial: kismi. Iptal: zaman asimi -> Partial
// "timeout", aksi halde Cancelled "user_cancel"; skor silinir.
void apply_gpu_verify(SubtestResult& r, BenchmarkReport& rep, const gpu_verify::Result& v, bool watchdog_timed_out);

// EF-X22: tamamlanmis CPU alt testine islemci dogrulama sonucunu uygular (saf). Ok: sayaclar + durum kodu.
// Mismatch: CPU metrikleri silinir, Failed "cpu_compute_mismatch". Incomplete: Partial "cpu_verify_incomplete" (skor
// kalir, "dogrulandi" denmez). Invalid: Failed. Iptal: skor silinir; zaman asimi Partial "timeout", aksi Cancelled.
void apply_cpu_verify(SubtestResult& r, const cpu_verify::Result& v, bool watchdog_timed_out);
// EF-X22: depolama dogrulama fazinin gecmis durum kodu (saf); faz kosulmadiysa (ve kurulum hatasi yoksa) bos.
std::optional<core::measure::VerifyCode> storage_verify_code(storage::VerifyStatus s, std::string_view reason) noexcept;

// Kilitli plani on olcumle kurar (sahne GPU yoksa Medium varsayilir ve "unavailable" sonucu tasinir).
LockedPlan make_plan(const RunnerOptions& o, const std::atomic<bool>& cancel);
// Kilitli plani yurutur. Ayni plan iki kez -> ayni parmak izi.
BenchmarkReport run_benchmark(const LockedPlan& plan, const RunnerOptions& o, std::atomic<bool>& cancel);

}  // namespace ef::benchmark
