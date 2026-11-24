// Oyun Hazirligi / Sistem: tarama, duzelt, geri al, kurtarma, kapi. (denetim 4.4: app.cpp'den ayrildi; App uye islevleri).
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
#include "core/readiness/fix_plan.h"   // P12-B1: Duzelt = gercekten duzeltir
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
#include "core/adapters/windows/display_process.h"
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
#include "platform/windows/metrics/raw_mouse.h"
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

void App::start_readiness_scan() {
    if (vm_.readiness.mouse_running || vm_.readiness.dns_running || vm_.readiness.health_running) return;  // T16/T10: DNS olcumu de ayni is parcacigini kullanir; T06: olcum ayni is parcacigini kullanir; bitince yerinde yenilenir
    if (readiness_worker_.joinable()) {
        if (vm_.readiness.loading) return;  // tarama suruyor
        readiness_worker_.join();
    }
    vm_.readiness.loading = true;
    if (window_) window_->set_view(vm_);
    // Donanim bolumu: mevcut saglik bulgulari (XMP, tek kanal, PCIe, ReBAR, BIOS/surucu yasi ...).
    std::vector<core::readiness::Check> hw;
    for (const auto& f : vm_.findings) {
        if (f.text_key == "health.bios_old") continue;  // software.bios ayni tarih gozlemini zaten tasir: iki "islem" sayilmaz
        core::readiness::Check c;
        c.id = "hw." + f.text_key;
        c.section = f.text_key.rfind("update.", 0) == 0 ? core::readiness::Section::Software : core::readiness::Section::Hardware;
        c.level = f.warning ? core::readiness::Level::Advice : core::readiness::Level::Info;
        c.text_key = f.text_key;
        hw.push_back(std::move(c));
    }
    // P6: kullanicinin sectigi (elle eklenen) ve onay listesindeki oyunlarin tam EXE yollari (yalniz yol bicimli kimlikler).
    std::vector<std::string> game_exes = prefs_.manual_game_exes;
    for (const auto& e : consent_.list())
        if (e.id.find('\\') !=std::string::npos && std::find(game_exes.begin(), game_exes.end(), e.id) == game_exes.end())
            game_exes.push_back(e.id);
    auto mouse_check = core::readiness::mouse_polling_check(false, mouse_result_);  // T06: son olcum bellekten (yoksa sayi yok)
    readiness_worker_ = spawn_worker([this, hw = std::move(hw), mouse_check = std::move(mouse_check), dns_report = dns_report_, health_report = health_report_, pings = last_pings_, days = prefs_.incident_days, game_exes = std::move(game_exes)]() mutable {
        core::readiness::GpuPrefInfo gpu_pref;
        auto checks = collect_readiness_checks(pings, days, game_exes, &gpu_pref);  // platform okuyuculari (salt okuma; ayar degismez); EF-X01 7/30 gun
        {
            std::lock_guard<std::mutex> lk(gpu_pref_mu_);  // T01: Performans plani bu okumadan oge uretir (tarama yoksa oge yok)
            gpu_pref_cache_ = std::move(gpu_pref);
        }
        for (auto& c : hw) checks.push_back(std::move(c));
        checks.push_back(std::move(mouse_check));
        for (auto& c : core::readiness::dns_bench_checks(false, dns_report, checks)) checks.push_back(std::move(c));  // T10: son olcum bellekten
        for (auto& c : core::diagnostics::health_test_checks(false, 0, 0, health_report)) checks.push_back(std::move(c));  // T16: son sonuc bellekten
        core::readiness::sort_checks(checks);
        post([this, checks = std::move(checks)]() mutable {
            vm_.readiness.checks = std::move(checks);
            // "Geri al" / "Geri al / Yeniden dene". Yonetici/geri alma isi suruyorsa journal okunmaz (dugmeler zaten
            // gizli); is bitince finish_admin_job yeniden tarar.
            // Yonetici isi suruyorsa eski undo listesi yeni bulgu listesiyle eslesmez: temizlenir (yanlis satir yok).
            if (!admin_busy_) vm_.readiness.undo = core::readiness::undo_states(vm_.readiness.checks, fix_targets());
            else vm_.readiness.undo.clear();
            vm_.readiness.loading = false;
            vm_.readiness.net_available = true;
            if (window_) window_->set_view(vm_);
        });
    });
}

bool App::run_auto_fix(const core::readiness::Check& c) {
    // Desteklenen otomatik duzeltmeler: "fixes" kapsami, Evet/Hayir, geri okuma, Islem Gecmisi. F-73: tek "Duzelt"
    // arka plan kaydinin uc anahtarini tek transaction'da kapatir; "Geri al" ucuunu birlikte geri alir.
    // Hangi bulgunun hangi islemlere donustugu core/readiness/fix_plan.cpp'dedir (Panel "Duzelt" ayni kurali kullanir).
    if (c.action_arg == "fix.restore_point") return run_restore_point_job(c);
    if (c.action_arg == "fix.image_check") return run_image_job(c, false);
    if (core::readiness::maintenance_job_operation(c)) return run_standby_purge_job(c);  // B5: elle bakim isi; ayar degil
    if (c.action_arg == "fix.dns") return run_admin_fix(c);  // kullanici hazir listeden secer; toplu Duzelt'e girmez
    auto item = core::readiness::auto_fix_item(c);
    if (!item) return false;  // uygulamanin yazamadigi bulgu (ornek fix.power): dugme gosterilmez, yazma yok
    if (item->admin) return run_admin_fix(c);  // OPT-10 / F-48: yonetici (broker, tek UAC)
    auto ops = std::move(item->ops);
    const bool display = item->display;
    if (ops.empty()) return false;
    if (!rec_ || !rec_->fixes_store || !rec_->fixes_engine) return false;
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return true;  // oyun calisiyor: neden karti gosterildi
    // Spec 2026-11-12 B3: "Uygulansin mi?" + degisiklik ("eski → yeni") + "Geri alinabilir." + mevcut ayrinti metni.
    const std::wstring finding = window_ ? window_->check_text(c) : std::wstring();
    const auto detail = platform::to_wide(cat().get(display ? core::i18n::StringId::ChkFixDetailDisplay : core::i18n::StringId::ChkFixDetail));
    if (!ui::ask_fix_apply(owner, cat(), ui::fix_change_line(cat(), c), finding, detail)) return true;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return true;  // bekleyis sirasinda durum degisti
    core::readiness::FixItem one;
    one.check_id = c.id;
    one.action_arg = c.action_arg;
    one.ops = std::move(ops);
    auto r = core::readiness::apply_fix_item(*rec_->fixes_engine, one, [this] { return new_fix_tx(); });
    if (!r.ok() && r.code() == contracts::ErrorCode::GateBlocked) set_readiness_notice("chk.notice.gate_blocked", true, c.id);
    if (r.ok() && r.value().state == contracts::TxState::Active && display) {
        // Goruntu gelmezse kullanici goremez: 15 sn icinde "Koru" yoksa eski ayara donulur. Bu sureli acil geri donus
        // oyun/algilama kapisina takilmaz; kullanici bozuk goruntude birakilmaz.
        if (!ui::confirm_display_keep(owner, cat(), 15)) {
            core::recovery::EngineOptions emergency;
            emergency.bypass_guard = true;
            (void)rec_->fixes_engine->rollback(std::move(r).value(), emergency);
        }
    }
    start_readiness_scan();  // gercek durumu yeniden oku
    return true;
}

