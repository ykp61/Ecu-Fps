#include "platform/windows/inventory/windows_features.h"

#include <windows.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <cstdio>

#include "platform/windows/inventory/field_json.h"
#include "platform/windows/win_env.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;

contracts::Field<bool> read_game_mode() {
    DWORD v = 0, sz = sizeof(v);
    const LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    if (st == ERROR_FILE_NOT_FOUND) return Field<bool>::unknown("not set (Windows default applies)");
    if (st != ERROR_SUCCESS) return Field<bool>::unknown("AutoGameModeEnabled read failed");
    return Field<bool>::known(v != 0, Provenance::Registry);
}

namespace {
// d3dkmthk.h ile ayni yerlesim (yalniz kullanilan alanlar).
struct KmtOpenFromLuid {
    LUID luid;
    UINT adapter;
};
struct KmtQueryInfo {
    UINT adapter;
    UINT type;
    void* data;
    UINT size;
};
struct KmtClose {
    UINT adapter;
};
constexpr UINT kQueryWddm27Caps = 70;  // KMTQAITYPE_WDDM_2_7_CAPS
using OpenFn = LONG(APIENTRY*)(KmtOpenFromLuid*);
using QueryFn = LONG(APIENTRY*)(const KmtQueryInfo*);
using CloseFn = LONG(APIENTRY*)(const KmtClose*);
}  // namespace

std::vector<HagsStatus> read_hags() {
    std::vector<HagsStatus> out;
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    if (!gdi) gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto open = reinterpret_cast<OpenFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid")));
    const auto query = reinterpret_cast<QueryFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo")));
    const auto close = reinterpret_cast<CloseFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTCloseAdapter")));
    Microsoft::WRL::ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return out;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i, a.Reset()) {
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d)) || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        HagsStatus h;
        char luid[40];
        std::snprintf(luid, sizeof luid, "%08lx:%08lx", static_cast<unsigned long>(d.AdapterLuid.HighPart),
                      static_cast<unsigned long>(d.AdapterLuid.LowPart));
        h.luid = luid;
        KmtOpenFromLuid o{d.AdapterLuid, 0};
        if (!open || !query || !close || open(&o) != 0) {
            h.supported = h.enabled = h.enabled_by_default = Field<bool>::unknown("D3DKMT query unavailable");
            out.push_back(h);
            continue;
        }
        UINT caps = 0;
        const KmtQueryInfo q{o.adapter, kQueryWddm27Caps, &caps, sizeof(caps)};
        if (query(&q) == 0) {
            h.supported = Field<bool>::known((caps & 1u) != 0, Provenance::Api);
            h.enabled = Field<bool>::known((caps & 2u) != 0, Provenance::Api);
            h.enabled_by_default = Field<bool>::known((caps & 4u) != 0, Provenance::Api);
        } else {
            h.supported = h.enabled = h.enabled_by_default = Field<bool>::unknown("driver older than WDDM 2.7");
        }
        const KmtClose c{o.adapter};
        close(&c);
        out.push_back(h);
    }
    return out;
}

core::json::Value windows_features_json() {
    auto o = core::json::Value::object();
    o.set("game_mode", field_json(read_game_mode()));
    auto arr = core::json::Value::array();
    for (const auto& h : read_hags()) {
        auto e = core::json::Value::object();
        e.set("luid", core::json::Value::string(h.luid));
        e.set("supported", field_json(h.supported));
        e.set("enabled", field_json(h.enabled));
        e.set("enabled_by_default", field_json(h.enabled_by_default));
        arr.push(std::move(e));
    }
    o.set("hags", std::move(arr));
    return o;
}

bool open_guided(core::guided::Action a) {
    const auto u = core::guided::uri(a);
    if (!core::guided::is_allowed_uri(u)) return false;
    const auto w = to_wide(u);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}  // namespace ef::platform::inventory
