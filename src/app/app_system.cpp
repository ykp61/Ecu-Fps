// Baslangic, temizlik, tercihler, gecmis kaydi, ayarlar, destek paketi, kaldirma. (denetim 4.4: app.cpp'den ayrildi; App uye islevleri).
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
#include <shlobj.h>
#include <thread>
#include <tlhelp32.h>
#include <ctime>
#include "core/diagnosis/calibration.h"  // DIAG-11: uretim yayin kapisi durumu
#include "core/cleanup/cleanup_hint.h"  // R-C2: oyun kapanisi oneri karar mantigi
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
#include "core/support/diag.h"  // Uyumluluk B8: destek paketi diag.json
#include "platform/windows/inventory/diag_collect.h"
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

void App::push_startup() {
    if (!startup_) return;
    ui::ViewModel::StartupView v;
    v.phase = startup_->phase();
    const auto& rows = startup_->rows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        ui::ViewModel::StartupRowView rv;
        rv.name = r.entry.name;
        rv.reason_key = r.rec.reason_key;
        rv.advice = r.rec.advice;
        rv.enabled_now = r.entry.approval == core::adapters::startup::Approval::Enabled;
        rv.desired = startup_->desired(i);
        rv.editable = startup_->editable(i);
        rv.admin = core::adapters::startup::requires_admin(r.entry.location);
        rv.ours = r.ours;
        v.rows.push_back(std::move(rv));
    }
    // WIN-10: secim kalici tercihten gelir; anahtar yalniz var olan + calisan hizmette duzenlenebilir.
    std::vector<std::string> selected;
    {
        std::lock_guard<std::mutex> lk(service_sel_mu_);
        selected = session_stop_services_;
    }
    for (const auto& s : startup_->services()) {
        ui::ViewModel::ServiceRowView rv;
        rv.name = s.name;
        rv.display_name = s.display_name;
        rv.breaks_key = s.breaks_key;
        rv.present = s.present;
        rv.running = s.running;
        rv.selected = std::find(selected.begin(), selected.end(), s.name) != selected.end();
        rv.editable = s.present && s.running;
        v.services.push_back(std::move(rv));
    }
    v.pending = startup_->pending();
    v.last = startup_->last();
    vm_.startup = v;
    if (window_) window_->set_startup(v);
}

void App::on_cleanup_select(core::cleanup::CleanupCategory c, bool on) {
    if (!cleanup_ || cleanup_admin_pending_) return;
    const auto* cp = cleanup_->plan().find(c);
    const bool needs_elevation =
        on && core::cleanup::info(c).needs_admin && cp && cp->preview.status == core::cleanup::PreviewStatus::NeedsAdmin &&
        !cleanup_->admin_previewed(c);
    if (needs_elevation) {
        start_admin_cleanup_scan(c);  // UAC yalniz kullanici bu kategoriyi actiginda istenir
        return;
    }
    cleanup_->set_selected(c, on);
}

void App::start_admin_cleanup_scan(core::cleanup::CleanupCategory c) {
    if (!cleanup_ || cleanup_admin_pending_) return;
    close_admin_cleanup();  // onceki yardimci plani yeni taramayla kullanilmaz
    cleanup_admin_notice_.clear();
    cleanup_admin_pending_ = true;
    push_cleanup();
    if (cleanup_admin_worker_.joinable()) cleanup_admin_worker_.join();
    const std::wstring volume = cleanup_->volume();
    cleanup_admin_worker_ = spawn_worker([this, volume, c] {
        // CleanupAdminLaunch unique_ptr tasir (kopyalanamaz): post hedefi kopyalanabilir olmali -> shared_ptr.
        auto res = std::make_shared<platform::CleanupAdminLaunch>(platform::launch_cleanup_admin(volume));
        post([this, res, c]() {
            cleanup_admin_pending_ = false;
            switch (res->status) {
                case platform::CleanupAdminStatus::Ok:
                    cleanup_admin_ = std::shared_ptr<platform::CleanupAdminClient>(std::move(res->client));
                    cleanup_->adopt_admin_previews(res->previews);
                    if (cleanup_->selectable(c)) cleanup_->set_selected(c, true);
                    break;
                case platform::CleanupAdminStatus::UacDenied: cleanup_admin_notice_ = "cleanup.admin_denied"; break;
                case platform::CleanupAdminStatus::Error: cleanup_admin_notice_ = "cleanup.admin_failed"; break;
            }
            push_cleanup();
        });
    });
}

void App::close_admin_cleanup() {
    if (cleanup_admin_) {
        cleanup_admin_->close();
        cleanup_admin_.reset();
    }
}

bool App::claim_standby_job() {
    // T15: bellek bosaltma secildiyse broker UAC'i bu calismaya aittir; baska yonetici isi suruyorsa baslamaz.
    if (!cleanup_ || cleanup_->phase() != core::cleanup::SessionPhase::Confirming ||
        !cleanup_->selected(core::cleanup::CleanupCategory::StandbyMemory))
        return true;
    if (admin_busy_) return false;
    admin_busy_ = true;
    cleanup_standby_job_ = true;
    return true;
}

// R-C2 (ADR-021/ADR-016 uyumlu): oyun ACIKKEN temizlik yok; oyun kapaninca yalniz salt okuma tarama ve bildirim.
// Hicbir dosya kendiliginden silinmez (silme yalniz Temizlik sayfasinda Evet ile).
// Karar mantigi saf islevlerde: core/cleanup/cleanup_hint.h (birim testli).
void App::on_game_exited() {
    core::cleanup::HintScanInput in;
    in.enabled = prefs_.cleanup_hint_after_game;
    in.session_present = cleanup_ != nullptr;
    in.hint_pending = cleanup_hint_pending_;
    in.uninstall_running = uninstall_phase_ == ui::SettingsView::UninstallPhase::Running;
    in.session_idle = cleanup_ && cleanup_->phase() == core::cleanup::SessionPhase::Idle;  // kullanici Temizlik'teyse onun durumuna dokunulmaz
    in.last_hint_ms = cleanup_hint_last_ms_;
    in.now_ms = mono_ms();
    if (!core::cleanup::should_start_hint_scan(in)) return;
    cleanup_hint_pending_ = true;
    cleanup_->scan();  // salt okuma onizleme; sonuc push_cleanup'tan maybe_cleanup_hint'e gelir
}

