#pragma once
// UI-02: ilk gercek plan kaynagi (W01/W02). Kurallar katalogdan:
//  - Dengeli plan etkin ve guc modu destekleniyorsa: AC guc modu -> "En iyi performans".
//    Pilli cihazda DC (pil) modu da ayri journal adimiyla ayni hedefe alinir; DC desteklenmiyorsa
//    AC ogesi "yalniz fise takiliyken" alan anahtariyla isaretlenir (gap 11).
//  - Aksi halde sistemde VAR olan "Yuksek performans" planina gecis (plan olusturulmaz).
//  - Zaten en iyi durumdaysa bos plan (sifir is).
// Tam kural secimi OPT-01'de genisler; bu fonksiyon onun ilk girdisidir.

#include <vector>

#include "core/adapters/windows/power.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr std::string_view kSchemeHighPerformance = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";

std::vector<PlanItem> power_plan_items(adapters::windows::PowerApi& api);

}  // namespace ef::core::perf
