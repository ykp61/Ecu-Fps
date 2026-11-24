#pragma once
// OPT-03: deney planlama ve degerlendirme (saf; birim testlenir).
//  - Birim: KOSU. Bir kosudaki binlerce kare bagimsiz tekrar sayilmaz; her kosu tek deger (ortalama FPS).
//  - Sira: isinma + ABBA (A B B A ...) ile zaman/isinma etkisi dengelenir.
//  - Kosullar: A ve B kosularinin parmak izi (degistirilen grup haric) ayni olmali; farkliysa karar reddedilir.
//  - Iyilesme iddiasi en az 5 A + 5 B gecerli kosu ister; daha azi "belirsiz".
//  - Kalite kategorisi (GAME-04) farkliysa kazanc "kalite takasi" olarak ayri etiketlenir.

#include <optional>
#include <string>
#include <vector>

#include "core/perf/before_after.h"

namespace ef::core::perf {

enum class Arm { A, B };
// warmup kosusu (puanlanmaz) + n cift icin ABBA dizisi. Ornek n=5: W A B B A A B B A A B
std::vector<std::optional<Arm>> abba_schedule(int pairs, int warmup_runs = 1);

struct RunOutcome {
    Arm arm = Arm::A;
    std::string condition_fingerprint;  // degistirilen grup disindaki tum kosul (MEAS-04 parmak izi)
    bool valid = false;                 // manifest gecerli
    double avg_fps = 0;                 // kosu duzeyi deger
};

enum class ExperimentVerdict { Improved, Regressed, Uncertain, ConditionsDiffer, InsufficientRuns };
std::string_view to_string(ExperimentVerdict v) noexcept;

struct ExperimentResult {
    ExperimentVerdict verdict = ExperimentVerdict::InsufficientRuns;
    std::size_t valid_a = 0, valid_b = 0;
    MetricComparison fps;  // kosu duzeyi Welch (B - A)
    bool quality_tradeoff = false;
};

inline constexpr std::size_t kMinRunsPerArm = 5;

ExperimentResult evaluate_experiment(const std::vector<RunOutcome>& runs, bool quality_tradeoff = false);

}  // namespace ef::core::perf
