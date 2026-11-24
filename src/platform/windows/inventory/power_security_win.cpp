#include "platform/windows/inventory/power_security_win.h"

#include <windows.h>
#include <powrprof.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "platform/windows/inventory/field_json.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/win_env.h"
#include "platform/windows/wmi.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;
namespace ci = core::inventory;

namespace {
std::string guid_text(const GUID& g) {
    char b[40];
    std::snprintf(b, sizeof b, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", static_cast<unsigned long>(g.Data1),
                  g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                  g.Data4[7]);
    return b;
}
}  // namespace

std::optional<std::string> read_effective_overlay_guid() {
    // Windows 10 1809+; eski surumlerde yok -> calisma aninda yukle.
    using OverlayFn = DWORD(WINAPI*)(GUID*);
    static const auto get_overlay = reinterpret_cast<OverlayFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"powrprof.dll"), "PowerGetEffectiveOverlayScheme")));
    GUID overlay{};
    if (get_overlay && get_overlay(&overlay) == ERROR_SUCCESS) return guid_text(overlay);
    return std::nullopt;
}

ci::PowerSecurity read_power_security() {
    ci::PowerSecurity s;
    const auto p = read_power();
    s.on_ac = p.on_ac;
    s.has_battery = p.has_battery;
    s.battery_saver = p.battery_saver;
    s.battery_percent = p.battery_percent;

    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) == ERROR_SUCCESS && active) {
        s.active_plan_guid = Field<std::string>::known(guid_text(*active), Provenance::Api);
        DWORD size = 0;
        PowerReadFriendlyName(nullptr, active, nullptr, nullptr, nullptr, &size);
        if (size > 0 && size < 4096) {
            std::wstring name(size / sizeof(wchar_t), L'\0');
            if (PowerReadFriendlyName(nullptr, active, nullptr, nullptr, reinterpret_cast<UCHAR*>(name.data()), &size) ==
                ERROR_SUCCESS) {
                name.resize(wcslen(name.c_str()));
                s.active_plan_name = Field<std::string>::known(to_utf8(name), Provenance::Api);
            }
        }
        if (!s.active_plan_name.has()) s.active_plan_name = Field<std::string>::unknown("friendly name unavailable");
        LocalFree(active);
    } else {
        s.active_plan_guid = s.active_plan_name = Field<std::string>::unknown("PowerGetActiveScheme failed");
    }

    if (auto overlay = read_effective_overlay_guid())
        s.overlay_guid = Field<std::string>::known(std::move(*overlay), Provenance::Api);
    else
        s.overlay_guid = Field<std::string>::unknown("power mode overlay unavailable");

    s.oem_mode = Field<std::string>::unknown("no standard Windows API for OEM performance modes");

    auto dg = wmi::query(L"ROOT\\Microsoft\\Windows\\DeviceGuard", L"SELECT * FROM Win32_DeviceGuard",
                         {L"VirtualizationBasedSecurityStatus", L"SecurityServicesRunning"});
    if (dg.ok() && !dg.value().empty()) {
        const auto& row = dg.value().front();
        const auto it = row.find("VirtualizationBasedSecurityStatus");
        if (it != row.end() && it->second.i) {
            const auto v = *it->second.i;
            const auto st = v == 2 ? ci::VbsState::Running : v == 1 ? ci::VbsState::EnabledNotRunning : ci::VbsState::Off;
            s.vbs = Field<std::string>::known(std::string(ci::to_string(st)), Provenance::Wmi);
        } else {
            s.vbs = Field<std::string>::unknown("VBS status not reported");
        }
        const auto run = row.find("SecurityServicesRunning");
        if (run != row.end()) {
            const auto& ints = run->second.ints;
            s.hvci_running = Field<bool>::known(std::find(ints.begin(), ints.end(), 2) != ints.end(), Provenance::Wmi);
            s.credential_guard_running = Field<bool>::known(std::find(ints.begin(), ints.end(), 1) != ints.end(), Provenance::Wmi);
        } else {
            s.hvci_running = s.credential_guard_running = Field<bool>::unknown("services not reported");
        }
    } else {
        const std::string why = dg.ok() ? "Win32_DeviceGuard empty" : dg.error().detail;
        s.vbs = Field<std::string>::unknown(why);
        s.hvci_running = s.credential_guard_running = Field<bool>::unknown(why);
    }
    return s;
}

core::json::Value to_json(const ci::PowerSecurity& v) {
    auto o = core::json::Value::object();
    o.set("active_plan_guid", field_json(v.active_plan_guid));
    o.set("active_plan", core::json::Value::string(
                             v.active_plan_guid.has() ? std::string(ci::to_string(ci::plan_from_guid(*v.active_plan_guid.value))) : "unknown"));
    o.set("active_plan_name", field_json(v.active_plan_name));
    o.set("overlay_guid", field_json(v.overlay_guid));
    o.set("overlay", core::json::Value::string(
                         v.overlay_guid.has() ? std::string(ci::to_string(ci::overlay_from_guid(*v.overlay_guid.value))) : "unknown"));
    o.set("oem_mode", field_json(v.oem_mode));
    o.set("vbs", field_json(v.vbs));
    o.set("hvci_running", field_json(v.hvci_running));
    o.set("credential_guard_running", field_json(v.credential_guard_running));
    auto notes = core::json::Value::array();
    for (auto n : ci::power_security_notes(v)) notes.push(core::json::Value::string(std::string(ci::note_key(n))));
    o.set("notes", std::move(notes));
    return o;
}

}  // namespace ef::platform::inventory
