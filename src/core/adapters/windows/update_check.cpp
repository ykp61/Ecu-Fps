#include "core/adapters/windows/update_check.h"

#include <array>
#include <charconv>
#include <cstdio>

namespace ef::core::adapters::windows {

GpuVendor vendor_from_pci(std::uint32_t vendor_id) noexcept {
    switch (vendor_id) {
        case 0x10DE: return GpuVendor::Nvidia;
        case 0x1002:
        case 0x1022: return GpuVendor::Amd;
        case 0x8086: return GpuVendor::Intel;
        default: return GpuVendor::Other;
    }
}

namespace {
std::optional<unsigned> parse_part(std::string_view s) {
    unsigned v = 0;
    if (s.empty() || s.size() > 6) return std::nullopt;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}
}  // namespace

std::optional<std::string> nvidia_display_version(std::string_view w) {
    std::array<unsigned, 4> p{};
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const auto dot = w.find('.', start);
        const bool last = i == 3;
        if (last != (dot == std::string_view::npos)) return std::nullopt;
        const auto part = parse_part(w.substr(start, last ? std::string_view::npos : dot - start));
        if (!part) return std::nullopt;
        p[static_cast<std::size_t>(i)] = *part;
        start = dot + 1;
    }
    if (p[3] > 9999) return std::nullopt;
    // Son 5 hane: (c mod 10) ve d (4 hane) -> "XYZ.AB"
    const unsigned v = (p[2] % 10) * 10000 + p[3];
    char buf[16];
    const auto n = std::snprintf(buf, sizeof buf, "%u.%02u", v / 100, v % 100);
    if (n <= 0) return std::nullopt;
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string_view official_driver_url(GpuVendor v) noexcept {
    switch (v) {
        case GpuVendor::Nvidia: return "https://www.nvidia.com/Download/index.aspx";
        case GpuVendor::Amd: return "https://www.amd.com/en/support/download/drivers.html";
        case GpuVendor::Intel: return "https://www.intel.com/content/www/us/en/download-center/home.html";
        case GpuVendor::Other: return "";
    }
    return "";
}

bool is_allowed_web_url(std::string_view url) noexcept {
    if (url.empty()) return false;
    for (auto v : {GpuVendor::Nvidia, GpuVendor::Amd, GpuVendor::Intel})
        if (official_driver_url(v) == url) return true;
    return false;
}

std::string_view to_string(UpdateCheckCode c) noexcept {
    switch (c) {
        case UpdateCheckCode::RebootPending: return "reboot_pending";
        case UpdateCheckCode::DriverOld: return "driver_old";
        case UpdateCheckCode::DriverDateUnknown: return "driver_date_unknown";
    }
    return "unknown";
}

std::string_view text_key(UpdateCheckCode c) noexcept {
    switch (c) {
        case UpdateCheckCode::RebootPending: return "update.reboot_pending";
        case UpdateCheckCode::DriverOld: return "update.driver_old";
        case UpdateCheckCode::DriverDateUnknown: return "update.driver_date_unknown";
    }
    return "";
}

UpdateCheckReport evaluate_update_check(const std::vector<DriverInfo>& drivers, const UpdateStatus& status,
                                        std::int64_t today) {
    UpdateCheckReport r;
    r.drivers = drivers;
    if (status.reboot_pending.has() && *status.reboot_pending.value)
        r.findings.push_back({UpdateCheckCode::RebootPending, "WUA RebootRequired=true", ""});
    for (const auto& d : drivers) {
        const auto vendor = vendor_from_pci(d.vendor_id);
        if (vendor == GpuVendor::Other) continue;  // yazilim/uzak bagdastirici vb.
        const std::string url(official_driver_url(vendor));
        if (!d.date_unix_days.has()) {
            r.findings.push_back({UpdateCheckCode::DriverDateUnknown, d.device_name + ": driver date unavailable", url});
            continue;
        }
        const auto age = today - *d.date_unix_days.value;
        if (today > 0 && age > kDriverOldDays) {
            std::string ev = d.device_name + ": driver " + d.version.value.value_or("?") + " is " +
                             std::to_string(age) + " days old";
            r.findings.push_back({UpdateCheckCode::DriverOld, std::move(ev), url});
        }
    }
    return r;
}

}  // namespace ef::core::adapters::windows
