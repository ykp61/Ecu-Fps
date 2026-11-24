#include "platform/windows/inventory/gpu_storage.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <intrin.h>
#include <winioctl.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/inventory/dxcore_probe.h"
#include "platform/windows/inventory/field_json.h"
#include "platform/windows/win_env.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;
using Microsoft::WRL::ComPtr;
namespace ci = core::inventory;

namespace {

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

std::optional<bool> d3d12_uma(IDXGIAdapter1* adapter) {
    static HMODULE mod = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod) return std::nullopt;
    static auto create = reinterpret_cast<D3D12CreateDeviceFn>(reinterpret_cast<void*>(GetProcAddress(mod, "D3D12CreateDevice")));
    if (!create) return std::nullopt;
    ComPtr<ID3D12Device> dev;
    if (FAILED(create(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) return std::nullopt;
    D3D12_FEATURE_DATA_ARCHITECTURE arch{};
    if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch)))) return std::nullopt;
    return arch.UMA != FALSE;
}

std::string version_string(LARGE_INTEGER v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", HIWORD(v.HighPart), LOWORD(v.HighPart), HIWORD(v.LowPart),
                  LOWORD(v.LowPart));
    return buf;
}

HANDLE open_device(const std::wstring& path) {
    // 0 erisim hakki: sorgu IOCTL'leri yonetici gerektirmez.
    return CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

ci::BusKind bus_kind(STORAGE_BUS_TYPE t) {
    switch (t) {
        case BusTypeNvme: return ci::BusKind::Nvme;
        case BusTypeSata:
        case BusTypeAta: return ci::BusKind::Sata;
        case BusTypeSas: return ci::BusKind::Sas;
        case BusTypeUsb: return ci::BusKind::Usb;
        case BusTypeRAID: return ci::BusKind::Raid;
        case BusTypeScsi: return ci::BusKind::Scsi;
        case BusTypeSd:
        case BusTypeMmc: return ci::BusKind::Sd;
        case BusTypeVirtual:
        case BusTypeFileBackedVirtual: return ci::BusKind::Virtual;
        default: return ci::BusKind::Other;
    }
}

std::string trim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    return s.substr(b);
}

}  // namespace

std::optional<bool> virtual_machine() {
    // 1) Firmware imzasi (Hyper-V/Azure, VMware, QEMU...): ic ice sanallastirmada CPUID yaniltir.
    if (const auto* t = smbios_table(); t && ci::smbios_indicates_vm(*t)) return true;
    int r[4] = {};
    __cpuid(r, 1);
    const bool hv = ((static_cast<unsigned>(r[2]) >> 31) & 1u) != 0;
    if (!hv) return false;
    __cpuid(r, 0x40000000);
    char vendor[13] = {};
    std::memcpy(vendor, &r[1], 4);
    std::memcpy(vendor + 4, &r[2], 4);
    std::memcpy(vendor + 8, &r[3], 4);
    if (std::string(vendor) == "Microsoft Hv" && static_cast<unsigned>(r[0]) >= 0x40000003u) {
        __cpuid(r, 0x40000003);
        if (r[1] & 1) return false;  // kok bolum: fiziksel makine (VBS/Hyper-V ana bilgisayar)
        return true;
    }
    return true;  // baska hipervizor (VMware, KVM, VirtualBox)
}

