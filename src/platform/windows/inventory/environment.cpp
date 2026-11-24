#include "platform/windows/inventory/environment.h"

#include <cstdio>

#include "core/product_info.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/inventory/os_user.h"

namespace ef::platform::inventory {

core::capabilities::Environment current_environment() {
    core::capabilities::Environment e;
    const auto os = read_os();
    e.os_build = (os.build.has() ? std::to_string(*os.build.value) : std::string("?")) + "." +
                 (os.ubr.has() ? std::to_string(*os.ubr.value) : std::string("?"));
    for (const auto& g : read_gpus()) {
        if (g.kind == core::inventory::GpuKind::Software) continue;
        char id[16];
        std::snprintf(id, sizeof id, "%04X:%04X", g.vendor_id.value.value_or(0), g.device_id.value.value_or(0));
        e.gpu_drivers.push_back(std::string(id) + "=" + g.driver_version.value.value_or("?"));
    }
    const auto v = core::product_version();
    e.app_version = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    return e;
}

}  // namespace ef::platform::inventory
