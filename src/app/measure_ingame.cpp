// P9/B5: Oyunda Test. app_measure.cpp'den ayrildi; davranis degismedi.
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

void MeasureController::start_ingame_test() {
    using core::measure::TestPhase;
    if (vm_.measure.phase == TestPhase::Preparing || vm_.measure.phase == TestPhase::Running ||
        vm_.measure.phase == TestPhase::Stopping)
        return;
    if (vm_.measure.endurance.running) return;  // R13: uzun test ile ayni anda ikinci test yok
    if (vm_.speed.running) return;              // tek aktif test
    // ADR-021: yalniz gercek izleyicinin gordugu oyun + uyumluluk kapisi. Red nedeni view modelde zaten
    // gosterilir (push_gate); burada ikinci kez zorlanir, override yok.
    if (!env_.gate_or_block(core::games::GuardedAction::LiveCapture)) return;
    const auto active = env_.active_game();
    if (!active) {
        env_.push_gate();
        return;
    }
    if (bench_worker_.joinable()) bench_worker_.join();
    const MeasureGameTarget game = *active;
    const core::measure::ProcessIdentity target{game.pid, game.create_time, {}};
    const std::string app_id = capture_app_id(game.game_id);
    bench_cancel_ = false;
    vm_.measure.source = core::measure::TestSource::InGame;
    vm_.measure.storage_blocked = false;
    vm_.measure.phase = TestPhase::Preparing;
    vm_.measure.stage_key = "measure.stage_prepare";
    vm_.measure.elapsed_s = 0;
    vm_.measure.total_s = 0;  // sure yalniz gercek zamanla gosterilir; sahte yuzde yok
    vm_.measure.started_ms = static_cast<std::int64_t>(GetTickCount64());
    vm_.measure.fail_reason.clear();
    publish_measure();

    env_.begin_run_marker("in_game");
    bench_worker_ = spawn_worker([this, game, target, app_id] {
        const auto t0 = GetTickCount64();
        // PDH ornekleri yalniz istemci surecinde (yukseltilmis yardimcida degil), ~1 Hz. Yakalama engelleyici
        // oldugu icin ayri ornekleyici is parcacigi; ornekler QPC ms ile zamanlanir.
        std::vector<std::pair<double, core::metrics::LiveSnapshot>> samples;
        // B3 inceleme (bosluk 2): ham birikimli CPU sureleri (QPC ms); drop penceresi basina "baska programlar CPU"
        // penceresinin kendi zamani icin hesaplanir (kayan ortalama drop'u sulandirmaz / geciktirmez).
        std::vector<std::pair<double, core::metrics::CpuTimes>> cpu_readings;
        // Spec 2026-11-12 C3: oyunun 3D yukunu tasiyan bagdastirici (LUID) + o andaki yuzde; yakalama boyunca.
        std::vector<core::diagnosis::AdapterLoadSample> adapter_samples;
        std::atomic<bool> sampling{true};
        auto cpu_coverage = core::inventory::CpuSetCoverage::Unknown;  // F-41: ana thread kume kapsamasi
        auto graphics_api = core::diagnosis::GraphicsApi::Unknown;  // F-54: yuklu modullerden (sezgi)
        // F-79: yakalama oncesi calisan overlay/izleme/RGB yigini; kaynagi belirsiz takilmalarda baglam.
        auto background = core::readiness::classify_background_stack(platform::readiness::read_running_exe_names());
        // F-81: oyun biriminin SSD/HDD turu; kaynagi belirsiz takilmalarda disk baglami.
        const auto storage = platform::readiness::read_storage_health(game.dir_key, false);  // oyun acikken disk komutu gonderilmez
        // F-63: adaptor toplam VRAM'i (varsa) VRAM basinci baglami icin; OS surec butcesi degildir.
        const auto adapter_vram_mib = largest_adapter_vram_mib();
        // Spec 2026-11-12 B: kare siniri suphesi icin oyunun ekraninin yenileme hizi; GPU suresi notu icin HAGS durumu
        // (D3DKMT WDDM 2.7 caps, salt okuma). Okunamayan deger nullopt kalir.
        const auto refresh_before = game_display_refresh_hz(game.pid);
        std::optional<bool> hags_on;
        {
            std::vector<core::diagnosis::HagsAdapterState> states;
            for (const auto& h : platform::inventory::read_hags()) {
                core::diagnosis::HagsAdapterState s;
                if (h.supported.has()) s.supported = *h.supported.value;
                if (h.enabled.has()) s.enabled = *h.enabled.value;
                states.push_back(s);
            }
            hags_on = core::diagnosis::hags_enabled_any(states);
        }
        bool game_on_hdd = false;
        if (storage.game_volume_root) {
            for (const auto& v : storage.volumes)
                if (v.root == *storage.game_volume_root && v.ssd && !*v.ssd) game_on_hdd = true;
        }
        std::thread sampler = spawn_worker([&, pid = game.pid] {
            platform::metrics::PdhSampler pdh;
            const bool open = pdh.open();
            GameCpuProbe game_cpu(pid);
            GameMainThreadProbe game_main_thread(pid);  // DIAG-10: ana thread (acilamazsa eksik kalir)
            GameMemoryProbe game_mem(pid);              // F-67: ozel bellek (erisim yoksa eksik kalir)
            ef::measurement::ProcessGpuSampler game_gpu(pid);  // F-63: ayrik VRAM (sayac yoksa eksik)
            game_gpu.open();
            // F-61: NVIDIA surucu limit bayragi + saat, salt okuma. 20 Hz orneklemeye ek yuk
            // bindirmemek icin ~2 Hz okunur; okunmayan orneklerde alanlar missing kalir.
            auto nv = platform::nvidia::Nvapi::open();
            std::size_t nv_tick = 0;
            graphics_api = core::diagnosis::classify_graphics_api(GameModuleProbe::names(pid));
            cpu_coverage = platform::inventory::read_game_cpu_coverage(pid, game_main_thread.tid);
            // Uyumluluk B3 (bosluk 2): "baska programlar CPU %" denge_v2 ile ayni SURE tabanli yontemle hesaplanir:
            // GetSystemTimes mesgul - oyun sureci - DPC/kesme - oturumun dwm.exe'si. Frekansla olceklenen Utility
            // toplamindan oyun zamani cikarilmaz. B3 inceleme: ornek basina kayan ortalama yazilmaz; ham okumalar
            // saklanir ve deger drop penceresinin kendi zamani icin hesaplanir. Okunamayan ornek saklanmaz.
            std::vector<HANDLE> exempt;
            for (const auto cpid : session_compositor_pids())
                if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, cpid)) exempt.push_back(h);
            struct Closer {
                std::vector<HANDLE>& v;
                ~Closer() {
                    for (HANDLE h : v) CloseHandle(h);
                }
            } closer{exempt};
            while (sampling.load()) {
                const double t = qpc_ms();
                auto snap = open ? pdh.sample() : core::metrics::LiveSnapshot{};
                if (const auto times = read_cpu_times(game_cpu.process, exempt)) cpu_readings.emplace_back(qpc_ms(), *times);
                if (const auto main_pct = game_main_thread.sample(t))
                    snap.cpu_main_thread_pct = core::metrics::percent(*main_pct);
                if (const auto priv_mib = game_mem.sample())
                    snap.game_private_mib = contracts::Measurement::of(*priv_mib, contracts::Unit::MiB);
                const auto gpu = game_gpu.sample();
                // Spec 2026-11-12 B: siniflandiriciya sistem toplami ("en yuklu motor") yerine oyun surecinin KENDI 3D
                // motor kullanimi verilir (en cok kullandigi bagdastirici, LUID bazinda). Surec sayaci okunamadiysa
                // deger eksik kalir; baska sureclerin yukunu tasiyan toplam onun yerine yazilmaz.
                if (gpu.engine_3d_pct && std::isfinite(*gpu.engine_3d_pct) && !gpu.engine_luid.empty())
                    adapter_samples.push_back({gpu.engine_luid, *gpu.engine_3d_pct});  // C3
                if (gpu.engine_3d_pct && std::isfinite(*gpu.engine_3d_pct))
                    snap.gpu_pct = core::metrics::percent(std::clamp(*gpu.engine_3d_pct, 0.0, 100.0));
                else
                    snap.gpu_pct = contracts::Measurement::missing(contracts::Unit::Percent, contracts::MissingReason::NotSampled);
                // Uyumluluk B4 (bosluk 15): Gorev Yoneticisi yontemi (en yuklu motor turu); "bu oyunda ne sinirladi" capraz
                // denetimi bunu kullanir, 3D ikincil alandir.
                if (gpu.engine_max_pct && std::isfinite(*gpu.engine_max_pct))
                    snap.game_gpu_engine_max_pct = core::metrics::percent(std::clamp(*gpu.engine_max_pct, 0.0, 100.0));
                if (gpu.dedicated_bytes) {
                    const double vram_mib = static_cast<double>(*gpu.dedicated_bytes) / (1024.0 * 1024.0);
                    if (std::isfinite(vram_mib) && vram_mib > 0.0)
                        snap.game_vram_dedicated_mib = contracts::Measurement::of(vram_mib, contracts::Unit::MiB);
                }
                // F-61: ayni birincil fiziksel GPU'nun surucu verisi. Okuma yoksa missing(NotSampled).
                if (nv.ok() && nv_tick % 10 == 0) {
                    const auto tel = nv.value()->telemetry();
                    if (tel.gpu_utilization_pct)
                        snap.gpu_vendor_utilization_pct = contracts::Measurement::of(
                            static_cast<double>(*tel.gpu_utilization_pct), contracts::Unit::Percent);
                    if (tel.graphics_clock_mhz)
                        snap.gpu_vendor_clock_mhz =
                            contracts::Measurement::of(*tel.graphics_clock_mhz, contracts::Unit::MHz);
                    if (tel.perf_decrease_mask)
                        snap.gpu_vendor_perf_decrease_mask = contracts::Measurement::of(
                            static_cast<double>(*tel.perf_decrease_mask), contracts::Unit::Count);
                }
                ++nv_tick;
                samples.emplace_back(t, snap);
                Sleep(kIngameSampleIntervalMs);
            }
        });
        auto cr = platform::capture_game_elevated(target, kIngameCaptureSeconds, kIngameMinSeconds, app_id, bench_cancel_);
        sampling = false;
        if (sampler.joinable()) sampler.join();
        // Test baslatilirken tam ekran oyun simge durumunda olabilir (bizim pencere onde): o an yenileme hizi
        // okunamaz. Yakalama bitince oyun ondeyken yeniden okunur; okunursa oyun sirasindaki ekran modu gecerlidir.
        const auto refresh_after = game_display_refresh_hz(game.pid);
        const auto refresh_hz = refresh_after ? refresh_after : refresh_before;
        // Spec 2026-11-12 C3 (salt okuma): oyunun 3D yukunun bulundugu bagdastirici, DXGI yuksek performans
        // tercihindeki bagdastiriciyla karsilastirilir. Farkliysa masaustunde oneri, dizustunde (pil var) bilgi.
        core::diagnosis::GameAdapterLevel adapter_level = core::diagnosis::GameAdapterLevel::None;
        bool vendor_limit_no_api = false;  // Uyumluluk B8 (bosluk 17): oyunun karti AMD/Intel (sinir API'si yok)
        {
            core::diagnosis::GameAdapterInput gi;
            const auto pick = core::diagnosis::pick_game_adapter(adapter_samples);
            if (!pick.luid.empty())
                for (const auto& g : platform::inventory::read_gpus())
                    if (g.luid.has() && *g.luid.value == pick.luid && g.vendor_id.has())
                        vendor_limit_no_api = core::diagnosis::vendor_limit_api_missing(*g.vendor_id.value);
            gi.game_luid = pick.luid;
            gi.game_3d_pct = pick.mean_pct;
            const auto pref = platform::readiness::read_gpu_preference();
            gi.high_perf_luid = pref.high_perf_luid;
            gi.hardware_adapters = pref.hardware_adapters;
            SYSTEM_POWER_STATUS ps{};
            if (GetSystemPowerStatus(&ps) && ps.BatteryFlag != 255) gi.laptop = (ps.BatteryFlag & 128) == 0;  // 128: pil yok
            adapter_level = core::diagnosis::evaluate_game_adapter(gi).level;
        }
        post([this, cr = std::move(cr), samples = std::move(samples), cpu_readings = std::move(cpu_readings), game, t0, cpu_coverage, game_on_hdd,
              adapter_vram_mib, graphics_api, background = std::move(background), refresh_hz, hags_on, adapter_level, vendor_limit_no_api] {
            env_.end_run_marker();
            using core::measure::TestPhase;
            vm_.measure.elapsed_s = static_cast<double>(GetTickCount64() - t0) / 1000.0;
            vm_.measure.stage_key.clear();
            auto failed = [this](std::string reason) {
                vm_.measure.phase = TestPhase::Failed;
                vm_.measure.fail_reason = std::move(reason);
                publish_measure();
            };
            if (cr.uac_denied()) return failed("capture_uac_denied");
            if (cr.cancelled() || bench_cancel_.load()) {
                vm_.measure.phase = TestPhase::Cancelled;
                publish_measure();
                return;
            }
            if (!cr.ok()) return failed("capture_failed");
            if (cr.access_denied()) return failed("capture_access_denied");
            const auto& cap = cr.capture;
            // EF-X18/T04: oyun olcum bitmeden kapandi/coktu -> kosu tamamlanmadi, gecmise yazilmaz.
            if (cap.outcome == core::measure::CaptureOutcome::GameExited) return failed("capture_game_exited");
            if (cap.target != core::measure::TargetError::None) return failed("capture_target_failed");
            if (!cap.start.ok()) return failed("capture_start_failed");
            if (cap.outcome != core::measure::CaptureOutcome::Complete) return failed("capture_failed");
            // EF-X18: ayni-kosul baglami; gozlenemeyen alan "unknown" (sahne/cozunurluk/olcek/limit/sync/upscaler/FG).
            auto manifest = cap.manifest;
            {
                core::measure::RunContextInput rc;
                rc.game_build = env_.game_build(game.dir_key);
                using A = core::diagnosis::GraphicsApi;  // yuklu modul sezgisi (kanit degil); runtime guveni ayri
                rc.renderer = graphics_api == A::D3D9     ? "d3d9"
                              : graphics_api == A::D3D11  ? "d3d11"
                              : graphics_api == A::D3D12  ? "d3d12"
                              : graphics_api == A::Vulkan ? "vulkan"
                                                          : "";
                rc.api_confidence = std::string(core::measure::to_string(core::measure::api_confidence(cap.summary.runtime)));
                rc.os_build = env_.capture_os_build();
                rc.gpu_driver = vm_.gpu_driver;
                rc.collector_commit = env_.capture_collector_commit();
                for (auto& [k, v] : core::measure::make_run_context(rc)) manifest.context[k] = v;
            }

            // Kare zamanlari ETW kokenine gore; FramePoint birikimli sunum zamani.
            std::vector<core::diagnosis::FramePoint> frames;
            frames.reserve(cap.summary.render_intervals_ms.size());
            double acc = 0;
            for (const double iv : cap.summary.render_intervals_ms) {
                acc += iv;
                frames.push_back({acc, iv});
            }
            // PDH ornekleri ayni QPC ekseninde. Origin yoksa hiza iddia edilmez: kaynak Unknown isaretlenir.
            std::vector<core::diagnosis::LiveDropSample> live;
            live.reserve(samples.size());
            const bool have_origin = cap.capture_origin_ms.has_value() && *cap.capture_origin_ms > 0.0;
            for (const auto& [t_qpc, snap] : samples) {
                if (have_origin) {
                    if (t_qpc < *cap.capture_origin_ms) continue;  // yardimci baslamadan onceki ornek
                    live.push_back({t_qpc - *cap.capture_origin_ms, true, snap});
                } else {
                    live.push_back({0.0, false, snap});
                }
            }
            core::diagnosis::DropInput din;
            din.run.run_id = core::measure::fingerprint(manifest);
            din.run.frame_domain = core::diagnosis::ClockDomain::Qpc;
            din.frames = std::move(frames);
            // Spec 2026-11-12 B: kare basi GPU mesgul suresi (kare olayinin kendi alani; frames ile bire bir).
            // Bos = olculmedi -> drop penceresi GPU suresi kaniti tasimaz.
            din.frame_gpu_busy_ms = cap.summary.render_gpu_busy_ms;
            din.run.adapter_dedicated_vram_mib = adapter_vram_mib;  // F-63: yalniz baglam
            din.run.gpu_limit_windows = core::diagnosis::gpu_limit_windows_from_live(live);  // F-61
            din.run.gpu_vendor_limit_api_missing = vendor_limit_no_api;  // Uyumluluk B8 (bosluk 17)
            din.sources = core::diagnosis::drop_sources_from_live(live);
            // B3 inceleme (bosluk 2): ham CPU sureleri kare eksenine (mikrosaniye) tasinir; origin yoksa hiza iddia
            // edilmez ve okumalar verilmez (pdh kaynagi zaten hizasiz; kok neden sayilmaz).
            if (have_origin) {
                din.run.other_cpu_times.reserve(cpu_readings.size());
                for (const auto& [t_qpc, times] : cpu_readings)
                    din.run.other_cpu_times.push_back(
                        {static_cast<std::int64_t>(std::llround((t_qpc - *cap.capture_origin_ms) * 1000.0)), times});
            }
            auto report = core::diagnosis::detect_drops(din);
            // F-41: yalniz okunabilen kume kapsamasi raporlanir; gercek zamanlayici yerlesimi iddia edilmez.
            switch (cpu_coverage) {
                case core::inventory::CpuSetCoverage::PerformanceAndEfficiency:
                    report.notes.push_back("cpu_placement_both");
                    break;
                case core::inventory::CpuSetCoverage::PerformanceOnly:
                    report.notes.push_back("cpu_placement_p_only");
                    break;
                case core::inventory::CpuSetCoverage::EfficiencyOnly:
                    report.notes.push_back("cpu_placement_e_only");
                    break;
                case core::inventory::CpuSetCoverage::Unknown:
                case core::inventory::CpuSetCoverage::NotHybrid:
                    break;
            }
            // F-54: yalniz drop varken API baglami. Yuklu modul sezgidir; sunum modu iddia edilmez.
            if (report.outcome == core::diagnosis::DropOutcome::DropsFound) {
                const std::string_view api_note = core::diagnosis::graphics_api_evidence(graphics_api);
                if (!api_note.empty()) report.notes.push_back(std::string(api_note));
            }
            // F-79: yalniz takilma bulundu ve baskin kaynak yoksa arka plan yigini baglam notu.
            // Surec varligi nedensellik kaniti degildir; not metni bunu boyle soyler.
            const bool unsourced =
                report.outcome == core::diagnosis::DropOutcome::DropsFound &&
                (!report.dominant.has_value() || *report.dominant == core::diagnosis::DropSource::Insufficient);
            if (unsourced) {
                if (!background.overlays.empty()) report.notes.push_back("background_overlay");
                if (!background.rgb.empty()) report.notes.push_back("background_rgb");
                if (!background.monitoring.empty()) report.notes.push_back("background_monitoring");
                if (game_on_hdd) report.notes.push_back("game_disk_hdd");  // F-81
            }

            core::measure::ResultView r = core::measure::ingame_result_view(game.name, manifest, cap.stats, now_unix_ms());
            r.source = core::measure::TestSource::InGame;
            r.headline_key = "drop.headline." + std::string(core::diagnosis::to_string(report.outcome));
            r.diagnosis = report.outcome == core::diagnosis::DropOutcome::Insufficient ? core::measure::Diagnosis::Insufficient
                          : report.outcome == core::diagnosis::DropOutcome::DropsFound ? core::measure::Diagnosis::Limited
                                                                                       : core::measure::Diagnosis::None;
            r.drops = report;
            {   // Spec 2026-11-12 B: "bu oyunda ne sinirladi" (kare siniri suphesi once; GPU mesgul orani pencereleri).
                core::diagnosis::GameLimitInput gl;
                gl.intervals_ms = cap.summary.render_intervals_ms;
                gl.gpu_busy_ms = cap.summary.render_gpu_busy_ms;
                gl.avg_fps = cap.stats.avg_fps;
                gl.refresh_hz = refresh_hz;
                gl.hags_on = hags_on;
                gl.run_valid = manifest.valid;
                core::measure::GameLimitView glv;
                // Oyunun kendi 3D motor kullanimi: yalniz yakalama penceresine hizalanan ornekler (hizasizsa yazilmaz).
                // Uyumluluk B4 (bosluk 15): capraz denetim en yuklu motor turuyla (Gorev Yoneticisi yontemi); 3D ikincil alan.
                double gpu_sum = 0, gpu3d_sum = 0;
                std::size_t gpu_n = 0, gpu3d_n = 0;
                for (const auto& s : live) {
                    if (!s.aligned) continue;
                    if (const auto v = s.snapshot.game_gpu_engine_max_pct.value(); v && std::isfinite(*v)) {
                        gpu_sum += *v;
                        ++gpu_n;
                    }
                    if (const auto v = s.snapshot.gpu_pct.value(); v && std::isfinite(*v)) {
                        gpu3d_sum += *v;
                        ++gpu3d_n;
                    }
                }
                if (gpu_n > 0) glv.game_gpu_max_pct = gpu_sum / static_cast<double>(gpu_n);
                if (gpu3d_n > 0) glv.game_gpu_pct = gpu3d_sum / static_cast<double>(gpu3d_n);
                gl.game_gpu_pct = glv.game_gpu_max_pct;  // capraz denetim: GPU suresi hukmuyle celisirse hukum verilmez
                glv.adapter_level = adapter_level;   // C3: farkli bagdastirici notu (oneri / bilgi)
                glv.result = core::diagnosis::evaluate_game_limit(gl);
                r.game_limit = std::move(glv);
            }
            vm_.measure.phase = TestPhase::Done;
            vm_.measure.result = r;
            vm_.measure.history.insert(vm_.measure.history.begin(), r);
            cap_measure_history(vm_.measure.history);
            {
                core::insights::HistoryEntry he;
                he.when_unix_ms = r.when_unix_ms;
                he.source = "in_game";
                he.title = game.name;  // gorunen ad; ic kimlik/yol yok
                he.fingerprint = core::measure::fingerprint(manifest);
                he.app_version = vm_.version;
                // B3: dis oyun sahnesi + yakalama protokolu surumu; gecersiz/yarim kosu fark uretmez.
                he.scene_version = std::string(core::insights::kGameSceneVersion);
                he.protocol_version = "capture_v" + std::to_string(ef::measurement::kCaptureResultVersion);
                he.diagnosis_version = core::diagnosis::diagnosis_version_stamp();
                he.conditions = run_conditions();
                if (const auto build = env_.game_build(game.dir_key); !build.empty())
                    he.conditions["game_build"] = build.substr(0, 160);  // bilinmeyen surum karsilastirilmaz
                // EF-X19.02: grafik serisi cap.stats ile ayni ham kare surelerinden (game_capture.cpp); yuzdelik seriden hesaplanmaz.
                he.fps_series = core::measure::fps_per_second(cap.summary.render_intervals_ms);
                {   // EF-X12: renderer yalniz gercekten gozlendiyse rehbere gider
                    using G = core::games::GuideRenderer;
                    using A = core::diagnosis::GraphicsApi;
                    const G obs = graphics_api == A::D3D9 ? G::D3D9 : graphics_api == A::D3D11 ? G::D3D11 : graphics_api == A::D3D12 ? G::D3D12 : graphics_api == A::Vulkan ? G::Vulkan : G::Unknown;
                    env_.note_game_renderer(game.dir_key, obs);
                    for (auto& row : vm_.games)
                        if (row.dir_key == game.dir_key) row.guide_renderer = obs;
                }
                he.run_valid = manifest.valid;
                auto add = [&he](std::string k, const contracts::Measurement& m) {
                    if (m.has_value()) he.metrics[std::move(k)] = *m.value();  // yalniz olculen deger
                };
                add("avg_fps", r.avg_fps);
                add("low1_fps", r.low1_fps);
                add("low01_fps", r.low01_fps);
                add("p99_ms", r.p99_ms);
                add("max_ms", r.worst_ms);
                he.metrics["valid_frames"] = static_cast<double>(report.valid_frames);
                he.metrics["measured_ms"] = report.measured_ms;
                he.metrics["drop_windows"] = static_cast<double>(report.total_windows);
                he.metrics["drop_time_ratio"] = report.drop_time_ratio;
                if (report.outcome == core::diagnosis::DropOutcome::DropsFound) {
                    double worst = 0;
                    for (const auto& w : report.worst) worst = std::max(worst, w.worst_ms);
                    he.metrics["drop_worst_ms"] = worst;
                }
                // Spec 2026-11-12 B: olculen GPU mesgul paylari (yalniz olculduyse; hukum metni kayda yazilmaz).
                if (r.game_limit) {
                    const auto limit_metrics = core::measure::game_limit_metrics(*r.game_limit);
                    he.metrics.insert(limit_metrics.begin(), limit_metrics.end());
                }
                for (const auto e : manifest.exclusions)
                    if (e != core::measure::Exclusion::None) he.exclusions.push_back("run." + std::string(core::measure::to_string(e)));
                if (report.outcome == core::diagnosis::DropOutcome::Insufficient) he.exclusions.push_back("drop.insufficient");
                if (!report.unaligned_sources.empty()) he.exclusions.push_back("drop.unaligned_sources");
                // UI-16: onceki ayni kaynak + kosul izli kayitla karsilastir; surum farkliysa iddia yok.
                core::measure::ComparisonView cmp;
                if (core::measure::context_comparable(manifest)) {  // EF-X18: sahne/cozunurluk/limit bilinmiyorsa ayni-kosul iddiasi yok
                    if (const auto* prev = core::measure::comparison_base(env_.history().entries(), he))
                        cmp = core::measure::measure_comparison(*prev, he, "avg_fps");
                }
                env_.history().add(std::move(he));
                env_.save_history();
                vm_.measure.comparison = cmp;
                env_.sync_bench_evidence();  // B10 inceleme: gecmis sinirinda dusen Uygulama Testi kaniti rapordan da cikar
                // Yeni kayit basa geldi: eski secim indeksleri kayar; secim ve karsilastirma temizlenir.
                vm_.history.selected.clear();
                env_.refresh_history_compare();
            }
            publish_measure();
        });
    });
}

}  // namespace ef::app
