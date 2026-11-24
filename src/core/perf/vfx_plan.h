#pragma once
// UI-10: "Windows animasyonlari ve golgeleri" grubu (W09, WIN-02 adaptoru). Yalniz su an ACIK olan
// efektler kapatilir; yazi yumusatma hicbir zaman listede degildir. Kapali'da onceki degerler geri gelir.
// Durustluk: oyun ici FPS etkisi genelde kucuktur; aciklama metni bunu soyler.

#include <vector>

#include "core/adapters/windows/display_process.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

std::vector<PlanItem> vfx_plan_items(adapters::windows::VisualEffectsApi& api);

}  // namespace ef::core::perf
