#pragma once
// INV-02: CPU, RAM ve anakart bilgisi (salt okuma, normal izin). Firmware yazma yetkisi degildir.

#include <cstdint>
#include <string>

#include "contracts/inventory.h"
#include "core/inventory/cpu_topology.h"
#include "core/inventory/smbios.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

struct CpuInfo {
    contracts::Field<std::string> vendor, brand;
    contracts::Field<std::uint32_t> nominal_mhz;
    core::inventory::CpuTopology topology;
};

struct MemoryInfo {
    contracts::Field<std::uint64_t> usable_mib;     // GlobalMemoryStatusEx
    contracts::Field<std::uint64_t> installed_mib;  // GetPhysicallyInstalledSystemMemory
    core::inventory::MemorySmbios smbios;
};

CpuInfo read_cpu();
// Islem omru boyunca onbellekli SMBIOS tablosu (okunamadiysa nullptr).
const core::inventory::SmbiosTable* smbios_table();
MemoryInfo read_memory();
core::inventory::BoardSmbios read_board();

core::json::Value to_json(const CpuInfo& v);
core::json::Value to_json(const MemoryInfo& v);
core::json::Value to_json(const core::inventory::BoardSmbios& v);

}  // namespace ef::platform::inventory
