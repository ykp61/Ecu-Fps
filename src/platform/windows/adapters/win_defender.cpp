#include "platform/windows/adapters/win_defender.h"

#include <windows.h>
#include <comdef.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <cstdio>

#include "platform/windows/win_env.h"
#include "platform/windows/wmi.h"

namespace ef::platform::adapters {

using contracts::ErrorCode;
using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kNs[] = L"ROOT\\Microsoft\\Windows\\Defender";
constexpr wchar_t kClass[] = L"MSFT_MpPreference";

struct ComScope {
    bool owned = false;
    ComScope() { owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() {
        if (owned) CoUninitialize();
    }
};

std::string hr_text(const char* what, HRESULT hr) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
    return buf;
}

ErrorCode hr_code(HRESULT hr) {
    return (hr == WBEM_E_ACCESS_DENIED || hr == E_ACCESSDENIED) ? ErrorCode::Denied : ErrorCode::IoError;
}

contracts::Result<ComPtr<IWbemServices>> connect() {
    using R = contracts::Result<ComPtr<IWbemServices>>;
    ComPtr<IWbemLocator> loc;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc));
    if (FAILED(hr)) return R::fail(ErrorCode::Unsupported, hr_text("WbemLocator", hr));
    ComPtr<IWbemServices> svc;
    hr = loc->ConnectServer(_bstr_t(kNs), nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &svc);
    if (FAILED(hr)) return R::fail(hr_code(hr) == ErrorCode::Denied ? ErrorCode::Denied : ErrorCode::Unsupported, hr_text("ConnectServer", hr));
    CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                      EOAC_NONE);
    return svc;
}

// ExclusionPath: dize dizisi. Yonetici degilse Defender tek elemanli "N/A: Must be an administrator ..." dizisi doner.
bool is_hidden_marker(const std::string& s) { return s.rfind("N/A", 0) == 0; }

contracts::Status exec_path_method(const wchar_t* method, const std::string& path) {
    ComScope com;
    auto svc = connect();
    if (!svc.ok()) return contracts::Status(svc.error());
    ComPtr<IWbemClassObject> cls;
    HRESULT hr = svc.value()->GetObject(_bstr_t(kClass), 0, nullptr, &cls, nullptr);
    if (FAILED(hr)) return contracts::Status::fail(hr_code(hr), hr_text("GetObject", hr));
    ComPtr<IWbemClassObject> in_sig;
    hr = cls->GetMethod(method, 0, &in_sig, nullptr);
    if (FAILED(hr) || !in_sig) return contracts::Status::fail(ErrorCode::Unsupported, hr_text("GetMethod", hr));
    ComPtr<IWbemClassObject> in;
    hr = in_sig->SpawnInstance(0, &in);
    if (FAILED(hr)) return contracts::Status::fail(ErrorCode::IoError, hr_text("SpawnInstance", hr));
    const std::wstring wpath = to_wide(path);
    SAFEARRAY* arr = SafeArrayCreateVector(VT_BSTR, 0, 1);
    if (!arr) return contracts::Status::fail(ErrorCode::IoError, "SafeArrayCreateVector failed");
    LONG idx = 0;
    BSTR b = SysAllocString(wpath.c_str());
    hr = SafeArrayPutElement(arr, &idx, b);  // BSTR kopyalanir
    SysFreeString(b);
    if (FAILED(hr)) {
        SafeArrayDestroy(arr);
        return contracts::Status::fail(ErrorCode::IoError, hr_text("SafeArrayPutElement", hr));
    }
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_ARRAY | VT_BSTR;
    v.parray = arr;
    hr = in->Put(L"ExclusionPath", 0, &v, 0);
    VariantClear(&v);  // diziyi serbest birakir
    if (FAILED(hr)) return contracts::Status::fail(ErrorCode::IoError, hr_text("Put ExclusionPath", hr));
    ComPtr<IWbemClassObject> out;
    hr = svc.value()->ExecMethod(_bstr_t(kClass), _bstr_t(method), 0, nullptr, in.Get(), &out, nullptr);
    if (FAILED(hr)) return contracts::Status::fail(hr_code(hr), hr_text("ExecMethod", hr));
    return contracts::ok_status();  // sonuc yine de geri okumayla dogrulanir (motor read_back)
}

