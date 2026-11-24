// Guncellemeler: surucu taramasi, Windows Update kurulumu, resmi paket. (denetim 4.4: app.cpp'den ayrildi; App uye islevleri).
#include "app/app_internal.h"

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
#include <map>
#include <optional>
#include <psapi.h>
#include <shellapi.h>
#include <thread>
#include <tlhelp32.h>
#include <ctime>
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
#include "core/perf/vfx_plan.h"
#include "core/perf/gaming_plan.h"
#include "core/perf/admin_plan.h"
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
#include "platform/windows/nvidia/nvapi_drs.h"  // F-61: salt okuma GPU limit/saat telemetrisi
#include "platform/windows/run_key_store.h"
#include "platform/windows/wua/wua_search.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"
#include "ui/consent/consent_dialog.h"
#include "ui/brand/brand.h"
#include "ui/theme/system_palette.h"
#include "ui/theme/theme.h"

namespace ef::app {

using core::i18n::StringId;
using core::perf::Phase;
using namespace detail;

void App::start_driver_scan() {
    // Spec 2026-11-12 H: salt okuma Windows Update surucu aramasi (yonetici yok, indirme / kurulum yok). Oyun acikken ya
    // da tespit guvenilir degilken baslamaz; tek arama. Sonuc oturum icidir (diske yazilmaz).
    if (vm_.game_gate_blocked) return;
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    if (vm_.updates.install.running) return;  // spec H2: kurulum surerken liste yeniden aranmaz
    if (!driver_scanner_) driver_scanner_ = std::make_unique<core::drivers::DriverScanner>([] { return platform::wua::make_driver_search(); });
    if (driver_scanner_->running()) return;
    updates_auto_pending_ = false;
    const bool started = driver_scanner_->start([this](const core::drivers::ScanView& v) {
        post([this, v] {
            vm_.updates.scan = v;
            SYSTEMTIME st{};
            GetLocalTime(&st);
            char hm[24];
            std::snprintf(hm, sizeof hm, "%02u.%02u.%04u %02u:%02u", static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wMonth),
                          static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute));
            vm_.updates.last_check = hm;
            if (window_) window_->set_updates(vm_.updates);
        });
    });
    if (!started) return;
    vm_.updates.scan = core::drivers::ScanView{};
    vm_.updates.scan.state = core::drivers::ScanState::Checking;
    if (window_) window_->set_updates(vm_.updates);
}

void App::run_driver_install(std::size_t k) {
    // Spec 2026-11-12 H2: tek Evet/Hayir (kurulacak oge + "Yonetici izni istenir") -> UAC -> yukseltilmis yardimci.
    // Yardimci ogeyi UpdateID + RevisionNumber ile yeniden arar; buradan yalniz kimlik gider (baslik / sinif gitmez).
    // Bir seferde tek kurulum; oyun acikken ya da arama surerken baslamaz. Sonuc yalniz yardimcinin yanitindan gelir.
    const auto& scan = vm_.updates.scan;
    if (scan.state != core::drivers::ScanState::Found || k >= scan.updates.size()) return;
    const auto& u = scan.updates[k];
    if (core::drivers::is_firmware(u)) return;  // uygulama BIOS / firmware yazmaz (yardimci da reddeder)
    const std::string target = core::drivers::install_target(u.update_id, u.revision);
    if (target.empty() || vm_.updates.install.running || admin_busy_) return;
    if (driver_scanner_ && driver_scanner_->running()) return;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;  // oyun acik: neden seridi gosterildi
    const HWND owner = window_ ? window_->hwnd() : host_;
    const std::wstring what = window_ ? window_->updates_install_what(k) : std::wstring();
    refresh_admin_path();  // onay notu ("UAC sorulmaz") pencereden hemen once guncel SCM durumuna uysun
    if (!ui::ask_driver_install(owner, cat(), what, vm_.admin_without_uac)) return;  // Hayir / Esc / pencere yok: hicbir sey baslamaz
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;  // bekleyis sirasinda oyun acildi: UAC acilmaz
    const std::string name = core::drivers::display_name(u);
    const contracts::SettingOperation op{contracts::OperationId::DriverInstall, contracts::OperationScope::Machine, target,
                                         std::string(core::drivers::kInstallValue), false, false};
    const bool started = run_admin_job([this, op, target, name]() -> std::function<void()> {
        const auto b = admin_channel_.run(core::ipc::RequestAction::DriverInstall, {op});
        const auto n = core::broker::driver_install_notice(b);
        return [this, n, target, name] {
            auto& in = vm_.updates.install;
            in = {};
            in.target = target;
            in.name = name;
            in.notice_key = std::string(n.key);
            in.warn = n.warn;
            in.reboot = n.reboot;
            in.no_restore_point = n.no_restore_point;
            in.previous_interrupted = n.previous_interrupted;
            in.hresult = n.hresult;
            if (window_) window_->set_updates(vm_.updates);
            if (n.installed) start_driver_scan();  // liste gercek durumu yeniden okur (sonuc karti korunur)
        };
    });
    if (!started) return;
    auto& in = vm_.updates.install;
    in = {};
    in.running = true;
    in.target = target;
    in.name = name;
    if (window_) window_->set_updates(vm_.updates);
}