std::vector<ci::GpuAdapter> read_gpus() {
    std::vector<ci::GpuAdapter> out;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return out;
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d))) continue;
        ci::GpuAdapter g;
        g.index = i;
        g.name = Field<std::string>::known(to_utf8(d.Description), Provenance::Api);
        g.vendor_id = Field<std::uint32_t>::known(d.VendorId, Provenance::Api);
        g.device_id = Field<std::uint32_t>::known(d.DeviceId, Provenance::Api);
        g.subsys_id = Field<std::uint32_t>::known(d.SubSysId, Provenance::Api);
        g.revision = Field<std::uint32_t>::known(d.Revision, Provenance::Api);
        char luid[40];
        std::snprintf(luid, sizeof luid, "%08lx:%08lx", static_cast<unsigned long>(d.AdapterLuid.HighPart),
                      static_cast<unsigned long>(d.AdapterLuid.LowPart));
        g.luid = Field<std::string>::known(luid, Provenance::Api);
        g.dedicated_vram_mib = Field<std::uint64_t>::known(d.DedicatedVideoMemory / (1024 * 1024), Provenance::Api);
        g.shared_mib = Field<std::uint64_t>::known(d.SharedSystemMemory / (1024 * 1024), Provenance::Api);
        LARGE_INTEGER umd{};
        if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
            g.driver_version = Field<std::string>::known(version_string(umd), Provenance::Api);
        else
            g.driver_version = Field<std::string>::unknown("UMD version not reported");
        const bool software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const bool remote = (d.Flags & DXGI_ADAPTER_FLAG_REMOTE) != 0;
        std::optional<bool> uma;
        // UI-05: once DXCore (cihaz olusturmaz, GPU surucusunu surece yuklemez); yoksa D3D12 mimari sorgusu.
        if (!software && !remote) {
            uma = dxcore_is_integrated(d.AdapterLuid);
            if (!uma) uma = d3d12_uma(a.Get());
        }
        g.uma = uma ? Field<bool>::known(*uma, Provenance::Api) : Field<bool>::unknown("DXCore/D3D12 architecture query unavailable");
        g.kind = ci::classify_gpu({d.VendorId, d.DeviceId, software, remote, uma, d.DedicatedVideoMemory / (1024 * 1024)},
                                  g.kind_source);
        out.push_back(std::move(g));
        a.Reset();
    }
    return out;
}

std::vector<ci::StorageDevice> read_disks() {
    std::vector<ci::StorageDevice> out;
    for (DWORD n = 0; n < 64; ++n) {
        HANDLE h = open_device(L"\\\\.\\PhysicalDrive" + std::to_wstring(n));
        if (h == INVALID_HANDLE_VALUE) continue;
        ci::StorageDevice d;
        d.disk_number = n;
        STORAGE_PROPERTY_QUERY q{};
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        alignas(8) unsigned char buf[4096] = {};
        DWORD got = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &got, nullptr) &&
            got >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buf);
            d.bus = bus_kind(desc->BusType);
            d.removable = Field<bool>::known(desc->RemovableMedia != 0, Provenance::Api);
            std::string model;
            if (desc->VendorIdOffset && desc->VendorIdOffset < got)
                model = trim(reinterpret_cast<const char*>(buf + desc->VendorIdOffset));
            if (desc->ProductIdOffset && desc->ProductIdOffset < got) {
                const std::string p = trim(reinterpret_cast<const char*>(buf + desc->ProductIdOffset));
                model = model.empty() ? p : model + " " + p;
            }
            d.model = model.empty() ? Field<std::string>::unknown("model not reported")
                                    : Field<std::string>::known(model, Provenance::Api);
            // Not: SerialNumberOffset okunmaz.
        } else {
            d.model = Field<std::string>::unknown("StorageDeviceProperty failed");
            d.removable = Field<bool>::unknown("StorageDeviceProperty failed");
        }
        q.PropertyId = StorageDeviceSeekPenaltyProperty;
        DEVICE_SEEK_PENALTY_DESCRIPTOR sp{};
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &sp, sizeof(sp), &got, nullptr) &&
            got >= sizeof(sp))
            d.media = sp.IncursSeekPenalty ? ci::MediaKind::Hdd : ci::MediaKind::Ssd;
        else if (d.bus == ci::BusKind::Nvme)
            d.media = ci::MediaKind::Ssd;  // NVMe her zaman yari iletken
        // Boyut: geometri sorgusu 0 erisim hakkiyla calisir (READ_CAPACITY/LENGTH_INFO okuma ister).
        alignas(8) unsigned char geo[256] = {};
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, geo, sizeof(geo), &got, nullptr) &&
            got >= offsetof(DISK_GEOMETRY_EX, Data)) {
            const auto* g = reinterpret_cast<const DISK_GEOMETRY_EX*>(geo);
            d.size_gib = Field<std::uint64_t>::known(static_cast<std::uint64_t>(g->DiskSize.QuadPart) >> 30, Provenance::Api);
        } else {
            d.size_gib = Field<std::uint64_t>::unknown("disk geometry unavailable (" + std::to_string(GetLastError()) + ")");
        }
        CloseHandle(h);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<ci::VolumeInfo> read_volumes() {
    std::vector<ci::VolumeInfo> out;
    wchar_t drives[256];
    const DWORD n = GetLogicalDriveStringsW(255, drives);
    for (const wchar_t* p = drives; n && *p; p += wcslen(p) + 1) {
        const UINT type = GetDriveTypeW(p);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        ci::VolumeInfo v;
        v.root = to_utf8(p);
        v.removable = Field<bool>::known(type == DRIVE_REMOVABLE, Provenance::Api);
        wchar_t fs[64] = {};
        if (GetVolumeInformationW(p, nullptr, 0, nullptr, nullptr, nullptr, fs, 64))
            v.filesystem = Field<std::string>::known(to_utf8(fs), Provenance::Api);
        else
            v.filesystem = Field<std::string>::unknown("volume not ready");
        ULARGE_INTEGER freeb{}, total{};
        if (GetDiskFreeSpaceExW(p, &freeb, &total, nullptr)) {
            v.total_gib = Field<std::uint64_t>::known(total.QuadPart >> 30, Provenance::Api);
            v.free_gib = Field<std::uint64_t>::known(freeb.QuadPart >> 30, Provenance::Api);
        } else {
            v.total_gib = v.free_gib = Field<std::uint64_t>::unknown("GetDiskFreeSpaceEx failed");
        }
        std::wstring dev = L"\\\\.\\" + std::wstring(p, 2);  // \\.\C:
        HANDLE h = open_device(dev);
        if (h != INVALID_HANDLE_VALUE) {
            STORAGE_DEVICE_NUMBER sdn{};
            DWORD got = 0;
            if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &got, nullptr))
                v.disk_number = Field<std::uint32_t>::known(sdn.DeviceNumber, Provenance::Api);
            else
                v.disk_number = Field<std::uint32_t>::unknown("volume spans disks or device number unavailable");
            CloseHandle(h);
        } else {
            v.disk_number = Field<std::uint32_t>::unknown("volume device open failed");
        }
        out.push_back(std::move(v));
    }
    return out;
}

