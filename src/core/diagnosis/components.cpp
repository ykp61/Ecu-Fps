#include "core/diagnosis/components.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/diagnosis/compute.h"

namespace ef::core::diagnosis {

using inventory::HealthCode;

std::string_view to_string(Component c) noexcept {
    switch (c) {
        case Component::Cpu: return "cpu";
        case Component::Gpu: return "gpu";
        case Component::Motherboard: return "motherboard";
        case Component::Ram: return "ram";
        case Component::Storage: return "storage";
    }
    return "cpu";
}

std::string_view to_string(Verdict v) noexcept {
    switch (v) {
        case Verdict::NoIssueFound: return "no_issue_found";
        case Verdict::Limiting: return "limiting";
        case Verdict::Abstain: return "abstain";
    }
    return "abstain";
}

namespace {
const inventory::HealthFinding* find(const std::vector<inventory::HealthFinding>& h, HealthCode c) {
    const auto it = std::find_if(h.begin(), h.end(), [&](const auto& f) { return f.code == c; });
    return it == h.end() ? nullptr : &*it;
}
}  // namespace

std::vector<ComponentFinding> component_report(const ComponentInput& in) {
    std::vector<ComponentFinding> out;
    // CPU / GPU: yalniz yuk altindaki teshisten.
    for (auto comp : {Component::Cpu, Component::Gpu}) {
        ComponentFinding f{comp};
        if (!in.runtime || *in.runtime == RuntimeClass::Insufficient) {
            // B10: yuk altinda olculdu ama yayin kapisi kapali: "olculmedi" denmez; olculdu, karar verilmez.
            f.abstain_reason = in.runtime_measured && !in.diagnosis_release_ready ? "diagnosis_calibration_unverified"
                                                                                  : "no_runtime_evidence";  // olcum yok: hukum yok
        } else if (!in.diagnosis_release_ready) {
            // BM-02: siniflandirici yayin kapisi kapali; calisma verisi olsa da hukum yayinlanmaz.
            f.abstain_reason = "diagnosis_calibration_unverified";
        } else if (*in.runtime == RuntimeClass::Mixed || *in.runtime == RuntimeClass::FrameCap) {
            f.abstain_reason = *in.runtime == RuntimeClass::FrameCap ? "frame_cap_limits_first" : "mixed_evidence";
        } else {
            const bool lim = (comp == Component::Gpu && *in.runtime == RuntimeClass::GpuBound) ||
                             (comp == Component::Cpu && *in.runtime == RuntimeClass::CpuOrEngineBound);
            f.verdict = lim ? Verdict::Limiting : Verdict::NoIssueFound;
            f.evidence.push_back(std::string("runtime:") + (lim ? "limiting_under_load" : "not_limiting_under_load"));
        }
        out.push_back(f);
    }
    // Anakart: PCIe dusumu (yuk altinda) sinirlama kanitidir; tek modul tek basina degil (kanal kullanimi olculmedi).
    {
        ComponentFinding f{Component::Motherboard};
        const auto* pw = find(in.health, HealthCode::PcieWidthReduced);
        const auto* sd = find(in.health, HealthCode::SingleDimm);
        // C2: yuk altinda okunan link (denge testi S3). Dogrudan okuma varsa hukum ondan gelir: azaminin altindaysa
        // sinirlama kaniti, azamideyse bostaki dusuk okuma (guc tasarrufu) sayilmaz. Neden (yuva, riser) iddia edilmez.
        //  - Kural hukum verdi (yuva azamisi biliniyor): "dusuk link" -> sinirlama; azamide / yuva siniri -> sorun yok.
        //  - Kural hukum veremedi (yuva azamisi okunamadi): yuk altinda GENISLIK kart azamisinin altinda olculduyse
        //    mevcut genislik kurali (PcieWidthReduced + yuk alti olcum) gecerlidir; kanit olculen link metnidir. Hiz
        //    nesli tek basina (Gen 5 kart, Gen 4 yuva: olagan) sinirlama sayilmaz.
        bool load_reduced = false, load_ok = false, load_width_below = false;
        if (in.pcie_load) {
            const PcieSample& s = *in.pcie_load;
            const PlatformFinding link = evaluate_pcie({s});
            const bool direct = link.reason == PlatformConfidence::DirectLinkReading;
            load_reduced = direct && link.cls == PlatformClass::PcieLinkReduced;
            load_ok = direct && link.cls == PlatformClass::NotLimiting;
            const bool loaded = s.gpu_load_pct && std::isfinite(*s.gpu_load_pct) && *s.gpu_load_pct >= kGpuLoadPct;
            load_width_below = !direct && loaded && s.cur_width && s.card_max_width && *s.cur_width < *s.card_max_width;
        }
        const bool load_limits = load_reduced || load_width_below;
        const bool width_under_load = pw && in.pcie_measured_under_load && !load_limits && !load_ok;
        if (load_limits) f.evidence.push_back(in.pcie_load_text.empty() ? std::string("pcie_load") : "pcie_load:" + in.pcie_load_text);
        if (width_under_load) f.evidence.push_back("pcie:" + pw->evidence);
        if (sd) f.evidence.push_back("memory_channel:" + sd->evidence);
        if (load_limits || width_under_load) f.verdict = Verdict::Limiting;
        else if (sd) f.abstain_reason = "memory_channel_not_measured";
        else if (pw && !in.pcie_measured_under_load && !load_ok) f.abstain_reason = "pcie_not_measured_under_load";  // bostaki dusus darbogaz degil
        else if (!in.inventory.dgpu_pcie && !load_ok) f.abstain_reason = "pcie_unknown";
        else {
            f.verdict = Verdict::NoIssueFound;
            f.evidence.push_back("pcie_and_channels_ok");
            if (load_ok) f.evidence.push_back("pcie_load_ok");
        }
        out.push_back(f);
    }
    // RAM: XMP/EXPO + kapasite (oneri varsa).
    {
        ComponentFinding f{Component::Ram};
        if (const auto* x = find(in.health, HealthCode::XmpLikelyOff)) {
            f.evidence.push_back("xmp:" + x->evidence);
            f.fixable_by_setting = true;
        }
        bool capacity_known = false;
        if (in.ram_total_gib && in.game_recommended_ram_gib) {
            capacity_known = true;
            if (*in.ram_total_gib < *in.game_recommended_ram_gib)
                f.evidence.push_back("capacity:" + std::to_string(*in.ram_total_gib) + "GiB<" + std::to_string(*in.game_recommended_ram_gib) + "GiB");
        }
        if (!f.evidence.empty()) f.verdict = Verdict::Limiting;
        else if (in.inventory.ram.empty()) f.abstain_reason = "ram_inventory_unknown";
        else if (!capacity_known) {
            f.verdict = Verdict::NoIssueFound;  // hiz anma degerinde; kapasite onerisi bilinmiyor (yalniz hiz hukmu)
            f.evidence.push_back("speed_at_rated;capacity_not_compared");
        } else {
            f.verdict = Verdict::NoIssueFound;
            f.evidence.push_back("speed_at_rated;capacity_meets_recommendation");
        }
        out.push_back(f);
    }
    // Depolama: oyun HDD'de, dolu disk.
    {
        ComponentFinding f{Component::Storage};
        for (const auto& g : in.games) {
            if (g.on_hdd) f.evidence.push_back("game_on_hdd:" + g.game);
            if (g.volume_free_pct && *g.volume_free_pct < 10.0) f.evidence.push_back("volume_low_free:" + g.game);
        }
        if (const auto* l = find(in.health, HealthCode::LowDiskSpace)) f.evidence.push_back("low_space:" + l->evidence);
        if (!f.evidence.empty()) f.verdict = Verdict::Limiting;
        else if (in.games.empty() && in.inventory.volumes.empty()) f.abstain_reason = "storage_unknown";
        else {
            f.verdict = Verdict::NoIssueFound;
            f.evidence.push_back("games_on_ssd_space_ok");
        }
        out.push_back(f);
    }
    return out;
}

std::optional<BenchmarkSceneEvidence> benchmark_scene_evidence_from_metrics(const std::map<std::string, double>& metrics) {
    auto get = [&](const char* k) -> std::optional<double> {
        const auto it = metrics.find(k);
        if (it == metrics.end() || !std::isfinite(it->second)) return std::nullopt;
        return it->second;
    };
    BenchmarkSceneEvidence e;
    e.avg_fps = get("scene_avg_fps");
    if (!e.avg_fps || !(*e.avg_fps > 0)) return std::nullopt;  // sahne skoru yok: kanit yok
    // B3 inceleme (bosluk 25): sahne olcum penceresinde pencere simge durumundaysa sahne degerleri bilesen kaniti degildir.
    if (get("scene_window_minimized").value_or(0) > 0.5) return std::nullopt;
    e.p99_ms = get("scene_p99_ms");
    e.gpu_util_pct = get("scene_gpu_pct");            // sahne penceresi PDH (~20 Hz)
    e.cpu_max_core_pct = get("scene_cpu_max_core_pct");
    e.sample_interval_ms = get("scene_sample_interval_ms");
    e.gpu_busy_ms = get("scene_gpu_busy_ms");         // yalniz GPU zaman damgasi olculduyse
    if (e.gpu_busy_ms && !(*e.gpu_busy_ms > 0)) e.gpu_busy_ms.reset();  // 0 = olcum yok, kanit degil
    return e;
}

BenchmarkRuntime benchmark_runtime_from_history(const std::vector<insights::HistoryEntry>& newest_first, const ReleaseStatus& gate,
                                                const RuntimeHardware& current) {
    BenchmarkRuntime out;
    if (current.cpu.empty() || current.gpu.empty()) return out;  // bugunku donanim bilinmiyor: eslesme kurulamaz
    const auto cond = [](const insights::HistoryEntry& e, const char* k) {
        const auto it = e.conditions.find(k);
        return it == e.conditions.end() ? std::string() : it->second;
    };
    const auto same = [](const std::string& rec, const std::string& now) { return rec == now.substr(0, kRuntimeConditionBytes); };
    for (const auto& e : newest_first) {
        if (e.source != "builtin") continue;
        const auto ev = benchmark_scene_evidence_from_metrics(e.metrics);
        if (!ev) continue;  // sahne kaniti yok: daha eski sahne kanitli kayda bakilir
        // Baska donanim / surucuyle alinmis kayit bugunun kaniti degildir (bayat kanit).
        if (!same(cond(e, "cpu"), current.cpu) || !same(cond(e, "gpu"), current.gpu) || !same(cond(e, "gpu_driver"), current.gpu_driver))
            continue;
        out.measured = true;
        const auto cls = runtime_class_from_benchmark_scene(*ev, gate).cls;
        if (gate.ready && cls != RuntimeClass::Insufficient) out.cls = cls;
        return out;
    }
    return out;
}

RuntimeEvidenceResult runtime_class_from_benchmark_scene(const BenchmarkSceneEvidence& e, const ReleaseStatus& gate) {
    RuntimeEvidenceResult out;
    if (!e.avg_fps || !std::isfinite(*e.avg_fps) || *e.avg_fps <= 0) {
        out.counter.push_back("frames_insufficient");
        return out;
    }
    EvidenceWindow w;
    w.scope = WindowScope::Measured;
    w.valid = true;  // cagiran yalniz tamamlanmis sahne kosusunu verir
    w.frames.sufficient = true;
    w.frames.avg_fps = e.avg_fps;
    if (e.p99_ms && std::isfinite(*e.p99_ms) && *e.p99_ms > 0) w.frames.p99_ms = e.p99_ms;
    // MEAS-03: coznurluk yetmiyorsa kullanim degerleri kok neden kaniti olarak kullanilmaz.
    const bool resolution_ok = e.sample_interval_ms && std::isfinite(*e.sample_interval_ms) &&
                               *e.sample_interval_ms > 0 && *e.sample_interval_ms <= kMaxBenchmarkSampleIntervalMs;
    auto add_metric = [&w](std::string name, const std::optional<double>& v) {
        if (!v || !std::isfinite(*v)) return;  // eksik kanit 0 sayilmaz
        w.metrics.push_back({std::move(name), *v, measure::Attribution::RootCandidate});
    };
    const bool had_util = e.gpu_util_pct.has_value() || e.cpu_max_core_pct.has_value();
    if (resolution_ok) {
        add_metric(std::string(kMetricGpu3d), e.gpu_util_pct);
        add_metric(std::string(kMetricCpuMaxCore), e.cpu_max_core_pct);
    }
    RunComputeInputs run;
    run.fps_cap_known_none = true;   // ekran disi sahne: bilinen oyun/surucu FPS siniri yok
    run.vsync = PresentSync::VsyncOff;
    WindowComputeExtras extras;
    // DIAG-10: kare olcekli zaman damgasi kaniti; yabanci GPU isi dahil oldugu icin "mesgul" ust siniridir
    // ve yalniz ayni sahne kosusunun kendi olcumudur. Yoksa extras bos kalir (uydurulmaz).
    extras.gpu_busy_ms = e.gpu_busy_ms;
    const auto v = classify_window_gated(w, run, extras, gate);
    out.supporting = v.supporting;
    out.counter = v.counter;
    // Deger vardi ama ornekleme araligi MEAS-03 sinirini asiyordu: kanit sayilmadi, nedeni kaybolmasin.
    if (!resolution_ok && had_util) out.counter.push_back("sampling_resolution_insufficient");
    switch (v.cls) {
        case ComputeClass::GpuBound: out.cls = RuntimeClass::GpuBound; break;
        case ComputeClass::CpuOrEngineBound: out.cls = RuntimeClass::CpuOrEngineBound; break;
        case ComputeClass::FrameCapOrPresentWait: out.cls = RuntimeClass::FrameCap; break;
        case ComputeClass::Mixed: out.cls = RuntimeClass::Mixed; break;
        case ComputeClass::Healthy: out.cls = RuntimeClass::Healthy; break;
        case ComputeClass::Insufficient: out.cls = RuntimeClass::Insufficient; break;
    }
    return out;
}

std::string_view verdict_key(const ComponentFinding& f) noexcept {
    switch (f.verdict) {
        case Verdict::Limiting: return "diag.verdict_limiting";
        case Verdict::NoIssueFound:
            for (const auto& e : f.evidence)
                if (e.rfind("runtime:", 0) == 0) return "diag.verdict_no_issue";
            return "diag.verdict_inventory_ok";
        case Verdict::Abstain:
            if (f.abstain_reason == "no_runtime_evidence") return "diag.verdict_not_measured";
            // B10: CPU/GPU yuk altinda olculdu, kalibrasyon dogrulanmadigi icin karar verilmedi.
            if (f.abstain_reason == "diagnosis_calibration_unverified") return "diag.verdict_measured_no_verdict";
            return "diag.verdict_abstain";
    }
    return "diag.verdict_abstain";
}

}  // namespace ef::core::diagnosis
