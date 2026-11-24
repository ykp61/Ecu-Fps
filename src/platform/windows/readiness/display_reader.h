#pragma once
// CHK-01/02/05 platform okuyuculari: ekran yollari (CCD API) ve Windows oyun ayarlari.
// Yalniz okuma: ayar degistirmez, yonetici gerekmez, pencere acmaz. Okunamayan deger nullopt.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

// Etkin ekran yollari: QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS [| QDC_VIRTUAL_REFRESH_RATE_AWARE]).
std::vector<core::readiness::DisplayInfo> read_displays();

// Ham okuma ozeti (prob ve tani icin): hangi QDC bayraklari kabul edildi, yol sayisi.
struct DisplayReadDiag {
    std::uint32_t qdc_flags = 0;
    long qdc_status = 0;
    bool virtual_refresh_aware = false;
};
std::vector<core::readiness::DisplayInfo> read_displays(DisplayReadDiag& diag);

core::readiness::WindowsGamingInfo read_windows_gaming();

// Spec 2026-11-12 C3: DXGI yuksek performans tercihindeki ilk donanim bagdastiricisinin LUID'i
// ("hhhhhhhh:llllllll", kucuk harf; PDH GPU sayac ornekleriyle ayni bicim) ve yazilim olmayan bagdastirici sayisi.
// Bagdastirici okunamadiysa LUID bos, sayi 0.
struct GpuPreferenceInfo {
    std::string high_perf_luid;
    std::size_t hardware_adapters = 0;
    // P6: DXGI guc tasarrufu tercihindeki ilk donanim bagdastiricisi (hibritte tumlesik kart); okunamadiysa bos.
    std::string min_power_luid;
};
GpuPreferenceInfo read_gpu_preference();

// P6 (salt okuma): dizustu/hibrit/pil durumu + verilen oyun EXE yollari (utf-8) icin Windows Grafik tercihi
// (HKCU\Software\Microsoft\DirectX\UserGpuPreferences\<yol>). Oyun tercihleri yalniz hibrit dizustunde okunur.
core::readiness::GpuPrefInfo read_gpu_pref_info(const std::vector<std::string>& game_exe_paths);

// ---- Saf yardimcilar (birim testlenir) ----

// "SwapEffectUpgradeEnable=1;VRROptimizeEnable=0;" icinden anahtar (buyuk/kucuk harf duyarsiz, bosluk toleransli).
// Anahtar yoksa veya deger 0/1 degilse nullopt.
std::optional<bool> parse_dx_user_setting(std::string_view settings, std::string_view key);

// Arka plan kaydi ("Olanlari kaydet") durumunun kayit degerlerinden turetilmesi. Parametre nullopt = deger yok.
//  policy_allow: HKLM\SOFTWARE\Policies\Microsoft\Windows\GameDVR\AllowGameDVR (0 = politika ile kapali)
//  app_capture:  HKCU\...\CurrentVersion\GameDVR\AppCaptureEnabled (Game Bar yakalama ana anahtari)
//  dvr_enabled:  HKCU\System\GameConfigStore\GameDVR_Enabled (eski/aynalanan ana anahtar)
//  historical:   HKCU\...\CurrentVersion\GameDVR\HistoricalCaptureEnabled ("Olanlari kaydet")
// Kural: politika 0 -> false; historical yok/0 -> false; iki ana anahtar da var ve celisiyorsa -> nullopt (tahmin yok);
// var olan ana anahtar(lar) 0 ise -> false; aksi halde true.
std::optional<bool> game_dvr_background_from(std::optional<std::uint32_t> policy_allow, std::optional<std::uint32_t> app_capture,
                                             std::optional<std::uint32_t> dvr_enabled, std::optional<std::uint32_t> historical);

// Oyun Modu: HKCU\Software\Microsoft\GameBar\AutoGameModeEnabled, yoksa ayni anahtardaki AllowAutoGameMode
// (eski surumlerin yazdigi ad); ikisi de yoksa Windows varsayilani acik.
std::optional<bool> game_mode_from(std::optional<std::uint32_t> auto_game_mode_enabled, std::optional<std::uint32_t> allow_auto_game_mode);

// Guc modu anahtari (WindowsGamingInfo::power_mode). GUID'ler kucuk harf metin.
//  Guc tasarrufu / Yuksek performans / Nihai performans plani -> "power_saver" / "high_performance" / "ultimate".
//  Dengeli plan: guc modu katmani (overlay) yalniz burada anlamlidir -> "best_efficiency" / "balanced" /
//    "better_performance" / "best_performance"; katman bilinmiyor/sifir GUID/taninmiyor -> "balanced".
//  Ozel plan: daima plan adi (katman yok sayilir); ad yoksa nullopt. Plan GUID'i yoksa nullopt.
std::optional<std::string> power_mode_from(const std::optional<std::string>& plan_guid, const std::optional<std::string>& plan_name,
                                           const std::optional<std::string>& overlay_guid);
// Plan turu (WindowsGamingInfo::power_plan_kind): bilinen Windows plan GUID'i -> Builtin, diger GUID -> Custom
// (ad ne olursa olsun), GUID yok -> Unknown.
core::readiness::WindowsGamingInfo::PowerPlanKind power_plan_kind_from(const std::optional<std::string>& plan_guid);

}  // namespace ef::platform::readiness
