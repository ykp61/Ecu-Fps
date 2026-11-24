// B5 (ADR-040 m.5): kesme (MSI) durumu okuyucusu. YALNIZ OKUMA (KEY_READ); hicbir deger yazilmaz, hicbir aygit
// etkinlestirilip/devre disi birakilmaz. Okunamayan alan nullopt/readable=false kalir; varsayilan uydurulmaz.
#include "platform/windows/readiness/interrupt_reader.h"

#include <windows.h>
#include <dxgi1_6.h>
#include <setupapi.h>
#include <wrl/client.h>

#include <cctype>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "platform/windows/readiness/network_reader.h"
#include "platform/windows/win_env.h"

namespace ef::platform::readiness {

using Microsoft::WRL::ComPtr;
namespace cr = core::readiness;

namespace {

constexpr wchar_t kEnumRoot[] = L"SYSTEM\\CurrentControlSet\\Enum\\";
constexpr wchar_t kMsiKey[] = L"Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties";
constexpr wchar_t kAffinityKey[] = L"Device Parameters\\Interrupt Management\\Affinity Policy";

bool not_found(LSTATUS st) { return st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND; }

// Ornegin Enum anahtari altindan MSISupported / DevicePriority okur (salt okuma).
cr::InterruptDeviceInfo read_instance(cr::InterruptDeviceRole role, std::string name, const std::wstring& instance_id) {
    cr::InterruptDeviceInfo d;
    d.role = role;
    d.name = std::move(name);
    if (instance_id.empty()) return d;  // readable=false
    HKEY inst = nullptr;
    const std::wstring path = std::wstring(kEnumRoot) + instance_id;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &inst) != ERROR_SUCCESS) return d;
    DWORD v = 0, sz = sizeof(v);
    const LSTATUS st = RegGetValueW(inst, kMsiKey, L"MSISupported", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    if (st == ERROR_SUCCESS) {
        d.readable = true;
        d.msi_supported = static_cast<std::uint32_t>(v);
    } else if (not_found(st)) {
        d.readable = true;  // ornek okundu; aygit MSI ayari bildirmiyor
    }                       // baska hata (erisim, tur): readable=false -> "veri yetersiz"
    if (d.readable) {
        DWORD pv = 0, psz = sizeof(pv);
        if (RegGetValueW(inst, kAffinityKey, L"DevicePriority", RRF_RT_REG_DWORD, nullptr, &pv, &psz) == ERROR_SUCCESS)
            d.priority = static_cast<std::uint32_t>(pv);
    }
    RegCloseKey(inst);
    return d;
}

// Oyun GPU'su: yuksek performansli (yazilim olmayan) DXGI adaptoru; graphics_reader ile ayni secim kurali.
bool pick_game_gpu(DXGI_ADAPTER_DESC1& out) {
    ComPtr<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return false;
    ComPtr<IDXGIFactory6> f6;
    if (SUCCEEDED(f1.As(&f6))) {
        ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; i < 16 && SUCCEEDED(f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a))); ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                out = d;
                return true;
            }
            a.Reset();
        }
    }
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; i < 16 && f1->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            out = d;
            return true;
        }
        a.Reset();
    }
    return false;
}

// SetupAPI ekran sinifi (mevcut aygitlar): donanim kimligi PCI uretici/aygit ile eslesen ilk aygitin ornek kimligi.
std::wstring display_instance_for(std::uint32_t vendor, std::uint32_t device) {
    static constexpr GUID kDisplayClass = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    HDEVINFO set = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; i < 32 && found.empty() && SetupDiEnumDeviceInfo(set, i, &d); ++i) {
        wchar_t ids[1024] = {};
        DWORD type = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, &type, reinterpret_cast<PBYTE>(ids), sizeof(ids) - 2 * sizeof(wchar_t),
                                               nullptr) ||
            type != REG_MULTI_SZ)
            continue;
        bool match = false;
        for (const wchar_t* p = ids; *p && !match; p += wcslen(p) + 1) match = hardware_id_matches_pci(to_utf8(p), vendor, device);
        if (!match) continue;
        wchar_t inst[512] = {};
        if (SetupDiGetDeviceInstanceIdW(set, &d, inst, static_cast<DWORD>(std::size(inst)), nullptr)) found = inst;
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

// Etkin ag bagdastiricisinin PnP ornek kimligi: Control\Network\{4D36E972-...}\{GUID}\Connection\PnpInstanceID.
std::wstring network_instance_for(const std::string& interface_guid) {
    if (interface_guid.empty()) return {};
    const std::wstring path = L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\" + to_wide(interface_guid) + L"\\Connection";
    wchar_t buf[512] = {};
    DWORD sz = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), L"PnpInstanceID", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &sz) != ERROR_SUCCESS) return {};
    return buf;
}

}  // namespace

bool hardware_id_matches_pci(std::string_view hardware_id, std::uint32_t vendor_id, std::uint32_t device_id) {
    char want[40];
    std::snprintf(want, sizeof want, "PCI\\VEN_%04X&DEV_%04X", vendor_id & 0xFFFFu, device_id & 0xFFFFu);
    std::string up(hardware_id);
    for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const std::string w(want);
    if (up.compare(0, w.size(), w) != 0) return false;
    return up.size() == w.size() || up[w.size()] == '&' || up[w.size()] == '\\';
}

std::vector<cr::InterruptDeviceInfo> read_interrupt_devices() {
    std::vector<cr::InterruptDeviceInfo> out;
    DXGI_ADAPTER_DESC1 gpu{};
    if (pick_game_gpu(gpu) && gpu.VendorId != 0x1414) {  // 0x1414: Microsoft (RDP/temel goruntu); gercek GPU degil
        out.push_back(read_instance(cr::InterruptDeviceRole::GameGpu, to_utf8(gpu.Description), display_instance_for(gpu.VendorId, gpu.DeviceId)));
    }
    const auto net = read_network(0);  // hiz olcumu yok (rx_sample_ms = 0)
    if (net.if_index != 0)
        out.push_back(read_instance(cr::InterruptDeviceRole::ActiveNetwork, net.adapter_description, network_instance_for(net.interface_guid)));
    return out;
}

}  // namespace ef::platform::readiness
