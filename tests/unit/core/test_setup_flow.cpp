// ADR-033 / spec 3: kurulum penceresi durum makinesi (saf). Done yalniz result.ok yanitiyla.
#include "core/setup/setup_flow.h"
#include "ef_test.h"

using namespace ef::core::setup;
using ef::core::ElevationOutcome;
using ef::core::update::UpdateOutcome;

namespace {

SetupVm checking() { return initial_vm("0.3.0", false); }

SetupVm ready() { return reduce(checking(), ev::CheckDone{UpdateOutcome::UpToDate, {}}); }

SetupVm installing() {
    auto vm = reduce(ready(), ev::InstallClicked{false});
    return reduce(vm, ev::ConfirmAnswer{true});
}

SetupReply reply_ok() {
    SetupReply r;
    r.result.ok = true;
    return r;
}

SetupVm progress(SetupVm vm, SetupStep s, StepRecord rec) { return reduce(std::move(vm), ev::WorkerProgress{SetupProgress{s, rec}}); }

StepState row(const SetupVm& vm, SetupRow r) { return vm.steps[static_cast<std::size_t>(r)]; }

}  // namespace

EF_TEST(setup_flow_check_results) {
    const auto c = checking();
    EF_CHECK(c.phase == SetupPhase::Checking && c.version == "0.3.0" && c.autostart && !c.license_open);
    const auto up = reduce(c, ev::CheckDone{UpdateOutcome::UpToDate, {}});
    EF_CHECK(up.phase == SetupPhase::Ready && up.latest_known);
    const auto newer = reduce(c, ev::CheckDone{UpdateOutcome::Mandatory, "0.3.1"});
    EF_CHECK(newer.phase == SetupPhase::Downloading && newer.newer_version == "0.3.1" && newer.progress == 0 && !newer.handoff);
    const auto nw = reduce(c, ev::CheckDone{UpdateOutcome::NotWritable, "0.3.1"});
    EF_CHECK(nw.phase == SetupPhase::Downloading);
    const auto off = reduce(c, ev::CheckDone{UpdateOutcome::CheckFailed, {}});
    EF_CHECK(off.phase == SetupPhase::Offline && off.offline_note == OfflineNote::CheckFailed && !off.latest_known);
    const auto bad = reduce(c, ev::CheckDone{UpdateOutcome::VerifyFailed, {}});
    EF_CHECK(bad.phase == SetupPhase::VerifyFailed);
    // Denetim atlandi: Ready ama "en guncel" denmez.
    const auto skip = reduce(c, ev::CheckSkipped{});
    EF_CHECK(skip.phase == SetupPhase::Ready && !skip.latest_known);
    // Gec gelen sonuc baska evreyi degistirmez.
    EF_CHECK(reduce(up, ev::CheckDone{UpdateOutcome::Mandatory, "9.9.9"}).phase == SetupPhase::Ready);
    EF_CHECK(reduce(c, ev::CheckDone{UpdateOutcome::Checking, {}}).phase == SetupPhase::Checking);
}

EF_TEST(setup_flow_download_progress_stop_verify) {
    auto d = reduce(checking(), ev::CheckDone{UpdateOutcome::Mandatory, "0.3.1"});
    d = reduce(d, ev::DownloadProgress{0.4});
    EF_CHECK(d.progress == 0.4);
    EF_CHECK(reduce(d, ev::DownloadProgress{7}).progress == 1.0);
    EF_CHECK(reduce(d, ev::DownloadProgress{-1}).progress == 0.0);
    // Durdur: Offline (indirme durduruldu); Yeniden dene yeni denetim.
    const auto stopped = reduce(d, ev::DownloadStop{});
    EF_CHECK(stopped.phase == SetupPhase::Offline && stopped.offline_note == OfflineNote::DownloadStopped);
    const auto again = reduce(stopped, ev::Retry{});
    EF_CHECK(again.phase == SetupPhase::Checking && again.newer_version.empty() && again.progress == 0);
    // Dogrulama hatasi: VerifyFailed; Yeniden dene -> Checking.
    const auto vf = reduce(d, ev::DownloadDone{ev::DownloadResult::VerifyFailed});
    EF_CHECK(vf.phase == SetupPhase::VerifyFailed);
    EF_CHECK(!setup_buttons(vf).install && !setup_buttons(vf).install_anyway && setup_buttons(vf).retry);
    EF_CHECK(reduce(vf, ev::Retry{}).phase == SetupPhase::Checking);
    EF_CHECK(reduce(vf, ev::InstallClicked{false}).phase == SetupPhase::VerifyFailed);
    // Ag hatasi / iptal: Offline.
    EF_CHECK(reduce(d, ev::DownloadDone{ev::DownloadResult::NetworkFailed}).offline_note == OfflineNote::DownloadFailed);
    EF_CHECK(reduce(d, ev::DownloadDone{ev::DownloadResult::Cancelled}).offline_note == OfflineNote::DownloadStopped);
    // Dogrulandi: devir (indirilen dosya baslatilir); Durdur artik etkisiz ve dugme devre disi.
    const auto ok = reduce(d, ev::DownloadDone{ev::DownloadResult::Verified});
    EF_CHECK(ok.phase == SetupPhase::Downloading && ok.handoff && ok.progress == 1.0);
    EF_CHECK(setup_buttons(ok).stop && !setup_buttons(ok).stop_enabled);
    EF_CHECK(reduce(ok, ev::DownloadStop{}).phase == SetupPhase::Downloading);
    EF_CHECK(reduce(ok, ev::DownloadProgress{0.1}).progress == 1.0);
    const auto lf = reduce(ok, ev::LaunchFailed{});
    EF_CHECK(lf.phase == SetupPhase::Offline && lf.offline_note == OfflineNote::LaunchFailed && !lf.handoff);
    // Indirme sirasinda kurulum yok.
    EF_CHECK(reduce(d, ev::InstallClicked{false}).phase == SetupPhase::Downloading);
}

