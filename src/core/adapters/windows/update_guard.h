#pragma once
// WIN-06 (R24): oyun oturumunda Windows Update riski. Kullanici uygulamasi icin belgeli bir
// "duraklat/etkin saat" yazma sozlesmesi olmadigindan (WIN-05 kurali) urun YAZMAZ; durumu okur,
// riskli durumda uyarir ve Windows Update sayfasina yonlendirir. Hizmet kapatilmaz.
// WIN-11 / ADR-036: duraklatma / erteleme / etkin saat YAZILMAZ (IAutomaticUpdates::Pause Windows 10'dan beri islemsiz;
// Pause*StartTime / ActiveHours* yalniz BT yoneticisinin aygit ilkesi, S103-S105).
// T03: YAZMA bu dosyada degil; plan.update_hold / plan.update_off katalog gruplari (core/perf/update_hold_plan.h) onayli plan
// ve journal ile yazar. advise_for_game_session writes_anything=false kalir (yalniz tavsiye).

#include <optional>
#include <string>
#include <vector>

#include "contracts/inventory.h"
#include "core/guided/guided.h"

namespace ef::core::adapters::windows {

struct UpdateStatus {
    contracts::Field<bool> reboot_pending;      // WUA ISystemInformation::RebootRequired
    contracts::Field<bool> update_service_running;  // wuauserv
    contracts::Field<int> active_downloads;     // BITS/WU indirme isi sayisi (yaklasik)
    contracts::Field<bool> managed_by_policy;   // WindowsUpdate politika anahtari (INV-01)
};

enum class UpdateRisk { RebootPending, DownloadActive, ManagedByPolicy };
std::string_view text_key(UpdateRisk r) noexcept;

struct UpdateAdvice {
    std::vector<UpdateRisk> risks;
    std::optional<guided::Action> action;  // varsa: Windows Update sayfasi
    bool writes_anything = false;          // daima false (sozlesme)
};

// Oturum oncesi/sirasinda kullaniciya gosterilecek tavsiye.
UpdateAdvice advise_for_game_session(const UpdateStatus& s);

}  // namespace ef::core::adapters::windows
