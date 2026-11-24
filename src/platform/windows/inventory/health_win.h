#pragma once
// INV-06 platform: GPU PCIe link (SetupAPI), ReBAR (kaynak penceresi), saglik girdisi toplama.

#include <optional>
#include <string>

#include "core/inventory/health.h"
#include "core/serialization/json.h"

namespace ef::platform::inventory {

struct GpuBusInfo {
    std::string instance_id_hint;  // VEN_/DEV_ (bilgi; seri yok)
    core::inventory::PcieInput pcie;
    std::optional<std::uint64_t> largest_mem_window_mib;  // ReBAR tahmini icin
};

// vendor/device'a gore ilk eslesen ekran aygitinin PCIe bilgisi.
std::optional<GpuBusInfo> read_gpu_bus(std::uint32_t vendor_id, std::uint32_t device_id);

// Spec 2026-11-12 C2: yalniz link (ReBAR / kaynak taramasi yok; yuk altinda tekrar okumak icin hafif yol) ve ust
// koprunun (yuvanin) azami genislik / hizi. Ust kopru okunamadiysa yuva alanlari bos kalir.
struct GpuLinkReading {
    core::inventory::PcieInput pcie;
    std::optional<std::uint32_t> slot_max_width, slot_max_gen;
};
std::optional<GpuLinkReading> read_gpu_link(std::uint32_t vendor_id, std::uint32_t device_id);

core::inventory::HealthInput collect_health_input();
core::json::Value health_json();

}  // namespace ef::platform::inventory
