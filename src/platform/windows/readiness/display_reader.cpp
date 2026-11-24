#include "platform/windows/readiness/display_reader.h"

#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <optional>

#include "core/inventory/power_security.h"
#include "platform/windows/inventory/power_security_win.h"
#include "platform/windows/inventory/windows_features.h"
#include "platform/windows/win_env.h"

namespace ef::platform::readiness {

namespace cr = core::readiness;
namespace ci = core::inventory;

using Microsoft::WRL::ComPtr;

namespace {

// ---------------- Ekran (CCD API, Microsoft Learn: QueryDisplayConfig / DisplayConfigGetDeviceInfo) ----------------

// F-44: etkin yolun adaptor LUID'sini GPU adi/ureticisiyle eslemek icin hafif adaptor listesi.
// Yazilim adaptorleri (Basic Render Driver) atlanir; ayri GPU sayilmaz.
struct AdapterLite {
    LUID luid{};
    std::string name;
    std::string vendor;
    bool high_perf = false;
};

std::string adapter_vendor_key(std::uint32_t vendor_id) {
    switch (vendor_id) {
        case 0x10de: return "nvidia";
        case 0x1002:
        case 0x1022: return "amd";
        case 0x8086: return "intel";
        case 0x1414: return "microsoft";
        default: return "other";
    }
}

bool same_luid(const LUID& a, const LUID& b) {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

std::vector<AdapterLite> read_adapters() {
    std::vector<AdapterLite> out;
    ComPtr<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return out;

    // EnumAdapterByGpuPreference(HIGH_PERFORMANCE) sirasindaki ilk yazilim-olmayan adaptor "yuksek performans".
    LUID high_perf{};
    bool have_high_perf = false;
    ComPtr<IDXGIFactory6> f6;
    if (SUCCEEDED(f1.As(&f6))) {
        ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; i < 16 && SUCCEEDED(f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                            IID_PPV_ARGS(&a)));
             ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                high_perf = d.AdapterLuid;
                have_high_perf = true;
                break;
            }
            a.Reset();
        }
    }

    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; i < 16 && f1->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            AdapterLite x;
            x.luid = d.AdapterLuid;
            x.name = to_utf8(d.Description);
            x.vendor = adapter_vendor_key(d.VendorId);
            // Factory6 yoksa ilk yazilim-olmayan adaptor yuksek performans kabul edilir (DXGI varsayilani).
            x.high_perf = have_high_perf ? same_luid(d.AdapterLuid, high_perf) : out.empty();
            out.push_back(std::move(x));
        }
        a.Reset();
    }
    return out;
}

}  // namespace

GpuPreferenceInfo read_gpu_preference() {
    GpuPreferenceInfo out;
    for (const auto& a : read_adapters()) {
        ++out.hardware_adapters;
        if (a.high_perf && out.high_perf_luid.empty()) {
            char luid[24];
            std::snprintf(luid, sizeof luid, "%08lx:%08lx", static_cast<unsigned long>(a.luid.HighPart), static_cast<unsigned long>(a.luid.LowPart));
            out.high_perf_luid = luid;
        }
    }
    // P6: guc tasarrufu tercihindeki ilk donanim karti (hibritte tumlesik GPU).
    ComPtr<IDXGIFactory1> f1;
    ComPtr<IDXGIFactory6> f6;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f1))) && SUCCEEDED(f1.As(&f6))) {
        ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; i < 16 && SUCCEEDED(f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_MINIMUM_POWER, IID_PPV_ARGS(&a))); ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                char luid[24];
                std::snprintf(luid, sizeof luid, "%08lx:%08lx", static_cast<unsigned long>(d.AdapterLuid.HighPart),
                              static_cast<unsigned long>(d.AdapterLuid.LowPart));
                out.min_power_luid = luid;
                break;
            }
            a.Reset();
        }
    }
    return out;
}

