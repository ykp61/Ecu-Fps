#include "core/diagnosis/calibration_io.h"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "core/measure/frame_stats.h"
#include "core/serialization/json.h"

namespace ef::core::diagnosis {

namespace {

using contracts::ErrorCode;
using contracts::Result;
using json::Value;

bool is_number(const Value* v) { return v && (v->is(Value::Type::Int) || v->is(Value::Type::Double)); }

Result<double> number_field(const Value& obj, std::string_view key, double lo, double hi) {
    const Value* v = obj.find(key);
    if (!v) return Result<double>::fail(ErrorCode::MissingValue, std::string(key));
    if (!is_number(v)) return Result<double>::fail(ErrorCode::ParseError, std::string(key));
    const double d = v->as_double();
    if (!std::isfinite(d) || d < lo || d > hi) return Result<double>::fail(ErrorCode::OutOfRange, std::string(key));
    return d;
}

Result<std::optional<double>> optional_number(const Value& obj, std::string_view key, double lo, double hi) {
    const Value* v = obj.find(key);
    if (!v || v->is(Value::Type::Null)) return std::optional<double>{};
    if (!is_number(v)) return Result<std::optional<double>>::fail(ErrorCode::ParseError, std::string(key));
    const double d = v->as_double();
    if (!std::isfinite(d) || d < lo || d > hi)
        return Result<std::optional<double>>::fail(ErrorCode::OutOfRange, std::string(key));
    return std::optional<double>{d};
}

Result<bool> optional_bool(const Value& obj, std::string_view key, bool fallback) {
    const Value* v = obj.find(key);
    if (!v || v->is(Value::Type::Null)) return fallback;
    if (!v->is(Value::Type::Bool)) return Result<bool>::fail(ErrorCode::ParseError, std::string(key));
    return v->as_bool();
}

Result<ComputeClass> parse_label(std::string_view s) {
    for (const ComputeClass c : {ComputeClass::GpuBound, ComputeClass::CpuOrEngineBound,
                                 ComputeClass::FrameCapOrPresentWait, ComputeClass::Mixed,
                                 ComputeClass::Healthy, ComputeClass::Insufficient})
        if (s == to_string(c)) return c;
    return Result<ComputeClass>::fail(ErrorCode::ParseError, "label");
}

Result<Provenance> parse_provenance(std::string_view s) {
    if (s == "synthetic") return Provenance::SyntheticFixture;
    if (s == "real") return Provenance::RealHardware;
    return Result<Provenance>::fail(ErrorCode::ParseError, "provenance");
}

bool opaque_group(std::string_view s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
bool hex_digest(std::string_view s) {
    if (s.size() < 8 || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
Result<LabelMethod> parse_label_method(std::string_view s) {
    if (s == "controlled_experiment") return LabelMethod::ControlledExperiment;
    if (s == "expert_review") return LabelMethod::ExpertReview;
    // Olcum kuralindan turetilen etiket ayni kurali dogrulayamaz: acikca reddedilir.
    if (s == "rule_derived") return Result<LabelMethod>::fail(ErrorCode::ParseError, "label_method:rule_derived");
    return Result<LabelMethod>::fail(ErrorCode::ParseError, "label_method");
}
constexpr std::array<std::pair<std::string_view, CpuVendor>, 4> kCpuNames{
    {{"unknown", CpuVendor::Unknown}, {"intel", CpuVendor::Intel}, {"amd", CpuVendor::Amd}, {"other", CpuVendor::Other}}};
constexpr std::array<std::pair<std::string_view, GpuFamily>, 5> kGpuNames{{{"unknown", GpuFamily::Unknown},
    {"nvidia", GpuFamily::Nvidia}, {"amd", GpuFamily::Amd}, {"intel", GpuFamily::Intel}, {"other", GpuFamily::Other}}};
constexpr std::array<std::pair<std::string_view, FormFactor>, 3> kFormNames{
    {{"unknown", FormFactor::Unknown}, {"desktop", FormFactor::Desktop}, {"laptop", FormFactor::Laptop}}};
constexpr std::array<std::pair<std::string_view, StorageKind>, 3> kStorageNames{
    {{"unknown", StorageKind::Unknown}, {"ssd", StorageKind::Ssd}, {"hdd", StorageKind::Hdd}}};

template <typename E, std::size_t N>
Result<E> enum_field(const Value& obj, std::string_view key, const std::array<std::pair<std::string_view, E>, N>& table) {
    auto s = json::get_string(obj, key, 16);
    if (!s.ok()) return Result<E>::fail(s.code(), std::string(key));
    for (const auto& [name, value] : table)
        if (s.value() == name) return value;
    return Result<E>::fail(ErrorCode::ParseError, std::string(key));
}
template <typename E, std::size_t N>
std::string enum_name(E v, const std::array<std::pair<std::string_view, E>, N>& table) {
    for (const auto& [name, value] : table)
        if (value == v) return std::string(name);
    return "unknown";
}
Result<DeviceFamily> parse_family(const Value& obj) {
    DeviceFamily f;
    auto cpu = enum_field(obj, "cpu_vendor", kCpuNames);
    if (!cpu.ok()) return Result<DeviceFamily>(cpu.error());
    auto gpu = enum_field(obj, "gpu_vendor", kGpuNames);
    if (!gpu.ok()) return Result<DeviceFamily>(gpu.error());
    auto form = enum_field(obj, "form_factor", kFormNames);
    if (!form.ok()) return Result<DeviceFamily>(form.error());
    auto storage = enum_field(obj, "storage_kind", kStorageNames);
    if (!storage.ok()) return Result<DeviceFamily>(storage.error());
    f.cpu = cpu.value();
    f.gpu = gpu.value();
    f.form = form.value();
    f.storage = storage.value();
    return f;
}
bool needs_v2(const LabeledWindow& x) noexcept {
    return !x.machine_group.empty() || !x.run_group.empty() || !x.machine_digest.empty() ||
           x.label_method != LabelMethod::Unspecified || x.fine_label != CalibrationLabel::Legacy;
}

Result<PresentSync> parse_vsync(std::string_view s) {
    if (s == "unknown") return PresentSync::Unknown;
    if (s == "on") return PresentSync::VsyncOn;
    if (s == "off") return PresentSync::VsyncOff;
    return Result<PresentSync>::fail(ErrorCode::ParseError, "vsync");
}

std::string_view vsync_name(PresentSync v) noexcept {
    switch (v) {
        case PresentSync::VsyncOn: return "on";
        case PresentSync::VsyncOff: return "off";
        case PresentSync::Unknown: break;
    }
    return "unknown";
}

bool known_metric(std::string_view name) {
    return name == kMetricGpu3d || name == kMetricCpuMaxCore || name == kMetricCpuMainThread;
}

// Ornek nesnesinde izin verilen alanlar; bilinmeyen alan sessizce yok sayilmaz.
bool allowed_sample_key(std::string_view k, bool v2) {
    static constexpr std::array<std::string_view, 15> kKeys{
        "id", "provenance", "label", "avg_fps", "p99_ms", "frames", "refresh_hz", "fps_cap",
        "fps_cap_known_none", "vsync", "frame_interval_cv", "gpu_busy_ms", "metrics", "note", "source"};
    for (const auto k2 : kKeys)
        if (k == k2) return true;
    static constexpr std::array<std::string_view, 8> kV2Keys{"machine_group", "run_group", "machine_digest",
        "label_method", "cpu_vendor", "gpu_vendor", "form_factor", "storage_kind"};
    if (v2) {
        for (const auto k3 : kV2Keys)
            if (k == k3) return true;
    }
    return false;
}

Result<LabeledWindow> parse_sample(const Value& obj, std::size_t index, bool v2) {
    const auto fail = [&](ErrorCode c, std::string d) {
        return Result<LabeledWindow>::fail(c, "samples[" + std::to_string(index) + "]." + d);
    };
    if (!obj.is(Value::Type::Object)) return fail(ErrorCode::ParseError, "not_object");
    for (const auto& k : obj.keys())
        if (!allowed_sample_key(k, v2)) return fail(ErrorCode::ParseError, "unknown_field:" + k);

    auto id = json::get_string(obj, "id", 96);
    if (!id.ok()) return fail(id.code(), "id");
    if (id.value().empty()) return fail(ErrorCode::ParseError, "id");
    auto provenance = json::get_string(obj, "provenance", 16);
    if (!provenance.ok()) return fail(provenance.code(), "provenance");
    auto prov = parse_provenance(provenance.value());
    if (!prov.ok()) return fail(prov.code(), prov.error().detail);
    auto label_s = json::get_string(obj, "label", 32);
    if (!label_s.ok()) return fail(label_s.code(), "label");
    ComputeClass label_cls = ComputeClass::Insufficient;
    CalibrationLabel fine = CalibrationLabel::Legacy;
    if (v2) {
        const auto f = calibration_label_from(label_s.value());
        if (!f) return fail(ErrorCode::ParseError, "label");
        fine = *f;
        label_cls = expected_compute_class(fine).value_or(ComputeClass::Insufficient);  // compute disi: out_of_scope
    } else {
        auto label = parse_label(label_s.value());
        if (!label.ok()) return fail(label.code(), label.error().detail);
        label_cls = label.value();
    }

    auto avg = number_field(obj, "avg_fps", 0.1, 10000.0);
    if (!avg.ok()) return fail(avg.code(), "avg_fps");
    auto p99 = optional_number(obj, "p99_ms", 0.001, 100000.0);
    if (!p99.ok()) return fail(p99.code(), "p99_ms");
    auto frames = json::get_int(obj, "frames", static_cast<std::int64_t>(measure::kMinIntervals), 1000000);
    if (!frames.ok()) return fail(frames.code(), "frames");

    LabeledWindow x;
    x.id = id.value();
    x.provenance = prov.value();
    x.label = label_cls;
    x.fine_label = fine;
    {
        auto refresh = optional_number(obj, "refresh_hz", 1.0, 1000.0);
        if (!refresh.ok()) return fail(refresh.code(), "refresh_hz");
        x.run.display_refresh_hz = refresh.value();
    }
    {
        auto cap = optional_number(obj, "fps_cap", 1.0, 10000.0);
        if (!cap.ok()) return fail(cap.code(), "fps_cap");
        x.run.fps_cap = cap.value();
    }
    {
        auto none = optional_bool(obj, "fps_cap_known_none", false);
        if (!none.ok()) return fail(none.code(), "fps_cap_known_none");
        x.run.fps_cap_known_none = none.value();
    }
    if (const Value* v = obj.find("vsync")) {
        if (!v->is(Value::Type::String)) return fail(ErrorCode::ParseError, "vsync");
        auto vs = parse_vsync(v->as_string());
        if (!vs.ok()) return fail(vs.code(), "vsync");
        x.run.vsync = vs.value();
    }
    {
        auto cv = optional_number(obj, "frame_interval_cv", 0.0, 100.0);
        if (!cv.ok()) return fail(cv.code(), "frame_interval_cv");
        x.extras.frame_interval_cv = cv.value();
    }
    {
        auto busy = optional_number(obj, "gpu_busy_ms", 0.0, 100000.0);
        if (!busy.ok()) return fail(busy.code(), "gpu_busy_ms");
        x.extras.gpu_busy_ms = busy.value();
    }
    if (const Value* metrics = obj.find("metrics")) {
        if (!metrics->is(Value::Type::Object)) return fail(ErrorCode::ParseError, "metrics");
        for (const auto& k : metrics->keys()) {
            if (!known_metric(k)) return fail(ErrorCode::ParseError, "unknown_metric:" + k);
            auto val = optional_number(*metrics, k, 0.0, 100.0);
            if (!val.ok()) return fail(val.code(), "metrics." + k);
            if (!val.value()) return fail(ErrorCode::ParseError, "metrics." + k);
            x.window.metrics.push_back({k, *val.value(), measure::Attribution::RootCandidate});
        }
    }

    if (v2) {
        auto mg = json::get_string(obj, "machine_group", 64);
        if (!mg.ok()) return fail(mg.code(), "machine_group");
        if (!opaque_group(mg.value())) return fail(ErrorCode::ParseError, "machine_group");
        auto rg = json::get_string(obj, "run_group", 64);
        if (!rg.ok()) return fail(rg.code(), "run_group");
        if (!opaque_group(rg.value())) return fail(ErrorCode::ParseError, "run_group");
        auto lm = json::get_string(obj, "label_method", 32);
        if (!lm.ok()) return fail(lm.code(), "label_method");
        auto method = parse_label_method(lm.value());
        if (!method.ok()) return fail(method.code(), method.error().detail);
        if (const Value* dg = obj.find("machine_digest")) {
            if (!dg->is(Value::Type::String) || !hex_digest(dg->as_string())) return fail(ErrorCode::ParseError, "machine_digest");
            x.machine_digest = dg->as_string();
        }
        auto fam = parse_family(obj);
        if (!fam.ok()) return fail(fam.code(), fam.error().detail);
        x.machine_group = mg.value();
        x.run_group = rg.value();
        x.label_method = method.value();
        x.family = fam.value();
    }

    x.window.run_id = x.id;
    x.window.scope = WindowScope::Measured;
    x.window.frames.n = static_cast<std::size_t>(frames.value());
    x.window.frames.valid_seconds = static_cast<double>(x.window.frames.n) / avg.value();
    x.window.frames.avg_fps = avg.value();
    x.window.frames.p99_ms = p99.value();
    x.window.frames.sufficient = true;  // n >= kMinIntervals dogrulandi
    x.window.valid = true;
    return x;
}

}  // namespace

contracts::Result<CalibrationDocument> parse_calibration(std::string_view text) {
    json::Limits limits;
    limits.max_bytes = 8u << 20;
    limits.max_items = 200000;
    auto root = json::parse(text, limits);
    if (!root.ok()) return Result<CalibrationDocument>(root.error());
    auto schema = json::check_schema(root.value(), kCalibrationSchemaVersion);
    if (!schema.ok()) return Result<CalibrationDocument>(schema.error());
    for (const auto& k : root.value().keys())
        if (k != "schema_version" && k != "format" && k != "samples")
            return Result<CalibrationDocument>::fail(ErrorCode::ParseError, "unknown_root_field:" + k);
    auto format = json::get_string(root.value(), "format", 32);
    if (!format.ok()) return Result<CalibrationDocument>(format.error());
    const bool v2 = schema.value() >= 2;
    if (format.value() != (v2 ? kCalibrationFormatV2 : kCalibrationFormat))
        return Result<CalibrationDocument>::fail(ErrorCode::ParseError, "format");
    const Value* samples = root.value().find("samples");
    if (!samples || !samples->is(Value::Type::Array) || samples->items().empty())
        return Result<CalibrationDocument>::fail(ErrorCode::ParseError, "samples");
    if (samples->items().size() > kMaxCalibrationSamples)
        return Result<CalibrationDocument>::fail(ErrorCode::ParseLimit, "samples");

    CalibrationDocument doc;
    doc.schema_version = v2 ? 2 : 1;
    doc.samples.reserve(samples->items().size());
    for (std::size_t i = 0; i < samples->items().size(); ++i) {
        auto x = parse_sample(samples->items()[i], i, v2);
        if (!x.ok()) return Result<CalibrationDocument>(x.error());
        for (const auto& prev : doc.samples)
            if (prev.id == x.value().id)
                return Result<CalibrationDocument>::fail(ErrorCode::ParseError,
                                                         "samples[" + std::to_string(i) + "].id");
        doc.samples.push_back(std::move(x.value()));
    }
    return doc;
}

contracts::Result<std::string> write_calibration(const CalibrationDocument& doc) {
    json::Value root = json::Value::object();
    bool v2 = doc.schema_version >= 2;
    for (const auto& s2 : doc.samples) v2 = v2 || needs_v2(s2);
    root.set("schema_version", json::Value::integer(v2 ? 2 : 1));
    root.set("format", json::Value::string(std::string(v2 ? kCalibrationFormatV2 : kCalibrationFormat)));
    json::Value samples = json::Value::array();
    for (const auto& x : doc.samples) {
        if (x.id.empty()) return Result<std::string>::fail(ErrorCode::MissingValue, "id");
        json::Value o = json::Value::object();
        o.set("id", json::Value::string(x.id));
        o.set("provenance", json::Value::string(x.provenance == Provenance::RealHardware ? "real" : "synthetic"));
        const std::string_view label_name =
            v2 && x.fine_label != CalibrationLabel::Legacy ? to_string(x.fine_label) : to_string(x.label);
        o.set("label", json::Value::string(std::string(label_name)));
        if (v2) {
            // Eksik grup/yontem uydurulmaz: v1 ornegi v2 belgeye karisamaz.
            if (x.machine_group.empty() || x.run_group.empty() || x.label_method == LabelMethod::Unspecified)
                return Result<std::string>::fail(ErrorCode::MissingValue, x.id + ".v2_fields");
            // Okuyucunun reddedecegi deger yazilmaz (aksi halde dosya kendi sonraki eklemesini bozar).
            if (!opaque_group(x.machine_group) || !opaque_group(x.run_group))
                return Result<std::string>::fail(ErrorCode::OutOfRange, x.id + ".group");
            if (!x.machine_digest.empty() && !hex_digest(x.machine_digest))
                return Result<std::string>::fail(ErrorCode::OutOfRange, x.id + ".machine_digest");
            o.set("machine_group", json::Value::string(x.machine_group));
            o.set("run_group", json::Value::string(x.run_group));
            if (!x.machine_digest.empty()) o.set("machine_digest", json::Value::string(x.machine_digest));
            o.set("label_method", json::Value::string(x.label_method == LabelMethod::ExpertReview ? "expert_review"
                                                                                                  : "controlled_experiment"));
            o.set("cpu_vendor", json::Value::string(enum_name(x.family.cpu, kCpuNames)));
            o.set("gpu_vendor", json::Value::string(enum_name(x.family.gpu, kGpuNames)));
            o.set("form_factor", json::Value::string(enum_name(x.family.form, kFormNames)));
            o.set("storage_kind", json::Value::string(enum_name(x.family.storage, kStorageNames)));
        }
        if (!x.window.frames.avg_fps || !(*x.window.frames.avg_fps > 0))
            return Result<std::string>::fail(ErrorCode::MissingValue, x.id + ".avg_fps");
        o.set("avg_fps", json::Value::number(*x.window.frames.avg_fps));
        if (x.window.frames.p99_ms) o.set("p99_ms", json::Value::number(*x.window.frames.p99_ms));
        o.set("frames", json::Value::integer(static_cast<std::int64_t>(x.window.frames.n)));
        if (x.run.display_refresh_hz) o.set("refresh_hz", json::Value::number(*x.run.display_refresh_hz));
        if (x.run.fps_cap) o.set("fps_cap", json::Value::number(*x.run.fps_cap));
        if (x.run.fps_cap_known_none) o.set("fps_cap_known_none", json::Value::boolean(true));
        o.set("vsync", json::Value::string(std::string(vsync_name(x.run.vsync))));
        if (x.extras.frame_interval_cv) o.set("frame_interval_cv", json::Value::number(*x.extras.frame_interval_cv));
        if (x.extras.gpu_busy_ms) o.set("gpu_busy_ms", json::Value::number(*x.extras.gpu_busy_ms));
        if (!x.window.metrics.empty()) {
            json::Value metrics = json::Value::object();
            for (const auto& m : x.window.metrics) {
                if (!known_metric(m.metric) || !m.value) continue;
                metrics.set(m.metric, json::Value::number(*m.value));
            }
            o.set("metrics", std::move(metrics));
        }
        samples.push(std::move(o));
    }
    root.set("samples", std::move(samples));
    return json::write(root);
}

}  // namespace ef::core::diagnosis
