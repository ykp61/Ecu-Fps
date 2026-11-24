#include "core/diagnosis/memory_storage.h"

#include <algorithm>

namespace ef::core::diagnosis {

namespace {

void add_unique(std::vector<EvidenceCode>& v, EvidenceCode c) {
    if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
}

std::optional<double> ratio(const MetricValue& num, const MetricValue& den) {
    if (!num.value || !den.value || !(*den.value > 0)) return std::nullopt;
    return *num.value / *den.value;
}

// Iliskili pencere sayisina gore alan karari. spikes: sivri pencere sayisi.
void decide(DomainFinding& f, std::size_t spikes, std::size_t coarse, const MemoryThresholds& th) {
    const bool enough = f.correlated_windows >= th.min_correlated_windows && f.correlated_windows * 2 >= spikes;
    if (enough) {
        f.verdict = DomainVerdict::Limiting;
        if (!f.counter.empty())
            f.reason = MemoryConfidence::CounterEvidencePresent;
        else if (f.correlated_windows >= 3)
            f.reason = MemoryConfidence::CorrelatedAcrossManyWindows;
        else
            f.reason = MemoryConfidence::FewCorrelatedWindows;
    } else if (coarse > 0 && f.correlated_windows + coarse >= th.min_correlated_windows) {
        // Sayac araligi sivriyi aciklamaya yetmiyor: sinir da yok denemez.
        f.verdict = DomainVerdict::Insufficient;
        f.reason = MemoryConfidence::CoarseResolution;
    } else {
        f.verdict = DomainVerdict::NotLimiting;
        f.reason = f.counter.empty() ? MemoryConfidence::NoLimitingSignal : MemoryConfidence::CounterEvidencePresent;
    }
}

DomainFinding diagnose_ram(const std::vector<const MemoryWindow*>& ws, const std::vector<bool>& spike, const MemoryThresholds& th) {
    DomainFinding f;
    std::size_t measured = 0, spikes = 0, coarse = 0;
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const auto& w = *ws[i];
        const auto avail = ratio(w.ram_available_bytes, w.ram_total_bytes);
        const auto commit = ratio(w.commit_bytes, w.commit_limit_bytes);
        if ((!avail && !commit) || !w.hard_faults_per_s.value) continue;  // kapasite veya fault yoksa pencere eksik
        ++measured;
        const bool low = avail && *avail < th.ram_available_low_ratio;
        const bool near = commit && *commit >= th.commit_near_ratio;
        const bool cap = low || near;
        const bool faults = *w.hard_faults_per_s.value >= th.hard_faults_high_per_s;
        if (low) add_unique(f.supporting, EvidenceCode::RamAvailableLow);
        if (near) add_unique(f.supporting, EvidenceCode::CommitNearLimit);
        if (spike[i]) ++spikes;
        if (spike[i] && faults && cap) {
            if (w.hard_faults_per_s.attributable) {
                ++f.correlated_windows;
                add_unique(f.supporting, EvidenceCode::HardFaultsWithSpikes);
            } else {
                ++coarse;
            }
        }
        if (faults && !spike[i]) add_unique(f.counter, EvidenceCode::HardFaultsWithoutSpikes);
        if (faults && spike[i] && !cap) add_unique(f.counter, EvidenceCode::HardFaultsWithoutCapacityPressure);
        if (spike[i] && !faults) add_unique(f.counter, EvidenceCode::SpikesWithoutHardFaults);
    }
    if (measured == 0) {
        f.verdict = DomainVerdict::Insufficient;
        f.reason = MemoryConfidence::MissingMetrics;
        add_unique(f.counter, EvidenceCode::RamMetricsMissing);
        return f;
    }
    if (spikes == 0) add_unique(f.counter, EvidenceCode::NoFrameSpikes);
    decide(f, spikes, coarse, th);
    return f;
}

DomainFinding diagnose_vram(const std::vector<const MemoryWindow*>& ws, const std::vector<bool>& spike, const AdapterMemory& ad,
                            std::uint32_t game_pid, const MemoryThresholds& th) {
    DomainFinding f;
    if (ad.kind == AdapterKind::Integrated || ad.shares_system_memory) {
        // Paylasimli bellek sistem RAM'inin parcasidir; RAM sayaclari onu zaten icerir. Oyunun paylasimli
        // kullanimi RAM'e eklenmez, ayri VRAM baskisi da uretilmez: tek kez, RAM alaninda.
        f.verdict = DomainVerdict::NotApplicable;
        f.reason = MemoryConfidence::NoLimitingSignal;
        add_unique(f.notes, EvidenceCode::SharedMemoryCountedOnceAsRam);
        return f;
    }
    if (!ad.dedicated_budget_bytes || *ad.dedicated_budget_bytes == 0) {
        f.verdict = DomainVerdict::Insufficient;
        f.reason = MemoryConfidence::MissingMetrics;
        add_unique(f.counter, EvidenceCode::VramBudgetMissing);
        return f;
    }
    const double budget = static_cast<double>(*ad.dedicated_budget_bytes);
    std::size_t measured = 0, spikes = 0;
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const auto& r = ws[i]->game_vram_dedicated;
        if (!r) continue;
        if (r->scope == measure::VramScope::OwnProcessDxgiBudget) {
            add_unique(f.notes, EvidenceCode::VramOwnProcessBudgetIgnored);
            continue;
        }
        if (r->scope == measure::VramScope::AdapterTotal) {
            add_unique(f.notes, EvidenceCode::VramAdapterTotalContextOnly);
            continue;
        }
        if (!measure::vram_valid_for_game(*r, game_pid)) {
            add_unique(f.notes, EvidenceCode::VramPidMismatch);
            continue;
        }
        if (!r->bytes) continue;
        ++measured;
        const bool near = static_cast<double>(*r->bytes) / budget >= th.vram_near_budget_ratio;
        if (spike[i]) ++spikes;
        if (spike[i] && near) {
            ++f.correlated_windows;
            add_unique(f.supporting, EvidenceCode::VramNearBudgetWithSpikes);
        }
        if (spike[i] && !near) add_unique(f.counter, EvidenceCode::VramWithinBudget);
    }
    if (measured == 0) {
        f.verdict = DomainVerdict::Insufficient;
        f.reason = MemoryConfidence::MissingMetrics;
        add_unique(f.counter, EvidenceCode::VramMetricsMissing);
        return f;
    }
    if (spikes == 0) add_unique(f.counter, EvidenceCode::NoFrameSpikes);
    decide(f, spikes, 0, th);
    return f;
}

