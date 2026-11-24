#include "platform/windows/readiness/graphics_reader.h"

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <setupapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ctime>
#include <iterator>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include "nvapi.h"
#pragma warning(pop)

#include "core/adapters/windows/update_check.h"
#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/inventory/driver_info.h"
#include "platform/windows/vendor_runtime.h"
#include "platform/windows/win_env.h"

namespace ef::platform::readiness {

using Microsoft::WRL::ComPtr;
namespace cr = core::readiness;
namespace aw = core::adapters::windows;

namespace {

// Hinnant days_from_civil: takvim tarihi -> 1970'ten gun.
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::optional<unsigned> num(std::string_view s) {
    unsigned v = 0;
    if (s.empty()) return std::nullopt;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

std::optional<std::int64_t> to_unix(unsigned y, unsigned m, unsigned d) {
    if (y < 1980 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    return days_from_civil(y, m, d) * 86400;
}

std::string lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool has(std::string_view s, std::string_view p) { return s.find(p) != std::string_view::npos; }

// ---- DirectX ----

using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

// D3D12 cihazi olusturulabildiyse true (ozellik sorgulari ayrica nullopt kalabilir).
bool read_d3d12(IDXGIAdapter1* adapter, cr::GraphicsInfo& g) {
    static HMODULE mod = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod) return false;
    const auto create =
        reinterpret_cast<D3D12CreateDeviceFn>(reinterpret_cast<void*>(GetProcAddress(mod, "D3D12CreateDevice")));
    if (!create) return false;
    ComPtr<ID3D12Device> dev;
    if (FAILED(create(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) return false;

    // 12_2'yi tanimayan eski runtime tum sorguyu E_INVALIDARG ile reddeder: 12_2'siz bir kez daha dene.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    for (UINT skip = 0; skip < 2 && !g.d3d12_max_feature_level; ++skip) {
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
        fl.NumFeatureLevels = static_cast<UINT>(std::size(levels)) - skip;
        fl.pFeatureLevelsRequested = levels + skip;
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl))))
            g.d3d12_max_feature_level = static_cast<std::uint32_t>(fl.MaxSupportedFeatureLevel);
    }

    // En yuksekten asagi: eski runtime bilinmeyen degeri E_INVALIDARG ile reddeder.
    for (unsigned sm = 0x69; sm >= 0x60; --sm) {
        D3D12_FEATURE_DATA_SHADER_MODEL d{static_cast<D3D_SHADER_MODEL>(sm)};
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &d, sizeof(d)))) {
            g.shader_model = static_cast<std::uint32_t>(d.HighestShaderModel);
            break;
        }
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5))))
        g.raytracing_tier = static_cast<std::uint32_t>(o5.RaytracingTier);
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6{};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o6, sizeof(o6))))
        g.vrs_tier2 = o6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7)))) {
        g.mesh_shaders = o7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
        g.sampler_feedback = o7.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9;
    }
    // OPTIONS7 sorgusu basarisizsa mesh/sampler feedback dogrulanamadi: nullopt kalir.
    return true;
}

// D3D12 cihazi olusmadiysa yedek: D3D11'in bu adaptorde verdigi en yuksek ozellik duzeyi.
// ppDevice = nullptr: cihaz olusturulmaz, yalniz desteklenen duzey dondurulur.
void read_d3d11(IDXGIAdapter1* adapter, cr::GraphicsInfo& g) {
    static HMODULE mod = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod) return;
    const auto create =
        reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(mod, "D3D11CreateDevice")));
    if (!create) return;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                        D3D_FEATURE_LEVEL_9_1};
    // 11_1'i tanimayan eski runtime E_INVALIDARG dondurur: 11_1'siz bir kez daha dene.
    for (UINT skip = 0; skip < 2; ++skip) {
        D3D_FEATURE_LEVEL got{};
        const HRESULT hr = create(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels + skip,
                                  static_cast<UINT>(std::size(levels)) - skip, D3D11_SDK_VERSION, nullptr, &got,
                                  nullptr);
        if (SUCCEEDED(hr)) {
            g.d3d11_feature_level = static_cast<std::uint32_t>(got);
            return;
        }
        if (hr != E_INVALIDARG) return;
    }
}

// Dosya var mi: true/false; erisim hatasi gibi belirsiz durumda nullopt.
std::optional<bool> file_exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
    const DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return false;
    return std::nullopt;
}

