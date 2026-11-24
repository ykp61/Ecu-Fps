#include "core/games/vendor_guide.h"

#include "core/capabilities/vendor_matrix.h"

namespace ef::core::games {

std::vector<VendorGuideSection> vendor_guide_sections() {
    return {
        {"vendor.nvidia.title",
         {"vendor.nvidia.sysmem", "vendor.nvidia.clean_install", "vendor.nvidia.overlay"}},
        {"vendor.amd.title",
         {"vendor.amd.profiles", "vendor.amd.hypr", "vendor.amd.ris", "vendor.amd.channel"}},
    };
}

std::optional<LowLatencyGuide> low_latency_guide(const LowLatencyProbe& probe) {
    using vendors::Vendor;
    if (probe.vendor != Vendor::Amd && probe.vendor != Vendor::Intel) return std::nullopt;
    const bool amd = probe.vendor == Vendor::Amd;

    LowLatencyGuide g;
    g.vendor = probe.vendor;
    // Neden yazma yok: tek kaynak vendor_matrix (G03 AMD surucu Anti-Lag, G04 Intel oyun profili).
    const auto row = capabilities::vendor_row(amd ? "G03" : "G04", probe.vendor);
    g.matrix_reason_key = row ? std::string(row->cell.reason) : std::string(capabilities::matrix_reason::kNotImplemented);

    const bool runtime_usable = probe.runtime == vendors::RuntimeState::Loaded;
    if (!runtime_usable || (probe.feature_reported.has_value() && !*probe.feature_reported)) {
        g.status = LowLatencyStatus::Unsupported;
        g.status_key = "vendor.ll.status.unsupported";
        return g;  // yoksa rehber de yok
    }
    if (!probe.feature_reported.has_value()) {
        g.status = LowLatencyStatus::InsufficientData;
        g.status_key = "vendor.ll.status.insufficient";
    } else {
        g.status = LowLatencyStatus::GuideOnly;
        g.status_key = "vendor.ll.status.guide";
    }
    g.mechanisms = {
        {LowLatencyKind::DriverPanel, amd ? "vendor.ll.amd.driver" : "vendor.ll.intel.driver"},
        {LowLatencyKind::InGameSdk, amd ? "vendor.ll.amd.ingame" : "vendor.ll.intel.ingame"},
    };
    g.footnote_key = "vendor.ll.footnote";
    return g;
}

std::vector<LowLatencyProbe> low_latency_probes(const std::vector<inventory::GpuAdapter>& gpus,
                                                const std::vector<vendors::RuntimeInfo>& runtimes) {
    using vendors::Vendor;
    std::vector<LowLatencyProbe> out;
    for (const auto& a : gpus) {
        if (a.kind == inventory::GpuKind::Software || a.kind == inventory::GpuKind::Remote) continue;
        const auto v = vendors::vendor_of(a.vendor_id.value.value_or(0));
        if (v != Vendor::Amd && v != Vendor::Intel) continue;
        bool seen = false;
        for (const auto& p : out) seen = seen || p.vendor == v;
        if (seen) continue;
        LowLatencyProbe p;
        p.vendor = v;
        for (const auto& rt : runtimes)
            if (rt.vendor == v) p.runtime = rt.state;
        out.push_back(p);
    }
    return out;
}

std::vector<LowLatencyLine> low_latency_card_lines(const std::vector<LowLatencyProbe>& probes) {
    std::vector<LowLatencyLine> out;
    for (const auto& pr : probes) {
        const auto g = low_latency_guide(pr);
        if (!g) continue;  // NVIDIA / bilinmeyen: satir yok
        const bool amd = g->vendor == vendors::Vendor::Amd;
        out.push_back({amd ? "vendor.ll.amd.title" : "vendor.ll.intel.title", true, false});
        out.push_back({g->status_key, false, true});
        for (const auto& m : g->mechanisms) out.push_back({m.text_key, false, false});
        if (!g->footnote_key.empty()) out.push_back({g->footnote_key, false, true});
        if (!g->matrix_reason_key.empty()) out.push_back({g->matrix_reason_key, false, true});
    }
    return out;
}

}  // namespace ef::core::games
