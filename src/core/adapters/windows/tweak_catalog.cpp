#include "core/adapters/windows/tweak_catalog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <charconv>

namespace ef::core::adapters::windows {

namespace {
// Girdi eklenince buraya konur (T03: Windows Update tutma/kapatma).
constexpr std::wstring_view kWuAu = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
constexpr std::wstring_view kDeliveryOpt = L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization";
constexpr std::wstring_view kAdvInfo = L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo";
constexpr std::wstring_view kPrivacyKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy";
constexpr std::wstring_view kCdm = L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
constexpr std::wstring_view kDataCollection = L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection";
constexpr std::wstring_view kStorePolicy = L"SOFTWARE\\Policies\\Microsoft\\WindowsStore";
constexpr std::wstring_view kPolSystem = L"SOFTWARE\\Policies\\Microsoft\\Windows\\System";
constexpr std::wstring_view kEdgePolicy = L"SOFTWARE\\Policies\\Microsoft\\Edge";
constexpr std::wstring_view kExplorerPolicy = L"Software\\Policies\\Microsoft\\Windows\\Explorer";
constexpr std::wstring_view kExplorerAdvanced = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr std::wstring_view kMouseKey = L"Control Panel\\Mouse";
constexpr std::wstring_view kOneDrivePolicy = L"SOFTWARE\\Policies\\Microsoft\\Windows\\OneDrive";
constexpr std::wstring_view kPriorityControl = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
constexpr std::wstring_view kDshPolicy = L"SOFTWARE\\Policies\\Microsoft\\Dsh";
constexpr std::array<TweakEntry, 25> kCatalog{{
    // plan.update_hold: Windows Update kapatilmaz; otomatik indirme bildirime cevrilir, oturum aciksa zorla yeniden baslatma engellenir.
    {"update.no_reboot", TweakHive::Machine, kWuAu, L"NoAutoRebootWithLoggedOnUsers", kTweakRegDword, "1", "plan.update_hold"},
    {"update.notify", TweakHive::Machine, kWuAu, L"AUOptions", kTweakRegDword, "2", "plan.update_hold"},
    {"update.do_off", TweakHive::Machine, kDeliveryOpt, L"DODownloadMode", kTweakRegDword, "0", "plan.update_hold"},
    // plan.update_off (varsayilan Hayir, Yuksek risk): otomatik guncellemeyi kapatir; AUOptions ayni degerle de yazilir.
    {"update.auto_off", TweakHive::Machine, kWuAu, L"NoAutoUpdate", kTweakRegDword, "1", "plan.update_off"},
    {"update.off_notify", TweakHive::Machine, kWuAu, L"AUOptions", kTweakRegDword, "2", "plan.update_off"},
    // T14 plan.privacy (HKCU, UAC yok): reklam kimligi, kisisellestirilmis oneriler, sessiz uygulama kurulumu. FPS etkisi yok.
    {"privacy.ad_id", TweakHive::User, kAdvInfo, L"Enabled", kTweakRegDword, "0", "plan.privacy"},
    {"privacy.tailored", TweakHive::User, kPrivacyKey, L"TailoredExperiencesWithDiagnosticDataEnabled", kTweakRegDword, "0", "plan.privacy"},
    {"privacy.cdm_silent", TweakHive::User, kCdm, L"SilentInstalledAppsEnabled", kTweakRegDword, "0", "plan.privacy"},
    {"privacy.cdm_suggest", TweakHive::User, kCdm, L"SystemPaneSuggestionsEnabled", kTweakRegDword, "0", "plan.privacy"},
    {"privacy.cdm_338388", TweakHive::User, kCdm, L"SubscribedContent-338388Enabled", kTweakRegDword, "0", "plan.privacy"},
    // T14 plan.privacy_system (HKLM, broker/UAC): tani verisi Gerekli (1); 0 yalniz Enterprise/Education'da gecerlidir ve YAZILMAZ.
    // Politika anahtarinda deger zaten varsa (0, kurum degeri vb.) dokunulmaz (skip_if = WhenPresent).
    {"privacy.telemetry", TweakHive::Machine, kDataCollection, L"AllowTelemetry", kTweakRegDword, "1", "plan.privacy_system",
     TweakSkip::WhenPresent},
    {"privacy.activities", TweakHive::Machine, kPolSystem, L"PublishUserActivities", kTweakRegDword, "0", "plan.privacy_system"},
    // T12 plan.feature.store (HKLM, broker/UAC): Magaza otomatik uygulama guncellemesi kapali (2). Magaza kullanilabilir kalir.
    // DOGRULANMADI: bu politika Home/Pro surumlerde yok sayilabilir; etki vaat edilmez.
    // T13 plan.debloat.edge / plan.debloat.onedrive (HKLM politika, broker/UAC, varsayilan Hayir): hicbir sey KALDIRILMAZ.
    // Deger zaten varsa (kurum veya baska ayar) dokunulmaz (WhenPresent). Edge grubu yalniz baska bir varsayilan tarayici varken sunulur.
    {"debloat.edge_startup_boost", TweakHive::Machine, kEdgePolicy, L"StartupBoostEnabled", kTweakRegDword, "0", "plan.debloat.edge",
     TweakSkip::WhenPresent},
    {"debloat.edge_background", TweakHive::Machine, kEdgePolicy, L"BackgroundModeEnabled", kTweakRegDword, "0", "plan.debloat.edge",
     TweakSkip::WhenPresent},
    // DisableFileSyncNGSC=1: OneDrive dosya esitlemesi Kapali'ya kadar durur (onay metni bunu soyler).
    {"debloat.onedrive_sync", TweakHive::Machine, kOneDrivePolicy, L"DisableFileSyncNGSC", kTweakRegDword, "1", "plan.debloat.onedrive",
     TweakSkip::WhenPresent},
    {"feature.store_autodl", TweakHive::Machine, kStorePolicy, L"AutoDownload", kTweakRegDword, "2", "plan.feature.store"},
    // T21 plan.win11_cleanup (HKCU, UAC yok, varsayilan Hayir): Baslat web aramasi/onerileri ve kilit ekrani ipuclari. Yalniz gorunum.
    // UCPD korumali anahtarlar (ornegin Explorer\Advanced TaskbarDa, varsayilan tarayici/dosya iliskileri) BILEREK yok: geri okuma basarisiz olur.
    {"win11.web_search", TweakHive::User, kExplorerPolicy, L"DisableSearchBoxSuggestions", kTweakRegDword, "1", "plan.win11_cleanup"},
    {"win11.start_recs", TweakHive::User, kExplorerAdvanced, L"Start_IrisRecommendations", kTweakRegDword, "0", "plan.win11_cleanup"},
    {"win11.lock_overlay", TweakHive::User, kCdm, L"RotatingLockScreenOverlayEnabled", kTweakRegDword, "0", "plan.win11_cleanup"},
    {"win11.lock_tips", TweakHive::User, kCdm, L"SubscribedContent-338387Enabled", kTweakRegDword, "0", "plan.win11_cleanup"},
    // R-C3 plan.widgets (HKLM politika, broker/UAC, varsayilan Hayir): Widgets/Haberler paneli (AllowNewsAndInterests=0, Microsoft Dsh
    // politikasi, cihaz kapsami). Yalniz arka plan temizligi; FPS etkisi BELGESIZ ve vaat edilmez. Kurum degeri varsa dokunulmaz.
    // Copilot (kullanici politikasi, belgede kullanimdan kaldirildi) ve Recall (yalniz Copilot+ PC) BILEREK yok.
    {"widgets.news_feed", TweakHive::Machine, kDshPolicy, L"AllowNewsAndInterests", kTweakRegDword, "0", "plan.widgets",
     TweakSkip::WhenPresent},
    // T21 plan.input (HKCU, varsayilan Hayir, Tartismali): Isaretci duyarliligini artir (ivme) kapali. Degerler REG_SZ; oturum acisinda gecerli olur.
    {"input.mouse_speed", TweakHive::User, kMouseKey, L"MouseSpeed", kTweakRegSz, "0", "plan.input"},
    {"input.mouse_thr1", TweakHive::User, kMouseKey, L"MouseThreshold1", kTweakRegSz, "0", "plan.input"},
    {"input.mouse_thr2", TweakHive::User, kMouseKey, L"MouseThreshold2", kTweakRegSz, "0", "plan.input"},
    // B2 / ADR-040 m.2 plan.foreground_boost (HKLM, broker/UAC, varsayilan Hayir, Tartismali): Win32PrioritySeparation tek deger (38).
    // KAPALI (kPrioritySeparationItemEnabled = false): 38 ile istemci varsayilani 2'nin denkligi resmi belgeden DOGRULANAMADI;
    // bu yuzden atlama kurali da yok. Bayrak acilmadan plana ve broker'a girmez.
    {"sched.priority_separation", TweakHive::Machine, kPriorityControl, L"Win32PrioritySeparation", kTweakRegDword, "38",
     "plan.foreground_boost"},
}};

std::wstring lower(std::wstring_view s) {
    std::wstring o(s);
    for (auto& c : o) c = static_cast<wchar_t>(std::towlower(c));
    return o;
}
bool contains(std::wstring_view hay, std::wstring_view needle) { return hay.find(needle) != std::wstring_view::npos; }

bool id_chars_ok(std::string_view id) noexcept {
    if (id.empty() || id.size() > 48) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}
}  // namespace

std::span<const TweakEntry> tweak_catalog() noexcept { return std::span<const TweakEntry>(kCatalog.data(), kCatalog.size()); }

const TweakEntry* find_tweak(std::string_view id) noexcept {
    for (const auto& e : kCatalog)
        if (e.id == id) return &e;
    return nullptr;
}

const TweakEntry* find_tweak_target(std::string_view target) noexcept {
    if (target.substr(0, kTweakTargetPrefix.size()) != kTweakTargetPrefix) return nullptr;
    return find_tweak(target.substr(kTweakTargetPrefix.size()));
}

bool tweak_item_enabled(const TweakEntry& e, bool priority_sep_enabled) noexcept {
    return e.id != kPrioritySeparationId || priority_sep_enabled;
}

std::vector<const TweakEntry*> tweaks_in_group(std::string_view group, bool priority_sep_enabled) {
    std::vector<const TweakEntry*> out;
    for (const auto& e : kCatalog)
        if (e.group_key == group && tweak_item_enabled(e, priority_sep_enabled)) out.push_back(&e);
    return out;
}

std::optional<RegRaw> tweak_on_raw(const TweakEntry& e) { return tweak_on_raw(e, TweakRuntime{}); }

std::optional<RegRaw> tweak_on_raw(const TweakEntry& e, const TweakRuntime& rt) {
    if (e.value_source == TweakValueSource::InstalledRamKb) {
        // Yalniz DWORD ve bos on_value; RAM okunamadiysa, sifirsa veya DWORD'a sigmiyorsa hedef yok (uydurma yok).
        if (e.reg_type != kTweakRegDword || !e.on_value.empty() || !rt.installed_ram_kb) return std::nullopt;
        const std::uint64_t kb = *rt.installed_ram_kb;
        if (kb == 0 || kb > 0xFFFFFFFFull) return std::nullopt;
        return dword_raw(static_cast<std::uint32_t>(kb));
    }
    if (e.value_source != TweakValueSource::Fixed) return std::nullopt;
    if (e.reg_type == kTweakRegDword) {
        std::uint32_t v = 0;
        const char* b = e.on_value.data();
        const char* en = b + e.on_value.size();
        const auto r = std::from_chars(b, en, v);
        if (e.on_value.empty() || r.ec != std::errc{} || r.ptr != en) return std::nullopt;
        return dword_raw(v);
    }
    if (e.reg_type == kTweakRegSz) {
        if (e.on_value.empty()) return std::nullopt;
        std::wstring w;
        for (char c : e.on_value) {
            if (static_cast<unsigned char>(c) >= 0x80) return std::nullopt;  // yalniz ASCII
            w.push_back(static_cast<wchar_t>(c));
        }
        return sz_raw(w);
    }
    return std::nullopt;
}

std::optional<std::string> tweak_on_text(const TweakEntry& e, const TweakRuntime& rt) {
    const auto raw = tweak_on_raw(e, rt);
    if (!raw) return std::nullopt;
    if (e.value_source == TweakValueSource::Fixed) return std::string(e.on_value);  // tweak_on_raw bicimi dogruladi
    const auto d = raw_dword(*raw);
    if (!d) return std::nullopt;
    return std::to_string(*d);
}

bool tweak_entry_valid(const TweakEntry& e) {
    if (!id_chars_ok(e.id) || e.subkey.empty() || e.name.empty() || e.group_key.empty()) return false;
    if (e.subkey.front() == L'\\' || e.subkey.back() == L'\\' || contains(e.subkey, L"..")) return false;
    if (e.value_source == TweakValueSource::InstalledRamKb) {
        if (e.reg_type != kTweakRegDword || !e.on_value.empty()) return false;  // sabit deger tasiyamaz
    } else if (e.value_source != TweakValueSource::Fixed || !tweak_on_raw(e)) {
        return false;
    }
    if (e.skip_if != TweakSkip::None && e.reg_type != kTweakRegDword) return false;
    // Guvenlik/anti-cheat kapatma ve kimlik/guvenli onyukleme alanlari katalogda olamaz.
    const std::wstring sk = lower(e.subkey);
    for (std::wstring_view bad : {std::wstring_view(L"image file execution options"), std::wstring_view(L"defender"),
                                  std::wstring_view(L"\\lsa"), std::wstring_view(L"secureboot"), std::wstring_view(L"deviceguard"),
                                  std::wstring_view(L"ci\\config"), std::wstring_view(L"\\sam\\"), std::wstring_view(L"securityproviders")})
        if (contains(sk, bad)) return false;
    if (sk == L"lsa" || sk.rfind(L"lsa\\", 0) == 0) return false;
    return true;
}

bool tweak_admin_allowed(const TweakEntry* e, std::string_view new_value, const TweakRuntime& rt) noexcept {
    if (!e || e->hive != TweakHive::Machine) return false;
    try {
        const auto want = tweak_on_text(*e, rt);  // broker'in kendi hesabi; istemci degeri yalniz karsilastirilir
        return want && new_value == *want;
    } catch (...) {
        return false;
    }
}

bool tweak_admin_allowed(const TweakEntry* e, std::string_view new_value) noexcept {
    return tweak_admin_allowed(e, new_value, TweakRuntime{});
}

bool tweak_apply_allowed(const TweakEntry* e, std::string_view new_value, const TweakRuntime& rt) noexcept {
    return e && tweak_item_enabled(*e) && tweak_admin_allowed(e, new_value, rt);
}

bool tweak_admin_allowed_gated(const TweakEntry* e, std::string_view new_value, bool priority_sep_enabled) noexcept {
    return e && tweak_item_enabled(*e, priority_sep_enabled) && tweak_admin_allowed(e, new_value);
}

namespace {
std::atomic<TweakRuntimeProvider> g_runtime_provider{nullptr};
}  // namespace

void set_tweak_runtime_provider(TweakRuntimeProvider p) noexcept { g_runtime_provider.store(p); }

TweakRuntime current_tweak_runtime() noexcept {
    const auto p = g_runtime_provider.load();
    return p ? p() : TweakRuntime{};
}

bool tweak_should_skip(const TweakEntry& e, const std::optional<RegRaw>& cur) { return tweak_should_skip(e, cur, TweakRuntime{}); }

bool tweak_should_skip(const TweakEntry& e, const std::optional<RegRaw>& cur, const TweakRuntime& rt) {
    if (e.skip_if == TweakSkip::None || !cur) return false;
    if (e.skip_if == TweakSkip::WhenPresent) return true;  // deger var: kurum veya daha siki ayar, dokunma
    const auto d = raw_dword(*cur);
    const auto want = tweak_on_raw(e, rt);
    if (!d || !want) return false;
    const auto w = raw_dword(*want);
    if (!w) return false;
    return e.skip_if == TweakSkip::WhenCurrentLowerOrEqual ? *d <= *w : *d >= *w;
}

}  // namespace ef::core::adapters::windows