std::optional<bool> read_dx9_legacy() {
    // GetSystemDirectoryW 64 bit surecte gercek System32'dir; 32 bit surecte WOW64 yonlendirmesi onu
    // SysWOW64'e cevirir ve iki kopya ayirt edilemez. Urun yalniz x64 dagitilir.
    static_assert(sizeof(void*) == 8, "read_dx9_legacy assumes a 64-bit process (System32 vs SysWOW64)");
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::nullopt;
    auto sys = file_exists(std::wstring(dir, n) + L"\\d3dx9_43.dll");
    if (!sys) return std::nullopt;
    n = GetSystemWow64DirectoryW(dir, MAX_PATH);
    if (n == 0) {
        if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) return sys;  // 32 bit Windows
        return std::nullopt;
    }
    if (n >= MAX_PATH) return std::nullopt;
    auto wow = file_exists(std::wstring(dir, n) + L"\\d3dx9_43.dll");
    if (!wow) return std::nullopt;
    return *sys && *wow;  // eski oyunlarin cogu 32 bit: iki kopya da gerekir
}

// ---- NVAPI VRR (G-SYNC) ----
// Kimlikler third_party/nvapi/nvapi_interface.h ile ayni (tablo baska TU'da tanimli, iki kez eklenemez).
constexpr unsigned kNvInitialize = 0x0150e828;
constexpr unsigned kNvUnload = 0xd22bdd7e;
constexpr unsigned kNvGetDisplayIdByDisplayName = 0xae457190;
constexpr unsigned kNvDispGetVRRInfo = 0xdf8fda57;

std::optional<NvVrrRaw> read_nvidia_vrr(const std::string& gdi_name) {
    if (gdi_name.empty()) return std::nullopt;
    auto rt = load_vendor_runtime(core::vendors::Vendor::Nvidia);
    if (!rt.module) {
        unload_vendor_runtime(rt);
        return std::nullopt;
    }
    using QueryFn = void*(__cdecl*)(unsigned int);
    const auto query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(rt.module), "nvapi_QueryInterface")));
    std::optional<NvVrrRaw> out;
    if (query) {
        const auto init = reinterpret_cast<NvAPI_Status(__cdecl*)()>(query(kNvInitialize));
        const auto unload = reinterpret_cast<NvAPI_Status(__cdecl*)()>(query(kNvUnload));
        const auto by_name =
            reinterpret_cast<NvAPI_Status(__cdecl*)(const char*, NvU32*)>(query(kNvGetDisplayIdByDisplayName));
        const auto vrr = reinterpret_cast<NvAPI_Status(__cdecl*)(NvU32, NV_GET_VRR_INFO*)>(query(kNvDispGetVRRInfo));
        if (init && by_name && vrr && init() == NVAPI_OK) {
            NvU32 id = 0;
            if (by_name(gdi_name.c_str(), &id) == NVAPI_OK) {
                NV_GET_VRR_INFO info{};
                info.version = NV_GET_VRR_INFO_VER;
                if (vrr(id, &info) == NVAPI_OK) {
                    NvVrrRaw r;
                    r.display = gdi_name;
                    r.enabled = info.bIsVRREnabled != 0;
                    r.possible = info.bIsVRRPossible != 0;
                    r.requested = info.bIsVRRRequested != 0;
                    r.indicator = info.bIsVRRIndicatorEnabled != 0;
                    r.in_vrr_mode = info.bIsDisplayInVRRMode != 0;
                    out = r;
                }
            }
            if (unload) unload();
        }
    }
    unload_vendor_runtime(rt);
    return out;
}

// Adaptore bagli birincil ekranin GDI adi; birincil bu adaptorde degilse ilk cikis.
std::string adapter_display_name(IDXGIAdapter1* adapter) {
    const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    std::string first;
    ComPtr<IDXGIOutput> out;
    for (UINT i = 0; adapter->EnumOutputs(i, &out) != DXGI_ERROR_NOT_FOUND && i < 16; ++i) {
        DXGI_OUTPUT_DESC d{};
        if (out && SUCCEEDED(out->GetDesc(&d)) && d.AttachedToDesktop) {
            const auto name = to_utf8(d.DeviceName);
            if (d.Monitor == primary) return name;
            if (first.empty()) first = name;
        }
        out.Reset();
    }
    return first;
}

