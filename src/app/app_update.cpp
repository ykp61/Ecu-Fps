// Acilista zorunlu guncelleme akisi (spec 2026-11-11-zorunlu-guncelleme-design.md §1/§4, ADR-028).
//  - Kontrol yalniz Ui rolunde, surec basina bir kez; gomulu yayin anahtari yoksa ag kullanilmaz (UnsignedBuild).
//  - Manifest imzasi gomulu anahtarla dogrulanir; indirilen EXE boyutu ve SHA-256'si imzali manifestle karsilastirilir,
//    diske yazildiktan sonra geri okunup yeniden dogrulanir. Degisim core::catalog::run_swap/recover_swap ile ayni
//    klasorde (exe.new -> exe, eski -> exe.old). Yeni surum 20 sn icinde saglik isareti vermezse eski surum geri gelir.
//  - Journal, ayarlar ve test verisi dokunulmaz; oyun surecine dokunulmaz.
//  - ADR-033 / spec 7: kurulu surumde (Program Files) UI yerel degisim YAPMAZ: imzali zarf ve indirilen baytlar
//    yardimci hizmete gider; hizmet her seyi yeniden dogrular, EXE'yi UAC'siz degistirir ve gozetir (saglik 60 sn,
//    yoksa geri alma). Yeni UI saglik sinyalini hizmete verir. Hizmet yok/bozuksa "Yardimci hizmeti onar" (tek UAC).
//    Tasinabilir kopyalarda bugunku yerinde degisim aynen surer.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "app/worker.h"
#include "app/app.h"
#include "app/update_support.h"
#include "core/broker/admin.h"
#include "core/product_info.h"
#include "core/recovery/sha256.h"
#include "core/setup/layout.h"
#include "core/update/release_key.h"
#include "core/update/update_check.h"
#include "platform/windows/catalog_signature.h"
#include "platform/windows/elevation.h"
#include "platform/windows/install/setup_worker_channel.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/legacy_health_signal.h"
#include "platform/windows/net/update_http.h"
#include "core/service/channel_choice.h"
#include "platform/windows/service_channel.h"
#include "platform/windows/service_update_client.h"
#include "platform/windows/win_env.h"
#include "ui/consent/fix_dialogs.h"

