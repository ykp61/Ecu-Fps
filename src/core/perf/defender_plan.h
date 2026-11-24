#pragma once
// T11 (ADR-035): secmeli Defender tarama dislamasi, grup "plan.defender" (Gelismis, varsayilan Hayir, Yuksek risk).
//  - Defender etkin antivirus degilse / yerel dislamalar gecerli degilse (usable() != true) oge yok.
//  - Yalniz launcher kaynakli oyun kurulum klasorleri; elle eklenen EXE'nin klasoru girmez (cagiran yalniz guvenilirleri verir).
//  - Defender dislama listesini yonetici olmayana gizledigi icin "zaten dislanmis" bilgisi cogunlukla okunamaz: liste okunursa
//    dislanmislar atlanir, okunamazsa TUM uygun klasorler sunulur; onceden dislanmis olan yakalamada (broker) tespit edilir ve
//    Kapali'da kaldirilmaz.
//  - En cok 16 klasor.
#include <string>
#include <vector>

#include "core/adapters/windows/defender_exclusion.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr const char* kDefenderGroup = "plan.defender";
inline constexpr std::size_t kDefenderMaxItems = 16;

struct DefenderFolder {
    std::string path;  // kurulum klasoru (utf-8, mutlak)
    std::string name;  // oyun adi (onay satirinda gosterilir)
};

std::vector<PlanItem> defender_plan_items(adapters::windows::DefenderApi& api, const std::vector<DefenderFolder>& trusted_folders);

}  // namespace ef::core::perf