DomainFinding diagnose_io(const std::vector<const MemoryWindow*>& ws, const std::vector<bool>& spike, const MemoryThresholds& th) {
    DomainFinding f;
    std::size_t measured = 0, spikes = 0, coarse = 0;
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const auto& m = ws[i]->game_disk_read_bps;
        if (!m.value) continue;
        ++measured;
        const bool high = *m.value >= th.disk_read_high_bps;
        if (spike[i]) ++spikes;
        if (spike[i] && high) {
            if (m.attributable) {
                ++f.correlated_windows;
                add_unique(f.supporting, EvidenceCode::DiskReadWithSpikes);
            } else {
                ++coarse;
            }
        }
        if (high && !spike[i]) add_unique(f.counter, EvidenceCode::DiskReadWithoutSpikes);
        if (spike[i] && !high) add_unique(f.counter, EvidenceCode::SpikesWithoutDiskRead);
    }
    if (measured == 0) {
        f.verdict = DomainVerdict::Insufficient;
        f.reason = MemoryConfidence::MissingMetrics;
        add_unique(f.counter, EvidenceCode::DiskMetricsMissing);
        return f;
    }
    if (spikes == 0) add_unique(f.counter, EvidenceCode::NoFrameSpikes);
    decide(f, spikes, coarse, th);
    return f;
}

MetricValue metric(const EvidenceWindow& w, std::string_view name) {
    for (const auto& c : w.metrics)
        if (c.metric == name) return {c.value, c.attribution == measure::Attribution::RootCandidate};
    return {};
}

}  // namespace

bool is_frame_spike(const measure::FrameStats& f, double spike_ratio) noexcept {
    if (!f.sufficient || !f.avg_fps || !f.p99_ms || !(*f.avg_fps > 0)) return false;
    return *f.p99_ms >= spike_ratio * (1000.0 / *f.avg_fps);
}

