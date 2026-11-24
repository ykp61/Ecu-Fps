#pragma once
// ADR-035 / T09: secili oyunlar icin "Normalin ustu" islemci onceligi, Windows'un imaj-basina PerfOptions ayariyla.
// OS sureci olustururken uygular; calisan oyun surecine HICBIR handle acilmaz (anti-cheat degismezi).
//   HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\<exe>\PerfOptions : CpuPriorityClass (DWORD) = 6
// Sinirlar:
//  - Hedef "ifeoprio:<exe>" (kucuk harf, [a-z0-9._ -], .exe ile biter) ve derlenmis izin listesinde (priority_games) olmali;
//    izin listesi karsilastirmasi buyuk/kucuk harfe duyarsizdir. Deger yalniz "above_normal".
//  - Yalniz <exe>\PerfOptions\CpuPriorityClass okunur/yazilir/silinir. IFEO'nun baska hicbir degerine (Debugger, GlobalFlag...)
//    ve baska bir alt anahtarina dokunulmaz.
//  - Yakalama: onceki DWORD (veya yok) + <exe> ve PerfOptions anahtarlarinin bizden once var olup olmadigi. Geri alma:
//    onceki deger yazilir / deger silinir; yalniz BIZIM actigimiz bos anahtarlar kaldirilir.
// Gercek HKLM yazimi ve Defender/EDR uyarisi DOGRULANMADI; birim testleri sahte kayit defteri kullanir.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/adapters/windows/tweak_adapter.h"  // MachineRegistryApi
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kIfeoTargetPrefix = "ifeoprio:";
inline constexpr std::string_view kIfeoPriorityValue = "above_normal";
inline constexpr std::uint32_t kIfeoCpuPriorityAboveNormal = 6;  // PROCESS_PRIORITY_CLASS_ABOVE_NORMAL
inline constexpr std::wstring_view kIfeoValueName = L"CpuPriorityClass";

struct PriorityGame {
    std::string_view exe;           // kanonik yazim (anahtar adi)
    std::string_view steam_app_id;  // bos = Steam disi
};
// Derlenmis izin listesi. Cekirdek (kernel) anti-cheat kullanan oyun EKLENMEZ.
std::span<const PriorityGame> priority_games() noexcept;
// Buyuk/kucuk harfe duyarsiz izin listesi eslemesi; yoksa nullptr.
const PriorityGame* find_priority_game(std::string_view exe) noexcept;

// "ifeoprio:<exe>": exe kucuk harfe cevrilir; izin listesinde degilse bos dize.
std::string ifeo_priority_target(std::string_view exe);
// Hedeften kanonik exe adi; bicim/izin listesi gecersizse nullopt (buyuk harfli hedef de reddedilir).
std::optional<std::string> ifeo_exe_from_target(std::string_view target);
std::wstring ifeo_exe_subkey(std::string_view canonical_exe);
std::wstring ifeo_perf_subkey(std::string_view canonical_exe);

class IfeoPriorityAdapter final : public recovery::SettingAdapter {
public:
    // hklm bos olabilir: islem reddedilir (okunamaz).
    explicit IfeoPriorityAdapter(MachineRegistryApi* hklm) : hklm_(hklm) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

    // Plan okumasi: CpuPriorityClass degeri (yoksa nullopt). Yalniz bu tek deger okunur.
    contracts::Result<std::optional<RegRaw>> read_value(std::string_view canonical_exe);

private:
    MachineRegistryApi* hklm_;
};

// Broker izni: Machine kapsam, gecerli "ifeoprio:<exe>" ve deger "above_normal".
bool ifeo_priority_admin_allowed(const contracts::SettingOperation& op) noexcept;

}  // namespace ef::core::adapters::windows