namespace {

const AdapterLite* find_adapter(const std::vector<AdapterLite>& adapters, const LUID& luid) {
    for (const auto& a : adapters)
        if (same_luid(a.luid, luid)) return &a;
    return nullptr;
}

// Eski SDK basliklarinda tanimli olmayabilir (NTDDI_VERSION Windows 10 tabani); degerler wingdi.h 10.0.26100 ile ayni.
constexpr UINT32 kQdcVirtualModeAware = 0x00000010;          // QDC_VIRTUAL_MODE_AWARE (Windows 10+)
constexpr UINT32 kQdcVirtualRefreshRateAware = 0x00000040;   // QDC_VIRTUAL_REFRESH_RATE_AWARE (Windows 11+)
constexpr UINT32 kPathSupportVirtualMode = 0x00000008;       // DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE
constexpr UINT32 kPathBoostRefreshRate = 0x00000010;         // DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE ("Dinamik yenileme hizi")
constexpr UINT32 kInfoGetAdvancedColor2 = 15;                // DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2 (Windows 11 24H2+)
constexpr UINT32 kColorModeHdr = 2;                          // DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR

// DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 ile ayni yerlesim (NTDDI_WIN11_GA kosullu basliktan bagimsiz).
struct AdvancedColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;  // bit0 advancedColorSupported, bit1 advancedColorActive, bit3 limitedByPolicy,
                   // bit4 highDynamicRangeSupported, bit5 highDynamicRangeUserEnabled, bit6/7 wide color
    DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
    UINT32 bitsPerColorChannel;
    UINT32 activeColorMode;  // 0 SDR, 1 WCG, 2 HDR
};
static_assert(sizeof(AdvancedColorInfo2) == sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER) + 16, "layout");

std::optional<double> rational(const DISPLAYCONFIG_RATIONAL& r) {
    if (r.Denominator == 0 || r.Numerator == 0) return std::nullopt;
    return static_cast<double>(r.Numerator) / static_cast<double>(r.Denominator);
}

LONG query_paths(UINT32 flags, std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    LONG r = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 4; ++attempt) {
        UINT32 pc = 0, mc = 0;
        r = GetDisplayConfigBufferSizes(flags, &pc, &mc);
        if (r != ERROR_SUCCESS) return r;
        paths.resize(pc);
        modes.resize(mc);
        r = QueryDisplayConfig(flags, &pc, paths.data(), &mc, modes.data(), nullptr);
        if (r == ERROR_INSUFFICIENT_BUFFER) continue;
        if (r == ERROR_SUCCESS) {
            paths.resize(pc);
            modes.resize(mc);
        }
        return r;
    }
    return r;
}

// Yol icin kaynak/hedef mod indeksleri; QDC_VIRTUAL_MODE_AWARE kullanildiysa ve yol sanal modu destekliyorsa
// 16 bitlik alanlar gecerlidir (DISPLAYCONFIG_PATH_SOURCE_INFO / _TARGET_INFO belgeleri).
void mode_indices(const DISPLAYCONFIG_PATH_INFO& p, bool virtual_mode_aware, UINT32& src, UINT32& tgt) {
    if (virtual_mode_aware && (p.flags & kPathSupportVirtualMode)) {
        src = p.sourceInfo.sourceModeInfoIdx;
        tgt = p.targetInfo.targetModeInfoIdx;
        if (src == 0xffff) src = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        if (tgt == 0xffff) tgt = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    } else {
        src = p.sourceInfo.modeInfoIdx;
        tgt = p.targetInfo.modeInfoIdx;
    }
}

