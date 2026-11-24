#pragma once
// Spec 2026-11-12 C1 + C2 + A3 (D6): denge kosusu ile salt okuma uyum denetimleri arasindaki bag (yalniz baslik).
// Kosucu kutuphanesi platform okuyucularina baglanmaz; ornekleyici turu sablon parametresidir
// (platform::metrics::BalanceCheckSampler). Uygulama ve gercek cihaz denemesi (arena_probe) AYNI yolu kullanir.
//  - PCIe linki kPcieStep (S3: GPU kapasitesi, 1440p) surerken okunur; yuk kaniti o adimin GPU mesgul oranidir.
//  - CPU hiz sayaclari CPU yuklu adimlarin (S1, K1-K3, T1) olcum pencerelerinde degerlendirilir.
//  - Arka plan GPU yuku ilk adimdan once olculur (BalanceOptions.before_steps); deger cagirandan gelir.

#include <cstdint>
#include <optional>
#include <vector>

#include "benchmark/balance/balance_runner.h"
#include "core/measure/balance_view.h"

namespace ef::benchmark::balance {

inline constexpr StepId kPcieStep = StepId::S3;
// Isinma bittikten sonra ilk okuma icin ek pay (hedef kurulumu + ilk kareler).
inline constexpr double kPcieReadDelayPadS = 0.5;

// Bu adim CPU'yu yukluyor mu (C1 pencereleri).
constexpr bool cpu_loaded_step(StepKind k) noexcept {
    return k == StepKind::CpuCapacity || k == StepKind::Coupled || k == StepKind::CpuRepeat;
}

// Kosucunun adim bildirimi -> ornekleyici: PCIe adimi baslarken okuma kurulur, biterken kapatilir.
template <class Sampler>
void notify_check_sampler(Sampler& sampler, const StepProgress& p, const StepTiming& timing) {
    if (p.id != kPcieStep) return;
    if (p.finished) sampler.disarm_pcie();
    else sampler.arm_pcie(timing.warmup_s + kPcieReadDelayPadS);
}

// Kosu bittikten ve ornekleyici durdurulduktan sonra: denetim sonuclari. `Window` ornekleyicinin pencere turudur
// ({begin, end} steady_clock).
template <class Window, class Sampler>
core::measure::BalanceRunChecks collect_run_checks(const BalanceReport& rep, const Sampler& sampler, std::optional<double> other_gpu_pct,
                                                   std::optional<std::uint32_t> firmware_limit_events) {
    core::measure::BalanceRunChecks c;
    std::vector<Window> cpu_windows;
    for (const StepResult& s : rep.steps)
        if (s.status == StepStatus::Measured && cpu_loaded_step(s.plan.kind) && s.measure_end > s.measure_begin)
            cpu_windows.push_back(Window{s.measure_begin, s.measure_end});
    c.cpu_speed = sampler.cpu_speed(cpu_windows);
    const StepResult& link_step = rep.step(kPcieStep);
    if (link_step.status == StepStatus::Measured && link_step.measure_end > link_step.measure_begin)
        c.pcie = sampler.pcie(Window{link_step.measure_begin, link_step.measure_end}, link_step.stats.gpu_busy_ratio);
    c.cpu_firmware_limit_events = firmware_limit_events;
    c.other_gpu_pct = other_gpu_pct;
    c.retries = rep.retries;
    return c;
}

// Denetim sonuclari -> kural girdileri (karistirici + notlar). Hukmu kural verir.
inline void apply_run_checks(core::diagnosis::BalanceInputs& in, const core::measure::BalanceRunChecks& c) {
    in.confounders.other_gpu_pct = c.other_gpu_pct;
    in.cpu_speed_drop = c.cpu_speed.noted();
    in.cpu_firmware_limit_logged = c.cpu_firmware_limit_events && *c.cpu_firmware_limit_events > 0;
}

}  // namespace ef::benchmark::balance