EF_TEST(setup_flow_confirm_yes_no) {
    const auto r = ready();
    EF_CHECK(setup_buttons(r).install && setup_buttons(r).install_enabled && setup_buttons(r).cancel && setup_buttons(r).rows);
    // Checking'de Kur gorunur ama devre disi ve etkisiz.
    EF_CHECK(setup_buttons(checking()).install && !setup_buttons(checking()).install_enabled);
    EF_CHECK(reduce(checking(), ev::InstallClicked{false}).phase == SetupPhase::Checking);
    const auto c = reduce(r, ev::InstallClicked{false});
    EF_CHECK(c.phase == SetupPhase::Confirm && c.confirm_from == SetupPhase::Ready);
    EF_CHECK(reduce(c, ev::ConfirmAnswer{false}).phase == SetupPhase::Ready);
    const auto i = reduce(c, ev::ConfirmAnswer{true});
    EF_CHECK(i.phase == SetupPhase::Installing);
    for (const auto s : i.steps) EF_CHECK(s == StepState::Pending);
    EF_CHECK(!setup_can_close(i) && setup_can_close(r));
    const auto b = setup_buttons(i);
    EF_CHECK(!b.cancel && !b.install && !b.retry && !b.close && !b.open && !b.stop);
}

EF_TEST(setup_flow_app_running) {
    const auto a = reduce(ready(), ev::InstallClicked{true});
    EF_CHECK(a.phase == SetupPhase::AppRunning && setup_buttons(a).retry && !setup_buttons(a).install);
    EF_CHECK(reduce(a, ev::Retry{true}).phase == SetupPhase::AppRunning);
    const auto c = reduce(a, ev::Retry{false});
    EF_CHECK(c.phase == SetupPhase::Confirm && c.confirm_from == SetupPhase::Ready);
}

EF_TEST(setup_flow_offline_install_anyway) {
    const auto off = reduce(checking(), ev::CheckDone{UpdateOutcome::CheckFailed, {}});
    const auto b = setup_buttons(off);
    EF_CHECK(b.retry && b.cancel && b.install_anyway && !b.install);
    const auto c = reduce(off, ev::InstallAnyway{false});
    EF_CHECK(c.phase == SetupPhase::Confirm && c.confirm_from == SetupPhase::Offline);
    EF_CHECK(reduce(c, ev::ConfirmAnswer{false}).phase == SetupPhase::Offline);  // Hayir: Offline'a doner
    EF_CHECK(reduce(c, ev::ConfirmAnswer{true}).phase == SetupPhase::Installing);
    EF_CHECK(reduce(off, ev::InstallAnyway{true}).phase == SetupPhase::AppRunning);
    EF_CHECK(reduce(ready(), ev::InstallAnyway{false}).phase == SetupPhase::Ready);  // yalniz Offline'da
    EF_CHECK(reduce(off, ev::Retry{}).phase == SetupPhase::Checking);
}