// Ayni GDI aygitinda, ayni cozunurlukte (dondurulmus ekran icin w/h ters de kabul) desteklenen en yuksek Hz.
// EnumDisplaySettingsExW varsayilan bayraklarla yalniz monitorun destekledigi modlari listeler.
std::optional<double> max_refresh_same_res(const wchar_t* gdi, std::uint32_t w, std::uint32_t h) {
    if (!gdi || !*gdi || !w || !h) return std::nullopt;
    DWORD best = 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsExW(gdi, i, &dm, 0); ++i) {
        const bool same = (dm.dmPelsWidth == w && dm.dmPelsHeight == h) || (dm.dmPelsWidth == h && dm.dmPelsHeight == w);
        const bool interlaced = (dm.dmFields & DM_DISPLAYFLAGS) && (dm.dmDisplayFlags & DM_INTERLACED);
        if (same && !interlaced && dm.dmDisplayFrequency > 1 && dm.dmDisplayFrequency > best) best = dm.dmDisplayFrequency;
        dm = DEVMODEW{};
        dm.dmSize = sizeof(dm);
        if (i > 20000) break;  // savunma: sonsuz liste yok
    }
    if (best == 0) return std::nullopt;
    return static_cast<double>(best);
}

void read_hdr(const DISPLAYCONFIG_PATH_INFO& p, cr::DisplayInfo& d) {
    AdvancedColorInfo2 a2{};
    a2.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(kInfoGetAdvancedColor2);
    a2.header.size = sizeof(a2);
    a2.header.adapterId = p.targetInfo.adapterId;
    a2.header.id = p.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&a2.header) == ERROR_SUCCESS) {
        d.hdr_supported = (a2.value & (1u << 4)) != 0;
        d.hdr_enabled = a2.activeColorMode == kColorModeHdr;
        return;
    }
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO a1{};
    a1.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    a1.header.size = sizeof(a1);
    a1.header.adapterId = p.targetInfo.adapterId;
    a1.header.id = p.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&a1.header) == ERROR_SUCCESS) {
        // Eski yapi (24H2 oncesi): "advanced color" HDR ya da zorlanmis genis renk (ACM/WCG) olabilir ve
        // advancedColorSupported HDR destegini ayirt etmez. Yalniz "acik ve zorlanmis genis renk degil" durumu
        // HDR'nin acik (dolayisiyla destekli) oldugunu kanitlar; diger durumlarda destek bilinmez.
        const bool hdr_active = a1.advancedColorEnabled != 0 && a1.wideColorEnforced == 0;
        d.hdr_enabled = hdr_active;
        if (hdr_active) d.hdr_supported = true;
        else d.hdr_supported.reset();
    }
}

// ---------------- Kayit defteri yardimcilari ----------------

// nullopt + found=false: deger yok; nullopt + found=true: okuma hatasi.
std::optional<std::uint32_t> reg_dword(HKEY root, const wchar_t* key, const wchar_t* name, bool& error) {
    DWORD v = 0, sz = sizeof(v);
    const LSTATUS st = RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    if (st == ERROR_SUCCESS) return static_cast<std::uint32_t>(v);
    if (st != ERROR_FILE_NOT_FOUND && st != ERROR_PATH_NOT_FOUND) error = true;
    return std::nullopt;
}

std::optional<std::wstring> reg_sz(HKEY root, const wchar_t* key, const wchar_t* name, bool& error) {
    DWORD sz = 0;
    LSTATUS st = RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &sz);
    if (st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND) return std::nullopt;
    if (st != ERROR_SUCCESS || sz == 0 || sz > 65536) {
        error = true;
        return std::nullopt;
    }
    std::wstring s(sz / sizeof(wchar_t), L'\0');
    st = RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, s.data(), &sz);
    if (st != ERROR_SUCCESS) {
        error = true;
        return std::nullopt;
    }
    s.resize(std::wcslen(s.c_str()));
    return s;
}

std::string lower_trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string out(s.substr(a, b - a));
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

// ---------------- Saf yardimcilar ----------------

std::optional<bool> parse_dx_user_setting(std::string_view settings, std::string_view key) {
    const std::string want = lower_trim(key);
    std::optional<bool> result;
    std::size_t pos = 0;
    while (pos <= settings.size()) {
        std::size_t end = settings.find(';', pos);
        if (end == std::string_view::npos) end = settings.size();
        const auto item = settings.substr(pos, end - pos);
        const auto eq = item.find('=');
        if (eq != std::string_view::npos && lower_trim(item.substr(0, eq)) == want) {
            const std::string v = lower_trim(item.substr(eq + 1));
            if (v == "1") result = true;
            else if (v == "0") result = false;
            else result = std::nullopt;  // tanimsiz deger: tahmin yok; sonraki tekrar varsa o gecerli
        }
        if (end == settings.size()) break;
        pos = end + 1;
    }
    return result;
}

