#include "core/setup/setup_flow.h"

#include <algorithm>
#include <cwctype>
#include <type_traits>

namespace ef::core::setup {

using update::UpdateOutcome;

namespace {

bool rows_visible(SetupPhase p) noexcept {
    return p == SetupPhase::Checking || p == SetupPhase::Ready || p == SetupPhase::Offline || p == SetupPhase::Confirm ||
           p == SetupPhase::UacDenied || p == SetupPhase::AppRunning;
}

void reset_steps(SetupVm& vm) {
    vm.steps.fill(StepState::Pending);
    vm.undo_failed.clear();
    vm.rollback_clean = true;
    vm.fail_kind = FailKind::Rollback;
    vm.fail_detail.clear();
    vm.fail_untrusted = false;
}

void to_confirm_or_running(SetupVm& vm, bool app_running, SetupPhase from) {
    if (app_running) {
        vm.phase = SetupPhase::AppRunning;
        return;
    }
    vm.confirm_from = from;
    vm.license_open = false;
    vm.phase = SetupPhase::Confirm;
}

void apply(SetupVm& vm, const ev::CheckDone& e) {
    if (vm.phase != SetupPhase::Checking) return;
    switch (e.outcome) {
        case UpdateOutcome::UpToDate:
            vm.latest_known = true;
            vm.phase = SetupPhase::Ready;
            break;
        case UpdateOutcome::Mandatory:
        case UpdateOutcome::NotWritable:  // setup indirmeyi kendi klasorune yazar; "yazilamaz" ayrimi yoktur
            vm.newer_version = e.newer_version;
            vm.progress = 0;
            vm.handoff = false;
            vm.phase = SetupPhase::Downloading;
            break;
        case UpdateOutcome::VerifyFailed: vm.phase = SetupPhase::VerifyFailed; break;
        case UpdateOutcome::CheckFailed:
            vm.offline_note = OfflineNote::CheckFailed;
            vm.phase = SetupPhase::Offline;
            break;
        case UpdateOutcome::Checking: break;
    }
}

void apply(SetupVm& vm, const ev::CheckSkipped&) {
    if (vm.phase != SetupPhase::Checking) return;
    vm.latest_known = false;
    vm.phase = SetupPhase::Ready;
}

void apply(SetupVm& vm, const ev::DownloadProgress& e) {
    if (vm.phase != SetupPhase::Downloading || vm.handoff) return;
    vm.progress = std::clamp(e.fraction, 0.0, 1.0);
}

void apply(SetupVm& vm, const ev::DownloadStop&) {
    if (vm.phase != SetupPhase::Downloading || vm.handoff) return;
    vm.offline_note = OfflineNote::DownloadStopped;
    vm.phase = SetupPhase::Offline;
}

void apply(SetupVm& vm, const ev::DownloadDone& e) {
    if (vm.phase != SetupPhase::Downloading || vm.handoff) return;
    switch (e.result) {
        case ev::DownloadResult::Verified:
            vm.progress = 1;
            vm.handoff = true;
            break;
        case ev::DownloadResult::VerifyFailed: vm.phase = SetupPhase::VerifyFailed; break;
        case ev::DownloadResult::NetworkFailed:
            vm.offline_note = OfflineNote::DownloadFailed;
            vm.phase = SetupPhase::Offline;
            break;
        case ev::DownloadResult::Cancelled:
            vm.offline_note = OfflineNote::DownloadStopped;
            vm.phase = SetupPhase::Offline;
            break;
    }
}

void apply(SetupVm& vm, const ev::LaunchFailed&) {
    if (vm.phase != SetupPhase::Downloading || !vm.handoff) return;
    vm.handoff = false;
    vm.offline_note = OfflineNote::LaunchFailed;
    vm.phase = SetupPhase::Offline;
}

void apply(SetupVm& vm, const ev::InstallClicked& e) {
    if (vm.phase != SetupPhase::Ready && vm.phase != SetupPhase::UacDenied && vm.phase != SetupPhase::AppRunning) return;
    to_confirm_or_running(vm, e.app_running, SetupPhase::Ready);
}

void apply(SetupVm& vm, const ev::InstallAnyway& e) {
    if (vm.phase != SetupPhase::Offline) return;
    to_confirm_or_running(vm, e.app_running, SetupPhase::Offline);
}

void apply(SetupVm& vm, const ev::ConfirmAnswer& e) {
    if (vm.phase != SetupPhase::Confirm) return;
    if (!e.yes) {
        vm.phase = vm.confirm_from;
        return;
    }
    reset_steps(vm);
    vm.license_open = false;
    vm.autostart_failed = false;
    vm.phase = SetupPhase::Installing;
}

void apply(SetupVm& vm, const ev::WorkerProgress& e) {
    if (vm.phase != SetupPhase::Installing) return;
    const auto row = row_of(e.p.step);
    if (!row) return;
    auto& st = vm.steps[static_cast<std::size_t>(*row)];
    switch (e.p.rec) {
        case StepRecord::Started:
            if (st != StepState::Failed) st = StepState::Running;
            break;
        case StepRecord::Done:
            if (st != StepState::Failed) st = row_last_step(e.p.step) ? StepState::Done : StepState::Running;
            break;
        case StepRecord::Undone:
            if (st != StepState::Failed) st = StepState::Reverted;
            break;
        case StepRecord::UndoFailed:
            st = StepState::Failed;
            if (std::find(vm.undo_failed.begin(), vm.undo_failed.end(), e.p.step) == vm.undo_failed.end()) vm.undo_failed.push_back(e.p.step);
            vm.rollback_clean = false;
            break;
        case StepRecord::Committed: break;
    }
}

void apply(SetupVm& vm, const ev::WorkerFinished& e) {
    if (vm.phase != SetupPhase::Installing) return;
    if (e.elevation == ElevationOutcome::Denied) {
        reset_steps(vm);
        vm.phase = SetupPhase::UacDenied;
        return;
    }
    vm.phase = SetupPhase::Failed;
    vm.fail_detail = e.detail;
    if (e.elevation != ElevationOutcome::Started) {
        vm.fail_kind = FailKind::NotStarted;
        vm.rollback_clean = true;
        return;
    }
    if (!e.reply && !e.request_sent) {
        // Isci istegi hic almadi (erken cikis / pipe / hello): islem motoru calismadi, hicbir sey yapilmadi.
        reset_steps(vm);
        vm.phase = SetupPhase::Failed;
        vm.fail_detail = e.detail;
        vm.fail_kind = FailKind::NotStarted;
        vm.rollback_clean = true;
        vm.fail_untrusted = e.worker_exit && *e.worker_exit == static_cast<std::uint32_t>(kExitUntrusted);
        return;
    }
    if (!e.reply) {
        // Istek gonderildi ama yanit yok: sonuc BILINMIYOR. Basari da temiz geri alma da varsayilmaz.
        vm.fail_kind = FailKind::ResultUnknown;
        vm.rollback_clean = false;
        return;
    }
    if (!e.reply->error.empty()) {
        vm.fail_kind = FailKind::NotStarted;  // islem motoru calismadi (mesgul / kurtarma)
        vm.rollback_clean = vm.undo_failed.empty();
        return;
    }
    const TxResult& r = e.reply->result;
    if (r.ok) {
        vm.steps.fill(StepState::Done);
        vm.phase = SetupPhase::Done;
        return;
    }
    vm.fail_kind = FailKind::Rollback;
    for (const SetupStep s : r.undo_failed)
        if (std::find(vm.undo_failed.begin(), vm.undo_failed.end(), s) == vm.undo_failed.end()) vm.undo_failed.push_back(s);
    vm.rollback_clean = r.rollback_clean && vm.undo_failed.empty();
    for (std::size_t i = 0; i < kSetupRows; ++i) {
        auto& st = vm.steps[i];
        bool failed = false;
        for (const SetupStep s : vm.undo_failed)
            if (const auto row = row_of(s); row && static_cast<std::size_t>(*row) == i) failed = true;
        if (failed) st = StepState::Failed;
        else if (st == StepState::Running || st == StepState::Done) st = StepState::Reverted;
    }
}

void apply(SetupVm& vm, const ev::Retry& e) {
    switch (vm.phase) {
        case SetupPhase::Offline:
        case SetupPhase::VerifyFailed:
            vm.newer_version.clear();
            vm.progress = 0;
            vm.handoff = false;
            vm.latest_known = false;
            vm.phase = SetupPhase::Checking;
            break;
        case SetupPhase::Failed:
        case SetupPhase::AppRunning: to_confirm_or_running(vm, e.app_running, SetupPhase::Ready); break;
        default: break;
    }
}

void apply(SetupVm& vm, const ev::AutostartResult& e) {
    if (vm.phase == SetupPhase::Done) vm.autostart_failed = !e.ok;
}

void apply(SetupVm& vm, const ev::ToggleAutostart&) {
    if (rows_visible(vm.phase) && vm.phase != SetupPhase::Confirm) vm.autostart = !vm.autostart;
}

void apply(SetupVm& vm, const ev::ToggleLicense&) {
    if (rows_visible(vm.phase) && vm.phase != SetupPhase::Confirm) vm.license_open = !vm.license_open;
}

}  // namespace

SetupVm reduce(SetupVm vm, const SetupEvent& e) {
    std::visit([&vm](const auto& x) { apply(vm, x); }, e);
    if (!rows_visible(vm.phase)) vm.license_open = false;
    return vm;
}

std::optional<SetupRow> row_of(SetupStep s) noexcept {
    switch (s) {
        case SetupStep::CreateDir:
        case SetupStep::CopyExe: return SetupRow::Files;
        case SetupStep::Shortcut:
        case SetupStep::DesktopShortcut: return SetupRow::Shortcut;  // "Kisayollar": Baslat menusu + ortak masaustu
        case SetupStep::ServiceCreate:
        case SetupStep::ServiceStart:
        case SetupStep::UpdatesDir: return SetupRow::Service;
        case SetupStep::Arp: return SetupRow::Arp;
        default: return std::nullopt;
    }
}

bool row_last_step(SetupStep s) noexcept {
    return s == SetupStep::CopyExe || s == SetupStep::DesktopShortcut || s == SetupStep::UpdatesDir || s == SetupStep::Arp;
}

bool launched_from_portable(const std::filesystem::path& self_exe, const InstallLayout& l) {
    if (self_exe.empty() || is_installed_location(self_exe, l)) return false;
    std::wstring name = self_exe.filename().wstring();
    std::wstring want(kExeName);
    if (name.size() != want.size()) return false;
    for (std::size_t i = 0; i < name.size(); ++i)
        if (std::towupper(name[i]) != std::towupper(want[i])) return false;
    return l.user_setup_dir.empty() || !same_path(self_exe.parent_path(), l.user_setup_dir);
}

SetupVm initial_vm(std::string version, bool from_portable) {
    SetupVm vm;
    vm.version = std::move(version);
    vm.from_portable = from_portable;
    return vm;
}

SetupButtons setup_buttons(const SetupVm& vm) noexcept {
    SetupButtons b;
    switch (vm.phase) {
        case SetupPhase::Checking:
            b.rows = b.cancel = b.install = true;
            break;
        case SetupPhase::Ready:
        case SetupPhase::UacDenied:
        case SetupPhase::Confirm:
            b.rows = b.cancel = b.install = b.install_enabled = true;
            break;
        case SetupPhase::AppRunning: b.rows = b.cancel = b.retry = true; break;
        case SetupPhase::Downloading:
            b.stop = true;
            b.stop_enabled = !vm.handoff;
            break;
        case SetupPhase::Offline: b.rows = b.cancel = b.retry = b.install_anyway = true; break;
        case SetupPhase::VerifyFailed: b.cancel = b.retry = true; break;
        case SetupPhase::Installing: break;
        case SetupPhase::Done: b.close = b.open = true; break;
        case SetupPhase::Failed: b.close = b.retry = true; break;
    }
    return b;
}

bool setup_can_close(const SetupVm& vm) noexcept { return vm.phase != SetupPhase::Installing; }

}  // namespace ef::core::setup