void App::maybe_cleanup_hint() {
    const auto ph = cleanup_->phase();
    if (ph == core::cleanup::SessionPhase::Scanning) return;  // sonucu bekle
    cleanup_hint_pending_ = false;
    const auto bytes = cleanup_->selected_bytes();  // yalniz varsayilan secili kategoriler (Geri Donusum Kutusu haric)
    core::cleanup::HintNotifyInput ni;
    ni.scan_ready = ph == core::cleanup::SessionPhase::Ready;
    ni.enabled = prefs_.cleanup_hint_after_game;
    ni.game_running = vm_.game_gate_blocked;
    ni.selected_bytes = bytes;
    ni.tray_added = tray_.added();
    if (!core::cleanup::should_notify_cleanup_hint(ni)) return;
    cleanup_hint_last_ms_ = mono_ms();
    const bool tr = cat().locale() == core::i18n::Locale::TrTR;
    std::wstring text = t(core::i18n::StringId::TrayCleanupHint);
    const std::wstring size = ui::format_bytes(bytes, tr);
    if (const auto p = text.find(L"{size}"); p != std::wstring::npos) text.replace(p, 6, size);
    tray_.notify(t(core::i18n::StringId::AppTitle), text);
}

void App::push_cleanup() {
    if (!cleanup_) return;
    if (cleanup_hint_pending_) maybe_cleanup_hint();
    if (cleanup_standby_job_ && cleanup_->phase() != core::cleanup::SessionPhase::Running) {
        cleanup_standby_job_ = false;  // calisma bitti (Done/Failed/kapi engeli): yonetici isi kilidi birakilir
        admin_busy_ = false;
    }
    ui::ViewModel::CleanupView v;
    v.phase = cleanup_->phase();
    for (auto c : core::cleanup::kProductCategories) {
        ui::ViewModel::CleanupRow r{c};
        if (const auto* cp = cleanup_->plan().find(c)) {
            r.status = cp->preview.status;
            r.files = cp->preview.files;
            r.bytes = cp->preview.bytes;
            r.reparse = cp->preview.skipped_reparse;
            r.needs_admin = cp->preview.needs_admin;
            r.allocated = cp->preview.allocated;
            r.partial = cp->preview.partial;
        } else if (v.phase == core::cleanup::SessionPhase::Idle || v.phase == core::cleanup::SessionPhase::Scanning) {
            continue;
        }
        r.selectable = cleanup_->selectable(c);
        r.selected = cleanup_->selected(c);
        // WIN-08 ek: yukseltilmis onizleme yoksa yonetici satiri tiklanabilir kalir; UAC'i tetikler.
        r.admin_gate = r.needs_admin && r.status == core::cleanup::PreviewStatus::NeedsAdmin;
        v.rows.push_back(r);
    }
    v.selected_bytes = cleanup_->selected_bytes();
    v.any_selected = cleanup_->any_selected();
    for (auto c : core::cleanup::kProductCategories)
        if (cleanup_->selected(c)) v.selected_allocated += cleanup_->plan().find(c)->preview.allocated;
    v.done = cleanup_->progress().done;
    v.total = cleanup_->progress().total;
    v.report = cleanup_->report();
    // EXT-CLN-01: birim secenekleri (DIAG-08 envanterindeki olculebilen yerel birimler) + secilen birimin kimligi
    v.volume = cleanup_->volume();
    for (const auto& sv : vm_.storage) {
        if (!sv.total_gib || v.volumes.size() >= ui::ViewModel::kMaxCleanupVolumes) continue;
        v.volumes.push_back(platform::to_wide(sv.root));
        v.volume_free.push_back(sv.free_gib ? std::optional<std::uint64_t>(*sv.free_gib << 30) : std::nullopt);
    }
    if (v.volumes.empty() && !v.volume.empty()) {
        v.volumes.push_back(v.volume);
        v.volume_free.push_back(std::nullopt);
    }
    v.volume_id = cleanup_->plan().volume;
    v.volume_changed = v.phase == core::cleanup::SessionPhase::Failed && cleanup_->failure() == "volume changed";
    v.scan_partial = cleanup_->plan().scan_stopped;
    v.scan_cancelled = cleanup_->scan_cancelled();
    v.admin_pending = cleanup_admin_pending_;
    v.admin_notice_key = cleanup_admin_notice_;
    v.gate_reason_key = cleanup_->gate_reason();
    v.maybe_partial = v.phase == core::cleanup::SessionPhase::Failed && cleanup_->failure().rfind("partial:", 0) == 0;
    // EXT-CLN-03: acik dosya listesi (en buyukler once; tam yol degil, kok goreli)
    if (auto fo = cleanup_->files_open()) {
        if (const auto* cp = cleanup_->plan().find(*fo)) {
            v.files_open = fo;
            v.files = core::cleanup::largest_files(*cp, ui::ViewModel::kMaxListedFiles);
            v.files_total = cp->files.size();
        }
    }
    vm_.cleanup = v;
    if (window_) window_->set_cleanup(v);
}

std::string saved_ui_language() {
    if (const auto dir = platform::app_data_dir()) {
        platform::WinJournalFs fs(*dir);
        if (auto txt = fs.read(kPrefsFile); txt.ok())
            if (auto p = core::settings::Preferences::parse(txt.value()); p.ok()) return p.value().ui_language;
    }
    return std::string(core::i18n::kUiLanguageSystem);
}

float windows_text_scale() {
    DWORD v = 0, n = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", RRF_RT_REG_DWORD, nullptr, &v, &n) !=
            ERROR_SUCCESS ||
        v < 100 || v > 225)
        return 1.f;
    return static_cast<float>(v) / 100.f;
}

std::string windows_ui_language() {
    ULONG count = 0, chars = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &chars) && chars > 1) {
        std::wstring buf(chars, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buf.data(), &chars) && count > 0)
            return platform::to_utf8(buf.c_str());  // cift sifirla biten listenin ilki
    }
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
        return platform::to_utf8(name);
    return {};
}

void App::load_prefs() {
    if (const auto dir = platform::app_data_dir()) {
        platform::WinJournalFs fs(*dir);
        if (auto txt = fs.read(kPrefsFile); txt.ok())
            if (auto p = core::settings::Preferences::parse(txt.value()); p.ok()) prefs_ = std::move(p).value();
    }
    prefs_saved_ = prefs_;  // Uyumluluk #26: baslatma alanlari icin baseline (diskte okunan)
    vm_.speed.server_ip = prefs_.game_server_ipv4;  // R-C4: kayitli oyun sunucusu adresi (sonuc yok; kullanici yeniden olcer)
    // EF-X11: dosyadaki elle eklenen liste ekleme ile ayni Windows/korumali kok kurallarindan gecer (elle duzenlenmis olabilir).
    prefs_.manual_game_exes = core::games::filter_manual_exes(prefs_.manual_game_exes, platform::games::manual_exe_context());
    // WIN-10: plan okumasi icin secim kopyasi (is parcacigi ilk okumadan once hazir).
    {
        std::lock_guard<std::mutex> lk(service_sel_mu_);
        session_stop_services_ = prefs_.session_stop_services;
    }
}

