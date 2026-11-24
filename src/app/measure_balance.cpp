// P9/B5: Uygulama Testi Darbogaz (Denge) kapsami. app_measure.cpp'den ayrildi; davranis degismedi.
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

namespace {
// Olcum sirasinda ~10 Hz ornek: birikimli CPU sureleri (makine toplami / mesgul / bu surec) ve ~2 Hz NVIDIA surucu
// limit bayragi. Baska sureclerin CPU'su bu sayaclarin FARKINDAN hesaplanir (core::metrics::other_cpu_time_percent_in).
struct BalanceSample {
    std::chrono::steady_clock::time_point t{};
    std::optional<core::metrics::CpuTimes> cpu;  // okunamadiysa bos (0 yazilmaz)
    std::optional<std::uint32_t> perf_mask;
    // denge_v3 (Uyumluluk B4 bosluk 9): ayni surucu okumasindan yuk ve grafik saati (kosu duzeyi kural icin).
    std::optional<double> gpu_load_pct, gpu_clock_mhz;
};
constexpr DWORD kBalanceSampleIntervalMs = 100;
// R-E1: test baslamadan once baska programlar CPU'yu mesgulken en cok 10 x 1,5 sn sakinlesme beklenir.
constexpr DWORD kBalanceCalmPollMs = 1500;
constexpr std::uint32_t kBalanceCalmMaxPolls = 10;
constexpr std::size_t kBalanceVendorEvery = 5;
}  // namespace

