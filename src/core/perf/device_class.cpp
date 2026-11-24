#include "core/perf/device_class.h"

#include "core/perf/controller.h"

namespace ef::core::perf {

DeviceClass classify(const DeviceFacts& f) noexcept {
    DeviceClass c;
    c.laptop = f.laptop.value_or(false);
    c.low_end = (f.ram_bytes && *f.ram_bytes <= kLowEndRamBytes) || (f.logical_cores && *f.logical_cores <= kLowEndCores);
    return c;
}

bool default_on_for(const PlanGroup& g, const DeviceClass& dc) noexcept {
    if (dc.low_end && g.key == "plan.background") return true;
    return g.default_on;
}

}  // namespace ef::core::perf