void App::load_history() {
    if (const auto dir = platform::app_data_dir()) {
        platform::WinJournalFs fs(*dir);
        if (auto txt = fs.read(kHistoryFile); txt.ok()) {
            if (auto h = core::insights::History::parse(txt.value()); h.ok()) {
                history_ = std::move(h).value();
                // Spec 2026-11-12 C: eski surumun uzun gecmisi ilk acilista en yeni uc kayda indirilir (diskten silinir).
                if (history_.trimmed_on_load() > 0) save_history();
            }
            // Bozuk/yeni sema: bos baslanir, dosya EZILMEZ (ilk yeni sonuca kadar); yanlis sonuc yeniden yazilmaz.
            else return;
        }
    }
    // Gecmisi olcum ekranina yansit (yalniz kayitli gercek degerler).
    for (const auto& e : history_.entries()) {
        core::measure::ResultView r;
        r.source = e.source == "in_game"     ? core::measure::TestSource::InGame
                   : e.source == "endurance" ? core::measure::TestSource::Endurance
                                             : core::measure::TestSource::Builtin;
        r.title = e.title;
        r.when_unix_ms = e.when_unix_ms;
        r.fps_series = e.fps_series;
        // B3: eski kayitlarda run_valid alani yoktur -> false; Genel Bakis son olcum karti fark/yuzde iddia etmez.
        r.run_valid = e.run_valid;
        r.diagnosis = core::measure::Diagnosis::Insufficient;
        if (std::find(e.exclusions.begin(), e.exclusions.end(), "run.interrupted") != e.exclusions.end()) {
            // EF-X22: yarida kesilen kosu: her kaynakta ayni baslik, deger yok.
            r.title.clear();
            r.headline_key = "bench.run.interrupted";
            r.show_confidence = false;
            vm_.measure.history.push_back(std::move(r));
            continue;
        }
        if (e.source == core::measure::kBalanceSource) {
            // Spec 2026-11-12 A5: denge kaydi Uygulama Testi'nin kapsamidir; kart kayittan yeniden uretilir. Gorunum
            // cikmayan kayit (bilinmeyen protokol) bu ekranda gosterilmez; Gecmis sayfasinda genel satir olarak kalir.
            r.balance = core::measure::balance_view_from_history(e);
            if (!r.balance) continue;
            r.title.clear();
            r.show_confidence = false;
            r.mode = r.balance->quick ? core::measure::TestMode::Quick : core::measure::TestMode::Full;
            vm_.measure.history.push_back(std::move(r));
            continue;
        }
        if (r.source == core::measure::TestSource::Builtin) {
            // M02/M03: Uygulama Testi kaydi teshis degil ("Veri yetersiz" yazilmaz); ic kimlik yerine kademe.
            r.tier = core::measure::scene_tier_from_title(e.title);
            r.title.clear();
            r.headline_key = core::measure::builtin_history_headline(e.exclusions);
            r.show_confidence = false;
        } else if (r.source == core::measure::TestSource::Endurance) {
            // R13: baslik sabit yerellestirilmis metin; sahne kimligi gosterilmez. Ortalama = isinmis pencere.
            r.title.clear();
            r.headline_key = "measure.endurance.history";
            r.show_confidence = false;
            if (const auto warm = e.metrics.find("warm_fps"); warm != e.metrics.end())
                r.avg_fps = contracts::Measurement::of(warm->second, contracts::Unit::Fps);
            else if (const auto cold = e.metrics.find("cold_fps"); cold != e.metrics.end())
                r.avg_fps = contracts::Measurement::of(cold->second, contracts::Unit::Fps);
        }
        if (r.source == core::measure::TestSource::InGame) {  // kaydedilen gercek degerler yeniden acilista da gorunur
            if (const auto it = e.metrics.find("avg_fps"); it != e.metrics.end()) r.avg_fps = contracts::Measurement::of(it->second, contracts::Unit::Fps);
            if (const auto it = e.metrics.find("low1_fps"); it != e.metrics.end()) r.low1_fps = contracts::Measurement::of(it->second, contracts::Unit::Fps);
            if (const auto it = e.metrics.find("p99_ms"); it != e.metrics.end()) r.p99_ms = contracts::Measurement::of(it->second, contracts::Unit::Milliseconds);
            if (const auto it = e.metrics.find("max_ms"); it != e.metrics.end()) r.worst_ms = contracts::Measurement::of(it->second, contracts::Unit::Milliseconds);
        }
        if (const auto it = e.metrics.find("scene_max_ms"); it != e.metrics.end()) r.worst_ms = contracts::Measurement::of(it->second, contracts::Unit::Milliseconds);
        if (const auto it = e.metrics.find("scene_avg_fps"); it != e.metrics.end()) r.avg_fps = contracts::Measurement::of(it->second, contracts::Unit::Fps);
        if (const auto it = e.metrics.find("scene_low1_fps"); it != e.metrics.end()) r.low1_fps = contracts::Measurement::of(it->second, contracts::Unit::Fps);
        if (const auto it = e.metrics.find("scene_p99_ms"); it != e.metrics.end()) r.p99_ms = contracts::Measurement::of(it->second, contracts::Unit::Milliseconds);
        vm_.measure.history.push_back(std::move(r));
    }
    // Test merkezi acilista son kayitli sonucu da gosterir (gercek kayit; yoksa kart yok).
    if (!vm_.measure.history.empty()) vm_.measure.result = vm_.measure.history.front();
    // B15: sonuc kartindaki "onceki teste gore" farki yeniden acilista da ayni karardir (yalniz Uygulama Testi Genel kaydi;
    // test sonundakiyle ayni taban + ortak politika). Oyunda Test kaydi kosu manifestosu gerektirdigi icin burada
    // karsilastirilmaz (manifest diskte yok): fark iddia edilmez.
    vm_.measure.comparison = core::measure::reopen_comparison(history_.entries(), vm_.measure.result ? &*vm_.measure.result : nullptr);
    bench_runtime_ = latest_benchmark_runtime();
    bench_pcie_ = latest_balance_pcie();
    vm_.measure.balance_latest = latest_balance_view();
}

