#include "platform/windows/readiness/storage_reader.h"

#include <windows.h>
#include <winioctl.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/windows/inventory/gpu_storage.h"

namespace ef::platform::readiness {

namespace {

using core::readiness::StorageHealthInfo;
using core::readiness::StorageVolumeInfo;

HANDLE open_query(const std::wstring& path) {
    // 0 erisim hakki: sorgu IOCTL'leri yonetici gerektirmez; yazma yok.
    return CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

std::optional<bool> media_is_ssd(HANDLE h) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceSeekPenaltyProperty;
    q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR sp{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &sp, sizeof(sp), &got, nullptr) ||
        got < sizeof(sp))
        return std::nullopt;
    return !sp.IncursSeekPenalty;  // seek penalty yoksa yari iletken
}

std::optional<bool> smart_failure(HANDLE h) {
    STORAGE_PREDICT_FAILURE pf{};
    DWORD got = 0;
    // Bazi sistemlerde/suruculerde desteklenmez; basarisizsa iddia uretilmez.
    if (!DeviceIoControl(h, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0, &pf, sizeof(pf), &got, nullptr) ||
        got < offsetof(STORAGE_PREDICT_FAILURE, VendorSpecific))
        return std::nullopt;
    return pf.PredictFailure != 0;
}

std::optional<std::uint32_t> device_number(HANDLE h) {
    STORAGE_DEVICE_NUMBER n{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &n, sizeof(n), &got, nullptr) || n.DeviceType != FILE_DEVICE_DISK) return std::nullopt;
    return static_cast<std::uint32_t>(n.DeviceNumber);
}

// NVMe log sayfasi 2 (salt okuma). Basarisizsa nullopt: "saglikli" denmez.
std::optional<core::readiness::NvmeHealthLog> read_nvme_health(std::uint32_t disk) {
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(disk);
    HANDLE h = open_query(path);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    const DWORD header = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters);
    const DWORD total = header + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + 512;
    std::vector<std::uint8_t> buf(total + 64, 0);
    auto* q = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buf.data());
    q->PropertyId = StorageDeviceProtocolSpecificProperty;
    q->QueryType = PropertyStandardQuery;
    auto* p = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(q->AdditionalParameters);
    p->ProtocolType = ProtocolTypeNvme;
    p->DataType = NVMeDataTypeLogPage;
    p->ProtocolDataRequestValue = 2;  // SMART / Health Information
    p->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    p->ProtocolDataLength = 512;
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), total, buf.data(), total, &got, nullptr);
    CloseHandle(h);
    if (!ok || got < sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) return std::nullopt;
    const auto* desc = reinterpret_cast<const STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buf.data());
    if (desc->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) || desc->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) return std::nullopt;
    const auto& data = desc->ProtocolSpecificData;
    const std::size_t base = static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(&data) - buf.data());
    if (data.ProtocolDataLength < 512 || base + data.ProtocolDataOffset + 512 > got) return std::nullopt;  // kisa yanit saglikli sayilmaz
    return core::readiness::parse_nvme_health_log(buf.data() + base + data.ProtocolDataOffset, 512);
}

std::optional<double> free_ratio(const wchar_t* root) {
    ULARGE_INTEGER freeb{}, total{};
    if (!GetDiskFreeSpaceExW(root, &freeb, &total, nullptr) || total.QuadPart == 0) return std::nullopt;
    return static_cast<double>(freeb.QuadPart) / static_cast<double>(total.QuadPart);
}

std::string root_from_dir(const std::string& dir) {
    if (dir.size() < 3 || dir[1] != ':' || (dir[2] != '\\' && dir[2] != '/')) return {};
    if (!std::isalpha(static_cast<unsigned char>(dir[0]))) return {};
    std::string out(1, static_cast<char>(std::toupper(static_cast<unsigned char>(dir[0]))));
    out += ":\\";
    return out;
}

}  // namespace

StorageHealthInfo read_storage_health(const std::string& game_dir, bool include_disks) {
    StorageHealthInfo out;

    wchar_t drives[256] = {};
    const DWORD n = GetLogicalDriveStringsW(255, drives);
    for (const wchar_t* p = drives; n && *p; p += wcslen(p) + 1) {
        if (GetDriveTypeW(p) != DRIVE_FIXED) continue;
        StorageVolumeInfo v;
        const std::wstring wroot(p);
        // GetLogicalDriveStringsW "C:\" bicimi verir; surucu koku ASCII'dir.
        v.root = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])))) + ":\\";
        v.free_ratio = free_ratio(p);
        std::wstring dev = L"\\\\.\\" + wroot.substr(0, 2);  // \\.\C:
        HANDLE h = open_query(dev);
        if (h != INVALID_HANDLE_VALUE) {
            v.ssd = media_is_ssd(h);
            v.smart_failure = smart_failure(h);
            v.disk_number = device_number(h);
            CloseHandle(h);
        }
        out.volumes.push_back(std::move(v));
    }

    using core::inventory::BusKind;
    using core::readiness::DiskHealthSupport;
    for (const auto& d : include_disks ? platform::inventory::read_disks() : std::vector<core::inventory::StorageDevice>{}) {
        core::readiness::DiskHealthInfo di;
        di.disk = d.disk_number;
        di.model = d.model.value.value_or("");
        if (d.bus == BusKind::Nvme) {
            di.nvme = read_nvme_health(d.disk_number);
            di.support = di.nvme ? DiskHealthSupport::Nvme : DiskHealthSupport::NvmeReadFailed;
        } else if (d.bus == BusKind::Sata) {
            di.support = DiskHealthSupport::AtaPredictOnly;
        } else if (d.bus == BusKind::Virtual) {
            di.support = DiskHealthSupport::Unsupported;
        } else {
            di.support = DiskHealthSupport::Unknown;  // USB/RAID/bilinmeyen: kopru destegi yoklanmadi
        }
        out.disks.push_back(std::move(di));
    }

    std::string root = root_from_dir(game_dir);
    if (!root.empty()) {
        for (const auto& v : out.volumes) {
            if (v.root == root) {
                out.game_volume_root = root;
                break;
            }
        }
    }
    return out;
}

}  // namespace ef::platform::readiness
