#include "core/vendors/dispatch.h"

#include "core/capabilities/vendor_matrix.h"

namespace ef::core::vendors {

using contracts::SupportState;

std::string_view to_string(Vendor v) noexcept {
    switch (v) {
        case Vendor::Nvidia: return "nvidia";
        case Vendor::Amd: return "amd";
        case Vendor::Intel: return "intel";
        case Vendor::None: return "none";
    }
    return "?";
}

Vendor vendor_of(std::uint32_t id) noexcept {
    if (id == inventory::kVendorNvidia) return Vendor::Nvidia;
    if (id == inventory::kVendorAmd) return Vendor::Amd;
    if (id == inventory::kVendorIntel) return Vendor::Intel;
    return Vendor::None;
}

std::wstring_view runtime_dll(Vendor v) noexcept {
    switch (v) {
        case Vendor::Nvidia: return L"nvapi64.dll";
        case Vendor::Amd: return L"amdadlx64.dll";
        case Vendor::Intel: return L"ControlLib.dll";
        case Vendor::None: return L"";
    }
    return L"";
}

std::string_view to_string(RuntimeState s) noexcept {
    switch (s) {
        case RuntimeState::Loaded: return "loaded";
        case RuntimeState::Missing: return "missing";
        case RuntimeState::Untrusted: return "untrusted";
        case RuntimeState::LoadFailed: return "load_failed";
    }
    return "?";
}

std::vector<AdapterRoute> route_adapters(const std::vector<inventory::GpuAdapter>& gpus, const std::vector<RuntimeInfo>& runtimes) {
    std::vector<AdapterRoute> out;
    for (const auto& g : gpus) {
        AdapterRoute r;
        r.adapter_index = g.index;
        r.vendor = vendor_of(g.vendor_id.value.value_or(0));
        if (g.kind == inventory::GpuKind::Software || g.kind == inventory::GpuKind::Remote) {
            r.support = SupportState::Unsupported;
            r.reason = "software or remote adapter";
        } else if (r.vendor == Vendor::None) {
            r.support = SupportState::Unsupported;
            r.reason = "no supported vendor runtime for this adapter";
        } else {
            const RuntimeInfo* rt = nullptr;
            for (const auto& x : runtimes)
                if (x.vendor == r.vendor) rt = &x;
            if (!rt || rt->state == RuntimeState::Missing) {
                r.support = SupportState::Unsupported;
                r.reason = "vendor driver runtime not installed";
            } else if (rt->state == RuntimeState::Untrusted) {
                r.support = SupportState::Denied;
                r.reason = "vendor runtime failed signature or location check";
            } else if (rt->state == RuntimeState::LoadFailed) {
                r.support = SupportState::Unknown;
                r.reason = "vendor runtime could not be loaded: " + rt->detail;
            } else {
                r.support = SupportState::Supported;
                r.reason = "runtime " + rt->file_version;
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}

Vendor adapter_vendor(const std::vector<inventory::GpuAdapter>& gpus, std::uint32_t adapter_index) noexcept {
    for (const auto& g : gpus) {
        if (g.index != adapter_index) continue;
        if (g.kind == inventory::GpuKind::Software || g.kind == inventory::GpuKind::Remote) return Vendor::None;
        return vendor_of(g.vendor_id.value.value_or(0));
    }
    return Vendor::None;
}

bool vendor_driver_write_allowed(const AdapterRoute& route, std::string_view feature_id) noexcept {
    if (route.support != SupportState::Supported) return false;
    const auto row = capabilities::vendor_row(feature_id, route.vendor);
    return row.has_value() && capabilities::is_vendor_driver_write(row->cell);
}

}  // namespace ef::core::vendors
