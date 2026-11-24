// P9/B5: Uygulama Testi (Genel kapsam). app_measure.cpp'den ayrildi; davranis degismedi.
#include "app/measure_controller.h"

#include "app/endurance_report_map.h"
#include "app/measure_sampling.h"
#include "app/measure_support.h"
#include "app/worker.h"
#include "benchmark/balance/balance_checks.h"  // spec 2026-11-12 C1/C2: salt okuma uyum denetimleri
#include "benchmark/balance/balance_runner.h"  // spec 2026-11-12 A5: denge testi kosucusu
#include "benchmark/endurance/endurance_runner.h"
#include "core/adapters/windows/display_process.h"
#include "core/diagnosis/balance.h"
#include "core/diagnosis/calibration.h"
#include "core/diagnosis/components.h"
#include "core/diagnosis/game_limit.h"
#include "core/diagnosis/graphics_api.h"
#include "core/diagnosis/mismatch.h"
#include "core/diagnosis/platform.h"
#include "core/diagnostics/incident_engine.h"
#include "core/games/process_risk.h"
#include "core/inventory/power_security.h"
#include "core/insights/history_compare.h"
#include "core/measure/balance_log.h"
#include "core/measure/bench_text.h"
#include "core/measure/run_marker.h"
#include "core/metrics/sampling.h"
#include "core/metrics/sensor.h"
#include "core/perf/admin_plan.h"
#include "core/readiness/readiness.h"
#include "measurement/frame/capture_result.h"
#include "measurement/telemetry/adapter_gpu.h"
#include "measurement/telemetry/process_gpu.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/adapters/win_power.h"
#include "platform/windows/capture_channel.h"
#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/inventory/cpu_sets.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/inventory/power_security_win.h"
#include "platform/windows/inventory/windows_features.h"
#include "platform/windows/metrics/balance_check_sampler.h"
#include "platform/windows/metrics/pdh_sampler.h"
#include "platform/windows/net/speedtest_http.h"
#include "platform/windows/nvidia/nvapi_drs.h"
#include "platform/windows/readiness/background_reader.h"
#include "platform/windows/readiness/display_reader.h"
#include "platform/windows/readiness/storage_reader.h"
#include "platform/windows/run_key_store.h"

