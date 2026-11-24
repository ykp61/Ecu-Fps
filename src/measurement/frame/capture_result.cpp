#include "measurement/frame/capture_result.h"

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>

#include "core/serialization/json.h"

namespace ef::measurement {

using contracts::ErrorCode;
using contracts::Result;
using contracts::Status;
using core::json::Value;

bool is_valid_interval(double ms) noexcept { return std::isfinite(ms) && ms > 0.0; }
bool is_valid_gpu_busy(double ms) noexcept { return std::isfinite(ms) && ms >= 0.0 && ms <= 86400000.0; }

namespace {

Status reject_unknown(const Value& obj, std::initializer_list<std::string_view> allowed, const char* what) {
    for (const auto& k : obj.keys()) {
        bool found = false;
        for (const auto a : allowed)
            if (k == a) found = true;
        if (!found) return Status::fail(ErrorCode::ParseError, std::string(what) + ": unknown field " + k);
    }
    return contracts::ok_status();
}

Result<const Value*> require_object(const Value& root, std::string_view key) {
    const Value* v = root.find(key);
    if (!v) return Result<const Value*>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::Object)) return Result<const Value*>::fail(ErrorCode::ParseError, std::string(key) + " object expected");
    return v;
}

Result<const Value*> require_array(const Value& root, std::string_view key) {
    const Value* v = root.find(key);
    if (!v) return Result<const Value*>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::Array)) return Result<const Value*>::fail(ErrorCode::ParseError, std::string(key) + " array expected");
    return v;
}

Result<std::uint64_t> get_u64(const Value& obj, std::string_view key, std::uint64_t max) {
    const Value* v = obj.find(key);
    if (!v) return Result<std::uint64_t>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::Int)) return Result<std::uint64_t>::fail(ErrorCode::ParseError, std::string(key) + " integer expected");
    const std::int64_t i = v->as_int();
    if (i < 0) return Result<std::uint64_t>::fail(ErrorCode::OutOfRange, std::string(key));
    const auto u = static_cast<std::uint64_t>(i);
    if (u > max) return Result<std::uint64_t>::fail(ErrorCode::OutOfRange, std::string(key));
    return u;
}

Result<std::size_t> get_size(const Value& obj, std::string_view key, std::size_t max) {
    auto v = get_u64(obj, key, static_cast<std::uint64_t>(max));
    if (!v.ok()) return Result<std::size_t>(v.error());
    return static_cast<std::size_t>(v.value());
}

Result<double> get_number(const Value& obj, std::string_view key, double min, double max) {
    const Value* v = obj.find(key);
    if (!v) return Result<double>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::Int) && !v->is(Value::Type::Double))
        return Result<double>::fail(ErrorCode::ParseError, std::string(key) + " number expected");
    const double d = v->as_double();
    if (!std::isfinite(d)) return Result<double>::fail(ErrorCode::OutOfRange, std::string(key));
    if (d < min || d > max) return Result<double>::fail(ErrorCode::OutOfRange, std::string(key));
    return d;
}

Result<std::optional<double>> get_optional_number(const Value& obj, std::string_view key, double max) {
    const Value* v = obj.find(key);
    if (!v) return Result<std::optional<double>>::fail(ErrorCode::MissingValue, std::string(key));
    if (v->is(Value::Type::Null)) return std::optional<double>{};
    if (!v->is(Value::Type::Int) && !v->is(Value::Type::Double))
        return Result<std::optional<double>>::fail(ErrorCode::ParseError, std::string(key) + " number or null expected");
    const double d = v->as_double();
    if (!std::isfinite(d) || d < 0.0 || d > max)
        return Result<std::optional<double>>::fail(ErrorCode::OutOfRange, std::string(key));
    return std::optional<double>(d);
}

std::string_view target_error_name(core::measure::TargetError e) noexcept { return core::measure::to_string(e); }

Result<core::measure::TargetError> parse_target_error(std::string_view s) {
    using R = Result<core::measure::TargetError>;
    using E = core::measure::TargetError;
    if (s == "none") return E::None;
    if (s == "no_candidate") return E::NoCandidate;
    if (s == "identity_mismatch") return E::IdentityMismatch;
    if (s == "overlay") return E::Overlay;
    if (s == "unknown_process") return E::UnknownProcess;
    return R::fail(ErrorCode::InvalidEnum, "target_error");
}

std::string_view start_error_name(FrameSessionError e) noexcept {
    switch (e) {
        case FrameSessionError::None: return "none";
        case FrameSessionError::AccessDenied: return "access_denied";
        case FrameSessionError::AlreadyRunning: return "already_running";
        case FrameSessionError::Other: return "other";
    }
    return "other";
}

Result<FrameSessionError> parse_start_error(std::string_view s) {
    using R = Result<FrameSessionError>;
    if (s == "none") return FrameSessionError::None;
    if (s == "access_denied") return FrameSessionError::AccessDenied;
    if (s == "already_running") return FrameSessionError::AlreadyRunning;
    if (s == "other") return FrameSessionError::Other;
    return R::fail(ErrorCode::InvalidEnum, "start_error");
}