bool policy_blocks_local_exclusions() {
    constexpr wchar_t kPol[] = L"SOFTWARE\\Policies\\Microsoft\\Windows Defender";
    DWORD v = 0, size = sizeof(v), type = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kPol, L"DisableLocalAdminMerge", RRF_RT_REG_DWORD, &type, &v, &size) == ERROR_SUCCESS && v == 1) return true;
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Exclusions\\Paths", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD values = 0;
        const LSTATUS s = RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(k);
        if (s != ERROR_SUCCESS || values > 0) return true;  // ilke dislamasi var (veya okunamadi): yerel dislama yok sayilabilir
    }
    return false;
}
}  // namespace

contracts::Result<std::vector<std::string>> WinDefenderApi::exclusion_paths() {
    using R = contracts::Result<std::vector<std::string>>;
    ComScope com;
    auto svc = connect();
    if (!svc.ok()) return R(svc.error());
    ComPtr<IEnumWbemClassObject> en;
    HRESULT hr = svc.value()->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT ExclusionPath FROM MSFT_MpPreference"),
                                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    if (FAILED(hr)) return R::fail(hr_code(hr), hr_text("ExecQuery", hr));
    ComPtr<IWbemClassObject> obj;
    ULONG n = 0;
    hr = en->Next(5000, 1, &obj, &n);
    if (FAILED(hr) || hr == WBEM_S_TIMEDOUT) return R::fail(ErrorCode::IoError, hr_text("Next", hr));
    if (n == 0) return R::fail(ErrorCode::Unsupported, "MSFT_MpPreference has no instance");
    VARIANT v;
    VariantInit(&v);
    hr = obj->Get(L"ExclusionPath", 0, &v, nullptr, nullptr);
    if (FAILED(hr)) {
        VariantClear(&v);
        return R::fail(ErrorCode::IoError, hr_text("Get ExclusionPath", hr));
    }
    std::vector<std::string> out;
    if (v.vt == (VT_ARRAY | VT_BSTR) && v.parray) {
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(v.parray, 1, &lo);
        SafeArrayGetUBound(v.parray, 1, &hi);
        for (LONG k = lo; k <= hi && (k - lo) < 4096; ++k) {
            BSTR b = nullptr;
            if (SUCCEEDED(SafeArrayGetElement(v.parray, &k, &b)) && b) {
                out.push_back(to_utf8(b));
                SysFreeString(b);
            }
        }
    } else if (v.vt != VT_NULL && v.vt != VT_EMPTY) {
        VariantClear(&v);
        return R::fail(ErrorCode::ParseError, "ExclusionPath has unexpected type");
    }
    VariantClear(&v);
    if (out.size() == 1 && is_hidden_marker(out[0])) return R::fail(ErrorCode::Denied, "exclusion list hidden (administrator required)");
    return out;
}

contracts::Status WinDefenderApi::add_path(const std::string& path) { return exec_path_method(L"Add", path); }
contracts::Status WinDefenderApi::remove_path(const std::string& path) { return exec_path_method(L"Remove", path); }

std::optional<bool> WinDefenderApi::usable() {
    auto rows = wmi::query(kNs, L"SELECT AMRunningMode, AMServiceEnabled, AntivirusEnabled FROM MSFT_MpComputerStatus",
                           {L"AMRunningMode", L"AMServiceEnabled", L"AntivirusEnabled"});
    if (!rows.ok() || rows.value().empty()) return std::nullopt;  // bilinmiyor: yazilmaz
    const auto& r = rows.value().front();
    if (const auto it = r.find("AMRunningMode"); it != r.end() && !it->second.is_null && it->second.s && *it->second.s != "Normal")
        return false;  // pasif / EDR engelleme / yan yana: baska antivirus etkin, yerel dislama gecerli degil
    for (const char* k : {"AMServiceEnabled", "AntivirusEnabled"}) {
        const auto it = r.find(k);
        if (it == r.end() || it->second.is_null || !it->second.i) return std::nullopt;
        if (*it->second.i == 0) return false;
    }
    if (policy_blocks_local_exclusions()) return false;
    return true;
}

}  // namespace ef::platform::adapters
