#pragma once
// INV-03: DXGI adaptorleri ve disk/birim eslemesi (salt okuma, normal izin).

#include <optional>
#include <vector>

#include "core/inventory/devices.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

std::vector<core::inventory::GpuAdapter> read_gpus();
std::vector<core::inventory::StorageDevice> read_disks();
std::vector<core::inventory::VolumeInfo> read_volumes();
// Sanal makine icinde mi? Once SMBIOS firmware imzasi (ic ice sanallastirmada guvenilir), sonra CPUID:
// Hyper-V kok bolumu (VBS/HVCI acik fiziksel makine) sanal sayilmaz (0x40000003 EBX bit 0).
std::optional<bool> virtual_machine();

core::json::Value gpus_json(const std::vector<core::inventory::GpuAdapter>& v);
core::json::Value disks_json(const std::vector<core::inventory::StorageDevice>& v);
core::json::Value volumes_json(const std::vector<core::inventory::VolumeInfo>& v);

}  // namespace ef::platform::inventory