core::measure::BalancePcieRecord App::latest_balance_pcie_record() const {
    // Yalniz EN YENI denge kaydi (eski kayda dusulmez: bayat okuma).
    for (const auto& e : history_.entries()) {
        if (e.source != core::measure::kBalanceSource) continue;
        if (!core::measure::balance_view_from_history(e)) continue;  // yarida kesilmis kosu
        return core::measure::balance_pcie_record(e);
    }
    return {};
}

std::optional<core::diagnosis::PcieLoadReading> App::latest_balance_pcie() const {
    // Kayit baska bir ekran kartiyla alindiysa (kosul "gpu" bugunkunden farkli) ya da bugunku kart henuz bilinmiyorsa
    // kullanilmaz.
    return core::measure::balance_pcie_for_gpu(latest_balance_pcie_record(), vm_.gpu);
}

std::optional<core::measure::BalanceView> App::latest_balance_view() const {
    // Panel "Performansi ne sinirliyor?" basligi: en yeni denge kaydi (yarida kesilen kosu gorunum uretmez, atlanir).
    for (const auto& e : history_.entries())
        if (e.source == core::measure::kBalanceSource)
            if (auto v = core::measure::balance_view_from_history(e)) return v;
    return std::nullopt;
}

core::diagnosis::BenchmarkRuntime App::latest_benchmark_runtime() const {
    // DIAG-11 + B10: sahne kanitli en yeni Uygulama Testi kaydi. Sinif yalniz yayin kapisi acikken verilir; kapi
    // kapaliyken "olculdu" bilgisi gecer ve bilesen raporu CPU/GPU icin "olculdu, karar yok" der (hukum uretilmez).
    // B10 inceleme: yalniz bugunku islemci / ekran karti / surucuyle alinmis kayit (envanter okunmadan once bos).
    return core::diagnosis::benchmark_runtime_from_history(history_.entries(), core::diagnosis::diagnosis_release_status(),
                                                           {vm_.cpu, vm_.gpu, vm_.gpu_driver});
}

void App::sync_bench_evidence() {
    // B10 inceleme: her yeni gecmis kaydindan sonra (gecmis sinirli: yeni kayit eski Uygulama Testi'ni dusurebilir)
    // bilesen raporunun kaniti yeniden okunur; olcum durumu, sinif ya da yuk altindaki PCIe okumasi degistiyse rapor
    // yeniden toplanir. Degismediyse rapor yeniden toplanmaz.
    const auto rt = latest_benchmark_runtime();
    const auto pcie_before = bench_pcie_ ? core::diagnosis::pcie_link_text(*bench_pcie_) : std::string();
    const bool had_pcie = bench_pcie_.has_value();
    bench_pcie_ = latest_balance_pcie();
    const bool pcie_changed = had_pcie != bench_pcie_.has_value() || (bench_pcie_ && core::diagnosis::pcie_link_text(*bench_pcie_) != pcie_before);
    const bool rt_changed = core::diagnosis::benchmark_runtime_changed(bench_runtime_, rt);
    bench_runtime_ = rt;
    if (rt_changed || pcie_changed) start_findings_refresh();
}