// ---- Yazilim (Uninstall) ----

// "14.40.33810.0" -> {14, 40, 33810, 0}; sayisal olmayan parca varsa nullopt.
std::optional<std::vector<unsigned long long>> version_parts(std::string_view v) {
    std::vector<unsigned long long> out;
    std::size_t start = 0;
    for (;;) {
        const auto dot = v.find('.', start);
        const auto part = v.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        unsigned long long x = 0;
        if (part.empty() || part.size() > 18) return std::nullopt;
        const auto r = std::from_chars(part.data(), part.data() + part.size(), x);
        if (r.ec != std::errc{} || r.ptr != part.data() + part.size()) return std::nullopt;
        out.push_back(x);
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return out;
}

std::optional<std::wstring> reg_string(HKEY k, const wchar_t* name) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return std::nullopt;
    if ((type != REG_SZ && type != REG_EXPAND_SZ) || size == 0 || size > 8192) return std::nullopt;
    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &size) != ERROR_SUCCESS)
        return std::nullopt;
    buf.resize(wcsnlen(buf.c_str(), buf.size()));
    return buf;
}

void scan_uninstall(HKEY root, REGSAM view, std::vector<cr::SoftwareItem>& items) {
    HKEY base = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &base) !=
        ERROR_SUCCESS)
        return;
    wchar_t sub[256];
    for (DWORD i = 0; i < 4096; ++i) {
        DWORD len = 256;
        const LONG r = RegEnumKeyExW(base, i, sub, &len, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        HKEY k = nullptr;
        if (RegOpenKeyExW(base, sub, 0, KEY_READ | view, &k) != ERROR_SUCCESS) continue;
        if (const auto dn = reg_string(k, L"DisplayName")) {
            const auto canon = canonical_software_name(to_utf8(*dn));
            if (!canon.empty()) {
                cr::SoftwareItem it;
                it.name = canon;
                if (auto v = reg_string(k, L"DisplayVersion")) it.version = to_utf8(*v);
                if (auto d = reg_string(k, L"InstallDate")) it.date_unix = install_date_to_unix(to_utf8(*d));
                // Ayni urun birden cok gorunumde: en yuksek/bilgisi en dolu kayit kalir.
                merge_software_item(items, std::move(it));
            }
        }
        RegCloseKey(k);
    }
    RegCloseKey(base);
}

// Ekran sinifi (GUID_DEVCLASS_DISPLAY) mevcut aygitlarinin hizmet adlari (SPDRP_SERVICE). Salt okuma.
// Hizmeti olmayan aygit bos dizgi olarak eklenir. Sinif listesi acilamazsa nullopt.
std::optional<std::vector<std::string>> read_display_services() {
    // {4d36e968-e325-11ce-bfc1-08002be10318} = GUID_DEVCLASS_DISPLAY (initguid tanim cakismasi olmasin diye yerel).
    static constexpr GUID kDisplayClass = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    HDEVINFO set = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return std::nullopt;
    std::vector<std::string> out;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; i < 32 && SetupDiEnumDeviceInfo(set, i, &d); ++i) {
        wchar_t buf[256] = {};
        DWORD type = 0;
        if (SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_SERVICE, &type, reinterpret_cast<PBYTE>(buf),
                                              sizeof(buf) - sizeof(wchar_t), nullptr) &&
            type == REG_SZ)
            out.push_back(to_utf8(buf));
        else
            out.emplace_back();
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

}  // namespace

std::optional<bool> basic_display_only(const std::optional<std::vector<std::string>>& display_services) {
    if (!display_services) return std::nullopt;
    if (display_services->empty()) return false;
    for (const auto& s : *display_services)
        if (lower(s) != "basicdisplay") return false;
    return true;
}

std::string graphics_vendor_key(std::optional<std::uint32_t> hw_vendor_id, std::optional<bool> basic_only) {
    if (hw_vendor_id && *hw_vendor_id != 0x1414) {
        switch (aw::vendor_from_pci(*hw_vendor_id)) {
            case aw::GpuVendor::Nvidia: return "nvidia";
            case aw::GpuVendor::Amd: return "amd";
            case aw::GpuVendor::Intel: return "intel";
            case aw::GpuVendor::Other: return "other";
        }
        return "other";
    }
    if (basic_only && *basic_only) return "basic_display";
    if (hw_vendor_id) return "microsoft";
    return {};
}

