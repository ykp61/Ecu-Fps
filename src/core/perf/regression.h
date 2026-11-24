#pragma once
// OPT-04: deney sonucuna gore sonraki adim (saf; birim testlenir).
//  - Dogrulanmis gerileme (Regressed, >= 5+5 kosu) -> ilgili grubu eski snapshot degerine geri al, sonra
//    gercek degeri geri oku ve olcumu tekrarla (geri alma da dogrulanir).
//  - Belirsiz / yetersiz -> basari iddiasi yok; tekrar hakki varsa tekrar, yoksa "belirsiz" ile birak
//    (degisiklik kullanicinin onayiyla yerinde kalir; otomatik "iyilesti" yazilmaz).
//  - Tek gurultulu kosu kalici ayar kriteri degildir (kosu sayisi kurali OPT-03'te).
//  - Kosullar farkli -> deney gecersiz; yeniden planla.

#include <vector>

#include "core/perf/experiment.h"

namespace ef::core::perf {

enum class NextStep { KeepAndReportImproved, KeepNoClaim, Repeat, RollbackGroup, VerifyRollbackValue, RemeasureAfterRollback, Replan, Done };
std::string_view to_string(NextStep s) noexcept;

struct RegressionPolicy {
    int max_repeats = 2;
};

// attempts: bu grup icin tamamlanan deney sayisi (1 = ilk deney).
std::vector<NextStep> next_steps(const ExperimentResult& r, int attempts, const RegressionPolicy& p = {});

}  // namespace ef::core::perf