void App::run_vendor_package(std::size_t k) {
    using LS = ui::ViewModel::UpdatesView::LocalView::Stage;
    auto& uv = vm_.updates;
    if (uv.local.stage == LS::Copying) {  // ayni dugme "Durdur": kopya birakilir, yarim dosya silinir
        if (uv.local.gpu == k) local_cancel_ = true;
        return;
    }
    if (uv.local.stage != LS::None || uv.install.running || admin_busy_ || k >= uv.gpus.size()) return;
    const auto vendor = core::drivers::gpu_vendor_from_pci(uv.gpus[k].vendor_id);
    if (!vendor) return;
    if (driver_scanner_ && driver_scanner_->running()) return;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;  // oyun acik: neden seridi gosterildi
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring title = t(StringId::UpdatesLocalPickTitle);
    if (const auto p = title.find(L"{vendor}"); p != std::wstring::npos) title.replace(p, 8, platform::to_wide(core::drivers::display_name(*vendor)));
    const std::wstring filter = t(StringId::UpdatesLocalFilter);
    const auto picked = platform::ask_open_path(owner, title, filter.c_str(), L"*.exe");
    if (picked.status == platform::OpenPathResult::Status::Canceled) return;  // vazgecti: durum degismez
    const std::string name = platform::to_utf8(std::filesystem::path(picked.path).filename().wstring());
    const std::string id = platform::vendor::new_package_id();
    auto fail = [this, name](std::string_view key) {
        auto& in = vm_.updates.install;
        in = {};
        in.name = name;
        in.notice_key = std::string(key);
        in.warn = true;
        if (window_) window_->set_updates(vm_.updates);
    };
    if (picked.status != platform::OpenPathResult::Status::Chosen || id.empty()) return fail("updates.local.copy_failed");
    if (local_worker_.joinable()) local_worker_.join();
    local_cancel_ = false;
    uv.install = {};
    uv.local = {};
    uv.local.stage = LS::Copying;
    uv.local.gpu = k;
    uv.local.name = name;
    if (window_) window_->set_updates(vm_.updates);
    local_worker_ = spawn_worker([this, src = std::filesystem::path(picked.path), id, name, k, v = *vendor] {
        platform::vendor::clear_package_dir();  // onceki oturumdan kalan yarim / eski kopyalar
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        const auto run = platform::vendor::import_package(src, id, [this] { return local_cancel_.load(); },
                                                          [this, &last](std::uint64_t done, std::uint64_t total) {
                                                              const auto now = std::chrono::steady_clock::now();
                                                              if (now - last < std::chrono::milliseconds(150) && done != total) return;
                                                              last = now;
                                                              post([this, done, total] {
                                                                  if (vm_.updates.local.stage != ui::ViewModel::UpdatesView::LocalView::Stage::Copying) return;
                                                                  vm_.updates.local.done = done;
                                                                  vm_.updates.local.total = total;
                                                                  if (window_) window_->set_updates(vm_.updates);
                                                              });
                                                          });
        using CO = core::drivers::CopyOutcome;
        if (run.outcome != CO::Ok) {
            const std::string_view key = run.outcome == CO::Cancelled                               ? "updates.local.copy_cancelled"
                                         : run.outcome == CO::TooLarge || run.outcome == CO::TooSmall ? "updates.local.size"
                                                                                                     : "updates.local.copy_failed";
            post([this, key, name] {
                vm_.updates.local = {};
                auto& in = vm_.updates.install;
                in = {};
                in.name = name;
                in.notice_key = std::string(key);
                in.warn = true;
                if (window_) window_->set_updates(vm_.updates);
            });
            return;
        }
        post([this] {
            vm_.updates.local.stage = ui::ViewModel::UpdatesView::LocalView::Stage::Verifying;
            if (window_) window_->set_updates(vm_.updates);
        });
        // On denetim (yalniz geri bildirim): asil karar yardimcida, kopya uzerinde yeniden verilir.
        core::drivers::SignatureCheck sig;
        if (const auto dir = platform::vendor::user_package_dir(false))
            sig = platform::vendor::verify_signer(*dir / platform::to_wide(core::drivers::package_file_name(id)));
        post([this, k, v, id, name, bytes = run.bytes, sig] { on_vendor_package_ready(k, v, id, name, bytes, sig); });
    });
}

