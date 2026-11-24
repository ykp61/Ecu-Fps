#include "core/diagnosis/platform.h"

#include <algorithm>
#include <map>

namespace ef::core::diagnosis {

std::string_view to_string(PlatformClass c) noexcept {
    switch (c) {
        case PlatformClass::NotLimiting: return "not_limiting";
        case PlatformClass::PcieLinkReduced: return "pcie_link_reduced";
        case PlatformClass::ThermalLimit: return "thermal_limit";
        case PlatformClass::PowerLimit: return "power_limit";
        case PlatformClass::VoltageLimit: return "voltage_limit";
        case PlatformClass::BackgroundContention: return "background_contention";
        case PlatformClass::Unsupported: return "unsupported";
        case PlatformClass::Insufficient: return "insufficient";
    }
    return "unknown";
}

std::string_view to_string(PlatformEvidence e) noexcept {
    switch (e) {
        case PlatformEvidence::GpuUnderLoad: return "gpu_under_load";
        case PlatformEvidence::GpuIdle: return "gpu_idle";
        case PlatformEvidence::GpuLoadUnknown: return "gpu_load_unknown";
        case PlatformEvidence::IdleDownclockIgnored: return "idle_downclock_ignored";
        case PlatformEvidence::LinkBelowCardMax: return "link_below_card_max";
        case PlatformEvidence::LinkBelowSlotMax: return "link_below_slot_max";
        case PlatformEvidence::LinkLimitedBySlot: return "link_limited_by_slot";
        case PlatformEvidence::LinkAtMax: return "link_at_max";
        case PlatformEvidence::LinkMaxUnknown: return "link_max_unknown";
        case PlatformEvidence::LinkCurrentUnknown: return "link_current_unknown";
        case PlatformEvidence::PerfCapThermal: return "perfcap_thermal";
        case PlatformEvidence::PerfCapPower: return "perfcap_power";
        case PlatformEvidence::PerfCapVoltage: return "perfcap_voltage";
        case PlatformEvidence::PerfCapNone: return "perfcap_none";
        case PlatformEvidence::PerfCapUnsupported: return "perfcap_unsupported";
        case PlatformEvidence::ClockDropUnderLoad: return "clock_drop_under_load";
        case PlatformEvidence::ClockStable: return "clock_stable";
        case PlatformEvidence::ClockUnknown: return "clock_unknown";
        case PlatformEvidence::CompetingCpuShare: return "competing_cpu_share";
        case PlatformEvidence::CompetingGpuShare: return "competing_gpu_share";
        case PlatformEvidence::CompetingShareLow: return "competing_share_low";
        case PlatformEvidence::ProcessDataMissing: return "process_data_missing";
    }
    return "unknown";
}

std::optional<PerfCapFlags> perf_caps_from_mask(std::uint32_t mask) {
    // NVAPI_GPU_PERF_DECREASE bitleri. Burada tutulanlar disinda bilinen bir bit varsa da
    // isi/guc sinifina sokulmaz; UNKNOWN veya taninmayan bit "limit yok" iddiasini engeller.
    constexpr std::uint32_t kThermal = 0x00000001u;
    constexpr std::uint32_t kPowerControl = 0x00000002u;
    constexpr std::uint32_t kAcBattery = 0x00000004u;
    constexpr std::uint32_t kApiTriggered = 0x00000008u;
    constexpr std::uint32_t kInsufficientPower = 0x00000010u;
    constexpr std::uint32_t kUnknown = 0x80000000u;
    constexpr std::uint32_t kKnown = kThermal | kPowerControl | kAcBattery | kApiTriggered |
                                     kInsufficientPower | kUnknown;
    if ((mask & kUnknown) != 0 || (mask & ~kKnown) != 0) return std::nullopt;
    PerfCapFlags f;
    f.thermal = (mask & kThermal) != 0;
    f.power = (mask & kPowerControl) != 0;
    f.voltage = false;  // bu bayrak setinde voltaj nedeni yok
    return f;
}

std::string_view to_string(PlatformConfidence r) noexcept {
    switch (r) {
        case PlatformConfidence::SignalsAgree: return "signals_agree";
        case PlatformConfidence::VendorFlagOnly: return "vendor_flag_only";
        case PlatformConfidence::DirectLinkReading: return "direct_link_reading";
        case PlatformConfidence::MeasuredShare: return "measured_share";
        case PlatformConfidence::ConflictingSignals: return "conflicting_signals";
        case PlatformConfidence::NoLoadObserved: return "no_load_observed";
        case PlatformConfidence::MissingData: return "missing_data";
        case PlatformConfidence::UnsupportedSensor: return "unsupported_sensor";
    }
    return "unknown";
}

namespace {

void add(std::vector<PlatformEvidence>& v, PlatformEvidence e) {
    if (std::find(v.begin(), v.end(), e) == v.end()) v.push_back(e);
}

// Tek boyut (gen veya genislik) icin karar.
enum class Dim { Unknown, Reduced, SlotLimited, AtMax };
Dim judge_dim(const std::optional<std::uint32_t>& cur, const std::optional<std::uint32_t>& card,
              const std::optional<std::uint32_t>& slot) {
    if (!cur || !card || !slot) return Dim::Unknown;
    const auto eff = std::min(*card, *slot);
    if (*cur < eff) return Dim::Reduced;  // hem kart hem yuva azamisinin altinda
    if (*slot < *card) return Dim::SlotLimited;
    return Dim::AtMax;
}

bool idle_link_low(const PcieSample& s) {
    return (s.cur_gen && s.card_max_gen && *s.cur_gen < *s.card_max_gen) ||
           (s.cur_width && s.card_max_width && *s.cur_width < *s.card_max_width);
}

}  // namespace

PlatformFinding evaluate_pcie(const std::vector<PcieSample>& windows) {
    PlatformFinding f;
    std::vector<const PcieSample*> loaded;
    bool idle_seen = false, idle_low = false, load_unknown = false;
    for (const auto& s : windows) {
        if (!s.gpu_load_pct) {
            load_unknown = true;
        } else if (*s.gpu_load_pct >= kGpuLoadPct) {
            loaded.push_back(&s);
        } else {
            idle_seen = true;
            if (idle_link_low(s)) idle_low = true;
        }
    }
    if (loaded.empty()) {
        // Bostaki dusuk link guc tasarrufudur; yuk alti gozlem olmadan karar yok.
        f.cls = PlatformClass::Insufficient;
        f.reason = idle_seen ? PlatformConfidence::NoLoadObserved : PlatformConfidence::MissingData;
        if (idle_seen) add(f.counter, PlatformEvidence::GpuIdle);
        if (idle_low) add(f.counter, PlatformEvidence::IdleDownclockIgnored);
        if (load_unknown) add(f.counter, PlatformEvidence::GpuLoadUnknown);
        return f;
    }
    add(f.supporting, PlatformEvidence::GpuUnderLoad);
    if (idle_low) add(f.counter, PlatformEvidence::IdleDownclockIgnored);

    std::size_t reduced = 0, ok = 0;
    bool max_unknown = false, cur_unknown = false, slot_limited = false;
    for (const auto* s : loaded) {
        if (!s->cur_gen && !s->cur_width) {
            cur_unknown = true;
            continue;
        }
        const Dim g = judge_dim(s->cur_gen, s->card_max_gen, s->slot_max_gen);
        const Dim w = judge_dim(s->cur_width, s->card_max_width, s->slot_max_width);
        if (g == Dim::Reduced || w == Dim::Reduced) {
            ++reduced;
            continue;
        }
        // Okunan boyutlardan biri icin azami bilinmiyorsa bu pencere degerlendirilemez.
        const bool g_missing = s->cur_gen && g == Dim::Unknown;
        const bool w_missing = s->cur_width && w == Dim::Unknown;
        if (g_missing || w_missing) {
            max_unknown = true;
            continue;
        }
        if (g == Dim::SlotLimited || w == Dim::SlotLimited) slot_limited = true;
        ++ok;
    }
    const std::size_t evaluable = reduced + ok;
    if (evaluable == 0) {
        f.cls = PlatformClass::Insufficient;
        f.reason = PlatformConfidence::MissingData;
        if (max_unknown) add(f.counter, PlatformEvidence::LinkMaxUnknown);
        if (cur_unknown) add(f.counter, PlatformEvidence::LinkCurrentUnknown);
        return f;
    }
    if (reduced * 2 > evaluable) {
        f.cls = PlatformClass::PcieLinkReduced;
        f.reason = PlatformConfidence::DirectLinkReading;
        add(f.supporting, PlatformEvidence::LinkBelowCardMax);
        add(f.supporting, PlatformEvidence::LinkBelowSlotMax);
    } else {
        f.cls = PlatformClass::NotLimiting;
        f.reason = PlatformConfidence::DirectLinkReading;
        add(f.counter, slot_limited ? PlatformEvidence::LinkLimitedBySlot : PlatformEvidence::LinkAtMax);
    }
    if (max_unknown) add(f.counter, PlatformEvidence::LinkMaxUnknown);
    if (cur_unknown) add(f.counter, PlatformEvidence::LinkCurrentUnknown);
    return f;
}

PlatformFinding evaluate_limits(const std::vector<LimitSample>& windows, std::optional<double> reference_clock_mhz) {
    PlatformFinding f;
    std::vector<const LimitSample*> loaded;
    bool any_load_known = false;
    for (const auto& s : windows) {
        if (!s.load_pct) continue;
        any_load_known = true;
        if (*s.load_pct >= kGpuLoadPct) loaded.push_back(&s);
    }
    if (loaded.empty()) {
        f.cls = PlatformClass::Insufficient;
        f.reason = any_load_known ? PlatformConfidence::NoLoadObserved : PlatformConfidence::MissingData;
        return f;
    }

    std::size_t clock_known = 0, clock_drop = 0;
    const bool ref_ok = reference_clock_mhz && *reference_clock_mhz > 0;
    for (const auto* s : loaded) {
        if (!ref_ok || !s->clock_mhz) continue;
        ++clock_known;
        if (*s->clock_mhz < *reference_clock_mhz * kClockDropRatio) ++clock_drop;
    }

    std::size_t supported = 0, thermal = 0, power = 0, voltage = 0;
    for (const auto* s : loaded) {
        if (!s->caps) continue;
        ++supported;
        thermal += s->caps->thermal ? 1u : 0u;
        power += s->caps->power ? 1u : 0u;
        voltage += s->caps->voltage ? 1u : 0u;
    }
    if (supported == 0) {
        // Bayrak yoksa "sorun yok" denmez: acikca desteklenmiyor.
        f.cls = PlatformClass::Unsupported;
        f.reason = PlatformConfidence::UnsupportedSensor;
        add(f.supporting, PlatformEvidence::PerfCapUnsupported);
        if (clock_drop > 0) add(f.supporting, PlatformEvidence::ClockDropUnderLoad);
        return f;
    }

    const double need = kLimitWindowShare * static_cast<double>(supported);
    auto meets = [need](std::size_t n) { return n > 0 && static_cast<double>(n) >= need; };
    // Esitlikte oncelik: isi > voltaj > guc.
    PlatformClass pick = PlatformClass::NotLimiting;
    std::size_t best = 0;
    if (meets(thermal) && thermal > best) { pick = PlatformClass::ThermalLimit; best = thermal; }
    if (meets(voltage) && voltage > best) { pick = PlatformClass::VoltageLimit; best = voltage; }
    if (meets(power) && power > best) { pick = PlatformClass::PowerLimit; best = power; }

    if (pick != PlatformClass::NotLimiting) {
        f.cls = pick;
        if (meets(thermal)) add(f.supporting, PlatformEvidence::PerfCapThermal);
        if (meets(voltage)) add(f.supporting, PlatformEvidence::PerfCapVoltage);
        if (meets(power)) add(f.supporting, PlatformEvidence::PerfCapPower);
        if (clock_drop > 0) {
            add(f.supporting, PlatformEvidence::ClockDropUnderLoad);
            f.reason = PlatformConfidence::SignalsAgree;
        } else {
            add(f.counter, clock_known > 0 ? PlatformEvidence::ClockStable : PlatformEvidence::ClockUnknown);
            f.reason = PlatformConfidence::VendorFlagOnly;
        }
        return f;
    }

    add(f.counter, PlatformEvidence::PerfCapNone);
    if (clock_known > 0 && clock_drop * 2 > clock_known) {
        // Frekans dustu ama uretici limit bildirmiyor: neden atfedilmez.
        f.cls = PlatformClass::Insufficient;
        f.reason = PlatformConfidence::ConflictingSignals;
        add(f.supporting, PlatformEvidence::ClockDropUnderLoad);
        return f;
    }
    f.cls = PlatformClass::NotLimiting;
    if (clock_known > 0) {
        add(f.counter, PlatformEvidence::ClockStable);
        f.reason = PlatformConfidence::SignalsAgree;
    } else {
        add(f.counter, PlatformEvidence::ClockUnknown);
        f.reason = PlatformConfidence::VendorFlagOnly;
    }
    return f;
}

PlatformFinding evaluate_background(const std::vector<ProcessWindow>& windows, std::uint32_t game_pid,
                                    std::uint32_t self_pid) {
    PlatformFinding f;
    struct Acc {
        std::string name;
        double cpu = 0, gpu = 0;
    };
    std::map<std::uint32_t, Acc> acc;
    std::size_t measured = 0;
    bool any_cpu = false, any_gpu = false;
    double total_cpu = 0, total_gpu = 0;
    for (const auto& w : windows) {
        if (!w.measured) continue;
        ++measured;
        for (const auto& p : w.processes) {
            if (p.pid == 0 || p.pid == game_pid || p.pid == self_pid) continue;  // bosta, oyun, biz
            auto& a = acc[p.pid];
            if (a.name.empty()) a.name = p.name;
            if (p.cpu_pct) {
                any_cpu = true;
                a.cpu += *p.cpu_pct;
                total_cpu += *p.cpu_pct;
            }
            if (p.gpu_pct) {
                any_gpu = true;
                a.gpu += *p.gpu_pct;
                total_gpu += *p.gpu_pct;
            }
        }
    }
    // Rakip listesi bos olabilir (yalniz oyun calisiyor); bu olcumdur, eksik veri degil. Ama hic olcum yoksa
    // ya da surecler var fakat paylari okunamadiysa karar verilmez.
    const bool has_procs = !acc.empty();
    if (measured == 0 || (has_procs && !any_cpu && !any_gpu)) {
        f.cls = PlatformClass::Insufficient;
        f.reason = PlatformConfidence::MissingData;
        add(f.counter, PlatformEvidence::ProcessDataMissing);
        return f;
    }
    const double n = static_cast<double>(measured);
    struct Row {
        std::string name;
        double cpu, gpu;
    };
    std::vector<Row> rows;
    for (const auto& [pid, a] : acc) rows.push_back({a.name, a.cpu / n, a.gpu / n});
    std::sort(rows.begin(), rows.end(),
              [](const Row& x, const Row& y) { return std::max(x.cpu, x.gpu) > std::max(y.cpu, y.gpu); });

    const double avg_cpu = total_cpu / n, avg_gpu = total_gpu / n;
    bool cpu_hit = avg_cpu >= kTotalCompetingPct, gpu_hit = avg_gpu >= kTotalCompetingPct;
    std::vector<std::string> names;
    for (const auto& r : rows) {
        const bool c = r.cpu >= kProcessSharePct, g = r.gpu >= kProcessSharePct;
        if (c) cpu_hit = true;
        if (g) gpu_hit = true;
        if (c || g) names.push_back(r.name);
    }
    if (!cpu_hit && !gpu_hit) {
        f.cls = PlatformClass::NotLimiting;
        f.reason = PlatformConfidence::MeasuredShare;
        add(f.counter, PlatformEvidence::CompetingShareLow);
        return f;
    }
    if (names.empty())  // yalniz toplam esigi asildi: en buyuk uc katkici
        for (std::size_t i = 0; i < rows.size() && i < 3; ++i) names.push_back(rows[i].name);
    f.cls = PlatformClass::BackgroundContention;
    f.reason = PlatformConfidence::MeasuredShare;
    if (cpu_hit) add(f.supporting, PlatformEvidence::CompetingCpuShare);
    if (gpu_hit) add(f.supporting, PlatformEvidence::CompetingGpuShare);
    f.processes = std::move(names);
    return f;
}

}  // namespace ef::core::diagnosis
