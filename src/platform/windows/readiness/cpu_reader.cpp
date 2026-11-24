#include "platform/windows/readiness/cpu_reader.h"

#include <cstring>
#include <string>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define EF_HAVE_CPUID 1
#else
#define EF_HAVE_CPUID 0
#endif

#include <windows.h>

#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/metrics/cpu_speed_sampler.h"
#include "platform/windows/inventory/os_user.h"

namespace ef::platform::readiness {

core::readiness::CpuHybridInfo read_cpu_hybrid() {
    core::readiness::CpuHybridInfo out;
    const auto cpu = inventory::read_cpu();
    if (cpu.vendor.has()) {
        const std::string& v = *cpu.vendor.value;
        if (v == "GenuineIntel")
            out.vendor = "intel";
        else if (v == "AuthenticAMD")
            out.vendor = "amd";
        else
            out.vendor = "other";
    }
    const auto& t = cpu.topology;
    if (t.performance_cores.has()) out.performance_cores = *t.performance_cores.value;
    if (t.efficiency_cores.has()) out.efficiency_cores = *t.efficiency_cores.value;
    if (t.logical_processors.has()) out.logical_processors = *t.logical_processors.value;
    if (t.physical_cores.has()) out.physical_cores = *t.physical_cores.value;
    if (t.hybrid.has()) out.hybrid = *t.hybrid.value;

    const auto os = inventory::read_os();
    if (os.build.has()) out.windows_build = *os.build.value;
    return out;
}

core::readiness::CpuSpeedLimitInfo read_cpu_speed_limit() {
    core::readiness::CpuSpeedLimitInfo out;
    metrics::CpuSpeedSampler speed;
    if (speed.open()) {
        Sleep(250);
        out.performance_limit_pct = speed.sample().limit_pct;
    }
    if (const auto ev = diagnostics::count_processor_speed_limit_events_since_boot(); ev.ok) {
        out.firmware_limit_events = ev.count;
        out.events_capped = ev.capped;
    }
    return out;
}

core::readiness::CpuPowerInfo read_cpu_power() {
    core::readiness::CpuPowerInfo out;
#if EF_HAVE_CPUID
    int regs[4]{};
    __cpuid(regs, 0);
    const int max_leaf = regs[0];
    char vendor[13]{};
    std::memcpy(vendor, &regs[1], 4);      // EBX
    std::memcpy(vendor + 4, &regs[3], 4);  // EDX
    std::memcpy(vendor + 8, &regs[2], 4);  // ECX
    const std::string v(vendor, 12);
    out.cpuid_ok = true;
    if (v == "GenuineIntel") {
        out.vendor = "intel";
        __cpuid(regs, 1);
        out.eist = (regs[2] & (1 << 7)) != 0;  // CPUID.01H:ECX[7] Enhanced SpeedStep
        if (max_leaf >= 6) {
            __cpuidex(regs, 6, 0);
            out.speed_shift = (regs[0] & (1 << 7)) != 0;  // CPUID.06H:EAX[7] HWP/Speed Shift
        }
    } else if (v == "AuthenticAMD") {
        out.vendor = "amd";
    } else {
        out.vendor = "other";
    }
#endif
    return out;
}

}  // namespace ef::platform::readiness
