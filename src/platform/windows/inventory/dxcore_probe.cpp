#include "platform/windows/inventory/dxcore_probe.h"

// initguid: DXCore GUID'lerinin TANIMI bu derleme biriminde (DECLSPEC_SELECTANY).
#include <initguid.h>
#include <dxcore_interface.h>
#include <wrl/client.h>

#include <cstdint>

namespace ef::platform::inventory {

using Microsoft::WRL::ComPtr;

std::optional<bool> dxcore_is_integrated(LUID luid) {
    using CreateFn = HRESULT(WINAPI*)(REFIID, void**);
    static HMODULE mod = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod) return std::nullopt;
    static auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(mod, "DXCoreCreateAdapterFactory")));
    if (!create) return std::nullopt;
    ComPtr<IDXCoreAdapterFactory> factory;
    if (FAILED(create(__uuidof(IDXCoreAdapterFactory), reinterpret_cast<void**>(factory.GetAddressOf())))) return std::nullopt;
    ComPtr<IDXCoreAdapterList> list;
    const GUID attrs[] = {DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS};
    if (FAILED(factory->CreateAdapterList(1, attrs, __uuidof(IDXCoreAdapterList), reinterpret_cast<void**>(list.GetAddressOf()))))
        return std::nullopt;
    const std::uint32_t n = list->GetAdapterCount();
    for (std::uint32_t i = 0; i < n; ++i) {
        ComPtr<IDXCoreAdapter> a;
        if (FAILED(list->GetAdapter(i, __uuidof(IDXCoreAdapter), reinterpret_cast<void**>(a.GetAddressOf())))) continue;
        LUID l{};
        if (!a->IsPropertySupported(DXCoreAdapterProperty::InstanceLuid) ||
            FAILED(a->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(l), &l)))
            continue;
        if (l.LowPart != luid.LowPart || l.HighPart != luid.HighPart) continue;
        if (!a->IsPropertySupported(DXCoreAdapterProperty::IsIntegrated)) return std::nullopt;
        bool integrated = false;
        if (FAILED(a->GetProperty(DXCoreAdapterProperty::IsIntegrated, sizeof(integrated), &integrated))) return std::nullopt;
        return integrated;
    }
    return std::nullopt;
}

}  // namespace ef::platform::inventory
