#pragma once
// F-60 platform okuyucu: yaygın anti-cheat servislerinin kurulu/çalışıyor durumu — salt okuma.
//  - Yalnız servis durumu sorgulanır; servis başlatılmaz/durdurulmaz, sürücüye erişilmez.
//  - Sorgu başarısızsa ProtectionEvidence::query_ok=false döner (uydurma "yok" yazılmaz).

#include "core/games/protection_gate.h"

namespace ef::platform::games {

core::games::ProtectionEvidence scan_protection_services();

}  // namespace ef::platform::games
