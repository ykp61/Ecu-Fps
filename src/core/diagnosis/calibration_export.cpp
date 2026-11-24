#include "core/diagnosis/calibration_export.h"

#include <cmath>
#include <utility>

#include "core/measure/frame_stats.h"

namespace ef::core::diagnosis {

namespace {

using contracts::ErrorCode;
using contracts::Result;

bool pct(const std::optional<double>& v) {
    return v && std::isfinite(*v) && *v >= 0.0 && *v <= 100.0;
}

bool positive(const std::optional<double>& v) { return v && std::isfinite(*v) && *v > 0.0; }

// Kullanim yuzdesi: yalniz sonlu ve [0,100] ise kanit olur; degilse yazilmaz (0 uydurulmaz).
void add_pct(EvidenceWindow& w, std::string_view name, const std::optional<double>& v) {
    if (!pct(v)) return;
    w.metrics.push_back({std::string(name), *v, measure::Attribution::RootCandidate});
}

}  // namespace

Result<LabeledWindow> make_labeled_window(const SceneCalibrationSample& s) {
    if (s.id.empty() || s.id.size() > 96)
        return Result<LabeledWindow>::fail(ErrorCode::OutOfRange, "id");
    if (!positive(s.avg_fps)) return Result<LabeledWindow>::fail(ErrorCode::MissingValue, "avg_fps");
    if (!s.frames || !std::isfinite(*s.frames))
        return Result<LabeledWindow>::fail(ErrorCode::MissingValue, "frames");
    const double frames = *s.frames;
    if (frames < static_cast<double>(measure::kMinIntervals) ||
        frames > 1000000.0 || std::floor(frames) != frames)
        return Result<LabeledWindow>::fail(ErrorCode::OutOfRange, "frames");

    LabeledWindow x;
    x.id = s.id;
    x.provenance = s.provenance;
    x.label = s.label;
    // EF-X20: v2 ornek korumalari.
    const bool v2 = s.label_method != LabelMethod::Unspecified || s.fine_label != CalibrationLabel::Legacy ||
                    !s.machine_group.empty() || !s.run_group.empty() || !s.machine_digest.empty();
    if (v2) {
        if (s.fine_label != CalibrationLabel::Legacy) {
            const auto exp = expected_compute_class(s.fine_label);
            if (exp ? *exp != s.label : s.label != ComputeClass::Insufficient)
                return Result<LabeledWindow>::fail(ErrorCode::OutOfRange, "fine_label");
        }
        // Bu yol FPS siniri/VSync durumunu OLCMEZ (asagida "sinir yok, VSync kapali" sabitlenir);
        // sinir ailesi etiketi bu sabitle celisir -> uydurma veri yazilmaz.
        if (s.label == ComputeClass::FrameCapOrPresentWait)
            return Result<LabeledWindow>::fail(ErrorCode::OutOfRange, "cap_state_unmeasured");
    }
    x.machine_group = s.machine_group;
    x.run_group = s.run_group;
    x.machine_digest = s.machine_digest;  // bicim write_calibration'da dogrulanir (gecersiz -> yazma yok)
    x.label_method = s.label_method;
    x.fine_label = s.fine_label;
    x.family = s.family;
    x.window.run_id = s.id;
    x.window.scope = WindowScope::Measured;
    x.window.frames.n = static_cast<std::size_t>(frames);
    x.window.frames.avg_fps = *s.avg_fps;
    x.window.frames.valid_seconds = frames / *s.avg_fps;
    x.window.frames.sufficient = true;  // n >= kMinIntervals yukarida dogrulandi
    x.window.valid = true;
    if (positive(s.p99_ms)) x.window.frames.p99_ms = *s.p99_ms;

    add_pct(x.window, kMetricGpu3d, s.gpu_engine_3d_pct);
    add_pct(x.window, kMetricCpuMaxCore, s.cpu_max_core_pct);
    add_pct(x.window, kMetricCpuMainThread, s.cpu_main_thread_pct);

    // Sahne kosusu bilinen yuk: FPS siniri yok, VSync kapali. Basvuru (expected_fps) yok.
    x.run.fps_cap_known_none = true;
    x.run.vsync = PresentSync::VsyncOff;
    // gpu_busy_ms zorunlu degil; yalniz gercek olcum (>0) yazilir.
    if (positive(s.gpu_busy_ms)) x.extras.gpu_busy_ms = *s.gpu_busy_ms;
    return x;
}

Result<CalibrationDocument> append_sample(CalibrationDocument doc, const LabeledWindow& w) {
    if (w.id.empty()) return Result<CalibrationDocument>::fail(ErrorCode::MissingValue, "id");
    for (const auto& prev : doc.samples)
        if (prev.id == w.id) return Result<CalibrationDocument>::fail(ErrorCode::ParseError, "id");
    if (doc.samples.size() >= kMaxCalibrationSamples)
        return Result<CalibrationDocument>::fail(ErrorCode::ParseLimit, "samples");
    doc.samples.push_back(w);
    return doc;
}

Result<std::string> append_sample_json(std::string_view existing, const SceneCalibrationSample& s) {
    auto w = make_labeled_window(s);
    if (!w.ok()) return Result<std::string>(w.error());
    CalibrationDocument doc;
    if (!existing.empty()) {
        auto parsed = parse_calibration(existing);
        if (!parsed.ok()) return Result<std::string>(parsed.error());  // bozuk dosya ezilmez
        doc = std::move(parsed.value());
    }
    auto next = append_sample(std::move(doc), w.value());
    if (!next.ok()) return Result<std::string>(next.error());
    return write_calibration(next.value());
}

}  // namespace ef::core::diagnosis