std::optional<std::int64_t> smbios_date_to_unix(std::string_view s) {
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    const auto a = s.find('/');
    const auto b = a == std::string_view::npos ? a : s.find('/', a + 1);
    if (b == std::string_view::npos) return std::nullopt;
    const auto m = num(s.substr(0, a)), d = num(s.substr(a + 1, b - a - 1));
    const auto ystr = s.substr(b + 1);
    auto y = num(ystr);
    if (!m || !d || !y) return std::nullopt;
    if (ystr.size() == 2) *y += *y < 80 ? 2000 : 1900;  // SMBIOS 2.3 oncesi iki haneli yil
    else if (ystr.size() != 4) return std::nullopt;
    return to_unix(*y, *m, *d);
}

std::optional<std::int64_t> install_date_to_unix(std::string_view s) {
    if (s.size() != 8) return std::nullopt;
    const auto y = num(s.substr(0, 4)), m = num(s.substr(4, 2)), d = num(s.substr(6, 2));
    if (!y || !m || !d) return std::nullopt;
    return to_unix(*y, *m, *d);
}

bool software_version_less(std::string_view a, std::string_view b) {
    const auto pa = version_parts(a), pb = version_parts(b);
    if (pa && pb) {
        // Eksik parca 0 sayilir: "1.2" == "1.2.0".
        const std::size_t n = std::max(pa->size(), pb->size());
        for (std::size_t i = 0; i < n; ++i) {
            const auto x = i < pa->size() ? (*pa)[i] : 0ULL, y = i < pb->size() ? (*pb)[i] : 0ULL;
            if (x != y) return x < y;
        }
        return false;
    }
    return a < b;  // sayisal degilse sozluk sirasi (kararli yedek)
}

void merge_software_item(std::vector<cr::SoftwareItem>& items, cr::SoftwareItem it) {
    auto same = std::find_if(items.begin(), items.end(), [&](const auto& x) { return x.name == it.name; });
    if (same == items.end()) {
        items.push_back(std::move(it));
        return;
    }
    const bool equal = !software_version_less(same->version, it.version) &&
                       !software_version_less(it.version, same->version);
    if (same->version.empty() || (!it.version.empty() && software_version_less(same->version, it.version)) ||
        (equal && !same->date_unix && it.date_unix))
        *same = std::move(it);
}

std::string canonical_software_name(std::string_view display_name) {
    const auto n = lower(display_name);
    if (has(n, "visual c++") && has(n, "redistributable")) {
        const bool v14 = has(n, "v14") || has(n, "2015") || has(n, "2017") || has(n, "2019") || has(n, "2022") ||
                         has(n, "2026");
        if (!v14) return {};
        if (has(n, "x64")) return "Microsoft Visual C++ v14 Redistributable (x64)";
        if (has(n, "x86")) return "Microsoft Visual C++ v14 Redistributable (x86)";
        return {};
    }
    if (starts(n, "nvidia app")) return "NVIDIA App";
    if (starts(n, "nvidia geforce experience")) return "NVIDIA GeForce Experience";
    if (starts(n, "amd software")) return "AMD Software: Adrenalin Edition";
    if (starts(n, "intel(r) graphics software") || starts(n, "intel graphics software")) return "Intel Graphics Software";
    if (has(n, "arc control") && has(n, "intel")) return "Intel Arc Control";
    if (n == "steam") return "Steam";
    if (starts(n, "epic games launcher")) return "Epic Games Launcher";
    if (n == "battle.net") return "Battle.net";
    if (n == "ea app") return "EA app";
    if (starts(n, "ubisoft connect")) return "Ubisoft Connect";
    if (n == "riot client") return "Riot Client";
    return {};
}

