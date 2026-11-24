// RDY-01: Hit -> UIA AutomationId. Dinamik kimlikler kHitRanges tablosundan uretilir (onek + indeks);
// bloklar ust uste binmedigi icin her kimligin tek bir AutomationId'si vardir. Platform cagrisi yok.
#include <iterator>
#include <string>

#include "ui/shell/screen.h"

namespace ef::ui {

namespace {

const wchar_t* fixed_automation_id(Hit h) {
    switch (h) {
        case Hit::Toggle: return L"perf.toggle";
        case Hit::Yes: return L"perf.yes";
        case Hit::No: return L"perf.no";
        case Hit::Retry: return L"perf.retry";
        case Hit::SysTabCleanup: return L"systab.cleanup";
        case Hit::SettingsStrip: return L"settings.strip";
        case Hit::SettingsCleanupHint: return L"settings.cleanup_hint";
        case Hit::TopbarStatus: return L"topbar.status";
        case Hit::OpenReadiness: return L"overview.open_readiness";
        case Hit::GamesWhy: return L"games.why";
        case Hit::UpdateStatus: return L"update.status";
        case Hit::UpdateNow: return L"update.now";
        case Hit::UpdateQuit: return L"update.quit";
        case Hit::UpdateRestore: return L"update.restore";
        case Hit::UpdateOpenPage: return L"update.open_page";
        case Hit::UpdateRepairService: return L"update.repair_service";
        case Hit::HistoryFilterAll: return L"history.filter.all";
        case Hit::HistoryFilterPerf: return L"history.filter.perf";
        case Hit::HistoryFilterFixes: return L"history.filter.fixes";
        case Hit::HistoryFilterStartup: return L"history.filter.startup";
        case Hit::HistoryTabAll: return L"history.tab.all";
        case Hit::HistoryTabChanges: return L"history.tab.changes";
        case Hit::HistoryTabTests: return L"history.tab.tests";
        case Hit::SourceBuiltin: return L"measure.source.builtin";
        case Hit::SourceGame: return L"measure.source.game";
        case Hit::ScopeGeneral: return L"measure.scope.general";
        case Hit::ScopeBalance: return L"measure.scope.balance";
        case Hit::LiveView: return L"measure.live_view";
        case Hit::LiveStep: return L"measure.live.step";
        case Hit::LiveFps: return L"measure.live.fps";
        case Hit::LiveLoad: return L"measure.live.load";
        case Hit::TestStart: return L"measure.start";
        case Hit::TestStop: return L"measure.stop";
        case Hit::Details: return L"perf.details";
        case Hit::WelcomeStart: return L"welcome.start";
        case Hit::DetectionRetry: return L"gate.retry";
        case Hit::GamesGuides: return L"games.guides";
        case Hit::GamesAddExe: return L"games.add_exe";
        case Hit::GamesFilterAll: return L"games.filter.all";
        case Hit::GamesFilterProtected: return L"games.filter.protected";
        case Hit::GamesFilterAdvice: return L"games.filter.advice";
        case Hit::GamesRescan: return L"games.rescan";
        case Hit::PerfKeep: return L"perf.keep_current";
        case Hit::PerfDriftReapply: return L"perf.drift.reapply";
        case Hit::PerfDriftKeep: return L"perf.drift.keep";
        case Hit::StartupRescan: return L"startup.rescan";
        case Hit::OpenMeasure: return L"overview.open_tests";
        case Hit::SpeedStart: return L"measure.speed.start";
        case Hit::SpeedStop: return L"measure.speed.stop";
        case Hit::SpeedServerPing: return L"measure.speed.server_ping";
        case Hit::ModalInput: return L"modal.input";
        case Hit::Autostart: return L"settings.autostart";
        case Hit::AppAnimations: return L"settings.animations";
        case Hit::CleanScan: return L"cleanup.scan";
        case Hit::CleanRun: return L"cleanup.run";
        case Hit::CleanYes: return L"cleanup.yes";
        case Hit::CleanNo: return L"cleanup.no";
        case Hit::CleanStop: return L"cleanup.stop";
        case Hit::CleanScanCancel: return L"cleanup.scan_cancel";
        case Hit::CleanStorageSettings: return L"cleanup.storage_settings";
        case Hit::CleanMore: return L"cleanup.more";
        case Hit::StartupApply: return L"startup.apply";
        case Hit::CheckStart: return L"check.start";
        case Hit::CheckYes: return L"check.yes";
        case Hit::CheckNo: return L"check.no";
        case Hit::StartupYes: return L"startup.yes";
        case Hit::StartupNo: return L"startup.no";
        case Hit::ModeQuick: return L"measure.mode.quick";
        case Hit::ModeFull: return L"measure.mode.full";
        case Hit::ChkRescan: return L"readiness.rescan";
        case Hit::ChkNetTest: return L"readiness.nettest";
        case Hit::ChkCheckup: return L"readiness.checkup";
        case Hit::SupportDumps: return L"settings.support.dumps";
        case Hit::SupportCreate: return L"settings.support.create";
        case Hit::SettingsGamesShow: return L"settings.games.toggle";
        case Hit::SettingsLicenses: return L"settings.licenses.toggle";
        case Hit::SettingsAdvanced: return L"settings.advanced.toggle";
        case Hit::AutostartNoticeOff: return L"autostart.notice.off";
        case Hit::AutostartNotice: return L"autostart.notice";
        case Hit::HistoryExport: return L"history.export";
        case Hit::EnduranceStart: return L"measure.endurance.start";
        case Hit::EnduranceStop: return L"measure.endurance.stop";
        case Hit::ProofStart: return L"measure.proof.start";
        case Hit::ProofStop: return L"measure.proof.stop";
        case Hit::BalanceReport: return L"measure.balance.report";
        case Hit::SysJump0: return L"systools.jump.cleanup";
        case Hit::SysJump1: return L"systools.jump.startup";
        case Hit::SysJump2: return L"systools.jump.services";
        case Hit::SysJump3: return L"systools.jump.system";
        case Hit::Modal: return L"modal";
        case Hit::ModalTitle: return L"modal.title";
        case Hit::ModalBody0: return L"modal.body.0";
        case Hit::ModalBody1: return L"modal.body.1";
        case Hit::ModalCheck: return L"modal.check";
        case Hit::ModalNo: return L"modal.no";
        case Hit::ModalYes: return L"modal.yes";
        case Hit::UpdatesCheck: return L"updates.check";
        case Hit::UpdatesCancel: return L"updates.cancel";
        case Hit::SetupCancel: return L"setup.cancel";
        case Hit::SetupInstall: return L"setup.install";
        case Hit::SetupRetry: return L"setup.retry";
        case Hit::SetupInstallAnyway: return L"setup.install_anyway";
        case Hit::SetupStop: return L"setup.stop";
        case Hit::SetupClose: return L"setup.close";
        case Hit::SetupOpen: return L"setup.open";
        case Hit::SetupAutostart: return L"setup.autostart";
        case Hit::SetupLicense: return L"setup.license";
        case Hit::SetupLicenseClose: return L"setup.license_close";
        case Hit::InstallOfferInstall: return L"install_offer.install";
        case Hit::InstallOfferText: return L"install_offer";
        case Hit::SettingsUpdateCheck: return L"settings.update.check";
        case Hit::SettingsLanguage: return L"settings.language";
        case Hit::PerfAdvanced: return L"perf.advanced";
        case Hit::PerfTips: return L"perf.tips";
        case Hit::BiosGuideToggle: return L"readiness.bios_guide";
        case Hit::SettingsLinkEula: return L"settings.about.eula";
        case Hit::SettingsLinkPrivacy: return L"settings.about.privacy";
        case Hit::SettingsLinkTransparency: return L"settings.about.transparency";
        default: return nullptr;
    }
}

}  // namespace

std::wstring hit_automation_id(Hit h) {
    static constexpr const wchar_t* nav[] = {L"nav.overview", L"nav.readiness", L"nav.games",   L"nav.measurements",
                                             L"nav.cleanup",  L"nav.startup",   L"nav.history", L"nav.settings",
                                             L"nav.system",   L"nav.updates"};
    static_assert(std::size(nav) == kPageCount, "her sayfanin nav AutomationId'si olmali");
    const HitSlot slot = classify_hit(h);
    switch (slot.kind) {
        case HitKind::None: return L"unknown";
        case HitKind::Nav: return nav[slot.index];
        case HitKind::Fixed: {
            const wchar_t* id = fixed_automation_id(h);
            return id ? id : L"unknown";
        }
        default: break;
    }
    for (const auto& r : kHitRanges)
        if (r.kind == slot.kind && r.uia_prefix) return r.uia_prefix + std::to_wstring(slot.index);
    return L"unknown";
}

}  // namespace ef::ui