core::json::Value gpus_json(const std::vector<ci::GpuAdapter>& v) {
    auto arr = core::json::Value::array();
    for (const auto& g : v) {
        auto o = core::json::Value::object();
        o.set("index", core::json::Value::integer(g.index));
        o.set("name", field_json(g.name));
        o.set("vendor", core::json::Value::string(std::string(ci::vendor_name(g.vendor_id.value.value_or(0)))));
        o.set("vendor_id", field_json(g.vendor_id));
        o.set("device_id", field_json(g.device_id));
        o.set("luid", field_json(g.luid));
        o.set("dedicated_vram_mib", field_json(g.dedicated_vram_mib));
        o.set("shared_mib", field_json(g.shared_mib));
        o.set("driver_version", field_json(g.driver_version));
        o.set("uma", field_json(g.uma));
        o.set("kind", core::json::Value::string(std::string(ci::to_string(g.kind))));
        o.set("kind_source", core::json::Value::string(std::string(contracts::to_string(g.kind_source))));
        arr.push(std::move(o));
    }
    return arr;
}

core::json::Value disks_json(const std::vector<ci::StorageDevice>& v) {
    auto arr = core::json::Value::array();
    for (const auto& d : v) {
        auto o = core::json::Value::object();
        o.set("disk_number", core::json::Value::integer(d.disk_number));
        o.set("model", field_json(d.model));
        o.set("bus", core::json::Value::string(std::string(ci::to_string(d.bus))));
        o.set("media", core::json::Value::string(std::string(ci::to_string(d.media))));
        o.set("removable", field_json(d.removable));
        o.set("size_gib", field_json(d.size_gib));
        arr.push(std::move(o));
    }
    return arr;
}

core::json::Value volumes_json(const std::vector<ci::VolumeInfo>& v) {
    auto arr = core::json::Value::array();
    for (const auto& x : v) {
        auto o = core::json::Value::object();
        o.set("root", core::json::Value::string(x.root));
        o.set("disk_number", field_json(x.disk_number));
        o.set("filesystem", field_json(x.filesystem));
        o.set("total_gib", field_json(x.total_gib));
        o.set("free_gib", field_json(x.free_gib));
        o.set("removable", field_json(x.removable));
        arr.push(std::move(o));
    }
    return arr;
}

}  // namespace ef::platform::inventory
