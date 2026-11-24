#pragma once
// T13 (ADR-035): geri alinabilir "temizlik" (rakip eksik analizi: tam Edge/OneDrive/medya uygulamasi silme yerine). Uc grup, hepsi
// Gelismis (varsayilan Hayir):
//  - plan.debloat.edge    (HKLM politika, UAC): Edge baslangic hizlandirma + arka plan modu kapali. Edge KALDIRILMAZ. Yalniz baska bir
//                          tarayici kurulu VE varsayilan tarayici Edge degilse sunulur (okunamazsa sunulmaz).
//  - plan.debloat.onedrive (HKLM politika, UAC): OneDrive dosya esitlemesini durdurur (Kapali'ya kadar); OneDrive KALDIRILMAZ.
//  - plan.debloat.apps    (kullanici kapsami, UAC yok): derlenmis listedeki medya uygulamalari bu kullanici icin kaldirilir; yalniz
//                          kurulu VE restorable()==true (makine icin provisioned) olanlar. Tam Edge kaldirma bu surumde yok (geri alma dogrulanamaz).
// Politika degerleri zaten ayarliysa veya baska bir deger (kurum) varsa oge yoktur (TweakSkip::WhenPresent).
#include <string>
#include <string_view>
#include <vector>

#include "core/adapters/windows/appx_remove.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr std::string_view kDebloatEdgeGroup = "plan.debloat.edge";
inline constexpr std::string_view kDebloatOneDriveGroup = "plan.debloat.onedrive";
inline constexpr std::string_view kDebloatAppsGroup = "plan.debloat.apps";

// Kurulu tarayicilar (StartMenuInternet kayitli istemciler) ve varsayilan https ProgId'si. read_ok=false: okunamadi (uydurma yok).
struct BrowserInfo {
    bool read_ok = false;
    std::vector<std::string> clients;  // StartMenuInternet alt anahtar adlari
    std::string default_progid;        // bos = bilinmiyor
};

// Edge'den baska gercek bir tarayici kurulu (IE harici) mu.
bool other_browser_installed(const BrowserInfo& b);
// Varsayilan tarayici bilinen ve Edge DEGIL mi.
bool default_browser_is_not_edge(const BrowserInfo& b);
// Her ikisi birden: Edge grubu yalniz o zaman sunulur.
bool edge_debloat_offered(const BrowserInfo& b);

std::vector<PlanItem> debloat_plan_items(adapters::windows::AppxApi& appx, const BrowserInfo& browsers, adapters::windows::TweakAdapter& tweak);

}  // namespace ef::core::perf
