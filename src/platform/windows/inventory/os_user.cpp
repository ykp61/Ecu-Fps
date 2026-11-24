#include "platform/windows/inventory/os_user.h"

#include <windows.h>
#include <lm.h>
#include <sddl.h>
#include <wtsapi32.h>

#include <memory>
#include <string>

#include "platform/windows/inventory/field_json.h"
#include "platform/windows/win_env.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;

namespace {

std::string win_error(const char* what, DWORD e) { return std::string(what) + " failed (" + std::to_string(e) + ")"; }

Field<std::string> reg_string(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[512];
    DWORD size = sizeof(buf);
    const LSTATUS st = RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &size);
    if (st == ERROR_FILE_NOT_FOUND) return Field<std::string>::unknown("registry value absent");
    if (st != ERROR_SUCCESS) return Field<std::string>::unknown(win_error("RegGetValue", static_cast<DWORD>(st)));
    return Field<std::string>::known(to_utf8(buf), Provenance::Registry);
}

Field<std::uint32_t> reg_dword(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    const LSTATUS st = RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &size);
    if (st == ERROR_FILE_NOT_FOUND) return Field<std::uint32_t>::unknown("registry value absent");
    if (st != ERROR_SUCCESS) return Field<std::uint32_t>::unknown(win_error("RegGetValue", static_cast<DWORD>(st)));
    return Field<std::uint32_t>::known(v, Provenance::Registry);
}

std::string arch_name(USHORT m) {
    switch (m) {
        case IMAGE_FILE_MACHINE_AMD64: return "x64";
        case IMAGE_FILE_MACHINE_ARM64: return "arm64";
        case IMAGE_FILE_MACHINE_I386: return "x86";
        case IMAGE_FILE_MACHINE_UNKNOWN: return "native";
        default: return "machine_" + std::to_string(m);
    }
}

using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

Field<std::string> token_user_sid(HANDLE token) {
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    if (len == 0) return Field<std::string>::unknown(win_error("GetTokenInformation", GetLastError()));
    auto buf = std::make_unique<unsigned char[]>(len);
    if (!GetTokenInformation(token, TokenUser, buf.get(), len, &len))
        return Field<std::string>::unknown(win_error("GetTokenInformation", GetLastError()));
    LPWSTR s = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.get())->User.Sid, &s))
        return Field<std::string>::unknown(win_error("ConvertSidToStringSid", GetLastError()));
    std::string out = to_utf8(s);
    LocalFree(s);
    return Field<std::string>::known(out, Provenance::Api);
}

Field<std::string> session_user_sid(DWORD session) {
    LPWSTR user = nullptr, domain = nullptr;
    DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSUserName, &user, &bytes))
        return Field<std::string>::unknown(win_error("WTSQuerySessionInformation", GetLastError()));
    std::wstring u(user);
    WTSFreeMemory(user);
    if (u.empty()) return Field<std::string>::unknown("no interactive user in session");
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSDomainName, &domain, &bytes))
        return Field<std::string>::unknown(win_error("WTSQuerySessionInformation(domain)", GetLastError()));
    const std::wstring account = std::wstring(domain) + L"\\" + u;
    WTSFreeMemory(domain);

    DWORD sid_len = 0, dom_len = 0;
    SID_NAME_USE use{};
    LookupAccountNameW(nullptr, account.c_str(), nullptr, &sid_len, nullptr, &dom_len, &use);
    if (sid_len == 0) return Field<std::string>::unknown(win_error("LookupAccountName", GetLastError()));
    auto sid = std::make_unique<unsigned char[]>(sid_len);
    std::wstring dom(dom_len, L'\0');
    if (!LookupAccountNameW(nullptr, account.c_str(), sid.get(), &sid_len, dom.data(), &dom_len, &use))
        return Field<std::string>::unknown(win_error("LookupAccountName", GetLastError()));
    LPWSTR s = nullptr;
    if (!ConvertSidToStringSidW(sid.get(), &s)) return Field<std::string>::unknown(win_error("ConvertSidToStringSid", GetLastError()));
    std::string out = to_utf8(s);
    LocalFree(s);
    return Field<std::string>::known(out, Provenance::Api);
}

contracts::PolicyEntry policy_key(const char* id, const wchar_t* path) {
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k);
    if (st == ERROR_SUCCESS) {
        DWORD values = 0, subkeys = 0;
        RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr,
                         nullptr);
        RegCloseKey(k);
        // Bos anahtar politika uygulamaz.
        return {id, (values + subkeys) > 0 ? contracts::PolicyState::Present : contracts::PolicyState::Absent, {}};
    }
    if (st == ERROR_FILE_NOT_FOUND) return {id, contracts::PolicyState::Absent, {}};
    return {id, contracts::PolicyState::Unknown, win_error("RegOpenKeyEx", static_cast<DWORD>(st))};
}

}  // namespace

