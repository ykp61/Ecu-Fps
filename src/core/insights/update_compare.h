#pragma once
// INS-02: guncelleme oncesi/sonrasi karsilastirma (saf; birim testlenir).
//  - Iki kosu grubunun baglami (os_build, driver, game_version, app_version, resolution...) karsilastirilir;
//    degisen anahtarlar listelenir. Tek anahtar degistiyse bile sonuc "ayni zamana denk geldi"dir (neden degil):
//    surum degisti diye surucu suclanmaz. Birden cok anahtar degistiyse "karisik kosul" (ayristirilamaz).
//  - Metrik farki kosu duzeyi Welch araligiyla (OPT-03 / before_after) verilir; aralik 0'i iceriyorsa "fark yok".

#include <map>
#include <string>
#include <vector>

#include "core/perf/before_after.h"

namespace ef::core::insights {

using Context = std::map<std::string, std::string>;

enum class Attribution { NoChangeInContext, SingleChangeCoincides, ConfoundedMultipleChanges, ContextMissing };
std::string_view to_string(Attribution a) noexcept;

struct ContextDiff {
    std::vector<std::string> changed;  // degisen anahtarlar
    std::vector<std::string> missing;  // bir tarafta olmayan anahtarlar
};
ContextDiff diff_context(const Context& before, const Context& after);

struct UpdateComparison {
    ContextDiff diff;
    Attribution attribution = Attribution::ContextMissing;
    perf::MetricComparison fps;  // kosu duzeyi (sonra - once)
};

// before_fps / after_fps: her kosunun ortalama FPS'i (kare degil). Baglam her grup icin tek (ayni kosul).
UpdateComparison compare_update(const Context& before, const Context& after, const std::vector<double>& before_fps,
                                const std::vector<double>& after_fps);

}  // namespace ef::core::insights