core::recovery::JournalTx App::new_fix_tx() const {
    core::recovery::JournalTx tx;
    tx.id = platform::random_nonce();
    tx.actor_sid = startup_ctx_.actor_sid;
    tx.target_sid = startup_ctx_.target_sid;
    tx.created_unix_ms = now_unix_ms();
    return tx;
}

void App::run_fix_all() {
    // P12-B1: Panel "Duzelt". Uygulamanin kendisinin yazip geri okuyabildigi TUM bulgular tek Evet/Hayir'da uygulanir
    // (mevcut kayit / geri okuma / "Geri al" altyapisi; yeni yazma yolu yok). Kullanici kapsamli ayarlar bulgu basina bir
    // islem olarak "fixes" motorunda; yonetici ayarlari (HAGS, hizli baslatma) tek UAC ile broker'da. Sayfa degismez.
    using core::readiness::FixItem;
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    const auto& rv = vm_.readiness;
    if (rv.loading || rv.net_running || rv.mouse_running || rv.dns_running || rv.health_running || rv.fix_running || admin_busy_) return;
    const auto plan = core::readiness::plan_fix_all(rv.checks, rv.undo);
    if (plan.empty()) return;  // duzeltilecek bir sey yok: dugme zaten devre disi
    if (plan.user_count() > 0 && (!rec_ || !rec_->fixes_store || !rec_->fixes_engine)) return;
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;  // oyun calisiyor: neden karti gosterildi
    if (controller_) {
        const auto ph = controller_->phase();
        if (ph == Phase::Applying || ph == Phase::Restoring) {  // ust uste ikinci UAC / yazma yok
            set_readiness_notice("chk.notice.perf_busy", true, core::readiness::kFixAllNoticeId);
            return;
        }
    }
    std::vector<std::wstring> lines;
    for (const auto& it : plan.items) {
        const auto ck = std::find_if(rv.checks.begin(), rv.checks.end(), [&](const core::readiness::Check& x) { return x.id == it.check_id; });
        if (ck == rv.checks.end()) continue;
        auto line = ui::fix_change_line(cat(), *ck);
        if (line.empty() && window_) line = window_->check_text(*ck);  // uydurma eski/yeni deger yazilmaz: bulgu cumlesi
        lines.push_back(std::move(line));
    }
    // Zorunlu ayrinti ayni dizgi anahtarlariyla: UAC / yeniden baslatma / ekran kararmasi.
    std::wstring detail;
    if (plan.admin_count() > 0) detail = t(ac(plan.has_restart() ? StringId::ChkFixDetailAdmin : StringId::ChkFixDetailAdminNoReboot));
    else detail = t(plan.has_display() ? StringId::ChkFixDetailDisplay : StringId::ChkFixDetail);
    if (plan.has_display() && plan.admin_count() > 0) detail += L" " + t(StringId::ChkFixDetailDisplay);
    if (!ui::ask_fix_all(owner, cat(), lines, detail)) return;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;  // bekleyis sirasinda durum degisti

    int applied = 0, failed = 0;
    bool gate_blocked = false;
    std::vector<contracts::SettingOperation> admin_ops;
    for (const auto& it : plan.items)
        if (it.admin) admin_ops.insert(admin_ops.end(), it.ops.begin(), it.ops.end());
    // Ekran yenileme hizi en sona: "Koru / Geri al" sorusu diger ayarlar yazildiktan sonra gelir.
    for (const bool display_pass : {false, true}) {
        for (const FixItem& it : plan.items) {
            if (it.admin || it.display != display_pass || gate_blocked) continue;
            auto r = core::readiness::apply_fix_item(*rec_->fixes_engine, it, [this] { return new_fix_tx(); });
            if (!r.ok()) {
                if (r.code() == contracts::ErrorCode::GateBlocked) gate_blocked = true;
                ++failed;
                continue;
            }
            if (r.value().state != contracts::TxState::Active) {  // motor geri aldi / dogrulayamadi
                ++failed;
                continue;
            }
            if (it.display && !ui::confirm_display_keep(owner, cat(), 15)) {
                core::recovery::EngineOptions emergency;  // sureli acil geri donus oyun/algilama kapisina takilmaz
                emergency.bypass_guard = true;
                (void)rec_->fixes_engine->rollback(std::move(r).value(), emergency);
                continue;  // kullanici eski ayari sectigi icin duzeltildi sayilmaz, hata da degil
            }
            ++applied;
        }
    }
    if (gate_blocked) {
        set_readiness_notice("chk.notice.gate_blocked", true, core::readiness::kFixAllNoticeId);
        start_readiness_scan();
        return;
    }
    if (!admin_ops.empty()) {
        const bool ran = run_admin_job([this, admin_ops, applied, failed]() -> std::function<void()> {
            const auto b = admin_channel_.run(core::ipc::RequestAction::Fix, admin_ops);
            const std::string key(core::broker::admin_notice_key(core::ipc::RequestAction::Fix, b));
            const int a_ok = static_cast<int>(b.count(core::broker::StepState::Applied));
            const int a_failed = static_cast<int>(admin_ops.size()) - a_ok;
            const bool reboot = b.any_pending_reboot();
            return [this, key, applied, failed, a_ok, a_failed, reboot] {
                const auto n = core::readiness::fix_all_notice(applied + a_ok, failed + a_failed, reboot, key);
                set_readiness_notice(n.key, n.warning, core::readiness::kFixAllNoticeId, applied + a_ok, failed + a_failed);
            };
        });
        if (ran) return;  // finish_admin_job yeniden tarar ve gecmisi tazeler
    }
    const auto n = core::readiness::fix_all_notice(applied, failed, false, {});
    set_readiness_notice(n.key, n.warning, core::readiness::kFixAllNoticeId, applied, failed);
    start_readiness_scan();  // gercek durumu yeniden oku (+ "Geri al" satirlari)
    push_history();          // kullanici fixes kaydi Islem Gecmisi'nde
}