EF_TEST(setup_flow_progress_row_mapping) {
    EF_CHECK(row_of(SetupStep::CreateDir) == SetupRow::Files && row_of(SetupStep::CopyExe) == SetupRow::Files);
    EF_CHECK(row_of(SetupStep::Shortcut) == SetupRow::Shortcut && row_of(SetupStep::Arp) == SetupRow::Arp);
    EF_CHECK(row_of(SetupStep::DesktopShortcut) == SetupRow::Shortcut && !row_of(SetupStep::DesktopShortcutRemove));
    EF_CHECK(row_of(SetupStep::ServiceCreate) == SetupRow::Service && row_of(SetupStep::ServiceStart) == SetupRow::Service &&
              row_of(SetupStep::UpdatesDir) == SetupRow::Service);
    EF_CHECK(!row_of(SetupStep::Probe) && !row_of(SetupStep::Commit) && !row_of(SetupStep::FilesRemove));
    auto vm = installing();
    vm = progress(vm, SetupStep::Probe, StepRecord::Started);  // satir yok: degisiklik yok
    for (const auto s : vm.steps) EF_CHECK(s == StepState::Pending);
    vm = progress(vm, SetupStep::CreateDir, StepRecord::Started);
    EF_CHECK(row(vm, SetupRow::Files) == StepState::Running);
    vm = progress(vm, SetupStep::CreateDir, StepRecord::Done);  // satirin son adimi degil
    EF_CHECK(row(vm, SetupRow::Files) == StepState::Running);
    vm = progress(vm, SetupStep::CopyExe, StepRecord::Started);
    vm = progress(vm, SetupStep::CopyExe, StepRecord::Done);
    EF_CHECK(row(vm, SetupRow::Files) == StepState::Done);
    vm = progress(vm, SetupStep::Shortcut, StepRecord::Started);
    EF_CHECK(row(vm, SetupRow::Shortcut) == StepState::Running && row(vm, SetupRow::Service) == StepState::Pending);
    vm = progress(vm, SetupStep::Shortcut, StepRecord::Done);  // "Kisayollar": masaustu kisayolu henuz yok
    EF_CHECK(row(vm, SetupRow::Shortcut) == StepState::Running);
    vm = progress(vm, SetupStep::DesktopShortcut, StepRecord::Started);
    vm = progress(vm, SetupStep::DesktopShortcut, StepRecord::Done);
    EF_CHECK(row(vm, SetupRow::Shortcut) == StepState::Done);
    vm = progress(vm, SetupStep::ServiceCreate, StepRecord::Started);
    vm = progress(vm, SetupStep::ServiceCreate, StepRecord::Done);
    vm = progress(vm, SetupStep::ServiceStart, StepRecord::Started);
    EF_CHECK(row(vm, SetupRow::Service) == StepState::Running);
    // Hata: geri alma kayitlari satirlari Reverted / Failed yapar.
    vm = progress(vm, SetupStep::ServiceStart, StepRecord::Undone);
    EF_CHECK(row(vm, SetupRow::Service) == StepState::Reverted);
    vm = progress(vm, SetupStep::ServiceCreate, StepRecord::Undone);
    vm = progress(vm, SetupStep::Shortcut, StepRecord::UndoFailed);
    EF_CHECK(row(vm, SetupRow::Shortcut) == StepState::Failed && !vm.rollback_clean);
    EF_CHECK(vm.undo_failed.size() == 1 && vm.undo_failed[0] == SetupStep::Shortcut);
    vm = progress(vm, SetupStep::Shortcut, StepRecord::Undone);  // Failed geri donmez
    EF_CHECK(row(vm, SetupRow::Shortcut) == StepState::Failed);
    vm = progress(vm, SetupStep::CopyExe, StepRecord::Undone);
    EF_CHECK(row(vm, SetupRow::Files) == StepState::Reverted);
    // Kurulum disi evrede ilerleme yok sayilir.
    EF_CHECK(row(progress(ready(), SetupStep::CreateDir, StepRecord::Started), SetupRow::Files) == StepState::Pending);
}

