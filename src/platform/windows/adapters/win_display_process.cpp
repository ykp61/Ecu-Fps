#include "platform/windows/adapters/win_display_process.h"

#include <windows.h>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Status;
namespace cw = core::adapters::windows;

// ---------------- Ekran ----------------
namespace {
// Uyumluluk B6 (bosluk 10): GDI aygit adi verilmisse yalniz o ad, masaustune bagliysa kabul edilir (sira numarasina
// dusulmez: yanlis ekran degismez). Ad yoksa (eski journal kaydi) masaustune bagli aygitlarin sirasi.
bool device_name(const cw::DisplayRef& ref, wchar_t (&out)[32]) {
    std::wstring want;
    if (!ref.gdi.empty()) {
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ref.gdi.data(), static_cast<int>(ref.gdi.size()), nullptr, 0);
        if (n <= 0 || n >= 32) return false;
        want.resize(static_cast<std::size_t>(n));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ref.gdi.data(), static_cast<int>(ref.gdi.size()), want.data(), n);
    }
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    std::uint32_t active = 0;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            const bool hit = want.empty() ? active == ref.index : _wcsicmp(dd.DeviceName, want.c_str()) == 0;
            if (hit) {
                wcsncpy_s(out, dd.DeviceName, _TRUNCATE);
                return true;
            }
            ++active;
        }
        dd.cb = sizeof(dd);
    }
    return false;
}
}  // namespace

contracts::Result<cw::DisplayMode> WinDisplayApi::current(const cw::DisplayRef& idx) {
    wchar_t name[32];
    if (!device_name(idx, name)) return contracts::Result<cw::DisplayMode>::fail(ErrorCode::MissingValue, "display not found");
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsExW(name, ENUM_CURRENT_SETTINGS, &dm, 0))
        return contracts::Result<cw::DisplayMode>::fail(ErrorCode::IoError, "EnumDisplaySettingsEx");
    return cw::DisplayMode{dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency};
}

contracts::Result<std::vector<cw::DisplayMode>> WinDisplayApi::modes(const cw::DisplayRef& idx) {
    wchar_t name[32];
    if (!device_name(idx, name)) return contracts::Result<std::vector<cw::DisplayMode>>::fail(ErrorCode::MissingValue, "display not found");
    std::vector<cw::DisplayMode> out;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; i < 4096 && EnumDisplaySettingsExW(name, i, &dm, 0); ++i) {
        const cw::DisplayMode m{dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency};
        bool dup = false;
        for (const auto& x : out) dup = dup || x == m;
        if (!dup) out.push_back(m);
    }
    return out;
}

namespace {
LONG change(const cw::DisplayRef& idx, const cw::DisplayMode& m, DWORD flags) {
    wchar_t name[32];
    if (!device_name(idx, name)) return DISP_CHANGE_BADPARAM;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsExW(name, ENUM_CURRENT_SETTINGS, &dm, 0)) return DISP_CHANGE_FAILED;
    dm.dmPelsWidth = m.width;
    dm.dmPelsHeight = m.height;
    dm.dmDisplayFrequency = m.hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    return ChangeDisplaySettingsExW(name, &dm, nullptr, flags, nullptr);
}
}  // namespace

Status WinDisplayApi::test(const cw::DisplayRef& idx, const cw::DisplayMode& m) {
    const LONG r = change(idx, m, CDS_TEST);
    return r == DISP_CHANGE_SUCCESSFUL ? contracts::ok_status() : Status::fail(ErrorCode::Unsupported, "CDS_TEST " + std::to_string(r));
}

Status WinDisplayApi::set(const cw::DisplayRef& idx, const cw::DisplayMode& m) {
    const LONG r = change(idx, m, 0);  // oturumluk; CDS_UPDATEREGISTRY yok
    return r == DISP_CHANGE_SUCCESSFUL ? contracts::ok_status() : Status::fail(ErrorCode::IoError, "ChangeDisplaySettingsEx " + std::to_string(r));
}

Status WinDisplayApi::set_persistent(const cw::DisplayRef& idx, const cw::DisplayMode& m) {
    const LONG r = change(idx, m, CDS_UPDATEREGISTRY);
    return r == DISP_CHANGE_SUCCESSFUL ? contracts::ok_status() : Status::fail(ErrorCode::IoError, "ChangeDisplaySettingsEx(registry) " + std::to_string(r));
}

