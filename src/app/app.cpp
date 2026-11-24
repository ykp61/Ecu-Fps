#include "app/worker.h"
#include "app/app.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <malloc.h>
#include <map>
#include <optional>
#include <psapi.h>
#include <shellapi.h>
#include <thread>
#include <tlhelp32.h>
#include <ctime>

#include "core/capabilities/device_verification.h"
#include "core/diagnosis/calibration.h"  // DIAG-11: uretim yayin kapisi durumu
#include "core/diagnosis/graphics_api.h"  // F-54: yuklu modullerden API siniflandirmasi
#include "core/diagnosis/calibration_export.h"  // DIAG-10: diagcal_v1 ornek uretimi
#include "core/diagnosis/components.h"          // DIAG-10: MEAS-03 ornekleme aralik kapisi
#include "core/games/config_risk.h"
#include "core/games/exe_resolver.h"
#include "core/games/process_risk.h"
#include "core/games/cs2.h"
#include "core/readiness/readiness.h"  // F-79: arka plan yigini siniflandirmasi
#include "core/insights/history_compare.h"
#include "core/insights/peer.h"
#include "core/support/bundle.h"
#include "core/measure/bench_text.h"
#include "app/endurance_report_map.h"
#include "benchmark/endurance/endurance_runner.h"
#include "benchmark/balance/balance_runner.h"  // spec 2026-11-12 A5: denge testi kosucusu
#include "benchmark/balance/balance_checks.h"  // spec 2026-11-12 C1/C2: salt okuma uyum denetimleri
#include "core/diagnosis/mismatch.h"
#include "measurement/telemetry/adapter_gpu.h"  // A3: arka plan GPU karistiricisi
#include "platform/windows/metrics/balance_check_sampler.h"
#include "platform/windows/readiness/display_reader.h"  // C3: yuksek performansli bagdastirici (LUID)
#include "core/diagnosis/balance.h"
#include "core/diagnosis/game_limit.h"  // spec 2026-11-12 B: Oyunda Test GPU mesgul orani + kare siniri suphesi
#include "core/inventory/power_security.h"
#include "platform/windows/adapters/win_power.h"
#include "app/model_loader.h"
#include "core/adapters/windows/dns.h"
#include "core/diagnosis/platform.h"
#include "core/measure/run_marker.h"
#include "core/metrics/sensor.h"
#include "core/diagnostics/incident_engine.h"
#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/capture_channel.h"
#include "platform/windows/net/speedtest_http.h"
#include "app/readiness_collect.h"
#include "app/setup_app.h"
#include "core/instance_policy.h"
#include "core/perf/exit_policy.h"  // Cik: geri alma yok, yalniz kapat
#include "platform/windows/admin_exit_gate.h"
#include "app/update_support.h"
#include "ui/consent/fix_dialogs.h"
#include "core/brand/tray_variant.h"
#include "core/metrics/sampling.h"
#include "core/cleanup/admin_transfer.h"
#include "platform/windows/cleanup/win_cleanup.h"
#include "platform/windows/crash_handler.h"
#include "platform/windows/data_remover.h"
#include "core/setup/layout.h"
#include "platform/windows/install/program_remover.h"  // ADR-033: kurulu programin kaldirilmasi (tek UAC)
#include "platform/windows/known_roots.h"
#include "core/service/uac_copy.h"
#include "platform/windows/save_dialog.h"
#include "platform/windows/vendor_install/vendor_install_win.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/games/game_icons.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/games/protection_services.h"
#include "platform/windows/games/steam_config.h"
#include "platform/windows/games/upscaler_scan.h"
#include "platform/windows/games/process_snapshot.h"
#include "core/perf/power_plan.h"
#include "core/perf/tweak_plan.h"
#include "core/perf/boot_plan.h"
#include "core/perf/update_hold_plan.h"
#include "core/perf/vfx_plan.h"
#include "core/perf/gaming_plan.h"
#include "core/perf/gpu_route_plan.h"
#include "core/perf/feature_plan.h"
#include "core/perf/defender_plan.h"
#include "core/perf/debloat_plan.h"
#include "platform/windows/adapters/win_appx.h"
#include "platform/windows/readiness/feature_usage_reader.h"
#include "core/perf/admin_plan.h"
#include "platform/windows/restore_point_win.h"
#include "core/perf/admin_sequencing.h"
#include "platform/windows/metrics/net_rate_sampler.h"
#include "platform/windows/metrics/pdh_sampler.h"
#include "platform/windows/inventory/gpu_storage.h"  // F-63: adaptor ayrik VRAM toplami
#include "measurement/telemetry/process_gpu.h"       // F-63: oyun PID ayrik VRAM sayaci
#include "measurement/frame/capture_result.h"        // B3: yakalama protokolu surum kimligi
#include "core/product_info.h"
#include "core/recovery/service.h"
#include "platform/windows/elevation.h"
#include "platform/windows/inventory/cpu_sets.h"
#include "platform/windows/readiness/background_reader.h"
#include "platform/windows/readiness/storage_reader.h"  // F-81: oyun diski turu (salt okuma)
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/inventory/windows_features.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/legacy_migration.h"
#include "platform/windows/nvidia/nvapi_drs.h"  // F-61: salt okuma GPU limit/saat telemetrisi
#include "platform/windows/run_key_store.h"
#include "platform/windows/inventory/diag_collect.h"  // Uyumluluk B8: cihaz dogrulama notu
#include "platform/windows/wua/wua_search.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"
#include "ui/consent/consent_dialog.h"
#include "ui/brand/brand.h"
#include "ui/theme/system_palette.h"
#include "ui/theme/theme.h"

#include "app/app_internal.h"

namespace ef::app {

using core::i18n::StringId;
using core::perf::Phase;


using namespace detail;

namespace {
// T18: cihaz sinifi gercek Windows okumasindan (salt okuma). Okunamayan bilgi bos kalir ve hicbir bayrak kurmaz.
core::perf::DeviceClass read_device_class() {
    core::perf::DeviceFacts f;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) f.ram_bytes = ms.ullTotalPhys;
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    if (si.dwNumberOfProcessors > 0) f.logical_cores = si.dwNumberOfProcessors;  // yalniz bu islemci grubu; baska grup varsa alt sinir
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps) && ps.BatteryFlag != 255) f.laptop = (ps.BatteryFlag & 128) == 0;  // 128: pil yok
    return core::perf::classify(f);
}
}  // namespace

std::wstring App::t(StringId id) const { return platform::to_wide(cat().get(id)); }

// Onay penceresi acilmadan hemen once SCM yeniden okunur (salt okuma, baglanmaz): sayfa gosteriminden beri hizmet
// durmus olabilir; "UAC sorulmaz" bayat bilgiyle gosterilmez.
StringId App::ac(StringId id) {
    refresh_admin_path();
    return core::service::admin_copy(id, vm_.admin_without_uac);
}

void App::refresh_admin_path() {
    // Yalniz gercek Ui: smoke / test arka plani UAC metnini korur. Hizmet hazir gorunse de son deneme PID/Hello yuzunden
    // UAC broker'ina dustuyse UAC metni kalir (bir sonraki basarili hizmet isteginde duzelir).
    const bool v = !smoke_ && args_.role == core::Role::Ui && platform::helper_service_ready() && !admin_channel_.service_fell_back();
    if (v == vm_.admin_without_uac) return;
    vm_.admin_without_uac = v;
    if (window_) window_->set_view(vm_);
}

std::int64_t App::mono_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

App::App(HINSTANCE inst, const core::LaunchArgs& args, int show_cmd, core::i18n::Catalog cat)
    : inst_(inst),
      args_(args),
      show_cmd_(show_cmd),
      smoke_(args.role == core::Role::Smoke),
      detection_(mono_ms, core::games::DetectionCoordinator::Hooks{[this] { perf_auto_restore(); },
                                                                  [this] { check_pending_exit(); },
                                                                  [this] { run_pending_recovery(); }}) {
    auto& slot = catalogs_[static_cast<std::size_t>(cat.locale())];
    slot.emplace(std::move(cat));
    cat_ptr_.store(&*slot, std::memory_order_release);
    init_measure();  // P9/B5: olcum denetleyicisi (durum + is parcaciklari)
}

const core::i18n::Catalog* App::catalog_for(core::i18n::Locale l) {
    auto& slot = catalogs_[static_cast<std::size_t>(l)];
    if (!slot) {
        auto c = core::i18n::Catalog::embedded(l);
        if (!c.ok()) return nullptr;  // gomulu tablo bozuk (derleme/test yakalar): eski dil surer
        slot.emplace(std::move(c).value());
    }
    return &*slot;
}

bool App::switch_catalog_if_needed() {
    const auto want = core::i18n::locale_switch(prefs_.ui_language, windows_ui_language(), cat().locale());
    if (!want) return false;
    const auto* next = catalog_for(*want);
    if (!next) return false;
    cat_ptr_.store(next, std::memory_order_release);
    // Tepsi ipucu hemen yeni dil; tepsi menusu her acilista t() ile kurulur.
    told_still_running_ = false;  // "tepside calisiyor" bildirimi yeni dilde bir kez daha gosterilebilir
    refresh_tray();
    return true;
}

bool App::apply_ui_language() {
    if (!window_) return switch_catalog_if_needed();
    const auto want = core::i18n::locale_switch(prefs_.ui_language, windows_ui_language(), cat().locale());
    if (!want || !catalog_for(*want)) return false;  // ayni dil ya da yuklenemedi: pencereye dokunulmaz
    // Ana pencere eski katalogla kurulmus metin/yerlesim tutar: ayni yer ve boyutta yeniden kurulur. Yikicida
    // on_closed cagrilmaz (GWLP_USERDATA once sifirlanir), surec tepside kalir; bekleyen kip pencereleri Hayir ile kapanir.
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (window_->hwnd() && GetWindowPlacement(window_->hwnd(), &wp)) reopen_placement_ = wp;
    window_.reset();
    const bool switched = switch_catalog_if_needed();  // katalog yukari da yuklendi: burada basarisiz olmaz
    open_window(true);
    if (window_) {
        window_->show_page(ui::Page::Settings);
        push_settings();  // ayarlar sayfasindaki notlar/metinler yalniz yeni katalogdan (onbellekte eski dil metni yok)
    }
    return switched;
}