EF_TEST(setup_flow_no_done_without_ok_reply) {
    // Yanit result.ok -> Done (tum satirlar Tamam).
    const auto done = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, reply_ok()});
    EF_CHECK(done.phase == SetupPhase::Done);
    for (const auto s : done.steps) EF_CHECK(s == StepState::Done);
    EF_CHECK(setup_buttons(done).close && setup_buttons(done).open);
    // Yanit yok: sonuc bilinmiyor; Done degil, temiz geri alma da varsayilmaz.
    const auto unknown = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, std::nullopt});
    EF_CHECK(unknown.phase == SetupPhase::Failed && unknown.fail_kind == FailKind::ResultUnknown && !unknown.rollback_clean);
    // Isci calistiramadi (error dolu): Done degil — result.ok yanlislikla true gelse bile.
    SetupReply busy = reply_ok();
    busy.error = "busy";
    const auto nb = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, busy});
    EF_CHECK(nb.phase == SetupPhase::Failed && nb.fail_kind == FailKind::NotStarted);
    // UAC baslatilamadi (Failed): Done degil.
    const auto ns = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Failed, reply_ok()});
    EF_CHECK(ns.phase == SetupPhase::Failed && ns.fail_kind == FailKind::NotStarted && ns.rollback_clean);
    // Isci adimlari Done olsa da sonuc yanitsiz Done olmaz.
    auto vm = installing();
    for (const auto s : {SetupStep::CopyExe, SetupStep::Shortcut, SetupStep::DesktopShortcut, SetupStep::UpdatesDir, SetupStep::Arp})
        vm = progress(vm, s, StepRecord::Done);
    EF_CHECK(vm.phase == SetupPhase::Installing);
    EF_CHECK(reduce(vm, ev::WorkerFinished{ElevationOutcome::Started, std::nullopt}).phase == SetupPhase::Failed);
    // Done yalniz Installing'den: baska evrede yanit etkisiz.
    EF_CHECK(reduce(ready(), ev::WorkerFinished{ElevationOutcome::Started, reply_ok()}).phase == SetupPhase::Ready);
}

EF_TEST(setup_flow_failure_before_request_is_not_started) {
    // Isci istegi almadan cikti (UAC kabul edildi, imza reddi kExitUntrusted): hicbir sey yapilmadi -> NotStarted.
    const auto untrusted = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, std::nullopt, false,
                                                                   static_cast<std::uint32_t>(ef::core::kExitUntrusted),
                                                                   "setup worker exited early (14)"});
    EF_CHECK(untrusted.phase == SetupPhase::Failed);
    EF_CHECK(untrusted.fail_kind == FailKind::NotStarted);
    EF_CHECK(untrusted.rollback_clean);
    EF_CHECK(untrusted.fail_untrusted);
    EF_CHECK(untrusted.fail_detail == "setup worker exited early (14)");
    for (const auto s : untrusted.steps) EF_CHECK(s == StepState::Pending);
    EF_CHECK(setup_buttons(untrusted).retry);
    // Baska erken cikis kodu / pipe-hello hatasi: NotStarted, imza reddi degil, ayrinti korunur.
    const auto early = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, std::nullopt, false, 12u, "setup worker exited early (12)"});
    EF_CHECK(early.fail_kind == FailKind::NotStarted && early.rollback_clean && !early.fail_untrusted);
    const auto hello = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, std::nullopt, false, std::nullopt, "hello ack: timeout"});
    EF_CHECK(hello.fail_kind == FailKind::NotStarted && hello.rollback_clean && !hello.fail_untrusted);
    EF_CHECK(hello.fail_detail == "hello ack: timeout");
    // Istek gonderildi, yanit yok: ResultUnknown degismez (cikis kodu verilse bile temiz sayilmaz).
    const auto unknown = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, std::nullopt, true, std::nullopt,
                                                                 "setup result unknown: timeout"});
    EF_CHECK(unknown.fail_kind == FailKind::ResultUnknown && !unknown.rollback_clean && !unknown.fail_untrusted);
    EF_CHECK(unknown.fail_detail == "setup result unknown: timeout");
    // Yeniden deneme -> onay -> Installing: onceki ayrinti/imza isareti temizlenir.
    auto again = reduce(untrusted, ev::Retry{false});
    again = reduce(again, ev::ConfirmAnswer{true});
    EF_CHECK(again.phase == SetupPhase::Installing);
    EF_CHECK(again.fail_detail.empty() && !again.fail_untrusted);
}

