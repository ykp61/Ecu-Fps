#pragma once
// GPU ve depolama kimligi modelleri + saf siniflandirma kurallari (INV-03).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/inventory.h"

namespace ef::core::inventory {

enum class GpuKind { Discrete, Integrated, Software, Remote, Unknown };
std::string_view to_string(GpuKind k) noexcept;

inline constexpr std::uint32_t kVendorNvidia = 0x10DE;
inline constexpr std::uint32_t kVendorAmd = 0x1002;
inline constexpr std::uint32_t kVendorIntel = 0x8086;
inline constexpr std::uint32_t kVendorMicrosoft = 0x1414;
std::string_view vendor_name(std::uint32_t vendor_id) noexcept;

struct GpuAdapter {
    std::uint32_t index = 0;  // DXGI siralama indeksi (0 = varsayilan cikti)
    contracts::Field<std::string> name;
    contracts::Field<std::uint32_t> vendor_id, device_id, subsys_id, revision;
    contracts::Field<std::string> luid;  // "hi:lo" — oturum ici kimlik
    contracts::Field<std::uint64_t> dedicated_vram_mib, shared_mib;
    contracts::Field<std::string> driver_version;  // UMD a.b.c.d
    contracts::Field<bool> uma;                    // D3D12 mimari bilgisi
    GpuKind kind = GpuKind::Unknown;
    contracts::Provenance kind_source = contracts::Provenance::None;
};

struct GpuClassifyInput {
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    bool software_flag = false;
    bool remote_flag = false;
    std::optional<bool> uma;          // D3D12 okunabildiyse
    std::uint64_t dedicated_mib = 0;
};
// Oncelik: yazilim/uzak bayragi > D3D12 UMA > sezgisel (provenance Derived).
GpuKind classify_gpu(const GpuClassifyInput& in, contracts::Provenance& source) noexcept;

// Donanim benchmarki icin uygun mu? Yazilim/uzak adaptor, uzak oturum veya sanal makinede hayir.
struct BenchEligibility {
    bool eligible = false;
    std::string reason;
};
BenchEligibility hardware_bench_eligible(const GpuAdapter& gpu, std::optional<bool> remote_session,
                                         std::optional<bool> hypervisor_present);

// Disk ve birim
enum class BusKind { Nvme, Sata, Sas, Usb, Raid, Scsi, Sd, Virtual, Other, Unknown };
std::string_view to_string(BusKind b) noexcept;
enum class MediaKind { Ssd, Hdd, Unknown };
std::string_view to_string(MediaKind m) noexcept;

struct StorageDevice {
    std::uint32_t disk_number = 0;
    contracts::Field<std::string> model;
    BusKind bus = BusKind::Unknown;
    MediaKind media = MediaKind::Unknown;
    contracts::Field<bool> removable;
    contracts::Field<std::uint64_t> size_gib;
};

struct VolumeInfo {
    std::string root;  // "C:\"
    contracts::Field<std::uint32_t> disk_number;  // birden fazla diske yayilan birimde unknown
    contracts::Field<std::string> filesystem;
    contracts::Field<std::uint64_t> total_gib, free_gib;
    contracts::Field<bool> removable;
};

// Bir yolun bulundugu birim ve disk (ornek: EXE USB'de, oyun NVMe'de). Bulunamazsa nullopt.
const VolumeInfo* volume_for_path(const std::vector<VolumeInfo>& vols, std::string_view path);
const StorageDevice* disk_for_volume(const std::vector<StorageDevice>& disks, const VolumeInfo& v);

}  // namespace ef::core::inventory