void App::save_history() {
    if (uninstall_exiting_ || uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (const auto dir = platform::app_data_dir()) {
        std::error_code ec;
        std::filesystem::create_directories(*dir, ec);
        platform::WinJournalFs fs(*dir);
        const auto st = fs.write_atomic(kHistoryFile, history_.serialize());
        vm_.measure.history_save_failed = !st.ok();  // sonuc ekranda; kaydin kaybolacagi sessiz kalmaz
    }
}

void App::begin_run_marker(const char* source) {
    if (smoke_) return;
    if (const auto dir = platform::app_data_dir()) {
        std::error_code ec;
        std::filesystem::create_directories(*dir, ec);
        platform::WinJournalFs fs(*dir);
        (void)fs.write_atomic(core::measure::kRunMarkerFile, core::measure::encode_run_marker({source, now_unix_ms()}));
    }
}

void App::end_run_marker() {
    if (smoke_) return;
    if (const auto dir = platform::app_data_dir()) {
        platform::WinJournalFs fs(*dir);
        (void)fs.remove(core::measure::kRunMarkerFile);
    }
}

bool App::recover_run_marker() {
    const auto dir = platform::app_data_dir();
    if (!dir) return false;
    platform::WinJournalFs fs(*dir);
    const auto txt = fs.read(core::measure::kRunMarkerFile);
    if (!txt.ok()) return false;
    // Gecmis dosyasi var ama okunamadiysa (bozuk/yeni sema) uzerine YAZILMAZ; isaret de korunur.
    if (const auto h = fs.read(kHistoryFile); h.ok() && !core::insights::History::parse(h.value()).ok()) return false;
    bool added = false;
    // Onceki oturumda test yarida kaldi: kayit dusulur, test yeniden baslatilmaz.
    if (const auto m = core::measure::decode_run_marker(txt.value())) {
        (void)history_.add(core::measure::interrupted_entry(*m));
        save_history();
        added = true;
    }
    (void)fs.remove(core::measure::kRunMarkerFile);
    return added;
}

bool App::save_prefs() {
    if (uninstall_exiting_ || uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return false;  // R20
    const auto dir = platform::app_data_dir();
    if (!dir) return false;
    std::error_code ec;
    std::filesystem::create_directories(*dir, ec);
    platform::WinJournalFs fs(*dir);
    // Uyumluluk #26 tepsi tarafi: kurulum penceresi bu arada release_manifest_seen'i yukseltmis ya da Windows ile
    // baslatma alanlarini yazmis olabilir. Kayittan hemen once diskteki dosya okunur; bayat kopya bunlari geri almaz.
    if (auto txt = fs.read(kPrefsFile); txt.ok())
        if (auto p = core::settings::Preferences::parse(txt.value()); p.ok())
            prefs_ = core::settings::merge_tray_fields(std::move(prefs_), p.value(), prefs_saved_);
    const std::string text = prefs_.serialize();
    if (!fs.write_atomic(kPrefsFile, text).ok()) return false;  // yazilamazsa bellekte surer
    const auto back = fs.read(kPrefsFile);  // geri okuma: diskteki icerik gercekten yazilan mi
    if (!back.ok() || back.value() != text) return false;
    prefs_saved_ = prefs_;
    return true;
}

std::optional<bool> App::original_client_animation() const {
    // Etkin Performans islemi pencere animasyonunu kapattiysa kaydedilmis ozgun deger; yoksa nullopt (sistemi izle).
    if (!rec_ || !rec_->user_store) return std::nullopt;
    for (const auto& tx : rec_->user_store->scan().transactions) {
        if (tx.state != contracts::TxState::Active) continue;
        for (const auto& st : tx.steps)
            if (st.op.id == contracts::OperationId::VisualEffectsSet && st.op.target == "vfx:client_animation" && st.snapshot.existed)
                return st.snapshot.old_value != "0";
    }
    return std::nullopt;
}

void App::reconcile_autostart() {
    // ADR-030 / spec G: acilista Run kaydini tercihle uzlastir (ilk calistirma varsayilan acik, tasinmis EXE yeni yola,
    // kapaliysa sil). Smoke, arka plan test kosulari ve ctest (EF_AUTOSTART_READONLY=1; on plan pencere testleri
    // EF_TEST_BACKGROUND'u kaldirir) kayit defterine yazmaz: yalniz okur.
    platform::WinRunKeyStore store = platform::WinRunKeyStore::production();
    const std::wstring cmd = platform::autostart_command();
    const bool readonly_env = [] {
        wchar_t v[8]{};
        return GetEnvironmentVariableW(L"EF_AUTOSTART_READONLY", v, 8) == 1 && v[0] == L'1';
    }();
    if (smoke_ || ui::test_background_mode() || readonly_env) {
        autostart_state_ = core::settings::autostart_read(store, cmd);
        return;
    }
    const auto r = core::settings::autostart_on_launch(store, prefs_, cmd);
    autostart_state_ = r.state;
    const bool saved = (r.prefs_changed || prefs_.autostart_notice_pending) ? save_prefs() : true;
    // Tani (sahip kurulumu 2026-11-13, Run degeri kayboldu; neden kanitlanamadi): ne goruldu, ne yapildi.
    platform::append_autostart_log("app " + core::settings::autostart_launch_trace(r, prefs_) + (saved ? "" : " prefs_save=failed"));
}

void App::set_autostart(bool on) {
    platform::WinRunKeyStore store = platform::WinRunKeyStore::production();
    autostart_state_ = core::settings::autostart_set(store, prefs_, on, platform::autostart_command());
    platform::append_autostart_log(core::settings::autostart_set_trace("app settings toggle", on, autostart_state_));
    autostart_notice_ = false;  // kullanici karar verdi: bildirim kalkar
    (void)save_prefs();
    push_settings();
    if (window_ && autostart_state_.failure != core::settings::AutostartFailure::None) {
        using F = core::settings::AutostartFailure;
        const auto f = autostart_state_.failure;
        window_->announce(t(f == F::WriteFailed    ? StringId::SettingsAutostartFailedWrite
                            : f == F::VerifyFailed ? StringId::SettingsAutostartFailedVerify
                            : f == F::RemoveFailed ? StringId::SettingsAutostartFailedRemove
                                                   : StringId::SettingsAutostartUnknown),
                          false);
    }
}

void App::push_settings() {
    ui::SettingsView s;
    s.autostart_known = autostart_state_.known;
    s.autostart = autostart_state_.known && autostart_state_.on;
    s.autostart_failure = autostart_state_.failure;
    s.autostart_notice = autostart_notice_;
    s.system_motion_original = original_client_animation();
    s.app_animations = prefs_.app_animations;  // UI-11/R33
    s.strip_enabled = prefs_.strip_enabled;
    s.cleanup_hint = prefs_.cleanup_hint_after_game;
    s.bundle_include_dumps = bundle_include_dumps_;
    s.bundle_busy = bundle_busy_;
    s.bundle_done = bundle_done_;
    s.bundle_failed = bundle_failed_;
    s.bundle_path = bundle_path_;
    s.uninstall_phase = uninstall_phase_;
    s.uninstall_restored_all = uninstall_restored_all_;
    s.uninstall_data_removed = uninstall_data_removed_;
    s.uninstall_unrestored = uninstall_unrestored_;
    s.uninstall_note = uninstall_note_.empty() ? std::wstring{} : localize_note_key(uninstall_note_);
    for (const auto& n : uninstall_notes_) s.uninstall_notes.push_back(localize_uninstall_note(n));
    // El ile guncelleme denetiminin durumu yeniden kurulumda korunur (onceden Idle'a donuyordu: denetim surerken baska bir
    // ayar degisince "Denetle" yeniden etkin olur, ikinci tiklama suren isciyi UI'da join ederdi).
    s.update_check = vm_.settings.update_check;
    // Uyumluluk B8 (bosluk 12): cihaz dogrulama notu yukleyiciden gelir; yeniden kurulumda korunur.
    s.device_unverified = vm_.settings.device_unverified;
    s.device_os_unverified = vm_.settings.device_os_unverified;
    s.device_gpu_unverified = vm_.settings.device_gpu_unverified;
    // Denetim 5.2: dil
    s.ui_language = prefs_.ui_language;
    s.windows_tr = core::i18n::select_locale(windows_ui_language()) == core::i18n::Locale::TrTR;
    s.language_notice = language_notice_;
    s.language_restart_pending = language_restart_pending_;
    for (const auto& e : consent_.list())
        s.games.push_back({e.id, e.display_name, e.decision == core::games::Consent::Allow});
    vm_.settings = s;
    if (window_) window_->set_settings(s);
}

void App::on_language_choice() {
    if (!window_) return;
    namespace i18n = core::i18n;
    const bool windows_tr = i18n::select_locale(windows_ui_language()) == i18n::Locale::TrTR;
    auto sys = t(StringId::SettingsLanguageSystem);
    if (const auto q = sys.find(L"{lang}"); q != std::wstring::npos)
        sys.replace(q, 6, t(windows_tr ? StringId::SettingsLanguageTurkish : StringId::SettingsLanguageEnglish));
    ui::ModalSpec spec;
    spec.caption = t(StringId::AppTitle);
    spec.title = t(StringId::SettingsLanguageQuestion);
    spec.body.push_back(t(StringId::SettingsLanguageBody));
    spec.choices = {sys, t(StringId::SettingsLanguageTurkish), t(StringId::SettingsLanguageEnglish)};
    spec.yes = t(StringId::SettingsLanguageSave);
    spec.no = t(StringId::SettingsLanguageCancel);
    spec.initial_choice = prefs_.ui_language == "tr-TR" ? 1 : prefs_.ui_language == "en-US" ? 2 : 0;  // kayitli tercih secili gelir
    const auto r = window_->run_modal(std::move(spec));
    static constexpr std::string_view kValues[] = {i18n::kUiLanguageSystem, "tr-TR", "en-US"};
    if (r.answer != ui::ModalAnswer::Yes || r.choice < 0 || r.choice >= static_cast<int>(std::size(kValues))) return;  // Vazgec
    const std::string prev = prefs_.ui_language;
    prefs_.ui_language = std::string(kValues[static_cast<std::size_t>(r.choice)]);
    if (!save_prefs()) {
        prefs_.ui_language = prev;  // kaydedilemedi: bellekte de eski tercih
        // Onceki (kayitli) tercih acik dilden farkliysa "yeniden baslatinca" bilgisi kaybolmaz: hata satiri yalniz bu
        // secimin kaydedilmedigini soyler.
        language_notice_ = ui::SettingsView::LanguageNotice::SaveFailed;
        language_restart_pending_ = i18n::resolve_ui_locale(prev, windows_ui_language()) != cat().locale();
    } else {
        // Uyumluluk #1: yeni dil hemen uygulanir (pencere kapatip acmak surecin yeniden baslamasi degildi; dil hic
        // degismiyordu). Pencere bu cagrinin icinden yikilmaz: kip penceresi kapandiktan sonra UI kuyrugunda yeniden kurulur.
        language_notice_ = ui::SettingsView::LanguageNotice::None;
        language_restart_pending_ = false;
        if (i18n::locale_switch(prefs_.ui_language, windows_ui_language(), cat().locale())) {
            post([this] {
                if (!i18n::locale_switch(prefs_.ui_language, windows_ui_language(), cat().locale())) return;  // zaten bu dil
                if (apply_ui_language()) return;
                // Katalog yuklenemedi (gomulu tablo bozuk; pratikte olusmaz): kayitli dil sonraki baslatmada gecerli.
                language_notice_ = ui::SettingsView::LanguageNotice::Restart;
                language_restart_pending_ = true;
                push_settings();
            });
        }
    }
    push_settings();
}

std::string App::os_build_string() const {
    // environment.cpp ile ayni bicim: <build>.<ubr>; okunamazsa "?" (uydurma yok). Kurulum penceresiyle ortak.
    return upd::os_build_string();
}

std::vector<core::support::DiagFlag> App::support_env_flags() const {
    // Ortam bayragi ve uygulamanin verdigi karar (ham deger en cok 8 karakter; bayraklar sayi/kisa addir).
    const auto raw = [](const wchar_t* name) {
        wchar_t v[16]{};
        const DWORD n = GetEnvironmentVariableW(name, v, 16);
        return n > 0 && n < 16 ? platform::to_utf8(std::wstring_view(v, n)).substr(0, 8) : std::string();
    };
    const auto readonly_env = raw(L"EF_AUTOSTART_READONLY") == "1";
    std::vector<core::support::DiagFlag> f;
    f.push_back({"EF_UPDATE_CHECK", raw(L"EF_UPDATE_CHECK"), upd::update_check_disabled_by_env() ? "update_check_off" : "update_check_on"});
    f.push_back({"EF_AUTOSTART_READONLY", raw(L"EF_AUTOSTART_READONLY"),
                 smoke_ || ui::test_background_mode() || readonly_env ? "autostart_read_only" : "autostart_reconcile"});
    f.push_back({"EF_SETUP_ROLE", raw(L"EF_SETUP_ROLE"),
                 std::string(setup_role_disabled_by_env() ? "setup_role_off" : "setup_role_on") + (install_offer_applies() ? ",install_offer" : "")});
    f.push_back({"EF_TEST_BACKGROUND", raw(L"EF_TEST_BACKGROUND"), ui::test_background_mode() ? "background_test" : "normal"});
    f.push_back({"EF_UI_RENDERER", raw(L"EF_UI_RENDERER"), {}});
    // Uyumluluk B9: yol yazilmaz (kullanici adi icerebilir); yalniz verilip verilmedigi ve kabul edilip edilmedigi.
    f.push_back({"EF_DATA_DIR", GetEnvironmentVariableW(L"EF_DATA_DIR", nullptr, 0) > 0 ? std::string("set") : std::string(),
                 platform::app_data_dir_overridden() ? "data_dir_test_override" : "data_dir_default"});
    return f;
}

void App::create_support_bundle() {
    using UP = ui::SettingsView::UninstallPhase;
    if (bundle_busy_ || vm_.balance_report.busy || uninstall_phase_ == UP::Running) return;
    const auto dir = platform::app_data_dir();
    if (!dir) {
        bundle_done_ = false;
        bundle_failed_ = true;
        bundle_path_.clear();
        push_settings();
        if (window_) window_->announce(t(StringId::SettingsSupportFailed), false);
        return;
    }
    // Spec B7: once kip penceresi — "Cokme kayitlarini da ekle" isaret kutusu (varsayilan kapali, bellek icerigi uyarisi).
    // Hayir / kapat / pencere yok: hicbir sey yapilmaz.
    if (!window_) return;
    const auto dumps_choice = ui::ask_support_bundle(window_->hwnd(), cat());
    if (!dumps_choice) return;
    if (bundle_busy_ || uninstall_phase_ == UP::Running) return;  // soru acikken durum degismis olabilir
    bundle_include_dumps_ = *dumps_choice;
    // Yol secimi UI is parcaciginda (COM STA); zip olusturma ve yazma arka planda.
    const HWND owner = window_ ? window_->hwnd() : host_;
    const auto picked = platform::ask_save_path(owner, t(StringId::SettingsSupport), L"EcuFps-support.zip", L"Zip", L"*.zip");
    if (picked.status == platform::SavePathResult::Status::Canceled) return;  // kullanici vazgecti: durum degismez
    if (picked.status != platform::SavePathResult::Status::Chosen) {
        bundle_done_ = false;
        bundle_failed_ = true;
        bundle_path_.clear();
        push_settings();
        if (window_) window_->announce(t(StringId::SettingsSupportFailed), false);
        return;
    }
    run_bundle_job(picked.path, bundle_include_dumps_, false);
}

void App::run_bundle_job(const std::wstring& save_path, bool dumps, bool report) {
    if (bundle_worker_.joinable()) bundle_worker_.join();
    const auto dir = platform::app_data_dir();
    if (!dir) return;
    if (report) {
        vm_.balance_report = {};
        vm_.balance_report.busy = true;
        if (window_) {
            window_->set_view(vm_);
            window_->announce(t(StringId::BalanceReportBusy), false);
        }
    } else {
        bundle_busy_ = true;
        bundle_done_ = false;
        bundle_failed_ = false;
        bundle_path_.clear();
        push_settings();
        if (window_) window_->announce(t(StringId::SettingsSupportBusy), false);
    }
    const std::string version = product_version_string();
    const std::string os_build = os_build_string();
    const auto ctx = platform::current_redaction_context();
    const std::filesystem::path data_dir = *dir;
    // Uyumluluk B8 (bosluk 8): uygulamaya ozel tani alanlari UI is parcaciginda (tercih/katalog burada); donanim ve
    // sistem okumalari asagida is parcaciginda. Kullanici adi / yol / seri numarasi alani yok; paket yine redakte eder.
    core::support::DiagInput diag;
    diag.app_version = version;
    diag.build_id = vm_.build_id;
    diag.language.preference = prefs_.ui_language;
    diag.language.windows_ui = windows_ui_language();
    diag.language.resolved = std::string(core::i18n::to_string(cat().locale()));
    diag.flags = support_env_flags();
    // Saha 2026-11-15: geri alinamamis ayarlar (kullanici + makine journal'lari, salt okuma; to_json redakte eder).
    if (rec_) {
        for (const auto& sc : rec_->user_scopes())
            if (sc.store)
                for (auto& i : core::recovery::failed_restore_items(sc.name, sc.store->scan())) diag.restore_failures.push_back(std::move(i));
        for (auto& i : admin_channel_.machine_failed_items()) diag.restore_failures.push_back(std::move(i));
        diag.restore_failures_known = true;
    }
    // P3: Denge raporu ayni paketin icine Denge gecmis kaydini da koyar (izin listesi alanlar; yol/kimlik yok; paket redakte eder).
    // logs/balance.log zaten logs dizininden girer.
    std::string history_extra;
    if (report) history_extra = history_.export_json(now_unix_ms(), core::measure::kBalanceSource);
    bundle_worker_ = spawn_worker([this, data_dir, dumps, version, os_build, ctx, save_path, diag, report, history_extra]() mutable {
        auto req = core::support::make_bundle_request(data_dir, dumps, version, os_build);
        if (report && !history_extra.empty()) req.extra_files.emplace_back("balance_history.json", history_extra);
        try {
            platform::inventory::collect_platform_diag(diag);
            req.diag_json = core::json::write(core::support::diag_json(diag));
        } catch (...) {
            req.diag_json.clear();  // tani okunamadi: paket loglarla yine olusur (manifestte diag.json yok)
        }
        auto built = core::support::build_bundle(req, ctx);
        bool ok = false;
        if (built.ok()) {
            std::ofstream out(std::filesystem::path(save_path), std::ios::binary | std::ios::trunc);
            if (out) {
                const auto& bytes = built.value().zip_bytes;
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                ok = out.good();
            }
        }
        post([this, ok, save_path, report] {
            const auto announce_saved = [this](const std::wstring& path) {
                std::wstring s = t(StringId::BalanceReportSaved);
                if (const auto pos = s.find(L"{path}"); pos != std::wstring::npos) s.replace(pos, 6, path);
                return s;
            };
            if (report) {
                vm_.balance_report.busy = false;
                vm_.balance_report.done = ok;
                vm_.balance_report.failed = !ok;
                vm_.balance_report.path = ok ? save_path : std::wstring{};
                if (window_) {
                    window_->set_view(vm_);
                    window_->announce(ok ? announce_saved(save_path) : t(StringId::BalanceReportFailed), ok);
                }
                return;
            }
            bundle_busy_ = false;
            bundle_done_ = ok;
            bundle_failed_ = !ok;
            bundle_path_ = ok ? save_path : std::wstring{};
            push_settings();
            if (window_) window_->announce(t(ok ? StringId::SettingsSupportSaved : StringId::SettingsSupportFailed), ok);
        });
    });
}

void App::save_balance_report() {
    // P3: Darbogaz "Bu PC'de olculemedi" iken "Raporu kaydet": mevcut destek paketi yolu (redakte log + balance.log + Denge
    // gecmis kaydi + diag), secici penceresiz Masaustune; gonderim yok. Kullanici dugmeye basarak istemistir.
    using UP = ui::SettingsView::UninstallPhase;
    if (bundle_busy_ || vm_.balance_report.busy || uninstall_phase_ == UP::Running || uninstall_exiting_) return;
    std::wstring desktop;
    {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &p)) && p) desktop = p;
        if (p) CoTaskMemFree(p);
    }
    std::error_code ec;
    if (desktop.empty() || !std::filesystem::is_directory(std::filesystem::path(desktop), ec)) {
        vm_.balance_report = {};
        vm_.balance_report.failed = true;
        if (window_) {
            window_->set_view(vm_);
            window_->announce(t(StringId::BalanceReportFailed), false);
        }
        return;
    }
    wchar_t stamp[32]{};
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    wcsftime(stamp, 32, L"%Y%m%d-%H%M%S", &lt);
    const std::wstring path = (std::filesystem::path(desktop) / (std::wstring(L"EcuFps-denge-raporu-") + stamp + L".zip")).wstring();
    run_bundle_job(path, false, true);  // cokme dokumu eklenmez (bellek icerigi; yalniz Ayarlar'da acik onayla)
}

