#pragma once
// T03 (ADR-035): Windows Update oyun sirasinda tutma. Windows Update KAPATILMAZ; hizmet (wuauserv) durdurulmaz.
// Iki katalog grubu: plan.update_hold (bildirim + zorla yeniden baslatma yok + Teslim Optimizasyonu kapali) ve
// plan.update_off (varsayilan Hayir: NoAutoUpdate). Yazma TweakValueSet (broker, journal, geri okuma, Kapali geri alir).
// Kurumsal politika: WindowsUpdate politikasi okunamiyorsa veya baskasi tarafindan konmussa HIC oge uretilmez.
// Kendi yazdigimiz degerler politika sayilmaz: own_values_active=true (Performans Acik, journal'da bu gruplarin
// ogeleri etkin) iken managed_by_policy=true "baskasi" anlamina gelmez ve plan yeniden kurulabilir.
#include <span>
#include <string_view>
#include <vector>

#include "contracts/models.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/adapters/windows/update_guard.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr std::string_view kUpdateHoldGroup = "plan.update_hold";
inline constexpr std::string_view kUpdateOffGroup = "plan.update_off";

// Her iki grubun ogeleri (kurumsal politikada veya okunamazsa bos).
std::vector<PlanItem> update_hold_plan_items(const adapters::windows::UpdateStatus& status, adapters::windows::TweakAdapter& adapter,
                                             bool own_values_active = false);

// Etkin journal islemlerinde bu gruplara ait bir TweakValueSet var mi (kendi yazdigimiz politika degerleri).
bool update_hold_own_active(std::span<const contracts::SettingOperation> active_ops);

}  // namespace ef::core::perf
