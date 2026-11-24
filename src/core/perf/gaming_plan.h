#pragma once
// OPT-09: Windows oyun ayarlari gruplari (kullanici kapsami, UAC yok, journal'li).
//  plan.gaming   : Oyun Modu acik + "Olanlari kaydet" (arka plan kaydi) kapali
//  plan.windowed : Pencereli oyunlar icin iyilestirmeler (DX10/11 flip model) + Windows VRR acik
// Yalniz hedeften FARKLI olan ogeler plana girer. Deger kayitli degilse Windows varsayilani esas alinir:
// Oyun Modu varsayilan acik, arka plan kaydi varsayilan kapali (plana girmez); pencereli iyilestirme ve
// VRR icin varsayilan surume gore degisir -> acik yazilir (geri almada deger silinir, varsayilan geri gelir).
#include <vector>

#include "core/adapters/windows/gaming_toggles.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

std::vector<PlanItem> gaming_plan_items(adapters::windows::GamingToggleAdapter& a);

}  // namespace ef::core::perf