bool App::run_image_job(const core::readiness::Check& c, bool repair) {
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return true;
    if (admin_busy_) return true;
    if (!ui::ask_fix(owner, cat(), t(repair ? StringId::ChkMaintRepairQuestion : StringId::ChkMaintCheckQuestion),
                     t(ac(repair ? StringId::ChkMaintRepairDetail : StringId::ChkMaintCheckDetail))))
        return true;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return true;
    const contracts::SettingOperation op{contracts::OperationId::MaintenanceJob, contracts::OperationScope::Machine,
                                         repair ? core::maintenance::kImageRepairTarget : core::maintenance::kImageCheckTarget,
                                         core::maintenance::kMaintenanceValue, false, false};
    (void)run_admin_job([this, op, id = c.id, check = c, repair]() -> std::function<void()> {
        const auto b = admin_channel_.run(core::ipc::RequestAction::Maintenance, {op});
        std::string key(core::broker::maintenance_notice_key(b));
        const bool repairable = !repair && core::broker::maintenance_outcome_key(b) == "repairable";
        return [this, key = std::move(key), id, check, repairable] {
            set_readiness_notice(key, key != "chk.notice.maint_healthy" && key != "chk.notice.maint_repaired", id);
            // Kontrol onarilabilir dediyse onarim AYRI Evet/Hayir ister; kendiliginden baslamaz.
            if (repairable) post([this, check] { (void)run_image_job(check, true); });
        };
    });
    return true;
}

bool App::run_standby_purge_job(const core::readiness::Check& c) {
    // B5 / ADR-040 m.6: yalniz esik bulgusunun "Duzelt"i; zamanlayici ve arka plan temizligi yok. Mevcut T15 bakim isi:
    // sabit is kimligi (fix_plan.cpp), yukseltilmis broker, once/sonra OLCULEN bekleme boyutu.
    const auto op = core::readiness::maintenance_job_operation(c);
    if (!op) return false;
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return true;
    if (admin_busy_) return true;
    if (!ui::ask_fix(owner, cat(), t(StringId::ChkStandbyPurgeQuestion), t(ac(StringId::ChkStandbyPurgeDetail)))) return true;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return true;
    (void)run_admin_job([this, op = *op, id = c.id]() -> std::function<void()> {
        const auto b = admin_channel_.run(core::ipc::RequestAction::Maintenance, {op});
        const bool applied = b.elevation == core::ElevationOutcome::Started && !b.steps.empty() && !b.steps.front().unknown &&
                             b.steps.front().state == core::broker::StepState::Applied;
        std::string key;
        int before_mib = 0, after_mib = 0;
        if (applied) {
            const auto pr = core::maintenance::parse_purge_detail(b.steps.front().detail);
            if (pr.ok && pr.before_bytes && pr.after_bytes) {
                key = "chk.notice.standby_purged";  // yalniz her iki OLCUM da varsa sayi gosterilir
                before_mib = static_cast<int>(*pr.before_bytes / (1024ull * 1024ull));
                after_mib = static_cast<int>(*pr.after_bytes / (1024ull * 1024ull));
            } else {
                key = pr.ok ? "chk.notice.standby_unmeasured" : "chk.notice.standby_failed";
            }
        } else {
            key = std::string(core::broker::maintenance_notice_key(b));  // UAC red / oyun kapisi / yardimci hatasi
            if (key == "chk.notice.maint_failed") key = "chk.notice.standby_failed";
        }
        return [this, key = std::move(key), id, before_mib, after_mib] {
            // {n} = once, {m} = sonra (MiB; olculen bayt degerlerinin tam MiB'e bolumu)
            set_readiness_notice(key, key != "chk.notice.standby_purged" && key != "chk.notice.standby_unmeasured", id, before_mib, after_mib);
            start_readiness_scan();  // bulgu yeniden olculur: esik hala asiliyorsa satir kalir, degilse kalkar
        };
    });
    return true;
}

bool App::run_restore_point_job(const core::readiness::Check& c) {
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return true;
    if (admin_busy_) return true;
    if (!ui::ask_fix(owner, cat(), t(StringId::ChkRpQuestion), t(ac(StringId::ChkRpDetail)))) return true;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return true;  // UAC acilmaz
    (void)run_admin_job([this, id = c.id]() -> std::function<void()> {
        const auto b = admin_channel_.run(core::ipc::RequestAction::RestorePoint, {core::perf::restore_point_operation()});
        std::string key(core::broker::restore_point_notice_key(b));
        return [this, key = std::move(key), id] { set_readiness_notice(key, key != "chk.notice.rp_created", id); };
    });
    return true;
}

