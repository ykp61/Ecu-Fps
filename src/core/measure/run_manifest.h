#pragma once
// MEAS-04: kosu manifestosu ve parmak izi (saf). docs/05_BENCHMARK_SPEC: her kosu kaynak, surumler,
// ayarlar, ornek sayisi, gecerli sure, eksik olay ve olcum kaynaklarini tasir. Parmak izi yalniz
// "ayni kosul" karsilastirmasi icindir (A/B); kimlik/seri numarasi icermez.

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string_view>
#include <map>
#include <string>
#include <vector>

#include "core/measure/frame_stats.h"
#include "core/measure/frames.h"

namespace ef::core::measure {

enum class Exclusion {
    None,
    EtwEventsLost,
    TooFewFrames,
    TargetMismatch,
    Overlay,
    RuntimeMixed,    // ayni surecte birden cok swapchain/runtime: kare serisi karisik (dusuk guven)
    RuntimeOther,    // DXGI/D3D9 disi veya bilinmeyen runtime: yorum dogrulanamadi (dusuk guven)
    RuntimeUnknown,  // geriye uyum: bilinmeyen runtime (RuntimeOther ile ayni sonuc)
    Cancelled,
    TooShort,
    GameExited  // EF-X18/T04: hedef oyun olcum bitmeden kapandi/coktu; kosu tamamlanmadi
};
std::string_view to_string(Exclusion e) noexcept;

struct RunManifest {
    int schema_version = 1;
    std::string source;                 // "builtin" / "in_game"
    std::string app;                    // oyun/uygulama kimligi (exe adi + surum)
    std::map<std::string, std::string> context;  // os_build, driver, gpu, resolution, vsync, vrr, cap, frame_gen, power, hags...
    std::string stats_definition{kLowDefinition};
    std::string presentmon_version = "2.6.0";
    double valid_seconds = 0;
    std::size_t intervals = 0, excluded_intervals = 0;
    std::uint32_t events_lost = 0;
    std::vector<Exclusion> exclusions;
    bool valid = false;
};

// Ayni-kosul parmak izi: source + app + context anahtarlari (sirali) + istatistik tanimi. FNV-1a 64, hex.
std::string fingerprint(const RunManifest& m);

// Ozet + istatistikten manifest doldurur; gecersizlik nedenlerini Exclusion'a cevirir.
// min_seconds altindaki kosu TooShort (ornek: 60 sn testte 10 sn).
RunManifest make_manifest(std::string source, std::string app, std::map<std::string, std::string> context, const FrameSummary& s,
                          const FrameStats& st, std::uint32_t events_lost, double min_seconds, bool cancelled);

// EF-X18/T04: yakalama sonucu (core::perf::RunOutcome ile karismasin diye CaptureOutcome).
// Complete yalniz hedef surec kosu boyunca yasadi, kimligi degismedi ve iptal yoksa.
enum class CaptureOutcome { NotStarted, Complete, Cancelled, GameExited, TargetChanged };
std::string_view to_string(CaptureOutcome o) noexcept;
CaptureOutcome classify_capture_outcome(bool cancelled, bool target_exited, bool identity_ok) noexcept;
// Complete disindaki her sonuc manifestoyu gecersiz kilar; ilgili dislama (tekrarsiz) eklenir.
void apply_capture_outcome(RunManifest& m, CaptureOutcome o);
// EF-X18 dogrulama bulgusu 2: hedef cikisi yalniz yakalama penceresi (seconds) dolmadan gozlendiyse
// erken cikistir (GameExited). Pencere dolduktan sonra (durdurma/son bosaltma sirasinda) gozlenen cikis
// kosuyu bozmaz; bilgi olarak exited_after_window ile tasinir. in_loop_exit_s: dongu icinde gozlenen an;
// exited_after_stop: durdurmadan sonra surec bitmis mi; loop_end_s: dongunun bittigi an; now_s: simdiki an.
struct TargetExitObservation {
    std::optional<double> early_exit_s;  // doluysa GameExited
    bool exited_after_window = false;    // tam pencereden sonra cikti (bilgi; sonucu degistirmez)
};
TargetExitObservation resolve_target_exit(std::optional<double> in_loop_exit_s, bool exited_after_stop, double loop_end_s, double seconds,
                                          double now_s) noexcept;

// EF-X18 dogrulama bulgusu 3: kayip sayaclari AYRI tutulur. Manifestonun events_lost alani bunlarin toplamidir
// (herhangi bir kayip kosuyu gecersiz kilar); kanit kaydi ise her sayaci ayri yazar, dropped iki kez sayilmaz.
struct CaptureLossCounters {
    std::uint32_t events_lost = 0;          // ETW kayip olay
    std::uint32_t buffers_lost = 0;         // ETW kayip tampon
    std::uint32_t overflowed_presents = 0;  // tuketici halkasi tasmasi
    std::uint64_t dropped_presents = 0;     // PresentData'nin dusurdugu present
};
// StreamHealth.events_lost karsiligi: events + overflowed + dropped (32 bitte doyar).
std::uint32_t stream_events_lost(const CaptureLossCounters& c) noexcept;
// Manifesto toplam kaybi: stream_events_lost + buffers_lost (32 bitte doyar).
std::uint32_t manifest_loss_total(const CaptureLossCounters& c) noexcept;
// Kanit satiri: "build_id=.. events_lost=.. buffers_lost=.. overflowed=.. dropped=.. manifest_loss_total=..".
// build_id bos, 64+ karakter veya bosluk/kontrol karakterli ise "unknown" yazilir.
std::string capture_counters_line(std::string_view build_id, const CaptureLossCounters& c);

// EF-X18: ayni-kosul baglami. Bos/gozlenemeyen/ASCII-disi/kontrol karakterli/160+ deger "unknown" (tahmin yok).
inline constexpr std::string_view kUnknownContext = "unknown";
struct RunContextInput {
    std::string scene, game_build, resolution, render_scale, fps_limit, sync, upscaler, frame_gen;  // ayni-kosul zorunlu
    std::string renderer, api_confidence, os_build, gpu_driver, collector_commit;                // kapsam/bilgi
};
std::map<std::string, std::string> make_run_context(const RunContextInput& in);
// Zorunlu anahtarlardan (scene..frame_gen) biri eksik/"unknown" ise false: ayni-kosul karsilastirmasi yapilmaz.
bool context_comparable(const RunManifest& m);

// EF-X18/T03: olcum yuku acik/kapali eslesmis kosular. FPS kaynagi iki kolda AYNI ve ETW'den BAGIMSIZ
// (oyunun kendi benchmark sonucu). Toplayici kapaliyken ETW FPS'i yoktur, uydurulmaz. Esik/"dusuk yuk"
// hukmu yok; yalniz olculen fark. condition_fingerprint: iki kolun kayitli ayarlarindan fingerprint().
// AppSceneInternal yalniz modeldedir: uygulama sahnesi ekran disi cizer (Present yok) ve ecufps.exe
// overlay listesindedir; gercek T03 turunda kullanilamaz.
enum class OverheadSource { AppSceneInternal, GameReported };
struct OverheadArm {
    bool collector_on = false;
    OverheadSource source = OverheadSource::GameReported;
    std::string condition_fingerprint;
    bool run_valid = false;
    std::optional<double> avg_fps;
    std::optional<double> p99_ms;
};
struct OverheadPair {
    OverheadArm on, off;
};
enum class OverheadVerdict { Measured, InsufficientPairs, NotComparable };
// EF-X18 dogrulama bulgusu 4: once her cift ayri denetlenir; gecersiz cift ATILIR (kume reddedilmez) ve nedeni
// discard_reasons'a yazilir (arm_order | pair_invalid | source_mismatch | condition_mismatch | missing_fps).
// Kalan gecerli ciftler kMinOverheadPairs altindaysa InsufficientPairs/too_few_pairs. Gecerli ciftler arasinda
// kosul izi/kaynak farkliysa kume NotComparable (condition_mismatch | source_mismatch): farkli sahneler
// birlestirilmez. Kaynak AppSceneInternal ise gercek hukum yok: NotComparable/model_only_source.
// p99 farki yalniz tum gecerli ciftlerde iki kolda da sonlu ve > 0 p99 varsa hesaplanir.
struct OverheadComparison {
    OverheadVerdict verdict = OverheadVerdict::NotComparable;
    std::string reason;  // too_few_pairs | condition_mismatch | source_mismatch | model_only_source (Measured: bos)
    std::size_t pairs_used = 0;
    std::size_t pairs_discarded = 0;
    std::vector<std::string> discard_reasons;  // atilan her cift icin, giris sirasiyla
    std::optional<double> median_avg_fps_delta_pct, min_avg_fps_delta_pct, max_avg_fps_delta_pct;  // (on-off)/off*100
    std::optional<double> median_p99_delta_ms;  // yalniz tum ciftlerde p99 varsa
};
inline constexpr std::size_t kMinOverheadPairs = 3;
OverheadComparison compare_overhead(const std::vector<OverheadPair>& pairs);

}  // namespace ef::core::measure
