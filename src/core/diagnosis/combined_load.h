#pragma once
// EF-X22.02 (dok madde 5): karisik yuk (sahne + depolama okumasi ayni anda) icin AYRI isi / guc tanisi (saf).
//  Kural docs/06_DIAGNOSIS_RULES.md "Karisik yuk isi/guc tanisi": ayni kosuda tek basina sahne (solo) ve karisik yuk
//  (combined) pencereleri ayri ayri balance_vendor_limit ile degerlendirilir (bayrak + yuk + saat birlikte; tek basina
//  sicaklik ya da bayrak yetmez). Isi ve guc bayraklari ayri sayilir. CPU isi/guc sensoru urunde yok -> her zaman
//  "olculmedi" notu; PSU arizasi hukmu hic uretilmez.
//  Yayin kapisi (fail-closed): aday sonuc yalniz diagnosis ReleaseStatus.ready iken (>= kMinRealHoldout = 30 bagimsiz
//  gercek donanim holdout ornegi + diger kosullar) yayimlanir; aksi halde published=false ve kapinin nedeni tasinir.

#include <string>
#include <string_view>
#include <vector>

#include "core/diagnosis/balance.h"
#include "core/diagnosis/release_gate.h"

namespace ef::core::diagnosis {

enum class CombinedLimit {
    NotSupported,      // bu GPU ureticisi icin isi/guc bayragi API'si yok (ya da hic ornekte bayrak alani yok)
    InsufficientData,  // pencere/yuk/saat yetersiz; hukum yok
    NoLimitSeen,       // karisik yukte dogrulanmis isi/guc kisiti gorulmedi (mutlak "sorun yok" degildir)
    CombinedOnly,      // kisit yalniz karisik yukte (tek basina sahnede yok): ortak isi/guc butcesi olasi
    AlsoSolo,          // kisit tek basina sahnede de var: karisik yuke ozgu degil
};

struct CombinedLimitInput {
    std::vector<LimitSample> solo;      // sahne alt testi pencereleri (ayni kosu)
    std::vector<LimitSample> combined;  // karisik yuk alt testi pencereleri
    bool vendor_api_missing = false;    // vendor_limit_api_missing(pci vendor)
};

struct CombinedLimitResult {
    CombinedLimit candidate = CombinedLimit::InsufficientData;  // ic aday (yayimlanmamis olabilir)
    bool thermal_seen = false, power_seen = false;              // karisik yuk pencerelerinde bayrak turu (ayri)
    bool cpu_not_measured = true;                               // CPU isi/guc sensoru yok: daima true
    // Hukum siniflari (NoLimitSeen/CombinedOnly/AlsoSolo) yalniz kapi acikken yayimlanir. NotSupported/InsufficientData
    // hukum degil cekimserliktir; her zaman gosterilebilir.
    bool published = false;
    std::string_view text_key;   // published ise sinif metni, degilse "diag.combined.withheld"
    std::string gate_reason_key;  // hukum tutulduysa ReleaseStatus.reason_key (or. "diag.gate.no_real_holdout")
};

std::string_view combined_text_key(CombinedLimit c) noexcept;
CombinedLimitResult diagnose_combined_limits(const CombinedLimitInput& in, const ReleaseStatus& gate);

}  // namespace ef::core::diagnosis