EF_TEST(setup_flow_failed_rollback) {
    auto vm = installing();
    vm = progress(vm, SetupStep::CopyExe, StepRecord::Done);
    vm = progress(vm, SetupStep::Shortcut, StepRecord::Started);
    SetupReply r;
    r.result.ok = false;
    r.result.failed_at = SetupStep::Shortcut;
    r.result.rollback_clean = true;
    const auto clean = reduce(vm, ev::WorkerFinished{ElevationOutcome::Started, r});
    EF_CHECK(clean.phase == SetupPhase::Failed && clean.fail_kind == FailKind::Rollback && clean.rollback_clean);
    EF_CHECK(row(clean, SetupRow::Files) == StepState::Reverted && row(clean, SetupRow::Shortcut) == StepState::Reverted);
    EF_CHECK(row(clean, SetupRow::Service) == StepState::Pending);
    EF_CHECK(setup_buttons(clean).close && setup_buttons(clean).retry);
    // Yeniden dene: tekrar Evet/Hayir (oyun / uygulama acik ise AppRunning).
    EF_CHECK(reduce(clean, ev::Retry{false}).phase == SetupPhase::Confirm);
    EF_CHECK(reduce(clean, ev::Retry{true}).phase == SetupPhase::AppRunning);
    // UndoFailed: temiz degil, adim listelenir.
    r.result.rollback_clean = false;
    r.result.undo_failed = {SetupStep::CopyExe};
    const auto dirty = reduce(vm, ev::WorkerFinished{ElevationOutcome::Started, r});
    EF_CHECK(!dirty.rollback_clean && dirty.undo_failed.size() == 1 && dirty.undo_failed[0] == SetupStep::CopyExe);
    EF_CHECK(row(dirty, SetupRow::Files) == StepState::Failed && row(dirty, SetupRow::Shortcut) == StepState::Reverted);
    // Sonuc temiz dese de ilerleme kaydinda UndoFailed varsa temiz sayilmaz.
    auto vm2 = progress(vm, SetupStep::Shortcut, StepRecord::UndoFailed);
    SetupReply r2;
    r2.result.ok = false;
    r2.result.rollback_clean = true;
    const auto mixed = reduce(vm2, ev::WorkerFinished{ElevationOutcome::Started, r2});
    EF_CHECK(!mixed.rollback_clean && row(mixed, SetupRow::Shortcut) == StepState::Failed);
}

EF_TEST(setup_flow_uac_denied) {
    auto vm = progress(installing(), SetupStep::CreateDir, StepRecord::Started);  // olmamali ama gelse bile
    const auto d = reduce(vm, ev::WorkerFinished{ElevationOutcome::Denied, std::nullopt});
    EF_CHECK(d.phase == SetupPhase::UacDenied);
    for (const auto s : d.steps) EF_CHECK(s == StepState::Pending);
    const auto b = setup_buttons(d);
    EF_CHECK(b.install && b.install_enabled && b.cancel && b.rows);  // Ready'ye doner (not satiriyla)
    EF_CHECK(reduce(d, ev::InstallClicked{false}).phase == SetupPhase::Confirm);
}

EF_TEST(setup_flow_toggles_and_autostart_result) {
    auto r = ready();
    r = reduce(r, ev::ToggleAutostart{});
    EF_CHECK(!r.autostart);
    r = reduce(r, ev::ToggleLicense{});
    EF_CHECK(r.license_open);
    // Onay acilinca lisans paneli kapanir; onay sirasinda anahtarlar degismez.
    const auto c = reduce(r, ev::InstallClicked{false});
    EF_CHECK(!c.license_open);
    EF_CHECK(reduce(c, ev::ToggleAutostart{}).autostart == c.autostart);
    EF_CHECK(!reduce(c, ev::ToggleLicense{}).license_open);
    // Indirme ekraninda satir yok: panel acilmaz.
    const auto d = reduce(checking(), ev::CheckDone{UpdateOutcome::Mandatory, "0.3.1"});
    EF_CHECK(!reduce(d, ev::ToggleLicense{}).license_open);
    // Autostart sonucu yalniz Done'da not olur.
    const auto done = reduce(installing(), ev::WorkerFinished{ElevationOutcome::Started, reply_ok()});
    EF_CHECK(reduce(done, ev::AutostartResult{false}).autostart_failed);
    EF_CHECK(!reduce(done, ev::AutostartResult{true}).autostart_failed);
    EF_CHECK(!reduce(ready(), ev::AutostartResult{false}).autostart_failed);
}

EF_TEST(setup_flow_launched_from_portable) {
    const auto l = make_layout({L"C:\\Program Files", L"C:\\ProgramData", L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs",
                                L"C:\\Users\\u\\AppData\\Local"});
    EF_CHECK(launched_from_portable(L"C:\\Users\\u\\Desktop\\EcuFps\\EcuFps.exe", l));
    EF_CHECK(launched_from_portable(L"D:\\oyun\\ECUFPS.EXE", l));
    EF_CHECK(!launched_from_portable(L"C:\\Users\\u\\Downloads\\EcuFps-Setup-0.3.0.exe", l));
    EF_CHECK(!launched_from_portable(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe", l));
    EF_CHECK(!launched_from_portable(L"C:\\Users\\u\\AppData\\Local\\EcuFps\\setup\\EcuFps.exe", l));
    EF_CHECK(!launched_from_portable(L"", l));
}