namespace ef::app {

using core::i18n::StringId;
using core::perf::Phase;
using core::update::UpdateOutcome;
using Step = ui::ViewModel::UpdateView::Step;

namespace {

constexpr wchar_t kHealthEventPrefix[] = L"Local\\EcuFps.UpdateHealthy.";
constexpr int kCleanupTries = 120, kCleanupSliceMs = 250;  // eski surec kapanana kadar .old silinmeye calisilir (<= 30 sn)
constexpr int kWaitSliceMs = 200;

std::wstring health_event_name(DWORD pid) { return kHealthEventPrefix + std::to_wstring(pid); }

core::catalog::SwapFs real_swap_fs() {
    core::catalog::SwapFs fs;
    // Ayni klasor = ayni birim: yeniden adlandirma atomiktir; calisan EXE'nin adi degistirilebilir (silinemez).
    fs.move = [](const std::string& from, const std::string& to) {
        return MoveFileExW(platform::to_wide(from).c_str(), platform::to_wide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    };
    fs.exists = [](const std::string& p) { return GetFileAttributesW(platform::to_wide(p).c_str()) != INVALID_FILE_ATTRIBUTES; };
    fs.remove = [](const std::string& p) {
        return DeleteFileW(platform::to_wide(p).c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
    };
    return fs;
}

core::catalog::SwapPaths swap_paths() { return core::update::update_swap_paths(platform::to_utf8(platform::self_exe_path().wstring())); }

std::wstring exe_dir() { return platform::self_exe_path().parent_path().wstring(); }

// Bu surec kurulum yerinden mi calisiyor (Program Files\Ecu Fps\EcuFps.exe). Salt okuma.
bool installed_self() {
    static const bool v = core::setup::is_installed_location(platform::normalized_self_exe(), core::setup::make_layout(platform::known_roots()));
    return v;
}

// Klasor yazilabilir mi: gecici dosya olusturulur ve kapanista silinir (yonetici izni istenmez).
bool dir_writable(const std::wstring& dir) {
    const std::wstring probe = dir + L"\\.ef-write-test-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool free_space_for(const std::wstring& dir, std::uint64_t bytes) {
    ULARGE_INTEGER avail{};
    if (!GetDiskFreeSpaceExW(dir.c_str(), &avail, nullptr, nullptr)) return false;
    return avail.QuadPart >= bytes + (8ull << 20);  // yeni dosya + pay
}

using upd::read_file;
using upd::write_file;

}  // namespace

void App::set_instance_hooks(std::function<void()> release, std::function<void()> reacquire) {
    release_instance_ = std::move(release);
    reacquire_instance_ = std::move(reacquire);
}

bool App::update_holds_writes() const { return !update_restore_scope_ && !core::update::writes_allowed(vm_.update.outcome); }

bool App::update_fix_records_pending() {
    const auto ft = fix_targets();  // salt okuma (kullanici + makine fixes journal'i)
    return !ft.active.empty() || !ft.recovery.empty() || !ft.interrupted.empty();
}

void App::push_update() {
    // Kart acikken sekmeler kapali: Oyun Hazirligi / oyun duzeltme kayitlari da karttaki "Onceki ayarlara don" ile geri
    // alinabilmeli (spec §1.3: geri alma hicbir zaman guncellemeye bagli degildir).
    const bool gate = vm_.update.outcome == UpdateOutcome::Mandatory || vm_.update.outcome == UpdateOutcome::NotWritable;
    vm_.update.rollback_available = (controller_ && controller_->switch_on()) || (gate && update_fix_records_pending());
    if (window_) window_->set_view(vm_);
}

void App::stop_update_threads() {
    update_cancel_ = true;
    platform::net::abort_update_requests();  // bekleyen ag cagrisi hemen doner (kapanis 15 sn beklemez)
    if (update_worker_.joinable()) update_worker_.join();
    // Degisim yapildi ama yeni surum hic baslatilmadi (launch_updated_exe calismadan kapanis): eski surum geri gelir.
    if (update_swap_pending_.exchange(false)) (void)core::catalog::recover_swap(real_swap_fs(), swap_paths(), false);
    if (update_wait_.joinable()) update_wait_.join();
    if (update_cleanup_.joinable()) update_cleanup_.join();
    if (service_check_.joinable()) service_check_.join();
}

void App::update_startup_recover() {
    if (args_.role != core::Role::Ui || !args_.updated_from.empty()) return;  // yeni surum kendi temizligini saglikta yapar
    if (installed_self()) return;  // kurulu surum: degisim kalintilarini yardimci hizmet yonetir (klasor yalniz yonetici yazar)
    const auto fs = real_swap_fs();
    const auto p = swap_paths();
    // Bu surec asil EXE adindan calisiyor. Yedek + saglik isareti: yeni surum saglikliydi, yalniz temizlik kaldi.
    // Yedek var ama isaret yok: bu ikili kendini henuz kanitlamadi; eski surum silinmez, onay pencere acildiktan sonra
    // (signal_update_healthy) yapilir. Pencere acilmadan cokerse yedek durur ve elle geri donus mumkun kalir.
    if (fs.exists(p.backup)) {
        if (fs.exists(p.marker)) (void)core::catalog::recover_swap(fs, p, true);
        else update_commit_after_window_ = true;
    } else if (fs.exists(p.marker)) {
        (void)fs.remove(p.marker);
    }
    if (fs.exists(p.staged)) (void)fs.remove(p.staged);  // yarim kalan indirme
    if (const auto failed = p.staged + ".failed"; fs.exists(failed)) (void)fs.remove(failed);
}

void App::start_update_check() {
    if (update_started_ || smoke_ || args_.role != core::Role::Ui) return;
    update_started_ = true;  // surec basina bir kez
    // Test/otomasyon anahtari: EF_UPDATE_CHECK=0 iken ag kullanilmaz (ctest ve final.ps1 ayarlar). Zorunlu kapiyi
    // zayiflatmaz: kontrol edilemeyen durum zaten 'acilir, uyari verir' (spec §1.3); burada yalniz uyari da gosterilmez.
    if (upd::update_check_disabled_by_env()) return;
    if (!core::update::has_release_key()) return;  // gomulu anahtar yok: dogrulanamaz, ag kullanilmaz (UpToDate kalir)
    vm_.update.outcome = UpdateOutcome::Checking;
    run_update_check(false);
}

// Ayarlar "Yazilim guncellemesi · Denetle". Acilis denetimiyle ayni dogrulanmis yol (imzali manifest, tekrar oynatma
// tabani). Fark: denetim surerken yazma kapisi kapanmaz (vm_.update.outcome degismez); sonuc Ayarlar satirina yazilir.
// Yeni surum bulunursa zorunlu guncelleme karti acilir (ADR-028: yeni surum zorunludur); hata mevcut durumu bozmaz.
void App::on_manual_update_check() {
    using UC = ui::SettingsView::UpdateCheck;
    if (smoke_ || args_.role != core::Role::Ui) return;
    if (vm_.settings.update_check == UC::Checking || vm_.update.outcome == UpdateOutcome::Checking) return;  // denetim suruyor
    if (vm_.update.step != Step::Idle || update_wait_.joinable()) return;  // guncelleme kuruluyor
    if (vm_.update.outcome == UpdateOutcome::Mandatory || vm_.update.outcome == UpdateOutcome::NotWritable) return;  // kart zaten acik
    if (upd::update_check_disabled_by_env() || !core::update::has_release_key()) {
        vm_.settings.update_check = UC::Unavailable;
        push_update();
        return;
    }
    // Onceki denetim isi sonucunu post ettikten sonra biter; outcome Checking degilse beklemeden birlesir.
    if (update_worker_.joinable()) update_worker_.join();
    vm_.settings.update_check = UC::Checking;
    push_update();
    run_update_check(true);
}

void App::run_update_check(bool manual) {
    const std::string version = vm_.version;
    const std::uint64_t highest_seen = prefs_.release_manifest_seen;  // kalici tekrar oynatma tabani (spec §2)
    update_worker_ = spawn_worker([this, version, highest_seen, manual] {
        // Kurulu surumde klasor kullaniciya yazilamaz ama degisimi hizmet yapar: zorunlu kart "Simdi guncelle" gosterir.
        // K27: hizmet standart hesaba hizmet vermez; standart hesapta 0.3 oncesi davranis (NotWritable: indirme sayfasi,
        // kurulum UAC ile yonetici kimligi ister). Aksi halde "Simdi guncelle" -> hizmet yok -> onar dongusu olurdu.
        // Kurulu surum yerel dosya yazmaz (on_update_now hizmet yolu): klasor yazilabilirligi (bozuk ACL) karari degistirmez.
        const bool installed = installed_self();
        const bool writable = installed ? platform::self_admin_member() : dir_writable(exe_dir());
        auto checked = upd::check_for_update(version, highest_seen, writable, update_cancel_);
        if (!checked) {
            if (manual && !update_cancel_.load())
                post([this] {
                    vm_.settings.update_check = ui::SettingsView::UpdateCheck::Failed;
                    push_update();
                });
            return;
        }
        auto d = std::move(checked->decision);
        std::string envelope = d.outcome == UpdateOutcome::Mandatory ? std::move(checked->envelope) : std::string();
        post([this, d = std::move(d), envelope = std::move(envelope), installed, manual]() mutable {
            // Yalniz dogrulanmis manifestten sonra taban yukselir; geri yazilmaz (kaydedilemezse bellekte surer).
            if (const auto next = core::update::next_highest_seen(prefs_.release_manifest_seen, d); next != prefs_.release_manifest_seen) {
                prefs_.release_manifest_seen = next;
                (void)save_prefs();
            }
            if (manual) {
                using UC = ui::SettingsView::UpdateCheck;
                const bool found = d.outcome == UpdateOutcome::Mandatory || d.outcome == UpdateOutcome::NotWritable;
                vm_.settings.update_check = found ? UC::Idle : d.outcome == UpdateOutcome::UpToDate ? UC::UpToDate : UC::Failed;
                // Hata (CheckFailed / VerifyFailed) yalniz Ayarlar satirinda; mevcut durum ve yazma kapisi degismez.
                if (!found && d.outcome != UpdateOutcome::UpToDate) {
                    push_update();
                    return;
                }
            }
            vm_.update.outcome = d.outcome;
            vm_.update.installed = installed;
            if (d.outcome == UpdateOutcome::Mandatory || d.outcome == UpdateOutcome::NotWritable) {
                vm_.update.new_version = d.manifest.version;
                update_manifest_ = std::move(d.manifest);
                update_envelope_ = std::move(envelope);
            }
            push_update();
        });
    });
}

void App::on_update_now() {
    if (vm_.update.outcome != UpdateOutcome::Mandatory || vm_.update.step != Step::Idle || !update_manifest_) return;
    if (update_wait_.joinable()) return;  // onceki deneme hala saglik bekliyor
    if (update_worker_.joinable()) update_worker_.join();  // kontrol isi bitti (sonucu post edildi)
    const bool via_service = installed_self();
    if (via_service && update_envelope_.empty()) return;  // hizmet zarfsiz hicbir sey kabul etmez
    vm_.update.step = Step::Downloading;
    vm_.update.notice_key.clear();
    vm_.update.service_unavailable = false;
    vm_.update.bytes = 0;
    vm_.update.total = update_manifest_->size;
    vm_.update.progress01 = 0;
    push_update();
    const auto m = *update_manifest_;
    update_worker_ = spawn_worker([this, m, via_service, envelope = update_envelope_] {
        auto finish = [this](std::string notice, UpdateOutcome outcome, bool service_unavailable = false) {
            post([this, notice = std::move(notice), outcome, service_unavailable] {
                vm_.update.step = Step::Idle;
                vm_.update.notice_key = notice;
                vm_.update.outcome = outcome;
                vm_.update.service_unavailable = service_unavailable;
                push_update();
            });
        };
        const auto dir = exe_dir();
        if (!via_service && !free_space_for(dir, m.size)) return finish({}, UpdateOutcome::NotWritable);
        auto got = platform::net::update_get(m.url, core::update::kExeMaxBytes, 0, update_cancel_, [this](std::uint64_t done, std::uint64_t total) {
            post([this, done, total] {
                vm_.update.bytes = done;
                if (total) vm_.update.total = total;
                vm_.update.progress01 = vm_.update.total ? static_cast<double>(done) / static_cast<double>(vm_.update.total) : 0.0;
                if (window_) window_->set_view(vm_);
            });
        });
        if (update_cancel_.load() || got.fail == platform::net::FetchFail::Cancelled) return;
        if (!got.ok()) return finish("update.download_failed", UpdateOutcome::Mandatory);
        // Imzali manifestle boyut + SHA-256; uyusmazsa hicbir dosya yazilmaz.
        if (!core::update::download_matches(got.body.size(), core::recovery::sha256_hex(got.body), m))
            return finish("update.verify_failed", UpdateOutcome::Mandatory);
        post([this] {
            vm_.update.step = Step::Installing;
            vm_.update.progress01 = 1;
            push_update();
        });
        if (via_service) {
            // Kurulu surum: yerel dosya yazilmaz. Hizmet zarfi ve baytlari yeniden dogrular, EXE'yi degistirir.
            const auto r = platform::service_update_send(envelope, got.body);
            got.body.clear();
            got.body.shrink_to_fit();
            if (update_cancel_.load()) return;
            if (r.result == platform::ServiceUpdateResult::Unavailable)
                return finish("update.service_unavailable", UpdateOutcome::Mandatory, true);  // zorunlu kalir; onarim sunulur
            if (r.result != platform::ServiceUpdateResult::Swapped) return finish("update.service_failed", UpdateOutcome::Mandatory);
            post([this] { launch_service_updated_exe(); });
            return;
        }
        const auto fs = real_swap_fs();
        const auto p = swap_paths();
        if (!write_file(p.staged, got.body)) return finish({}, UpdateOutcome::NotWritable);
        got.body.clear();
        got.body.shrink_to_fit();
        // Geri okuma: diske yazilan dosya da ayni boy ve ozette olmali.
        const auto back = read_file(p.staged, core::update::kExeMaxBytes);
        if (!back || !core::update::download_matches(back->size(), core::recovery::sha256_hex(*back), m)) {
            (void)fs.remove(p.staged);
            return finish("update.verify_failed", UpdateOutcome::Mandatory);
        }
        if (fs.exists(p.marker)) (void)fs.remove(p.marker);
        const auto step = core::catalog::run_swap(fs, p);
        if (step != core::catalog::SwapStep::Replaced) {
            (void)core::catalog::recover_swap(fs, p, false);
            (void)fs.remove(p.staged);
            return finish("update.rolled_back", UpdateOutcome::CheckFailed);
        }
        update_swap_pending_ = true;  // kapanis launch_updated_exe'den once olursa stop_update_threads geri alir
        if (update_cancel_.load()) return;
        post([this] { launch_updated_exe(); });
    });
}

void App::launch_updated_exe() {
    if (!update_swap_pending_.exchange(false)) return;  // kapanista geri alindi
    const auto p = swap_paths();
    auto rolled_back = [this] {
        if (reacquire_instance_) reacquire_instance_();
        vm_.update.step = Step::Idle;
        vm_.update.outcome = UpdateOutcome::CheckFailed;  // eski surum calismaya devam eder; yazma kapisi acilir
        vm_.update.notice_key = "update.rolled_back";
        push_update();
    };
    if (release_instance_) release_instance_();  // yeni surum tek ornek kilidini alabilsin
    const std::wstring exe = platform::to_wide(p.exe);
    std::wstring cmd = L"\"" + exe + L"\" " + core::build_launch_args(core::LaunchArgs{core::Role::Ui, std::nullopt, {}, false, vm_.version});
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const bool started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, exe_dir().c_str(), &si, &pi) != 0;
    HANDLE ev = started ? CreateEventW(nullptr, TRUE, FALSE, health_event_name(pi.dwProcessId).c_str()) : nullptr;
    if (!started || !ev) {
        if (started) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        (void)core::catalog::recover_swap(real_swap_fs(), p, false);
        rolled_back();
        return;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (update_wait_.joinable()) update_wait_.join();
    update_wait_ = spawn_worker([this, ev, proc = pi.hProcess, p] {
        bool healthy = false, cancelled = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(core::update::kHealthWaitMs);
        HANDLE hs[2] = {ev, proc};
        while (std::chrono::steady_clock::now() < deadline) {
            if (update_cancel_.load()) {
                cancelled = true;
                break;
            }
            const DWORD r = WaitForMultipleObjects(2, hs, FALSE, kWaitSliceMs);
            if (r == WAIT_OBJECT_0) {
                healthy = true;
                break;
            }
            if (r == WAIT_OBJECT_0 + 1 || r == WAIT_FAILED) break;  // yeni surum kapandi: saglikli degil
        }
        if (cancelled && WaitForSingleObject(ev, 0) == WAIT_OBJECT_0) healthy = true;  // isaret kapanisla ayni anda geldi
        // Eski surec saglik isaretini beklerken kapaniyorsa (tepsi Cik / pencere): saglik kanitlanmadi = zaman asimi.
        // Yeni surec durdurulur ve eski surum geri gelir; aksi halde sonraki acilis kanitlanmamis surumu onaylardi.
        if (!healthy) {
            if (WaitForSingleObject(proc, 0) == WAIT_TIMEOUT) {
                TerminateProcess(proc, 1);
                WaitForSingleObject(proc, 5000);
            }
            (void)core::catalog::recover_swap(real_swap_fs(), p, false);  // eski surum geri gelir
        }
        CloseHandle(ev);
        CloseHandle(proc);
        if (cancelled) return;  // kapanis suruyor: UI'ya yazilmaz
        if (healthy) {
            post([] { PostQuitMessage(0); });  // yeni surum calisiyor; eski surec ayar geri almadan kapanir
            return;
        }
        post([this] {
            if (reacquire_instance_) reacquire_instance_();
            vm_.update.step = Step::Idle;
            vm_.update.outcome = UpdateOutcome::CheckFailed;
            vm_.update.notice_key = "update.rolled_back";
            push_update();
        });
    });
}

// Kurulu surum: hizmet degisimi yapti ve gozetiyor. Tek ornek kilidi birakilir, yeni EXE --updated-from ile baslatilir ve
// bu surec ayar geri almadan kapanir. Yerel saglik bekleme / geri alma YOK: yeni UI saglik vermezse hizmet 60 sn sonra
// eski EXE'yi geri getirir.
void App::launch_service_updated_exe() {
    const auto layout = core::setup::make_layout(platform::known_roots());
    if (release_instance_) release_instance_();
    const std::wstring exe = layout.exe.wstring();
    std::wstring cmd = L"\"" + exe + L"\" " + core::build_launch_args(core::LaunchArgs{core::Role::Ui, std::nullopt, {}, false, vm_.version});
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, layout.dir.c_str(), &si, &pi)) {
        // Yeni surum baslatilamadi: hizmet saglik almayinca eski EXE'yi geri getirir. Bu surec calismaya devam eder.
        if (reacquire_instance_) reacquire_instance_();
        vm_.update.step = Step::Idle;
        vm_.update.outcome = UpdateOutcome::CheckFailed;
        vm_.update.notice_key = "update.rolled_back";
        push_update();
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostQuitMessage(0);
}

void App::on_update_repair_service() {
    if (vm_.update.outcome != UpdateOutcome::Mandatory || vm_.update.step != Step::Idle || !vm_.update.service_unavailable) return;
    if (update_worker_.joinable()) update_worker_.join();
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!ui::ask_fix(owner, cat(), t(StringId::UpdateRepairQuestion), t(StringId::UpdateRepairDetail))) return;  // Hayir: degisiklik yok
    vm_.update.step = Step::Repairing;
    vm_.update.notice_key.clear();
    push_update();
    update_worker_ = spawn_worker([this] {
        // Tek UAC: yukseltilmis ayni EXE kurulum isciliginde Repair (dosya + hizmet + ACL, journal + geri alma).
        const auto run = platform::install::run_setup_worker_request(core::setup::SetupKind::Repair, {});
        const bool ok = run.elevation == core::ElevationOutcome::Started && run.reply && run.reply->error.empty() && run.reply->result.ok;
        if (update_cancel_.load()) return;
        post([this, ok] {
            vm_.update.step = Step::Idle;
            if (!ok) {
                vm_.update.notice_key = "update.repair_failed";
                push_update();
                return;
            }
            vm_.update.service_unavailable = false;
            push_update();
            on_update_now();  // guncelleme zorunlu: onarimdan sonra hizmet uzerinden yeniden denenir
        });
    });
}

// Kurulu surum: hizmet var ama calismiyorsa (guncelleme sonrasi SCM yeniden baslatmadi, elle durduruldu, eski dar politika)
// hicbir ayar UAC'siz olmaz ve hizmetin kendi onarimi hic calismaz. Evet/Hayir -> tek UAC Repair: hizmet + hata eylemleri
// yazilir/geri okunur, hizmet baslatilir (baslamazsa Repair hata doner ve gorunur bildirim verilir). Yalniz yonetici hesabi probe edilir.
void App::check_helper_service_running() {
    if (smoke_ || !installed_self() || args_.role != core::Role::Ui || service_check_.joinable()) return;
    service_check_ = spawn_worker([this] {
        core::service::ServiceProbe probe;
        { auto c = platform::connect_helper_service(probe, static_cast<std::uint32_t>(GetCurrentProcessId())); }
        if (update_cancel_.load() || !probe.ui_installed || !probe.admin_member || !probe.exists || probe.running) return;
        post([this] {
            const HWND owner = window_ ? window_->hwnd() : host_;
            if (!ui::ask_fix(owner, cat(), t(StringId::UpdateRepairQuestion), t(StringId::UpdateServiceStoppedDetail))) return;
            vm_.update.step = Step::Repairing;
            vm_.update.notice_key.clear();
            push_update();
            if (update_worker_.joinable()) update_worker_.join();
            update_worker_ = spawn_worker([this] {
                const auto run = platform::install::run_setup_worker_request(core::setup::SetupKind::Repair, {});
                const bool ok = run.elevation == core::ElevationOutcome::Started && run.reply && run.reply->error.empty() && run.reply->result.ok;
                if (update_cancel_.load()) return;
                post([this, ok] {
                    vm_.update.step = Step::Idle;
                    vm_.update.notice_key = ok ? "" : "update.repair_failed";
                    push_update();
                });
            });
        });
    });
}

void App::signal_update_healthy() {
    if (smoke_) return;
    check_helper_service_running();
    // T3b (ADR-043 ek): onceki urunun UI'si bizi ONCEKI kurulum yolunda --updated-from ile baslatti. Saglik sinyali onceki
    // hizmete, onun pipe'ina gider (60 sn icinde gelmezse o hizmet eski EXE'yi geri getirir). Tasinabilir degisim yolu
    // (isaret + .old silme) burada CALISMAZ: o klasor onceki hizmetindir. Onceki hizmet yoksa hicbir sey yapilmaz.
    if (!args_.updated_from.empty() && platform::launched_from_legacy_install()) {
        vm_.update.notice_key = "update.updated";
        vm_.update.notice_version = vm_.version;
        push_update();
        if (update_cleanup_.joinable()) return;
        update_cleanup_ = spawn_worker([version = vm_.version] { (void)platform::send_legacy_health_signal(version); });
        return;
    }
    if (installed_self()) {
        // Kurulu surum: onay ve yedek temizligi hizmetindir. Yalniz guncellemeyle acildiysak saglik sinyali verilir.
        if (args_.updated_from.empty()) return;
        vm_.update.notice_key = "update.updated";
        vm_.update.notice_version = vm_.version;
        push_update();
        if (update_cleanup_.joinable()) return;
        update_cleanup_ = spawn_worker([version = vm_.version] { (void)platform::service_update_healthy(version); });
        return;
    }
    const auto fs = real_swap_fs();
    const auto p = swap_paths();
    if (args_.updated_from.empty()) {
        // Normal acilis, onaylanmamis degisim (yedek var, isaret yok): pencere acildi = saglik; once isaret, sonra onay.
        if (!update_commit_after_window_) return;
        update_commit_after_window_ = false;
        if (!write_file(p.marker, vm_.version)) return;  // isaret yazilamazsa yedek silinmez
        (void)core::catalog::recover_swap(fs, p, true);   // yedek silinemezse isaret kalir; sonraki acilis tamamlar
        return;
    }
    // Dosya isareti + adli olay (eski surec bekliyor). Isaret yeni surumun gercekten calistigini kaydeder.
    (void)write_file(p.marker, vm_.version);
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, health_event_name(GetCurrentProcessId()).c_str())) {
        SetEvent(ev);
        CloseHandle(ev);
    }
    vm_.update.notice_key = "update.updated";
    vm_.update.notice_version = vm_.version;
    push_update();
    // Eski surec kapaninca .old silinebilir (calisan EXE silinemez): sinirli sure tekrar denenir.
    update_cleanup_ = spawn_worker([this, fs, p] {
        // recover_swap(true) isareti yalniz yedek gercekten silinince kaldirir; kapanis yarida keserse isaret kalir.
        for (int i = 0; i < kCleanupTries && !update_cancel_.load(); ++i) {
            if (!fs.exists(p.backup)) {
                if (fs.exists(p.marker)) (void)fs.remove(p.marker);
                return;
            }
            if (core::catalog::recover_swap(fs, p, true) && !fs.exists(p.backup)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(kCleanupSliceMs));
        }
    });
}

