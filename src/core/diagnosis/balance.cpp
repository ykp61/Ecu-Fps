#include "core/diagnosis/balance.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ef::core::diagnosis {

namespace {

// Saniyelik ornek ust siniri (bellek korumasi; adim suresi saniyelerle olculur).
constexpr std::size_t kMaxSeconds = 3600;

bool usable_interval(double ms) noexcept { return std::isfinite(ms) && ms > 0.0; }

enum class Band { Gpu, Balanced, Cpu };

Band band_of(double ratio) noexcept {
    if (ratio >= kBalanceCpuLimitedRatio) return Band::Cpu;
    if (ratio <= kBalanceGpuLimitedRatio) return Band::Gpu;
    return Band::Balanced;
}

BalanceVerdict verdict_of(Band b) noexcept {
    switch (b) {
        case Band::Cpu: return BalanceVerdict::CpuLimited;
        case Band::Gpu: return BalanceVerdict::GpuLimited;
        case Band::Balanced: break;
    }
    return BalanceVerdict::Balanced;
}

bool valid_step(const StepStats& s) noexcept { return s.state == StepState::Valid && std::isfinite(s.fps) && s.fps > 0.0; }

// Iki olcum arasindaki uyum = kucuk / buyuk. Tolerans disindaysa suruklenme vardir.
double agreement(double a, double b) noexcept { return std::min(a, b) / std::max(a, b); }
bool drifted(double a, double b) noexcept { return agreement(a, b) < 1.0 - kBalanceDriftTolerance; }

void undetermined(BalanceRow& row, std::string_view reason) noexcept {
    row.verdict = BalanceVerdict::Undetermined;
    row.reason = reason;
}

}  // namespace

std::vector<double> per_second_fps(std::span<const double> frame_ms) {
    double total = 0;
    for (const double d : frame_ms)
        if (usable_interval(d)) total += d;
    const double whole = std::floor(total / 1000.0);
    const std::size_t windows = whole >= static_cast<double>(kMaxSeconds) ? kMaxSeconds : static_cast<std::size_t>(whole);
    std::vector<double> out(windows, 0.0);
    if (windows == 0) return out;
    double t = 0;
    for (const double d : frame_ms) {
        if (!usable_interval(d)) continue;
        const double start = t, end = t + d;
        t = end;
        const double first = std::floor(start / 1000.0);
        if (first >= static_cast<double>(windows)) break;  // kalan kareler eksik son saniyededir
        for (auto k = static_cast<std::size_t>(first); k < windows; ++k) {
            const double w0 = static_cast<double>(k) * 1000.0, w1 = w0 + 1000.0;
            if (w0 >= end) break;
            const double overlap = std::min(end, w1) - std::max(start, w0);
            if (overlap > 0.0) out[k] += overlap / d;
        }
    }
    return out;
}