cr::GraphicsInfo read_graphics(std::optional<NvVrrRaw>* raw) {
    cr::GraphicsInfo g;
    g.now_unix = static_cast<std::int64_t>(std::time(nullptr));
    g.dx9_legacy_runtime = read_dx9_legacy();

    ComPtr<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return g;
    ComPtr<IDXGIAdapter1> chosen;
    DXGI_ADAPTER_DESC1 desc{};
    ComPtr<IDXGIFactory6> f6;
    if (SUCCEEDED(f1.As(&f6))) {
        ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; i < 16 && SUCCEEDED(f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                            IID_PPV_ARGS(&a)));
             ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                chosen = a;
                desc = d;
                break;
            }
            a.Reset();
        }
    }
    if (!chosen) {
        ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; i < 16 && f1->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                chosen = a;
                desc = d;
                break;
            }
            a.Reset();
        }
    }
    // Surucusuz bilgisayarda DXGI cogunlukla yalniz yazilim bayrakli "Basic Render Driver"i listeler (yukarida
    // elendi) -> chosen bos kalir. Yazilim olmayan 0x1414 adaptor ise RDP/Hyper-V/dolayli ekran olabilir.
    // Her iki durumda "surucu yok" yalniz SetupAPI kanitiyla (tum ekran aygitlari BasicDisplay) soylenir.
    const bool microsoft_or_none = !chosen || desc.VendorId == 0x1414;
    const std::optional<bool> basic_only =
        microsoft_or_none ? basic_display_only(read_display_services()) : std::optional<bool>{};
    g.vendor = graphics_vendor_key(chosen ? std::optional<std::uint32_t>(desc.VendorId) : std::nullopt, basic_only);
    if (!chosen) return g;

    g.gpu_name = to_utf8(desc.Description);

    // Surucu: SetupAPI (WIN-07 okuyucusu). Ayni ureticide ad eslesmesi tercih edilir.
    const auto drivers = inventory::read_display_drivers();
    const aw::DriverInfo* pick = nullptr;
    for (const auto& d : drivers) {
        if (d.vendor_id != desc.VendorId) continue;
        if (!pick || d.device_name == g.gpu_name) pick = &d;
        if (d.device_name == g.gpu_name) break;
    }
    if (pick) {
        if (pick->version.has()) g.driver_version = *pick->version.value;
        if (pick->date_unix_days.has()) g.driver_date_unix = *pick->date_unix_days.value * 86400;
    }

    if (!read_d3d12(chosen.Get(), g)) read_d3d11(chosen.Get(), g);

    if (g.vendor == "nvidia") {
        if (const auto v = read_nvidia_vrr(adapter_display_name(chosen.Get()))) {
            // bIsVRREnabled yalniz VRR o an etkinken (tam ekran sunum) 1 olabilir; masaustunde G-SYNC acik bir
            // ekranda enabled=0, in_vrr_mode=1 gozlendi (RTX 5080, 621.01). Yanlis "kapali" uyarisini onlemek
            // icin ikisinden biri yeterli sayilir.
            g.gsync_enabled = v->enabled || v->in_vrr_mode;
            g.gsync_possible = v->possible;
            if (raw) *raw = v;
        }
    }
    return g;
}

cr::SoftwareInfo read_software() {
    cr::SoftwareInfo s;
    s.now_unix = static_cast<std::int64_t>(std::time(nullptr));
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (const REGSAM view : {static_cast<REGSAM>(KEY_WOW64_64KEY), static_cast<REGSAM>(KEY_WOW64_32KEY)})
            scan_uninstall(root, view, s.items);
    std::sort(s.items.begin(), s.items.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    // .NET (salt okuma). 64 bit gorunum: surec x64.
    if (HKEY k = nullptr; RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64\\sharedfx\\Microsoft.WindowsDesktop.App", 0,
                                        KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        wchar_t name[128];
        for (DWORD i = 0; i < 256; ++i) {
            DWORD len = 128;
            const LONG r = RegEnumValueW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS) continue;
            const auto ver = to_utf8(std::wstring(name, len));
            if (version_parts(ver)) s.dotnet_desktop_versions.push_back(ver);
        }
        RegCloseKey(k);
    }
    if (HKEY k = nullptr; RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full", 0,
                                        KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        DWORD rel = 0, size = sizeof(rel), type = 0;
        if (RegQueryValueExW(k, L"Release", nullptr, &type, reinterpret_cast<LPBYTE>(&rel), &size) == ERROR_SUCCESS && type == REG_DWORD)
            s.dotnet_framework_release = rel;
        RegCloseKey(k);
    }

    const auto b = inventory::read_board();
    if (b.bios_date.has()) s.bios_date_unix = smbios_date_to_unix(*b.bios_date.value);
    if (b.board_vendor.has()) s.board_vendor = *b.board_vendor.value;
    if (b.board_product.has()) s.board_model = *b.board_product.value;
    return s;
}

}  // namespace ef::platform::readiness
