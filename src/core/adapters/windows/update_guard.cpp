#include "core/adapters/windows/update_guard.h"

namespace ef::core::adapters::windows {

std::string_view text_key(UpdateRisk r) noexcept {
    switch (r) {
        case UpdateRisk::RebootPending: return "update.reboot_pending";
        case UpdateRisk::DownloadActive: return "update.download_active";
        case UpdateRisk::ManagedByPolicy: return "update.managed";
    }
    return "?";
}

UpdateAdvice advise_for_game_session(const UpdateStatus& s) {
    UpdateAdvice a;
    if (s.reboot_pending.has() && *s.reboot_pending.value) a.risks.push_back(UpdateRisk::RebootPending);
    if (s.active_downloads.has() && *s.active_downloads.value > 0) a.risks.push_back(UpdateRisk::DownloadActive);
    if (s.managed_by_policy.has() && *s.managed_by_policy.value) a.risks.push_back(UpdateRisk::ManagedByPolicy);
    // Politikayla yonetilen cihazda bile sayfa yalniz bilgi icin acilir; urun ayar degistirmez.
    if (!a.risks.empty()) a.action = guided::Action::WindowsUpdate;
    return a;
}

}  // namespace ef::core::adapters::windows