std::string_view runtime_name(core::measure::Runtime r) noexcept {
    switch (r) {
        case core::measure::Runtime::Dxgi: return "dxgi";
        case core::measure::Runtime::D3d9: return "d3d9";
        case core::measure::Runtime::Other: return "other";
        case core::measure::Runtime::Unknown: return "unknown";
    }
    return "unknown";
}

Result<core::measure::Runtime> parse_runtime(std::string_view s) {
    using R = Result<core::measure::Runtime>;
    using E = core::measure::Runtime;
    if (s == "dxgi") return E::Dxgi;
    if (s == "d3d9") return E::D3d9;
    if (s == "other") return E::Other;
    if (s == "unknown") return E::Unknown;
    return R::fail(ErrorCode::InvalidEnum, "runtime");
}

Result<core::measure::Validity> parse_validity(std::string_view s) {
    using R = Result<core::measure::Validity>;
    using E = core::measure::Validity;
    if (s == "valid") return E::Valid;
    if (s == "low_confidence") return E::LowConfidence;
    if (s == "invalid") return E::Invalid;
    return R::fail(ErrorCode::InvalidEnum, "validity");
}

Result<core::measure::Exclusion> parse_exclusion(std::string_view s) {
    using R = Result<core::measure::Exclusion>;
    using E = core::measure::Exclusion;
    if (s == "none") return E::None;
    if (s == "etw_events_lost") return E::EtwEventsLost;
    if (s == "too_few_frames") return E::TooFewFrames;
    if (s == "target_mismatch") return E::TargetMismatch;
    if (s == "overlay") return E::Overlay;
    if (s == "runtime_unknown") return E::RuntimeUnknown;
    if (s == "cancelled") return E::Cancelled;
    if (s == "too_short") return E::TooShort;
    if (s == "runtime_mixed") return E::RuntimeMixed;  // onceden kodlanip cozulemiyordu (gercek kosu capture_failed)
    if (s == "runtime_other") return E::RuntimeOther;
    if (s == "game_exited") return E::GameExited;
    return R::fail(ErrorCode::InvalidEnum, "exclusion");
}

Result<core::measure::CaptureOutcome> parse_capture_outcome(std::string_view s) {
    using E = core::measure::CaptureOutcome;
    for (const E e : {E::NotStarted, E::Complete, E::Cancelled, E::GameExited, E::TargetChanged})
        if (s == core::measure::to_string(e)) return e;
    return Result<E>::fail(ErrorCode::InvalidEnum, "run_outcome");
}

bool plain_ascii(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (const char c : s)
        if (c < 0x20 || c > 0x7E) return false;
    return true;
}

Value string_array(const std::vector<std::string>& v) {
    auto a = Value::array();
    for (const auto& s : v) a.push(Value::string(s));
    return a;
}

Result<std::vector<core::measure::Exclusion>> get_exclusions(const Value& obj) {
    using R = Result<std::vector<core::measure::Exclusion>>;
    auto arr = require_array(obj, "exclusions");
    if (!arr.ok()) return R(arr.error());
    if (arr.value()->items().size() > 16) return R::fail(ErrorCode::ParseLimit, "exclusions");
    std::vector<core::measure::Exclusion> out;
    for (const auto& item : arr.value()->items()) {
        if (!item.is(Value::Type::String)) return R::fail(ErrorCode::ParseError, "exclusion type");
        auto e = parse_exclusion(item.as_string());
        if (!e.ok()) return R(e.error());
        out.push_back(e.value());
    }
    return out;
}

Result<std::vector<std::string>> get_string_list(const Value& obj, std::string_view key, std::size_t max_items, std::size_t max_len) {
    using R = Result<std::vector<std::string>>;
    auto arr = require_array(obj, key);
    if (!arr.ok()) return R(arr.error());
    if (arr.value()->items().size() > max_items) return R::fail(ErrorCode::ParseLimit, std::string(key));
    std::vector<std::string> out;
    for (const auto& item : arr.value()->items()) {
        if (!item.is(Value::Type::String)) return R::fail(ErrorCode::ParseError, std::string(key) + " string expected");
        if (!plain_ascii(item.as_string()) || item.as_string().size() > max_len)
            return R::fail(ErrorCode::ParseError, std::string(key) + " text");
        out.push_back(item.as_string());
    }
    return out;
}

Result<std::map<std::string, std::string>> get_context(const Value& obj) {
    using R = Result<std::map<std::string, std::string>>;
    const Value* ctx = obj.find("context");
    if (!ctx) return R::fail(ErrorCode::MissingValue, "context");
    if (!ctx->is(Value::Type::Object)) return R::fail(ErrorCode::ParseError, "context object expected");
    if (ctx->keys().size() > 64) return R::fail(ErrorCode::ParseLimit, "context");
    std::map<std::string, std::string> out;
    for (std::size_t i = 0; i < ctx->keys().size(); ++i) {
        const std::string& k = ctx->keys()[i];
        if (k.empty() || k.size() > 64 || !plain_ascii(k)) return R::fail(ErrorCode::ParseError, "context key");
        const Value& v = ctx->items()[i];
        if (!v.is(Value::Type::String)) return R::fail(ErrorCode::ParseError, "context value");
        if (!plain_ascii(v.as_string()) || v.as_string().size() > 512) return R::fail(ErrorCode::ParseError, "context value text");
        out.emplace(k, v.as_string());
    }
    return out;
}

}  // namespace