void App::on_update_restore() {
    if (!controller_) return;
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    if (admin_busy_) {
        controller_->notify_busy();
        return;
    }
    auto perf_restorable = [this] {
        const auto ph = controller_->phase();
        return ph == Phase::On || ph == Phase::ConfirmRestore || ph == Phase::RecoveryNeeded;
    };
    if (!perf_restorable() && !update_fix_records_pending()) return;
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!ui::ask_fix(owner, cat(), t(StringId::UpdateRestoreQuestion), t(StringId::UpdateRestoreDetail))) return;  // Hayir: degisiklik yok
    // Geri alma guncellemeye bagli degildir; oyun kapisi ise ayni kalir (calisan oyunda ayar yazilmaz).
    ScopedFlag scope(update_restore_scope_, true);
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;
    if (admin_busy_) {
        controller_->notify_busy();
        return;
    }
    // Iletisim kutusu acikken durum degismis olabilir (otomatik geri yukleme, cikis geri yuklemesi): Kapali'dan toggle
    // Performans Acik akisini baslatirdi. Yalniz hala geri alinacak bir durum varsa ilerlenir.
    if (perf_restorable()) {
        if (controller_->phase() != Phase::ConfirmRestore) controller_->toggle();  // On / RecoveryNeeded -> geri yukleme
        if (controller_->phase() == Phase::ConfirmRestore) controller_->answer(true);
        push_update();
        return;  // Performans geri yuklemesi suruyor; kalan duzeltme kaydi varsa dugme yeniden sunulur
    }
    if (!update_fix_records_pending()) {
        push_update();
        return;
    }
    // Oyun Hazirligi / oyun duzeltmeleri: kullanici kapsami motorla, makine kapsami broker ile (tek UAC) geri alinir.
    if (!rec_ || !rec_->fixes_store || !rec_->fixes_engine) return;
    auto* store = rec_->fixes_store.get();
    auto* engine = rec_->fixes_engine.get();
    auto ops = admin_channel_.fix_recovery_ops();
    for (auto& op : admin_channel_.fix_active_ops()) ops.push_back(std::move(op));
    const bool started = run_admin_job([this, store, engine, ops = std::move(ops)]() -> std::function<void()> {
        core::broker::BatchResult b;
        b.elevation = core::ElevationOutcome::Started;  // kullanici kapsami: UAC yok
        if (!ops.empty()) b = admin_channel_.run(core::ipc::RequestAction::UndoFix, ops);
        const auto user = core::broker::restore_matching(store, engine, [](const contracts::SettingOperation&) { return true; }, "update.restore");
        if (user.state != core::broker::StepState::NothingToDo || b.steps.empty()) b.steps.push_back(user);
        const std::string key(core::broker::admin_notice_key(core::ipc::RequestAction::UndoFix, b));
        return [this, key] {
            set_readiness_notice(key, core::broker::admin_notice_warning(key), {});
            push_update();
        };
    });
    if (!started) controller_->notify_busy();
    push_update();
}

void App::on_update_quit() { request_exit_restore(); }

void App::on_update_open_page() {
    const auto url = platform::to_wide(core::update::kReleasePageUrl);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace ef::app
