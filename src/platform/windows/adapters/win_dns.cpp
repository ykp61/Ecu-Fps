#include "platform/windows/adapters/win_dns.h"

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <lm.h>
#include <netioapi.h>
#include <windows.h>
#include <objbase.h>

#include <memory>
#include <string>

#include "platform/windows/win_env.h"

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Result;
using contracts::Status;

namespace {

using GetFn = DWORD(WINAPI*)(GUID, DNS_INTERFACE_SETTINGS*);
using SetFn = DWORD(WINAPI*)(GUID, const DNS_INTERFACE_SETTINGS*);
using FreeFn = void(WINAPI*)(DNS_INTERFACE_SETTINGS*);

struct Fns {
    GetFn get = nullptr;
    SetFn set = nullptr;
    FreeFn free = nullptr;
};

// Eski Windows'ta yoktur: ihracat calisma aninda aranir (yukleme hatasi yerine Unsupported).
const Fns& fns() {
    static const Fns f = [] {
        Fns r;
        if (const HMODULE m = GetModuleHandleW(L"iphlpapi.dll")) {
            r.get = reinterpret_cast<GetFn>(GetProcAddress(m, "GetInterfaceDnsSettings"));
            r.set = reinterpret_cast<SetFn>(GetProcAddress(m, "SetInterfaceDnsSettings"));
            r.free = reinterpret_cast<FreeFn>(GetProcAddress(m, "FreeInterfaceDnsSettings"));
        }
        return r;
    }();
    return f;
}

bool to_guid(const std::string& s, GUID& g) {
    const std::wstring w = to_wide(s);
    return IIDFromString(w.c_str(), &g) == S_OK;
}

// Arayuz gercekten var mi: GUID -> LUID cevrimi yoksa yok.
bool exists(const GUID& g) {
    NET_LUID luid{};
    return ConvertInterfaceGuidToLuid(&g, &luid) == NO_ERROR;
}

std::optional<bool> key_has_children(HKEY root, const wchar_t* path) {
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(root, path, 0, KEY_READ, &k);
    if (st == ERROR_FILE_NOT_FOUND) return false;
    if (st != ERROR_SUCCESS) return std::nullopt;
    DWORD subkeys = 0;
    const LSTATUS q = RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    if (q != ERROR_SUCCESS) return std::nullopt;
    return subkeys > 0;
}

std::optional<bool> value_exists(HKEY root, const wchar_t* path, const wchar_t* name) {
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(root, path, 0, KEY_READ, &k);
    if (st == ERROR_FILE_NOT_FOUND) return false;
    if (st != ERROR_SUCCESS) return std::nullopt;
    const LSTATUS q = RegQueryValueExW(k, name, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    if (q == ERROR_FILE_NOT_FOUND) return false;
    if (q != ERROR_SUCCESS) return std::nullopt;
    return true;
}

}  // namespace

Result<std::string> WinDnsApi::read(const std::string& guid, bool v6) {
    const auto& f = fns();
    if (!f.get || !f.free) return Result<std::string>::fail(ErrorCode::Unsupported, "GetInterfaceDnsSettings unavailable");
    GUID g{};
    if (!to_guid(guid, g)) return Result<std::string>::fail(ErrorCode::ParseError, "guid");
    if (!exists(g)) return Result<std::string>::fail(ErrorCode::MissingValue, "interface not found");
    DNS_INTERFACE_SETTINGS s{};
    s.Version = DNS_INTERFACE_SETTINGS_VERSION1;
    s.Flags = v6 ? DNS_SETTING_IPV6 : 0;
    const DWORD rc = f.get(g, &s);
    if (rc != NO_ERROR) return Result<std::string>::fail(rc == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "GetInterfaceDnsSettings " + std::to_string(rc));
    std::string out = s.NameServer ? to_utf8(s.NameServer) : std::string();
    f.free(&s);
    // Windows bosluk veya virgul kullanabilir: tek bicim (virgul, bosluksuz).
    std::string norm;
    for (char c : out) {
        if (c == ' ' || c == ',') {
            if (!norm.empty() && norm.back() != ',') norm.push_back(',');
        } else {
            norm.push_back(c);
        }
    }
    while (!norm.empty() && norm.back() == ',') norm.pop_back();
    return norm;
}

Status WinDnsApi::write(const std::string& guid, bool v6, const std::string& servers) {
    const auto& f = fns();
    if (!f.set) return Status::fail(ErrorCode::Unsupported, "SetInterfaceDnsSettings unavailable");
    GUID g{};
    if (!to_guid(guid, g)) return Status::fail(ErrorCode::ParseError, "guid");
    if (!exists(g)) return Status::fail(ErrorCode::MissingValue, "interface not found");
    std::wstring w = to_wide(servers);  // bos: otomatige don
    DNS_INTERFACE_SETTINGS s{};
    s.Version = DNS_INTERFACE_SETTINGS_VERSION1;
    s.Flags = DNS_SETTING_NAMESERVER | (v6 ? DNS_SETTING_IPV6 : 0);
    s.NameServer = w.data();
    const DWORD rc = f.set(g, &s);
    if (rc != NO_ERROR) return Status::fail(rc == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, "SetInterfaceDnsSettings " + std::to_string(rc));
    return contracts::ok_status();
}

core::adapters::windows::DnsPolicyFacts read_dns_policy_facts() {
    core::adapters::windows::DnsPolicyFacts f;
    {
        LPWSTR name = nullptr;
        NETSETUP_JOIN_STATUS st = NetSetupUnknownStatus;
        if (NetGetJoinInformation(nullptr, &name, &st) == NERR_Success) f.domain_joined = st == NetSetupDomainName;
        if (name) NetApiBufferFree(name);
    }
    {   // NRPT: ilke deposu ve yerel depo (Add-DnsClientNrptRule, DirectAccess, VPN istemcileri).
        const auto gpo = key_has_children(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient\\DnsPolicyConfig");
        const auto local = key_has_children(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters\\DnsPolicyConfig");
        if (gpo == std::optional<bool>(true) || local == std::optional<bool>(true)) f.nrpt_rules = true;
        else if (gpo && local) f.nrpt_rules = false;
    }
    f.doh_policy = value_exists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient", L"DoHPolicy");
    {
        ULONG len = 16 * 1024;
        std::unique_ptr<unsigned char[]> buf;
        ULONG rc = ERROR_BUFFER_OVERFLOW;
        for (int i = 0; i < 3 && rc == ERROR_BUFFER_OVERFLOW; ++i) {
            buf.reset(new unsigned char[len]);
            rc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get()), &len);
        }
        if (rc == NO_ERROR) {
            // Isim tahmini "VPN yok" kaniti degildir: calisan her sanal/tunel bagdastirici, bilinen zararsizlar disinda,
            // VPN sayilir (degisiklik onerilmez).
            const auto has = [](const std::wstring& s, std::initializer_list<const wchar_t*> keys) {
                for (const auto* k : keys)
                    if (s.find(k) != std::wstring::npos) return true;
                return false;
            };
            bool vpn = false;
            for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get()); a; a = a->Next) {
                if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                const std::wstring d = a->Description ? a->Description : L"";
                const bool named = has(d, {L"VPN", L"WireGuard", L"TAP-Windows", L"Wintun", L"AnyConnect", L"Cisco Secure", L"PANGP", L"GlobalProtect",
                                           L"NordLynx", L"Fortinet", L"OpenVPN", L"Tunnel"});
                const bool benign = has(d, {L"Hyper-V", L"VirtualBox", L"VMware", L"Bluetooth", L"Teredo", L"ISATAP", L"6to4", L"Loopback"});
                const bool virtual_if = a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_TUNNEL || a->IfType == IF_TYPE_PROP_VIRTUAL;
                if (named || (virtual_if && !benign)) vpn = true;
            }
            f.vpn_active = vpn;
        }
    }
    return f;
}

}  // namespace ef::platform::adapters
