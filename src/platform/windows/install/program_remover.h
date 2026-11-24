#pragma once
// ADR-033 / spec 8: Ayarlar "Kaldir" (ve ARP --uninstall) icin kurulu programin kaldirilmasi. Uygulama ici Evet/Hayir
// ve makine geri yuklemesinden SONRA, uninstall is parcaciginda cagrilir: tek UAC ile yukseltilmis kurulum iscisi
// (SetupKind::Uninstall) hizmeti, ARP'yi, kisayolu, ProgramData'yi (yalniz makine journal'lari temizse) ve dosyalari
// islemsel olarak siler. Bu oturumda calistirilmaz.

#include "core/recovery/uninstall.h"
#include "platform/windows/install/setup_worker_channel.h"

namespace ef::platform::install {

// Saf esleme (birim testli): isci sonucu -> ProgramRemover::Outcome.
//  UAC reddi -> denied; baslatilamadi / yanit yok / isci calistiramadi -> attempted=false veya removed=false (basari
//  asla varsayilmaz); islem motoru ok -> removed (+ reboot_pending).
core::recovery::ProgramRemover::Outcome program_outcome(const SetupRun& run);

class WorkerProgramRemover final : public core::recovery::ProgramRemover {
public:
    Outcome remove_program() override;
};

}  // namespace ef::platform::install
