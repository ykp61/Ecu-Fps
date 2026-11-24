#pragma once
// T18: Performans Acik uygulanirken gosterilen "Adim i/n - grup" ve asama satiri (saf; birim testlenir).
// Tum sayilar motorun gercek ilerlemesinden gelir; sure tahmini ve sahte yuzde yoktur.

#include <cstddef>
#include <string_view>

#include "core/recovery/engine.h"

namespace ef::core::perf {

enum class ApplyPhase { Prepare, RestorePoint, Apply, Verify };

// Hazirlik: anlik goruntu alma (Capturing) ve grup henuz bilinmiyorsa. Geri yukleme noktasi: adimin grubu plan.restore_point.
// Uygulama: ayar yazma. Son dogrulama: tum adimlar yazildiktan sonra durum kaydi.
ApplyPhase apply_phase(const recovery::Progress& p) noexcept;
std::string_view apply_phase_key(ApplyPhase p) noexcept;  // "perf.progress.phase.prepare" ...

// "Adim i/n" gosterilebilir mi: adim 1..n araliginda ve grup biliniyor.
bool has_step_line(const recovery::Progress& p) noexcept;

}  // namespace ef::core::perf
