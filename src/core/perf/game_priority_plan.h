#pragma once
// T09 (ADR-035): secili oyunlar icin Windows'un imaj-basina onceligi (IFEO PerfOptions), grup "plan.game_priority"
// (Gelismis, varsayilan Hayir, Dusuk risk, Tartismali kanit). Oyun surecine dokunulmaz; OS bir sonraki acilista uygular.
//  - Oge yalniz: guvenilir kitaplik taramasindaki bir oyun + derlenmis izin listesindeki exe + cekirdek anti-cheat yok
//    (FACEIT kuruluysa veya durumu okunamadiysa CS2 elenir; EAC/BattlEye/Vanguard/Ricochet kayitli oyun hic girmez).
//  - CpuPriorityClass degeri zaten varsa (kullanicinin kendi ayari dahil) oge yok: onceki deger ezilmez.
//  - Okunamayan kayit defteri -> oge yok (uydurma yok).
#include <string>
#include <vector>

#include "core/adapters/windows/ifeo_priority.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr const char* kGamePriorityGroup = "plan.game_priority";

enum class FaceitState { Unknown, Absent, Present };

struct PriorityLibraryGame {
    std::string launcher;  // "steam" / "epic" / ...
    std::string app_id;    // uyumluluk matrisi kimligi
    std::string exe_hint;  // varsa manifest EXE ipucu (goreli veya mutlak)
    std::string name;      // oyun adi (onay satirinda gosterilir)
};

struct GamePriorityInput {
    bool scan_trusted = false;  // kitaplik taramasi guvenilir mi (degilse oge yok)
    std::vector<PriorityLibraryGame> games;  // yalniz launcher kaynakli (guvenilir klasorlu) oyunlar
    FaceitState faceit = FaceitState::Unknown;
};

// Oyunun cekirdek anti-cheat'i olabilir mi (uyumluluk matrisi). VAC kullanici modudur; FACEIT kuruluysa/bilinmiyorsa korumali sayilir.
bool game_kernel_protected(const std::string& app_id, FaceitState faceit);
// Oyunun izin listesindeki exe adi (kanonik); yoksa bos.
std::string game_priority_exe(const PriorityLibraryGame& g);

std::vector<PlanItem> game_priority_plan_items(adapters::windows::IfeoPriorityAdapter& adapter, const GamePriorityInput& in);

}  // namespace ef::core::perf