MemoryWindow memory_window_from(const EvidenceWindow& w) {
    MemoryWindow m;
    m.start_ms = w.start_ms;
    m.end_ms = w.end_ms;
    m.scope = w.scope;
    m.frames = w.frames;
    m.ram_available_bytes = metric(w, "ram_available_bytes");
    m.ram_total_bytes = metric(w, "ram_total_bytes");
    m.commit_bytes = metric(w, "commit_bytes");
    m.commit_limit_bytes = metric(w, "commit_limit_bytes");
    m.hard_faults_per_s = metric(w, "hard_faults_per_s");
    m.game_disk_read_bps = metric(w, "game_disk_read_bps");
    return m;
}

MemoryStorageDiagnosis diagnose_memory_storage(const std::vector<MemoryWindow>& windows, const AdapterMemory& adapter,
                                               std::uint32_t game_pid, const MemoryThresholds& th) {
    MemoryStorageDiagnosis d;
    // Yukleme/menu: surekli FPS sinirina girmez; ardisik pencereler tek "yukleme suresi" bolumu olur.
    std::vector<const MemoryWindow*> usable;
    std::vector<bool> spike;
    for (const auto& w : windows) {
        if (w.scope == WindowScope::MenuOrLoading) {
            if (!d.loading.empty() && w.start_ms <= d.loading.back().end_ms + 1e-6)
                d.loading.back().end_ms = std::max(d.loading.back().end_ms, w.end_ms);
            else
                d.loading.push_back({w.start_ms, w.end_ms});
            continue;
        }
        if (w.scope != WindowScope::Measured || !w.frames.sufficient) continue;
        usable.push_back(&w);
        spike.push_back(is_frame_spike(w.frames, th.spike_ratio));
    }
    for (const auto& s : d.loading) d.loading_total_ms += s.duration_ms();
    if (!d.loading.empty()) add_unique(d.notes, EvidenceCode::LoadingReportedSeparately);

    if (usable.empty()) {
        d.cls = MemStorageClass::Insufficient;
        d.reason = MemoryConfidence::NoMeasuredWindows;
        add_unique(d.counter, EvidenceCode::NoMeasuredWindows);
        d.ram.reason = d.vram.reason = d.io.reason = MemoryConfidence::NoMeasuredWindows;
        return d;
    }

    d.ram = diagnose_ram(usable, spike, th);
    d.vram = diagnose_vram(usable, spike, adapter, game_pid, th);
    d.io = diagnose_io(usable, spike, th);

    struct Dom {
        const DomainFinding* f;
        MemStorageClass cls;
    };
    const Dom doms[] = {{&d.ram, MemStorageClass::MemoryPressure}, {&d.vram, MemStorageClass::VramPressure}, {&d.io, MemStorageClass::IoStutter}};
    std::size_t limiting = 0;
    bool insufficient = false, coarse = false, any_counter = false;
    const DomainFinding* first = nullptr;
    for (const auto& x : doms) {
        for (auto c : x.f->supporting) add_unique(d.supporting, c);
        for (auto c : x.f->counter) add_unique(d.counter, c);
        for (auto c : x.f->notes) add_unique(d.notes, c);
        if (x.f->verdict == DomainVerdict::Limiting) {
            ++limiting;
            if (!first) {
                first = x.f;
                d.cls = x.cls;
            }
            if (!x.f->counter.empty()) any_counter = true;
        }
        if (x.f->verdict == DomainVerdict::Insufficient) {
            insufficient = true;
            if (x.f->reason == MemoryConfidence::CoarseResolution) coarse = true;
        }
    }
    if (limiting == 0) {
        // Bir alan olculemediyse "sorun yok" denemez.
        d.cls = insufficient ? MemStorageClass::Insufficient : MemStorageClass::Healthy;
        d.reason = insufficient ? (coarse ? MemoryConfidence::CoarseResolution : MemoryConfidence::MissingMetrics)
                                : MemoryConfidence::NoLimitingSignal;
    } else if (limiting == 1) {
        d.reason = first->reason;
    } else {
        d.cls = MemStorageClass::Mixed;
        d.reason = any_counter ? MemoryConfidence::CounterEvidencePresent : first->reason;
    }
    d.is_hardware_fault = false;
    return d;
}

}  // namespace ef::core::diagnosis
