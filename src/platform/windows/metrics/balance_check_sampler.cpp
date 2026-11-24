#include "platform/windows/metrics/balance_check_sampler.h"

#include <windows.h>

#include <algorithm>

#include "platform/windows/inventory/health_win.h"
#include "platform/windows/metrics/cpu_speed_sampler.h"

namespace ef::platform::metrics {

namespace {
using Clock = std::chrono::steady_clock;
std::int64_t us_of(Clock::time_point t) noexcept { return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count(); }
double seconds_of(Clock::time_point t) noexcept { return static_cast<double>(us_of(t)) / 1e6; }
constexpr int kTickMs = 50;  // durdurma / PCIe zamanlamasi cozunurlugu
}  // namespace

BalanceCheckSampler::~BalanceCheckSampler() { stop(); }

void BalanceCheckSampler::start() {
    if (thread_.joinable()) return;
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void BalanceCheckSampler::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void BalanceCheckSampler::set_adapter(std::uint32_t vendor_id, std::uint32_t device_id) noexcept {
    vendor_ = vendor_id;
    device_ = device_id;
}

void BalanceCheckSampler::arm_pcie(double delay_s) noexcept {
    pcie_next_us_ = us_of(Clock::now()) + static_cast<std::int64_t>(std::max(0.0, delay_s) * 1e6);
    pcie_armed_ = true;
}

void BalanceCheckSampler::disarm_pcie() noexcept { pcie_armed_ = false; }

void BalanceCheckSampler::run() {
    CpuSpeedSampler speed;
    const bool speed_open = speed.open();
    auto next_cpu = Clock::now() + std::chrono::milliseconds(kCpuSampleIntervalMs);
    while (running_.load(std::memory_order_relaxed)) {
        const auto now = Clock::now();
        if (speed_open && now >= next_cpu) {
            const CpuSpeedReading r = speed.sample();  // son toplamadan beri ortalama
            cpu_.push_back({now, r.performance_pct, r.limit_pct});
            next_cpu = now + std::chrono::milliseconds(kCpuSampleIntervalMs);
        }
        if (pcie_armed_.load(std::memory_order_relaxed) && us_of(now) >= pcie_next_us_.load(std::memory_order_relaxed)) {
            pcie_next_us_ = us_of(now) + static_cast<std::int64_t>(kPcieReadIntervalS * 1e6);
            const std::uint32_t ven = vendor_.load(), dev = device_.load();
            if (ven != 0 && dev != 0) {
                if (const auto link = inventory::read_gpu_link(ven, dev)) {
                    core::diagnosis::PcieLoadReading r;
                    r.cur_width = link->pcie.current_width;
                    r.max_width = link->pcie.max_width;
                    r.cur_gen = link->pcie.current_gen;
                    r.max_gen = link->pcie.max_gen;
                    r.slot_max_width = link->slot_max_width;
                    r.slot_max_gen = link->slot_max_gen;
                    pcie_.push_back({Clock::now(), r});  // okuma bittigi an
                }
            }
        }
        Sleep(kTickMs);
    }
}

core::diagnosis::CpuSpeedFinding BalanceCheckSampler::cpu_speed(std::span<const SteadyWindow> cpu_windows) const {
    std::vector<core::diagnosis::CpuSpeedSample> samples;
    samples.reserve(cpu_.size());
    for (const CpuAt& s : cpu_) samples.push_back({seconds_of(s.t), s.performance_pct, s.limit_pct});
    std::vector<core::diagnosis::CpuSpeedWindow> windows;
    for (const SteadyWindow& w : cpu_windows)
        if (w.end > w.begin) windows.push_back({seconds_of(w.begin), seconds_of(w.end)});
    return core::diagnosis::evaluate_cpu_speed(samples, windows);
}

std::optional<core::diagnosis::PcieLoadReading> BalanceCheckSampler::pcie(const SteadyWindow& window,
                                                                         std::optional<double> gpu_busy_ratio) const {
    if (!(window.end > window.begin)) return std::nullopt;
    const PcieAt* last = nullptr;
    for (const PcieAt& p : pcie_)
        if (p.t >= window.begin && p.t <= window.end) last = &p;
    if (!last) return std::nullopt;
    core::diagnosis::PcieLoadReading r = last->reading;
    if (gpu_busy_ratio) r.gpu_load_pct = std::clamp(*gpu_busy_ratio, 0.0, 1.0) * 100.0;
    return r;
}

}  // namespace ef::platform::metrics
