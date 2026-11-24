#include "benchmark/endurance/endurance_runner.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "benchmark/scene/d3d_renderer.h"

namespace ef::benchmark {

namespace {
using Clock = std::chrono::steady_clock;
double seconds_between(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }

// Kalici ornekleyici: oturum acildiktan sonra baslar, kosu bitince durur. Okumalar kosucu saatiyle damgalanir;
// dilim sonunda yalniz o dilimin cizim penceresindekiler kullanilir, eskiler atilir.
class Sampler {
public:
    Sampler(const std::function<EnduranceTelemetry()>& probe, Clock::time_point t0) : probe_(probe), t0_(t0) {
        if (probe_) th_ = std::thread([this] { loop(); });
    }
    ~Sampler() { stop(); }
    void stop() {
        run_ = false;
        if (th_.joinable()) th_.join();
    }
    // [a, b] penceresindeki okumalari kopyalar; a'dan onceki okumalari siler (bellek sinirli kalir).
    std::vector<TelemetryReading> take(double a, double b) {
        std::lock_guard lock(m_);
        std::vector<TelemetryReading> out;
        for (const auto& r : readings_)
            if (r.t_s >= a && r.t_s <= b) out.push_back(r);
        readings_.erase(std::remove_if(readings_.begin(), readings_.end(), [b](const TelemetryReading& r) { return r.t_s <= b; }), readings_.end());
        return out;
    }

private:
    void loop() {
        while (run_.load()) {
            const auto t = probe_();
            TelemetryReading r;
            r.t_s = seconds_between(t0_, Clock::now());  // okuma donus ani: cizim penceresi icinde mi buna gore
            r.gpu_clock_mhz = t.gpu_clock_mhz;
            r.gpu_load_pct = t.gpu_load_pct;
            r.thermal = t.thermal;
            r.power = t.power;
            {
                std::lock_guard lock(m_);
                readings_.push_back(r);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }
    std::function<EnduranceTelemetry()> probe_;
    Clock::time_point t0_;
    std::atomic<bool> run_{true};
    std::mutex m_;
    std::vector<TelemetryReading> readings_;
    std::thread th_;
};
}  // namespace

EnduranceRun run_endurance(const SceneConfig& scene, const EnduranceBudget& budget, const std::atomic<bool>& cancel,
                           const EnduranceProbes& probes) {
    EnduranceRun out;
    // Cihaz + sahne bir kez kurulur; hazirlik suresi kosu saatine girmez (soguk pencere cizimle baslar).
    SceneConfig slice = scene;
    slice.warmup_frames = 0;
    slice.warmup_s = 0;
    slice.measure_s = 1.0;  // ~1 sn'lik dilim: ornek ve butce/iptal kontrol araligi
    slice.frame_tap = nullptr;
    slice.preview = nullptr;
    SceneSession session;
    if (cancel.load()) {
        out.stop = EnduranceStop::Cancelled;
        out.report = analyze_endurance(out.samples, out.stop);
        return out;
    }
    if (!session.open(slice)) {
        out.stop = EnduranceStop::DeviceLost;  // sahne calisamadi: kullanici iptali degil
        out.report = analyze_endurance(out.samples, out.stop);
        return out;
    }
    const auto& adapter = session.adapter();
    const bool telemetry_valid = probes.telemetry && adapter.kind == RendererKind::Hardware &&
                                 (probes.vendor_id == 0 || probes.vendor_id == adapter.vendor_id);
    const auto t0 = Clock::now();
    if (probes.on_start) probes.on_start(telemetry_valid);
    Sampler sampler(telemetry_valid ? probes.telemetry : std::function<EnduranceTelemetry()>{}, t0);
    SensorWatch watch;
    double next_error_check = 30.0;
    for (;;) {
        if (cancel.load()) {
            out.stop = EnduranceStop::Cancelled;
            break;
        }
        const auto r = session.run_slice(slice, cancel);
        if (r.cancelled) {
            out.stop = EnduranceStop::Cancelled;
            break;
        }
        EnduranceSample s;
        s.t_s = seconds_between(t0, Clock::now());
        if (r.ok && !r.frame_ms.empty()) {
            const double sum = std::accumulate(r.frame_ms.begin(), r.frame_ms.end(), 0.0);
            if (sum > 0) s.fps = 1000.0 * static_cast<double>(r.frame_ms.size()) / sum;
            if (telemetry_valid) {
                // Yalniz bu dilimin gercek cizim penceresi: hazirlik ve dilim sonu bosaltmasi disarida.
                const double a = seconds_between(t0, r.measure_begin), b = seconds_between(t0, r.measure_end);
                telemetry_in_window(sampler.take(a, b), a, b, s);
            }
        }
        if (r.ok) {
            out.samples.push_back(s);
            if (probes.on_sample) probes.on_sample(s);
        }
        if (r.ok && telemetry_valid && watch.lost(s.gpu_clock_mhz.has_value())) {
            out.stop = EnduranceStop::SensorLost;
            break;
        }
        if (probes.new_hardware_error && s.t_s >= next_error_check) {
            next_error_check = s.t_s + 30.0;
            if (probes.new_hardware_error()) {
                out.stop = EnduranceStop::HardwareError;
                break;
            }
        }
        if (const auto b = budget_exceeded(budget, s)) {
            out.stop = *b;
            break;
        }
        if (!r.ok) {
            out.stop = EnduranceStop::DeviceLost;  // kullanici iptali degil: raporda ayri gorunur
            break;
        }
        if (planned_duration_reached(budget, s)) {  // gercek kabul bulgusu: tam kosu TimeBudget/gecersiz sayiliyordu
            out.stop = EnduranceStop::Completed;
            break;
        }
    }
    sampler.stop();
    // Son 30 sn veya cihaz kaybi sirasindaki WHEA da sayilir; kullanici iptali kapsam disi.
    if (probes.new_hardware_error && out.stop != EnduranceStop::Cancelled && out.stop != EnduranceStop::HardwareError && probes.new_hardware_error())
        out.stop = EnduranceStop::HardwareError;
    out.report = analyze_endurance(out.samples, out.stop);
    out.report.telemetry_unsupported = !telemetry_valid;  // desteklenmeyen != okunamayan
    return out;
}

}  // namespace ef::benchmark