CaptureResultHeader header_from(const GameCaptureResult& r, const core::measure::ProcessIdentity& target) {
    CaptureResultHeader h;
    h.target_pid = target.pid;
    h.target_create_time = target.create_time;
    h.target_image = target.image_path;
    h.target_error = r.target;

    h.start_ok = r.start.ok();
    h.start_error = r.start.error;
    h.start_win32 = r.start.win32;
    h.start_detail = r.start.detail;

    h.summary_swapchain = r.summary.swapchain;
    h.summary_other_swapchains = r.summary.other_swapchains;
    h.summary_render_frames = r.summary.render_frames;
    h.summary_displayed_frames = r.summary.displayed_frames;
    h.summary_generated_frames = r.summary.generated_frames;
    h.summary_runtime = r.summary.runtime;
    h.summary_validity = r.summary.validity;
    h.summary_reasons = r.summary.reasons;

    h.stats_n = r.stats.n;
    h.stats_excluded = r.stats.excluded;
    h.stats_valid_seconds = r.stats.valid_seconds;
    h.avg_fps = r.stats.avg_fps;
    h.low1_fps = r.stats.low1_fps;
    h.low01_fps = r.stats.low01_fps;
    h.p95_ms = r.stats.p95_ms;
    h.p99_ms = r.stats.p99_ms;
    h.p99_9_ms = r.stats.p99_9_ms;
    h.stats_median_ms = r.stats.median_ms;
    h.stats_max_ms = r.stats.max_ms;
    h.stats_pattern = std::string(core::measure::to_string(r.stats.pattern));
    h.stats_sufficient = r.stats.sufficient;

    h.manifest_schema_version = r.manifest.schema_version;
    h.manifest_source = r.manifest.source;
    h.manifest_app = r.manifest.app;
    h.manifest_context = r.manifest.context;
    h.manifest_stats_definition = r.manifest.stats_definition;
    h.manifest_presentmon_version = r.manifest.presentmon_version;
    h.manifest_valid_seconds = r.manifest.valid_seconds;
    h.manifest_intervals = r.manifest.intervals;
    h.manifest_excluded_intervals = r.manifest.excluded_intervals;
    h.manifest_events_lost = r.manifest.events_lost;
    h.manifest_exclusions = r.manifest.exclusions;
    h.manifest_valid = r.manifest.valid;

    h.dropped_presents = r.dropped_presents;
    h.capture_origin_ms = r.capture_origin_ms;
    h.interval_count = r.summary.render_intervals_ms.size();
    h.total_bytes = h.interval_count * sizeof(float);
    h.display_interval_count = r.summary.display_intervals_ms.size();
    h.display_total_bytes = h.display_interval_count * sizeof(float);
    h.run_outcome = r.outcome;
    h.target_exited_after_s = r.target_exited_after_s;
    h.gpu_busy_count = r.summary.render_gpu_busy_ms.size();
    h.gpu_busy_total_bytes = h.gpu_busy_count * sizeof(float);
    return h;
}

Result<GameCaptureResult> result_from(const CaptureResultHeader& h, std::vector<double> intervals, std::vector<double> display_intervals) {
    return result_from(h, std::move(intervals), std::move(display_intervals), {});  // GPU plani bos olmali
}