App::~App() {
    stop_update_threads();  // zorunlu guncelleme: indirme/saglik bekleme/temizlik (sonraki okumadan once durur)
    // Denetim 4.3: once TUM iptal sinyalleri, sonra beklemeler. Her is parcacigi kendi iptalini ayni anda gorur; kapanis
    // suresi en yavas isin suresi kadar olur, toplamlari kadar degil. Yazma isleri (Performans, broker, paket, kaldirma)
    // yine yarida birakilmaz: iptal sonraki adimdan once durur, journal tutarli kalir.
    {
        std::lock_guard lk(live_mu_);
        live_stop_ = true;
    }
    live_cv_.notify_all();
    if (cleanup_) {
        cleanup_->cancel_scan();  // arka plan taramasi kapanisi beklemesin (dosya silinmez)
        cleanup_->cancel();       // silinen geri gelmez; sonraki dosyadan once durur
    }
    check_cancel_ = true;
    {
        std::lock_guard lk(watch_mu_);
        watch_stop_ = true;
    }
    watch_cv_.notify_all();
    dns_cancel_ = true;
    health_cancel_ = true;                            // T16: saglik testi kapanista durur (yuk is parcaciklari birakilir)
    readiness_cancel_ = true;                         // ag testi suruyorsa kalan paketler gonderilmez
    if (driver_scanner_) driver_scanner_->cancel();  // spec H: suren Windows Update aramasi birakilir (salt okuma)
    measure_->request_speed_cancel();                 // Task 12: hiz testi kapanista durur
    local_cancel_ = true;                             // ADR-032: paket kopyasi durur (yarim kopya silinir)
    measure_->request_bench_cancel();                 // Uygulama Testi: kapanista iptal + temizlik
    measure_->request_endurance_cancel();             // R13: uzun test iptal edilir; ayar degisikligi yok
    if (controller_) controller_->cancel();

    // Saha raporu 2026-11-14 (ikinci PC: tepsiden Cik kapatmiyordu): kullanici ciktigini hemen gorur. Pencere ve tepsi
    // ikonu beklemelerden ONCE kalkar (onceden tum join'ler bitene kadar ekranda "yanit vermiyor" kaliyordu).

    platform::append_exit_log("exit: begin");
    if (window_ && window_->hwnd()) ShowWindow(window_->hwnd(), SW_HIDE);
    tray_.remove();
    // B2 / bosluk 4: gizli ana pencere hemen yok edilir. Ikinci baslatma artik olu pencereye mesaj birakip kapanmaz;
    // pencereyi bulamaz ve bu surecin tek ornek kilidini birakmasini bekler. Is parcaciklarinin post'u basarisiz olur
    // (kopya silinir); mesaj dongusu zaten bitti.
    if (host_) {
        DestroyWindow(host_);
        host_destroyed_ = true;
    }
    const auto running = [](std::thread& th) {
        return th.joinable() && WaitForSingleObject(static_cast<HANDLE>(th.native_handle()), 0) == WAIT_TIMEOUT;
    };
    // Sistem Kontrolu tarama asamasi salt okumadir (bosluk 6): yazma isi sayilmaz, asagida sinirli beklenir.
    const bool check_scanning = check_ && check_->phase() == core::perf::CheckPhase::Scanning;
    bool instance_released = false;
    const auto release_instance = [&](const char* when) {
        if (instance_released) return;
        instance_released = true;
        if (release_instance_) release_instance_();
        platform::append_exit_log(std::string("exit: instance lock released (") + when + ")");
    };
    {
        bool any_write = false;
        for (auto* th : {&cleanup_worker_, &cleanup_admin_worker_, &startup_worker_, &local_worker_, &measure_->bench_thread(), &admin_worker_,
                         &worker_, &bundle_worker_, &uninstall_worker_})
            any_write = any_write || running(*th);
        if (!check_scanning) any_write = any_write || running(check_worker_);
        // Yazma isi yoksa kilit hemen birakilir; varsa o is bitince (yeni ornegin acilis kurtarmasi yarim yazmayla yarismaz).
        if (core::release_instance_early(any_write)) release_instance("hidden");
    }
    // Yazma isleri (journal'li Performans/duzeltme, temizlik silme, baslangic degisikligi, paket kopyasi, destek paketi,
    // kaldirma) sonuna kadar beklenir: hicbir yerel yazma adimin ortasinda birakilmaz.
    if (cleanup_worker_.joinable()) cleanup_worker_.join();
    if (cleanup_admin_worker_.joinable()) cleanup_admin_worker_.join();
    close_admin_cleanup();
    if (startup_worker_.joinable()) startup_worker_.join();
    if (!check_scanning && check_worker_.joinable()) check_worker_.join();  // uygulama asamasi: journal'li yazma
    if (local_worker_.joinable()) local_worker_.join();
    if (measure_->bench_thread().joinable()) measure_->bench_thread().join();  // journal'li degisiklik yarida birakilmaz
    if (worker_.joinable()) worker_.join();              // calisan uygulama/geri alma isi: journal tutarli kalir
    // R21/R20: yarim kalan paket/kaldirma isi birakilmaz.
    if (bundle_worker_.joinable()) bundle_worker_.join();
    if (uninstall_worker_.joinable()) uninstall_worker_.join();
    // B2 / bosluk 5: yonetici isi sinirli beklenir. Broker/hizmet cagrisinda takili kaldiysa (UAC istemi, uzun bakim
    // yaniti) birakilir: istek broker'da tamamlanir ve journal'a yazilir, sonraki acilista uzlastirilir. Yerel kullanici
    // kapsamli yazma suruyorsa (broker cagrisi yok) sonuna kadar beklenir.
    // B2 inceleme: kapi (surec genelinde) ancak tam beklenen yazma isleri bittikten SONRA birakilir. Onceden en basta
    // birakiliyordu; Performans geri alma / Sistem Kontrolu uygulama / kaldirma isinin yonetici adimi "abandoned" ile
    // basarisiz oluyor ve yonetici kapsamli ayarlar uygulanmis kaliyordu. Artik yalniz admin_worker_ sinirli beklenir.
    platform::admin_exit_gate().abandon();
    bool admin_left = false;
    if (admin_worker_.joinable()) {
        const bool finished = WaitForSingleObject(static_cast<HANDLE>(admin_worker_.native_handle()), core::kAdminExitWaitMs) == WAIT_OBJECT_0;
        switch (core::admin_exit_decision(finished, platform::admin_exit_gate().active_calls())) {
            case core::AdminExitWait::Joined:
            case core::AdminExitWait::KeepWaiting: admin_worker_.join(); break;
            case core::AdminExitWait::LeaveToBroker:
                admin_left = true;
                platform::append_exit_log("exit: admin job still in broker/service call after " + std::to_string(core::kAdminExitWaitMs / 1000) +
                                          " s: left to broker (startup reconciliation)");
                break;
        }
    }
    if (!admin_left) release_instance("write workers joined");
    platform::append_exit_log("exit: write workers joined");
    driver_scanner_.reset();  // salt okuma; WUA aramasi en cok ~5 sn icinde birakilir (wua_search)
    // Salt okuma isleri (canli ornek, oyun izleyici, hazirlik taramasi / ag testi, hiz testi, uzun test, bulgu tazeleme,
    // model yukleyici, Sistem Kontrolu taramasi): toplam 3 sn. Bu surede bitmeyen is ayar degistirmez; yazma isleri yukarida
    // bitti, bu yuzden surec dogrudan sonlandirilir (beklemek yerine; ornek: ikinci PC'de bir okuma cagrisi donmuyordu).
    const auto deadline = GetTickCount64() + 3000;
    std::string stuck;
    std::vector<std::thread*> read_only{&live_thread_, &watch_thread_, &readiness_worker_, &measure_->speed_thread(), &measure_->endurance_thread(), &refresher_, &loader_};
    if (check_scanning) read_only.push_back(&check_worker_);
    for (auto* th : read_only) {
        if (!th->joinable()) continue;
        const auto now = GetTickCount64();
        const DWORD left = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        if (WaitForSingleObject(static_cast<HANDLE>(th->native_handle()), left) == WAIT_OBJECT_0) {
            th->join();
        } else {
            stuck += (stuck.empty() ? "" : ",") + std::to_string(GetThreadId(static_cast<HANDLE>(th->native_handle())));
        }
    }
    if (admin_left) {
        // Karar kilit altinda: is parcacigi hala broker cagrisindaysa surec sonlanir (cagridan cikip yerel yazmaya
        // gecemez; tek ornek kilidi isletim sistemince birakilir). Cagridan cikmissa yerel isini bitirmesi beklenir.
        platform::admin_exit_gate().run_if_in_call([] {
            platform::append_exit_log("exit: terminating with admin job left to broker");
            TerminateProcess(GetCurrentProcess(), 0);
        });
        admin_worker_.join();
        release_instance("admin job joined");
    }
    if (!stuck.empty()) {
        platform::append_exit_log("exit: read-only workers still running after 3 s (thread ids " + stuck + "): terminating");
        TerminateProcess(GetCurrentProcess(), 0);
    }
    platform::append_exit_log("exit: all workers joined");
    measure_->stop_live_view();  // A4: zamanlayici + bekleyen onizleme karesi
    window_.reset();
    tray_.remove();
    if (tray_icon_) DestroyIcon(tray_icon_);
    if (host_ && !host_destroyed_) DestroyWindow(host_);
}

void App::start_findings_refresh() {
    if (refresher_busy_ && !refresher_done_.load()) {  // suren tazeleme bitince bir kez daha (UI is parcacigi beklemez)
        refresher_rerun_ = true;
        return;
    }
    // Ya bos ya da govdesi bitmis (sonuc post'u kaybolmus olabilir: PostMessage kuyrugu dolu): join aninda doner.
    if (refresher_.joinable()) refresher_.join();
    refresher_busy_ = true;
    refresher_done_ = false;
    // DIAG-11: is parcacigina deger kopyasi (veri yarisi yok)
    refresher_ = spawn_worker([this, rt = bench_runtime_, pcie = bench_pcie_] {
        ui::ViewModel tmp;
        load_findings(tmp, rt, pcie);
        post([this, f = std::move(tmp.findings), c = std::move(tmp.components), k = std::move(tmp.diagnosis_gate_key)]() mutable {
            ui::adopt_component_report(vm_, std::move(f), std::move(c), std::move(k));
            if (window_) window_->set_view(vm_);
            refresher_busy_ = false;
            if (refresher_rerun_) {
                refresher_rerun_ = false;
                start_findings_refresh();
            }
        });
        refresher_done_ = true;
    });
}

void App::post(std::function<void()> fn) { ui::MainWindow::post_run(host_, std::move(fn)); }

