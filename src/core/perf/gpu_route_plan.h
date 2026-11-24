#pragma once
// T01: hibrit dizustunde oyunlari ayrik ekran kartina yonlendirme (Windows Grafik tercihi = Yuksek performans).
// Grup "plan.gpu_route". Plan yalniz hazirlik verisinden (GpuPrefInfo) uretilir; veri yoksa/bos ise oge yoktur (tahmin yok).
//  - laptop == true VE hybrid == true degilse bos.
//  - Yalniz tercihi SystemDefault veya PowerSaving olan oyunlar; Unknown ve HighPerformance atlanir.
//  - writable verilirse yalniz true donen EXE'ler girer (adaptorun yazma kokleri ile ayni kural). En cok 24 oge.
#include <functional>
#include <string_view>
#include <vector>

#include "core/perf/controller.h"
#include "core/readiness/readiness.h"

namespace ef::core::perf {

inline constexpr const char* kGpuRouteGroup = "plan.gpu_route";
inline constexpr std::size_t kGpuRouteMaxItems = 24;

std::vector<PlanItem> gpu_route_plan_items(const readiness::GpuPrefInfo& info,
                                           const std::function<bool(std::string_view)>& writable = {});

}  // namespace ef::core::perf