contracts::OsContext read_os() {
    contracts::OsContext os;
    constexpr const wchar_t* cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    os.product_name = reg_string(HKEY_LOCAL_MACHINE, cv, L"ProductName");
    os.display_version = reg_string(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion");
    os.edition_id = reg_string(HKEY_LOCAL_MACHINE, cv, L"EditionID");
    os.ubr = reg_dword(HKEY_LOCAL_MACHINE, cv, L"UBR");

    // Gercek surum: RtlGetVersion (GetVersionEx manifest'e gore yalan soyleyebilir).
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion")));
        RTL_OSVERSIONINFOW vi{};
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (fn && fn(&vi) == 0) {
            os.major = Field<std::uint32_t>::known(vi.dwMajorVersion, Provenance::Api);
            os.minor = Field<std::uint32_t>::known(vi.dwMinorVersion, Provenance::Api);
            os.build = Field<std::uint32_t>::known(vi.dwBuildNumber, Provenance::Api);
        }
    }
    if (!os.build.has()) {
        os.major = os.minor = os.build = Field<std::uint32_t>::unknown("RtlGetVersion unavailable");
    }
    // Windows 11 urun adi kayit defterinde hala "Windows 10" yazabilir: build >= 22000 ise duzelt.
    if (os.product_name.has() && os.build.has() && *os.build.value >= 22000) {
        auto& n = *os.product_name.value;
        if (n.rfind("Windows 10", 0) == 0) {
            n.replace(0, 10, "Windows 11");
            os.product_name.source = Provenance::Derived;
        }
    }
    USHORT process = 0, native = 0;
    if (IsWow64Process2(GetCurrentProcess(), &process, &native)) {
        os.native_arch = Field<std::string>::known(arch_name(native), Provenance::Api);
#if defined(_M_X64)
        os.process_arch = Field<std::string>::known(process == IMAGE_FILE_MACHINE_UNKNOWN ? "x64" : arch_name(process), Provenance::Api);
#else
        os.process_arch = Field<std::string>::known(arch_name(process), Provenance::Api);
#endif
    } else {
        os.native_arch = os.process_arch = Field<std::string>::unknown(win_error("IsWow64Process2", GetLastError()));
    }
    return os;
}

contracts::UserContext read_user() {
    contracts::UserContext u;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        u.process_user_sid = token_user_sid(tok);
        TOKEN_ELEVATION el{};
        DWORD len = 0;
        if (GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len))
            u.elevated = Field<bool>::known(el.TokenIsElevated != 0, Provenance::Api);
        else
            u.elevated = Field<bool>::unknown(win_error("TokenElevation", GetLastError()));
        CloseHandle(tok);
    } else {
        u.process_user_sid = Field<std::string>::unknown(win_error("OpenProcessToken", GetLastError()));
        u.elevated = Field<bool>::unknown("no token");
    }
    DWORD session = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        u.session_id = Field<std::uint32_t>::known(session, Provenance::Api);
        u.session_user_sid = session_user_sid(session);
    } else {
        u.session_id = Field<std::uint32_t>::unknown(win_error("ProcessIdToSessionId", GetLastError()));
        u.session_user_sid = Field<std::string>::unknown("session unknown");
    }
    u.remote_session = Field<bool>::known(GetSystemMetrics(SM_REMOTESESSION) != 0, Provenance::Api);
    contracts::resolve_target_user(u);
    return u;
}

contracts::PowerContext read_power() {
    contracts::PowerContext p;
    SYSTEM_POWER_STATUS s{};
    if (!GetSystemPowerStatus(&s)) {
        const auto why = win_error("GetSystemPowerStatus", GetLastError());
        p.on_ac = Field<bool>::unknown(why);
        p.has_battery = Field<bool>::unknown(why);
        p.battery_percent = Field<int>::unknown(why);
        p.battery_saver = Field<bool>::unknown(why);
        return p;
    }
    if (s.ACLineStatus == 255) p.on_ac = Field<bool>::unknown("AC status unknown");
    else p.on_ac = Field<bool>::known(s.ACLineStatus == 1, Provenance::Api);
    const bool no_battery = (s.BatteryFlag & 128) != 0;
    if (s.BatteryFlag == 255) p.has_battery = Field<bool>::unknown("battery status unknown");
    else p.has_battery = Field<bool>::known(!no_battery, Provenance::Api);
    if (no_battery) p.battery_percent = Field<int>::unknown("no system battery");
    else if (s.BatteryLifePercent == 255) p.battery_percent = Field<int>::unknown("battery percent unknown");
    else p.battery_percent = Field<int>::known(s.BatteryLifePercent, Provenance::Api);
    p.battery_saver = Field<bool>::known(s.SystemStatusFlag == 1, Provenance::Api);
    return p;
}