Result<GameCaptureResult> result_from(const CaptureResultHeader& h, std::vector<double> intervals, std::vector<double> display_intervals,
                                      std::vector<double> gpu_busy) {
    using R = Result<GameCaptureResult>;
    if (h.interval_count > kMaxIntervalCount || h.total_bytes > kMaxTotalBytes)
        return R::fail(ErrorCode::ParseLimit, "frame plan");
    if (h.total_bytes != h.interval_count * sizeof(float)) return R::fail(ErrorCode::ParseError, "frame plan bytes");
    if (intervals.size() != h.interval_count) return R::fail(ErrorCode::ParseError, "interval count mismatch");
    if (h.display_interval_count > kMaxIntervalCount || h.display_total_bytes > kMaxTotalBytes)
        return R::fail(ErrorCode::ParseLimit, "display frame plan");
    if (h.display_total_bytes != h.display_interval_count * sizeof(float))
        return R::fail(ErrorCode::ParseError, "display frame plan bytes");
    if (display_intervals.size() != h.display_interval_count)
        return R::fail(ErrorCode::ParseError, "display interval count mismatch");
    for (const double v : display_intervals)
        if (!is_valid_interval(v)) return R::fail(ErrorCode::OutOfRange, "display interval");
    if (h.target_exited_after_s.has_value() != (h.run_outcome == core::measure::CaptureOutcome::GameExited))
        return R::fail(ErrorCode::ParseError, "target exit");
    if (h.start_ok != (h.start_error == FrameSessionError::None)) return R::fail(ErrorCode::ParseError, "start flag");
    for (const double v : intervals)
        if (!is_valid_interval(v)) return R::fail(ErrorCode::OutOfRange, "interval");
    if (h.gpu_busy_count > kMaxIntervalCount || h.gpu_busy_total_bytes > kMaxTotalBytes)
        return R::fail(ErrorCode::ParseLimit, "gpu busy plan");
    if (h.gpu_busy_total_bytes != h.gpu_busy_count * sizeof(float)) return R::fail(ErrorCode::ParseError, "gpu busy plan bytes");
    if (h.gpu_busy_count != 0 && h.gpu_busy_count != h.interval_count)
        return R::fail(ErrorCode::ParseError, "gpu busy plan alignment");
    if (gpu_busy.size() != h.gpu_busy_count) return R::fail(ErrorCode::ParseError, "gpu busy count mismatch");
    for (const double v : gpu_busy)
        if (!is_valid_gpu_busy(v)) return R::fail(ErrorCode::OutOfRange, "gpu busy");

    GameCaptureResult r;
    r.target = h.target_error;
    r.start.error = h.start_error;
    r.start.win32 = h.start_win32;
    r.start.detail = h.start_detail;

    r.summary.swapchain = h.summary_swapchain;
    r.summary.other_swapchains = h.summary_other_swapchains;
    r.summary.render_frames = h.summary_render_frames;
    r.summary.displayed_frames = h.summary_displayed_frames;
    r.summary.generated_frames = h.summary_generated_frames;
    r.summary.render_intervals_ms = std::move(intervals);
    r.summary.display_intervals_ms = std::move(display_intervals);
    r.summary.render_gpu_busy_ms = std::move(gpu_busy);
    r.summary.runtime = h.summary_runtime;
    r.summary.validity = h.summary_validity;
    r.summary.reasons = h.summary_reasons;

    r.stats.n = h.stats_n;
    r.stats.excluded = h.stats_excluded;
    r.stats.valid_seconds = h.stats_valid_seconds;
    r.stats.avg_fps = h.avg_fps;
    r.stats.low1_fps = h.low1_fps;
    r.stats.low01_fps = h.low01_fps;
    r.stats.p95_ms = h.p95_ms;
    r.stats.p99_ms = h.p99_ms;
    r.stats.p99_9_ms = h.p99_9_ms;
    r.stats.median_ms = h.stats_median_ms;
    r.stats.max_ms = h.stats_max_ms;
    r.stats.pattern = core::measure::frame_pattern_from_string(h.stats_pattern);
    r.stats.sufficient = h.stats_sufficient;

    r.manifest.schema_version = h.manifest_schema_version;
    r.manifest.source = h.manifest_source;
    r.manifest.app = h.manifest_app;
    r.manifest.context = h.manifest_context;
    r.manifest.stats_definition = h.manifest_stats_definition;
    r.manifest.presentmon_version = h.manifest_presentmon_version;
    r.manifest.valid_seconds = h.manifest_valid_seconds;
    r.manifest.intervals = h.manifest_intervals;
    r.manifest.excluded_intervals = h.manifest_excluded_intervals;
    r.manifest.events_lost = h.manifest_events_lost;
    r.manifest.exclusions = h.manifest_exclusions;
    r.manifest.valid = h.manifest_valid;

    r.dropped_presents = h.dropped_presents;
    r.capture_origin_ms = h.capture_origin_ms;
    r.outcome = h.run_outcome;
    r.target_exited_after_s = h.target_exited_after_s;
    return r;
}

Result<GameCaptureResult> result_from(const CaptureResultHeader& h, std::vector<double> intervals) {
    return result_from(h, std::move(intervals), {}, {});  // ekran ve GPU plani bos olmali; aksi halde sayi uyusmazligi
}