std::optional<bool> game_dvr_background_from(std::optional<std::uint32_t> policy_allow, std::optional<std::uint32_t> app_capture,
                                             std::optional<std::uint32_t> dvr_enabled, std::optional<std::uint32_t> historical) {
    if (policy_allow && *policy_allow == 0) return false;  // grup ilkesi tum Game DVR'i kapatir
    // "Olanlari kaydet" varsayilan olarak kapali; deger yoksa kapali (Settings > Oyun > Yakalamalar).
    if (!historical || *historical == 0) return false;
    // Iki ana yakalama anahtari normalde aynalanir. Ikisi de kayitli ama celisiyorsa hangisinin gecerli oldugu
    // bilinmez: tahmin yok.
    if (app_capture && dvr_enabled && ((*app_capture != 0) != (*dvr_enabled != 0))) return std::nullopt;
    // Kayitli olan ana anahtar(lar) kapaliysa arka plan kaydi calismaz; hicbiri kayitli degilse varsayilan acik.
    const bool app_off = app_capture && *app_capture == 0;
    const bool dvr_off = dvr_enabled && *dvr_enabled == 0;
    if (app_off || dvr_off) return false;
    return true;
}

std::optional<bool> game_mode_from(std::optional<std::uint32_t> auto_game_mode_enabled, std::optional<std::uint32_t> allow_auto_game_mode) {
    if (auto_game_mode_enabled) return *auto_game_mode_enabled != 0;
    if (allow_auto_game_mode) return *allow_auto_game_mode != 0;
    return true;  // Windows 10 1703+ / Windows 11 varsayilani: acik
}

std::optional<std::string> power_mode_from(const std::optional<std::string>& plan_guid, const std::optional<std::string>& plan_name,
                                           const std::optional<std::string>& overlay_guid) {
    if (!plan_guid) return std::nullopt;
    switch (ci::plan_from_guid(*plan_guid)) {
        case ci::PowerPlan::PowerSaver: return std::string("power_saver");
        case ci::PowerPlan::HighPerformance: return std::string("high_performance");
        case ci::PowerPlan::Ultimate: return std::string("ultimate");
        case ci::PowerPlan::Balanced: {
            // Guc modu kaydiricisi (overlay) yalniz Dengeli planda etkindir.
            if (overlay_guid) {
                const auto o = ci::overlay_from_guid(*overlay_guid);
                if (o != ci::PowerOverlay::Other) return std::string(ci::to_string(o));
            }
            return std::string("balanced");
        }
        case ci::PowerPlan::Custom:
            // Ozel planda katman anlamsizdir; "balanced" + otomatik duzeltme onerilmez, plan adi gosterilir.
            if (plan_name && !plan_name->empty()) return *plan_name;
            return std::nullopt;
    }
    return std::nullopt;
}

cr::WindowsGamingInfo::PowerPlanKind power_plan_kind_from(const std::optional<std::string>& plan_guid) {
    using K = cr::WindowsGamingInfo::PowerPlanKind;
    if (!plan_guid) return K::Unknown;
    return ci::plan_from_guid(*plan_guid) == ci::PowerPlan::Custom ? K::Custom : K::Builtin;
}

// ---------------- Ekran okuyucu ----------------

