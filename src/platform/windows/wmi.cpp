#include "platform/windows/wmi.h"

#include <windows.h>
#include <comdef.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include "platform/windows/win_env.h"

namespace ef::platform::wmi {

using contracts::ErrorCode;
using Microsoft::WRL::ComPtr;

namespace {
struct ComScope {
    bool owned = false;
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owned = SUCCEEDED(hr);  // RPC_E_CHANGED_MODE: zaten baslatilmis, sorun degil
    }
    ~ComScope() {
        if (owned) CoUninitialize();
    }
};

std::string hr_text(const char* what, HRESULT hr) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
    return buf;
}

Value convert(const VARIANT& v) {
    Value out;
    switch (v.vt) {
        case VT_NULL:
        case VT_EMPTY: return out;
        case VT_BSTR: out.s = to_utf8(v.bstrVal ? v.bstrVal : L""); break;
        case VT_I1: out.i = v.cVal; break;
        case VT_UI1: out.i = v.bVal; break;
        case VT_I2: out.i = v.iVal; break;
        case VT_UI2: out.i = v.uiVal; break;
        case VT_I4: out.i = v.lVal; break;
        case VT_UI4: out.i = v.ulVal; break;
        case VT_I8: out.i = v.llVal; break;
        case VT_UI8: out.i = static_cast<std::int64_t>(v.ullVal); break;
        case VT_BOOL: out.i = v.boolVal != VARIANT_FALSE ? 1 : 0; break;
        default:
            if ((v.vt & VT_ARRAY) && v.parray) {
                const VARTYPE et = static_cast<VARTYPE>(v.vt & ~VT_ARRAY);
                LONG lo = 0, hi = -1;
                SafeArrayGetLBound(v.parray, 1, &lo);
                SafeArrayGetUBound(v.parray, 1, &hi);
                for (LONG k = lo; k <= hi && (k - lo) < 256; ++k) {
                    if (et == VT_I4 || et == VT_UI4) {
                        LONG x = 0;
                        if (SUCCEEDED(SafeArrayGetElement(v.parray, &k, &x))) out.ints.push_back(x);
                    } else if (et == VT_I2 || et == VT_UI2) {
                        SHORT x = 0;
                        if (SUCCEEDED(SafeArrayGetElement(v.parray, &k, &x))) out.ints.push_back(x);
                    } else if (et == VT_UI1) {
                        BYTE x = 0;
                        if (SUCCEEDED(SafeArrayGetElement(v.parray, &k, &x))) out.ints.push_back(x);
                    }
                }
                break;
            }
            return out;
    }
    out.is_null = false;
    if (!out.ints.empty()) out.is_null = false;
    return out;
}
}  // namespace

contracts::Result<std::vector<Row>> query(const wchar_t* ns, const wchar_t* wql, const std::vector<std::wstring>& props) {
    using R = contracts::Result<std::vector<Row>>;
    ComScope com;
    ComPtr<IWbemLocator> loc;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc));
    if (FAILED(hr)) return R::fail(ErrorCode::Unsupported, hr_text("WbemLocator", hr));
    ComPtr<IWbemServices> svc;
    // B2 / bosluk 6: bozuk/yavas WMI'da baglanti suresiz beklemesin (belgeli ust sinir ~2 dk).
    hr = loc->ConnectServer(_bstr_t(ns), nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &svc);
    if (hr == WBEM_E_ACCESS_DENIED || hr == E_ACCESSDENIED) return R::fail(ErrorCode::Denied, hr_text("ConnectServer", hr));
    if (FAILED(hr)) return R::fail(ErrorCode::Unsupported, hr_text("ConnectServer", hr));
    CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    ComPtr<IEnumWbemClassObject> en;
    hr = svc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(wql), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    if (hr == WBEM_E_ACCESS_DENIED) return R::fail(ErrorCode::Denied, hr_text("ExecQuery", hr));
    if (FAILED(hr)) return R::fail(ErrorCode::Unsupported, hr_text("ExecQuery", hr));
    std::vector<Row> rows;
    for (int guard = 0; guard < 1024; ++guard) {
        ComPtr<IWbemClassObject> obj;
        ULONG n = 0;
        hr = en->Next(5000, 1, &obj, &n);
        if (FAILED(hr)) return R::fail(ErrorCode::IoError, hr_text("Next", hr));
        if (hr == WBEM_S_TIMEDOUT) return R::fail(ErrorCode::IoError, "enumeration timed out");  // eksik liste tam sayilmaz
        if (n == 0) break;
        Row row;
        for (const auto& p : props) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(p.c_str(), 0, &v, nullptr, nullptr))) row[to_utf8(p)] = convert(v);
            VariantClear(&v);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace ef::platform::wmi