std::string encode_header(const CaptureResultHeader& h) {
    auto target = Value::object();
    target.set("pid", Value::integer(static_cast<std::int64_t>(h.target_pid)));
    target.set("create_time", Value::integer(static_cast<std::int64_t>(h.target_create_time)));
    target.set("image", Value::string(h.target_image));

    auto start = Value::object();
    start.set("ok", Value::boolean(h.start_ok));
    start.set("error", Value::string(std::string(start_error_name(h.start_error))));
    start.set("win32", Value::integer(static_cast<std::int64_t>(h.start_win32)));
    start.set("detail", Value::string(h.start_detail));

    auto summary = Value::object();
    summary.set("swapchain", Value::integer(static_cast<std::int64_t>(h.summary_swapchain)));
    summary.set("other_swapchains", Value::integer(static_cast<std::int64_t>(h.summary_other_swapchains)));
    summary.set("render_frames", Value::integer(static_cast<std::int64_t>(h.summary_render_frames)));
    summary.set("displayed_frames", Value::integer(static_cast<std::int64_t>(h.summary_displayed_frames)));
    summary.set("generated_frames", Value::integer(static_cast<std::int64_t>(h.summary_generated_frames)));
    summary.set("runtime", Value::string(std::string(runtime_name(h.summary_runtime))));
    summary.set("validity", Value::string(std::string(core::measure::to_string(h.summary_validity))));
    summary.set("reasons", string_array(h.summary_reasons));

    auto stats = Value::object();
    stats.set("n", Value::integer(static_cast<std::int64_t>(h.stats_n)));
    stats.set("excluded", Value::integer(static_cast<std::int64_t>(h.stats_excluded)));
    stats.set("valid_seconds", Value::number(h.stats_valid_seconds));
    stats.set("avg_fps", h.avg_fps ? Value::number(*h.avg_fps) : Value::null());
    stats.set("low1_fps", h.low1_fps ? Value::number(*h.low1_fps) : Value::null());
    stats.set("low01_fps", h.low01_fps ? Value::number(*h.low01_fps) : Value::null());
    stats.set("p95_ms", h.p95_ms ? Value::number(*h.p95_ms) : Value::null());
    stats.set("p99_ms", h.p99_ms ? Value::number(*h.p99_ms) : Value::null());
    stats.set("p99_9_ms", h.p99_9_ms ? Value::number(*h.p99_9_ms) : Value::null());
    stats.set("median_ms", Value::number(h.stats_median_ms));
    stats.set("max_ms", Value::number(h.stats_max_ms));
    stats.set("pattern", Value::string(h.stats_pattern));
    stats.set("sufficient", Value::boolean(h.stats_sufficient));

    auto context = Value::object();
    for (const auto& [k, v] : h.manifest_context) context.set(k, Value::string(v));

    auto manifest = Value::object();
    manifest.set("schema_version", Value::integer(h.manifest_schema_version));
    manifest.set("source", Value::string(h.manifest_source));
    manifest.set("app", Value::string(h.manifest_app));
    manifest.set("context", std::move(context));
    manifest.set("stats_definition", Value::string(h.manifest_stats_definition));
    manifest.set("presentmon_version", Value::string(h.manifest_presentmon_version));
    manifest.set("valid_seconds", Value::number(h.manifest_valid_seconds));
    manifest.set("intervals", Value::integer(static_cast<std::int64_t>(h.manifest_intervals)));
    manifest.set("excluded_intervals", Value::integer(static_cast<std::int64_t>(h.manifest_excluded_intervals)));
    manifest.set("events_lost", Value::integer(static_cast<std::int64_t>(h.manifest_events_lost)));
    auto exclusions = Value::array();
    for (const auto e : h.manifest_exclusions)
        exclusions.push(Value::string(std::string(core::measure::to_string(e))));
    manifest.set("exclusions", std::move(exclusions));
    manifest.set("valid", Value::boolean(h.manifest_valid));

    auto root = Value::object();
    root.set("v", Value::integer(kCaptureResultVersion));
    root.set("target", std::move(target));
    root.set("target_error", Value::string(std::string(target_error_name(h.target_error))));
    root.set("start", std::move(start));
    root.set("summary", std::move(summary));
    root.set("stats", std::move(stats));
    root.set("manifest", std::move(manifest));
    root.set("dropped_presents", Value::integer(static_cast<std::int64_t>(h.dropped_presents)));
    root.set("capture_origin_ms", h.capture_origin_ms ? Value::number(*h.capture_origin_ms) : Value::null());
    root.set("interval_count", Value::integer(static_cast<std::int64_t>(h.interval_count)));
    root.set("total_bytes", Value::integer(static_cast<std::int64_t>(h.total_bytes)));
    root.set("display_interval_count", Value::integer(static_cast<std::int64_t>(h.display_interval_count)));
    root.set("display_total_bytes", Value::integer(static_cast<std::int64_t>(h.display_total_bytes)));
    root.set("run_outcome", Value::string(std::string(core::measure::to_string(h.run_outcome))));
    root.set("target_exited_after_s", h.target_exited_after_s ? Value::number(*h.target_exited_after_s) : Value::null());
    root.set("gpu_busy_count", Value::integer(static_cast<std::int64_t>(h.gpu_busy_count)));
    root.set("gpu_busy_total_bytes", Value::integer(static_cast<std::int64_t>(h.gpu_busy_total_bytes)));
    return core::json::write(root);
}

