#include "core/support/diag.h"

#include <cmath>
#include <utility>

namespace ef::core::support {

namespace {
using json::Value;

template <typename T>
Value opt_int(const std::optional<T>& v) {
    return v ? Value::integer(static_cast<std::int64_t>(*v)) : Value::null();
}
Value opt_bool(const std::optional<bool>& v) { return v ? Value::boolean(*v) : Value::null(); }
Value opt_num(const std::optional<double>& v) {
    return v && std::isfinite(*v) ? Value::number(std::round(*v * 100.0) / 100.0) : Value::null();
}
Value str_or_null(const std::string& s) { return s.empty() ? Value::null() : Value::string(s); }
}  // namespace

json::Value diag_json(const DiagInput& in) {
    auto root = Value::object();
    root.set("schema_version", Value::integer(kDiagSchemaVersion));
    root.set("app_version", str_or_null(in.app_version));
    root.set("build_id", str_or_null(in.build_id));
    root.set("os_build", str_or_null(in.os_build));

    auto cpu = Value::object();
    cpu.set("vendor", str_or_null(in.cpu.vendor));
    cpu.set("brand", str_or_null(in.cpu.brand));
    cpu.set("packages", opt_int(in.cpu.packages));
    cpu.set("physical_cores", opt_int(in.cpu.physical_cores));
    cpu.set("logical_processors", opt_int(in.cpu.logical_processors));
    cpu.set("performance_cores", opt_int(in.cpu.performance_cores));
    cpu.set("efficiency_cores", opt_int(in.cpu.efficiency_cores));
    cpu.set("smt", opt_bool(in.cpu.smt));
    cpu.set("hybrid", opt_bool(in.cpu.hybrid));
    root.set("cpu", std::move(cpu));

    auto adapters = Value::array();
    std::string used;
    for (const auto& a : in.adapters) {
        auto o = Value::object();
        o.set("index", Value::integer(a.index));
        o.set("name", str_or_null(a.name));
        o.set("vendor_id", opt_int(a.vendor_id));
        o.set("device_id", opt_int(a.device_id));
        o.set("luid", str_or_null(a.luid));
        o.set("driver_version", str_or_null(a.driver_version));
        o.set("dedicated_vram_mib", opt_int(a.dedicated_vram_mib));
        o.set("shared_mib", opt_int(a.shared_mib));
        o.set("kind", str_or_null(a.kind));
        o.set("high_performance", Value::boolean(a.high_performance));
        if (a.high_performance && used.empty()) used = a.luid;
        adapters.push(std::move(o));
    }
    root.set("adapters", std::move(adapters));
    root.set("game_adapter_luid", str_or_null(used));  // oyunlarin (DXGI yuksek performans) kullandigi kart

    auto displays = Value::array();
    for (const auto& d : in.displays) {
        auto o = Value::object();
        o.set("index", Value::integer(d.index));
        o.set("name", str_or_null(d.name));
        o.set("primary", Value::boolean(d.primary));
        o.set("width", d.width ? Value::integer(d.width) : Value::null());
        o.set("height", d.height ? Value::integer(d.height) : Value::null());
        o.set("hz_current", opt_num(d.hz_current));
        o.set("hz_max_same_res", opt_num(d.hz_max_same_res));
        o.set("dynamic_refresh", Value::boolean(d.dynamic_refresh));
        o.set("dpi", opt_int(d.dpi));
        o.set("hdr_supported", opt_bool(d.hdr_supported));
        o.set("hdr_enabled", opt_bool(d.hdr_enabled));
        o.set("vrr_supported", opt_bool(d.vrr_supported));
        o.set("gpu_vendor", str_or_null(d.gpu_vendor));
        displays.push(std::move(o));
    }
    root.set("displays", std::move(displays));

    auto power = Value::object();
    power.set("on_ac", opt_bool(in.power.on_ac));
    power.set("has_battery", opt_bool(in.power.has_battery));
    power.set("battery_saver", opt_bool(in.power.battery_saver));
    power.set("battery_percent", opt_int(in.power.battery_percent));
    power.set("overlay", str_or_null(in.power.overlay));
    power.set("plan", str_or_null(in.power.plan));
    root.set("power", std::move(power));

    root.set("system_disk_used_pct", opt_num(in.system_disk_used_pct));

    auto lang = Value::object();
    lang.set("preference", str_or_null(in.language.preference));
    lang.set("windows_ui", str_or_null(in.language.windows_ui));
    lang.set("resolved", str_or_null(in.language.resolved));
    root.set("language", std::move(lang));

    auto flags = Value::array();
    for (const auto& f : in.flags) {
        auto o = Value::object();
        o.set("name", Value::string(f.name));
        o.set("value", f.value.empty() ? Value::null() : Value::string(f.value.substr(0, 8)));
        o.set("decision", str_or_null(f.decision));
        flags.push(std::move(o));
    }
    root.set("env_flags", std::move(flags));

    root.set("device_verification", capabilities::to_json(in.device));
    root.set("support_matrix", in.support_matrix);
    root.set("restore_failures", in.restore_failures_known ? recovery::to_json(in.restore_failures) : Value::null());
    return root;
}

}  // namespace ef::core::support
