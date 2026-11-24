#include "platform/windows/adapters/win_power.h"

#include <windows.h>
#include <powrprof.h>

#include <cstdio>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Status;

namespace {
std::string guid_text(const GUID& g) {
    char b[40];
    std::snprintf(b, sizeof b, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", static_cast<unsigned long>(g.Data1), g.Data2,
                  g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}

bool parse_guid(const std::string& s, GUID& g) {
    if (!core::adapters::windows::is_guid_text(s)) return false;
    unsigned long d1 = 0;
    unsigned int d2 = 0, d3 = 0, b[8] = {};
    if (sscanf_s(s.c_str(), "%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6],
                    &b[7]) != 11)
        return false;
    g.Data1 = d1;
    g.Data2 = static_cast<unsigned short>(d2);
    g.Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; ++i) g.Data4[i] = static_cast<unsigned char>(b[i]);
    return true;
}

ErrorCode map(DWORD e) {
    if (e == ERROR_ACCESS_DENIED) return ErrorCode::Denied;
    if (e == ERROR_CALL_NOT_IMPLEMENTED || e == ERROR_NOT_SUPPORTED) return ErrorCode::Unsupported;
    return ErrorCode::IoError;
}

using GetModeFn = DWORD(WINAPI*)(GUID*);
using SetModeFn = DWORD(WINAPI*)(const GUID*);
template <typename F>
F powrprof_fn(const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"powrprof.dll"), name)));
}
}  // namespace

contracts::Result<std::string> WinPowerApi::get_ac_mode() {
    using R = contracts::Result<std::string>;
    static const auto fn = powrprof_fn<GetModeFn>("PowerGetUserConfiguredACPowerMode");
    if (!fn) return R::fail(ErrorCode::Unsupported, "power modes require Windows 11");
    GUID g{};
    const DWORD e = fn(&g);
    if (e != ERROR_SUCCESS) return R::fail(map(e), "PowerGetUserConfiguredACPowerMode (" + std::to_string(e) + ")");
    return guid_text(g);
}

Status WinPowerApi::set_ac_mode(const std::string& s) {
    static const auto fn = powrprof_fn<SetModeFn>("PowerSetUserConfiguredACPowerMode");
    if (!fn) return Status::fail(ErrorCode::Unsupported, "power modes require Windows 11");
    GUID g{};
    if (!parse_guid(s, g)) return Status::fail(ErrorCode::ParseError, "guid");
    const DWORD e = fn(&g);
    return e == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(map(e), "PowerSetUserConfiguredACPowerMode (" + std::to_string(e) + ")");
}

// Gap 11: pil (DC) kullanici guc modu. Ayni Windows 11 powrprof ailesi; yoksa kontrollu Unsupported.
contracts::Result<std::string> WinPowerApi::get_dc_mode() {
    using R = contracts::Result<std::string>;
    static const auto fn = powrprof_fn<GetModeFn>("PowerGetUserConfiguredDCPowerMode");
    if (!fn) return R::fail(ErrorCode::Unsupported, "DC power mode requires Windows 11");
    GUID g{};
    const DWORD e = fn(&g);
    if (e != ERROR_SUCCESS) return R::fail(map(e), "PowerGetUserConfiguredDCPowerMode (" + std::to_string(e) + ")");
    return guid_text(g);
}

Status WinPowerApi::set_dc_mode(const std::string& s) {
    static const auto fn = powrprof_fn<SetModeFn>("PowerSetUserConfiguredDCPowerMode");
    if (!fn) return Status::fail(ErrorCode::Unsupported, "DC power mode requires Windows 11");
    GUID g{};
    if (!parse_guid(s, g)) return Status::fail(ErrorCode::ParseError, "guid");
    const DWORD e = fn(&g);
    return e == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(map(e), "PowerSetUserConfiguredDCPowerMode (" + std::to_string(e) + ")");
}

contracts::Result<bool> WinPowerApi::has_battery() {
    SYSTEM_POWER_STATUS st{};
    if (!GetSystemPowerStatus(&st)) return contracts::Result<bool>::fail(map(GetLastError()), "GetSystemPowerStatus");
    if (st.BatteryFlag == 255) return contracts::Result<bool>::fail(ErrorCode::IoError, "battery status unknown");
    return (st.BatteryFlag & 128) == 0;  // 128: sistem pili yok
}

contracts::Result<std::string> WinPowerApi::get_active_scheme() {
    GUID* g = nullptr;
    const DWORD e = PowerGetActiveScheme(nullptr, &g);
    if (e != ERROR_SUCCESS || !g) return contracts::Result<std::string>::fail(map(e), "PowerGetActiveScheme");
    std::string s = guid_text(*g);
    LocalFree(g);
    return s;
}

Status WinPowerApi::set_active_scheme(const std::string& s) {
    GUID g{};
    if (!parse_guid(s, g)) return Status::fail(ErrorCode::ParseError, "guid");
    const DWORD e = PowerSetActiveScheme(nullptr, &g);
    return e == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(map(e), "PowerSetActiveScheme (" + std::to_string(e) + ")");
}

contracts::Result<std::vector<std::string>> WinPowerApi::list_schemes() {
    std::vector<std::string> out;
    for (ULONG i = 0; i < 64; ++i) {
        GUID g{};
        DWORD size = sizeof(g);
        const DWORD e = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, reinterpret_cast<UCHAR*>(&g), &size);
        if (e == ERROR_NO_MORE_ITEMS) break;
        if (e != ERROR_SUCCESS) return contracts::Result<std::vector<std::string>>::fail(map(e), "PowerEnumerate");
        out.push_back(guid_text(g));
    }
    return out;
}

}  // namespace ef::platform::adapters
