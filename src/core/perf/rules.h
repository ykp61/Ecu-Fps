#pragma once
// OPT-01: kural secimi ve cakisma cozumu (saf; birim testlenir). Aday kurallardan tipli plan uretir.
//  - Plan disi: yetenek Supported degil, geri alma (restore) yolu yok, belirsiz API (Unknown), surum kosulu
//    tutmuyor, bagimlilik plana girmemis, kalite dusuren kural onaysiz.
//  - Karsilikli dislayan gruplarda (ornek ayni ayarin iki degeri) yalniz en yuksek oncelikli kalir.
//  - Sifir islem: "optimize edildi" uretilmez; sonuc NothingToDo + nedenler.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "contracts/support.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

struct RuleContext {
    std::map<std::string, contracts::Capability> caps;  // feature_id -> yetenek
    std::string os_build;                               // "26300"
    std::string driver_version;                         // "621.01"
    std::string game_id;                                // "steam:730" (bos = genel)
    bool quality_tradeoff_approved = false;             // kullanici kalite takasini onayladi
};

struct Rule {
    std::string id;                 // "R-W01-power-mode"
    std::string feature_id;         // yetenek anahtari
    PlanItem item;
    bool reversible = true;         // snapshot + restore yolu var
    bool lowers_quality = false;
    std::string exclusive_group;    // bos = yok
    int priority = 0;               // grup icinde buyuk kazanir
    std::vector<std::string> requires_rules;  // bu kurallar plana girmediyse bu da girmez
    std::string min_os_build;       // bos = kosul yok (sayisal karsilastirma)
    std::string game_id;            // bos = her oyun
};

enum class Exclusion {
    CapabilityNotSupported, CapabilityUnknown, NotReversible, OsTooOld, OtherGame, QualityNotApproved, LostExclusiveGroup,
    MissingDependency,
};
std::string_view to_string(Exclusion e) noexcept;

struct RuleDecision {
    std::string rule_id;
    std::optional<Exclusion> excluded;  // nullopt = plana girdi
};

struct RuleSelection {
    Plan plan;
    std::vector<RuleDecision> decisions;
    bool nothing_to_do() const noexcept { return plan.empty(); }
};

RuleSelection select_rules(const std::vector<Rule>& rules, const RuleContext& ctx);

}  // namespace ef::core::perf