bool App::run_admin_fix(const core::readiness::Check& c) {
    // Yonetici duzeltmesi (HAGS / F-48 hizli baslatma): Evet/Hayir -> tek UAC -> broker makine "fixes" journal'i
    // (Performans Kapali geri almaz). UAC reddi/hata: hicbir sey yazilmaz; sonuc kullaniciya bildirilir (M2) ve
    // tarama gercek durumu yeniden okur.
    contracts::SettingOperation op;
    const bool fast_startup = c.action_arg == "fix.fast_startup";
    const bool dns = c.action_arg == "fix.dns";
    std::string dns_tgt;
    if (c.action_arg == "fix.hags") op = core::perf::hags_on_operation();
    else if (fast_startup) op = core::perf::fast_startup_off_operation();
    else if (dns) {
        dns_tgt = core::readiness::fix_target_for_check(c.id);  // satirdaki arayuz GUID'i
        if (dns_tgt.empty()) return true;
    } else return false;
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return true;  // oyun calisiyor: neden karti gosterildi
    if (admin_busy_) return true;  // onceki yonetici islemi suruyor (dugmeler zaten gizli)
    // Spec 2026-11-12 B3: "Uygulansin mi?" + degisiklik ("eski → yeni") + "Geri alinabilir." + mevcut ayrinti metni.
    const std::wstring finding = window_ ? window_->check_text(c) : std::wstring();
    if (dns) {
        // EF-X14: kullanici derlenmis listeden birini secer (Evet secimsiz kapali). Hayir/Esc: hicbir sey degismez.
        const auto pre = c.args.find("preselect");  // T10: karsilastirma sonucu satirinda onerilen hazir liste onceden secili gelir
        const auto pick = ui::ask_dns_preset(owner, cat(), finding, platform::to_wide(cat().get(ac(core::i18n::StringId::ChkFixDetailDns))),
                                             pre != c.args.end() ? std::string_view(pre->second) : std::string_view());
        if (!pick) return true;
        auto built = core::adapters::windows::dns_preset_operation(dns_tgt, *pick);
        if (!built) return true;
        op = std::move(*built);
    } else {
        const auto detail = platform::to_wide(
            cat().get(ac(fast_startup ? core::i18n::StringId::ChkFixDetailAdminNoReboot : core::i18n::StringId::ChkFixDetailAdmin)));
        if (!ui::ask_fix_apply(owner, cat(), ui::fix_change_line(cat(), c), finding, detail)) return true;  // Hayir: hicbir sey degismez
    }
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return true;  // UAC acilmaz
    // HAGS etkin durumu yeniden baslatmaya kadar degismez; hizli baslatma bir sonraki kapanistan itibaren gecerlidir.
    run_admin_async(core::ipc::RequestAction::Fix, {op}, c.id);
    return true;
}

void App::set_readiness_notice(std::string key, bool warning, std::string check_id, int n, int failed) {
    vm_.readiness.notice_key = std::move(key);
    vm_.readiness.notice_warning = warning;
    vm_.readiness.notice_check_id = std::move(check_id);
    vm_.readiness.notice_n = n;
    vm_.readiness.notice_failed = failed;
    if (!window_) return;
    window_->set_view(vm_);
    // RDY-08: sonuc yalniz gorsel degil; ekran okuyucu odagi tasimadan duyurur (UIA Notification).
    if (const auto id = core::i18n::id_of(vm_.readiness.notice_key)) {
        auto text = t(*id);
        for (const auto& [k, v] : {std::pair<std::wstring_view, int>{L"{n}", n}, {L"{m}", failed}})
            for (auto p = text.find(k); p != std::wstring::npos; p = text.find(k, p)) text.replace(p, k.size(), std::to_wstring(v));
        (void)window_->announce(text, true);
    }
}

bool App::run_admin_job(std::function<std::function<void()>()> job) {
    if (admin_busy_) {  // ayni anda tek yonetici/geri alma isi; kullanicinin Evet'i sessizce kaybolmaz
        set_readiness_notice("chk.notice.perf_busy", true);
        return false;
    }
    if (controller_) {
        // Performans uygulamasi/geri yuklemesi kendi broker'ini (UAC) baslatabilir: ust uste ikinci UAC yok.
        const auto ph = controller_->phase();
        if (ph == Phase::Applying || ph == Phase::Restoring) {
            set_readiness_notice("chk.notice.perf_busy", true);
            return false;
        }
    }
    if (admin_worker_.joinable()) admin_worker_.join();
    admin_busy_ = true;
    vm_.readiness.fix_running = true;  // "Yonetici islemi suruyor"; satir dugmeleri gizlenir
    vm_.readiness.notice_key.clear();
    vm_.readiness.notice_check_id.clear();
    if (window_) window_->set_view(vm_);
    admin_worker_ = spawn_worker([this, job = std::move(job)] {
        // UAC istemi, broker baslangici, pipe beklemeleri veya kullanici kapsamli geri alma burada; UI akar (L6).
        auto on_ui = job();
        post([this, on_ui = std::move(on_ui)] {
            admin_busy_ = false;
            vm_.readiness.fix_running = false;
            if (on_ui) on_ui();
            finish_admin_job();
        });
    });
    return true;
}

void App::finish_admin_job() {
    refresh_admin_path();  // hizmet denemesi dusmus olabilir (service_fell_back) veya hizmet durumu degismis olabilir
    if (rec_) (void)rec_->refresh_machine_scopes();  // broker makine journal dizinlerini simdi olusturmus olabilir
    start_readiness_scan();  // gercek durumu yeniden oku (+ "Geri al" satirlari)
    push_history();          // makine fixes kaydi Islem Gecmisi'nde
    if (perf_auto_restore_deferred_) {
        perf_auto_restore_deferred_ = false;
        perf_auto_restore();  // oyun calisiyorsa yine bekler (oyun kapaninca)
    }
    check_pending_exit();  // yonetici islemi sirasinda istenen cikis geri yuklemesi
    run_pending_recovery();
}

bool App::run_admin_async(core::ipc::RequestAction action, std::vector<contracts::SettingOperation> ops, std::string check_id) {
    return run_admin_job([this, action, ops = std::move(ops), check_id = std::move(check_id)]() -> std::function<void()> {
        const auto b = admin_channel_.run(action, ops);
        return [this, action, b, check_id, ops] {
            if (action == core::ipc::RequestAction::Recover && b.count(core::broker::StepState::GateBlocked)) {
                // UAC sonrasi oyun acildi: kurtarma kaybolmaz; sonraki taze guvenilir tespitte yeniden denenir.
                pending_recovery_ops_ = ops;
                recovery_retry_at_ms_ = mono_ms() + 30000;  // arka uc az once reddetti: hemen yeni UAC acilmaz
                detection_.owe_recovery();
            }
            const std::string key(core::broker::admin_notice_key(action, b));
            set_readiness_notice(key, core::broker::admin_notice_warning(key), check_id);
            if (action == core::ipc::RequestAction::Recover) {
                // Kurtarma sonrasi Performans durumu journal'dan yeniden okunur (yalniz bosta iken).
                const auto ph = controller_->phase();
                if (ph == Phase::Off || ph == Phase::On || ph == Phase::RecoveryNeeded) controller_->init();
                if (const auto id = core::i18n::id_of(key); !window_ && tray_.added() && id) tray_.notify(t(StringId::AppTitle), t(*id));
            }
        };
    });
}

