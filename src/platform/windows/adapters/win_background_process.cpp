#include "platform/windows/adapters/win_background_process.h"

#include <windows.h>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Status;
namespace cw = core::adapters::windows;

namespace {
// Erisim yalniz PROCESS_QUERY_LIMITED_INFORMATION (+ istege bagli PROCESS_SET_INFORMATION). SYNCHRONIZE istenmez:
// yasiyor mu denetimi GetExitCodeProcess ile yapilir. Kimlik: PID + olusturulma zamani eslesmeli.
HANDLE open_verified(const cw::ProcessId& id, bool for_write, Status& err) {
    const DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | (for_write ? PROCESS_SET_INFORMATION : 0);
    HANDLE h = OpenProcess(access, FALSE, id.pid);
    if (!h) {
        const DWORD e = GetLastError();
        err = e == ERROR_ACCESS_DENIED ? Status::fail(ErrorCode::Denied, "process is protected or access denied")
                                       : Status::fail(ErrorCode::MissingValue, "process not found");
        return nullptr;
    }
    FILETIME c{}, x{}, k{}, u{};
    DWORD code = 0;
    const bool ok = GetProcessTimes(h, &c, &x, &k, &u) &&
                    ((static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime) == id.creation &&
                    GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    if (!ok) {
        CloseHandle(h);
        err = Status::fail(ErrorCode::MissingValue, "process exited or PID reused");
        return nullptr;
    }
    err = contracts::ok_status();
    return h;
}

DWORD to_win(cw::PriorityClass p) {
    switch (p) {
        case cw::PriorityClass::Idle: return IDLE_PRIORITY_CLASS;
        case cw::PriorityClass::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
        case cw::PriorityClass::Normal: return NORMAL_PRIORITY_CLASS;
        case cw::PriorityClass::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
        case cw::PriorityClass::High: return HIGH_PRIORITY_CLASS;
        case cw::PriorityClass::Realtime: return REALTIME_PRIORITY_CLASS;
    }
    return NORMAL_PRIORITY_CLASS;
}
}  // namespace

Status WinBackgroundProcessApi::verify(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, false, err);
    if (h) CloseHandle(h);
    return err;
}

contracts::Result<std::string> WinBackgroundProcessApi::image_path(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, false, err);
    if (!h) return contracts::Result<std::string>(err.error());
    wchar_t buf[1024];
    DWORD n = 1024;
    const BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &n);
    CloseHandle(h);
    if (!ok || n == 0) return contracts::Result<std::string>::fail(ErrorCode::IoError, "QueryFullProcessImageName");
    const int m = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    if (m <= 0) return contracts::Result<std::string>::fail(ErrorCode::IoError, "utf8");
    std::string out(static_cast<std::size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), out.data(), m, nullptr, nullptr);
    return out;
}

contracts::Result<cw::PriorityClass> WinBackgroundProcessApi::get_priority(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, false, err);
    if (!h) return contracts::Result<cw::PriorityClass>(err.error());
    const DWORD c = GetPriorityClass(h);
    CloseHandle(h);
    switch (c) {
        case IDLE_PRIORITY_CLASS: return cw::PriorityClass::Idle;
        case BELOW_NORMAL_PRIORITY_CLASS: return cw::PriorityClass::BelowNormal;
        case NORMAL_PRIORITY_CLASS: return cw::PriorityClass::Normal;
        case ABOVE_NORMAL_PRIORITY_CLASS: return cw::PriorityClass::AboveNormal;
        case HIGH_PRIORITY_CLASS: return cw::PriorityClass::High;
        case REALTIME_PRIORITY_CLASS: return cw::PriorityClass::Realtime;
        default: return contracts::Result<cw::PriorityClass>::fail(ErrorCode::IoError, "GetPriorityClass");
    }
}

Status WinBackgroundProcessApi::set_priority(const cw::ProcessId& id, cw::PriorityClass p) {
    if (p == cw::PriorityClass::Realtime || p == cw::PriorityClass::High)
        return Status::fail(ErrorCode::Denied, "high/realtime is never written");
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, true, err);
    if (!h) return err;
    const BOOL ok = SetPriorityClass(h, to_win(p));
    CloseHandle(h);
    return ok ? contracts::ok_status() : Status::fail(ErrorCode::Denied, "SetPriorityClass");
}

contracts::Result<cw::ThrottleState> WinBackgroundProcessApi::get_throttling(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, false, err);
    if (!h) return contracts::Result<cw::ThrottleState>(err.error());
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    const BOOL ok = GetProcessInformation(h, ProcessPowerThrottling, &s, sizeof(s));
    CloseHandle(h);
    if (!ok) return contracts::Result<cw::ThrottleState>::fail(ErrorCode::Unsupported, "ProcessPowerThrottling query");
    return cw::ThrottleState{s.ControlMask, s.StateMask};
}

Status WinBackgroundProcessApi::set_throttling(const cw::ProcessId& id, cw::ThrottleState st) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, true, err);
    if (!h) return err;
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = st.control;
    s.StateMask = st.state;
    const BOOL ok = SetProcessInformation(h, ProcessPowerThrottling, &s, sizeof(s));
    CloseHandle(h);
    return ok ? contracts::ok_status() : Status::fail(ErrorCode::Denied, "SetProcessInformation");
}

}  // namespace ef::platform::adapters
