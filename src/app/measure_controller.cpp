// P9/B5: MeasureController govdesi: canli test ekrani, uzun test, hiz testi, durdurma. Diger olcumler measure_*.cpp.
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
#include "platform/windows/readiness/network_reader.h"
#include "platform/windows/readiness/display_reader.h"
#include "platform/windows/readiness/storage_reader.h"
#include "platform/windows/run_key_store.h"

namespace ef::app {

using namespace detail;

MeasureController::MeasureController(MeasureHost& env, ui::ViewModel& vm) : env_(env), vm_(vm) {}

void MeasureController::on_live_frame_posted() {
    live_frame_posted_.store(false, std::memory_order_release);
    live_view_push();
}

void MeasureController::start_live_view() {
    live_frame_posted_.store(false, std::memory_order_release);
    live_image_.reset();
    live_mailbox_->clear();
    live_mailbox_->set_wanted(true);  // pencere gorunur (cagiran denetledi); ilk zamanlayici turunda yeniden degerlendirilir
    // Cizim is parcacigi: pikselleri kopyalayip cikar; UI'yi beklemez. Bildirim yalniz bekleyen yokken postalanir.
    live_preview_cb_ = [box = live_mailbox_, host = env_.host_hwnd(), posted = &live_frame_posted_](const benchmark::PreviewFrame& f) {
        if (!box->offer(f.pixels, f.width, f.height, f.stride)) return;
        if (!posted->exchange(true, std::memory_order_acq_rel) && !PostMessageW(host, kMsgLiveFrame, 0, 0))
            posted->store(false, std::memory_order_release);  // kuyruk dolu: sonraki kare yeniden dener (zamanlayici da okur)
    };
    if (!live_view_timer_) live_view_timer_ = SetTimer(env_.host_hwnd(), kTimerLiveView, kLiveViewPeriodMs, nullptr) != 0;
    live_view_push();
}

void MeasureController::stop_live_view() {
    if (live_view_timer_) {
        KillTimer(env_.host_hwnd(), kTimerLiveView);
        live_view_timer_ = false;
    }
    vm_.measure.live_view.active = false;
    live_mailbox_->set_wanted(false);
    live_mailbox_->clear();
    live_image_.reset();
}

void MeasureController::live_view_push() {
    if (!vm_.measure.live_view.active) return;
    // Onizleme yalniz gorunum ekrandayken kopyalanir (pencere kapali / simge durumunda / baska sayfa: kare dusurulur).
    live_mailbox_->set_wanted(env_.live_view_visible());
    if (auto img = live_mailbox_->take()) live_image_ = std::move(img);
    const auto tap = frame_tap_.snapshot();
    core::measure::LiveSample s;
    s.preview = live_image_;
    switch (tap.phase) {
        case benchmark::TapPhase::Prepare: s.phase = core::measure::LivePhase::Prepare; break;
        case benchmark::TapPhase::Warmup: s.phase = core::measure::LivePhase::Warmup; break;
        case benchmark::TapPhase::Measure: s.phase = core::measure::LivePhase::Measure; break;
        case benchmark::TapPhase::Idle: s.phase = core::measure::LivePhase::Idle; break;
    }
    s.step_index = tap.step_index;
    s.step_total = tap.step_total;
    s.step_code = tap.step_code;
    s.width = tap.width;
    s.height = tap.height;
    s.has_numbers = tap.has_window;
    s.fps = tap.fps;
    s.cpu_ms = tap.cpu_ms;
    s.gpu_ms = tap.gpu_ms;
    static_assert(core::measure::kLiveRingSize == benchmark::kTapRingSize, "canli halka boyutu FrameTap ile ayni olmali");
    s.ring_count = std::min(tap.ring_count, s.fps_ring.size());
    for (std::size_t i = 0; i < s.ring_count; ++i) s.fps_ring[i] = tap.fps_ring[i];
    env_.publish_live_sample(s);
}

std::map<std::string, std::string> MeasureController::run_conditions() const {
    std::map<std::string, std::string> c;
    auto put = [&c](const char* k, const std::string& v) {
        if (!v.empty()) c[k] = v.substr(0, 160);
    };
    put("os", vm_.os);
    put("cpu", vm_.cpu);
    put("gpu", vm_.gpu);
    put("gpu_driver", vm_.gpu_driver);
    put("build", vm_.build_id);
    if (perf()) c["power"] = perf()->switch_on() ? "performance_on" : "performance_off";
    return c;
}

void MeasureController::start_endurance_test() {
    if (env_.uninstall_running()) return;  // R20: veri silinirken test baslamaz
    if (vm_.measure.endurance.running) return;
    if (core::measure::busy(vm_.measure)) return;  // tek aktif test
    if (vm_.speed.running) return;                 // tek aktif test
    if (!vm_.measure.builtin_available) return;
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // oyun calisirken agir test yok
    if (endurance_worker_.joinable()) endurance_worker_.join();
    endurance_cancel_ = false;
    {
        auto& e = vm_.measure.endurance;
        e = core::measure::EnduranceState{};
        e.running = true;
        e.budget_s = benchmark::EnduranceBudget{}.planned_seconds;  // 600 s planli sure; sabit, kullanici degistiremez
        e.started_ms = 0;  // B14: tek saat. Taban kosucu t=0 aninda (sahne kurulduktan sonra) alinir; o zamana dek "Hazirlaniyor"
    }
    publish_measure();
    env_.begin_run_marker("endurance");
    endurance_worker_ = spawn_worker([this] {
        // Uyumluluk B3 (bosluk 25): test suresince bu surec EcoQoS'tan cikar; is parcacigi bitince onceki durum geri yazilir.
        platform::adapters::WinSelfThrottleApi self_throttle;
        core::adapters::windows::ScopedExecutionSpeedOptOut eco_opt_out(self_throttle);
        benchmark::SceneConfig scene;
        scene.tier = benchmark::Tier::Medium;  // Uygulama Testi ile ayni sabit sahne kademesi
        benchmark::EnduranceBudget budget;      // sure/yazma/bellek tavanlari (INS-03)
        benchmark::EnduranceRun run;
        bool crashed = false;
        try {
            // NVIDIA varsa gercek saat/yuk/limit bayraklari; yoksa analyze_endurance sensor sinirini acikca raporlar.
            benchmark::EnduranceProbes probes;
            auto nv = platform::nvidia::Nvapi::open();
            if (nv.ok()) {
                const auto* api = nv.value().get();
                probes.vendor_id = 0x10DE;  // Telemetri yalniz sahne NVIDIA kartinda calistiysa kullanilir
                probes.telemetry = [api] {
                    benchmark::EnduranceTelemetry t;
                    const auto tel = api->telemetry();
                    t.gpu_clock_mhz = tel.graphics_clock_mhz;
                    if (tel.gpu_utilization_pct) t.gpu_load_pct = static_cast<double>(*tel.gpu_utilization_pct);
                    if (tel.perf_decrease_mask)
                        if (const auto caps = core::diagnosis::perf_caps_from_mask(*tel.perf_decrease_mask)) t.thermal = caps->thermal, t.power = caps->power;
                    return t;
                };
            }
            // Yeni olumcul WHEA: taban baslangicta alinir; okunamayan gunluk durdurma nedeni sayilmaz.
            const auto whea_base = platform::diagnostics::newest_fatal_whea();
            probes.new_hardware_error = [whea_base] {
                const auto now = platform::diagnostics::newest_fatal_whea();
                return whea_base.ok && now.ok && now.record > whea_base.record;
            };
            // B14 tek saat: taban kosucu t=0 aninda bu is parcaciginda okunur; UI ilerlemesi ve ornek zamanlari ayni saatten.
            probes.on_start = [this](bool telemetry_supported) {
                const auto tick = static_cast<std::int64_t>(GetTickCount64());
                post([this, tick, telemetry_supported] {
                    auto& e = vm_.measure.endurance;
                    if (!e.running) return;
                    e.started_ms = tick > 0 ? tick : 1;
                    e.telemetry_unsupported = !telemetry_supported;
                    publish_measure();
                });
            };
            probes.on_sample = [this](const benchmark::EnduranceSample& s) {
                post([this, s] {
                    auto& e = vm_.measure.endurance;
                    if (!e.running) return;
                    if (s.t_s > e.elapsed_s) e.elapsed_s = s.t_s;
                    e.has_live = true;
                    e.live_fps = s.fps;
                    e.live_gpu_mhz = s.gpu_clock_mhz;
                    e.live_gpu_load_pct = s.gpu_load_pct;
                    publish_measure();
                });
            };
            run = benchmark::run_endurance(scene, budget, endurance_cancel_, probes);
        } catch (...) {
            crashed = true;
        }
        post([this, run = std::move(run), crashed] {
            env_.end_run_marker();
            auto& e = vm_.measure.endurance;
            // Gercek gecen sure: kosucu saatiyle son ornek (UI ile ayni saat); ornek yoksa 0.
            e.elapsed_s = run.report.duration_s;
            e.started_ms = 0;
            e.running = false;
            e.has_live = false;
            e.live_fps = e.live_gpu_mhz = e.live_gpu_load_pct = std::nullopt;
            if (crashed) {
                e.failed = true;
                e.has_result = false;
                publish_measure();
                return;
            }
            e.failed = false;
            e.has_result = true;
            e.result = endurance_view_from_report(run.report);
            // Gecmis: yalniz olculen metrikler + gercek dislamalar; saglik raporu degildir.
            const std::string internal_title = std::string(benchmark::kSceneVersion) + " " + std::string(benchmark::to_string(benchmark::Tier::Medium));
            core::measure::ResultView r;
            r.source = core::measure::TestSource::Endurance;
            r.when_unix_ms = now_unix_ms();
            r.diagnosis = core::measure::Diagnosis::Insufficient;  // tek uzun test teshis raporu degildir
            r.headline_key = "measure.endurance.history";
            r.show_confidence = false;
            r.run_valid = e.result.stop == core::measure::EnduranceStopKind::Completed && e.result.has_comparison();
            if (e.result.warm_fps) r.avg_fps = contracts::Measurement::of(*e.result.warm_fps, contracts::Unit::Fps);
            else if (e.result.cold_fps) r.avg_fps = contracts::Measurement::of(*e.result.cold_fps, contracts::Unit::Fps);
            vm_.measure.history.insert(vm_.measure.history.begin(), r);
            cap_measure_history(vm_.measure.history);
            {
                core::insights::HistoryEntry he;
                he.when_unix_ms = r.when_unix_ms;
                he.source = "endurance";
                he.title = internal_title;  // sahne kimligi; UI yerellestirilmis basligi gosterir
                he.fingerprint = internal_title + "|endurance";
                he.app_version = vm_.version;
                he.scene_version = std::string(benchmark::kSceneVersion);
                he.protocol_version = std::string(core::insights::kEnduranceProtocolVersion);
                he.diagnosis_version = core::diagnosis::diagnosis_version_stamp();
                he.conditions = run_conditions();
                // Tamamlanmis ve soguk/isinmis karsilastirmasi olan kosu gecerlidir; iptal/butce yarimdir.
                he.run_valid = e.result.stop == core::measure::EnduranceStopKind::Completed && e.result.has_comparison();
                he.metrics = core::measure::endurance_metrics(e.result);  // yalniz olculen
                he.exclusions = core::measure::endurance_exclusions(e.result);
                env_.history().add(std::move(he));
                env_.save_history();
                env_.sync_bench_evidence();  // B10 inceleme: gecmis sinirinda dusen kanit bilesen raporundan da cikar
            }
            publish_measure();
        });
    });
}

void MeasureController::start_speed_test() {
    if (vm_.speed.running || vm_.speed.server_running) return;
    if (core::measure::busy(vm_.measure) || vm_.measure.endurance.running) return;  // tek aktif test (olcum suruyor)
    if (env_.uninstall_running()) return;  // R20: veri silinirken yeni is yok
    // Oyun acikken veya tespit guvenilir degilken sorulmaz bile (kart "Oyun acikken hiz testi yapilmaz" der).
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;
    if (!env_.ask_speed_consent()) return;  // Hayir: ag baglantisi kurulmaz
    // Onay penceresi acikken oyun baslamis olabilir: kapi yeniden dogrulanir.
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;
    if (speed_worker_.joinable()) speed_worker_.join();
    speed_cancel_ = false;
    speed_gate_blocked_ = false;
    {
        const auto keep = vm_.speed;  // R-C4: sunucu ping alanlari hiz testi sifirlamasindan etkilenmez
        vm_.speed = ui::ViewModel::SpeedView{};
        vm_.speed.server_ip = keep.server_ip;
        vm_.speed.server_state = keep.server_state;
        vm_.speed.server_sent = keep.server_sent;
        vm_.speed.server_received = keep.server_received;
        vm_.speed.server_avg_ms = keep.server_avg_ms;
        vm_.speed.server_jitter_ms = keep.server_jitter_ms;
    }
    vm_.speed.running = true;
    vm_.speed.phase = core::net::SpeedPhase::Latency;
    publish_speed();
    speed_worker_ = spawn_worker([this] {
        platform::net::SpeedTestRunner runner;
        const auto result = runner.run(
            speed_cancel_, [this] { return speed_gate_blocked_.load(); },
            [this](core::net::SpeedPhase ph, double mbps, std::uint64_t bytes) {
                // Kosucu en fazla 10 Hz bildirir; UI is parcaciginda kapi da yeniden denetlenir (bayat tespit dahil).
                post([this, ph, mbps, bytes] {
                    if (!vm_.speed.running) return;
                    if (!env_.gate(core::games::GuardedAction::HeavyBenchmark).allowed) speed_gate_blocked_ = true;
                    vm_.speed.phase = ph;
                    vm_.speed.live_mbps = mbps;
                    vm_.speed.bytes = bytes;
                    publish_speed();
                });
            });
        post([this, result] {
            auto& v = vm_.speed;
            v.running = false;
            v.phase = result.phase;
            v.fail = result.fail;
            v.live_mbps = 0;
            v.down = result.down_mbps;
            v.up = result.up_mbps;
            v.latency = result.latency_ms;
            v.down_loaded = result.down_loaded_ms;
            v.up_loaded = result.up_loaded_ms;
            v.bytes = result.bytes_used;
            publish_speed();
        });
    });
}

void MeasureController::stop_speed_test() {
    if (vm_.speed.running) speed_cancel_ = true;  // kosucu sonraki okuma/yazmadan once durur; sonuc sayi tasimaz
}

void MeasureController::start_server_ping() {
    if (vm_.speed.running || vm_.speed.server_running) return;
    if (core::measure::busy(vm_.measure) || vm_.measure.endurance.running) return;  // tek aktif test
    if (env_.uninstall_running()) return;
    // Hiz testiyle ayni kural: oyun acikken veya tespit guvenilir degilken olcum yapilmaz (ADR-021).
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;
    const auto ip = env_.ask_server_address(vm_.speed.server_ip);
    if (!ip) return;  // Hayir / kapatildi: hicbir paket gonderilmez, adres degismez
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // pencere acikken oyun baslamis olabilir
    if (speed_worker_.joinable()) speed_worker_.join();
    speed_cancel_ = false;
    {
        auto& v = vm_.speed;
        v.server_ip = *ip;
        v.server_state = core::net::ServerPingState::Running;
        v.server_running = true;
        v.server_sent = v.server_received = 0;
        v.server_avg_ms = v.server_jitter_ms = 0;
    }
    env_.prefs().game_server_ipv4 = *ip;  // yalniz bu bilgisayarda (tercih dosyasi)
    env_.save_prefs();
    publish_speed();
    speed_worker_ = spawn_worker([this, addr = *ip] {
        namespace pr = platform::readiness;
        const std::vector<pr::PingTarget> targets{{"game_server", addr}};
        const auto runs = pr::run_ping_test(targets, core::net::kServerPingSamples, core::net::kServerPingIntervalMs, &speed_cancel_);
        const auto stats = pr::ping_test_stats(targets, runs);
        const bool cancelled = speed_cancel_.load();
        post([this, stats, cancelled] {
            auto& v = vm_.speed;
            v.server_running = false;
            const core::readiness::PingStats st = stats.empty() ? core::readiness::PingStats{} : stats.front();
            v.server_sent = st.sent;
            v.server_received = st.received;
            if (cancelled) v.server_state = core::net::ServerPingState::Cancelled;
            else if (st.failed || st.sent == 0) v.server_state = core::net::ServerPingState::Failed;
            else if (st.received < core::net::kServerPingMinReceived) v.server_state = core::net::ServerPingState::Insufficient;
            else {
                v.server_state = core::net::ServerPingState::Done;
                v.server_avg_ms = st.avg_ms;
                v.server_jitter_ms = st.jitter_ms;
            }
            publish_speed();
        });
    });
}

void MeasureController::stop_endurance_test() {
    // Iptal bayragi her ornekte kontrol edilir; sonuc geldiginde analiz "Cancelled" olarak raporlanir.
    if (vm_.measure.endurance.running) endurance_cancel_ = true;
}

}  // namespace ef::app