core::readiness::FixTargets App::fix_targets() {
    core::readiness::FixTargets out;
    // Etkin / dogrulanamamis geri alma / yarim kalmis / dis degisiklik ayri tutulur (satir metni durust olsun).
    if (rec_ && rec_->fixes_store)
        core::broker::journal_fix_targets(rec_->fixes_store->scan(), out.active, out.recovery, out.interrupted, out.conflict);
    // Makine fixes journal'i (salt okuma; UAC yok)
    admin_channel_.fix_target_lists(out.active, out.recovery, out.interrupted, out.conflict);
    return out;
}

void App::on_readiness_undo(std::size_t k) {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (k >= vm_.readiness.checks.size()) return;
    const auto c = vm_.readiness.checks[k];
    const auto target = core::readiness::fix_target_for_check(c.id);
    if (target.empty()) return;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;  // oyun calisiyor: neden karti gosterildi
    if (admin_busy_) return;
    const bool admin = core::perf::admin_fix_target(target);
    const bool no_reboot = target == core::perf::fast_startup_off_operation().target;
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring question = t(StringId::ChkUndoQuestion);
    if (const auto p = question.find(L"{finding}"); p != std::wstring::npos)
        question.replace(p, 9, window_ ? window_->check_text(c) : std::wstring());
    const bool dns_target = core::adapters::windows::parse_dns_target(target).has_value();
    const auto detail = t(ac(dns_target ? StringId::ChkUndoDetailDns
                                        : admin ? (no_reboot ? StringId::ChkUndoDetailAdminNoReboot : StringId::ChkUndoDetailAdmin) : StringId::ChkUndoDetail));
    if (!ui::ask_fix(owner, cat(), question, detail)) return;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;
    if (admin) {
        // HAGS: makine fixes journal'indaki etkin VEYA geri alinmasi dogrulanamamis (RecoveryRequired / yarim kalmis)
        // kayit broker'da birebir geri alinir / yeniden denenir (tek UAC; broker once oturum uzlastirmasi yapar).
        auto ops = admin_channel_.fix_recovery_ops();
        for (auto& op : admin_channel_.fix_active_ops()) ops.push_back(std::move(op));
        for (const auto& op : ops)
            if (op.target == target) {
                run_admin_async(core::ipc::RequestAction::UndoFix, {op}, c.id);
                return;
            }
        set_readiness_notice("chk.notice.nothing", true, c.id);  // RDY-06: beklenen islem olmadi -> uyari
        start_readiness_scan();
        return;
    }
    // Kullanici kapsami: hedefin geri alinmamis TUM kayitlari (Active, RecoveryRequired, yarim kalmis) en yeniden
    // eskiye motorla geri alinir (gercek hedef okunur; dis degisiklik ezilmez; geri okumayla dogrulanir). Yonetici
    // isiyle ayni arka plan is parcaciginda (UI thread'i beklemez; ayni anda tek is).
    if (!rec_ || !rec_->fixes_store || !rec_->fixes_engine) return;
    auto* store = rec_->fixes_store.get();
    auto* engine = rec_->fixes_engine.get();
    // F-73: cok adimli duzeltmede (arka plan kaydi) bulgunun TUM hedefleri birlikte geri alinir.
    auto targets = core::readiness::fix_targets_for_check(c);
    (void)run_admin_job([store, engine, target, targets = std::move(targets), this, check_id = c.id]() -> std::function<void()> {
        core::broker::BatchResult b;
        b.elevation = core::ElevationOutcome::Started;  // kullanici kapsami: UAC yok
        b.steps.push_back(core::broker::restore_matching(
            store, engine,
            [&targets](const contracts::SettingOperation& o) {
                return std::any_of(targets.begin(), targets.end(),
                                   [&o](const std::string& t) { return core::readiness::fix_target_matches(t, o.target); });
            },
            target));
        const std::string key(core::broker::admin_notice_key(core::ipc::RequestAction::UndoFix, b));
        return [this, key, check_id] { set_readiness_notice(key, core::broker::admin_notice_warning(key), check_id); };
    });
}

void App::on_readiness_keep(std::size_t k) {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (k >= vm_.readiness.checks.size()) return;
    const auto c = vm_.readiness.checks[k];
    const auto target = core::readiness::fix_target_for_check(c.id);
    if (target.empty()) return;
    // Ayar yazilmaz; ama yonetici yolu broker baslatir (oturum basi uzlastirma yarim kayitlari geri alabilir):
    // oyun calisirken baslatilmaz.
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;
    if (admin_busy_) return;
    const bool admin = core::perf::admin_fix_target(target);
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring question = t(StringId::ChkKeepQuestion);
    if (const auto p = question.find(L"{finding}"); p != std::wstring::npos)
        question.replace(p, 9, window_ ? window_->check_text(c) : std::wstring());
    const auto detail = t(ac(admin ? StringId::ChkKeepDetailAdmin : StringId::ChkKeepDetail));
    // RDY-05: geri donussuz islem -> uyari ikonu (varsayilan dugme yine Hayir)
    if (!ui::ask_fix(owner, cat(), question, detail, ui::FixDialogIcon::Warning)) return;  // Hayir: kayit oldugu gibi kalir
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;
    if (admin) {
        // Makine fixes journal'i korumali: kayit yalniz broker'da kapatilir (izin listesindeki KeepCurrent; yazma yok).
        for (const auto& op : admin_channel_.fix_conflict_ops())
            if (op.target == target) {
                (void)run_admin_async(core::ipc::RequestAction::KeepCurrent, {op}, c.id);
                return;
            }
        set_readiness_notice("chk.notice.keep_nothing", true, c.id);  // RDY-06: Koru'ya ozel metin, uyari
        start_readiness_scan();
        return;
    }
    if (!rec_ || !rec_->fixes_store) return;
    auto* store = rec_->fixes_store.get();
    auto targets = core::readiness::fix_targets_for_check(c);
    (void)run_admin_job([store, target, targets = std::move(targets), this, check_id = c.id]() -> std::function<void()> {
        core::broker::BatchResult b;
        b.elevation = core::ElevationOutcome::Started;  // kullanici kapsami: UAC yok
        b.steps.push_back(core::broker::keep_matching(
            store,
            [&targets](const contracts::SettingOperation& o) {
                return std::any_of(targets.begin(), targets.end(),
                                   [&o](const std::string& t) { return core::readiness::fix_target_matches(t, o.target); });
            },
            target));
        const std::string key(core::broker::admin_notice_key(core::ipc::RequestAction::KeepCurrent, b));
        return [this, key, check_id] { set_readiness_notice(key, core::broker::admin_notice_warning(key), check_id); };
    });
}