// ---------------- Gorsel efektler ----------------
namespace {
struct SpiPair {
    UINT get, set;
};
bool spi_for(cw::VisualEffect e, SpiPair& p) {
    switch (e) {
        case cw::VisualEffect::ClientAreaAnimation: p = {SPI_GETCLIENTAREAANIMATION, SPI_SETCLIENTAREAANIMATION}; return true;
        case cw::VisualEffect::MenuAnimation: p = {SPI_GETMENUANIMATION, SPI_SETMENUANIMATION}; return true;
        case cw::VisualEffect::ComboBoxAnimation: p = {SPI_GETCOMBOBOXANIMATION, SPI_SETCOMBOBOXANIMATION}; return true;
        case cw::VisualEffect::ListBoxSmoothScrolling: p = {SPI_GETLISTBOXSMOOTHSCROLLING, SPI_SETLISTBOXSMOOTHSCROLLING}; return true;
        case cw::VisualEffect::CursorShadow: p = {SPI_GETCURSORSHADOW, SPI_SETCURSORSHADOW}; return true;
        case cw::VisualEffect::DropShadow: p = {SPI_GETDROPSHADOW, SPI_SETDROPSHADOW}; return true;
        default: return false;
    }
}
constexpr wchar_t kPersonalize[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
}  // namespace

contracts::Result<bool> WinVisualEffectsApi::get(cw::VisualEffect e) {
    if (e == cw::VisualEffect::Transparency) {
        DWORD v = 1, sz = sizeof(v);
        const auto st = RegGetValueW(HKEY_CURRENT_USER, kPersonalize, L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &v, &sz);
        if (st == ERROR_FILE_NOT_FOUND) return true;  // varsayilan acik
        if (st != ERROR_SUCCESS) return contracts::Result<bool>::fail(ErrorCode::IoError, "EnableTransparency");
        return v != 0;
    }
    SpiPair p{};
    if (!spi_for(e, p)) return contracts::Result<bool>::fail(ErrorCode::Unsupported, "effect");
    BOOL v = FALSE;
    if (!SystemParametersInfoW(p.get, 0, &v, 0)) return contracts::Result<bool>::fail(ErrorCode::IoError, "SPI get");
    return v != FALSE;
}

Status WinVisualEffectsApi::set(cw::VisualEffect e, bool on) {
    if (e == cw::VisualEffect::Transparency) {
        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kPersonalize, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return Status::fail(ErrorCode::IoError, "Personalize key");
        const DWORD v = on ? 1 : 0;
        const auto st = RegSetValueExW(k, L"EnableTransparency", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
        RegCloseKey(k);
        return st == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(ErrorCode::IoError, "EnableTransparency write");
    }
    SpiPair p{};
    if (!spi_for(e, p)) return Status::fail(ErrorCode::Unsupported, "effect");
    // Bu SPI'ler BOOL'u pvParam isaretci degeri olarak alir.
    if (!SystemParametersInfoW(p.set, 0, reinterpret_cast<PVOID>(static_cast<UINT_PTR>(on ? TRUE : FALSE)), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        return Status::fail(ErrorCode::IoError, "SPI set");
    return contracts::ok_status();
}

// ---------------- Surec ----------------
std::uint64_t process_creation_time(std::uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME c{}, e{}, k{}, u{};
    std::uint64_t v = 0;
    if (GetProcessTimes(h, &c, &e, &k, &u)) v = (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(h);
    return v;
}

namespace {
// Kimligi dogrulanmis tanitici: PID + olusturulma zamani eslesmeli ve surec yasiyor olmali.
HANDLE open_verified(const cw::ProcessId& id, DWORD access, Status& err) {
    HANDLE h = OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, id.pid);
    if (!h) {
        const DWORD e = GetLastError();
        err = e == ERROR_ACCESS_DENIED ? Status::fail(ErrorCode::Denied, "process is protected or access denied")
                                       : Status::fail(ErrorCode::MissingValue, "process not found");
        return nullptr;
    }
    FILETIME c{}, x{}, k{}, u{};
    const bool ok = GetProcessTimes(h, &c, &x, &k, &u) &&
                    ((static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime) == id.creation &&
                    WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
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

Status WinProcessApi::verify(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, 0, err);
    if (h) CloseHandle(h);
    return err;
}

contracts::Result<cw::PriorityClass> WinProcessApi::get_priority(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, 0, err);
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

Status WinProcessApi::set_priority(const cw::ProcessId& id, cw::PriorityClass p) {
    if (p == cw::PriorityClass::Realtime) return Status::fail(ErrorCode::Denied, "realtime is never set");
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, PROCESS_SET_INFORMATION, err);
    if (!h) return err;
    const BOOL ok = SetPriorityClass(h, to_win(p));
    CloseHandle(h);
    return ok ? contracts::ok_status() : Status::fail(ErrorCode::Denied, "SetPriorityClass");
}

contracts::Result<cw::ThrottleState> WinProcessApi::get_throttling(const cw::ProcessId& id) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, 0, err);
    if (!h) return contracts::Result<cw::ThrottleState>(err.error());
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    const BOOL ok = GetProcessInformation(h, ProcessPowerThrottling, &s, sizeof(s));
    CloseHandle(h);
    if (!ok) return contracts::Result<cw::ThrottleState>::fail(ErrorCode::Unsupported, "ProcessPowerThrottling query");
    return cw::ThrottleState{s.ControlMask, s.StateMask};
}

Status WinProcessApi::set_throttling(const cw::ProcessId& id, cw::ThrottleState st) {
    Status err = contracts::ok_status();
    HANDLE h = open_verified(id, PROCESS_SET_INFORMATION, err);
    if (!h) return err;
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = st.control;
    s.StateMask = st.state;
    const BOOL ok = SetProcessInformation(h, ProcessPowerThrottling, &s, sizeof(s));
    CloseHandle(h);
    return ok ? contracts::ok_status() : Status::fail(ErrorCode::Denied, "SetProcessInformation");
}

std::optional<cw::ThrottleState> WinSelfThrottleApi::get() {
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (!GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s))) return std::nullopt;
    return cw::ThrottleState{s.ControlMask, s.StateMask};
}

bool WinSelfThrottleApi::set(cw::ThrottleState st) {
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = st.control;
    s.StateMask = st.state;
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s)) != FALSE;
}

}  // namespace ef::platform::adapters

namespace ef::platform::adapters {

core::games::ProcessState read_process_state(std::uint32_t pid) {
    core::games::ProcessState st;
    if (pid == 0) return st;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return st;
    if (const DWORD pc = GetPriorityClass(h); pc != 0) {
        st.priority_class = pc;
        st.known = true;
    }
    DWORD_PTR proc_mask = 0, sys_mask = 0;
    if (GetProcessAffinityMask(h, &proc_mask, &sys_mask)) {
        st.affinity_mask = static_cast<std::uint64_t>(proc_mask);
        st.system_mask = static_cast<std::uint64_t>(sys_mask);
        st.known = true;
    }
    CloseHandle(h);
    return st;
}

}  // namespace ef::platform::adapters