LRESULT CALLBACK App::host_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        static_cast<App*>(cs->lpCreateParams)->host_ = h;  // DefWindowProc gecerli HWND ile cagrilmali
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    return self ? self->host(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::host(UINT m, WPARAM w, LPARAM l) {
    if (m == platform::activate_message_id()) {  // ikinci baslatma: pencereyi goster
        // B2: ikinci ornek SendMessageTimeout ile bekler; pencere kurulumu (D2D) uzun surebilir, cevap hemen doner.
        post([this] { open_window(true); });
        return static_cast<LRESULT>(core::kActivateReply);  // ikinci ornek yalniz bu yaniti aktivasyon sayar
    }
    if (m == tray_.taskbar_created_msg()) {  // Explorer yeniden basladi
        tray_.readd();
        refresh_tray();
        return 0;
    }
    switch (m) {
        case ui::MainWindow::kMsgRun: {
            std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(l));
            if (fn && *fn) (*fn)();
            return 0;
        }
        case ui::Tray::kCallbackMsg:
            switch (tray_.handle(w, l, tray_menu())) {
                case ui::Tray::CmdShow: open_window(true); break;
                case ui::Tray::CmdToggle:
                    // Acma/kapatma onayi pencerede gosterilir (Evet/Hayir); tepsi dogrudan uygulamaz.
                    open_window(true);
                    if (controller_) perf_toggle();
                    break;
                case ui::Tray::CmdExitRestore: request_exit_restore(); break;
                default: break;
            }
            return 0;
        case WM_TIMER:
            if (w == MeasureController::kTimerLiveView) {
                measure_->live_view_push();
                return 0;
            }
            break;
        case kMsgLiveFrame:
            measure_->on_live_frame_posted();
            return 0;
        case WM_SETTINGCHANGE:     // gorev cubugu acik/koyu (ImmersiveColorSet) veya yuksek kontrast
        case WM_SYSCOLORCHANGE:
        case WM_DISPLAYCHANGE:     // DPI/ekran degisimi: ikon boyutu
        case WM_THEMECHANGED:
            update_tray_icon();
            return 0;
        case WM_CLOSE:
            // Ana gizli pencereye kapatma (ornek: kaldirici, oturum yoneticisi): cik ve geri al.
            request_exit_restore();
            return 0;
        case kMsgWindowClosed:
            window_.reset();  // arayuz kaynaklari birakilir; surec tepside surer
            // Tepside bos CRT yigin bloklari isletim sistemine iade edilir ve surecin KENDI calisma kumesi kirpilir
            // (yalniz bu surec; sistem ayari degildir). Oyun ikonlari izleme onbelleginde kalir: pencere yeniden
            // acildiginda dosya yeniden okunmadan gosterilir.
            _heapmin();
            SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
            if (smoke_ || !tray_.added()) {
                request_exit_restore();
            } else if (!told_still_running_) {
                told_still_running_ = true;
                tray_.notify(t(StringId::AppTitle), t(StringId::TrayStillRunning));
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(host_, m, w, l);
}

void App::close_window_deferred() { PostMessageW(host_, kMsgWindowClosed, 0, 0); }

void App::wire_window() {
    auto* w = window_.get();
    w->on_toggle = [this] { perf_toggle(); };
    w->on_answer = [this](bool yes) {
        // Evet = ayar mutasyonu (uygula/geri yukle). Oyun calisirken baslatilmaz; taslak birakilir.
        // Geri yukleme onayi (Kapali) guncelleme beklemesine bagli degildir; uygulama onayi bekler.
        ScopedFlag scope(update_restore_scope_, controller_ && controller_->phase() == Phase::ConfirmRestore);
        if (yes && !verify_or_block(core::games::GuardedAction::SettingMutation)) return;
        perf_answer(yes);
    };
    w->on_closed = [this] { close_window_deferred(); };
    w->on_detection_retry = [this] { request_detection_rescan(); };
    w->on_perf_keep = [this] {
        if (!controller_ || admin_busy_ || !controller_->external_conflict()) return;
        const HWND owner = window_ ? window_->hwnd() : host_;
        if (!ui::ask_fix(owner, cat(), t(StringId::PerfKeepQuestion), t(StringId::PerfKeepDetail), ui::FixDialogIcon::Warning)) return;
        if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;
        controller_->keep_external();
    };
    w->on_perf_drift_reapply = [this] {
        // Yalniz Evet/Hayir onayini acar; yazma Evet'ten sonra (on_answer: oyun kapisi + arka uc WriteGuard) olur.
        if (!controller_ || admin_busy_) return;
        controller_->reapply_drift();
    };
    w->on_perf_drift_keep = [this] {
        if (!controller_ || admin_busy_) return;
        controller_->keep_drift();  // ayar yazilmaz; kullanicinin mevcut degeri korunur
    };
    w->on_group_select = [this](const std::string& g, bool on) { controller_->set_selected(g, on); };
    w->on_page_shown = [this](ui::Page pg) {
        refresh_admin_path();  // UAC metinleri: hizmet durumu degismis olabilir (salt okuma SCM sorgusu)
        // Temizlik onizlemesi yalniz sayfa ilk acildiginda (arka planda klasor taramasi yok).
        // Spec 2026-11-12 B4: Sistem araclari tek sayfa — Temizlik ve Baslangic bolumleri birlikte gorunur; ikisi de
        // sayfa ilk acildiginda (salt okuma) taranir.
        if (ui::systools_page(pg) && cleanup_ && cleanup_->phase() == core::cleanup::SessionPhase::Idle) cleanup_->scan();
        if (ui::systools_page(pg) && startup_ && (startup_->phase() == core::startup::StartupPhase::Idle || startup_->phase() == core::startup::StartupPhase::Failed)) startup_->scan();
        if (pg == ui::Page::History) push_history();
        // Spec H: arama yalniz "Denetle"ye basinca ya da sayfa ilk acildiginda; oyun acikken baslamaz (kapi acilinca baslar).
        updates_auto_pending_ = pg == ui::Page::Updates && vm_.updates.scan.state == core::drivers::ScanState::NeverChecked;
        if (updates_auto_pending_) start_driver_scan();
        if (pg == ui::Page::Readiness && vm_.readiness.loading) start_readiness_scan();  // her acilista guncel journal (sinirli: kota 256 kayit/kapsam)
    };
    w->on_cleanup = [this](ui::MainWindow::CleanAction a) {
        using A = ui::MainWindow::CleanAction;
        switch (a) {
            case A::Scan:
                if (cleanup_admin_pending_) return;  // UAC beklerken tarama baslatilmaz
                close_admin_cleanup();
                cleanup_admin_notice_.clear();
                cleanup_->scan();
                break;
            case A::Run:
                if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
                if (cleanup_admin_pending_) return;
                if (!gate_or_block(core::games::GuardedAction::DiskCleanup)) break;
                if (admin_busy_ && cleanup_->selected(core::cleanup::CleanupCategory::StandbyMemory)) break;  // baska yonetici isi suruyor: ikinci UAC yok
                cleanup_->request_run();
                if (cleanup_->phase() == core::cleanup::SessionPhase::Confirming) {
                    // Spec 2026-11-12 B4 / E: onay uygulama ici kip penceresinde (yikici; ilk odak Hayir). Gosterilemezse Hayir.
                    std::size_t n = 0;
                    for (const auto& r : vm_.cleanup.rows) n += r.selected ? 1 : 0;
                    const HWND owner = window_ ? window_->hwnd() : host_;
                    const bool yes = ui::ask_cleanup(owner, cat(), n, ui::format_bytes(vm_.cleanup.selected_bytes, cat().locale() == core::i18n::Locale::TrTR),
                                                     vm_.cleanup.volume);
                    // Soru acikken durum degismis olabilir (kaldirma basladi / UAC bekliyor): Evet ayni kapilardan gecer.
                    if (yes && uninstall_phase_ != ui::SettingsView::UninstallPhase::Running && !cleanup_admin_pending_ &&
                        verify_or_block(core::games::GuardedAction::DiskCleanup) && claim_standby_job())
                        cleanup_->answer(true);
                    else
                        cleanup_->answer(false);
                }
                break;
            case A::Yes:
                if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
                if (cleanup_admin_pending_) return;
                if (verify_or_block(core::games::GuardedAction::DiskCleanup) && claim_standby_job()) cleanup_->answer(true);
                break;
            case A::No: cleanup_->answer(false); break;
            case A::Stop: cleanup_->cancel(); break;
            case A::ScanCancel: cleanup_->cancel_scan(); break;  // EXT-CLN: tarama durur; dosya silinmez
            case A::StorageSettings:
                // EXT-CLN: yalniz izinli ms-settings URI'sini acar; temizlik sonucuna sayilmaz.
                platform::inventory::open_guided(core::guided::Action::StorageSettings);
                break;
        }
    };
    w->on_cleanup_select = [this](core::cleanup::CleanupCategory c, bool on) { on_cleanup_select(c, on); };
    w->on_cleanup_volume = [this](const std::wstring& v) {
        if (cleanup_admin_pending_) return;
        if (v == cleanup_->volume()) return;  // ayni birim: yonetici yardimcisi ve onizleme gecerli kalir
        close_admin_cleanup();  // birim degisince eski yardimci plani gecersiz
        cleanup_admin_notice_.clear();
        cleanup_->set_volume(v);
    };
    w->on_cleanup_files = [this](core::cleanup::CleanupCategory c) { cleanup_->toggle_files(c); };
    w->on_startup = [this](ui::MainWindow::StartupAction a) {
        using A = ui::MainWindow::StartupAction;
        switch (a) {
            case A::Apply:
                if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
                if (!gate_or_block(core::games::GuardedAction::SettingMutation)) break;
                startup_->request_apply();
                if (startup_->phase() == core::startup::StartupPhase::Confirming) {
                    // Spec 2026-11-12 E: onay uygulama ici kip penceresinde (ilk odak Hayir). Gosterilemezse Hayir.
                    const HWND owner = window_ ? window_->hwnd() : host_;
                    const bool yes = ui::ask_startup(owner, cat());
                    if (yes && uninstall_phase_ != ui::SettingsView::UninstallPhase::Running &&
                        verify_or_block(core::games::GuardedAction::SettingMutation))
                        startup_->answer(true);
                    else
                        startup_->answer(false);
                }
                break;
            case A::Yes:
                if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
                if (verify_or_block(core::games::GuardedAction::SettingMutation)) startup_->answer(true);
                break;
            case A::No: startup_->answer(false); break;
            case A::Rescan: startup_->scan(); break;
        }
    };
    w->on_startup_select = [this](std::size_t i, bool on) { startup_->set_desired(i, on); };
    // WIN-10: hizmet secimi yalniz tercihtir; sistem degisikligi Performans Acik onayiyla (Evet) uygulanir.
    w->on_startup_service_select = [this](std::size_t i, bool selected) {
        if (i >= vm_.startup.services.size()) return;
        const auto& row = vm_.startup.services[i];
        if (!row.editable) return;
        {
            std::lock_guard<std::mutex> lk(service_sel_mu_);
            auto it = std::find(session_stop_services_.begin(), session_stop_services_.end(), row.name);
            if (selected && it == session_stop_services_.end()) {
                if (session_stop_services_.size() >= 8) return;  // sinir: prefs sozlesmesiyle ayni
                session_stop_services_.push_back(row.name);
            } else if (!selected && it != session_stop_services_.end()) {
                session_stop_services_.erase(it);
            }
            prefs_.session_stop_services = session_stop_services_;
        }
        save_prefs();
        push_startup();
        if (controller_) controller_->refresh_preview();  // onizleme sayaci yeni secimi gosterir
    };
    w->on_check = [this](ui::MainWindow::CheckAction a) {
        using A = ui::MainWindow::CheckAction;
        if (a == A::Start) check_->start();  // tarama salt okuma
        else if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20: plan uygulanmaz
        else if (a == A::No || verify_or_block(core::games::GuardedAction::SettingMutation)) check_->answer(a == A::Yes, false);
    };
    w->on_check_select = [this](std::size_t i, bool on) {
        check_->set_selected(i, on);
        if (i < check_selected_.size()) check_selected_[i] = on;
        push_check();
    };
    w->on_test_start = [this](core::measure::TestSource src) {
        if (vm_.measure.endurance.running) return;  // R13: uzun test surerken ikinci test baslamaz
        // Spec 2026-11-12 A5: Denge testi Uygulama Testi'nin kapsamidir (kaynak yine Builtin).
        if (src == core::measure::TestSource::Builtin) {
            if (vm_.measure.scope == core::measure::BuiltinScope::Balance) measure_->start_balance_test();
            else measure_->start_builtin_test();
        } else if (src == core::measure::TestSource::InGame) {
            measure_->start_ingame_test();
        }
    };
    w->on_test_scope = [this](core::measure::BuiltinScope scope) {
        if (core::measure::busy(vm_.measure) || vm_.measure.endurance.running) return;  // test islerken degismez
        vm_.measure.scope = scope;
        if (window_) window_->set_measure(vm_.measure);
    };
    // R13/INS-03: uzun test yalniz kullanici istegiyle; otomatik/tekrar yok.
    w->on_proof_start = [this] { measure_->start_proof_experiment(); };  // P1: tek Evet/Hayir pencere icinde sorulur
    w->on_proof_stop = [this] { measure_->stop_proof_experiment(); };
    w->on_balance_report = [this] { save_balance_report(); };  // P3: Darbogaz "Raporu kaydet" (destek paketi yolu, Masaustu)
    w->on_endurance_start = [this] { measure_->start_endurance_test(); };
    w->on_endurance_stop = [this] { measure_->stop_endurance_test(); };
    w->on_speed_start = [this] { measure_->start_speed_test(); };
    w->on_speed_server_ping = [this] { measure_->start_server_ping(); };
    w->on_speed_stop = [this] { measure_->stop_speed_test(); };
    // Spec 2026-11-12 H: Windows Update surucu aramasi (salt okuma, yonetici yok; onay gerekmez) ve resmi uretici sayfasi.
    w->on_updates_check = [this] { start_driver_scan(); };
    w->on_updates_cancel = [this] {
        if (driver_scanner_) driver_scanner_->cancel();
    };
    w->on_updates_link = [](const std::string& url) {
        if (url.empty() || !core::readiness::action_uri_allowed(url)) return;  // izin listesi disi: hicbir sey acilmaz
        const auto wu = platform::to_wide(url);
        ShellExecuteW(nullptr, L"open", wu.c_str(), nullptr, nullptr, SW_SHOWNORMAL);  // kullanici tiklar; tarayicida acilir
    };
    w->on_updates_install = [this](std::size_t k) { run_driver_install(k); };  // spec H2
    w->on_updates_local = [this](std::size_t k) { run_vendor_package(k); };    // ADR-032
    // Spec 2026-11-12 A4: "Canli test ekrani" kalici tercih (varsayilan acik). Kosu sirasinda degismez: onizleme
    // acik / kapali durumu kosunun kosuludur.
    w->on_live_view = [this](bool on) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        if (core::measure::busy(vm_.measure) || vm_.measure.endurance.running) return;
        vm_.measure.live_view.user_on = on;
        prefs_.live_test_view = on;
        save_prefs();
        if (window_) window_->set_measure(vm_.measure);
    };
    w->on_test_mode = [this](core::measure::TestMode m) {
        if (core::measure::busy(vm_.measure)) return;  // M05/M07: test islerken (Durduruluyor dahil) degismez
        vm_.measure.mode = m;
        measure_->proof_refresh_estimate();  // P1: dugme metnindeki sure secili kosu suresinden
        if (window_) window_->set_measure(vm_.measure);
    };
    w->on_storage_select = [this](std::size_t k) {
        if (core::measure::busy(vm_.measure)) return;
        if (k <= vm_.measure.storage_roots.size()) vm_.measure.storage_choice = k, vm_.measure.storage_blocked = false;
        if (window_) window_->set_measure(vm_.measure);
    };
    w->on_test_stop = [this] {
        if (measure_->proof_running()) measure_->stop_proof_experiment();  // P1: deney durdurulur; Acik ise geri yuklenir
        if (vm_.measure.phase == core::measure::TestPhase::Running || vm_.measure.phase == core::measure::TestPhase::Preparing) {
            measure_->request_bench_cancel();
            vm_.measure.phase = core::measure::TestPhase::Stopping;
            if (window_) window_->set_measure(vm_.measure);
        }
    };
    w->on_welcome_dismiss = [this] {
        prefs_.welcome_seen = true;
        save_prefs();
        vm_.show_welcome = false;
        if (window_) window_->set_view(vm_);
    };
    w->on_readiness = [this](ui::MainWindow::ReadinessAction a) {
        if (a == ui::MainWindow::ReadinessAction::Rescan) start_readiness_scan();
        else if (a == ui::MainWindow::ReadinessAction::FixAll) run_fix_all();  // P12-B1: Panel "Duzelt"
        else start_network_test();
    };
    w->on_readiness_fix = [this](std::size_t k) { on_readiness_fix(k); };
    w->on_readiness_undo = [this](std::size_t k) {
        ScopedFlag scope(update_restore_scope_, true);  // geri alma guncelleme beklemesine bagli degildir
        on_readiness_undo(k);
    };
    w->on_update_now = [this] { on_update_now(); };
    w->on_update_quit = [this] { on_update_quit(); };
    w->on_update_restore = [this] { on_update_restore(); };
    w->on_update_open_page = [this] { on_update_open_page(); };
    w->on_update_repair_service = [this] { on_update_repair_service(); };
    w->on_readiness_keep = [this](std::size_t k) { on_readiness_keep(k); };
    w->on_game_guide = [this](std::size_t k) {
        if (k >= vm_.games.size() || vm_.games[k].pro_slug.empty()) return;
        const std::string uri = "https://prosettings.net/games/" + vm_.games[k].pro_slug + "/";
        if (!core::readiness::action_uri_allowed(uri)) return;
        const auto w2 = platform::to_wide(uri);
        ShellExecuteW(nullptr, L"open", w2.c_str(), nullptr, nullptr, SW_SHOWNORMAL);  // kullanici tiklar; tarayicida acilir
    };
    w->on_game_fullscreen = [this](std::size_t k) { on_game_fullscreen(k); };
    w->on_game_nvidia = [this](std::size_t k) { on_game_nvidia(k); };
    w->on_games_add_exe = [this] { add_manual_game(); };
    w->on_game_manual_remove = [this](std::size_t k) { remove_manual_game(k); };
    w->on_autostart = [this](bool on) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
        set_autostart(on);  // ADR-030: gorunur HKCU Run; yaz/sil + geri oku, hata nedeni gorunur
    };
    w->on_install_offer = [this] {
        // Spec 8: Kur -> ayni EXE --setup ile (yukseltilmemis, sabit arguman) baslar; Ui normal cikis yolundan cikar
        // (Performans geri yuklemesi + tek ornek kilidi birakilir). Baslatilamazsa Ui calismaya devam eder.
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
        if (launch_self_setup()) request_exit_restore();
    };
    w->on_app_animations = [this](bool on) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        prefs_.app_animations = on;  // UI-11/R33: kalici tercih; Windows tercihiyle birlikte uygulanir
        save_prefs();
        push_settings();
    };
    w->on_strip_enabled = [this](bool on) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        prefs_.strip_enabled = on;  // RGB isik seridi: app_animations ile ayni kalici tercih dosyasi
        save_prefs();
        push_settings();
    };
    w->on_cleanup_hint = [this](bool on) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        prefs_.cleanup_hint_after_game = on;  // R-C2: yalniz bildirim tercihi; temizlik hicbir zaman kendiliginden calismaz
        save_prefs();
        push_settings();
    };
    w->on_bundle_dumps = [this](bool on) {
        if (bundle_busy_ || uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        bundle_include_dumps_ = on;  // yalniz oturum; varsayilan kapali (bellek icerigi uyarisi UI'da)
        push_settings();
    };
    w->on_bundle_create = [this] { create_support_bundle(); };
    w->on_update_check = [this] { on_manual_update_check(); };
    w->on_language = [this] { on_language_choice(); };
    w->on_uninstall = [this](ui::MainWindow::UninstallAction a) {
        using A = ui::MainWindow::UninstallAction;
        using UP = ui::SettingsView::UninstallPhase;
        if (uninstall_phase_ == UP::Running) return;
        if (a == A::Begin) {
            if (uninstall_phase_ == UP::Confirming) return;
            uninstall_note_.clear();
            if (vm_.game_running) uninstall_note_ = core::i18n::key_of(StringId::SettingsUninstallGameRunning);
            else if (admin_busy_ || (controller_ && controller_->busy())) uninstall_note_ = core::i18n::key_of(StringId::SettingsUninstallBusy);
            uninstall_phase_ = UP::Idle;
            push_settings();
            if (!uninstall_note_.empty()) return;
            // Spec B7 / E: onay uygulama ici kip penceresinde (yikici; ilk odak Hayir). Pencere yoksa sorulmaz, baslamaz.
            if (!window_ || !ui::ask_uninstall(window_->hwnd(), cat())) return;
            start_uninstall();  // kapi ve mesguliyet burada yeniden denetlenir (soru acikken oyun baslamis olabilir)
        } else if (a == A::Yes) {
            if (uninstall_phase_ != UP::Confirming) return;
            start_uninstall();
        } else {
            if (uninstall_phase_ != UP::Confirming) return;
            uninstall_phase_ = UP::Idle;
            push_settings();
        }
    };
    w->on_consent_set = [this](const std::string& id, bool allow) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        for (const auto& e : consent_.list())
            if (e.id == id) consent_.set(id, e.display_name, allow ? core::games::Consent::Allow : core::games::Consent::Deny, now_unix_ms());
        save_consent();
        push_settings();
    };
    w->on_consent_reset = [this](const std::string& id) {
        if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
        consent_.clear(id);  // bir sonraki algilamada yeniden sorulur
        save_consent();
        push_settings();
    };
    // R16: salt okuma secimi/karsilastirma; disa aktarma yalniz kullanici dugmeyle ve yerel dosyaya.
    w->on_history_select = [this](std::size_t index) { select_history(index); };
    w->on_history_export = [this] { export_history(); };
}

