#include "platform/windows/inventory/cpu_mem.h"

#include <windows.h>
#include <intrin.h>

#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include "platform/windows/inventory/field_json.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;
namespace ci = core::inventory;

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    return s.substr(b);
}

std::optional<std::string> raw_smbios() {
    const UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size == 0 || size > (16u << 20)) return std::nullopt;
    std::string buf(size, '\0');
    if (GetSystemFirmwareTable('RSMB', 0, buf.data(), size) != size) return std::nullopt;
    return buf;
}

const ci::SmbiosTable* smbios_cached(std::string& why) {
    static std::optional<ci::SmbiosTable> table;
    static std::string error;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        auto raw = raw_smbios();
        if (!raw) {
            error = "GetSystemFirmwareTable(RSMB) failed (" + std::to_string(GetLastError()) + ")";
        } else if (auto t = ci::parse_smbios(*raw); t.ok()) {
            table = std::move(t.value());
        } else {
            error = "SMBIOS parse: " + t.error().detail;
        }
    }
    why = error;
    return table ? &*table : nullptr;
}

}  // namespace

CpuInfo read_cpu() {
    CpuInfo c;
    int r[4] = {};
    __cpuid(r, 0);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &r[1], 4);
    std::memcpy(vendor + 4, &r[3], 4);
    std::memcpy(vendor + 8, &r[2], 4);
    c.vendor = Field<std::string>::known(vendor, Provenance::Api);
    __cpuid(r, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(r[0]) >= 0x80000004u) {
        char brand[49] = {};
        for (int i = 0; i < 3; ++i) {
            __cpuid(r, static_cast<int>(0x80000002 + i));
            std::memcpy(brand + i * 16, r, 16);
        }
        c.brand = Field<std::string>::known(trim(brand), Provenance::Api);
    } else {
        c.brand = Field<std::string>::unknown("CPUID brand string unsupported");
    }
    DWORD mhz = 0, sz = sizeof(mhz);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz",
                     RRF_RT_REG_DWORD, nullptr, &mhz, &sz) == ERROR_SUCCESS && mhz)
        c.nominal_mhz = Field<std::uint32_t>::known(mhz, Provenance::Registry);
    else
        c.nominal_mhz = Field<std::uint32_t>::unknown("~MHz not available");

    std::vector<ci::CoreRecord> cores;
    std::vector<ci::CacheRecord> caches;
    std::uint32_t packages = 0;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    if (len) {
        auto buf = std::make_unique<unsigned char[]>(len);
        if (GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.get()), &len)) {
            for (DWORD off = 0; off < len;) {
                const auto* e = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.get() + off);
                if (e->Size == 0) break;
                if (e->Relationship == RelationProcessorCore) {
                    ci::CoreRecord cr;
                    cr.efficiency_class = e->Processor.EfficiencyClass;
                    std::uint32_t logical = 0;
                    for (WORD g = 0; g < e->Processor.GroupCount; ++g)
                        logical += static_cast<std::uint32_t>(__popcnt64(e->Processor.GroupMask[g].Mask));
                    cr.logical_count = logical;
                    cr.group = e->Processor.GroupCount ? e->Processor.GroupMask[0].Group : 0;
                    cores.push_back(cr);
                } else if (e->Relationship == RelationProcessorPackage) {
                    ++packages;
                } else if (e->Relationship == RelationCache) {
                    caches.push_back({e->Cache.Level, e->Cache.CacheSize,
                                      e->Cache.Type == CacheUnified || e->Cache.Type == CacheData});
                }
                off += e->Size;
            }
        }
    }
    c.topology = ci::summarize_cpu(packages, cores, caches);
    return c;
}

MemoryInfo read_memory() {
    MemoryInfo m;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        m.usable_mib = Field<std::uint64_t>::known(ms.ullTotalPhys / (1024 * 1024), Provenance::Api);
    else
        m.usable_mib = Field<std::uint64_t>::unknown("GlobalMemoryStatusEx failed");
    ULONGLONG kib = 0;
    if (GetPhysicallyInstalledSystemMemory(&kib))
        m.installed_mib = Field<std::uint64_t>::known(kib / 1024, Provenance::Smbios);
    else
        m.installed_mib = Field<std::uint64_t>::unknown("GetPhysicallyInstalledSystemMemory failed");
    std::string why;
    if (const auto* t = smbios_cached(why)) {
        m.smbios = ci::decode_memory(*t);
    } else {
        m.smbios.total_slots = m.smbios.populated_slots = Field<std::uint32_t>::unknown(why);
        m.smbios.channel_count = Field<std::uint32_t>::unknown("physical channel count is not derivable from DIMM count");
    }
    return m;
}

const ci::SmbiosTable* smbios_table() {
    std::string why;
    return smbios_cached(why);
}

ci::BoardSmbios read_board() {
    std::string why;
    if (const auto* t = smbios_cached(why)) return ci::decode_board(*t);
    ci::BoardSmbios b;
    b.board_vendor = b.board_product = b.bios_vendor = b.bios_version = b.bios_date = Field<std::string>::unknown(why);
    return b;
}

core::json::Value to_json(const CpuInfo& v) {
    auto o = core::json::Value::object();
    o.set("vendor", field_json(v.vendor));
    o.set("brand", field_json(v.brand));
    o.set("nominal_mhz", field_json(v.nominal_mhz));
    const auto& t = v.topology;
    o.set("packages", field_json(t.packages));
    o.set("physical_cores", field_json(t.physical_cores));
    o.set("logical_processors", field_json(t.logical_processors));
    o.set("performance_cores", field_json(t.performance_cores));
    o.set("efficiency_cores", field_json(t.efficiency_cores));
    o.set("hybrid", field_json(t.hybrid));
    o.set("smt", field_json(t.smt));
    o.set("l3_total_kib", field_json(t.l3_total_kib));
    o.set("l3_slices", field_json(t.l3_slices));
    o.set("l3_max_slice_kib", field_json(t.l3_max_slice_kib));
    return o;
}

core::json::Value to_json(const MemoryInfo& v) {
    auto o = core::json::Value::object();
    o.set("usable_mib", field_json(v.usable_mib));
    o.set("installed_mib", field_json(v.installed_mib));
    o.set("total_slots", field_json(v.smbios.total_slots));
    o.set("populated_slots", field_json(v.smbios.populated_slots));
    o.set("channel_count", field_json(v.smbios.channel_count));
    auto arr = core::json::Value::array();
    for (const auto& d : v.smbios.populated) {
        auto e = core::json::Value::object();
        e.set("size_mib", field_json(d.size_mib));
        e.set("type", field_json(d.type));
        e.set("speed_mts", field_json(d.speed_mts));
        e.set("configured_mts", field_json(d.configured_mts));
        e.set("manufacturer", field_json(d.manufacturer));
        e.set("part_number", field_json(d.part_number));
        e.set("locator", field_json(d.locator));
        e.set("bank_locator", field_json(d.bank_locator));
        arr.push(std::move(e));
    }
    o.set("modules", std::move(arr));
    return o;
}

core::json::Value to_json(const ci::BoardSmbios& v) {
    auto o = core::json::Value::object();
    o.set("board_vendor", field_json(v.board_vendor));
    o.set("board_product", field_json(v.board_product));
    o.set("bios_vendor", field_json(v.bios_vendor));
    o.set("bios_version", field_json(v.bios_version));
    o.set("bios_date", field_json(v.bios_date));
    return o;
}

}  // namespace ef::platform::inventory