void App::on_vendor_package_ready(std::size_t k, core::drivers::GpuVendor vendor, std::string id, std::string name, std::uint64_t bytes,
                                  core::drivers::SignatureCheck sig) {
    namespace cd = core::drivers;
    vm_.updates.local = {};
    auto set_notice = [this, &name](std::string_view key) {
        auto& in = vm_.updates.install;
        in = {};
        in.name = name;
        in.notice_key = std::string(key);
        in.warn = true;
        if (window_) window_->set_updates(vm_.updates);
    };
    switch (cd::check_signer(sig.status, sig.signer, vendor)) {
        case cd::SignerVerdict::Ok: break;
        case cd::SignerVerdict::WrongSigner:
            platform::vendor::remove_package(id);
            return set_notice("updates.local.wrong_signer");
        default:
            platform::vendor::remove_package(id);
            return set_notice("updates.local.bad_signature");
    }
    if (window_) window_->set_updates(vm_.updates);
    if (vm_.updates.install.running || admin_busy_ || k >= vm_.updates.gpus.size()) return platform::vendor::remove_package(id);
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring signed_by = t(StringId::UpdatesLocalSignedBy);
    if (const auto p = signed_by.find(L"{signer}"); p != std::wstring::npos) signed_by.replace(p, 8, platform::to_wide(sig.signer));
    const bool tr = cat().locale() == core::i18n::Locale::TrTR;
    const std::wstring what = platform::to_wide(name) + L" · " + ui::format_bytes(bytes, tr) + L" · " + signed_by;
    if (!ui::ask_vendor_install(owner, cat(), what)) return platform::vendor::remove_package(id);  // Hayir: hicbir sey baslamaz
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return platform::vendor::remove_package(id);  // oyun acildi: UAC acilmaz
    const std::string target = cd::vendor_target(vendor, id);
    if (target.empty()) return platform::vendor::remove_package(id);
    const contracts::SettingOperation op{contracts::OperationId::VendorDriverInstall, contracts::OperationScope::Machine, target,
                                         std::string(cd::kVendorInstallValue), false, false};
    const bool started = run_admin_job([this, op, target, name, id, k]() -> std::function<void()> {
        const auto b = admin_channel_.run(core::ipc::RequestAction::VendorInstall, {op});
        const auto n = core::broker::vendor_install_notice(b);
        platform::vendor::remove_package(id);  // kullanici klasorundeki kopya her sonucta silinir
        return [this, n, target, name, k] {
            auto& in = vm_.updates.install;
            in = {};
            in.target = target;
            in.name = name;
            in.notice_key = std::string(n.key);
            in.warn = n.warn;
            in.reboot = n.reboot;
            in.no_restore_point = n.no_restore_point;
            in.previous_interrupted = n.previous_interrupted;
            in.exit_code = n.exit_code;
            in.version = n.version;
            // Geri okunan gercek surum satira yansir (okunamadiysa eski deger kalir; uydurulmaz).
            if (!n.version.empty() && k < vm_.updates.gpus.size()) vm_.updates.gpus[k].driver_version = n.version;
            if (window_) window_->set_updates(vm_.updates);
            if (n.installed) start_driver_scan();  // liste gercek durumu yeniden okur (sonuc karti korunur)
        };
    });
    if (!started) return platform::vendor::remove_package(id);
    auto& in = vm_.updates.install;
    in = {};
    in.running = true;
    in.target = target;
    in.name = name;
    if (window_) window_->set_updates(vm_.updates);
}

}  // namespace ef::app