std::wstring App::localize_note_key(const std::string& key) const {
    if (const auto id = core::i18n::id_of(key)) return t(*id);
    return platform::to_wide(key);
}

std::wstring App::localize_uninstall_note(const std::string& note) const {
    // run_uninstall notlari sabit Ingilizce anahtarlardir; ayrintili hata metni platformdan gelir.
    const auto prefixed = [this, &note](const std::string& prefix, StringId id) -> std::optional<std::wstring> {
        if (note.rfind(prefix, 0) != 0) return std::nullopt;
        return t(id) + L" " + platform::to_wide(note.substr(prefix.size()));
    };
    if (note == "journal has an unknown newer schema; nothing removed") return t(StringId::SettingsUninstallNoteSchema);
    if (note == "some changes could not be restored; data kept so they can be retried")
        return t(StringId::SettingsUninstallNoteUnrestored);
    if (note == "machine changes need administrator approval; nothing was removed") return t(StringId::SettingsUninstallNoteMachineDenied);
    if (note == "a game started or game status could not be confirmed; nothing was removed") return t(StringId::SettingsUninstallNoteGate);
    if (note == "externally changed values were kept") return t(StringId::SettingsUninstallNoteExternalKept);
    if (note == core::recovery::kNoteProgramRebootPending) return t(StringId::SettingsUninstallNoteRebootPending);
    if (note == core::recovery::kNoteProgramDenied) return t(StringId::SettingsUninstallNoteProgramDenied);
    if (note == core::recovery::kNoteProgramFailed) return t(StringId::SettingsUninstallNoteProgramFailed);
    if (note == "machine data needs administrator rights and was left in place")
        return t(StringId::SettingsUninstallNoteMachineAdmin);
    if (auto w = prefixed("autostart: ", StringId::SettingsUninstallNoteAutostart)) return *w;
    if (auto w = prefixed("machine data: ", StringId::SettingsUninstallNoteMachine)) return *w;
    if (auto w = prefixed("user data: ", StringId::SettingsUninstallNoteUser)) return *w;
    return platform::to_wide(note);
}

