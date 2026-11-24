#include "core/inventory/devices.h"

#include <cctype>

namespace ef::core::inventory {

using contracts::Provenance;

std::string_view to_string(GpuKind k) noexcept {
    switch (k) {
        case GpuKind::Discrete: return "discrete";
        case GpuKind::Integrated: return "integrated";
        case GpuKind::Software: return "software";
        case GpuKind::Remote: return "remote";
        case GpuKind::Unknown: return "unknown";
    }
    return "?";
}

std::string_view vendor_name(std::uint32_t v) noexcept {
    switch (v) {
        case kVendorNvidia: return "NVIDIA";
        case kVendorAmd: return "AMD";
        case kVendorIntel: return "Intel";
        case kVendorMicrosoft: return "Microsoft";
        default: return "other";
    }
}

GpuKind classify_gpu(const GpuClassifyInput& in, Provenance& source) noexcept {
    if (in.remote_flag) { source = Provenance::Api; return GpuKind::Remote; }
    if (in.software_flag || (in.vendor_id == kVendorMicrosoft && in.device_id == 0x8C)) {
        source = Provenance::Api;
        return GpuKind::Software;  // WARP / Basic Render Driver
    }
    if (in.uma.has_value()) {
        source = Provenance::Api;
        return *in.uma ? GpuKind::Integrated : GpuKind::Discrete;
    }
    // Sezgisel: AMD/Intel ve <=1 GiB ayrilmis bellek -> entegre; NVIDIA masaustu -> harici.
    source = Provenance::Derived;
    if (in.vendor_id == kVendorNvidia) return GpuKind::Discrete;
    if ((in.vendor_id == kVendorAmd || in.vendor_id == kVendorIntel) && in.dedicated_mib <= 1024) return GpuKind::Integrated;
    if (in.dedicated_mib >= 2048) return GpuKind::Discrete;
    source = Provenance::None;
    return GpuKind::Unknown;
}

BenchEligibility hardware_bench_eligible(const GpuAdapter& gpu, std::optional<bool> remote_session,
                                         std::optional<bool> hypervisor_present) {
    if (gpu.kind == GpuKind::Software) return {false, "software rasterizer (WARP) is not a physical GPU"};
    if (gpu.kind == GpuKind::Remote) return {false, "remote display adapter"};
    if (gpu.kind == GpuKind::Unknown) return {false, "adapter type unknown"};
    if (!remote_session.has_value()) return {false, "session type unknown"};
    if (*remote_session) return {false, "remote desktop session"};
    if (hypervisor_present.value_or(false))
        return {false, "running under a hypervisor; results are not physical hardware scores"};
    return {true, {}};
}

std::string_view to_string(BusKind b) noexcept {
    switch (b) {
        case BusKind::Nvme: return "nvme";
        case BusKind::Sata: return "sata";
        case BusKind::Sas: return "sas";
        case BusKind::Usb: return "usb";
        case BusKind::Raid: return "raid";
        case BusKind::Scsi: return "scsi";
        case BusKind::Sd: return "sd";
        case BusKind::Virtual: return "virtual";
        case BusKind::Other: return "other";
        case BusKind::Unknown: return "unknown";
    }
    return "?";
}

std::string_view to_string(MediaKind m) noexcept {
    switch (m) {
        case MediaKind::Ssd: return "ssd";
        case MediaKind::Hdd: return "hdd";
        case MediaKind::Unknown: return "unknown";
    }
    return "?";
}

const VolumeInfo* volume_for_path(const std::vector<VolumeInfo>& vols, std::string_view path) {
    // En uzun eslesen kok (baglama noktasi destegi); harf buyuk/kucuk duyarsiz.
    const VolumeInfo* best = nullptr;
    std::size_t best_len = 0;
    for (const auto& v : vols) {
        const auto& r = v.root;
        if (r.empty() || r.size() > path.size()) continue;
        bool match = true;
        for (std::size_t i = 0; i < r.size(); ++i) {
            char a = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
            char b = static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
            if (a == '/') a = '\\';
            if (b == '/') b = '\\';
            if (a != b) { match = false; break; }
        }
        if (match && r.size() > best_len) { best = &v; best_len = r.size(); }
    }
    return best;
}

const StorageDevice* disk_for_volume(const std::vector<StorageDevice>& disks, const VolumeInfo& v) {
    if (!v.disk_number.has()) return nullptr;
    for (const auto& d : disks)
        if (d.disk_number == *v.disk_number.value) return &d;
    return nullptr;
}

}  // namespace ef::core::inventory
