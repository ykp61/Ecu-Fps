#pragma once
// P2 WP5: arka plan program adaylarindan Performans plani ogeleri (SAF). Aday basina surec basina bir islem
// (BackgroundProcessLower, hedef "bgproc:<pid>:<olusturma>:<yol ozeti>", deger "low"). Plana yalniz gercek, olculmus
// surec goruntusunden gelen adaylar girer; hicbir sey yazmaz. Uygulama yalniz kullanicinin Evet'inden sonra, journal'li.
#include <vector>

#include "core/perf/background_candidates.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr const char* kBackgroundGroup = "plan.background";
// Plan boyut siniri (surec sayisi): bir program cok surecli olabilir; asiri plan onayi okunmaz hale getirir.
inline constexpr std::size_t kBackgroundMaxProcesses = 24;

std::vector<PlanItem> background_plan_items(const std::vector<BgCandidate>& candidates);

}  // namespace ef::core::perf