void App::open_window(bool activate) {
    if (!window_) {
        // Uyumluluk #1: tepsideyken "Windows diline uy" ile Windows dili degistiyse pencere yeni dille acilir.
        (void)switch_catalog_if_needed();
        ui::MainWindow::Options o;
        o.window_class = platform::kMainWindowClass;
        o.title = t(StringId::AppTitle);
        o.smoke = smoke_;
        o.quit_on_destroy = false;  // yasam dongusu App'te
        window_ = ui::MainWindow::create(inst_, theme_, cat(), o);
        if (!window_) return;
        wire_window();
        // Spec G: ilk acilis bildirimi bir kez, ilk acilan pencerede (--tray ile baslandiysa pencere acilinca).
        if (prefs_.autostart_notice_pending && !smoke_) {
            prefs_.autostart_notice_pending = false;
            (void)save_prefs();
            autostart_notice_ = autostart_state_.on;
            push_settings();
        }
        window_->set_view(vm_);
        // UI-09: ornekleme pencerenin gorunurlugune bagli (simge durumu/gizli/kapali -> durur).
        window_->activity().subscribe([this](ui::Visibility v) {
            set_live_visible(v == ui::Visibility::Visible);
            // Spec 2026-11-12 E: tepside / simge durumundayken sorulamayan yonetici kurtarma sorusu pencere gorununce sorulur.
            if (v == ui::Visibility::Visible && !deferred_machine_offer_.empty())
                post([this] {
                    if (!deferred_machine_offer_.empty()) offer_machine_recovery(std::exchange(deferred_machine_offer_, {}));
                });
        });
        start_live();
        // Dil degisiminde yeniden kurulan pencere onceki yer ve boyutta acilir (simge durumu ise normal).
        auto placement = std::exchange(reopen_placement_, std::nullopt);
        if (ui::test_background_mode()) {
            // Test modu: pencere etkinlestirilmez ve z-sirasinin en altina konur (kullanicinin oyunundan odak calmaz).
            ShowWindow(window_->hwnd(), SW_SHOWNOACTIVATE);
            SetWindowPos(window_->hwnd(), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            UpdateWindow(window_->hwnd());
            return;
        }
        if (placement && !smoke_) {
            if (placement->showCmd != SW_SHOWMAXIMIZED) placement->showCmd = SW_SHOWNORMAL;
            SetWindowPlacement(window_->hwnd(), &*placement);
        } else {
            ShowWindow(window_->hwnd(), smoke_ ? SW_SHOWNOACTIVATE : (activate ? SW_SHOW : show_cmd_));
        }
        UpdateWindow(window_->hwnd());
    } else {
        if (ui::test_background_mode()) return;
        ShowWindow(window_->hwnd(), IsIconic(window_->hwnd()) ? SW_RESTORE : SW_SHOW);
    }
    if (activate) SetForegroundWindow(window_->hwnd());
}

ui::TrayMenu App::tray_menu() const {
    ui::TrayMenu m;
    const bool on = controller_ && controller_->switch_on();
    m.toggle_text = t(on ? StringId::TrayToggleOff : StringId::TrayToggleOn);
    m.toggle_enabled = controller_ && !controller_->busy();
    m.show_text = t(StringId::TrayShow);
    m.exit_text = t(StringId::TrayExitRestore);
    return m;
}

void App::refresh_tray() { update_tray_icon(); }

void App::update_tray_icon() {
    namespace cb = core::brand;
    const bool hc = ui::high_contrast_on();
    // Yuksek kontrast gorev cubugu zemini: sistem pencere rengi; on plan sistem metin rengi.
    const COLORREF bg = GetSysColor(COLOR_WINDOW);
    const bool hc_dark = (GetRValue(bg) + GetGValue(bg) + GetBValue(bg)) < 3 * 128;
    const auto choice = cb::choose_tray(state_known_, controller_ ? controller_->phase() : Phase::Off,
                                        ui::taskbar_uses_light_theme() ? cb::TaskbarTheme::Light : cb::TaskbarTheme::Dark, hc, true,
                                        hc_dark);
    // Durum metni renkten bagimsiz: ipucu her zaman gercek durumu soyler.
    if (const auto id = core::i18n::id_of(choice.tip_key)) tray_.set_tip(t(*id));
    const int px = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
    const int variant = static_cast<int>(choice.variant);
    if (variant == tray_variant_ && px == tray_px_ && tray_icon_) return;  // degisiklik yok: yeniden yukleme yok
    HICON icon = nullptr;
    switch (choice.variant) {
        case cb::TrayVariant::DarkActive: icon = ui::brand::load_tray_icon(inst_, IDI_TRAY_DARK_ACTIVE, px); break;
        case cb::TrayVariant::DarkNeutral: icon = ui::brand::load_tray_icon(inst_, IDI_TRAY_DARK_NEUTRAL, px); break;
        case cb::TrayVariant::LightActive: icon = ui::brand::load_tray_icon(inst_, IDI_TRAY_LIGHT_ACTIVE, px); break;
        case cb::TrayVariant::LightNeutral: icon = ui::brand::load_tray_icon(inst_, IDI_TRAY_LIGHT_NEUTRAL, px); break;
        case cb::TrayVariant::HcWhite: icon = ui::brand::load_tray_icon(inst_, IDI_TRAY_HC_WHITE, px); break;
        case cb::TrayVariant::HcBlack: icon = ui::brand::load_tray_icon(inst_, IDI_TRAY_HC_BLACK, px); break;
        case cb::TrayVariant::HcSystem: {
            const COLORREF fg = GetSysColor(COLOR_WINDOWTEXT);
            icon = ui::brand::render_mark_icon(px, {GetRValue(fg) / 255.f, GetGValue(fg) / 255.f, GetBValue(fg) / 255.f, 1.f});
            if (!icon) icon = ui::brand::load_tray_icon(inst_, hc_dark ? IDI_TRAY_HC_WHITE : IDI_TRAY_HC_BLACK, px);
            break;
        }
    }
    if (!icon) return;  // kaynak yoksa mevcut ikon kalir
    tray_.set_icon(icon);
    if (tray_icon_) DestroyIcon(tray_icon_);
    tray_icon_ = icon;
    tray_variant_ = variant;
    tray_px_ = px;
}

void App::request_exit_restore() {
    // R20: kaldirma surerken ikinci bir geri yukleme/cikis baslatilmaz; is bitince sonuc uygulanir.
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    platform::append_exit_log(std::string("exit requested: phase=") + std::string(core::perf::to_string(controller_ ? controller_->phase() : Phase::Off)) +
                              " pending=" + (pending_exit_ ? "1" : "0") + " admin_busy=" + (admin_busy_ ? "1" : "0"));
    if (pending_exit_) {
        // Saha raporu 2026-11-14: cikis bekliyorken (oyun acik / is suruyor / geri alma ertelendi) ikinci "Cik" sessizce
        // yok sayiliyordu. Simdi kullaniciya sorulur: Evet = Performans ayarlari geri alinmadan cikilir (kayitlar korunur;
        // sonraki acilista Performans Acik gorunur, Kapali eski degerleri geri yukler). Hayir = beklemeye devam.
        open_window(true);
        const HWND owner = window_ ? window_->hwnd() : host_;
        if (ui::ask_fix(owner, cat(), t(StringId::ExitForceQuestion), t(StringId::ExitForceDetail), ui::FixDialogIcon::Warning)) {
            platform::append_exit_log("exit forced by user while waiting");
            PostQuitMessage(0);
        }
        return;
    }
    pending_exit_ = true;
    // P1 deneyi suruyorsa: iptal edilir; deneyin kendi geri yukleme akisi Performans'i Kapali'ya dondurur, proof_finish cikisi surdurur.
    if (measure_->proof_running()) {
        platform::append_exit_log("exit: proof experiment cancelled, restoring Performance before quit");
        measure_->stop_proof_experiment();
    }
    check_pending_exit();
}

void App::perf_toggle() {
    if (!controller_) return;
    if (measure_->proof_running()) return;  // P1: deney suren Performans anahtari kilitli (gecisleri deney yurutur)
    refresh_admin_path();  // onay kartindaki "UAC istenecek" / "UAC sorulmaz" gercek yola uysun
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20: journal'a es zamanli yazma yok
    if (admin_busy_) {  // Oyun Hazirligi yonetici islemi (UAC/broker) suruyor: ikinci broker/UAC baslatilmaz
        controller_->notify_busy();
        return;
    }
    controller_->toggle();
}

void App::perf_answer(bool yes) {
    if (!controller_) return;
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (yes && admin_busy_) {  // onay asamasi korunur; is bitince yeniden Evet denebilir
        controller_->notify_busy();
        return;
    }
    controller_->answer(yes);
}

void App::perf_auto_restore() {
    if (!controller_) return;
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    switch (core::perf::auto_restore_when(!gate(core::games::GuardedAction::SettingMutation).allowed, admin_busy_)) {
        case core::perf::DeferredRun::AfterGameExit:
            // Oyun calisiyor veya durum dogrulanamiyor: ayar degistirilmez; is kaybolmaz, sonraki taze "oyun yok" sonucuna kalir.
            perf_auto_restore_deferred_ = false;
            detection_.owe_restore();
            return;
        case core::perf::DeferredRun::AfterAdminJob:  // yonetici islemi bitince (finish_admin_job) yeniden denenir
            perf_auto_restore_deferred_ = true;
            return;
        default: break;
    }
    if (mono_ms() < auto_restore_retry_at_ms_) {  // arka uc az once reddetti: her 3 sn'de yeni deneme/UAC yok
        detection_.owe_restore();
        return;
    }
    controller_->auto_restore();
}

void App::check_pending_exit() {
    if (!pending_exit_ || !controller_) return;
    if (admin_busy_) return;  // yonetici islemi bitince finish_admin_job yeniden cagirir; yazma yarida kesilmez
    // Owner karari 2026-11-17: Cik Performans ayarlarini geri ALMAZ; yalniz kapatir. Kayit korunur, sonraki acilista
    // durum gercek hedeflerden uzlastirilir. Geri alma yalniz acik "Kapali" (Evet/Hayir) ile.
    using core::perf::ExitAction;
    switch (core::perf::exit_action(controller_->phase(), measure_->proof_running())) {
        case ExitAction::Quit:
            platform::append_exit_log("exit: settings left as applied (no restore)");
            PostQuitMessage(0);
            return;
        case ExitAction::DeclinePlan: controller_->answer(false); return;  // onaysiz plan: hicbir sey degismez
        case ExitAction::Wait: return;  // is suruyor: bitince on_change yeniden kontrol eder
    }
}

void App::load_consent() {
    if (const auto dir = platform::app_data_dir()) {
        platform::WinJournalFs fs(*dir);
        if (auto txt = fs.read(kConsentFile); txt.ok()) {
            if (auto st = core::games::ConsentStore::parse(txt.value()); st.ok()) consent_ = std::move(st).value();
            // Bozuk dosya silinmez; bos depoyla devam edilir ve ilk kayitta yeniden yazilir.
        }
    }
}

void App::save_consent() {
    // R20: basarili kaldirmadan sonra silinen veri yeniden olusturulmaz; silme surerken de yazilmaz.
    if (uninstall_exiting_ || uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    if (const auto dir = platform::app_data_dir()) {
        std::error_code ec;
        std::filesystem::create_directories(*dir, ec);
        platform::WinJournalFs fs(*dir);
        (void)fs.write_atomic(kConsentFile, consent_.serialize());  // yazilamazsa bellekte surer
    }
    refresh_watch_consent();
}

void App::set_live_visible(bool v) {
    {
        std::lock_guard lk(live_mu_);
        live_visible_ = v;
    }
    live_cv_.notify_all();
}

void App::start_live() {
    if (live_thread_.joinable()) return;
    live_thread_ = spawn_worker([this] {
        platform::metrics::NetRateSampler net_rate;  // Task 9: GetIfEntry2 sayac farki, paket gondermez
        platform::metrics::PdhSampler sampler;  // acilis ~0,3 sn CPU: gorunur olunca acilir, gorunmez olunca kapanir
        // EF-X24: NVIDIA surucu sicakligi; takilirsa canli kart beklemez (sure asimi + devre kesici).
        // Takilan cagri ayrik is parcaciginda surebilir: NVAPI nesnesi paylasimli tutulur, canli is parcacigi bitse de yasar.
        auto nv_holder = std::make_shared<std::shared_ptr<platform::nvidia::Nvapi>>();
        bool nv_tried = false;
        core::metrics::TimedSensor gpu_temp([nv_holder]() {
            core::metrics::SensorValue v;
            v.source = "nvapi";
            const auto nv = *nv_holder;
            if (!nv) {
                v.value = contracts::Measurement::missing(contracts::Unit::Celsius, contracts::MissingReason::NotSupported);
                return v;
            }
            const auto tel = nv->telemetry();
            v.device = tel.name;
            v.captured_ms = static_cast<std::int64_t>(GetTickCount64());
            v.value = tel.temperature_c ? contracts::Measurement::of(static_cast<double>(*tel.temperature_c), contracts::Unit::Celsius)
                                        : contracts::Measurement::missing(contracts::Unit::Celsius, contracts::MissingReason::NotSupported);
            return v;
        });
        core::metrics::SamplingGate gate(1000);
        // Kisa simge durumu / geri yukleme dongusu PDH'yi yeniden acma bedelini (~0,3 sn CPU) odemesin: sorgu ancak
        // bu sure boyunca gorunmez kalinca kapatilir.
        constexpr std::int64_t kCloseAfterHiddenMs = 10000;
        std::int64_t hidden_since = -1;
        bool ever_opened = false;
        std::unique_lock lk(live_mu_);
        while (!live_stop_) {
            gate.set_visible(live_visible_);
            const auto now = static_cast<std::int64_t>(GetTickCount64());
            if (!gate.visible()) {
                if (sampler.is_open()) {
                    if (hidden_since < 0) hidden_since = now;
                    if (const auto left = hidden_since + kCloseAfterHiddenMs - now; left > 0) {
                        live_cv_.wait_for(lk, std::chrono::milliseconds(left), [this] { return live_stop_ || live_visible_; });
                        continue;
                    }
                    // Gorunmezken PDH sorgusu ve tamponlari tutulmaz; ilk gorunur ornekte yeniden acilir (asagida
                    // "if (!sampler.is_open()) sampler.open()"). Kapatma kilit disinda: UI is parcacigi beklemez.
                    // sampler bu is parcacigina aittir; kilit yalniz live_visible_ / live_stop_ icindir.
                    lk.unlock();
                    sampler.close();
                    lk.lock();
                    continue;  // kilit birakilmisken gorunurluk degismis olabilir: bastan degerlendir
                }
                live_cv_.wait(lk, [this] { return live_stop_ || live_visible_; });  // gorunmezken hic uyanma yok
                continue;
            }
            hidden_since = -1;
            if (gate.should_sample(now)) {
                lk.unlock();
                // Yeniden acilista ilk toplama yalniz hazirlik: PDH oran sayaclari (CPU vb.) iki toplama ister, ilk
                // ornek NotSampled olur. Bu ornek kutucuklara / yumusaticilara / rapora (report_) gonderilmez;
                // gecerli ilk ornek bir aralik (1 sn) sonra gelir, o ana kadar son gecerli deger ekranda kalir.
                const bool priming = !sampler.is_open() && ever_opened;
                if (!sampler.is_open()) {
                    sampler.open();
                    ever_opened = true;
                }
                auto snap = sampler.sample();
                if (priming) {
                    net_rate.sample(snap, snap.mono_ms);  // ag sayac tabanini da tazele (uzun gorunmezlik araligi yok)
                    lk.lock();
                    continue;
                }
                net_rate.sample(snap, snap.mono_ms);
                if (!nv_tried) {
                    nv_tried = true;
                    if (auto r = platform::nvidia::Nvapi::open(); r.ok()) *nv_holder = std::move(r).value();
                }
                if (*nv_holder) {
                    const auto tnow = static_cast<std::int64_t>(GetTickCount64());
                    const auto tv = gpu_temp.read(tnow);
                    snap.gpu_temp_c = core::metrics::fresh_value(tv, static_cast<std::int64_t>(GetTickCount64()));
                    snap.gpu_temp_source = tv.source;
                    snap.gpu_temp_device = tv.device;
                }
                post([this, snap]() mutable {
                    // OPT-08: rapor ham (yumusatilmamis) ornekle hesaplanir.
                    const auto st0 = report_.stage();
                    report_.push(core::perf::sample_from_live(snap));
                    if (st0 == core::perf::ReportCollector::Stage::CollectingAfter) push_before_after();
                    // Yalniz gorunum yumusatmasi (eksik deger yumusatilmaz, sifirlanir).
                    snap.cpu_pct = sm_cpu_.push(snap.cpu_pct);
                    snap.gpu_pct = sm_gpu_.push(snap.gpu_pct);
                    snap.disk_pct = sm_disk_.push(snap.disk_pct);
                    vm_.live = snap;
                    if (window_) window_->set_live(snap);
                });
                lk.lock();
                continue;
            }
            live_cv_.wait_for(lk, std::chrono::milliseconds(gate.wait_ms(now)));
        }
    });
}

// P2: gercek surec goruntusunden (salt okuma) dusuk oncelige alinabilir arka plan programlari. Plan kisa sure (20 sn)
// onbellekte kalir; sureler dolunca yeniden taranir. Onay ile uygulama arasi plan degisirse mevcut parmak izi
// kontrolu eski onayi gecersiz kilar. Hicbir sey yazmaz; yazma yalniz Evet'ten sonra journal'li motorda.
std::vector<core::perf::PlanItem> App::background_plan_items() {
    constexpr std::int64_t kTtlMs = 20'000;
    std::lock_guard<std::mutex> lk(bg_mu_);
    const std::int64_t now = mono_ms();
    if (bg_scan_ms_ == 0 || now - bg_scan_ms_ >= kTtlMs) {
        // Oyun acik ya da koruma kapisi belirsizse lansor/sohbet programlari muhafazakar elenir.
        const bool game_or_unsure = !permit_.verify(core::games::GuardedAction::SettingMutation).allowed;
        const auto snapshot = bg_scanner_.scan(game_or_unsure);
        core::perf::BgOptions opt;
        opt.include_browsers = false;  // v1: tarayici yok (sahip karari bekliyor)
        bg_items_ = core::perf::background_plan_items(core::perf::select_background_candidates(snapshot, opt));
        bg_scan_ms_ = mono_ms();
    }
    // Her cagrida salt okuma ile elenir: zaten dusuk oncelikte/EcoQoS'ta olan, artik yasamayan ya da kimligi tutmayan
    // surec planda yer almaz (yoksa tek adim hatasi butun islemi geri alirdi). Yazma yok.
    std::vector<core::perf::PlanItem> out;
    for (const auto& i : bg_items_)
        if (adapters_.bg_process.capture(i.op).ok()) out.push_back(i);
    return out;
}

core::games::Consent App::on_game_detected(const std::string& exe_path, const std::wstring& display_name) {
    const auto id = core::games::normalize_game_id(exe_path);
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return consent_.get(id);  // R20: silme sirasinda sorma/kaydetme
    if (!consent_.needs_prompt(id)) return consent_.get(id);
    const HWND owner = window_ ? window_->hwnd() : host_;
    const auto answer = ui::ask_game_consent(owner, cat(), display_name);
    if (!answer) return core::games::Consent::Ask;  // cevapsiz: kaydedilmez
    const auto c = *answer ? core::games::Consent::Allow : core::games::Consent::Deny;
    consent_.set(id, platform::to_utf8(display_name), c, now_unix_ms());
    save_consent();
    push_settings();
    return c;
}

int App::run() {
    // R21: COM STA (IFileSaveDialog). Basarisizsa paket kaydi denenmez; uygulama normal surer.
    ComStaInit com;
    // R21: yerel cokme kaydi. En fazla 3 kucuk dump; otomatik gonderim YOK.
    if (const auto dir = platform::app_data_dir()) platform::install_crash_handler({*dir / L"crash", 3});
    platform::guard_current_thread();  // denetim 4.1: UI is parcacigindaki terminate de dump yazar
    // Kalici gizli ana pencere (tepsi sahibi; tek ornek aktivasyonu buraya gelir).
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = host_proc;
    wc.hInstance = inst_;
    wc.lpszClassName = platform::kHostWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 3;
    host_ = CreateWindowExW(WS_EX_TOOLWINDOW, platform::kHostWindowClass, L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, inst_, this);
    if (!host_) return 4;
    // B2 inceleme: yonetici olarak calisan ornek, orta butunlukteki ikinci baslatmanin aktivasyon mesajini da alir (UIPI).
    // Yalniz bu kayitli mesaj acilir; basarisizsa ikinci baslatma Blocked gorur ve hemen vazgecer.
    ChangeWindowMessageFilterEx(host_, platform::activate_message_id(), MSGFLT_ALLOW, nullptr);

    // --- Performans anahtari (journal + islem motoru; yalniz Evet ile uygulanir) ---
    rec_ = platform::open_recovery_host(adapters_.registry);
    const auto user = platform::inventory::read_user();
    core::perf::JournalBackendDeps deps;
    deps.scope = {"user", rec_->user_store.get(), rec_->user_engine.get()};
    deps.build_plan = [this]() -> contracts::Result<core::perf::Plan> {
        core::perf::Plan plan;
        plan.items = core::perf::power_plan_items(adapters_.power_api);
        bool scheme_change = false;
        for (const auto& i : plan.items)
            if (i.op.id == contracts::OperationId::PowerSchemeSet) scheme_change = true;
        // F-32/F-33/F-42: makine guc plani alt ayarlari. Plan degisikligiyle ayni islemde uygulanmaz
        // (anlik goruntu eski plana, yazma yeni plana giderdi); anlik deger planda yoksa plana girmez.
        for (auto& i : core::perf::vfx_plan_items(adapters_.vfx_api)) plan.items.push_back(std::move(i));
        for (auto& i : core::perf::gaming_plan_items(adapters_.gaming)) plan.items.push_back(std::move(i));  // OPT-09
        for (auto& i : background_plan_items()) plan.items.push_back(std::move(i));  // P2: arka plan programlari (dusuk oncelik)
        // T01: hibrit dizustunde oyunlari ayrik ekran kartina yonlendir. Veri hazirlik taramasindan (yoksa oge yok);
        // yalniz adaptorun yazma kokleri altindaki EXE'ler (apply ile ayni kural).
        {
            core::readiness::GpuPrefInfo gp;
            {
                std::lock_guard<std::mutex> lk(gpu_pref_mu_);
                gp = gpu_pref_cache_;
            }
            for (auto& i : core::perf::gpu_route_plan_items(gp, [this](std::string_view exe) { return adapters_.gpu_pref.exe_writable(exe); }))
                plan.items.push_back(std::move(i));
        }
        // OPT-10: yonetici grubu (tek UAC, broker). Okuma yukseltme istemez; destek dogrulanmadiysa plana girmez.
        for (auto& i : core::perf::hags_plan_items(adapters_.hags_api)) plan.items.push_back(std::move(i));
        adapters_.drs_api.refresh();  // baska aracin surucu profili degisikligi gorulsun
        {
            // A3 (P12 karar 7): melez GPU kapisi. Oyun surecinin adaptoru burada bilinmez (nullopt); yalniz tek donanim GPU'su
            // NVIDIA ise acilir. Dogrulanamazsa parca plana (ve Evet/Hayir onayina) girmez; neden Plan::omitted'a yazilir.
            const bool drs_up = adapters_.drs_api.available();
            const auto gate = core::perf::nvidia_adapter_gate(platform::inventory::read_gpus(), std::nullopt, drs_up);
            if (const auto om = core::perf::nvidia_omission(gate)) plan.omitted.push_back(*om);
            for (auto& i : core::perf::nvidia_plan_items(drs_up ? &adapters_.drs_api : nullptr, gate))
                plan.items.push_back(std::move(i));
        }
        for (auto& i : core::perf::power_subs_plan_items(adapters_.power_subs_api, scheme_change))
            plan.items.push_back(std::move(i));
        // WIN-10: yalniz kullanicinin isaretledigi, derlenmis listedeki ve durdurma onkosullari gecen hizmetler.
        std::vector<std::string> svc;
        {
            std::lock_guard<std::mutex> lk(service_sel_mu_);
            svc = session_stop_services_;
        }
        for (auto& i : core::perf::service_stop_plan_items(adapters_.service_api, adapters_.service_busy, svc))
            plan.items.push_back(std::move(i));
        // T03: Windows Update tutma (broker, UAC). Kurumsal politikada veya okunamazsa oge yok. Kendi yazdigimiz politika
        // degerleri (journal'da etkin) kurumsal sayilmaz; yoksa Acik iken yeniden kurulan plan kendi yazimizi kurum sanardi.
        {
            core::adapters::windows::UpdateStatus us;
            us.managed_by_policy = contracts::Field<bool>::unknown("not read");
            for (const auto& p : platform::inventory::read_policy().policies)
                if (p.id == "windows_update")
                    us.managed_by_policy = p.state == contracts::PolicyState::Unknown
                                               ? contracts::Field<bool>::unknown(p.reason)
                                               : contracts::Field<bool>::known(p.state == contracts::PolicyState::Present, contracts::Provenance::Registry);
            const auto active = admin_channel_.active_ops();
            const bool own = core::perf::update_hold_own_active(active);
            for (auto& i : core::perf::update_hold_plan_items(us, adapters_.tweak, own)) plan.items.push_back(std::move(i));
        }
        // T14: gizlilik ayarlari (katalog; HKCU uygulama icinde, HKLM broker). Zaten ayarli/kurum degeri olanlar plana girmez.
        for (auto& i : core::perf::tweak_plan_items("plan.privacy", adapters_.tweak)) plan.items.push_back(std::move(i));
        for (auto& i : core::perf::tweak_plan_items("plan.privacy_system", adapters_.tweak)) plan.items.push_back(std::move(i));
        // T21: Gelismis katalog gruplari (HKCU, UAC yok, varsayilan Hayir): Windows 11 arayuz temizligi, fare ivmesi.
        for (auto& i : core::perf::tweak_plan_items("plan.win11_cleanup", adapters_.tweak)) plan.items.push_back(std::move(i));
        for (auto& i : core::perf::tweak_plan_items("plan.input", adapters_.tweak)) plan.items.push_back(std::move(i));
        for (auto& i : core::perf::tweak_plan_items("plan.widgets", adapters_.tweak)) plan.items.push_back(std::move(i));  // R-C3 (HKLM, UAC)
        // ADR-040 (B0 cagrilari; gruplar bu surumde ogesiz): on plan onceligi (B2) ve svchost esigi (B3) katalog girdisiyle,
        // dinamik tick (B4) bayrakla KAPALI ve BCD burada okunmaz (okunamayan = oge yok). USB askiya alma (B1) guc alt
        // ayarlari tablosundan, ek NVIDIA ayarlari (B6) nvidia_plan_items icinden gelir.
        for (auto& i : core::perf::tweak_plan_items("plan.foreground_boost", adapters_.tweak)) plan.items.push_back(std::move(i));
        for (auto& i : core::perf::tweak_plan_items("plan.svchost_split", adapters_.tweak)) plan.items.push_back(std::move(i));
        for (auto& i : core::perf::boot_plan_items(adapters_.boot_option_api, core::perf::BootPlanInputs{}, &plan.omitted))
            plan.items.push_back(std::move(i));
        // T12: ozellik sihirbazi (Gelismis, hepsi varsayilan Hayir). Kullanim okumasi salt okuma; kullanimda/bilinmiyorsa grup yok;
        // plan.services ile cakisan grup sunulmaz (ayni hizmet hem oturumluk durdurulup hem Devre Disi birakilmaz).
        {
            const auto usage = platform::readiness::read_feature_usage();
            auto feature_items = core::perf::feature_plan_items(usage, adapters_.service_api, adapters_.service_busy, adapters_.tweak,
                                                                std::span<const core::perf::PlanItem>(plan.items.data(), plan.items.size()));
            for (auto& i : feature_items) plan.items.push_back(std::move(i));
        }
        // T11: secmeli Defender haric tutma (Gelismis, varsayilan Hayir). Defender etkin degilse/ilke yonetiyorsa oge yok.
        {
            std::vector<core::perf::DefenderFolder> folders;
            {
                std::lock_guard<std::mutex> lk(defender_mu_);
                for (const auto& f : defender_folders_) folders.push_back({f.first, f.second});
            }
            for (auto& i : core::perf::defender_plan_items(adapters_.defender_api, folders)) plan.items.push_back(std::move(i));
        }
        // T09: secili oyunlar icin imaj-basina CPU onceligi (Gelismis, varsayilan Hayir). Guvenilmez tarama/cekirdek AC/FACEIT: oge yok.
        {
            core::perf::GamePriorityInput gp;
            {
                std::lock_guard<std::mutex> lk(defender_mu_);
                gp = game_priority_;
            }
            for (auto& i : core::perf::game_priority_plan_items(adapters_.ifeo_priority, gp)) plan.items.push_back(std::move(i));
        }
        // T13: geri alinabilir temizlik (Gelismis, varsayilan Hayir). Edge yalniz baska varsayilan tarayici varken; uygulamalar yalniz
        // kurulu + makine icin provisioned (geri kaydedilebilir) olanlar. Okunamayan/dogrulanamayan: oge yok.
        for (auto& i : core::perf::debloat_plan_items(adapters_.appx_api, platform::adapters::read_browser_info(), adapters_.tweak))
            plan.items.push_back(std::move(i));
        // T08: baska yonetici ogesi varsa ayni UAC'de ilk adim olarak otomatik geri yukleme noktasi (yoksa oge yok: ek UAC yok).
        {
            const bool has_admin = std::any_of(plan.items.begin(), plan.items.end(), [](const core::perf::PlanItem& i) { return i.needs_admin; });
            if (auto rp = core::perf::restore_point_plan_item(has_admin, platform::system_restore_enabled(), std::nullopt))
                plan.items.push_back(std::move(*rp));
        }
        return plan;
    };
    deps.new_tx_id = [] { return platform::random_nonce(); };
    deps.now_unix_ms = now_unix_ms;
    deps.actor_sid = user.process_user_sid.value.value_or("");
    deps.target_sid = user.target_user_sid.value.value_or(deps.actor_sid);
    deps.elevated = platform::is_process_elevated();
    deps.admin = &admin_channel_;  // OPT-02/OPT-10: yonetici adimlari broker'a (UAC reddi -> kismi sonuc)
    deps.restore_log = [](std::string_view line) { platform::append_restore_log(line); };  // saha 2026-11-15: logs/restore.log
    // CORE-03: acilista run_startup_recovery "yeni sema" gorduyse yeni degisiklik yasak (is parcacigi okur).
    deps.writes_blocked = [this] { return journal_writes_blocked_.load(); };
    // R-F1: eski surum verisi gocu (main'de journal acilmadan once calisti); aktarilamayan bekleyen kayit -> kurtarma durumu.
    deps.legacy_pending = [] { return platform::legacy_user_data_pending(); };
    {
        // Arka uc islem siniri: UI ne derse desin her yazma motordan gecerken guncel durum sorulur.
        const core::recovery::WriteGuard guard = [this]() -> core::recovery::WriteDecision {
            const auto d = permit_.verify(core::games::GuardedAction::SettingMutation);
            return {d.allowed, d.reason_key};
        };
        deps.permit = guard;
        for (auto* engine : {rec_->user_engine.get(), rec_->fixes_engine.get(), rec_->startup_engine.get()})
            if (engine) engine->set_guard(guard);
    }
    backend_ =std::make_unique<core::perf::JournalBackend>(deps);
    controller_ = std::make_unique<core::perf::PerfController>(
        *backend_,
        [this](std::function<void()> work) {
            if (worker_.joinable()) worker_.join();
            worker_ = spawn_worker(std::move(work));
        },
        [this](std::function<void()> on_ui) { post(std::move(on_ui)); });
    controller_->set_device_class(read_device_class());
    controller_->on_change = [this] {
        measure_->proof_refresh_precheck();  // P1: Baslat dugmesinin yanindaki neden (secim/faz degisince)
        vm_.perf = ui::make_perf_view(*controller_);
        if (window_) window_->set_perf(vm_.perf);
        refresh_tray();
        const auto ph = controller_->phase();
        if (controller_->auto_active() != last_auto_active_) {  // OPT-06: cokme sonrasi geri alma icin kalici
            last_auto_active_ = controller_->auto_active();
            prefs_.auto_active = last_auto_active_;
            save_prefs();
        }
        // OPT-08: once = Evet'ten onceki son 10 ornek; sonra = dogrulanmis Acik sonrasi 10 ornek.
        if (ph == Phase::Applying && last_phase_ != Phase::Applying) report_.mark_before();
        else if (ph == Phase::On && last_phase_ == Phase::Applying) report_.begin_after();
        else if (last_phase_ == Phase::Applying && ph != Phase::Applying) report_.cancel();
        else if (ph == Phase::Off && last_phase_ != Phase::Off) report_.cancel();
        if (ph != last_phase_) push_before_after();
        const bool finished = (last_phase_ == Phase::Applying || last_phase_ == Phase::Restoring) && ph != last_phase_;
        last_phase_ = ph;
        if (finished && ph == Phase::On && controller_->notice() == core::perf::Notice::GameBlocked && controller_->auto_active()) {
            auto_restore_retry_at_ms_ = mono_ms() + 30000;
            detection_.owe_restore();  // islem sinirinda reddedildi: otomatik geri alma kaybolmaz
        }
        if (finished) {
            {
                std::lock_guard<std::mutex> lk(bg_mu_);
                bg_scan_ms_ = 0;  // P2: uygulama/geri alma sonrasi arka plan adaylari yeniden taranir
            }
            controller_->refresh_preview();  // UI-10: panel yeni sistem durumunu gostersin
            post([this] { run_pending_recovery(); });  // Performans islemi sirasinda bekletilen, onaylanmis kurtarma
            // Islem sonrasi sistem kontrolu bulgulari tazelenir (eski bulgu kalmaz). P1: deney boyunca ertelenir (olcum
            // sirasinda arka plan taramasi CPU harcamasin); deney bitince proof_finish tazeler.
            if (!measure_->proof_running()) start_findings_refresh();
        }
        measure_->proof_on_perf_change();  // P1: deney controller gecislerini bu geri cagriyla izler
        check_pending_exit();
    };

    // WIN-08: temizlik oturumu (gercek dosya sistemi; yonetici kategorileri yukseltilmemis surecte reddedilir)
    {
        core::cleanup::SessionDeps cd;
        cd.preview = [](const std::vector<core::cleanup::CleanupCategory>& cats, const std::wstring& volume,
                        const core::cleanup::PreviewOptions& opt) {
            platform::cleanup::WinCleanupFs fs;
            return core::cleanup::preview(cats, platform::cleanup::discover_env(), fs, volume, opt);
        };
        cd.run = [this](const core::cleanup::CleanupPlan& plan, const std::vector<core::cleanup::CleanupCategory>& sel,
                        const core::cleanup::RunOptions& opt) -> contracts::Result<core::cleanup::CleanupReport> {
            // Yonetici kategorileri yukseltilmis yardimcida (kendi tuttugu planla), kullanici kategorileri burada.
            // Yardimci once calisir: iletisim kaybi olursa kullanici dosyalarina hic dokunulmamis olur.
            std::vector<core::cleanup::CleanupCategory> admin, local;
            bool want_standby = false;
            for (const auto c : sel) {
                if (c == core::cleanup::CleanupCategory::StandbyMemory) want_standby = true;  // T15: dosya degil; broker bakim isi
                else if (core::cleanup::is_admin_product_category(c)) admin.push_back(c);
                else local.push_back(c);
            }
            core::cleanup::CleanupReport merged;
            if (!admin.empty()) {
                auto client = cleanup_admin_;  // calisma boyunca oturumu canli tut
                if (!client || client->closed())
                    return contracts::Result<core::cleanup::CleanupReport>::fail(contracts::ErrorCode::Denied,
                                                                                 "administrator helper not ready");
                auto ar = client->run(admin, opt);
                if (!ar.ok())  // yardimci yarida kaldiysa bazi dosyalar silinmis olabilir: "hic silinmedi" denmez
                    return contracts::Result<core::cleanup::CleanupReport>::fail(ar.code(), "partial: " + ar.error().detail);
                merged = std::move(ar).value();
            }
            if (!local.empty()) {
                platform::cleanup::WinCleanupFs fs;
                auto lr = core::cleanup::run(plan, local, fs, opt);
                if (!lr.ok()) {
                    if (admin.empty()) return lr.error();
                    merged.incomplete = true;  // yonetici kategorileri silindi; rapor atilmaz
                    return merged;
                }
                merged = core::cleanup::merge_reports(std::move(merged), lr.value());
            }
            if (want_standby) {
                // T15: kullanici bu satiri secip normal Temizlik Evet'ini verdi. Kendi UAC'i; sabit is kimligi (yol/komut yok).
                // Dosyalar once islenir; bosaltma son adimdir. Gercek bosaltma bu surumde DOGRULANMADI.
                const contracts::SettingOperation op{contracts::OperationId::MaintenanceJob, contracts::OperationScope::Machine,
                                                     core::maintenance::kStandbyPurgeTarget, core::maintenance::kMaintenanceValue, false, false};
                const auto b = admin_channel_.run(core::ipc::RequestAction::Maintenance, {op});
                if (b.elevation == core::ElevationOutcome::Started && !b.steps.empty() && !b.steps.front().unknown &&
                    b.steps.front().state == core::broker::StepState::GateBlocked) {
                    if (merged.gate_reason.empty()) merged.gate_reason = "chk.notice.gate_blocked";  // UAC sonrasi oyun acildi
                } else {
                    merged.standby.ran = true;
                    if (b.elevation == core::ElevationOutcome::Started && !b.steps.empty() && !b.steps.front().unknown &&
                        b.steps.front().state == core::broker::StepState::Applied) {
                        const auto pr = core::maintenance::parse_purge_detail(b.steps.front().detail);
                        merged.standby.ok = pr.ok;
                        merged.standby.before_bytes = pr.before_bytes;
                        merged.standby.after_bytes = pr.after_bytes;
                    }
                }
            }
            return merged;
        };
        cd.run_background = [this](std::function<void()> work) {
            if (cleanup_worker_.joinable()) cleanup_worker_.join();
            cleanup_worker_ = spawn_worker(std::move(work));
        };
        cd.post_ui = [this](std::function<void()> f) { post(std::move(f)); };
        cd.gate_denied = [this] { return permit_.verify(core::games::GuardedAction::DiskCleanup).reason_key; };
        cleanup_ = std::make_unique<core::cleanup::CleanupSession>(std::move(cd));
        cleanup_->on_change = [this] { push_cleanup(); };
        cleanup_->set_volume(platform::cleanup::system_volume());  // varsayilan: sistem birimi (Idle: tarama yok)
    }
    // WIN-09: baslangic girdileri (ayri "startup" journal kapsami; Performans Kapali bunlari geri almaz, ADR-019)
    {
        startup_ctx_.store = rec_->startup_store.get();
        startup_ctx_.engine = rec_->startup_engine.get();
        startup_ctx_.api = &adapters_.startup_api;
        startup_ctx_.new_tx_id = [] { return platform::random_nonce(); };
        startup_ctx_.now_unix_ms = now_unix_ms;
        startup_ctx_.actor_sid = deps.actor_sid;
        startup_ctx_.target_sid = deps.target_sid;
        core::startup::StartupDeps sd;
        sd.scan = [this]() -> contracts::Result<core::startup::ScanResult> {
            namespace sa = core::adapters::startup;
            core::startup::ScanResult out;
            std::vector<std::string> act;
            if (rec_->startup_store) act = core::startup::active_targets(*rec_->startup_store);
            platform::adapters::WinStartupApi api;
            bool any = false;
            for (auto l : {sa::Location::UserRun, sa::Location::MachineRun64, sa::Location::MachineRun32, sa::Location::UserFolder,
                           sa::Location::CommonFolder}) {
                auto r = api.list(l);
                if (!r.ok()) continue;
                any = true;
                for (auto& e : r.value()) {
                    core::startup::StartupRow row{e, sa::recommend(sa::from_entry(e)), false};
                    row.ours = std::find(act.begin(), act.end(), sa::format_target(e.location, e.name)) != act.end();
                    out.rows.push_back(std::move(row));
                }
            }
            if (!any) return contracts::Result<core::startup::ScanResult>::fail(contracts::ErrorCode::IoError, "startup list");
            platform::adapters::WinServiceApi svc;
            for (const auto& s : core::adapters::windows::session_stoppable_services()) {
                core::startup::ServiceRow r{std::string(s.name), std::string(s.name), s.breaks_key, false, false};
                if (auto g = svc.get(std::string(s.name)); g.ok()) {
                    r.present = true;
                    r.running = g.value().running;
                    if (!g.value().display_name.empty()) r.display_name = g.value().display_name;
                }
                out.services.push_back(std::move(r));
            }
            return out;
        };
        sd.change = [this](const core::adapters::startup::Entry& e, bool en) { return core::startup::change_entry(startup_ctx_, e, en); };
        sd.run_background = [this](std::function<void()> work) {
            if (startup_worker_.joinable()) startup_worker_.join();
            startup_worker_ = spawn_worker(std::move(work));
        };
        sd.post_ui = [this](std::function<void()> f) { post(std::move(f)); };
        sd.gate_denied = [this] { return permit_.verify(core::games::GuardedAction::SettingMutation).reason_key; };
        startup_ = std::make_unique<core::startup::StartupSession>(std::move(sd));
        startup_->on_change = [this] { push_startup(); };
    }
    // OPT-07: sistem kontrolu (otomatik = Performans planindaki kullanici kapsami ogeler; rehberli = saglik/surucu)
    {
        core::perf::CheckDeps cd;
        cd.scan = [this]() {
            std::vector<core::perf::CheckFinding> out;
            if (auto pl = backend_->plan(); pl.ok())
                for (const auto& it : pl.value().items) {
                    // Ayni plan grubu (ornek VFX'in birden cok kayit adimi) tek satir: tek Evet/Hayir.
                    auto g = std::find_if(out.begin(), out.end(), [&](const core::perf::CheckFinding& f) { return f.item && f.item->group() == it.group(); });
                    if (g != out.end()) g->extra_items.push_back(it);
                    else out.push_back({it.title_key, core::perf::FixKind::Auto, it, {}});
                }
            // B2 / bosluk 6: cikis basladiysa kalan (yavas olabilecek WMI/WUA/BITS) bulgu okumasi yapilmaz.
            if (check_cancel_) return out;
            ui::ViewModel tmp;
            load_findings(tmp);
            if (check_cancel_) return out;
            for (const auto& f : tmp.findings) {
                if (!f.warning) continue;
                const bool driver = f.text_key.rfind("update.", 0) == 0;
                const bool bios = f.text_key == "health.xmp_off" || f.text_key == "health.rebar_off" || f.text_key == "health.bios_old";
                out.push_back({f.text_key, driver ? core::perf::FixKind::GuidedDriver : bios ? core::perf::FixKind::GuidedBios : core::perf::FixKind::GuidedHardware,
                               std::nullopt, {f.text_key}});
            }
            return out;
        };
        cd.apply = [this](const core::perf::Plan& p, const std::function<void(std::size_t, std::size_t)>& prog) {
            // check_cancel_ yalniz cikista true olur ve geri sifirlanmaz (B2: cikis sinyali is basinda silinmesin).
            return backend_->apply(p, [prog](const core::recovery::Progress& x) { prog(x.done, x.total); }, check_cancel_);
        };
        cd.run_background = [this](std::function<void()> work) {
            if (check_worker_.joinable()) check_worker_.join();
            check_worker_ = spawn_worker(std::move(work));
        };
        cd.post_ui = [this](std::function<void()> f) { post(std::move(f)); };
        check_ = std::make_unique<core::perf::SystemCheck>(std::move(cd));
        check_->on_change = [this] {
            const auto ph = check_->phase();
            if (ph == core::perf::CheckPhase::Review && check_selected_.size() != check_->findings().size()) {
                check_selected_.clear();
                for (const auto& f : check_->findings()) check_selected_.push_back(f.kind == core::perf::FixKind::Auto);
            }
            if (ph == core::perf::CheckPhase::Report || ph == core::perf::CheckPhase::Failed) {
                check_selected_.clear();
                controller_->init();  // uygulanan degisiklikler Performans anahtarina yansir (Kapali hepsini geri alir)
            }
            push_check();
        };
    }
    load_consent();
    load_prefs();
    reconcile_autostart();  // ADR-030: tercih yuklendikten sonra, ilk push_settings'ten once
    refresh_watch_consent();
    load_history();  // INS-01
    // EF-X22: kesik kosu, gecmis YUKLENDIKTEN sonra eklenir (aksi halde kayit tum gecmisi ezerdi). Smoke rolu gercek
    // veri klasorune dokunmaz.
    if (!smoke_ && recover_run_marker()) {
        vm_.measure.history.clear();
        load_history();
    }
    controller_->load_selection(prefs_.plan_item_selected);
    controller_->set_no_effect_groups(prefs_.proof_no_effect_groups);  // P4: kayitli "Fark yok" gruplari (yalniz vurgu)
    controller_->on_selection_changed = [this](const std::string& g, bool on) {
        prefs_.plan_item_selected[g] = on;  // kalici: bir sonraki acilista da ayni secim
        save_prefs();
    };
    vm_.show_welcome = !prefs_.welcome_seen && !smoke_;
    vm_.measure.builtin_available = true;  // BENCH-06: Uygulama Testi bagli
    measure_->proof_refresh_estimate();    // P1
    measure_->proof_refresh_precheck();
    vm_.measure.live_view.user_on = true;  // sahip karari: canli test ekrani tum testlerde her zaman acik (anahtar kaldirildi)
    push_settings();
    {
        const auto v = core::product_version();
        vm_.build_id = std::string(core::build_id());
        capture_collector_commit_ = core::games::observed_collector_commit(vm_.build_id);
        vm_.version = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    }
    // ADR-033 spec 8: kurulum yeri disinda calisan Ui (eski tasinabilir Run kaydi, 0.2.x'ten zorunlu guncelleme)
    // engellemeyen "Kurulum gerekli · Kur" seridi gosterir. Salt okuma (yol karsilastirmasi).
    vm_.install_offer = !smoke_ && args_.role == core::Role::Ui && install_offer_applies();
    refresh_admin_path();
    // Zorunlu guncelleme: onceki degisimin kalintilari, sonra surec basina tek kontrol (pencere hemen acilir).
    update_startup_recover();
    start_update_check();

    // Tepsi (smoke testinde yok). Kabuk yoksa (oturum/CI) tepsisiz devam: pencere kapaninca cikilir.
    if (!smoke_) {
        update_tray_icon();  // simge + "Durum dogrulaniyor" (baslangic durumu henuz okunmadi)
        tray_.add(host_, t(StringId::TrayTipChecking));
    }
    // --tray: yalniz tepsi; aksi halde pencere hemen acilir.
    if (!args_.tray || !tray_.added()) open_window(false);
    if (args_.role == core::Role::Uninstall) {
        // ADR-033 spec 8 (ARP / --uninstall): sessiz kaldirma yok. Ayarlar acilir ve ayni "Kaldir" onayi (Evet/Hayir)
        // sorulur; oyun kapisi / mesguliyet notu ve sonraki adimlar Ayarlar'daki dugmeyle birebir aynidir.
        post([this] {
            if (!window_) return;
            window_->show_page(ui::Page::Settings);
            if (window_->on_uninstall) window_->on_uninstall(ui::MainWindow::UninstallAction::Begin);
        });
    }
    signal_update_healthy();  // --updated-from: pencere acildi -> eski surece saglik isareti

    // Acilis: once yarim kalan islemler uzlastirilir (REC-05), sonra anahtar durumu journal'dan okunur.
    // Yukseltilmemis surec yalniz kullanici kapsamlarini uzlastirir; makine (broker) journal'larindaki yarim kalmis
    // kayitlar sessizce birakilmaz: Evet/Hayir ile sorulur, Evet -> tek UAC, broker oturum basinda uzlastirir (M2).
    worker_ = spawn_worker([this] {
        // Yarim islem geri alma da yazmadir: oyun acikken veya durum dogrulanamazken yapilmaz; kayitlar korunur ve
        // ilk taze guvenilir "oyun yok" sonucunda calisir.
        const bool allowed = permit_.verify(core::games::GuardedAction::SettingMutation).allowed;
        core::recovery::RecoverySummary recovery;
        if (allowed) {
            recovery = core::recovery::run_startup_recovery(rec_->user_scopes());
            // Saha 2026-11-15: acilis uzlastirmasinin sonucu ve hala geri alinamamis ayarlar logs/restore.log'a.
            for (const auto& [name, rep] : recovery.scopes) {
                if (rep.restored.empty() && rep.recovery_required.empty() && rep.corrupt.empty()) continue;
                platform::append_restore_log("startup reconcile scope=" + name + " restored=" + std::to_string(rep.restored.size()) +
                                             " recovery_required=" + std::to_string(rep.recovery_required.size()) +
                                             " unreadable=" + std::to_string(rep.corrupt.size()));
            }
            for (const auto& sc : rec_->user_scopes())
                if (sc.store)
                    for (const auto& i : core::recovery::failed_restore_items(sc.name, sc.store->scan()))
                        platform::append_restore_log("startup " + core::recovery::restore_log_line(i));
        } else {
            for (const auto& sc : rec_->user_scopes())
                if (sc.store && !sc.store->scan().newer.empty()) recovery.writes_blocked = true;
        }
        auto machine_pending = admin_channel_.unreconciled_ops();  // salt okuma; UAC yok
        // Koruma Modu (T07): Windows ana yapi numarasi (registry okumasi is parcaciginda; okunamazsa 0 = karsilastirma yok).
        const std::uint32_t os_build_now = core::capabilities::os_build_major(os_build_string()).value_or(0);
        post([this, allowed, os_build_now, recovery = std::move(recovery), machine_pending = std::move(machine_pending)]() mutable {
            // CORE-03: bilinmeyen yeni sema varsa yeni degisiklik yasagi (JournalBackend + motor da uygular).
            journal_writes_blocked_ = recovery.writes_blocked;
            if (!allowed) {
                startup_recovery_deferred_ = true;
                detection_.owe_recovery();
            }
            controller_->set_os_updated(os_build_now != 0 && prefs_.last_os_build != 0 && prefs_.last_os_build != os_build_now);
            if (os_build_now != 0 && prefs_.last_os_build != os_build_now) {
                prefs_.last_os_build = os_build_now;
                save_prefs();
            }
            controller_->init();
            if (prefs_.auto_active && !journal_writes_blocked_) controller_->resume_auto();  // OPT-06: onceki oturum otomatik acmisti
            start_game_watch();  // ilk goruntude oyun yoksa otomatik acilan Performans geri alinir
            controller_->refresh_preview();  // UI-10: "neleri degistirir" paneli
            state_known_ = true;  // artik dogrulanmis durum biliniyor
            update_tray_icon();
            offer_machine_recovery(std::move(machine_pending));
        });
    });
    // Envanter arka planda okunur; pencere hemen acilir, kartlar iskeletle bekler.
    // C2: en yeni denge kaydinin link okumasi deger kopyasi olarak tasinir; bugunku ekran karti envanterden okununca
    // kayitla karsilastirilir (kart degistiyse eski kartin linki bilesen raporuna girmez).
    // B10 inceleme: Uygulama Testi kaniti da bugunku donanimla eslesmelidir; gecmisin (en fazla kMaxEntries kayit) deger
    // kopyasi tasinir, is parcacigi uyenin bench_runtime_'ina dokunmaz (UI is parcacigi onu yazar: veri yarisi yok).
    loader_ = spawn_worker([this, pcie_rec = latest_balance_pcie_record(), hist = history_.entries()] {
        ui::ViewModel sys;
        load_system(sys);
        const std::string os_build = os_build_string();  // EF-X18: yukleyici is parcaciginda okunur (UI'da registry yok)
        // Uyumluluk B8 (bosluk 12): dogrulanmis cihaz kombinasyonu mu (salt okuma; engellemeyen Ayarlar notu).
        const auto device = platform::inventory::current_device_verification();
        const auto pcie = core::measure::balance_pcie_for_gpu(pcie_rec, sys.gpu);
        const auto runtime = core::diagnosis::benchmark_runtime_from_history(hist, core::diagnosis::diagnosis_release_status(),
                                                                             {sys.cpu, sys.gpu, sys.gpu_driver});
        post([this, sys, os_build, device] {
            vm_.os = sys.os;
            vm_.settings.device_unverified = !device.verified;
            vm_.settings.device_os_unverified = !device.os_verified;
            vm_.settings.device_gpu_unverified = !device.gpu_verified;
            vm_.cpu = sys.cpu;
            vm_.gpu = sys.gpu;
            vm_.gpu_cards = sys.gpu_cards;
            bench_pcie_ = latest_balance_pcie();  // ekran karti artik biliniyor (sonraki rapor tazelemeleri icin)
            vm_.gpu_driver = sys.gpu_driver;
            bench_runtime_ = latest_benchmark_runtime();  // B10 inceleme: islemci / ekran karti / surucu artik biliniyor
            capture_os_build_ = os_build.find('?') == std::string::npos ? os_build : std::string();
            capture_os_driver_scope_ = core::games::observed_os_driver_scope(os_build, sys.gpu_driver);
            vm_.ram = sys.ram;
            vm_.board = sys.board;
            vm_.display = sys.display;
            // Guncellemeler uretici satirlari (envanter; arama sonucu ve son denetim zamani korunur).
            vm_.updates.gpus = sys.updates.gpus;
            vm_.updates.cpu_vendor = sys.updates.cpu_vendor;
            vm_.updates.board_vendor = sys.updates.board_vendor;
            vm_.updates.board_model = sys.updates.board_model;
            vm_.updates.bios_version = sys.updates.bios_version;
            vm_.updates.bios_date = sys.updates.bios_date;
            vm_.updates.problem_devices = sys.updates.problem_devices;
            vm_.updates.problem_devices_loaded = sys.updates.problem_devices_loaded;
            vm_.system_loading = false;
            if (window_) window_->set_view(vm_);
        });
        ui::ViewModel sto;
        load_storage(sto);
        post([this, s = std::move(sto.storage)]() mutable {
            vm_.storage = std::move(s);
            vm_.storage_loading = false;
            // BENCH-05: depolama testi icin yalniz olculebilen yerel birimler; secim varsayilan "yapma".
            std::vector<std::string> roots;
            for (const auto& v : vm_.storage)
                if (v.total_gib) roots.push_back(v.root);
            // B10 (sahip istegi): varsayilan gercekten "Yapma"; sistem birimi otomatik secilmez (saf yardimci, birim testli).
            core::measure::adopt_storage_roots(vm_.measure, std::move(roots));
            if (window_) window_->set_view(vm_);
            push_cleanup();  // EXT-CLN-01: temizlik birim secenekleri envanterden
        });
        ui::ViewModel fnd;
        load_findings(fnd, runtime, pcie);
        post([this, f = std::move(fnd.findings), c = std::move(fnd.components),
              k = std::move(fnd.diagnosis_gate_key)]() mutable {
            ui::adopt_component_report(vm_, std::move(f), std::move(c), std::move(k));
            if (window_) window_->set_view(vm_);
        });
    });

    MSG msg{};
    BOOL r;
    while ((r = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (r == -1) return 5;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace ef::app
