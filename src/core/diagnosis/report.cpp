#include "core/diagnosis/report.h"

namespace ef::core::diagnosis {

std::string_view to_string(ReportStatus s) noexcept {
    switch (s) {
        case ReportStatus::Healthy: return "healthy";
        case ReportStatus::Limiting: return "limiting";
        case ReportStatus::Mixed: return "mixed";
        case ReportStatus::Insufficient: return "insufficient";
    }
    return "insufficient";
}

DiagnosisReport make_report(const std::optional<ComputeVerdict>& compute, const std::optional<MemoryStorageDiagnosis>& mem,
                            const std::vector<PlatformFinding>& platform, const std::vector<ComponentFinding>& components,
                            const ReleaseStatus& release) {
    DiagnosisReport r;
    r.release_ready = release.ready;
    r.release_key = release.reason_key;
    if (compute && !release.ready) {
        // BM-02: kapi kapali -> yayinlanabilir sinif yok. Destekleyen/karsit kanit korunur, guven nedeni
        // "kalibrasyon dogrulanmadi" olur; genel durum bu maddeden Healthy/Limiting uretemez.
        ReportItem it{"compute"};
        it.supporting = compute->supporting;
        it.counter = compute->counter;
        it.counter.push_back("diagnosis_calibration_unverified");
        it.confidence = std::string(to_string(ComputeConfidence::CalibrationUnverified));
        r.items.push_back(std::move(it));
    } else if (compute) {
        ReportItem it{"compute"};
        switch (compute->cls) {
            case ComputeClass::Healthy: it.status = ReportStatus::Healthy; break;
            case ComputeClass::GpuBound:
            case ComputeClass::CpuOrEngineBound:
                it.status = ReportStatus::Limiting;
                it.physical_limit = !compute->is_fault;  // beklenen performansta: bilesenin dogal siniri
                it.fix = compute->is_fault ? FixGroup::GameSettings : FixGroup::Hardware;
                break;
            case ComputeClass::FrameCapOrPresentWait:
                it.status = ReportStatus::Limiting;
                it.fix = FixGroup::GameSettings;  // FPS siniri / VSync kullanicinin secimi
                break;
            case ComputeClass::Mixed: it.status = ReportStatus::Mixed; break;
            case ComputeClass::Insufficient: it.status = ReportStatus::Insufficient; break;
        }
        it.supporting = compute->supporting;
        it.counter = compute->counter;
        it.confidence = std::string(to_string(compute->reason));
        r.has_runtime_evidence = compute->cls != ComputeClass::Insufficient;
        r.items.push_back(std::move(it));
    }
    if (mem) {
        ReportItem it{"memory_storage"};
        switch (mem->cls) {
            case MemStorageClass::Healthy: it.status = ReportStatus::Healthy; break;
            case MemStorageClass::MemoryPressure:
            case MemStorageClass::VramPressure:
                it.status = ReportStatus::Limiting;
                it.fix = FixGroup::GameSettings;  // doku/cozunurluk; kapasite yukseltmesi ayrica fiziksel
                break;
            case MemStorageClass::IoStutter:
                it.status = ReportStatus::Limiting;
                it.fix = FixGroup::Windows;
                break;
            case MemStorageClass::Mixed: it.status = ReportStatus::Mixed; break;
            case MemStorageClass::Insufficient: it.status = ReportStatus::Insufficient; break;
        }
        it.confidence = "memio:" + std::to_string(static_cast<int>(mem->reason));
        r.items.push_back(std::move(it));
    }
    for (const auto& p : platform) {
        ReportItem it{"platform"};
        switch (p.cls) {
            case PlatformClass::NotLimiting: it.status = ReportStatus::Healthy; break;
            case PlatformClass::PcieLinkReduced: it.status = ReportStatus::Limiting, it.fix = FixGroup::Hardware; break;
            case PlatformClass::ThermalLimit:
            case PlatformClass::PowerLimit:
            case PlatformClass::VoltageLimit: it.status = ReportStatus::Limiting, it.physical_limit = true; break;
            case PlatformClass::BackgroundContention: it.status = ReportStatus::Limiting, it.fix = FixGroup::Windows; break;
            case PlatformClass::Unsupported:
            case PlatformClass::Insufficient: it.status = ReportStatus::Insufficient; break;
        }
        for (auto e : p.supporting) it.supporting.push_back(std::string(to_string(e)));
        for (auto e : p.counter) it.counter.push_back(std::string(to_string(e)));
        for (const auto& n : p.processes) it.supporting.push_back("process:" + n);
        it.confidence = std::string(to_string(p.reason));
        r.items.push_back(std::move(it));
    }
    for (const auto& c : components) {
        ReportItem it{"component." + std::string(to_string(c.component))};
        it.status = c.verdict == Verdict::Limiting ? ReportStatus::Limiting : c.verdict == Verdict::NoIssueFound ? ReportStatus::Healthy : ReportStatus::Insufficient;
        it.supporting = c.evidence;
        if (!c.abstain_reason.empty()) it.counter.push_back(c.abstain_reason);
        it.confidence = c.verdict == Verdict::Abstain ? "abstain" : "inventory_evidence";
        if (c.verdict == Verdict::Limiting) it.fix = c.fixable_by_setting ? FixGroup::Bios : FixGroup::Hardware;
        r.items.push_back(std::move(it));
    }
    bool any_limit = false, any_mixed = false;
    for (const auto& it : r.items) {
        any_limit |= it.status == ReportStatus::Limiting;
        any_mixed |= it.status == ReportStatus::Mixed;
    }
    if (any_limit) r.overall = any_mixed ? ReportStatus::Mixed : ReportStatus::Limiting;
    else if (any_mixed) r.overall = ReportStatus::Mixed;
    else if (r.has_runtime_evidence && compute && compute->cls == ComputeClass::Healthy) r.overall = ReportStatus::Healthy;
    else r.overall = ReportStatus::Insufficient;  // yuk alti kanit yok: "sorun yok" denmez
    return r;
}

}  // namespace ef::core::diagnosis
