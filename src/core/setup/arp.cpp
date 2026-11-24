#include "core/setup/arp.h"

#include <limits>

namespace ef::core::setup {

ArpValues make_arp_values(const InstallLayout& l, std::wstring_view version, std::uint64_t exe_bytes, std::wstring_view yyyymmdd) {
    ArpValues v;
    v.display_version = std::wstring(version);
    v.display_icon = l.exe.empty() ? std::wstring{} : l.exe.native() + L",0";
    v.install_location = l.dir.native();
    v.uninstall_string = uninstall_command(l);
    const std::uint64_t kb = exe_bytes / 1024 + (exe_bytes % 1024 ? 1 : 0);
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint32_t>::max();
    v.estimated_size_kb = static_cast<std::uint32_t>(kb > kMax ? kMax : kb);
    v.install_date = std::wstring(yyyymmdd);
    return v;
}

std::optional<ArpValues> plan_display_version_refresh(const std::optional<ArpValues>& current, const InstallLayout& l,
                                                      std::wstring_view running_version) {
    if (!current || running_version.empty() || l.dir.empty()) return std::nullopt;
    // P9/B4: ayni klasorun farkli yazimi (harf, sonda ayirac, '/') yine bizim kaydimizdir; tam esitlik tazelemeyi atlatirdi.
    if (current->install_location.empty() || !same_path(current->install_location, l.dir)) return std::nullopt;
    if (current->display_version == running_version) return std::nullopt;
    ArpValues v = *current;
    v.display_version = std::wstring(running_version);
    return v;
}

}  // namespace ef::core::setup