Result<CaptureResultHeader> decode_header(std::string_view payload) {
    using R = Result<CaptureResultHeader>;
    core::json::Limits limits;
    limits.max_bytes = 64 * 1024;
    limits.max_depth = 8;
    limits.max_string = 4096;
    limits.max_items = 512;
    auto doc = core::json::parse(payload, limits);
    if (!doc.ok()) return R(doc.error());
    const Value& root = doc.value();
    if (!root.is(Value::Type::Object)) return R::fail(ErrorCode::ParseError, "header object expected");
    if (auto s = reject_unknown(root, {"v", "target", "target_error", "start", "summary", "stats", "manifest", "dropped_presents",
                                       "capture_origin_ms", "interval_count", "total_bytes", "display_interval_count",
                                       "display_total_bytes", "run_outcome", "target_exited_after_s", "gpu_busy_count",
                                       "gpu_busy_total_bytes"},
                                "header");
        !s.ok())
        return R(s.error());

    auto ver = core::json::get_int(root, "v", 1, 1000);
    if (!ver.ok()) return R(ver.error());
    if (ver.value() != kCaptureResultVersion)
        return R::fail(ver.value() > kCaptureResultVersion ? ErrorCode::SchemaTooNew : ErrorCode::InvalidEnum, "version");

    CaptureResultHeader h;

    auto target = require_object(root, "target");
    if (!target.ok()) return R(target.error());
    if (auto s = reject_unknown(*target.value(), {"pid", "create_time", "image"}, "target"); !s.ok()) return R(s.error());
    auto pid = get_u64(*target.value(), "pid", 0xFFFFFFFFull);
    if (!pid.ok()) return R(pid.error());
    h.target_pid = static_cast<std::uint32_t>(pid.value());
    auto create_time = get_u64(*target.value(), "create_time", std::numeric_limits<std::uint64_t>::max());
    if (!create_time.ok()) return R(create_time.error());
    h.target_create_time = create_time.value();
    auto image = core::json::get_string(*target.value(), "image", 2048);
    if (!image.ok()) return R(image.error());
    h.target_image = image.value();

    auto target_error = core::json::get_string(root, "target_error", 32);
    if (!target_error.ok()) return R(target_error.error());
    auto ter = parse_target_error(target_error.value());
    if (!ter.ok()) return R(ter.error());
    h.target_error = ter.value();

    auto start = require_object(root, "start");
    if (!start.ok()) return R(start.error());
    if (auto s = reject_unknown(*start.value(), {"ok", "error", "win32", "detail"}, "start"); !s.ok()) return R(s.error());
    auto start_ok = core::json::get_bool(*start.value(), "ok");
    if (!start_ok.ok()) return R(start_ok.error());
    h.start_ok = start_ok.value();
    auto start_error = core::json::get_string(*start.value(), "error", 32);
    if (!start_error.ok()) return R(start_error.error());
    auto se = parse_start_error(start_error.value());
    if (!se.ok()) return R(se.error());
    h.start_error = se.value();
    auto win32 = get_u64(*start.value(), "win32", 0xFFFFFFFFull);
    if (!win32.ok()) return R(win32.error());
    h.start_win32 = static_cast<std::uint32_t>(win32.value());
    auto detail = core::json::get_string(*start.value(), "detail", 512);
    if (!detail.ok()) return R(detail.error());
    h.start_detail = detail.value();

    auto summary = require_object(root, "summary");
    if (!summary.ok()) return R(summary.error());
    if (auto s = reject_unknown(*summary.value(),
                                {"swapchain", "other_swapchains", "render_frames", "displayed_frames", "generated_frames", "runtime",
                                 "validity", "reasons"},
                                "summary");
        !s.ok())
        return R(s.error());
    auto swapchain = get_u64(*summary.value(), "swapchain", std::numeric_limits<std::uint64_t>::max());
    if (!swapchain.ok()) return R(swapchain.error());
    h.summary_swapchain = swapchain.value();
    auto other = get_size(*summary.value(), "other_swapchains", 4096);
    if (!other.ok()) return R(other.error());
    h.summary_other_swapchains = other.value();
    for (auto [key, dst] : {std::pair{"render_frames", &h.summary_render_frames},
                            std::pair{"displayed_frames", &h.summary_displayed_frames},
                            std::pair{"generated_frames", &h.summary_generated_frames}}) {
        auto v = get_size(*summary.value(), key, 1'000'000'000);
        if (!v.ok()) return R(v.error());
        *dst = v.value();
    }
    auto runtime = core::json::get_string(*summary.value(), "runtime", 16);
    if (!runtime.ok()) return R(runtime.error());
    auto rt = parse_runtime(runtime.value());
    if (!rt.ok()) return R(rt.error());
    h.summary_runtime = rt.value();
    auto validity = core::json::get_string(*summary.value(), "validity", 32);
    if (!validity.ok()) return R(validity.error());
    auto va = parse_validity(validity.value());
    if (!va.ok()) return R(va.error());
    h.summary_validity = va.value();
    auto reasons = get_string_list(*summary.value(), "reasons", 16, 64);
    if (!reasons.ok()) return R(reasons.error());
    h.summary_reasons = std::move(reasons).value();

    auto stats = require_object(root, "stats");
    if (!stats.ok()) return R(stats.error());
    if (auto s = reject_unknown(*stats.value(),
                                {"n", "excluded", "valid_seconds", "avg_fps", "low1_fps", "low01_fps", "p95_ms", "p99_ms", "p99_9_ms",
                                 "median_ms", "max_ms", "pattern", "sufficient"},
                                "stats");
        !s.ok())
        return R(s.error());
    auto n = get_size(*stats.value(), "n", kMaxIntervalCount);
    if (!n.ok()) return R(n.error());
    h.stats_n = n.value();
    auto excluded = get_size(*stats.value(), "excluded", kMaxIntervalCount);
    if (!excluded.ok()) return R(excluded.error());
    h.stats_excluded = excluded.value();
    auto valid_seconds = get_number(*stats.value(), "valid_seconds", 0.0, 86400.0);
    if (!valid_seconds.ok()) return R(valid_seconds.error());
    h.stats_valid_seconds = valid_seconds.value();
    for (auto [key, dst] : {std::pair{"avg_fps", &h.avg_fps}, std::pair{"low1_fps", &h.low1_fps},
                            std::pair{"low01_fps", &h.low01_fps}, std::pair{"p95_ms", &h.p95_ms}, std::pair{"p99_ms", &h.p99_ms},
                            std::pair{"p99_9_ms", &h.p99_9_ms}}) {
        auto v = get_optional_number(*stats.value(), key, 1'000'000.0);
        if (!v.ok()) return R(v.error());
        *dst = v.value();
    }
    auto median = get_number(*stats.value(), "median_ms", 0.0, 1'000'000.0);
    if (!median.ok()) return R(median.error());
    h.stats_median_ms = median.value();
    auto max = get_number(*stats.value(), "max_ms", 0.0, 1'000'000.0);
    if (!max.ok()) return R(max.error());
    h.stats_max_ms = max.value();
    auto pattern = core::json::get_string(*stats.value(), "pattern", 32);
    if (!pattern.ok()) return R(pattern.error());
    h.stats_pattern = std::move(pattern).value();
    auto sufficient = core::json::get_bool(*stats.value(), "sufficient");
    if (!sufficient.ok()) return R(sufficient.error());
    h.stats_sufficient = sufficient.value();

    auto manifest = require_object(root, "manifest");
    if (!manifest.ok()) return R(manifest.error());
    if (auto s = reject_unknown(*manifest.value(),
                                {"schema_version", "source", "app", "context", "stats_definition", "presentmon_version", "valid_seconds",
                                 "intervals", "excluded_intervals", "events_lost", "exclusions", "valid"},
                                "manifest");
        !s.ok())
        return R(s.error());
    auto schema = core::json::get_int(*manifest.value(), "schema_version", 1, 1000);
    if (!schema.ok()) return R(schema.error());
    if (schema.value() != 1)
        return R::fail(schema.value() > 1 ? ErrorCode::SchemaTooNew : ErrorCode::InvalidEnum, "manifest schema_version");
    h.manifest_schema_version = static_cast<int>(schema.value());
    auto source = core::json::get_string(*manifest.value(), "source", 32);
    if (!source.ok()) return R(source.error());
    h.manifest_source = source.value();
    auto app = core::json::get_string(*manifest.value(), "app", 128);
    if (!app.ok()) return R(app.error());
    h.manifest_app = app.value();
    auto context = get_context(*manifest.value());
    if (!context.ok()) return R(context.error());
    h.manifest_context = std::move(context).value();
    auto definition = core::json::get_string(*manifest.value(), "stats_definition", 32);
    if (!definition.ok()) return R(definition.error());
    h.manifest_stats_definition = definition.value();
    auto presentmon = core::json::get_string(*manifest.value(), "presentmon_version", 32);
    if (!presentmon.ok()) return R(presentmon.error());
    h.manifest_presentmon_version = presentmon.value();
    auto manifest_seconds = get_number(*manifest.value(), "valid_seconds", 0.0, 86400.0);
    if (!manifest_seconds.ok()) return R(manifest_seconds.error());
    h.manifest_valid_seconds = manifest_seconds.value();
    auto intervals = get_size(*manifest.value(), "intervals", kMaxIntervalCount);
    if (!intervals.ok()) return R(intervals.error());
    h.manifest_intervals = intervals.value();
    auto manifest_excluded = get_size(*manifest.value(), "excluded_intervals", kMaxIntervalCount);
    if (!manifest_excluded.ok()) return R(manifest_excluded.error());
    h.manifest_excluded_intervals = manifest_excluded.value();
    auto events_lost = get_u64(*manifest.value(), "events_lost", 0xFFFFFFFFull);
    if (!events_lost.ok()) return R(events_lost.error());
    h.manifest_events_lost = static_cast<std::uint32_t>(events_lost.value());
    auto exclusions = get_exclusions(*manifest.value());
    if (!exclusions.ok()) return R(exclusions.error());
    h.manifest_exclusions = std::move(exclusions).value();
    auto manifest_valid = core::json::get_bool(*manifest.value(), "valid");
    if (!manifest_valid.ok()) return R(manifest_valid.error());
    h.manifest_valid = manifest_valid.value();

    auto dropped = get_u64(root, "dropped_presents", std::numeric_limits<std::uint64_t>::max());
    if (!dropped.ok()) return R(dropped.error());
    h.dropped_presents = dropped.value();
    // QPC kokeni: negatif/NaN/sonsuz reddedilir; null = bilinmiyor (hiza iddia edilmez).
    auto origin = get_optional_number(root, "capture_origin_ms", 1e12);
    if (!origin.ok()) return R(origin.error());
    h.capture_origin_ms = origin.value();
    auto count = get_size(root, "interval_count", kMaxIntervalCount);
    if (!count.ok()) return R(count.error());
    h.interval_count = count.value();
    auto bytes = get_size(root, "total_bytes", kMaxTotalBytes);
    if (!bytes.ok()) return R(bytes.error());
    h.total_bytes = bytes.value();
    if (h.total_bytes != h.interval_count * sizeof(float)) return R::fail(ErrorCode::ParseError, "frame plan bytes");
    if (h.start_ok != (h.start_error == FrameSessionError::None)) return R::fail(ErrorCode::ParseError, "start flag");
    auto dcount = get_size(root, "display_interval_count", kMaxIntervalCount);
    if (!dcount.ok()) return R(dcount.error());
    h.display_interval_count = dcount.value();
    auto dbytes = get_size(root, "display_total_bytes", kMaxTotalBytes);
    if (!dbytes.ok()) return R(dbytes.error());
    h.display_total_bytes = dbytes.value();
    if (h.display_total_bytes != h.display_interval_count * sizeof(float))
        return R::fail(ErrorCode::ParseError, "display frame plan bytes");
    auto outcome = core::json::get_string(root, "run_outcome", 32);
    if (!outcome.ok()) return R(outcome.error());
    auto parsed = parse_capture_outcome(outcome.value());
    if (!parsed.ok()) return R(parsed.error());
    h.run_outcome = parsed.value();
    auto exited = get_optional_number(root, "target_exited_after_s", 1e6);
    if (!exited.ok()) return R(exited.error());
    h.target_exited_after_s = exited.value();
    if (h.target_exited_after_s.has_value() != (h.run_outcome == core::measure::CaptureOutcome::GameExited))
        return R::fail(ErrorCode::ParseError, "target exit");
    auto gcount = get_size(root, "gpu_busy_count", kMaxIntervalCount);
    if (!gcount.ok()) return R(gcount.error());
    h.gpu_busy_count = gcount.value();
    auto gbytes = get_size(root, "gpu_busy_total_bytes", kMaxTotalBytes);
    if (!gbytes.ok()) return R(gbytes.error());
    h.gpu_busy_total_bytes = gbytes.value();
    if (h.gpu_busy_total_bytes != h.gpu_busy_count * sizeof(float)) return R::fail(ErrorCode::ParseError, "gpu busy plan bytes");
    if (h.gpu_busy_count != 0 && h.gpu_busy_count != h.interval_count)
        return R::fail(ErrorCode::ParseError, "gpu busy plan alignment");
    return h;
}

Status encode_chunk(const double* values, std::size_t n, std::string& out, ChunkKind kind) {
    if (n > kMaxChunkValues) return Status::fail(ErrorCode::ParseLimit, "chunk too large");
    if (n > 0 && !values) return Status::fail(ErrorCode::ParseError, "missing values");
    const bool busy = kind == ChunkKind::GpuBusy;
    for (std::size_t i = 0; i < n; ++i) {
        if (busy ? !is_valid_gpu_busy(values[i]) : !is_valid_interval(values[i]))
            return Status::fail(ErrorCode::OutOfRange, busy ? "gpu busy" : "interval");
        const float f = static_cast<float>(values[i]);
        if (!std::isfinite(f) || (busy ? f < 0.0f : !(f > 0.0f)))
            return Status::fail(ErrorCode::OutOfRange, busy ? "gpu busy float32" : "interval float32");
    }
    out.resize(n * sizeof(float));
    for (std::size_t i = 0; i < n; ++i) {
        const float f = static_cast<float>(values[i]);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        out[i * 4 + 0] = static_cast<char>(bits & 0xFFu);
        out[i * 4 + 1] = static_cast<char>((bits >> 8) & 0xFFu);
        out[i * 4 + 2] = static_cast<char>((bits >> 16) & 0xFFu);
        out[i * 4 + 3] = static_cast<char>((bits >> 24) & 0xFFu);
    }
    return contracts::ok_status();
}

Result<std::vector<double>> decode_chunk(std::string_view bytes, std::size_t max_values, ChunkKind kind) {
    using R = Result<std::vector<double>>;
    if (bytes.empty()) return R::fail(ErrorCode::ParseError, "empty chunk");
    if (bytes.size() % sizeof(float) != 0) return R::fail(ErrorCode::ParseError, "chunk alignment");
    if (bytes.size() > kMaxChunkBytes) return R::fail(ErrorCode::ParseLimit, "chunk too large");
    const std::size_t n = bytes.size() / sizeof(float);
    if (n > kMaxChunkValues) return R::fail(ErrorCode::ParseLimit, "chunk values");
    if (n > max_values) return R::fail(ErrorCode::ParseLimit, "chunk value count");
    std::vector<double> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i * 4 + 0]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i * 4 + 1]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i * 4 + 2]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i * 4 + 3]));
        const std::uint32_t bits = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        float f = 0;
        std::memcpy(&f, &bits, sizeof f);
        if (kind == ChunkKind::GpuBusy) {
            if (!is_valid_gpu_busy(static_cast<double>(f)) || std::signbit(f)) return R::fail(ErrorCode::OutOfRange, "gpu busy");
        } else if (!std::isfinite(f) || !(f > 0.0f)) {
            return R::fail(ErrorCode::OutOfRange, "interval");
        }
        out.push_back(static_cast<double>(f));
    }
    return out;
}

}  // namespace ef::measurement
