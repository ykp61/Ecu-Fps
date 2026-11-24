#pragma once
// ADR-033 / spec 2 ve 4: kurulum journal'i (ProgramData\EcuFps\setup\setup.journal). Klasor SY+BA-yalniz korumali
// DACL ile olusturulur/yeniden uygulanir. append: ac / satir ekle / FlushFileBuffers (donmeden once kalici).
// Kaldirmada MachineDataRemove klasoru silse de sonraki kayit klasoru yeniden kurar; clear dosyayi siler ve bos kalan
// setup ile ProgramData\EcuFps klasorlerini kaldirir (geride bir sey kalmaz). Bu oturumda calistirilmaz.

#include <filesystem>
#include <vector>

#include "core/setup/install_tx.h"
#include "core/setup/layout.h"

namespace ef::platform::install {

class FsSetupJournal final : public core::setup::SetupJournal {
public:
    explicit FsSetupJournal(const core::setup::InstallLayout& l) : machine_dir_(l.machine_dir), dir_(l.setup_dir), file_(l.setup_journal) {}

    contracts::Status append(const core::setup::JournalEntry& e) override;
    contracts::Result<std::vector<core::setup::JournalEntry>> read() override;
    contracts::Status clear() override;

private:
    contracts::Status ensure_dir();
    std::filesystem::path machine_dir_, dir_, file_;
};

}  // namespace ef::platform::install
