#include "core/diagnosis/calibration.h"

#include <cstdio>
#include <map>
#include <set>
#include <utility>

#include "core/diagnosis/validated_calibration.h"

namespace ef::core::diagnosis {

namespace {

std::uint64_t fnv1a(std::string_view s) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

bool limiting(ComputeClass c) {
    return c == ComputeClass::GpuBound || c == ComputeClass::CpuOrEngineBound || c == ComputeClass::FrameCapOrPresentWait;
}
bool negative_label(ComputeClass c) { return c == ComputeClass::Healthy || c == ComputeClass::Insufficient; }
std::size_t idx(ComputeClass c) noexcept { return static_cast<std::size_t>(c); }

std::optional<ComputeClass> compute_target(const LabeledWindow& x) noexcept {
    if (x.fine_label == CalibrationLabel::Legacy) return x.label;
    return expected_compute_class(x.fine_label);
}

struct LabelName { CalibrationLabel l; std::string_view name; };
constexpr LabelName kLabelNames[] = {
    {CalibrationLabel::GpuBound, "gpu_bound"}, {CalibrationLabel::CpuOrEngineBound, "cpu_or_engine_bound"},
    {CalibrationLabel::CpuMainThread, "cpu_main_thread"},
    {CalibrationLabel::FrameCapOrPresentWait, "frame_cap_or_present_wait"}, {CalibrationLabel::FpsCap, "fps_cap"},
    {CalibrationLabel::Vsync, "vsync"}, {CalibrationLabel::EngineFrameLimit, "engine_frame_limit"},
    {CalibrationLabel::Mixed, "mixed"}, {CalibrationLabel::Healthy, "healthy"},
    {CalibrationLabel::Insufficient, "insufficient"}, {CalibrationLabel::RamPressure, "ram_pressure"},
    {CalibrationLabel::StorageWait, "storage_wait"}, {CalibrationLabel::ThermalPower, "thermal_power"},
    {CalibrationLabel::Unknown, "unknown"}};
static_assert(sizeof(kLabelNames) / sizeof(kLabelNames[0]) == static_cast<std::size_t>(CalibrationLabel::Unknown),
              "Legacy disindaki her ince etiketin adi olmali");

// EF-X20 dogrulama bulgusu 1: yanlis pozitif orani YALNIZ uygun gercek holdout orneklerinden hesaplanir
// (pay ve payda). Sentetik veya uygun olmayan ornek ekleyerek oran seyreltilemez.
struct GateInputs {
    bool present = true;
    std::size_t real = 0, real_eligible = 0, holdout_total = 0, eligible_false_positive = 0;
    std::size_t machines = 0, positives = 0, negatives = 0;
};

ReleaseStatus gate_from(const GateInputs& g) {
    ReleaseStatus s;
    s.real_holdout = g.real;
    s.holdout_total = g.holdout_total;
    s.synthetic_only = g.real == 0;
    if (!g.present || g.real < kMinRealHoldout) {
        s.reason_key = "diag.gate.no_real_holdout";  // sentetik fikstur gercek dogruluk sayilmaz
        return s;
    }
    if (g.real_eligible < kMinRealHoldout) {
        s.reason_key = "diag.gate.label_or_group_missing";
        return s;
    }
    if (g.machines < kMinHoldoutMachines) {
        s.reason_key = "diag.gate.machine_coverage";
        return s;
    }
    if (g.positives < kMinHoldoutPerSide || g.negatives < kMinHoldoutPerSide) {
        s.reason_key = "diag.gate.class_imbalance";
        return s;
    }
    const double fp = static_cast<double>(g.eligible_false_positive) / static_cast<double>(g.real_eligible);
    s.ready = fp <= kMaxFalsePositiveRate;
    s.reason_key = s.ready ? "diag.gate.ready" : "diag.gate.false_positive_rate";
    return s;
}

std::string num(const std::optional<double>& v) {
    if (!v) return "-";
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", *v);
    return b;
}

// Olcum izi: ayni pencerenin farkli kimlik/makine altinda kopyalanmasini yakalar.
std::string window_fingerprint(const LabeledWindow& x) {
    std::string f = num(x.window.frames.avg_fps) + "|" + num(x.window.frames.p99_ms) + "|" +
                    std::to_string(x.window.frames.n) + "|" + num(x.run.display_refresh_hz) + "|" + num(x.run.fps_cap) +
                    "|" + (x.run.fps_cap_known_none ? "n" : "c") + "|" + std::to_string(static_cast<int>(x.run.vsync)) +
                    "|" + num(x.extras.frame_interval_cv) + "|" + num(x.extras.gpu_busy_ms);
    for (const auto& m : x.window.metrics) f += "|" + m.metric + "=" + num(m.value);
    return f;
}

}  // namespace

std::string_view to_string(CalibrationLabel l) noexcept {
    for (const auto& n : kLabelNames)
        if (n.l == l) return n.name;
    return "legacy";
}

std::optional<CalibrationLabel> calibration_label_from(std::string_view s) noexcept {
    for (const auto& n : kLabelNames)
        if (n.name == s) return n.l;
    return std::nullopt;
}

std::optional<ComputeClass> expected_compute_class(CalibrationLabel l) noexcept {
    switch (l) {
        case CalibrationLabel::GpuBound: return ComputeClass::GpuBound;
        case CalibrationLabel::CpuOrEngineBound:
        case CalibrationLabel::CpuMainThread: return ComputeClass::CpuOrEngineBound;
        case CalibrationLabel::FrameCapOrPresentWait:
        case CalibrationLabel::FpsCap:
        case CalibrationLabel::Vsync:
        case CalibrationLabel::EngineFrameLimit: return ComputeClass::FrameCapOrPresentWait;
        case CalibrationLabel::Mixed: return ComputeClass::Mixed;
        case CalibrationLabel::Healthy: return ComputeClass::Healthy;
        case CalibrationLabel::Insufficient: return ComputeClass::Insufficient;
        case CalibrationLabel::Legacy:
        case CalibrationLabel::RamPressure:
        case CalibrationLabel::StorageWait:
        case CalibrationLabel::ThermalPower:
        case CalibrationLabel::Unknown: break;
    }
    return std::nullopt;
}

LabelDomain label_domain(CalibrationLabel l) noexcept {
    switch (l) {
        case CalibrationLabel::RamPressure:
        case CalibrationLabel::StorageWait: return LabelDomain::MemoryStorage;
        case CalibrationLabel::ThermalPower: return LabelDomain::PlatformLimits;
        case CalibrationLabel::Unknown: return LabelDomain::Unknown;
        default: return LabelDomain::Compute;
    }
}

Split split_of(const std::string& id) noexcept { return (fnv1a(id) >> 7) & 1 ? Split::Holdout : Split::Tune; }

Split split_for(const LabeledWindow& x) noexcept {
    if (x.machine_group.empty()) return split_of(x.id);
    if ((fnv1a(x.machine_group) >> 7) % 3 == 0) return Split::Holdout;      // makine bazinda ayrik holdout
    return (fnv1a(x.run_group) >> 7) & 1 ? Split::Validation : Split::Tune;  // kosu bolunmez
}

bool gate_eligible(const LabeledWindow& x) noexcept {
    // EF-X20 dogrulama bulgusu 3: machine_digest yoksa yinelenmis makine denetimi calisamaz -> uygun degil.
    return x.provenance == Provenance::RealHardware && !x.machine_group.empty() && !x.run_group.empty() &&
           !x.machine_digest.empty() &&
           (x.label_method == LabelMethod::ControlledExperiment || x.label_method == LabelMethod::ExpertReview);
}

std::uint32_t family_bits(const DeviceFamily& d) noexcept {
    if (d.cpu == CpuVendor::Unknown || d.gpu == GpuFamily::Unknown || d.form == FormFactor::Unknown ||
        d.storage == StorageKind::Unknown)
        return 0;
    const auto cpu = static_cast<std::uint32_t>(d.cpu) - 1u;    // 0..2
    const auto gpu = static_cast<std::uint32_t>(d.gpu) - 1u;    // 0..3
    const auto form = static_cast<std::uint32_t>(d.form) - 1u;  // 0..1
    const std::uint32_t combo = 1u << (cpu * 8u + gpu * 2u + form);
    const std::uint32_t storage = 1u << (24u + static_cast<std::uint32_t>(d.storage) - 1u);
    return combo | storage;
}

CalibrationStats evaluate(const std::vector<LabeledWindow>& set, Split which) {
    CalibrationStats s;
    std::set<std::string> machines;
    std::map<std::uint32_t, std::size_t> family_counts;
    std::map<std::uint32_t, std::set<std::string>> family_machines;
    for (const auto& x : set) {
        if (split_for(x) != which) continue;
        const auto target = compute_target(x);
        if (!target) {
            ++s.out_of_scope;
            continue;
        }
        ++s.total;
        (x.provenance == Provenance::RealHardware ? s.real : s.synthetic) += 1;
        const bool eligible = gate_eligible(x);
        if (x.provenance == Provenance::RealHardware) {
            if (eligible) {
                machines.insert(x.machine_group);
                if (limiting(*target)) ++s.positives;
                else if (negative_label(*target)) ++s.negatives;
                const std::uint32_t bits = family_bits(x.family);
                for (std::uint32_t b = 0; b < 32; ++b)
                    if (bits & (1u << b)) {
                        ++family_counts[1u << b];
                        family_machines[1u << b].insert(x.machine_group);
                    }
            } else {
                ++s.real_ineligible;
                if (!x.machine_group.empty() && !x.run_group.empty() && x.machine_digest.empty())
                    ++s.real_missing_digest;
            }
        }
        const auto v = classify_window(x.window, x.run, x.extras);
        ++s.confusion[idx(*target)][idx(v.cls)];
        if (v.cls == *target) {
            ++s.correct;
            if (eligible) ++s.eligible_correct;
            continue;
        }
        if (v.cls == ComputeClass::Insufficient || v.cls == ComputeClass::Mixed) {
            ++s.abstained;
            if (eligible) ++s.eligible_abstained;
            continue;
        }
        if (limiting(v.cls) && !limiting(*target)) {
            ++s.false_positive;
            if (eligible) ++s.eligible_false_positive;
        } else if (v.cls == ComputeClass::Healthy && limiting(*target)) {
            ++s.false_negative;
        } else {
            ++s.wrong_class;
        }
    }
    s.distinct_machines = machines.size();
    // Aile kapsami makine basinadir: tek PC'nin cok ornegi aileyi temsil etmez.
    for (const auto& [bit, n] : family_counts)
        if (n >= kMinFamilyHoldoutSamples && family_machines[bit].size() >= kMinFamilyHoldoutMachines)
            s.covered_families |= bit;
    return s;
}

std::size_t eligible_real(const CalibrationStats& s) noexcept {
    return s.real >= s.real_ineligible ? s.real - s.real_ineligible : 0;
}

std::vector<ModelReport> evaluate_models(const std::vector<LabeledWindow>& set, Split which) {
    std::vector<ModelReport> out(3);
    out[0].model = "compute";
    out[0].scored = true;
    out[0].stats = evaluate(set, which);
    out[0].labeled = out[0].stats.total;
    out[1].model = "memory_storage";   // girdiler veri setinde yok -> puanlanmaz
    out[2].model = "platform_limits";  // termal/guc kaniti veri setinde yok -> puanlanmaz
    for (const auto& x : set) {
        if (split_for(x) != which) continue;
        const auto d = label_domain(x.fine_label);
        if (d == LabelDomain::MemoryStorage) ++out[1].labeled;
        else if (d == LabelDomain::PlatformLimits) ++out[2].labeled;
    }
    return out;
}

SplitIntegrity validate_split_integrity(const std::vector<LabeledWindow>& set) {
    SplitIntegrity out;
    const auto issue = [&out](std::string code, const std::string& id) {
        out.ok = false;
        out.issues.push_back({std::move(code), id});
    };
    std::map<std::string, std::string> run_machine;
    std::map<std::string, std::set<std::string>> digest_machines;
    std::map<std::string, std::set<Split>> machine_splits;
    struct WindowSeen {
        Split split;
        std::string machine_group;
    };
    std::map<std::string, WindowSeen> real_windows;
    for (const auto& x : set) {
        const Split sp = split_for(x);
        const bool real = x.provenance == Provenance::RealHardware;
        // Bulgu 4: grupsuz (v1) gercek ornek sizinti degildir; zaten kapiya uygun degil -> engellemeyen not.
        if (real && (x.machine_group.empty() || x.run_group.empty()))
            out.notes.push_back({"ungrouped_real_sample", x.id});
        if (!x.machine_group.empty()) {
            machine_splits[x.machine_group].insert(sp);
            if (!x.run_group.empty()) {
                const auto [it, inserted] = run_machine.emplace(x.run_group, x.machine_group);
                if (!inserted && it->second != x.machine_group) issue("run_group_multi_machine", x.id);
            }
            if (!x.machine_digest.empty()) digest_machines[x.machine_digest].insert(x.machine_group);
        }
        if (real) {
            // Bulgu 2: ayni olcum baska makine adi altinda (ayni bolmede bile) makine/sinif sayisini sisirir.
            const auto [wit, fresh] = real_windows.emplace(window_fingerprint(x), WindowSeen{sp, x.machine_group});
            if (!fresh) {
                if (wit->second.split != sp) issue("duplicate_window_cross_split", x.id);
                if (wit->second.machine_group != x.machine_group) issue("duplicate_window_multi_machine", x.id);
                // Ayni makine/bolmede yeni kimlikle kopya: holdout'u sisirip FP oranini ve sinif dengesini bozar.
                if (wit->second.split == sp && wit->second.machine_group == x.machine_group) issue("duplicate_window", x.id);
            }
        }
    }
    for (const auto& [m, splits] : machine_splits)  // savunma: insa geregi olmamali
        if (splits.count(Split::Holdout) && splits.size() > 1) issue("holdout_machine_leak", m);
    for (const auto& [digest, names] : digest_machines) {
        if (names.size() < 2) continue;
        bool in_holdout = false;
        for (const auto& n : names)
            if (machine_splits[n].count(Split::Holdout)) in_holdout = true;
        issue(in_holdout ? "duplicate_holdout_machine" : "duplicate_machine", *names.begin());
    }
    return out;
}

double accuracy_all(const CalibrationStats& s) noexcept {
    return s.total == 0 ? 0.0 : static_cast<double>(s.correct) / static_cast<double>(s.total);
}

double accuracy_answered(const CalibrationStats& s) noexcept {
    const std::size_t answered = s.total >= s.abstained ? s.total - s.abstained : 0;
    return answered == 0 ? 0.0 : static_cast<double>(s.correct) / static_cast<double>(answered);
}

double coverage(const CalibrationStats& s) noexcept {
    return s.total == 0 ? 0.0 : static_cast<double>(s.total - s.abstained) / static_cast<double>(s.total);
}

ReleaseStatus release_status(const CalibrationStats& h) {
    GateInputs g;
    g.real = h.real;
    g.real_eligible = eligible_real(h);
    g.holdout_total = h.total;  // bilgi; oran paydasi degil
    g.eligible_false_positive = h.eligible_false_positive;
    g.machines = h.distinct_machines;
    g.positives = h.positives;
    g.negatives = h.negatives;
    return gate_from(g);
}

ReleaseStatus release_status(const CalibrationStats& h, const SplitIntegrity& integrity) {
    auto s = release_status(h);
    if (!integrity.ok) {
        s.ready = false;
        s.reason_key = "diag.gate.split_leakage";
    }
    return s;
}

bool diagnosis_release_ready(const CalibrationStats& h) noexcept { return release_status(h).ready; }

ReleaseStatus release_status_from_validated(const ValidatedCalibration& v) {
    GateInputs g;
    g.present = v.present;
    g.real = v.real_holdout;
    g.real_eligible = v.real_holdout;  // baslik yalniz uygun gercek holdout sayisini tasir
    g.holdout_total = v.present ? v.holdout_total : 0;
    // Bulgu 1: payda uygun gercek holdout (real_holdout); holdout_total ile seyreltilmez.
    g.eligible_false_positive = v.false_positive;
    g.machines = v.holdout_machines;
    g.positives = v.holdout_positives;
    g.negatives = v.holdout_negatives;
    return gate_from(g);
}

const ValidatedCalibration& validated_calibration() noexcept { return kValidatedCalibration; }

ReleaseStatus release_status_for_device(const ReleaseStatus& base, std::uint32_t covered, const DeviceFamily& d) {
    if (!base.ready) return base;
    const std::uint32_t bits = family_bits(d);
    if (bits != 0 && (covered & bits) == bits) return base;
    ReleaseStatus s = base;
    s.ready = false;
    s.reason_key = "diag.gate.device_family_uncovered";
    return s;
}

ReleaseStatus diagnosis_release_status_for(const DeviceFamily& d) {
    const auto& v = validated_calibration();
    return release_status_for_device(release_status_from_validated(v), v.covered_families, d);
}

ReleaseStatus diagnosis_release_status() {
    // Uretim: gomulu ozet yoksa kapali; cihaz ailesi bilinmiyorsa (DeviceFamily{}) yine kapali.
    return diagnosis_release_status_for(DeviceFamily{});
}

std::string diagnosis_version_stamp() {
    const auto& v = validated_calibration();
    std::string s(kComputeRulesVersion);
    if (!v.present || v.dataset_sha256.size() < 16) return s + ":uncalibrated";
    return s + ":diagcal-" + std::string(v.dataset_sha256.substr(0, 16));
}

RunComputeVerdict classify_run_with_production_gate(const Timeline& tl, const RunComputeInputs& run,
                                                    const std::vector<WindowComputeExtras>& extras) {
    return classify_run_gated(tl, run, extras, diagnosis_release_status());
}

}  // namespace ef::core::diagnosis
