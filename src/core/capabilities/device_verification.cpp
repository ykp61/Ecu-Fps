#include "core/capabilities/device_verification.h"

#include <array>

namespace ef::core::capabilities {

namespace {
// Gercek cihaz kaniti olan kombinasyonlar. Yeni satir yalniz o cihazda kosulmus kanitla eklenir.
constexpr std::array<VerifiedCombination, 1> kVerified{{
    {26300, 0x10DE, "artifacts/environment.md: Windows 11 Pro 26H2 (26300) + NVIDIA GeForce RTX 5080"},
}};
}  // namespace

std::span<const VerifiedCombination> verified_combinations() noexcept { return kVerified; }

std::optional<std::uint32_t> os_build_major(std::string_view os_build) noexcept {
    std::uint32_t v = 0;
    std::size_t digits = 0;
    for (const char c : os_build) {
        if (c < '0' || c > '9') break;
        if (v > 100000000u) return std::nullopt;
        v = v * 10 + static_cast<std::uint32_t>(c - '0');
        ++digits;
    }
    if (digits == 0 || v == 0) return std::nullopt;
    return v;
}

DeviceVerification verify_device(std::string_view os_build, std::optional<std::uint32_t> gpu_vendor_id) {
    DeviceVerification d;
    d.os_build = os_build_major(os_build);
    if (gpu_vendor_id && *gpu_vendor_id != 0) d.gpu_vendor_id = gpu_vendor_id;
    d.os_known = d.os_build.has_value();
    d.gpu_known = d.gpu_vendor_id.has_value();
    for (const auto& c : kVerified) {
        const bool os_ok = d.os_known && *d.os_build == c.os_build;
        const bool gpu_ok = d.gpu_known && *d.gpu_vendor_id == c.gpu_vendor_id;
        d.os_verified = d.os_verified || os_ok;
        d.gpu_verified = d.gpu_verified || gpu_ok;
        d.verified = d.verified || (os_ok && gpu_ok);
    }
    return d;
}

json::Value to_json(const DeviceVerification& v) {
    auto o = json::Value::object();
    o.set("verified", json::Value::boolean(v.verified));
    o.set("os_build", v.os_build ? json::Value::integer(*v.os_build) : json::Value::null());
    o.set("os_verified", json::Value::boolean(v.os_verified));
    o.set("gpu_vendor_id", v.gpu_vendor_id ? json::Value::integer(*v.gpu_vendor_id) : json::Value::null());
    o.set("gpu_verified", json::Value::boolean(v.gpu_verified));
    auto list = json::Value::array();
    for (const auto& c : verified_combinations()) {
        auto r = json::Value::object();
        r.set("os_build", json::Value::integer(c.os_build));
        r.set("gpu_vendor_id", json::Value::integer(c.gpu_vendor_id));
        list.push(std::move(r));
    }
    o.set("verified_combinations", std::move(list));
    return o;
}

}  // namespace ef::core::capabilities