namespace ef::app {

using namespace detail;

void MeasureController::start_builtin_test() {
    using core::measure::TestPhase;
    if (env_.uninstall_running()) return;  // R20: veri silinirken test baslamaz
    if (vm_.measure.phase == TestPhase::Preparing || vm_.measure.phase == TestPhase::Running || vm_.measure.phase == TestPhase::Stopping) return;
    if (vm_.measure.endurance.running) return;  // R13: uzun test ile ayni anda ikinci test yok
    if (vm_.speed.running) return;              // tek aktif test: hiz testi agi/islemciyi kullanirken olcum yok
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // oyun calisirken agir test yok
    if (bench_worker_.joinable()) bench_worker_.join();
    bench_cancel_ = false;
    vm_.measure.source = core::measure::TestSource::Builtin;
    vm_.measure.gate_stopped = false;
    vm_.measure.phase = TestPhase::Preparing;
    vm_.measure.stage_key = "measure.stage_prepare";
    vm_.measure.elapsed_s = 0;
    vm_.measure.total_s = 0;  // toplam sure bilinmiyor: sahte yuzde gosterilmez
    vm_.measure.started_ms = static_cast<std::int64_t>(GetTickCount64());  // M04: UI 1 Hz ile gecen sureyi buradan yazar
    vm_.measure.fail_reason.clear();
    // EF-X23: veri riski kaniti olan diskte yazma testi calismaz (once yedek).
    vm_.measure.storage_blocked = false;
    // M11: onceki sonuc yeni sonuc gelene kadar gorunur kalir ("Onceki sonuc"); iptal/hata onu silmez.
    publish_measure();
    env_.begin_run_marker("builtin");
    bench_worker_ = spawn_worker([this, choice = vm_.measure.storage_choice, roots = vm_.measure.storage_roots,
                                 full = vm_.measure.mode == core::measure::TestMode::Full,
                                 hwnd = env_.window_hwnd()] {
        // Uyumluluk B3 (bosluk 25): test suresince bu surec EcoQoS'tan cikar; is parcacigi bitince onceki durum geri yazilir.
        platform::adapters::WinSelfThrottleApi self_throttle;
        core::adapters::windows::ScopedExecutionSpeedOptOut eco_opt_out(self_throttle);
        const auto t0 = GetTickCount64();
        benchmark::RunnerOptions o;  // depolama birimi secilmediyse depolama/birlesik "atlandi" (yazma yok)
        if (choice > 0 && choice <= roots.size()) {
            // EF-X23: karar taze okumayla; Oyun Hazirligi hic acilmamis olabilir. Kanit yalniz secilen diski engeller.
            const auto health = platform::readiness::read_storage_health();
            auto checks = core::readiness::storage_health_checks(health);
            for (auto& c : core::diagnostics::incident_checks(core::diagnostics::analyze_incidents(platform::diagnostics::read_system_incidents()))) checks.push_back(std::move(c));
            if (core::readiness::storage_write_stress_allowed(checks, core::readiness::disk_of_volume(health, roots[choice - 1]))) o.storage_volume = platform::to_wide(roots[choice - 1]);
            else post([this] {
                vm_.measure.storage_blocked = true;
                publish_measure();
            });
        }
        o.full = full;  // plan v3.5: Hizli / Tam
        o.subtest_timeout_s = full ? 300 : 120;
        o.progress = [this, t0](const std::string& stage, double) {
            const double el = static_cast<double>(GetTickCount64() - t0) / 1000.0;
            post([this, stage, el] {
                if (vm_.measure.phase == core::measure::TestPhase::Stopping) return;
                vm_.measure.phase = core::measure::TestPhase::Running;
                vm_.measure.stage_key = "measure.stage_" + stage;
                vm_.measure.elapsed_s = el;
                publish_measure();
            });
        };
        benchmark::BenchmarkReport rep;
        bool crashed = false;
        // DIAG-11: sahne olcum penceresi boyunca ~20 Hz PDH ornekleme (ayri is parcacigi; salt okuma,
        // yonetici gerekmez). Ornek zamanlari steady_clock: raporun sahne penceresiyle ayni saat.
        std::vector<std::pair<std::chrono::steady_clock::time_point, core::metrics::LiveSnapshot>> scene_samples;
        // B3 inceleme (bosluk 25): pencerenin simge durumunda goruldugu ornek anlari (pencere yoksa denetim yok).
        std::vector<std::chrono::steady_clock::time_point> minimized_at;
        try {
            const auto plan = benchmark::make_plan(o, bench_cancel_);
            std::atomic<bool> sampling{true};
            std::thread sampler = spawn_worker([&scene_samples, &minimized_at, &sampling, hwnd] {
                platform::metrics::PdhSampler pdh;
                const bool open = pdh.open();
                while (sampling.load(std::memory_order_relaxed)) {
                    const auto now = std::chrono::steady_clock::now();
                    if (hwnd && IsIconic(hwnd)) minimized_at.push_back(now);
                    scene_samples.emplace_back(now, open ? pdh.sample() : core::metrics::LiveSnapshot{});
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
            try {
                rep = benchmark::run_benchmark(plan, o, bench_cancel_);
            } catch (...) {
                sampling = false;  // iptal/hata yolunda da ornekleyici kapatilir (joinable terminate yok)
                if (sampler.joinable()) sampler.join();
                throw;
            }
            sampling = false;
            if (sampler.joinable()) sampler.join();
        } catch (...) {
            crashed = true;  // M11: neden + sonraki adim gosterilir; ayar degismez
        }
        // DIAG-11: yalniz sahnenin GERCEK olcum penceresine dusen ornekler ortalanir. Pencere yoksa
        // (sahne kosmadi/iptal) deger uydurulmaz: measurement missing(NotSampled) kalir.
        std::vector<contracts::Measurement> gpu_samples, cpu_samples, cpu_core_samples;
        std::optional<double> scene_sample_ms;
        bool scene_minimized = false;  // B3 inceleme (bosluk 25)
        if (rep.scene_measured) {
            for (const auto t : minimized_at)
                if (t >= rep.scene_measure_begin && t <= rep.scene_measure_end) scene_minimized = true;
            for (const auto& [t, snap] : scene_samples) {
                if (t < rep.scene_measure_begin || t > rep.scene_measure_end) continue;
                gpu_samples.push_back(snap.gpu_pct);
                cpu_samples.push_back(snap.cpu_pct);
                cpu_core_samples.push_back(snap.cpu_max_core_pct);
            }
            // MEAS-03: gercek ortalama ornek araligi (pencere suresi / ornek sayisi). Tek ornekte anlamsiz.
            if (gpu_samples.size() >= 2) {
                const double dur_ms =
                    std::chrono::duration<double, std::milli>(rep.scene_measure_end - rep.scene_measure_begin).count();
                if (dur_ms > 0) scene_sample_ms = dur_ms / static_cast<double>(gpu_samples.size());
            }
        }
        const auto scene_gpu = core::metrics::mean_measured(gpu_samples);
        const auto scene_cpu = core::metrics::mean_measured(cpu_samples);
        const auto scene_cpu_core = core::metrics::mean_measured(cpu_core_samples);
        // DIAG-10 Ar-Ge kancasi.
        if (!crashed) maybe_export_calibration(rep, scene_gpu, scene_cpu_core, scene_sample_ms);
        post([this, rep = std::move(rep), t0, crashed, scene_gpu, scene_cpu, scene_cpu_core, scene_sample_ms, scene_minimized] {
            env_.end_run_marker();
            using core::measure::TestPhase;
            vm_.measure.elapsed_s = static_cast<double>(GetTickCount64() - t0) / 1000.0;
            vm_.measure.stage_key.clear();
            if (crashed) {
                vm_.measure.phase = TestPhase::Failed;
                vm_.measure.fail_reason = "internal_error";
                publish_measure();
                return;
            }
            if (rep.cancelled) {
                vm_.measure.phase = vm_.measure.gate_stopped ? TestPhase::Failed : TestPhase::Cancelled;
                if (vm_.measure.gate_stopped) vm_.measure.fail_reason = "game_started";  // "kullanici durdurdu" denmez
                vm_.measure.gate_stopped = false;
                publish_measure();
                return;
            }
            // M02: Uygulama Testi teshis degildir; baslik alt test durumlarindan, guven satiri yok.
            std::vector<core::measure::SubtestOutcome> outcomes;
            for (const auto& s : rep.subtests) outcomes.push_back({s.id, std::string(benchmark::to_string(s.status)), s.reason});
            const auto summary = core::measure::builtin_summary(outcomes);
            if (!summary.any_data) {
                // M11: hicbir bolum veri uretmedi: sonuc yazilmaz, onceki sonuc korunur, neden + sonraki adim gosterilir.
                vm_.measure.phase = TestPhase::Failed;
                vm_.measure.fail_reason = summary.fail_reason;
                publish_measure();
                return;
            }
            core::measure::ResultView r;
            r.source = core::measure::TestSource::Builtin;
            // M03: ic kimlik ("scene_v3 medium") UI'da gosterilmez; yalniz kademe (yerellestirilmis) ve gecmis kaydi.
            const std::string internal_title = std::string(benchmark::kSceneVersion) + " " + std::string(benchmark::to_string(rep.plan.scene.tier));
            r.tier = std::string(benchmark::to_string(rep.plan.scene.tier));
            r.when_unix_ms = now_unix_ms();
            r.diagnosis = core::measure::Diagnosis::Insufficient;  // yerlesik sahne teshis girdisi degil
            r.headline_key = summary.headline_key;
            r.headline_args = summary.args;
            r.show_confidence = false;
            bool scene_ok = false;
            for (const auto& s : rep.subtests) {
                if (s.id != "scene") continue;
                scene_ok = s.status == benchmark::SubtestStatus::Completed;
                for (const auto& [k, v] : s.metrics) {
                    if (k == "scene_avg_fps") r.avg_fps = contracts::Measurement::of(v, contracts::Unit::Fps);
                    else if (k == "scene_low1_fps") r.low1_fps = contracts::Measurement::of(v, contracts::Unit::Fps);
                    else if (k == "scene_p99_ms") r.p99_ms = contracts::Measurement::of(v, contracts::Unit::Milliseconds);
                    else if (k == "scene_max_ms") r.worst_ms = contracts::Measurement::of(v, contracts::Unit::Milliseconds);
                }
                if (!scene_ok) {
                    // BM-05: Failed/Cancelled "örneklenmedi" değil kaynak hatası; Unavailable desteklenmiyor.
                    const auto why = core::measure::scene_missing_reason(std::string(benchmark::to_string(s.status)));
                    r.avg_fps = contracts::Measurement::missing(contracts::Unit::Fps, why);
                }
            }
            for (const auto& s : rep.subtests) r.evidence_keys.push_back("bench." + s.id + "." + std::string(benchmark::to_string(s.status)));
            if (scene_minimized) r.evidence_keys.push_back("bench.scene.window_minimized");  // B3 inceleme (bosluk 25)
            // BM-03/04: başlık güveni yalnız sahneye bakmaz; CPU/RAM durumu, kısmi/başarısız bölümler ve
            // ölçülmüş MT çekişmesi de hesaba katılır.
            {
                std::map<std::string, double> all_metrics;
                for (const auto& s : rep.subtests)
                    for (const auto& kv : s.metrics) all_metrics[kv.first] = kv.second;
                r.confidence = core::measure::result_confidence(outcomes, all_metrics);
            }
            {  // BENCH v2: bilesen kartlari (gercek metrikler; atlanan/basarisiz alt test nedeniyle)
                std::map<std::string, double> mm;
                std::map<std::string, core::measure::SubtestInfo> si;
                for (const auto& s : rep.subtests) {
                    for (const auto& [k, val] : s.metrics) mm[k] = val;
                    si[s.id] = {std::string(benchmark::to_string(s.status)), s.reason, {s.args.begin(), s.args.end()}};
                }
                // DIAG-11: sahne penceresi kaynak kullanimi yalniz gercekten olculduyse isaretlenir; isaret
                // yoksa (eski kayit, sahne kosmadi) kartlarda bu satirlar hic gorunmez. Deger eksikse
                // (PDH desteklemiyor) satir yine gorunur ve "Olculemedi" der - 0 yazilmaz.
                if (rep.scene_measured) {
                    mm["scene_util_sampled"] = 1;
                    if (scene_gpu.has_value()) mm["scene_gpu_pct"] = *scene_gpu.value();
                    if (scene_cpu.has_value()) mm["scene_cpu_pct"] = *scene_cpu.value();
                    if (scene_cpu_core.has_value()) mm["scene_cpu_max_core_pct"] = *scene_cpu_core.value();
                    if (scene_sample_ms) mm["scene_sample_interval_ms"] = *scene_sample_ms;
                    if (scene_minimized) mm["scene_window_minimized"] = 1;  // B3 inceleme (bosluk 25): kart notu
                }
                std::map<std::string, std::string> args{{"tier", std::string(benchmark::to_string(rep.plan.scene.tier))},
                                                        {"res", std::to_string(rep.plan.scene.width) + "x" + std::to_string(rep.plan.scene.height)},
                                                        {"volume", rep.plan.storage_volume.empty() ? std::string("-") : platform::to_utf8(rep.plan.storage_volume)}};
                r.components = core::measure::component_cards(mm, si, args);
                r.mode = rep.plan.full ? core::measure::TestMode::Full : core::measure::TestMode::Quick;
            }
            r.fps_series = rep.scene_fps_series;
            r.run_valid = scene_ok && !rep.cancelled;  // gecmis kaydiyla ayni karar; yeniden acilista degismez
            vm_.measure.phase = TestPhase::Done;
            vm_.measure.result = r;
            vm_.measure.history.insert(vm_.measure.history.begin(), r);
            cap_measure_history(vm_.measure.history);
            {
                core::insights::HistoryEntry he;
                he.when_unix_ms = r.when_unix_ms;
                he.source = "builtin";
                he.fps_series = rep.scene_fps_series;
                he.title = internal_title;  // kayit/manifest kimligi (UI kademe metnini gosterir)
                he.fingerprint = rep.plan_fingerprint;
                he.app_version = vm_.version;
                // B3: karsilastirma uyumlulugu icin surumler + kosu gecerliligi kayda yazilir.
                he.scene_version = std::string(benchmark::kSceneVersion);
                he.protocol_version = std::string(core::insights::kBuiltinProtocolVersion);
                he.diagnosis_version = core::diagnosis::diagnosis_version_stamp();
                he.conditions = run_conditions();
                he.conditions["resolution"] = std::to_string(rep.plan.scene.width) + "x" + std::to_string(rep.plan.scene.height);
                he.conditions["tier"] = std::string(benchmark::to_string(rep.plan.scene.tier));
                he.conditions["mode"] = rep.plan.full ? "full" : "quick";
                he.conditions["storage_target"] = rep.plan.storage_volume.empty() ? "none" : "selected";
                bool scene_completed = false;
                for (const auto& s : rep.subtests) {
                    for (const auto& [k, val] : s.metrics) he.metrics[k] = val;
                    if (s.id == "scene" && s.status == benchmark::SubtestStatus::Completed) scene_completed = true;
                    if (s.status != benchmark::SubtestStatus::Completed) he.exclusions.push_back(s.id + "." + std::string(benchmark::to_string(s.status)));
                }
                // FPS karsilastirmasinin tabani sahne alt testidir; iptal/yarim sahne gecerli kosu degildir.
                he.run_valid = scene_completed && !rep.cancelled;
                if (rep.scene_measured) {  // DIAG-11: gecmis/karsilastirma icin sahne penceresi degerleri
                    he.metrics["scene_util_sampled"] = 1;
                    if (scene_gpu.has_value()) he.metrics["scene_gpu_pct"] = *scene_gpu.value();
                    if (scene_cpu.has_value()) he.metrics["scene_cpu_pct"] = *scene_cpu.value();
                    if (scene_cpu_core.has_value()) he.metrics["scene_cpu_max_core_pct"] = *scene_cpu_core.value();
                    if (scene_sample_ms) he.metrics["scene_sample_interval_ms"] = *scene_sample_ms;
                    // B3 inceleme (bosluk 25): simge durumunda olculen sahne degerleri karsilastirilmaz ("scene." dislamasi).
                    if (scene_minimized) {
                        he.metrics["scene_window_minimized"] = 1;
                        he.exclusions.push_back("scene.window_minimized");
                    }
                }
                // UI-16: onceki ayni kaynak + kosul izli kayitla karsilastir; surum farkliysa iddia yok.
                core::measure::ComparisonView cmp;
                if (const auto* prev = core::measure::comparison_base(env_.history().entries(), he))
                    cmp = core::measure::measure_comparison(*prev, he, "scene_avg_fps");
                env_.history().add(std::move(he));
                env_.save_history();
                vm_.measure.comparison = cmp;
                env_.sync_bench_evidence();  // DIAG-11 + B10 inceleme: yeni kayit esastir; degistiyse bilesen raporu tazelenir
                // Yeni kayit basa geldi: eski secim indeksleri kayar; secim ve karsilastirma temizlenir.
                vm_.history.selected.clear();
                env_.refresh_history_compare();
            }
            publish_measure();
        });
    });
}

}  // namespace ef::app