bool App::can_ask() const { return window_ && IsWindowVisible(window_->hwnd()) && !IsIconic(window_->hwnd()); }

void App::offer_machine_recovery(std::vector<contracts::SettingOperation> ops) {
    // M2: yarim kalmis makine kaydi sessizce birakilmaz ve sessizce yukseltilmez: Evet/Hayir, sonra tek UAC.
    if (ops.empty() || smoke_) return;
    if (!gate(core::games::GuardedAction::SettingMutation).allowed) {
        // Ilk tespit henuz yok veya oyun calisiyor: soru kaybolmaz, ilk taze guvenilir "oyun yok" sonucunda sorulur.
        deferred_machine_offer_ = std::move(ops);
        detection_.owe_recovery();
        return;
    }
    if (!can_ask()) {
        // Spec 2026-11-12 E: soru uygulama ici kip penceresidir; ana pencere yokken (tepside) / simge durumundayken
        // gosterilemez. Soru kaybolmaz: pencere gorunur olunca sorulur (open_window'daki gorunurluk abonesi).
        deferred_machine_offer_ = std::move(ops);
        return;
    }
    const HWND owner = window_ ? window_->hwnd() : host_;
    if (!ui::ask_fix(owner, cat(), t(StringId::RecMachineQuestion), t(ac(StringId::RecMachineDetail)))) return;  // Hayir: sonraki acilista yine sorulur
    // Evet reddedilmez: acilista otomatik Performans geri almasi (Restoring) veya baska yonetici isi suruyorsa
    // kurtarma bekletilir ve o bitince kendiliginden baslar (perf_busy bildirimiyle kaybolmaz).
    pending_recovery_ops_ = std::move(ops);
    run_pending_recovery();
}

void App::run_pending_recovery() {
    run_deferred_startup_recovery();  // acilista oyun/algilama nedeniyle ertelenen yarim islem kurtarmasi
    // Kapi herhangi bir nedenle kapaliysa bekleyen is sonraki taze guvenilir sonuca borclanir; kaybolmaz.
    if ((startup_recovery_deferred_ || !pending_recovery_ops_.empty()) && !gate(core::games::GuardedAction::SettingMutation).allowed)
        detection_.owe_recovery();
    if (pending_recovery_ops_.empty()) return;
    if (mono_ms() < recovery_retry_at_ms_) {
        detection_.owe_recovery();
        return;
    }
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20: silme bitmeden yazma yok
    const auto ph = controller_ ? controller_->phase() : Phase::Off;
    const bool perf_busy = ph == Phase::Applying || ph == Phase::Restoring;
    const bool game = !gate(core::games::GuardedAction::SettingMutation).allowed;
    // Bekleme: oyun kapaninca (install_watch_state), yonetici isi bitince (finish_admin_job), Performans bitince (on_change).
    if (core::perf::machine_recovery_when(game, admin_busy_, perf_busy) != core::perf::DeferredRun::Now) return;
    auto ops = std::move(pending_recovery_ops_);
    pending_recovery_ops_.clear();
    if (!run_admin_async(core::ipc::RequestAction::Recover, ops)) pending_recovery_ops_ = std::move(ops);  // sonra yeniden
}

void App::start_network_test() {
    if (vm_.readiness.loading || vm_.readiness.net_running || vm_.readiness.mouse_running || vm_.readiness.dns_running || vm_.readiness.health_running) return;
    if (readiness_worker_.joinable()) readiness_worker_.join();
    vm_.readiness.net_running = true;
    if (window_) window_->set_view(vm_);
    readiness_worker_ = spawn_worker([this] {
        auto pings = run_network_test(readiness_cancel_);  // kullanici istedi: yalniz ICMP yanki
        post([this, pings = std::move(pings)]() mutable {
            last_pings_ = std::move(pings);
            vm_.readiness.net_running = false;
            start_readiness_scan();  // bulgular olcumle yeniden kurulur
        });
    });
}

void App::refresh_mouse_check() {
    for (auto& c : vm_.readiness.checks)
        if (c.id == "latency.mouse_polling") {
            c = core::readiness::mouse_polling_check(vm_.readiness.mouse_running, mouse_result_);
            break;
        }
    if (window_) window_->set_view(vm_);
}

void App::refresh_dns_bench_rows() {
    auto& ch = vm_.readiness.checks;
    ch.erase(std::remove_if(ch.begin(), ch.end(), [](const core::readiness::Check& c) { return c.id.rfind("network.dns_bench", 0) == 0; }), ch.end());
    for (auto& c : core::readiness::dns_bench_checks(vm_.readiness.dns_running, dns_report_, ch)) ch.push_back(std::move(c));
    core::readiness::sort_checks(ch);
    vm_.readiness.undo.assign(ch.size(), core::readiness::UndoState::None);
    if (!admin_busy_) vm_.readiness.undo = core::readiness::undo_states(ch, fix_targets());
    if (window_) window_->set_view(vm_);
}