void App::start_uninstall() {
    using UP = ui::SettingsView::UninstallPhase;
    if (uninstall_phase_ == UP::Running) return;
    const auto permit = permit_.verify(core::games::GuardedAction::SettingMutation);
    if (!permit.allowed && permit.reason_key != "gate.game_running") {
        uninstall_note_ = permit.reason_key;  // gercek neden: "oyun calisiyor" denmez (push_settings cevirir)
        uninstall_phase_ = UP::Idle;
        request_detection_rescan();
        push_settings();
        return;
    }
    const bool game_blocked = !permit.allowed;
    if (game_blocked || vm_.game_running || admin_busy_ || (controller_ && controller_->busy())) {
        uninstall_note_ = core::i18n::key_of((game_blocked || vm_.game_running) ? StringId::SettingsUninstallGameRunning
                                                                                : StringId::SettingsUninstallBusy);
        uninstall_phase_ = UP::Idle;
        push_settings();
        return;
    }
    if (uninstall_worker_.joinable()) uninstall_worker_.join();
    uninstall_note_.clear();
    uninstall_notes_.clear();
    uninstall_restored_all_ = false;
    uninstall_data_removed_ = false;
    uninstall_unrestored_ = 0;
    uninstall_phase_ = UP::Running;
    push_settings();
    if (window_) window_->announce(t(StringId::SettingsUninstallRunning), false);
    // Tum kapsamlar (kullanici + varsa makine journal'lari): once geri yukle+dogrula, sonra sil.
    std::vector<core::recovery::JournalScope> scopes = rec_ ? rec_->scopes() : std::vector<core::recovery::JournalScope>{};
    uninstall_worker_ = spawn_worker([this, scopes = std::move(scopes)]() mutable {
        platform::FsDataRemover remover = platform::FsDataRemover::production();
        // Makine kayitlari yalniz broker'da yazilabilir: once (grup basina bir UAC ile) geri al; reddedilirse veya
        // bekleyis sirasinda oyun acildiysa kullanici verisine de dokunulmaz.
        core::recovery::UninstallResult result;
        bool machine_ok = true, gate_closed = false;
        auto run_group = [&](core::ipc::RequestAction action, std::vector<contracts::SettingOperation> ops) {
            if (ops.empty() || !machine_ok || gate_closed) return;
            const auto b = admin_channel_.run(action, ops);
            if (b.elevation != core::ElevationOutcome::Started) machine_ok = false;
            if (b.count(core::broker::StepState::GateBlocked)) gate_closed = true;
        };
        {
            auto perf = admin_channel_.active_ops();
            for (auto& op : admin_channel_.perf_recovery_ops()) perf.push_back(std::move(op));
            run_group(core::ipc::RequestAction::Restore, std::move(perf));
            auto fixes = admin_channel_.fix_active_ops();
            for (auto& op : admin_channel_.fix_recovery_ops()) fixes.push_back(std::move(op));
            run_group(core::ipc::RequestAction::UndoFix, std::move(fixes));
            auto conflicts = admin_channel_.perf_conflict_ops();
            for (auto& op : admin_channel_.fix_conflict_ops()) conflicts.push_back(std::move(op));
            run_group(core::ipc::RequestAction::KeepCurrent, std::move(conflicts));
        }
        // UAC bekleyisinden sonra: run_uninstall yarim kayit kurtarmasi da yazar, guncel durum yeniden sorulur.
        if (machine_ok && !gate_closed && !permit_.verify(core::games::GuardedAction::SettingMutation).allowed) gate_closed = true;
        if (gate_closed) result.notes.push_back("a game started or game status could not be confirmed; nothing was removed");
        else if (machine_ok) {
            // ADR-033 spec 8: kurulu kopya -> program da kaldirilir (tek UAC, islemsel isci); tasinabilir -> eski davranis.
            const bool installed = core::setup::is_installed_location(platform::normalized_self_exe(),
                                                                      core::setup::make_layout(platform::known_roots()));
            platform::install::WorkerProgramRemover program;
            result = core::recovery::run_full_uninstall(scopes, remover, installed ? &program : nullptr);
        }
        else result.notes.push_back("machine changes need administrator approval; nothing was removed");
        post([this, result] {
            if (!result.data_removed && controller_) {  // uygulama acik kaliyor: Performans karti gercek durumu gostersin
                const auto ph = controller_->phase();
                if (ph == Phase::Off || ph == Phase::On || ph == Phase::RecoveryNeeded) controller_->init();
            }
            uninstall_restored_all_ = result.restored_all;
            uninstall_data_removed_ = result.data_removed;
            uninstall_unrestored_ = result.unrestored.size();
            uninstall_notes_.clear();
            uninstall_notes_ = result.notes;  // ham notlar: push_settings guncel dille cevirir
            uninstall_phase_ = ui::SettingsView::UninstallPhase::Done;
            // Veri silindiyse artik hicbir prefs/history/consent yazilmaz (silinen veri geri olusturulmaz).
            if (result.data_removed) uninstall_exiting_ = true;
            {
                // Kaldirma Run kaydini silmis olabilir: anahtar onbellekteki degeri degil gercek durumu gostersin.
                platform::WinRunKeyStore store = platform::WinRunKeyStore::production();
                autostart_state_ = core::settings::autostart_read(store, platform::autostart_command());
                autostart_notice_ = false;
            }
            push_settings();
            if (result.restored_all && result.data_removed && result.notes.empty()) {
                // R20: tam basari -> hemen cik. Not varsa (ornek: makine verisi yonetici ister) not gorunur kalir.
                if (window_) window_->announce(t(StringId::SettingsUninstallDataRemoved));
                PostQuitMessage(0);
                return;
            }
            if (window_)
                window_->announce(t(result.restored_all ? StringId::SettingsUninstallRestoredOk : StringId::SettingsUninstallRestoredFailed),
                                  result.restored_all);
        });
    });
}

}  // namespace ef::app
