#pragma once
// Onaydan gercek yazima: korunan bir islem baslamadan HEMEN once guncel durumu yeniden dogrulayan arka uc siniri.
// UI kontrolu tek guvence degildir: Evet/Hayir, UAC ve broker bekleyislerinden sonra bu arabirim yeniden sorulur.
// verify() herhangi bir is parcacigindan cagrilabilir ve onbellekteki "son izin"e degil guncel gozleme dayanir.

#include "core/games/protection_gate.h"

namespace ef::core::games {

class ActionPermit {
public:
    virtual ~ActionPermit() = default;
    virtual GateDecision verify(GuardedAction action) = 0;
};

}  // namespace ef::core::games
