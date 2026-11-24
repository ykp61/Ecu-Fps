#pragma once
// ADR-033 / spec 3: kurulum penceresinin durum makinesi (saf; pencere, ag, dosya yok). Pencere olaylari reduce() ile
// SetupVm'e islenir; cizim yalniz SetupVm'i okur. Kural: "Kurulum tamamlandi" (Done) yalniz isciden gelen ve
// result.ok == true olan bir yanitla gosterilir; UAC reddi, bilinmeyen sonuc veya hata asla basari sayilmaz.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/roles.h"
#include "core/setup/layout.h"
#include "core/setup/setup_protocol.h"
#include "core/update/update_check.h"

namespace ef::core::setup {

enum class SetupPhase { Checking, Ready, Downloading, Offline, VerifyFailed, Confirm, Installing, Done, Failed, UacDenied, AppRunning };

// Penceredeki dort adim satiri (spec 3): Dosyalar (CreateDir + CopyExe), Kisayollar (Shortcut + DesktopShortcut), Yardimci hizmet
// (ServiceCreate + ServiceStart + UpdatesDir), Windows Uygulamalar kaydi (Arp).
enum class SetupRow { Files, Shortcut, Service, Arp };
inline constexpr std::size_t kSetupRows = 4;
enum class StepState { Pending, Running, Done, Reverted, Failed };

// Offline evresinin alt nedeni (ayni dugmeler: Yeniden dene / Vazgec / "<surum> kur").
enum class OfflineNote { CheckFailed, DownloadStopped, DownloadFailed, LaunchFailed };
// Failed evresinin nedeni.
enum class FailKind { Rollback, NotStarted, ResultUnknown };

struct SetupVm {
    SetupPhase phase = SetupPhase::Checking;
    std::string version, newer_version;
    double progress = 0;  // indirme 0..1
    std::array<StepState, kSetupRows> steps{};
    bool autostart = true;
    bool license_open = false;
    bool rollback_clean = true;
    bool from_portable = false;
    // Ek durum
    // Imzali manifest bu surumun en guncel oldugunu dogruladi (UpToDate). Denetim atlandiysa (EF_UPDATE_CHECK=0 /
    // gomulu anahtar yok) veya yapilamadiysa false: pencere "en guncel surum" demez (uydurma yok).
    bool latest_known = false;
    bool handoff = false;         // indirilen dogrulandi; yeni surum baslatiliyor
    OfflineNote offline_note = OfflineNote::CheckFailed;
    FailKind fail_kind = FailKind::Rollback;
    std::vector<SetupStep> undo_failed;  // geri alinamayan adimlar (isci kaydi / sonuc)
    SetupPhase confirm_from = SetupPhase::Ready;  // Evet/Hayir'da Hayir donusu
    bool autostart_failed = false;  // Done: Run degeri yazilamadi / dogrulanamadi
    // Failed (NotStarted / ResultUnknown): kanaldan gelen teknik ayrinti (ekranda ikincil satir; bos ise gosterilmez).
    std::string fail_detail;
    // NotStarted: yukseltilmis isci imza denetiminde reddetti (cikis kodu kExitUntrusted).
    bool fail_untrusted = false;
};

namespace ev {
struct CheckDone {
    update::UpdateOutcome outcome = update::UpdateOutcome::CheckFailed;
    std::string newer_version;
};
struct CheckSkipped {};
struct DownloadProgress {
    double fraction = 0;
};
struct DownloadStop {};
enum class DownloadResult { Verified, VerifyFailed, NetworkFailed, Cancelled };
struct DownloadDone {
    DownloadResult result = DownloadResult::NetworkFailed;
};
struct LaunchFailed {};
struct InstallClicked {
    bool app_running = false;
};
struct InstallAnyway {
    bool app_running = false;
};
struct ConfirmAnswer {
    bool yes = false;
};
struct WorkerProgress {
    SetupProgress p;
};
struct WorkerFinished {
    ElevationOutcome elevation = ElevationOutcome::Failed;
    std::optional<SetupReply> reply;
    // Istek cercevesi yazilmaya calisildi mi. false: isci istegi hic almadi (erken cikis, pipe/hello hatasi) ->
    // NotStarted (hicbir sey yapilmadi). true + yanit yok -> ResultUnknown. Varsayilan true: belirsizlikte basari
    // veya "degisiklik yok" varsayilmaz.
    bool request_sent = true;
    std::optional<std::uint32_t> worker_exit;  // isci istekten once ciktiysa cikis kodu
    std::string detail;                        // teknik ayrinti (kanal metni)
};
struct Retry {
    bool app_running = false;
};
struct AutostartResult {
    bool ok = false;
};
struct ToggleAutostart {};
struct ToggleLicense {};
}  // namespace ev

using SetupEvent = std::variant<ev::CheckDone, ev::CheckSkipped, ev::DownloadProgress, ev::DownloadStop, ev::DownloadDone, ev::LaunchFailed,
                                ev::InstallClicked, ev::InstallAnyway, ev::ConfirmAnswer, ev::WorkerProgress, ev::WorkerFinished, ev::Retry,
                                ev::AutostartResult, ev::ToggleAutostart, ev::ToggleLicense>;

SetupVm reduce(SetupVm vm, const SetupEvent& e);

// Isci adimi -> pencere satiri (Probe / Commit / kaldirma adimlari satir degildir).
std::optional<SetupRow> row_of(SetupStep s) noexcept;
// Satirin son adimi (Done kaydi satiri tamamlar).
bool row_last_step(SetupStep s) noexcept;

// Kurulum penceresi taşınabilir bir kopyadan mi acildi: dosya adi EcuFps.exe, kurulum yeri degil, kullanici kurulum
// indirme klasoru de degil (indirilen kurulum dosyasi EcuFps-Setup-<surum>.exe adini tasir).
bool launched_from_portable(const std::filesystem::path& self_exe, const InstallLayout& l);

// Pencere ilk durumu: Checking (denetim baslar), surum, tasinabilir mi.
SetupVm initial_vm(std::string version, bool from_portable);

// Odak / Tab sirasi icin hangi dugmelerin gorundugu (cizim ve pencere ayni kurali kullanir).
struct SetupButtons {
    bool cancel = false, install = false, install_enabled = false, retry = false, install_anyway = false, stop = false, stop_enabled = false,
         close = false, open = false, rows = false;
};
SetupButtons setup_buttons(const SetupVm& vm) noexcept;
// Pencere kapatilabilir mi (kurulum surerken kapatma yok: sonuc isciden gelir).
bool setup_can_close(const SetupVm& vm) noexcept;

}  // namespace ef::core::setup
