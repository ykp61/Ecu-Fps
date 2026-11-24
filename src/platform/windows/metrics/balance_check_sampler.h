#pragma once
// Spec 2026-11-12 C1 + C2: denge kosusu boyunca arka planda calisan SALT OKUMA ornekleyicisi.
//  - C1: ~2 Hz "% Processor Performance" / "% Performance Limit" (CpuSpeedSampler). Kural saftir
//    (core::diagnosis::evaluate_cpu_speed); pencereleri cagiran verir (CPU yuklu adimlarin olcum pencereleri).
//  - C2: PCIe adimi (S3) surerken GPU linki (DEVPKEY genislik / hiz, ust koprunun azamisi) ~2 sn arayla yeniden
//    okunur. Sonuc yalniz adimin SON olcum penceresine dusen okumadan alinir: adim tekrarlandiysa ya da okuma
//    pencere disinda kaldiysa "yuk altinda okunmadi" sayilir.
// Hicbir ayar yazmaz, GPU'ya is gondermez; yonetici gerekmez. GPU motoru sayaclari burada TOPLANMAZ (olcum sirasinda
// CPU harcar). start()/stop() ayni is parcacigindan; set_adapter/arm_pcie/disarm_pcie kosucu is parcacigindan
// cagrilabilir. Sonuclar stop() dondukten sonra okunur.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "core/diagnosis/mismatch.h"

namespace ef::platform::metrics {

struct SteadyWindow {
    std::chrono::steady_clock::time_point begin{}, end{};
};

class BalanceCheckSampler {
public:
    BalanceCheckSampler() = default;
    ~BalanceCheckSampler();
    BalanceCheckSampler(const BalanceCheckSampler&) = delete;
    BalanceCheckSampler& operator=(const BalanceCheckSampler&) = delete;

    void start();
    void stop();

    // Cizen bagdastirici (PCI uretici / aygit kimligi). 0 / 0: PCIe okunmaz.
    void set_adapter(std::uint32_t vendor_id, std::uint32_t device_id) noexcept;
    // PCIe adimi basladi: `delay_s` sonra ilk okuma, sonra kPcieReadIntervalS arayla. disarm: adim bitti.
    void arm_pcie(double delay_s) noexcept;
    void disarm_pcie() noexcept;

    // ---- stop() sonrasi ----
    core::diagnosis::CpuSpeedFinding cpu_speed(std::span<const SteadyWindow> cpu_windows) const;
    // Pencereye dusen SON okuma; `gpu_busy_ratio` o adimin GPU mesgul oranidir (yuk kaniti). Okuma yoksa nullopt.
    std::optional<core::diagnosis::PcieLoadReading> pcie(const SteadyWindow& window, std::optional<double> gpu_busy_ratio) const;
    std::size_t cpu_sample_count() const noexcept { return cpu_.size(); }
    std::size_t pcie_read_count() const noexcept { return pcie_.size(); }

    static constexpr int kCpuSampleIntervalMs = 500;
    static constexpr double kPcieReadIntervalS = 2.0;

private:
    struct CpuAt {
        std::chrono::steady_clock::time_point t{};
        std::optional<double> performance_pct, limit_pct;
    };
    struct PcieAt {
        std::chrono::steady_clock::time_point t{};
        core::diagnosis::PcieLoadReading reading;
    };
    void run();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint32_t> vendor_{0}, device_{0};
    std::atomic<bool> pcie_armed_{false};
    std::atomic<std::int64_t> pcie_next_us_{0};  // steady_clock, mikrosaniye
    std::vector<CpuAt> cpu_;
    std::vector<PcieAt> pcie_;
};

}  // namespace ef::platform::metrics