double median_of(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

std::optional<double> coefficient_of_variation(std::span<const double> values) noexcept {
    const std::size_t n = values.size();
    if (n < 2) return std::nullopt;
    double sum = 0;
    for (const double v : values) sum += v;
    const double mean = sum / static_cast<double>(n);
    if (!std::isfinite(mean) || !(mean > 0.0)) return std::nullopt;
    double sq = 0;
    for (const double v : values) sq += (v - mean) * (v - mean);
    const double cv = std::sqrt(sq / static_cast<double>(n - 1)) / mean;
    if (!std::isfinite(cv)) return std::nullopt;
    return cv;
}

FrameWindow measured_window(std::span<const double> frame_ms, double warmup_s, double measure_s) noexcept {
    const std::size_t n = frame_ms.size();
    const double w0 = warmup_s > 0.0 ? warmup_s * 1000.0 : 0.0;
    const double w1 = measure_s > 0.0 ? w0 + measure_s * 1000.0 : std::numeric_limits<double>::infinity();
    FrameWindow w{n, n};
    bool started = false;
    double t = 0;  // karenin baslangici: gonderilmeden once gecen sure
    for (std::size_t i = 0; i < n; ++i) {
        if (!started && t >= w0) {
            w.first = i;
            started = true;
        }
        if (started && t >= w1) {
            w.last = i;
            break;
        }
        if (usable_interval(frame_ms[i])) t += frame_ms[i];
    }
    return w;
}

std::string_view step_invalid_reason(std::size_t frames, std::size_t seconds, double cv, std::uint32_t disjoint_frames,
                                     double measure_s, bool gpu_time_required) noexcept {
    if (frames < kBalanceStepMinFrames) return balance_reason::kTooFewFrames;
    const double need = std::max(static_cast<double>(kBalanceStepMinSeconds), measure_s - kBalanceStepSecondsSlack);
    if (!(static_cast<double>(seconds) >= need)) return balance_reason::kTooFewSeconds;
    if (gpu_time_required &&
        static_cast<double>(disjoint_frames) * 100.0 > kBalanceStepMaxDisjointPct * static_cast<double>(frames))
        return balance_reason::kTimestampDisjoint;
    if (!(cv <= kBalanceStepMaxCv)) return balance_reason::kUnstable;  // NaN da kararsiz sayilir
    return {};
}

std::optional<double> gpu_busy_ratio(std::span<const double> gpu_busy_ms, std::span<const double> frame_ms) noexcept {
    double busy = 0, interval = 0;
    std::size_t busy_n = 0, interval_n = 0;
    for (const double b : gpu_busy_ms)
        if (std::isfinite(b) && b >= 0.0) {
            busy += b;
            ++busy_n;
        }
    for (const double d : frame_ms)
        if (usable_interval(d)) {
            interval += d;
            ++interval_n;
        }
    if (busy_n == 0 || interval_n == 0) return std::nullopt;
    const double avg_interval = interval / static_cast<double>(interval_n);
    if (!(avg_interval > 0.0)) return std::nullopt;
    return std::clamp(busy / static_cast<double>(busy_n) / avg_interval, 0.0, 1.0);
}

StepStats summarize_step(std::span<const double> measured_frame_ms, std::span<const double> gpu_busy_ms, double measure_s,
                         std::uint32_t disjoint_frames, bool gpu_time_required) {
    StepStats s;
    for (const double d : measured_frame_ms)
        if (usable_interval(d)) ++s.frames;
    const std::vector<double> fps = per_second_fps(measured_frame_ms);
    s.seconds = fps.size();
    s.cv = coefficient_of_variation(fps).value_or(0.0);
    s.fps = median_of(fps);
    s.gpu_busy_ratio = gpu_busy_ratio(gpu_busy_ms, measured_frame_ms);
    s.disjoint_frames = disjoint_frames;
    s.reason = step_invalid_reason(s.frames, s.seconds, s.cv, disjoint_frames, measure_s, gpu_time_required);
    // denge_v4: GPU suresine dayanmayan adimda (S1/T1) zaman damgasi gecersiz kare payi esigi asarsa GPU zamani
    // guvenilmezdir: oran verilmez (sinir dogrulanamaz -> "en az"); adim FPS'i CPU ritminden geldigi icin gecerli kalir.
    if (!gpu_time_required &&
        static_cast<double>(disjoint_frames) * 100.0 > kBalanceStepMaxDisjointPct * static_cast<double>(s.frames))
        s.gpu_busy_ratio.reset();
    s.state = s.reason.empty() ? StepState::Valid : StepState::Invalid;
    return s;
}

StepStats unmeasurable_step(std::string_view reason) noexcept {
    StepStats s;
    s.state = StepState::Unmeasurable;
    s.reason = reason.empty() ? balance_reason::kNotRun : reason;
    return s;
}

StepStats flag_window_minimized(StepStats stats, bool minimized) noexcept {
    if (!minimized || (stats.state != StepState::Valid && stats.state != StepState::Invalid)) return stats;
    stats.state = StepState::Invalid;
    stats.reason = balance_reason::kWindowMinimized;
    return stats;
}

bool should_retry_step(const StepStats& stats, std::uint32_t attempts, std::uint32_t run_retries) noexcept {
    // denge_v2: gecersiz zaman damgasi da gecici olabilir (GPU saat / guc durumu gecisi): kararsizlik gibi bir kez tekrar.
    if (stats.state != StepState::Invalid ||
        (stats.reason != balance_reason::kUnstable && stats.reason != balance_reason::kTimestampDisjoint))
        return false;
    return attempts >= 1 && attempts < kBalanceStepMaxAttempts && run_retries < kBalanceRunMaxRetries;
}

bool bound_confirmed(StepRole role, const std::optional<double>& ratio) noexcept {
    if (!ratio || !std::isfinite(*ratio)) return false;
    return role == StepRole::CpuCapacity ? *ratio <= kBalanceCpuStepMaxGpuBusy : *ratio >= kBalanceGpuStepMinGpuBusy;
}

VendorLimit balance_vendor_limit(const std::vector<LimitSample>& windows) {
    bool flag_seen = false;
    std::size_t loaded = 0, loaded_clocked = 0;
    std::optional<double> reference_clock_mhz;
    for (const auto& s : windows) {
        if (s.caps && (s.caps->thermal || s.caps->power)) flag_seen = true;
        if (s.load_pct && std::isfinite(*s.load_pct) && *s.load_pct >= kGpuLoadPct) {
            ++loaded;
            if (s.clock_mhz && std::isfinite(*s.clock_mhz) && *s.clock_mhz > 0.0) {
                ++loaded_clocked;
                if (!reference_clock_mhz || *s.clock_mhz > *reference_clock_mhz) reference_clock_mhz = *s.clock_mhz;
            }
        }
    }
    if (!flag_seen) return VendorLimit::None;
    // drops.cpp ile ayni savunma siniri: en az 2 yuk alti pencere yoksa kosu duzeyi teshis yok. Inceleme B4: yuk ya da
    // saat okunamadiysa "saat dusmedi" denemez; kural degerlendirilemedi (ihtiyatli: karistirici).
    if (loaded < 2 || loaded_clocked < 2) return VendorLimit::Unverifiable;
    const PlatformFinding f = evaluate_limits(windows, reference_clock_mhz);
    bool limit = false, clock_drop = false;
    for (const auto e : f.supporting) {
        if (e == PlatformEvidence::PerfCapThermal || e == PlatformEvidence::PerfCapPower) limit = true;
        else if (e == PlatformEvidence::ClockDropUnderLoad) clock_drop = true;
    }
    return limit && clock_drop ? VendorLimit::Throttle : VendorLimit::FlagOnly;
}

std::optional<CpuShares> cpu_shares(const CpuFrameSplit& s) noexcept {
    for (const double v : {s.sim_ms, s.cull_ms, s.submit_ms})
        if (!std::isfinite(v) || v < 0.0) return std::nullopt;
    const double total = s.sim_ms + s.cull_ms + s.submit_ms;
    if (!(total > 0.0)) return std::nullopt;
    return CpuShares{s.sim_ms / total, (s.cull_ms + s.submit_ms) / total};
}

BalanceResult evaluate_balance(const BalanceInputs& in) {
    BalanceResult r;
    const bool cpu_ok = valid_step(in.cpu);
    const bool cpu_repeat_ok = valid_step(in.cpu_repeat);

    // Olculen degerler hukumden bagimsiz olarak tasinir (gecersiz adimin degeri gosterilmez).
    if (cpu_ok) {
        r.cpu_fps = in.cpu.fps;
        r.cpu_lower_bound = !bound_confirmed(StepRole::CpuCapacity, in.cpu.gpu_busy_ratio) ||
                            (cpu_repeat_ok && !bound_confirmed(StepRole::CpuCapacity, in.cpu_repeat.gpu_busy_ratio));
    }
    for (std::size_t i = 0; i < kBalanceRows; ++i) {
        BalanceRow& row = r.rows[i];
        if (valid_step(in.gpu[i])) {
            row.gpu_fps = in.gpu[i].fps;
            row.gpu_lower_bound = !bound_confirmed(StepRole::GpuCapacity, in.gpu[i].gpu_busy_ratio);
        }
        if (valid_step(in.coupled[i])) {
            row.coupled_fps = in.coupled[i].fps;
            row.coupled_gpu_busy = in.coupled[i].gpu_busy_ratio;
        }
        if (r.cpu_fps && row.gpu_fps) {
            row.ratio = *row.gpu_fps / *r.cpu_fps;
            if (row.coupled_fps) row.coupled_ratio = *row.coupled_fps / std::min(*r.cpu_fps, *row.gpu_fps);
        }
    }

    // Suruklenme (T1/S1 ve T2/S2). Aralik yalniz olculen iki degerden kurulur.
    double cpu_lo = 0, cpu_hi = 0;
    if (cpu_ok && cpu_repeat_ok) {
        cpu_lo = cpu_hi = in.cpu.fps;
        if (drifted(in.cpu.fps, in.cpu_repeat.fps)) {
            r.cpu_drift = true;
            cpu_lo = std::min(in.cpu.fps, in.cpu_repeat.fps);
            cpu_hi = std::max(in.cpu.fps, in.cpu_repeat.fps);
            r.cpu_fps_lo = cpu_lo;
            r.cpu_fps_hi = cpu_hi;
            r.notes.push_back(balance_note::kCpuDrift);
        }
    }
    const bool gpu_repeat_known = valid_step(in.gpu[0]) && valid_step(in.gpu_repeat);
    double gpu_agreement = 1.0;  // T2/S2 uyumu; suruklenme yoksa 1
    if (gpu_repeat_known) {
        if (!bound_confirmed(StepRole::GpuCapacity, in.gpu_repeat.gpu_busy_ratio)) r.rows[0].gpu_lower_bound = true;
        if (drifted(in.gpu[0].fps, in.gpu_repeat.fps)) {
            r.gpu_drift = true;
            gpu_agreement = agreement(in.gpu[0].fps, in.gpu_repeat.fps);
            r.rows[0].gpu_fps_lo = std::min(in.gpu[0].fps, in.gpu_repeat.fps);
            r.rows[0].gpu_fps_hi = std::max(in.gpu[0].fps, in.gpu_repeat.fps);
            r.notes.push_back(balance_note::kGpuDrift);
        }
    }

    // Notlar (hukmu engellemez).
    if (in.cpu_speed_drop) r.notes.push_back(balance_note::kCpuSpeedDrop);
    if (in.cpu_speed_drop && in.cpu_firmware_limit_logged) r.notes.push_back(balance_note::kCpuFirmwareLimitLogged);
    if (in.confounders.vendor_limit_flag && !in.confounders.vendor_throttle && !in.confounders.vendor_limit_unverified) r.notes.push_back(balance_note::kVendorLimitFlag);
    if (in.confounders.vendor_limit_no_api) r.notes.push_back(balance_note::kVendorLimitNoApi);
    if (!in.confounders.other_cpu_pct) r.notes.push_back(balance_note::kBackgroundCpuUnknown);
    if (!in.confounders.other_gpu_pct) r.notes.push_back(balance_note::kBackgroundGpuUnknown);
    if (in.cpu_split)
        if (const auto shares = cpu_shares(*in.cpu_split)) {
            r.cpu_sim_share = shares->sim;
            r.cpu_draw_share = shares->draw;
        }

    // Tum satirlari cekimser birakan nedenler. Ortam nedenleri once gelir: gecersiz S1'i de aciklarlar.
    const auto& c = in.confounders;
    if (c.on_battery) r.global_reasons.push_back(balance_reason::kOnBattery);
    if (c.battery_saver) r.global_reasons.push_back(balance_reason::kBatterySaver);
    if (c.power_saver_plan) r.global_reasons.push_back(balance_reason::kPowerSaverPlan);
    if (c.efficiency_overlay) r.global_reasons.push_back(balance_reason::kEfficiencyOverlay);
    if (c.ran_on_integrated_gpu) r.global_reasons.push_back(balance_reason::kRanOnIntegratedGpu);
    if (c.vendor_throttle) r.global_reasons.push_back(balance_reason::kVendorThrottle);
    if (c.vendor_limit_unverified && !c.vendor_throttle) r.global_reasons.push_back(balance_reason::kVendorLimitUnverified);
    if (c.other_cpu_pct && !(*c.other_cpu_pct < kBalanceOtherCpuMaxPct)) r.global_reasons.push_back(balance_reason::kBackgroundCpu);
    if (c.other_gpu_pct && !(*c.other_gpu_pct < kBalanceOtherGpuMaxPct)) r.global_reasons.push_back(balance_reason::kBackgroundGpu);
    if (!cpu_ok) {
        r.global_reasons.push_back(balance_reason::kCpuStepInvalid);
        r.cpu_step_reason = in.cpu.reason;
    } else if (!cpu_repeat_ok) {
        r.global_reasons.push_back(balance_reason::kCpuRepeatInvalid);  // suruklenme denetlenemedi
        r.cpu_repeat_reason = in.cpu_repeat.reason;
    }
    if (!r.global_reasons.empty()) {
        r.global_reason = r.global_reasons.front();
        for (BalanceRow& row : r.rows) undetermined(row, r.global_reason);
        return r;
    }

    for (std::size_t i = 0; i < kBalanceRows; ++i) {
        BalanceRow& row = r.rows[i];
        // A2: cozunurlugun hedefi olusturulamadiysa / bellege sigmadiysa satir "olculemedi"dir. Bu, S adimi
        // kadar ayni cozunurlukteki K adimi icin de gecerlidir (hedef K'de kaybedilmis olabilir); "gecersiz
        // olcum" (Belirlenemedi) ile karistirilmaz.
        const StepStats* unmeasured = nullptr;
        for (const StepStats* s : {&in.gpu[i], &in.coupled[i]})
            if (s->state == StepState::Unmeasurable || s->state == StepState::NotRun) {
                unmeasured = s;
                break;
            }
        if (unmeasured) {
            row.verdict = BalanceVerdict::Unmeasurable;
            row.reason = unmeasured->reason.empty() ? balance_reason::kNotRun : unmeasured->reason;
            continue;
        }
        if (!row.gpu_fps) {
            undetermined(row, balance_reason::kGpuStepInvalid);
            continue;
        }
        if (!row.coupled_fps) {
            undetermined(row, balance_reason::kCoupledStepInvalid);
            continue;
        }
        if (!gpu_repeat_known) {
            undetermined(row, balance_reason::kGpuRepeatInvalid);
            continue;
        }
        // Karar araligi. Tekrari olmayan cozunurluklerde T2/S2 uyumu ayni oranda iki yone uygulanir
        // (GPU tarafindaki isinma butun GPU adimlarini etkiler); bu aralik gosterilmez, yalniz cekimserlik icindir.
        double gpu_lo = *row.gpu_fps, gpu_hi = *row.gpu_fps;
        if (r.gpu_drift) {
            if (i == 0) {
                gpu_lo = *row.gpu_fps_lo;
                gpu_hi = *row.gpu_fps_hi;
            } else {
                gpu_lo = *row.gpu_fps * gpu_agreement;
                gpu_hi = *row.gpu_fps / gpu_agreement;
            }
        }
        row.ratio_lo = gpu_lo / cpu_hi;
        row.ratio_hi = gpu_hi / cpu_lo;
        const Band band = band_of(*row.ratio);
        if (band_of(*row.ratio_lo) != band_of(*row.ratio_hi)) {
            undetermined(row, balance_reason::kDrift);
            continue;
        }
        // "En az" deger yalniz hukmu guclendiriyorsa kullanilir.
        if (r.cpu_lower_bound && band != Band::Gpu) {
            undetermined(row, balance_reason::kCpuLowerBound);
            continue;
        }
        if (row.gpu_lower_bound && band != Band::Cpu) {
            undetermined(row, balance_reason::kGpuLowerBound);
            continue;
        }
        // Birlikte dogrulama.
        if (!(*row.coupled_ratio >= kBalanceCoupledMinRatio && *row.coupled_ratio <= kBalanceCoupledMaxRatio)) {
            undetermined(row, balance_reason::kCoupledMismatch);
            continue;
        }
        if (band != Band::Balanced) {
            const auto& busy = row.coupled_gpu_busy;
            if (!busy || !std::isfinite(*busy)) {
                undetermined(row, balance_reason::kCoupledBusyUnknown);
                continue;
            }
            // GPU bandi: GPU birlikte adimda doymus (>= 0,90). CPU bandi: GPU doymamis (< 0,90; bkz. balance.h).
            const bool agrees = band == Band::Gpu ? *busy >= kBalanceGpuStepMinGpuBusy : *busy < kBalanceCoupledCpuMaxGpuBusy;
            if (!agrees) {
                undetermined(row, balance_reason::kCoupledBusyMismatch);
                continue;
            }
        }
        row.verdict = verdict_of(band);
        row.reason = {};
    }
    return r;
}

std::string_view to_string(BalanceVerdict v) noexcept {
    switch (v) {
        case BalanceVerdict::Undetermined: return "undetermined";
        case BalanceVerdict::CpuLimited: return "cpu";
        case BalanceVerdict::Balanced: return "balanced";
        case BalanceVerdict::GpuLimited: return "gpu";
        case BalanceVerdict::Unmeasurable: return "unmeasurable";
    }
    return "undetermined";
}

std::string_view to_string(StepState s) noexcept {
    switch (s) {
        case StepState::NotRun: return "not_run";
        case StepState::Unmeasurable: return "unmeasurable";
        case StepState::Invalid: return "invalid";
        case StepState::Valid: return "valid";
    }
    return "not_run";
}

int verdict_code(BalanceVerdict v) noexcept {
    switch (v) {
        case BalanceVerdict::Undetermined: return 0;
        case BalanceVerdict::CpuLimited: return 1;
        case BalanceVerdict::Balanced: return 2;
        case BalanceVerdict::GpuLimited: return 3;
        case BalanceVerdict::Unmeasurable: return 4;
    }
    return 0;
}

std::optional<BalanceVerdict> verdict_from_code(double code) noexcept {
    for (const BalanceVerdict v : {BalanceVerdict::Undetermined, BalanceVerdict::CpuLimited, BalanceVerdict::Balanced,
                                   BalanceVerdict::GpuLimited, BalanceVerdict::Unmeasurable})
        if (code == static_cast<double>(verdict_code(v))) return v;
    return std::nullopt;
}

std::vector<std::pair<std::string, double>> balance_history_metrics(const BalanceResult& r) {
    std::vector<std::pair<std::string, double>> out;
    if (r.cpu_fps) {
        out.emplace_back("balance.cpu_fps", *r.cpu_fps);
        out.emplace_back("balance.cpu_lower_bound", r.cpu_lower_bound ? 1.0 : 0.0);
    }
    for (std::size_t i = 0; i < kBalanceRows; ++i) {
        const BalanceRow& row = r.rows[i];
        const std::string key(kBalanceRowKeys[i]);
        if (row.gpu_fps) {
            out.emplace_back("balance.gpu_fps_" + key, *row.gpu_fps);
            out.emplace_back("balance.gpu_lower_bound_" + key, row.gpu_lower_bound ? 1.0 : 0.0);
        }
        if (row.coupled_fps) out.emplace_back("balance.coupled_fps_" + key, *row.coupled_fps);
        out.emplace_back("balance.verdict_" + key, static_cast<double>(verdict_code(row.verdict)));
    }
    return out;
}

}  // namespace ef::core::diagnosis
