#pragma once
// T11 platform: Microsoft Defender tarama dislamasi (WMI root\Microsoft\Windows\Defender, MSFT_MpPreference + MSFT_MpComputerStatus).
//  - exclusion_paths: MSFT_MpPreference.ExclusionPath (yonetici degilse Defender listeyi gizler -> Denied; bos liste uydurulmaz).
//  - add_path/remove_path: MSFT_MpPreference::Add/Remove (ExclusionPath). Yazma yalniz yukseltilmis broker'da basarili olur.
//  - usable: Defender etkin antivirus (AMRunningMode == Normal, AntivirusEnabled) ve ilke ile yonetilen dislama / DisableLocalAdminMerge yok.
// DOGRULANMADI: gercek WMI yazimi ve Tamper Protection davranisi (derleme dogrulandi, cihazda calistirilmadi).
#include "core/adapters/windows/defender_exclusion.h"

namespace ef::platform::adapters {

class WinDefenderApi final : public core::adapters::windows::DefenderApi {
public:
    contracts::Result<std::vector<std::string>> exclusion_paths() override;
    contracts::Status add_path(const std::string& path) override;
    contracts::Status remove_path(const std::string& path) override;
    std::optional<bool> usable() override;
};

}  // namespace ef::platform::adapters