contracts::PolicyContext read_policy() {
    contracts::PolicyContext pc;
    LPWSTR name = nullptr;
    NETSETUP_JOIN_STATUS st{};
    const NET_API_STATUS r = NetGetJoinInformation(nullptr, &name, &st);
    if (r == NERR_Success) {
        pc.domain_joined = Field<bool>::known(st == NetSetupDomainName, Provenance::Api);
        NetApiBufferFree(name);
    } else {
        pc.domain_joined = Field<bool>::unknown(win_error("NetGetJoinInformation", r));
    }
    // MDM kaydi: Enrollments altinda UPN tasiyan alt anahtar.
    HKEY k = nullptr;
    const LSTATUS e = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Enrollments", 0, KEY_READ, &k);
    if (e == ERROR_SUCCESS) {
        bool mdm = false;
        wchar_t sub[256];
        for (DWORD i = 0;; ++i) {
            DWORD n = 256;
            if (RegEnumKeyExW(k, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            wchar_t upn[256];
            DWORD sz = sizeof(upn);
            if (RegGetValueW(k, sub, L"UPN", RRF_RT_REG_SZ, nullptr, upn, &sz) == ERROR_SUCCESS && upn[0]) mdm = true;
        }
        RegCloseKey(k);
        pc.mdm_enrolled = Field<bool>::known(mdm, Provenance::Registry);
    } else if (e == ERROR_FILE_NOT_FOUND) {
        pc.mdm_enrolled = Field<bool>::known(false, Provenance::Registry);
    } else {
        pc.mdm_enrolled = Field<bool>::unknown(win_error("RegOpenKeyEx", static_cast<DWORD>(e)));
    }
    pc.policies.push_back(policy_key("windows_update", L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate"));
    pc.policies.push_back(policy_key("game_dvr", L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR"));
    pc.policies.push_back(policy_key("power", L"SOFTWARE\\Policies\\Microsoft\\Power\\PowerSettings"));
    pc.policies.push_back(policy_key("device_guard", L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeviceGuard"));
    return pc;
}

core::json::Value to_json(const contracts::OsContext& v) {
    auto o = core::json::Value::object();
    o.set("product_name", field_json(v.product_name));
    o.set("display_version", field_json(v.display_version));
    o.set("edition_id", field_json(v.edition_id));
    o.set("major", field_json(v.major));
    o.set("minor", field_json(v.minor));
    o.set("build", field_json(v.build));
    o.set("ubr", field_json(v.ubr));
    o.set("native_arch", field_json(v.native_arch));
    o.set("process_arch", field_json(v.process_arch));
    return o;
}

core::json::Value to_json(const contracts::UserContext& v) {
    auto o = core::json::Value::object();
    o.set("process_user_sid", field_json(v.process_user_sid));
    o.set("session_user_sid", field_json(v.session_user_sid));
    o.set("session_id", field_json(v.session_id));
    o.set("elevated", field_json(v.elevated));
    o.set("remote_session", field_json(v.remote_session));
    o.set("target_user_sid", field_json(v.target_user_sid));
    o.set("different_admin", field_json(v.different_admin));
    return o;
}

core::json::Value to_json(const contracts::PowerContext& v) {
    auto o = core::json::Value::object();
    o.set("on_ac", field_json(v.on_ac));
    o.set("has_battery", field_json(v.has_battery));
    o.set("battery_percent", field_json(v.battery_percent));
    o.set("battery_saver", field_json(v.battery_saver));
    return o;
}

core::json::Value to_json(const contracts::PolicyContext& v) {
    auto o = core::json::Value::object();
    o.set("domain_joined", field_json(v.domain_joined));
    o.set("mdm_enrolled", field_json(v.mdm_enrolled));
    auto arr = core::json::Value::array();
    for (const auto& p : v.policies) {
        auto e = core::json::Value::object();
        e.set("id", core::json::Value::string(p.id));
        e.set("state", core::json::Value::string(std::string(contracts::to_string(p.state))));
        if (!p.reason.empty()) e.set("reason", core::json::Value::string(p.reason));
        arr.push(std::move(e));
    }
    o.set("policies", std::move(arr));
    return o;
}

}  // namespace ef::platform::inventory