void App::start_dns_bench() {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    if (vm_.readiness.loading || vm_.readiness.net_running || vm_.readiness.mouse_running || vm_.readiness.dns_running || vm_.readiness.health_running || vm_.readiness.fix_running) return;
    if (vm_.measure.endurance.running || vm_.speed.running || core::measure::busy(vm_.measure)) return;  // tek aktif test
    if (!verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // oyun calisiyor/belirsiz: olcum yok
    if (readiness_worker_.joinable()) readiness_worker_.join();
    vm_.readiness.dns_running = true;
    dns_cancel_ = false;
    refresh_dns_bench_rows();  // satir "olculuyor" der; dugme gizlenir
    readiness_worker_ = spawn_worker([this] {
        // Yalniz DNS sorgusu (salt okuma): hicbir ayar yazilmaz. Oyun acilirsa / uygulama kapanirsa iptal edilir.
        auto report = run_dns_compare(dns_cancel_);
        post([this, report = std::move(report)]() mutable {
            if (report) dns_report_ = std::move(report);  // iptal/olcum yapilamadi: onceki sonuc korunur
            vm_.readiness.dns_running = false;
            refresh_dns_bench_rows();
        });
    });
}

void App::refresh_health_rows() {
    auto& ch = vm_.readiness.checks;
    ch.erase(std::remove_if(ch.begin(), ch.end(), [](const core::readiness::Check& c) { return c.id.rfind("hardware.health_test", 0) == 0; }), ch.end());
    for (auto& c : core::diagnostics::health_test_checks(vm_.readiness.health_running, health_phase_, health_sec_, health_report_)) ch.push_back(std::move(c));
    core::readiness::sort_checks(ch);
    vm_.readiness.undo.assign(ch.size(), core::readiness::UndoState::None);
    if (!admin_busy_) vm_.readiness.undo = core::readiness::undo_states(ch, fix_targets());
    if (window_) window_->set_view(vm_);
}

void App::start_health_test() {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    if (vm_.readiness.loading || vm_.readiness.net_running || vm_.readiness.mouse_running || vm_.readiness.dns_running || vm_.readiness.health_running || vm_.readiness.fix_running) return;
    if (vm_.measure.endurance.running || vm_.speed.running || core::measure::busy(vm_.measure)) return;  // tek aktif test
    if (!verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // oyun calisiyor/belirsiz: test yok
    if (readiness_worker_.joinable()) readiness_worker_.join();
    vm_.readiness.health_running = true;
    health_cancel_ = false;
    health_phase_ = 1;
    health_sec_ = 0;
    refresh_health_rows();  // satir "suruyor" der; dugme gizlenir
    readiness_worker_ = spawn_worker([this] {
        // Yalniz CPU yuku + bellek deseni; hicbir ayar yazilmaz. Oyun acilirsa / uygulama kapanirsa iptal edilir.
        auto report = run_health_test(health_cancel_, [this](int phase, int sec) {
            post([this, phase, sec] {
                if (!vm_.readiness.health_running) return;
                health_phase_ = phase;
                health_sec_ = sec;
                refresh_health_rows();
            });
        });
        post([this, report = std::move(report)]() mutable {
            if (report) health_report_ = std::move(report);  // iptal: onceki sonuc korunur
            vm_.readiness.health_running = false;
            refresh_health_rows();
        });
    });
}

void App::start_mouse_measure() {
    using core::measure::PollingResult;
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;
    if (vm_.readiness.loading || vm_.readiness.net_running || vm_.readiness.mouse_running || vm_.readiness.dns_running || vm_.readiness.health_running || vm_.readiness.fix_running) return;
    if (vm_.measure.endurance.running || vm_.speed.running || core::measure::busy(vm_.measure)) return;  // tek aktif test
    if (!verify_or_block(core::games::GuardedAction::HeavyBenchmark)) return;  // oyun calisiyor/belirsiz: olcum yok
    if (readiness_worker_.joinable()) readiness_worker_.join();
    vm_.readiness.mouse_running = true;
    refresh_mouse_check();  // satir "Fareyi 5 saniye ..." der; dugme gizlenir
    readiness_worker_ = spawn_worker([this] {
        // Yalniz Raw Input zaman damgalari (salt okuma); fare icerigi saklanmaz, ayar yazilmaz.
        const auto cap = platform::metrics::capture_raw_mouse(5000, readiness_cancel_);
        PollingResult r;  // Insufficient: pencere kurulamadi / iptal / yeterince hareket yok
        if (cap) r = core::measure::estimate_polling_rate(cap->qpc_ticks, cap->qpc_freq);
        post([this, r] {
            mouse_result_ = r;
            vm_.readiness.mouse_running = false;
            refresh_mouse_check();
        });
    });
}

void App::on_readiness_fix(std::size_t k) {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (k >= vm_.readiness.checks.size()) return;
    const auto c = vm_.readiness.checks[k];  // kopya: onay penceresi acikken tarama listeyi degistirebilir
    using core::readiness::ActionKind;
    if (c.action == ActionKind::RunNetworkTest) {
        start_network_test();
        return;
    }
    if (c.action == ActionKind::MeasureMouse) {
        start_mouse_measure();
        return;
    }
    if (c.action == ActionKind::RunDnsBench) {
        start_dns_bench();
        return;
    }
    if (c.action == ActionKind::RunHealthTest) {
        start_health_test();
        return;
    }
    if (c.action == ActionKind::IncidentWindow) {
        // EF-X01: yalniz uygulamanin preferences.json tercihi + salt okuma yeniden tarama; sistem ayari yazilmaz.
        if (vm_.readiness.loading || vm_.readiness.net_running || vm_.readiness.mouse_running || vm_.readiness.dns_running || vm_.readiness.health_running || vm_.readiness.fix_running) return;
        prefs_.incident_days = c.action_arg == "30" ? core::diagnostics::kLongDays : core::diagnostics::kDefaultDays;
        save_prefs();
        start_readiness_scan();
        return;
    }
    // P12-B1: "Duzelt" yalniz gercekten duzeltir. Windows ayar sayfasi / web sayfasi acip birakan yonlendirmeler (OpenSettings,
    // OpenUrl) ve uygulamanin yazamadigi AutoFix (fix.power) artik dugme degildir; sayfa acilmaz, hicbir sey yazilmaz.
    if (c.action == ActionKind::AutoFix) (void)run_auto_fix(c);  // yapildi / Hayir / yazilamaz: sayfa acilmaz
}

core::games::GateDecision App::gate(core::games::GuardedAction a) const {
    auto c = detection_.context();
    if (a == core::games::GuardedAction::LiveCapture && active_game_) {
        // Canli olcum kimligi: izleyicinin algiladigi oyun + kurulu surum (bilinmiyorsa bos -> kapali).
        // Koruma surumu/toplayici/kapsam gozlenmediginde bos kalir; kapi kanit eksigini reddeder.
        c.capture_game_id = active_game_->game_id;
        c.capture_game_build = active_game_->build;
        c.capture_collector_commit = capture_collector_commit_;
        c.capture_os_driver_scope = capture_os_driver_scope_;
    }
    return core::games::check(a, c);
}

bool App::gate_or_block(core::games::GuardedAction a) {
    if (update_holds_writes()) {  // guncelleme kontrolu suruyor / zorunlu guncelleme bekliyor: serit nedeni gosterir
        push_update();
        return false;
    }
    const auto d = gate(a);
    if (!d.allowed) push_gate();
    return d.allowed;
}

bool App::verify_or_block(core::games::GuardedAction a) {
    if (update_holds_writes()) {
        push_update();
        return false;
    }
    if (const auto d = gate(a); !d.allowed) {  // bilinen engel: canli sorguya gerek yok
        push_gate();
        return false;
    }
    const auto d = permit_.verify(a);
    if (d.allowed) return true;
    action_denied_key_ = d.reason_key;
    {
        std::lock_guard lk(watch_mu_);
        watch_force_ = true;  // izleyici de gercegi hemen gorsun
    }
    watch_cv_.notify_all();
    push_gate();
    return false;
}

void App::push_gate() {
    using core::games::BlockCause;
    const auto cause = detection_.cause();
    const auto& running = detection_.running();
    vm_.game_running = cause == BlockCause::GameRunning;
    // R-C2: oyun gercekten kapandi (tespit hatasi/eskime degil: sebep Yok). Salt okuma tarama baslatilir; silme yok.
    if (prev_game_running_ && !vm_.game_running && cause == BlockCause::None) on_game_exited();
    prev_game_running_ = vm_.game_running;
    vm_.library_untrusted = cause == BlockCause::LibraryError;
    vm_.game_detection = detection_.detection();
    vm_.game_gate_blocked = cause != BlockCause::None || !action_denied_key_.empty();
    measure_->set_speed_gate_blocked(vm_.game_gate_blocked);  // Task 12: suren hiz testi oyun acilinca durur
    // Spec H: oyun acilinca suren surucu aramasi durur ("Denetim durduruldu"); sayfa ilk acildiginda kapi kapaliysa
    // bekleyen ilk denetim kapi acilinca baslar.
    if (vm_.game_gate_blocked && vm_.readiness.dns_running) dns_cancel_ = true;  // T10: oyun acilinca DNS olcumu durur
    if (vm_.game_gate_blocked && vm_.readiness.health_running) health_cancel_ = true;  // T16: oyun acilinca saglik testi durur
    if (vm_.game_gate_blocked && driver_scanner_ && driver_scanner_->running()) driver_scanner_->cancel();
    if (!vm_.game_gate_blocked && updates_auto_pending_ && vm_.updates.scan.state == core::drivers::ScanState::NeverChecked) start_driver_scan();
    if (cause == BlockCause::None && !action_denied_key_.empty()) {
        vm_.game_block_reason_key = action_denied_key_;  // son islem sinirinda reddedildi; tarama henuz yetismedi
        vm_.game_block_title_key = "gate.action_blocked_title";
    } else {
        vm_.game_block_reason_key = std::string(core::games::reason_key(cause));
        vm_.game_block_title_key = std::string(core::games::title_key(cause));
    }
    vm_.game_block_can_retry = cause == BlockCause::LibraryError || cause == BlockCause::ProcessIdentity ||
                               cause == BlockCause::ProcessListFailed || cause == BlockCause::Stale;
    vm_.library_issues.clear();
    if (cause == BlockCause::LibraryError)
        for (const auto& is : detection_.library_issues()) vm_.library_issues.push_back({std::string(core::games::issue_key(is.kind)), is.path});
    const auto ctx = detection_.context();
    for (auto& r : vm_.games) {
        r.running = std::find(running.begin(), running.end(), r.dir_key) != running.end();
        // Canli olcum nedeni tespit durumuna gore tazelenir (ilk tarama/hatasi dahil).
        if (const auto gi = game_info_.find(r.dir_key); gi != game_info_.end()) {
            core::games::GateContext gc;
            gc.detection_ok = ctx.detection_ok;
            gc.detection_reason_key = ctx.detection_reason_key;
            gc.detection_age_ms = ctx.detection_age_ms;
            gc.capture_game_id = gi->second.app_id;
            gc.capture_game_build = gi->second.build;
            gc.capture_collector_commit = capture_collector_commit_;
            gc.capture_os_driver_scope = capture_os_driver_scope_;
            r.capture_key = core::games::check(core::games::GuardedAction::LiveCapture, gc).reason_key;
        }
        // F-43: süreç riskleri her çağrıda tazelenir; duran oyundan eski uyarı ve tekrar kalmaz.
        std::erase_if(r.risk_keys, [](const std::string& k) { return k.rfind("risk.process.", 0) == 0; });
    }
    if (active_game_) {
        // F-43: Realtime/High öncelik ve elle daraltılmış affinity yalnız çalışan oyun sürecinde okunur.
        const auto st = platform::adapters::read_process_state(active_game_->pid);
        for (auto& r : vm_.games) {
            if (r.dir_key != active_game_->dir_key || !r.running) continue;
            for (const auto& risk : core::games::scan_process_state(st)) r.risk_keys.push_back(risk.code);
        }
    }
    vm_.measure.game_running = vm_.game_running;
    vm_.measure.gate_blocked = vm_.game_gate_blocked;
    vm_.measure.gate_reason_key = vm_.game_block_reason_key;
    // Oyunda Test kaynagi: gercek izleyici + ADR-021 uyumluluk kapisi. Oyun yoksa ya da kapi kapaliysa
    // source disabled ve nedeni yerellestirilmis metinle gosterilir (sessiz yok sayma/override yok).
    std::vector<core::games::RunningGameCapture> running_caps;
    if (active_game_)
        running_caps.push_back({active_game_->game_id, active_game_->build, std::string(), capture_collector_commit_,
                                capture_os_driver_scope_});
    const auto av = core::games::capture_source(running_caps);
    vm_.measure.ingame_available = av.allowed;
    vm_.measure.ingame_reason_key = av.allowed ? std::string() : av.reason_key;
    if (window_) window_->set_view(vm_);
}

}  // namespace ef::app
