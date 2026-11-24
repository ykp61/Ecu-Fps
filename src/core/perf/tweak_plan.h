#pragma once
// ADR-035 / T02: katalog ayarlarindan plan ogeleri. Yalniz mevcut degeri hedeften FARKLI olan girdiler plana girer;
// okunamayan girdi atlanir (uydurma yok); skip_if kosulu saglanan girdi atlanir (ornegin daha siki deger gevsetilmez).
// Machine girdileri needs_admin=true (broker, UAC); User girdileri uygulama icinde uygulanir.
// Bu surumde katalog bos oldugundan hicbir grup oge uretmez; ilk tuketiciler grubunu plan_groups()'a ekler.
#include <span>
#include <string_view>
#include <vector>

#include "core/adapters/windows/tweak_adapter.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

// Derlenmis katalogdaki grup girdileri.
std::vector<PlanItem> tweak_plan_items(std::string_view group, adapters::windows::TweakAdapter& adapter);
// Ayni kural, verilen girdi listesiyle (testler ve ileride birlesik gruplar icin).
std::vector<PlanItem> tweak_plan_items_for(std::span<const adapters::windows::TweakEntry* const> entries,
                                           adapters::windows::TweakAdapter& adapter);

}  // namespace ef::core::perf