std::vector<cr::DisplayInfo> read_displays(DisplayReadDiag& diag) {
    std::vector<cr::DisplayInfo> out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    // Sanal yenileme farkindaligi Windows 11+; eski surumde ERROR_INVALID_PARAMETER -> bayraksiz tekrar.
    const UINT32 attempts[] = {QDC_ONLY_ACTIVE_PATHS | kQdcVirtualRefreshRateAware,
                               QDC_ONLY_ACTIVE_PATHS | kQdcVirtualModeAware | kQdcVirtualRefreshRateAware,
                               QDC_ONLY_ACTIVE_PATHS};
    LONG r = ERROR_GEN_FAILURE;
    UINT32 used = 0;
    for (UINT32 f : attempts) {
        r = query_paths(f, paths, modes);
        used = f;
        if (r == ERROR_SUCCESS) break;
    }
    diag.qdc_flags = used;
    diag.qdc_status = r;
    diag.virtual_refresh_aware = r == ERROR_SUCCESS && (used & kQdcVirtualRefreshRateAware);
    if (r != ERROR_SUCCESS) return out;
    const bool vm_aware = (used & kQdcVirtualModeAware) != 0;
    // F-44: adaptor listesi yol basina degil, okuma basina bir kez cikarilir.
    const auto adapters = read_adapters();

    std::uint32_t index = 0;
    // Uyumluluk B6 (bosluk 10): kopyalanmis (clone) ekranlarda birden fazla hedef ayni kaynagi (ayni GDI aygiti,
    // ayni masaustu modu) paylasir. Kaynak basina tek satir: ayni aygit iki kez "duzeltilmez".
    std::vector<std::pair<LUID, UINT32>> seen_sources;
    for (const auto& p : paths) {
        if (!(p.flags & DISPLAYCONFIG_PATH_ACTIVE)) continue;
        const LUID sa = p.sourceInfo.adapterId;
        const UINT32 sid = p.sourceInfo.id;
        bool clone = false;
        for (const auto& [l, id] : seen_sources)
            clone = clone || (l.LowPart == sa.LowPart && l.HighPart == sa.HighPart && id == sid);
        if (clone) continue;
        seen_sources.emplace_back(sa, sid);
        cr::DisplayInfo d;
        d.index = index++;

        DISPLAYCONFIG_TARGET_DEVICE_NAME tn{};
        tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tn.header.size = sizeof(tn);
        tn.header.adapterId = p.targetInfo.adapterId;
        tn.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS && tn.monitorFriendlyDeviceName[0])
            d.name = to_utf8(tn.monitorFriendlyDeviceName);

        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn{};
        sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sn.header.size = sizeof(sn);
        sn.header.adapterId = p.sourceInfo.adapterId;
        sn.header.id = p.sourceInfo.id;
        const bool have_gdi = DisplayConfigGetDeviceInfo(&sn.header) == ERROR_SUCCESS;
        if (have_gdi && sn.viewGdiDeviceName[0]) d.gdi_device = to_utf8(sn.viewGdiDeviceName);

        UINT32 si = DISPLAYCONFIG_PATH_MODE_IDX_INVALID, ti = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        mode_indices(p, vm_aware, si, ti);
        if (si < modes.size() && modes[si].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            const auto& sm = modes[si].sourceMode;
            d.width = sm.width;
            d.height = sm.height;
            d.primary = sm.position.x == 0 && sm.position.y == 0;
        }
        std::optional<double> physical;
        if (ti < modes.size() && modes[ti].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            physical = rational(modes[ti].targetMode.targetVideoSignalInfo.vSyncFreq);
        // QDC_VIRTUAL_REFRESH_RATE_AWARE ile targetInfo.refreshRate sanal (masaustu) hizdir.
        const auto virt = rational(p.targetInfo.refreshRate);
        d.hz_current = physical ? physical : virt;
        d.dynamic_refresh = (p.flags & kPathBoostRefreshRate) != 0 ||
                            (diag.virtual_refresh_aware && physical && virt && std::fabs(*physical - *virt) >= 1.0);
        if (have_gdi) d.hz_max_same_res = max_refresh_same_res(sn.viewGdiDeviceName, d.width, d.height);
        // Masaustu modu listesi tam sayi Hz verir; 143.98 gibi fiziksel degerin altinda kalirsa fizikseli kullan.
        if (d.hz_max_same_res && d.hz_current && *d.hz_current > *d.hz_max_same_res) d.hz_max_same_res = d.hz_current;
        read_hdr(p, d);
        // F-44: yolun kaynak adaptoru (sourceInfo.adapterId) ekrani surun GPU'dur. Kaynak eslesmezse hedef
        // adaptor denenir; o da eslesmezse gpu_known false kalir (tahmin yok).
        const AdapterLite* gpu = find_adapter(adapters, p.sourceInfo.adapterId);
        if (!gpu) gpu = find_adapter(adapters, p.targetInfo.adapterId);
        if (gpu) {
            d.gpu_known = true;
            d.gpu_name = gpu->name;
            d.gpu_vendor = gpu->vendor;
            d.gpu_high_perf = gpu->high_perf;
            d.multi_gpu = adapters.size() >= 2;
        }
        // VRR (G-SYNC/FreeSync) icin belgeli genel Windows API'si bulunamadi: vrr_supported nullopt kalir.
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<cr::DisplayInfo> read_displays() {
    DisplayReadDiag diag;
    return read_displays(diag);
}

// ---------------- Windows oyun ayarlari ----------------
// Kayit anahtarlari Ayarlar uygulamasinin GOZLEMLENEN depolama yerleridir; resmi/belgeli API degildir.
// Bu yuzden yalniz okunur; deger yoksa ya belgelenmis varsayilan kullanilir ya da nullopt birakilir.

cr::WindowsGamingInfo read_windows_gaming() {
    cr::WindowsGamingInfo w;

    {  // Oyun Modu: HKCU\Software\Microsoft\GameBar\AutoGameModeEnabled (DWORD); yoksa ayni anahtarda
       // AllowAutoGameMode (eski ad). Ikisi de yoksa Windows varsayilani "acik" (Windows 10 1703+ / Windows 11).
        bool err = false;
        const wchar_t* gb = L"Software\\Microsoft\\GameBar";
        const auto v = reg_dword(HKEY_CURRENT_USER, gb, L"AutoGameModeEnabled", err);
        std::optional<std::uint32_t> allow;
        if (!v && !err) allow = reg_dword(HKEY_CURRENT_USER, gb, L"AllowAutoGameMode", err);
        if (v || !err) w.game_mode = game_mode_from(v, allow);
    }

    {  // Arka plan kaydi ("Olanlari kaydet"):
       //  HKCU\Software\Microsoft\Windows\CurrentVersion\GameDVR\HistoricalCaptureEnabled  -> "Olanlari kaydet" (topluluk
       //    kaynaklari ve Ayarlar gozlemi; varsayilan kapali)
       //  HKCU\Software\Microsoft\Windows\CurrentVersion\GameDVR\AppCaptureEnabled         -> Game Bar yakalama ana anahtari
       //  HKCU\System\GameConfigStore\GameDVR_Enabled                                      -> eski ana anahtar (aynalanir)
       //  HKLM\SOFTWARE\Policies\Microsoft\Windows\GameDVR\AllowGameDVR                    -> grup ilkesi (belgeli ADMX)
        bool err = false;
        const auto pol = reg_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", L"AllowGameDVR", err);
        const wchar_t* dvr = L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
        const auto app = reg_dword(HKEY_CURRENT_USER, dvr, L"AppCaptureEnabled", err);
        const auto hist = reg_dword(HKEY_CURRENT_USER, dvr, L"HistoricalCaptureEnabled", err);
        const auto cfg = reg_dword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", err);
        if (!err) w.game_dvr_background = game_dvr_background_from(pol, app, cfg, hist);
    }

    {  // Pencereli oyun iyilestirmeleri + Windows VRR:
       //  HKCU\Software\Microsoft\DirectX\UserGpuPreferences\DirectXUserGlobalSettings (REG_SZ, "Ad=0/1;" listesi)
       //  SwapEffectUpgradeEnable -> "Pencereli oyunlar icin iyilestirmeler"; VRROptimizeEnable -> "Degisken yenileme hizi".
       //  Anahtar/deger yoksa Windows varsayilani gecerlidir ve surume gore degisir: tahmin edilmez (nullopt).
        bool err = false;
        const auto s = reg_sz(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", L"DirectXUserGlobalSettings", err);
        if (s) {
            const std::string u = to_utf8(*s);
            w.windowed_optimizations = parse_dx_user_setting(u, "SwapEffectUpgradeEnable");
            w.windows_vrr = parse_dx_user_setting(u, "VRROptimizeEnable");
        }
    }

    {  // HAGS: D3DKMTQueryAdapterInfo(WDDM_2_7_CAPS) - mevcut envanter okuyucusu. Ayar sistem genelidir;
       // destekleyen herhangi bir bagdastiricida acik gorunuyorsa acik sayilir.
        bool any_known = false, any_supported = false, any_enabled_known = false, any_enabled = false;
        for (const auto& h : platform::inventory::read_hags()) {
            if (!h.supported.has()) continue;
            any_known = true;
            if (!*h.supported.value) continue;
            any_supported = true;
            if (h.enabled.has()) {
                any_enabled_known = true;
                any_enabled = any_enabled || *h.enabled.value;
            }
        }
        if (any_known) w.hags_supported = any_supported;
        if (any_supported && any_enabled_known) w.hags_enabled = any_enabled;
    }

    {  // F-48: hizli baslatma. Deger yoksa Windows varsayilani yazilmaz (nullopt): yalniz acikca kayitliysa bildirilir.
        bool err = false;
        const auto v = reg_dword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power",
                                 L"HiberbootEnabled", err);
        if (!err && v) w.fast_startup = *v != 0;
    }

    {  // Guc + VBS/HVCI: mevcut INV-05 okuyucusu (powrprof + Win32_DeviceGuard WMI, salt okuma).
        const auto ps = platform::inventory::read_power_security();
        if (ps.vbs.has()) w.vbs_running = *ps.vbs.value == ci::to_string(ci::VbsState::Running);
        if (ps.hvci_running.has()) w.hvci_enabled = *ps.hvci_running.value;

        auto field = [](const auto& f) -> std::optional<std::string> {
            if (f.has()) return *f.value;
            return std::nullopt;
        };
        w.power_mode = power_mode_from(field(ps.active_plan_guid), field(ps.active_plan_name), field(ps.overlay_guid));
        w.power_plan_kind = power_plan_kind_from(field(ps.active_plan_guid));
    }
    return w;
}

cr::GpuPrefInfo read_gpu_pref_info(const std::vector<std::string>& game_exe_paths) {
    cr::GpuPrefInfo g;
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        if (ps.BatteryFlag != 255) g.laptop = (ps.BatteryFlag & 128) == 0;  // 128: pil yok
        if (ps.ACLineStatus == 0) g.on_battery = true;
        else if (ps.ACLineStatus == 1) g.on_battery = false;  // 255: bilinmiyor -> bos
    }
    const auto pref = read_gpu_preference();
    if (pref.hardware_adapters >= 2 && !pref.high_perf_luid.empty() && !pref.min_power_luid.empty())
        g.hybrid = pref.high_perf_luid != pref.min_power_luid;
    if (!g.laptop.value_or(false) || !g.hybrid.value_or(false)) return g;  // oyun tercihleri yalniz hibrit dizustunde okunur
    for (const auto& path : game_exe_paths) {
        if (path.empty()) continue;
        const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (n <= 1) continue;
        std::wstring w(static_cast<std::size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
        w.resize(static_cast<std::size_t>(n) - 1);
        bool err = false;
        const auto v = reg_sz(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", w.c_str(), err);
        cr::GameGpuPref gp;
        const auto slash = path.find_last_of("\\/");
        gp.game = slash == std::string::npos ? path : path.substr(slash + 1);
        gp.exe = path;
        gp.pref = cr::gpu_pref_from_setting(v ? std::optional<std::string>(to_utf8(*v)) : std::nullopt, err);
        g.games.push_back(std::move(gp));
    }
    return g;
}

}  // namespace ef::platform::readiness
