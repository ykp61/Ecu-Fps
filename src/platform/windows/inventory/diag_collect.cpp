#include "platform/windows/inventory/diag_collect.h"

#include <windows.h>

#include <string>
#include <vector>

#include "core/capabilities/registry.h"
#include "core/inventory/power_security.h"
#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/inventory/environment.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/inventory/power_security_win.h"
#include "platform/windows/readiness/display_reader.h"
#include "platform/windows/win_env.h"

namespace ef::platform::inventory {

namespace ci = core::inventory;
namespace cs = core::support;

namespace {
template <typename T>
std::optional<T> opt(const contracts::Field<T>& f) {
    return f.has() ? std::optional<T>(*f.value) : std::nullopt;
}

// Monitor GDI aygit adi (UTF-8 "\\.\DISPLAY1") -> etkin DPI (MDT_EFFECTIVE_DPI). shcore.dll yalniz System32'den,
// calisma aninda yuklenir (yeni statik import yok). Okunamayan ekran listede yer almaz.
struct MonitorDpi {
    std::string device;
    std::uint32_t dpi = 0;
};
using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
struct DpiCtx {
    GetDpiForMonitorFn fn = nullptr;
    std::vector<MonitorDpi>* out = nullptr;
};
BOOL CALLBACK collect_monitor_dpi(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    auto* c = reinterpret_cast<DpiCtx*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    UINT dx = 0, dy = 0;
    if (GetMonitorInfoW(m, &mi) && SUCCEEDED(c->fn(m, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy)) && dx > 0)
        c->out->push_back({to_utf8(mi.szDevice), dx});
    return TRUE;
}
std::vector<MonitorDpi> read_monitor_dpis() {
    std::vector<MonitorDpi> out;
    HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shcore) return out;
    DpiCtx ctx;
    ctx.fn = reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor")));
    ctx.out = &out;
    if (ctx.fn) EnumDisplayMonitors(nullptr, nullptr, collect_monitor_dpi, reinterpret_cast<LPARAM>(&ctx));
    FreeLibrary(shcore);
    return out;
}

std::optional<double> system_disk_used_pct() {
    wchar_t win[MAX_PATH]{};
    const UINT n = GetWindowsDirectoryW(win, MAX_PATH);
    if (n < 3 || n >= MAX_PATH || win[1] != L':') return std::nullopt;
    const wchar_t root[4] = {win[0], L':', L'\\', 0};
    ULARGE_INTEGER avail{}, total{}, free_total{};
    if (!GetDiskFreeSpaceExW(root, &avail, &total, &free_total) || total.QuadPart == 0) return std::nullopt;
    return 100.0 * static_cast<double>(total.QuadPart - free_total.QuadPart) / static_cast<double>(total.QuadPart);
}
}  // namespace

std::optional<std::uint32_t> game_gpu_vendor_id() {
    const auto pref = readiness::read_gpu_preference();
    if (pref.high_perf_luid.empty()) return std::nullopt;
    for (const auto& g : read_gpus())
        if (g.luid.has() && *g.luid.value == pref.high_perf_luid && g.vendor_id.has()) return *g.vendor_id.value;
    return std::nullopt;
}

core::capabilities::DeviceVerification current_device_verification() {
    return core::capabilities::verify_device(current_environment().os_build, game_gpu_vendor_id());
}

void collect_platform_diag(cs::DiagInput& in) {
    const auto env = current_environment();
    in.os_build = env.os_build;

    const auto cpu = read_cpu();
    in.cpu.vendor = cpu.vendor.value.value_or("");
    in.cpu.brand = cpu.brand.value.value_or("");
    const auto& t = cpu.topology;
    in.cpu.packages = opt(t.packages);
    in.cpu.physical_cores = opt(t.physical_cores);
    in.cpu.logical_processors = opt(t.logical_processors);
    in.cpu.performance_cores = opt(t.performance_cores);
    in.cpu.efficiency_cores = opt(t.efficiency_cores);
    in.cpu.smt = opt(t.smt);
    in.cpu.hybrid = opt(t.hybrid);

    const auto pref = readiness::read_gpu_preference();
    std::optional<std::uint32_t> game_vendor;
    for (const auto& g : read_gpus()) {
        cs::DiagAdapter a;
        a.index = g.index;
        a.name = g.name.value.value_or("");
        a.vendor_id = opt(g.vendor_id);
        a.device_id = opt(g.device_id);
        a.luid = g.luid.value.value_or("");
        a.driver_version = g.driver_version.value.value_or("");
        a.dedicated_vram_mib = opt(g.dedicated_vram_mib);
        a.shared_mib = opt(g.shared_mib);
        a.kind = std::string(ci::to_string(g.kind));
        a.high_performance = !pref.high_perf_luid.empty() && a.luid == pref.high_perf_luid;
        if (a.high_performance && !game_vendor) game_vendor = a.vendor_id;
        in.adapters.push_back(std::move(a));
    }

    const auto dpis = read_monitor_dpis();
    for (const auto& d : readiness::read_displays()) {
        cs::DiagDisplay o;
        o.index = d.index;
        o.name = d.name;
        o.primary = d.primary;
        o.width = d.width;
        o.height = d.height;
        o.hz_current = d.hz_current;
        o.hz_max_same_res = d.hz_max_same_res;
        o.dynamic_refresh = d.dynamic_refresh;
        o.hdr_supported = d.hdr_supported;
        o.hdr_enabled = d.hdr_enabled;
        o.vrr_supported = d.vrr_supported;
        o.gpu_vendor = d.gpu_known ? d.gpu_vendor : std::string();
        for (const auto& m : dpis)
            if (!d.gdi_device.empty() && m.device == d.gdi_device) o.dpi = m.dpi;
        in.displays.push_back(std::move(o));
    }

    const auto ps = read_power_security();
    in.power.on_ac = opt(ps.on_ac);
    in.power.has_battery = opt(ps.has_battery);
    in.power.battery_saver = opt(ps.battery_saver);
    in.power.battery_percent = opt(ps.battery_percent);
    if (ps.overlay_guid.has()) in.power.overlay = std::string(ci::to_string(ci::overlay_from_guid(*ps.overlay_guid.value)));
    if (ps.active_plan_guid.has()) in.power.plan = std::string(ci::to_string(ci::plan_from_guid(*ps.active_plan_guid.value)));

    in.system_disk_used_pct = system_disk_used_pct();
    in.device = core::capabilities::verify_device(env.os_build, game_vendor);

    // Bosluk 12: destek matrisi bu ortamin parmak iziyle. Kayit yoksa satirlar durustce "not queried" kalir.
    const auto& features = core::capabilities::embedded_features();
    if (!features.empty()) {
        const core::capabilities::Registry reg(features);
        in.support_matrix = reg.support_matrix(core::capabilities::fingerprint(env));
    }
}

}  // namespace ef::platform::inventory
