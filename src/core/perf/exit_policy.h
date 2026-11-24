#pragma once
// Cikis kurali (owner karari 2026-11-17): "Cik" Performans ayarlarini GERI ALMAZ; yalniz uygulamayi kapatir.
// Uygulanan ayarlar oldugu gibi kalir; kayit (journal) korunur. Sonraki acilista durum gercek hedeflerden
// uzlastirilir (PerfController::init: hepsi etkinse Acik, bazilari kapaliysa hangileri oldugu listelenir).
// Geri alma yalniz acik "Kapali" yoluyla (Evet/Hayir) yapilir.
//  - Is suruyor (plan / uygulama / geri alma): bitince yeniden degerlendirilir; yazma yarida kesilmez.
//  - Onay bekleyen plan (Confirming): Hayir sayilir, hicbir sey degismez, cikilir.
//  - Diger asamalar (Kapali / Acik / geri alma onayi / kurtarma): kayit oldugu gibi korunarak cik.

#include "core/perf/controller.h"

namespace ef::core::perf {

enum class ExitAction {
    Quit,         // ayarlara dokunmadan cik
    DeclinePlan,  // onay bekleyen plan: Hayir (hicbir sey degismez), sonra cik
    Wait,         // is suruyor
};

constexpr ExitAction exit_action(Phase ph) noexcept {
    switch (ph) {
        case Phase::Confirming: return ExitAction::DeclinePlan;
        case Phase::Planning:
        case Phase::Applying:
        case Phase::Restoring: return ExitAction::Wait;
        case Phase::Off:
        case Phase::On:
        case Phase::ConfirmRestore:
        case Phase::RecoveryNeeded: return ExitAction::Quit;
    }
    return ExitAction::Wait;
}

// P1 kanit deneyi suruyorsa (Performans'i deney yurutur) Cik once deneyi iptal edip Kapali'ya donmesini bekler; deney bitince
// normal kurala doner. Deney yokken davranis exit_action(ph) ile aynidir (Cik geri almaz).
constexpr ExitAction exit_action(Phase ph, bool experiment_active) noexcept {
    return experiment_active ? ExitAction::Wait : exit_action(ph);
}

}  // namespace ef::core::perf