void MeasureController::start_balance_test() {
    using core::measure::TestPhase;
    namespace bb = benchmark::balance;
    // Uygulama Testi ile ayni kapilar (start_builtin_test): veri silme, tek aktif test, oyun kapisi + guncelleme beklemesi.
    if (env_.uninstall_running()) return;  // R20: veri silinirken test baslamaz
    if (vm_.measure.phase == TestPhase::Preparing || vm_.measure.phase == TestPhase::Running || vm_.measure.phase == TestPhase::Stopping) return;
    if (vm_.measure.endurance.running) return;  // R13: uzun test ile ayni anda ikinci test yok
    if (vm_.speed.running) return;              // tek aktif test: hiz testi agi/islemciyi kullanirken olcum yok
    if (!env_.verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // oyun calisirken agir test yok
    if (bench_worker_.joinable()) bench_worker_.join();
    bench_cancel_ = false;
    vm_.measure.source = core::measure::TestSource::Builtin;  // kullaniciya gorunen kaynak yine Uygulama Testi
    vm_.measure.gate_stopped = false;
    vm_.measure.phase = TestPhase::Preparing;
    vm_.measure.stage_key = "measure.stage_balance_prepare";
    vm_.measure.elapsed_s = 0;
    vm_.measure.total_s = 0;  // ilerleme tamamlanan adim sayisindan; sahte yuzde yok
    vm_.measure.started_ms = static_cast<std::int64_t>(GetTickCount64());
    vm_.measure.fail_reason.clear();
    vm_.measure.storage_blocked = false;  // denge testi diske yazmaz
    // Spec 2026-11-12 A4: canli test ekrani — ayar acik VE pencere gorunurse. Kapaliysa ya da pencere gorunmuyorsa
    // (tepside, simge durumunda) onizleme uretilmez; test aynen calisir. Karar kosu boyunca degismez (kosul).
    const bool live = vm_.measure.live_view.user_on && env_.window_visible();
    frame_tap_.frames.store(0, std::memory_order_relaxed);  // onceki is parcacigi yukarida join edildi
    frame_tap_.sum_us.store(0, std::memory_order_relaxed);
    frame_tap_.begin_step(0, 0, 0, 0, 0);  // onceki kosunun adim kimligi / canli sayilari yeni kosuya tasinmaz
    frame_tap_.set_phase(benchmark::TapPhase::Idle);
    vm_.measure.live_view.active = live;
    // Once pencere canli gorunume gecer, sonra posta kutusu acilir: start_live_view "gorunum ekranda mi" sorusunu
    // pencerenin guncel durumuna sorar (ters sirada ilk onizleme kareleri ilk zamanlayici turuna kadar dusurulurdu).
    publish_measure();
    if (live) start_live_view();
    // Kare sayaci yalniz canli test ekrani acikken baglanir; kapaliyken sahne dongusune hic dokunulmaz.
    benchmark::FrameTap* const tap = live ? &frame_tap_ : nullptr;
    const benchmark::PreviewCallback* const preview = live ? &live_preview_cb_ : nullptr;
    env_.begin_run_marker("balance");
    bench_worker_ = spawn_worker([this, quick = vm_.measure.mode != core::measure::TestMode::Full, tap, preview,
                                  hwnd = env_.window_hwnd()] {
        // Uyumluluk B3 (bosluk 25): test suresince bu surec EcoQoS'tan cikar; is parcacigi bitince onceki durum geri yazilir.
        platform::adapters::WinSelfThrottleApi self_throttle;
        core::adapters::windows::ScopedExecutionSpeedOptOut eco_opt_out(self_throttle);
        const auto t0 = GetTickCount64();
        bb::BalanceOptions o;
        // Uyumluluk B3: ornekleyici (~10 Hz) pencere simge durumunda gorulen son ani yazar; olcum penceresi icinde
        // (ya da adim biterken) simge durumundaysa adim gecersiz sayilir. Pencere yoksa (tepside acilmamis) denetim yok.
        std::atomic<std::int64_t> minimized_us{std::numeric_limits<std::int64_t>::min()};
        if (hwnd)
            o.window_minimized_during = [hwnd, &minimized_us](std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point) {
                return IsIconic(hwnd) != FALSE || minimized_us.load(std::memory_order_relaxed) >= steady_us(begin);
            };
        o.quick = quick;  // Hizli (1 + 4 sn) / Tam (2 + 8 sn)
        o.frame_tap = tap;
        o.preview = preview;  // A4: nullptr = onizleme uretilmez
        // Spec C1 / C2 (salt okuma): CPU hiz sayaclari kosu boyunca ~2 Hz, PCIe linki S3 surerken okunur. A3: ilk
        // adimdan once ~1 sn test bagdastiricisindaki BASKA sureclerin 3D kullanimi (arka plan GPU karistiricisi).
        platform::metrics::BalanceCheckSampler checks;
        std::optional<double> other_gpu_pct;
        const std::vector<std::uint32_t> compositor = session_compositor_pids();  // denge_v2: baska program sayilmaz
        o.before_steps = [&checks, &other_gpu_pct, &compositor](const benchmark::AdapterInfo& adapter, const std::atomic<bool>& cancel) {
            checks.set_adapter(adapter.vendor_id, adapter.device_id);
            {   // R-E1: gecici arka plan yuku (tarama, indirme, derleme) gecene kadar bekle; gecmezse test yine baslar, karar kurali degismez.
                std::vector<HANDLE> exempt;
                for (const auto pid : compositor)
                    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) exempt.push_back(h);
                core::metrics::wait_for_calm_cpu([&exempt] { return read_cpu_times(GetCurrentProcess(), exempt); },
                                                 [&cancel] {
                                                     for (DWORD waited = 0; waited < kBalanceCalmPollMs && !cancel.load(); waited += kBalanceSampleIntervalMs) Sleep(kBalanceSampleIntervalMs);
                                                 },
                                                 [&cancel] { return cancel.load(); }, core::diagnosis::kBalanceOtherCpuMaxPct, kBalanceCalmMaxPolls);
                for (HANDLE h : exempt) CloseHandle(h);
            }
            other_gpu_pct = ef::measurement::sample_other_process_gpu_pct(adapter.luid, GetCurrentProcessId(),
                                                                         ef::measurement::kOtherGpuSampleSeconds, cancel, compositor);
        };
        o.progress = [this, t0, &checks, timing = bb::effective_timing(o)](const bb::StepProgress& p) {
            bb::notify_check_sampler(checks, p, timing);
            if (p.finished) return;
            const double el = static_cast<double>(GetTickCount64() - t0) / 1000.0;
            std::string stage(core::measure::balance_stage_key(static_cast<std::uint32_t>(p.id) + 1u));
            post([this, stage = std::move(stage), el] {
                if (vm_.measure.phase == core::measure::TestPhase::Stopping) return;
                vm_.measure.phase = core::measure::TestPhase::Running;
                vm_.measure.stage_key = stage;
                vm_.measure.elapsed_s = el;
                publish_measure();
            });
        };
        // Guc durumu kosunun basinda ve sonunda okunur (kosu sirasinda fis cekilmis olabilir).
        const BalanceEnvironment env_before = read_balance_environment();
        bb::BalanceReport rep;
        bool crashed = false;
        std::vector<BalanceSample> samples;
        try {
            std::atomic<bool> sampling{true};
            std::thread sampler = spawn_worker([&samples, &sampling, &compositor, &minimized_us, hwnd] {
                // denge_v2: bilesimci surec sureleri (yalniz sorgu hakki; acilamazsa sayilmaz ve olcum eski kurala duser).
                std::vector<HANDLE> exempt;
                for (const auto pid : compositor)
                    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) exempt.push_back(h);
                struct Closer {
                    std::vector<HANDLE>& v;
                    ~Closer() {
                        for (HANDLE h : v) CloseHandle(h);
                    }
                } closer{exempt};
                // NVIDIA varsa surucu limit bayragi (salt okuma); yoksa bayrak hic okunmaz (limit yok denmez).
                auto nv = platform::nvidia::Nvapi::open();
                std::size_t tick = 0;
                while (sampling.load(std::memory_order_relaxed)) {
                    BalanceSample s;
                    s.t = std::chrono::steady_clock::now();
                    s.cpu = read_cpu_times(GetCurrentProcess(), exempt);
                    if (hwnd && IsIconic(hwnd)) minimized_us.store(steady_us(s.t), std::memory_order_relaxed);
                    if (nv.ok() && tick % kBalanceVendorEvery == 0)
                        if (const auto tel = nv.value()->telemetry(); tel.perf_decrease_mask) {
                            s.perf_mask = *tel.perf_decrease_mask;
                            if (tel.gpu_utilization_pct) s.gpu_load_pct = static_cast<double>(*tel.gpu_utilization_pct);
                            s.gpu_clock_mhz = tel.graphics_clock_mhz;
                        }
                    ++tick;
                    samples.push_back(s);
                    Sleep(kBalanceSampleIntervalMs);
                }
            });
            try {
                checks.start();
                rep = bb::run_balance(o, bench_cancel_);
            } catch (...) {
                sampling = false;  // hata yolunda da ornekleyici kapatilir (joinable terminate yok)
                if (sampler.joinable()) sampler.join();
                throw;
            }
            sampling = false;
            if (sampler.joinable()) sampler.join();
        } catch (...) {
            crashed = true;
        }
        checks.stop();
        // C1 / C2 / A3: denetim sonuclari ve kural girdileri (arka plan GPU karistiricisi, CPU hiz notu). Olay gunlugu
        // yalniz tamamlanan kosuda okunur. Okunamayan deger bos kalir (0 yazilmaz).
        core::measure::BalanceRunChecks run_checks;
        if (!crashed && rep.ok) {
            const auto fw = platform::diagnostics::count_processor_speed_limit_events_since_boot();
            run_checks = bb::collect_run_checks<platform::metrics::SteadyWindow>(rep, checks, other_gpu_pct,
                                                                                  fw.ok ? std::optional<std::uint32_t>(fw.count) : std::nullopt);
            bb::apply_run_checks(rep.inputs, run_checks);
        }
        const BalanceEnvironment env_after = read_balance_environment();
        // Karistirici girdileri: yalniz adimlarin GERCEK olcum pencerelerine dusen ornekler. Ornek yoksa "diger surec
        // CPU'su" bilinmiyor kalir (0 yazilmaz; kural bunu not olarak soyler).
        {
            const bool nvidia = rep.adapter.vendor_id == 0x10DE;  // bayrak yalniz cizen kart NVIDIA ise kullanilir
            std::vector<core::metrics::TimeWindow> windows;
            for (const auto& st : rep.steps)
                if (st.status == bb::StepStatus::Measured && st.measure_end > st.measure_begin)
                    windows.push_back({steady_us(st.measure_begin), steady_us(st.measure_end)});
            std::vector<core::metrics::CpuTimeReading> cpu;
            // denge_v3: tek bir isi/guc bayragi tum kosuyu iptal etmez. drops.cpp ile ayni kural (balance_vendor_limit).
            std::vector<core::diagnosis::LimitSample> limit_windows;
            for (const auto& s : samples) {
                const auto t_us = steady_us(s.t);
                if (s.cpu) cpu.push_back({t_us, *s.cpu});
                const bool in_window = std::any_of(windows.begin(), windows.end(), [t_us](const core::metrics::TimeWindow& w) { return t_us >= w.begin_us && t_us <= w.end_us; });
                if (!in_window) continue;
                if (nvidia && s.perf_mask) {
                    core::diagnosis::LimitSample ls;
                    ls.caps = core::diagnosis::perf_caps_from_mask(*s.perf_mask);
                    ls.load_pct = s.gpu_load_pct;
                    ls.clock_mhz = s.gpu_clock_mhz;
                    limit_windows.push_back(ls);
                }
            }
            const auto vendor_limit = core::diagnosis::balance_vendor_limit(limit_windows);
            auto& c = rep.inputs.confounders;
            c.on_battery = env_before.on_battery || env_after.on_battery;
            c.battery_saver = env_before.battery_saver || env_after.battery_saver;
            c.power_saver_plan = env_before.power_saver_plan || env_after.power_saver_plan;
            c.efficiency_overlay = env_before.efficiency_overlay || env_after.efficiency_overlay;
            {   // P6: test tumlesik kartta mi kostu (ayrik kart varken)? Okunamayan LUID -> iddia yok.
                const auto gp = platform::readiness::read_gpu_preference();
                c.ran_on_integrated_gpu = core::diagnosis::ran_on_integrated_with_discrete(rep.adapter.luid, gp.high_perf_luid,
                                                                                         gp.min_power_luid, gp.hardware_adapters);
            }
            c.vendor_throttle = vendor_limit == core::diagnosis::VendorLimit::Throttle;
            c.vendor_limit_flag = vendor_limit == core::diagnosis::VendorLimit::FlagOnly;
            c.vendor_limit_unverified = vendor_limit == core::diagnosis::VendorLimit::Unverifiable;
            if (const auto other = core::metrics::other_cpu_time_percent_in(cpu, windows); other.has_value()) c.other_cpu_pct = *other.value();
            run_checks.other_cpu_pct = c.other_cpu_pct;  // saha teshisi: kayitta saklanir
            // Uyumluluk B8 (bosluk 17): AMD/Intel kartta surucu sinir telemetrisi yok -> "desteklenmiyor" notu (hukum degismez).
            c.vendor_limit_no_api = core::diagnosis::vendor_limit_api_missing(rep.adapter.vendor_id);
            // Uyumluluk B8 (bosluk 8): logs/balance.log — kosu satiri + adim basina bir satir (destek paketine girer).
            {
                core::measure::BalanceRunLog rl;
                rl.rule = core::diagnosis::kBalanceRuleVersion;
                if (rep.adapter.vendor_id != 0) rl.adapter_vendor_id = rep.adapter.vendor_id;
                rl.quick = rep.quick;
                rl.ok = rep.ok && !crashed;
                rl.cancelled = rep.cancelled;
                rl.error = crashed ? std::string_view("internal_error") : std::string_view(rep.error);
                rl.retries = rep.retries;
                rl.other_cpu_pct = c.other_cpu_pct;
                platform::append_balance_log(core::measure::balance_run_log_line(rl));
                for (const auto& st : rep.steps) {
                    if (st.attempts == 0 && st.status == bb::StepStatus::NotRun) continue;  // kosulmayan adim satir uretmez
                    core::measure::BalanceStepLog sl;
                    sl.step = bb::to_string(st.plan.id);
                    sl.status = bb::to_string(st.status);
                    sl.attempts = st.attempts;
                    sl.frames = st.stats.frames;
                    sl.fps = st.stats.fps;
                    sl.cv = st.stats.cv;
                    sl.gpu_busy = st.stats.gpu_busy_ratio;
                    sl.disjoint_frames = st.stats.disjoint_frames;
                    sl.invalid = st.stats.reason;
                    sl.error = st.error;
                    if (st.status == bb::StepStatus::Measured && st.measure_end > st.measure_begin) {
                        const std::vector<core::metrics::TimeWindow> one{{steady_us(st.measure_begin), steady_us(st.measure_end)}};
                        if (const auto other = core::metrics::other_cpu_time_percent_in(cpu, one); other.has_value()) sl.other_cpu_pct = *other.value();
                    }
                    platform::append_balance_log(core::measure::balance_step_log_line(sl));
                }
            }
        }
        post([this, rep = std::move(rep), run_checks = std::move(run_checks), t0, crashed] {
            env_.end_run_marker();
            stop_live_view();  // A4: kosu bitti (her donus yolu); asagidaki set_measure pencereyi normal sayfaya dondurur
            using core::measure::TestPhase;
            vm_.measure.elapsed_s = static_cast<double>(GetTickCount64() - t0) / 1000.0;
            vm_.measure.stage_key.clear();
            const auto fail = [this](std::string reason) {
                vm_.measure.phase = TestPhase::Failed;
                vm_.measure.fail_reason = std::move(reason);
                publish_measure();
            };
            if (crashed) return fail("internal_error");  // M11: neden + sonraki adim; onceki sonuc korunur
            if (rep.cancelled) {
                vm_.measure.phase = vm_.measure.gate_stopped ? TestPhase::Failed : TestPhase::Cancelled;
                if (vm_.measure.gate_stopped) vm_.measure.fail_reason = "game_started";  // "kullanici durdurdu" denmez
                vm_.measure.gate_stopped = false;
                publish_measure();
                return;
            }
            // Kosu hata ile bittiyse (donanim karti yok, cihaz kaybi, zaman asimi...) hukum uretilmez, kayit yazilmaz.
            if (!rep.ok) return fail(core::measure::balance_fail_reason(rep.error));
            const auto result = core::diagnosis::evaluate_balance(rep.inputs);
            core::insights::HistoryEntry he;
            he.when_unix_ms = now_unix_ms();
            he.source = std::string(core::measure::kBalanceSource);
            he.title = std::string(benchmark::kArenaVersion);  // kayit kimligi; UI yerellestirilmis basligi gosterir
            he.fingerprint = rep.fingerprint;
            he.app_version = vm_.version;
            he.scene_version = std::string(benchmark::kArenaVersion);
            he.protocol_version = std::string(core::insights::kBalanceProtocolVersion);
            he.diagnosis_version = std::string(core::diagnosis::kBalanceRuleVersion);  // kendi kural surumu (kalibrasyon kapisina bagli degil)
            he.conditions = run_conditions();
            he.conditions["mode"] = rep.quick ? "quick" : "full";
            he.conditions["preview"] = rep.preview ? "on" : "off";  // farkli durumlar karsilastirilmaz
            {
                // Kosul degeri en cok 160 bayt saklanir (history.cpp). Bayt uzerinden kesmek cok baytli bir UTF-8
                // karakterini bolebilirdi: ad, sigana kadar SONDAN karakter atilarak kisaltilir.
                std::wstring wide = rep.adapter.name;
                std::string name = platform::to_utf8(wide);
                while (name.size() > 160 && !wide.empty()) {
                    wide.pop_back();
                    name = platform::to_utf8(wide);
                }
                if (!name.empty()) he.conditions["adapter"] = std::move(name);
            }
            if (rep.adapter.driver_concurrent_creates && rep.adapter.driver_command_lists)
                he.conditions["d3d_threading"] = std::string(*rep.adapter.driver_concurrent_creates ? "creates_on" : "creates_off") +
                                                 (*rep.adapter.driver_command_lists ? ",lists_on" : ",lists_off");
            he.metrics = core::measure::balance_metrics(result);
            core::measure::add_balance_check_metrics(he.metrics, run_checks);  // C1 / C2 olcumleri + kosu kosullari
            for (auto& [k, v] : core::measure::balance_check_conditions(run_checks)) he.conditions[k] = std::move(v);
            he.exclusions = core::measure::balance_exclusions(result);
            he.run_valid = core::measure::balance_run_valid(result);
            core::measure::ResultView r;
            r.source = core::measure::TestSource::Builtin;
            r.when_unix_ms = he.when_unix_ms;
            r.diagnosis = core::measure::Diagnosis::Insufficient;  // baslik denge gorunumunden gelir
            r.show_confidence = false;
            r.mode = rep.quick ? core::measure::TestMode::Quick : core::measure::TestMode::Full;
            r.run_valid = he.run_valid;
            r.balance = core::measure::balance_view_from_history(he);  // kart her zaman kayittan (yeniden acilista ayni)
            if (!r.balance) return fail("internal_error");
            vm_.measure.phase = TestPhase::Done;
            vm_.measure.result = r;
            vm_.measure.history.insert(vm_.measure.history.begin(), r);
            cap_measure_history(vm_.measure.history);
            // Olcumler sayfasindaki "onceki kayda gore ortalama FPS farki" satiri denge kaydi icin uretilmez; iki denge
            // kaydi Gecmis sayfasinda ortak politika (ayni protokol / sahne surumu / onizleme durumu) ile karsilastirilir.
            vm_.measure.comparison = core::measure::ComparisonView{};
            env_.history().add(std::move(he));
            env_.save_history();
            vm_.measure.balance_latest = r.balance;  // Panel basligi
            vm_.history.selected.clear();  // yeni kayit basa geldi: eski secim indeksleri kayar
            env_.refresh_history_compare();
            publish_measure();
            // C2: yuk altinda okunan PCIe linki bilesen raporuna (anakart satiri) yansir; denge kaydi gecmis sinirinda eski
            // Uygulama Testi'ni de dusurebilir. Kanit degismediyse rapor yeniden toplanmaz.
            env_.sync_bench_evidence();
        });
    });
}

}  // namespace ef::app
