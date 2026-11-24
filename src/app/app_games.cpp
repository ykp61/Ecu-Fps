// Oyunlar: kapi, izleme, kitaplik, gecmis, tam ekran ve NVIDIA profili. (denetim 4.4: app.cpp'den ayrildi; App uye islevleri).
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
#include "platform/windows/vendor_runtime.h"   // B-012: AMD/Intel runtime varligi (yukle + birak)
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

void App::push_check() {
    if (!check_) return;
    ui::ViewModel::CheckView v;
    v.phase = check_->phase();
    const auto& f = check_->findings();
    for (std::size_t i = 0; i < f.size(); ++i) {
        ui::ViewModel::CheckRow r;
        r.automatic = f[i].kind == core::perf::FixKind::Auto;
        r.text_key = r.automatic && f[i].item ? f[i].item->title_key : f[i].code;
        r.selected = r.automatic;  // varsayilan secili; kullanici degistirebilir (set_selected)
        v.rows.push_back(std::move(r));
    }
    for (std::size_t i = 0; i < v.rows.size() && i < check_selected_.size(); ++i)
        if (v.rows[i].automatic) v.rows[i].selected = check_selected_[i];
    v.done = check_->progress().first;
    v.total = check_->progress().second;
    v.report = check_->report();
    vm_.check = v;
    if (window_) window_->set_check(v);
}

void App::refresh_watch_consent() {
    std::vector<std::string> ids, exes;
    for (const auto& e : consent_.list()) {
        ids.push_back(e.id);
        exes.push_back(core::games::exe_basename_lower(e.id));
    }
    for (const auto& e : prefs_.observed_game_exes) {  // calisirken gercekten gorulen EXE'ler (Steam manifesti vermez)
        if (std::find(ids.begin(), ids.end(), e) != ids.end()) continue;
        ids.push_back(e);
        exes.push_back(core::games::exe_basename_lower(e));
    }
    {
        std::lock_guard lk(watch_mu_);
        watch_consent_ids_ = std::move(ids);
        watch_manual_exes_ = prefs_.manual_game_exes;
    }
    permit_.set_known_exes(std::move(exes));
    permit_.set_manual_exes(prefs_.manual_game_exes);
}

void App::request_detection_rescan() {
    vm_.detection_retrying = true;
    push_gate();
    {
        std::lock_guard lk(watch_mu_);
        watch_force_ = true;
    }
    watch_cv_.notify_all();
}

std::shared_ptr<App::LibraryFacts> App::collect_library_facts(platform::games::LibraryScanResult library,
                                                              const std::vector<std::string>& consent_ids, bool deep,
                                                              const std::function<bool()>& still_no_game,
                                                              platform::games::GameIconCache* icons) {
    auto facts = std::make_shared<LibraryFacts>();
    facts->has_rows = true;
    facts->deep = deep;
    facts->exes.reserve(consent_ids.size());
    for (const auto& id : consent_ids) facts->exes.push_back(core::games::exe_basename_lower(id));
    const std::vector<core::games::AdapterRecord> registry{core::games::cs2::adapter_record()};
    // F-59/F-60: salt-okuma tarama. Oyun kumesi degistiginde yeniden kurulur; UI is parcacigini bekletmez.
    const auto launch_options = platform::games::steam_launch_options();
    const auto cs2_video = platform::games::steam_cs2_video();
    {
        const auto evidence = platform::games::scan_protection_services();
        facts->protections = core::games::protection_status(evidence);
        // T09: FACEIT bilinmiyorsa (servis yoneticisi okunamadi) CS2 oncelik ogesi cikmaz (fail-closed).
        facts->game_priority.scan_trusted = library.trusted();
        facts->game_priority.faceit = !evidence.query_ok ? core::perf::FaceitState::Unknown
                                      : evidence.faceit_installed ? core::perf::FaceitState::Present
                                                                  : core::perf::FaceitState::Absent;
    }
    platform::nvidia::LazyDrs nv_drs;  // is parcacigina ozel oturum: UI/plan oturumuyla paylasilmaz
    // A3 (P12 karar 7): oyun-basi NVIDIA profili de melez GPU kapisindan gecer; hedef adaptor bilinmedigi icin yalniz tek donanim
    // GPU'su NVIDIA ise acilir (melezde satir/yazma yok; neden: nvidia_adapter_gate().reason_key).
    const bool nv_up = deep && nv_drs.available();  // oyun acikken NVAPI yuklenmez
    // Envanter yalniz derin taramada okunur (oyun acikken eskisi gibi dokunulmaz).
    const auto gpus = deep ? platform::inventory::read_gpus() : std::vector<core::inventory::GpuAdapter>{};
    const bool nv_ready = nv_up && core::perf::nvidia_adapter_gate(gpus, std::nullopt, true).allowed();
    // B-012 (R-B7): AMD/Intel runtime'i yalniz derin taramada (oyun kapali) imza + konum denetimiyle yuklenip hemen
    // birakilir; hicbir API cagrilmaz, ayar okunmaz/yazilmaz. Ozellik durumu okunmadigi icin rehber "veri yetersiz" der.
    if (deep) {
        std::vector<core::vendors::RuntimeInfo> runtimes;
        for (const auto& p : core::games::low_latency_probes(gpus, {})) {
            auto rt = platform::load_vendor_runtime(p.vendor);
            runtimes.push_back(rt.info);
            platform::unload_vendor_runtime(rt);
        }
        facts->vendor_ll = core::games::low_latency_probes(gpus, runtimes);
        facts->vendor_ll_read = true;
    }
    std::int64_t last_recheck = mono_ms();
    // F-57: tum oyunlar icin toplam giris butcesiyle sinirli upscaler taramasi.
    std::size_t upscaler_budget = 12000;
    // Spec 2026-11-12 §4: oyun ikonlari yalniz dosyadan (Steam kitaplik onbellegi, EXE ikon kaynagi) ve yalniz derin
    // kapi acikken okunur. Oyun calisirken hicbir dosyaya dokunulmaz: satir son bilinen ikonunu korur.
    platform::games::WinLibraryFs icon_fs;
    std::string steam_root;
    if (icons) {
        icons->begin_pass();
        if (deep)
            if (const auto root = icon_fs.steam_root(); root.status == core::games::SourceValue::Status::Found) steam_root = root.value;
    }
    std::optional<platform::games::GameIconLoader> icon_loader;  // ilk ihtiyacta: kendi COM kapsami + WIC fabrikasi
    for (const auto& g : library.games) {
        // Tarama surerken oyun acilirsa kalan klasorlere dokunulmaz; derin is borclu kalir.
        if (facts->deep && mono_ms() - last_recheck >= 2000) {
            last_recheck = mono_ms();
            if (!still_no_game || !still_no_game()) facts->deep = false;
        }
        const bool nv_ok = nv_ready && facts->deep;
        // EF-X11: elle eklenen oyunun anahtari tam EXE yoludur; klasoru yazma koku ve klasor taramasi olmaz.
        const bool folder_ok = core::games::game_folder_trusted(g);
        const std::string d = core::games::game_watch_key(g);
        facts->dirs.push_back(d);
        if (folder_ok) {
            facts->write_roots.push_back(g.install_dir);
            facts->defender_folders.emplace_back(g.install_dir, g.name);  // T11
            facts->game_priority.games.push_back({g.launcher, g.app_id, g.exe_hint, g.name});  // T09
        }
        if (!g.exe_hint.empty()) facts->exes.push_back(core::games::exe_basename_lower(g.exe_hint));
        facts->names[d] = platform::to_wide(g.name);
        facts->info[d] = InstalledGameInfo{g.name, g.app_id, g.version};  // kapinin gordugu kimlik + surum
        // Oyunlar sayfasi: gercek manifest verisi; kapi karari UI is parcaciginda (push_gate) yazilir.
        ui::ViewModel::GameRowView r;
        r.name = g.name;
        r.launcher = g.launcher;
        r.version = g.version;
        r.dir_key = d;
        r.guide_store = g.launcher;
        r.guide_app_id = g.app_id;
        r.guide_build = g.version;
        r.manual_exe = g.manual_exe;  // EF-X11: elle eklenen (launcher satirina bagli olabilir)
        r.manual_missing = g.manual_missing;
        for (const auto& a : registry)
            if (a.store == g.launcher && a.app_id == g.app_id) r.guide_verified_builds = a.verified_versions;
        std::string icon_exe;  // cozulmus ve diskte duran oyun EXE'si (ikon kaynagi adayi)
        // F-47: EXE yolu yalniz manifest ipucu veya gozlenmis tam yol ile cozulur; bulunamazsa dugme yok.
        if (folder_ok) {
            namespace nv = core::adapters::nvidia;
            const auto res = core::games::resolve_game_exe(g.install_dir, g.exe_hint, consent_ids);
            if (nv_ok)
                for (const auto& id : consent_ids) {
                    if (!core::games::path_under_dir(id, g.install_dir)) continue;
                    auto name = core::games::exe_basename_lower(id);
                    if (nv::parse_drs_target("drs:app:" + name + ":1057EB71").ok() &&
                        std::find(r.nv_candidates.begin(), r.nv_candidates.end(), name) == r.nv_candidates.end())
                        r.nv_candidates.push_back(std::move(name));
                }
            if (res.status == core::games::ExeResolveStatus::Resolved) {
                std::error_code ec;
                if (std::filesystem::is_regular_file(std::filesystem::path(platform::to_wide(res.path)), ec)) {
                    icon_exe = res.path;
                    r.fs_target = core::adapters::windows::appcompat_target_for_exe(res.path);
                    r.fs_known = !r.fs_target.empty();
                    // A1.1: surucude bu oyunun profili var mi (salt okuma). NVIDIA yoksa satir hic yazilmaz.
                    const auto exe = core::games::exe_basename_lower(res.path);
                    if (nv_ok && nv::parse_drs_target("drs:app:" + exe + ":1057EB71").ok()) {
                        nv::DrsTarget dt;
                        dt.global = false;
                        dt.app = exe;
                        dt.setting = static_cast<std::uint32_t>(nv::DrsSetting::PowerMode);
                        const auto cur = nv_drs.get(dt);
                        r.nv_exe = exe;
                        r.nv_has_profile = cur.ok();
                        r.nv_read_failed = !cur.ok() && cur.code() != contracts::ErrorCode::MissingValue;
                        r.nv_external = cur.ok() && cur.value().location == nv::DrsLocation::CurrentProfile && cur.value().value == 1;
                    }
                }
            }
        }
        for (const auto& a : registry)
            if (a.store == g.launcher && a.app_id == g.app_id && (a.capabilities & static_cast<std::uint32_t>(core::games::Capability::ConfigRead)))
                r.config_read = true;
        r.pro_slug = core::readiness::pro_settings_slug(g.launcher, g.app_id, g.name);  // GUIDE-01
        // F-60: oyun-başına bilinen koruma sistemleri (durum her zaman doğrulanmamış; gate kararı üstte).
        for (const auto& e : core::games::builtin_matrix()) {
            if (e.game_id != g.app_id) continue;
            if (!r.protection.empty()) r.protection += ", ";
            r.protection += e.protection;
        }
        // F-59: Steam başlatma seçenekleri + (CS2 ise) video ayarları salt okunur risk taraması.
        if (g.launcher == "steam") {
            std::vector<core::games::ConfigRisk> risks;
            r.risk_scanned = launch_options.scanned;  // dosya okundu; seçenek yokluğu "risk yok" demektir
            if (const auto it = launch_options.options.find(g.app_id); it != launch_options.options.end()) {
                risks = core::games::scan_launch_options(it->second);
            }
            if (g.app_id == std::string(core::games::cs2::kAppId) && cs2_video) {
                r.risk_scanned = true;
                risks = core::games::merge_risks(std::move(risks), core::games::scan_cs2_video(*cs2_video));
            }
            for (const auto& risk : risks) r.risk_keys.push_back(risk.code);
        }
        // F-57: yalnız DLL adı/sürümü okunur; oyun dosyasına yazılmaz.
        if (folder_ok && facts->deep && upscaler_budget > 0) {
            auto us = platform::games::scan_upscalers(g.install_dir, std::min<std::size_t>(upscaler_budget, 4000));
            r.upscalers_scanned = us.scanned;
            r.upscalers = std::move(us.assets);
            upscaler_budget -= std::min(upscaler_budget, us.files_seen);
        }
        if (icons) {
            const std::string icon_key = g.launcher + "|" + d;
            if (facts->deep) {
                if (!icon_loader) icon_loader.emplace();
                r.icon = icons->resolve(icon_key, core::games::icon_candidates(g, icon_exe, steam_root, icon_fs), *icon_loader);
            } else {
                r.icon = icons->remembered(icon_key);
            }
        }
        facts->rows.push_back(std::move(r));
    }
    if (icons) icons->end_pass(facts->deep);
    std::sort(facts->rows.begin(), facts->rows.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    facts->library = std::move(library);
    return facts;
}

void App::apply_library_facts(const LibraryFacts& facts) {
    detection_.set_library(facts.library);
    // EF-X11: kaynak satiri gercek sonuctan (guvenilmez taramada da); bos sonuc (toplama hatasi) onceki satiri silmez.
    if (!facts.library.providers.empty()) {
        vm_.library_providers.clear();
        for (const auto& pr : facts.library.providers)
            vm_.library_providers.push_back({pr.provider, std::string(core::games::provider_status_key(pr.status)), pr.games});
    }
    vm_.detection_retrying = false;
    // Okunamayan kitaplik onceki gercek listeyi silmez ve yazma izinli kokleri bosaltmaz.
    if (facts.has_rows && (facts.library.trusted() || !facts.rows.empty() || vm_.games_loading)) {
        game_names_ = facts.names;
        game_info_ = facts.info;
        vm_.games = facts.rows;
        for (auto& r : vm_.games)
            if (const auto it = game_renderers_.find(r.dir_key); it != game_renderers_.end()) r.guide_renderer = it->second;
        vm_.games_loading = false;
        vm_.games_deep_deferred = !facts.deep;
        vm_.protections = facts.protections;
        vm_.protections_loading = false;
        if (facts.vendor_ll_read) vm_.vendor_ll = facts.vendor_ll;  // oyun acikken (derin degil) son bilinen durum kalir
        adapters_.appcompat.set_allowed_roots(facts.write_roots);  // F-47: YAZMA yalniz kurulu oyun klasorlerinin altina
        adapters_.gpu_pref.set_allowed_roots(facts.write_roots);   // T01: ayni kok kaynagi
        {
            std::lock_guard<std::mutex> lk(defender_mu_);
            defender_folders_ = facts.defender_folders;  // T11: plan icin guvenilir oyun klasorleri
            game_priority_ = facts.game_priority;        // T09
        }
        refresh_game_fullscreen_states();
    }
    push_gate();
}

void App::start_game_watch() {
    const std::uint64_t generation = ++watch_generation_;
    detection_.begin_session(generation);
    push_gate();
    watch_thread_ = spawn_worker([this, generation] { watch_loop(generation); });
}

void App::watch_loop(std::uint64_t generation) {
    using namespace core::games;
    constexpr std::int64_t kProcessPollMs = 3000;
    std::optional<GameWatcher> watcher;
    std::string fingerprint;
    bool have_library = false, trusted = false;
    std::size_t game_count = 0;
    std::int64_t next_library = 0, library_observed = -1;
    std::size_t facts_consent_size = 0;
    bool deep_owed = false, rebuild_owed = false;
    platform::games::GameIconCache icon_cache;  // yalniz bu is parcacigi (kilit yok); kitaplikta olmayanlar her derin turda duser
    // Yeniden kurulumdan hemen once TAZE kanit: bir onceki turun sonucu oyun acilmis/kapanmis olabilir.
    // Gercek izleyicinin KOPYASI: gozlem gecmisi korunur (yolu sonradan okunamayan oyun "yok" sayilmaz), asil durum degismez.
    const auto no_game_now = [&watcher](const platform::games::LibraryScanResult& lib, const std::vector<std::string>& consent) {
        std::vector<std::string> dirs, exes;
        for (const auto& g : lib.games) {
            dirs.push_back(core::games::game_watch_key(g));  // EF-X11: elle eklenen = tam EXE
            if (!g.exe_hint.empty()) exes.push_back(g.exe_hint);
        }
        exes.insert(exes.end(), consent.begin(), consent.end());
        try {
            GameWatcher probe = watcher ? *watcher : GameWatcher({}, {});
            probe.set_library(std::move(dirs), std::move(exes));
            return poll_proves_no_game(probe.update(platform::games::snapshot_processes()));
        } catch (...) {
            return false;
        }
    };
    while (!watch_stop_.load()) {
        const bool forced = watch_force_.exchange(false);
        bool library_tick = false;
        if (forced || !have_library || mono_ms() >= next_library) {
            library_tick = true;
            library_observed = mono_ms();  // gozlem, tarama BASLAMADAN once zamanlanir
            platform::games::LibraryScanResult lib;
            std::vector<std::string> manual;
            {
                std::lock_guard lk(watch_mu_);
                manual = watch_manual_exes_;
            }
            const auto scan_tick = static_cast<std::int64_t>(GetTickCount64());  // izin tazeligi: tarama BASLAMADAN once
            try {
                lib = platform::games::scan_installed_library({}, manual);
            } catch (...) {
                lib = {};  // beklenmeyen hata "oyun yok" sayilmaz (Failed)
            }
            // Uyumluluk B8 (bosluk 20): islem siniri izni bu taramayi yeniden kullanir (UI is parcaciginda kitaplik G/C'si yok).
            permit_.offer_library(lib, scan_tick, manual);
            trusted = lib.trusted();
            game_count = lib.games.size();
            next_library = mono_ms() + library_poll_ms(lib);
            auto fp = library_fingerprint(lib);
            std::shared_ptr<LibraryFacts> facts;
            std::vector<std::string> consent;
            {
                std::lock_guard lk(watch_mu_);
                consent = watch_consent_ids_;
            }
            const std::size_t consent_size = consent.size();
            rebuild_owed = watch_rebuild_.exchange(false) || rebuild_owed;
            LibraryRebuildInput want;
            want.must = !have_library || fp != fingerprint;
            want.wanted = consent_size != facts_consent_size || rebuild_owed || deep_owed;
            // Partial/Failed kitaplik "oyun yok" kaniti degildir: calisan oyunun klasoru listede olmayabilir.
            want.no_game = (want.must || want.wanted) && lib.trusted() && no_game_now(lib, consent);
            const auto plan = decide_library_rebuild(want);
            if (plan.rebuild) {
                // Kitaplik acilistan sonra degisti (kurulum/kaldirma/guven): izleyici yeni listeyle surer, is parcacigi degismez.
                try {
                    const auto recheck_lib = lib;
                    facts = collect_library_facts(std::move(lib), consent, plan.deep,
                                                  [&] { return !watch_stop_.load() && no_game_now(recheck_lib, consent); }, &icon_cache);
                } catch (...) {
                    facts.reset();
                }
                if (facts) {
                    if (watcher) watcher->set_library(facts->dirs, facts->exes);
                    else watcher.emplace(facts->dirs, facts->exes);
                    fingerprint = std::move(fp);
                    have_library = true;
                    facts_consent_size = consent_size;
                    deep_owed = !facts->deep;
                    if (facts->deep) rebuild_owed = false;
                } else {
                    // Toplama basarisiz: iz saklanmaz (sonraki turda yeniden kurulur); guvenilir "oyun yok" uretilmez.
                    facts = std::make_shared<LibraryFacts>();
                    trusted = false;
                    if (!watcher) game_count = 0;
                }
            } else {
                facts = std::make_shared<LibraryFacts>();
                facts->library = std::move(lib);
            }
            post([this, generation, facts] {
                if (generation == watch_generation_) apply_library_facts(*facts);
            });
        }
        WatchResult r;
        bool emit = true;
        if (game_count > 0 && watcher) {
            const auto observed = mono_ms();  // liste okunmadan ONCE: gec islenen gozlem yeni sayilmaz
            try {
                r = watcher->update(platform::games::snapshot_processes());
            } catch (...) {
                r = WatchResult{};
                r.detection = DetectionState::Failed;  // beklenmeyen istisna da tarama basarisizligidir
            }
            r.observed_at_ms = observed;
        } else if (library_tick) {
            // Kurulu oyun yok: surec listesi gereksiz. Guvenilir bos tarama tazelik kanitidir; kitaplik hatasi "yok" degildir.
            r.detection = trusted ? DetectionState::Ok : DetectionState::Incomplete;
            r.observed_at_ms = library_observed;
        } else {
            emit = false;
        }
        if (emit)
            post([this, generation, r = std::move(r)]() mutable { on_watch_result(generation, std::move(r)); });
        const auto now = mono_ms();
        const std::int64_t wait = game_count > 0 ? kProcessPollMs : std::max<std::int64_t>(50, next_library - now);
        std::unique_lock lk(watch_mu_);
        watch_cv_.wait_for(lk, std::chrono::milliseconds(wait), [this] { return watch_stop_.load() || watch_force_.load(); });
    }
}

void App::on_watch_result(std::uint64_t generation, core::games::WatchResult result) {
    // Kitaplik guvenilmez olsa da gorulen oyun gercektir: kosan test durdurulur.
    const bool saw_game = generation == watch_generation_ && !result.running.empty();
    if (generation == watch_generation_ && !uninstall_exiting_) {
        bool added = false;
        for (auto& id : observed_tracker_.update(result, result.observed_at_ms))
            if (prefs_.observed_game_exes.size() < 64 &&
                std::find(prefs_.observed_game_exes.begin(), prefs_.observed_game_exes.end(), id) == prefs_.observed_game_exes.end()) {
                prefs_.observed_game_exes.push_back(std::move(id));
                added = true;
            }
        if (added) {
            save_prefs();
            refresh_watch_consent();  // oyun kapaninca kart yeniden kurulur (oyun acikken klasor taramasi yapilmaz)
        }
    }
    // Nihai durum TEK adimda yerlesir; tazelik gozlem zamanindan; borclu yan etkiler koordinatorde.
    const auto st = detection_.on_result(generation, std::move(result));
    if (st.discarded) return;
    if (saw_game) on_game_appeared();
    // Bekleyen cikis: durum kalici olarak dogrulanamaz hale geldiyse kullanici sessiz bekletilmez (yaklasik 30 sn sonra sorulur).
    untrusted_streak_ = st.detection_ok ? 0 : untrusted_streak_ + 1;
    if (pending_exit_ && untrusted_streak_ == 10) check_pending_exit();
    if (st.detection_ok) {
        if (st.active) {
            ActiveGame a;
            a.dir_key = st.active->install_dir;
            a.pid = st.active->pid;
            a.create_time = st.active->create_time;
            if (const auto gi = game_info_.find(a.dir_key); gi != game_info_.end()) {
                a.game_id = gi->second.app_id;
                a.build = gi->second.build;
                a.name = gi->second.name;
            }
            active_game_ = std::move(a);
        } else {
            active_game_.reset();
        }
        if (st.running_dirs.empty()) action_denied_key_.clear();
    }
    push_gate();
}

void App::on_game_appeared() {
    // Test surerken oyun acildiysa kaynak yuku kullaniciyi etkilemesin: durdurulur ve neden gosterilir.
    using core::measure::TestPhase;
    if (vm_.measure.source == core::measure::TestSource::Builtin &&
        (vm_.measure.phase == TestPhase::Running || vm_.measure.phase == TestPhase::Preparing)) {
        measure_->request_bench_cancel();
        vm_.measure.phase = TestPhase::Stopping;
        vm_.measure.gate_stopped = true;
        if (window_) window_->set_measure(vm_.measure);
    }
    if (vm_.measure.endurance.running) measure_->stop_endurance_test();
}

void App::run_deferred_startup_recovery() {
    if (startup_recovery_deferred_ && mono_ms() < recovery_retry_at_ms_) {
        detection_.owe_recovery();
    } else if (startup_recovery_deferred_) {
        const auto ph = controller_ ? controller_->phase() : Phase::Off;
        // Yonetici/Performans isi suruyorsa veya kapi kapaliysa bayrak kalir; is bitince (finish_admin_job, on_change) yeniden denenir.
        if (!admin_busy_ && ph != Phase::Applying && ph != Phase::Restoring && gate(core::games::GuardedAction::SettingMutation).allowed) {
            startup_recovery_deferred_ = false;
            const bool started = run_admin_job([this]() -> std::function<void()> {
                // reconcile kapiyi cagirana birakir: yarim kaydi geri almadan hemen once guncel durum.
                if (!permit_.verify(core::games::GuardedAction::SettingMutation).allowed)
                    return [this] {
                        startup_recovery_deferred_ = true;
                        recovery_retry_at_ms_ = mono_ms() + 30000;
                        detection_.owe_recovery();
                    };
                auto recovery = core::recovery::run_startup_recovery(rec_->user_scopes());
                return [this, recovery = std::move(recovery)] {
                    journal_writes_blocked_ = recovery.writes_blocked;
                    const auto p2 = controller_->phase();
                    if (p2 == Phase::Off || p2 == Phase::On || p2 == Phase::RecoveryNeeded) controller_->init();
                };
            });
            if (!started) startup_recovery_deferred_ = true;
        }
    }
    if (!deferred_machine_offer_.empty()) offer_machine_recovery(std::exchange(deferred_machine_offer_, {}));
}

void App::refresh_game_fullscreen_states() {
    if (vm_.games.empty()) return;
    const auto targets = fix_targets();
    const auto listed = [](const std::vector<std::string>& v, const std::string& t) {
        return std::find(v.begin(), v.end(), t) != v.end();
    };
    for (auto& r : vm_.games) {
        {
            using core::perf::GameProfileRecord;
            const auto record = [&](const std::string& exe) {
                return core::perf::game_profile_record(exe, targets.active, targets.recovery, targets.interrupted, targets.conflict);
            };
            auto rec = GameProfileRecord::None;
            r.nv_record_exe.clear();
            if (!r.nv_exe.empty()) {
                rec = record(r.nv_exe);
                if (rec != GameProfileRecord::None) r.nv_record_exe = r.nv_exe;
            } else {
                // EXE belirsizlesti (ikinci EXE goruldu): kayitli islemin geri alinmasi kaybolmaz.
                // Baska satirin kesin EXE'si bu satira yazilmaz: yanlis oyunda "ayarli" gorunmesin.
                for (const auto& c : r.nv_candidates) {
                    const bool other = std::any_of(vm_.games.begin(), vm_.games.end(), [&](const auto& o) { return &o != &r && o.nv_exe == c; });
                    if (other) continue;
                    rec = record(c);
                    if (rec != GameProfileRecord::None) {
                        r.nv_record_exe = c;
                        break;
                    }
                }
            }
            r.nv_applied = rec != GameProfileRecord::None;
            r.nv_partial = rec == GameProfileRecord::Partial;
            r.nv_attention = rec == GameProfileRecord::Attention;
            r.nv_conflict = rec == GameProfileRecord::Conflict;
        }
        if (r.fs_target.empty()) {
            r.fs_known = false;
            r.fs_read_ok = true;
            r.fs_disabled = false;
            r.fs_applied = false;
            continue;
        }
        const auto st = adapters_.appcompat.token_state(r.fs_target);
        r.fs_read_ok = st.ok();
        r.fs_disabled = st.ok() && st.value().value_or(false);
        r.fs_applied = listed(targets.active, r.fs_target) || listed(targets.recovery, r.fs_target) ||
                       listed(targets.interrupted, r.fs_target) || listed(targets.conflict, r.fs_target);
    }
}

void App::on_game_fullscreen(std::size_t k) {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (k >= vm_.games.size()) return;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;  // oyun calisiyor: neden karti gosterildi
    if (admin_busy_) return;
    if (!rec_ || !rec_->fixes_store || !rec_->fixes_engine) return;
    refresh_game_fullscreen_states();  // karar gercek registry + journal durumundan verilir
    if (k >= vm_.games.size()) return;
    const auto row = vm_.games[k];
    if (!row.fs_known || row.fs_target.empty() || !row.fs_read_ok) return;
    const bool undo = row.fs_applied;
    if (!undo && row.fs_disabled) return;  // baska kaynaktan kapali: sessizce sahiplenilmez
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring question = t(undo ? StringId::GamesFsUndoQuestion : StringId::GamesFsQuestion);
    if (const auto p = question.find(L"{game}"); p != std::wstring::npos) question.replace(p, 6, platform::to_wide(row.name));
    const auto detail = t(undo ? StringId::GamesFsUndoDetail : StringId::GamesFsDetail);
    if (!ui::ask_fix(owner, cat(), question, detail)) return;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;  // bekleyis sirasinda oyun acilmis olabilir
    vm_.games_notice_key.clear();
    vm_.games_notice_warning = false;
    const std::string target = row.fs_target;
    if (undo) {
        auto* store = rec_->fixes_store.get();
        auto* engine = rec_->fixes_engine.get();
        const bool started_undo = run_admin_job([store, engine, target, this]() -> std::function<void()> {
            core::broker::BatchResult b;
            b.elevation = core::ElevationOutcome::Started;  // kullanici kapsami: UAC yok
            b.steps.push_back(core::broker::restore_matching(
                store, engine, [&target](const contracts::SettingOperation& o) { return o.target == target; }, target));
            const auto state = b.steps.front().state;
            const bool ok = state == core::broker::StepState::Restored;
            const bool nothing = state == core::broker::StepState::NothingToDo;
            const bool gate = state == core::broker::StepState::GateBlocked;
            return [this, ok, nothing, gate] {
                vm_.games_notice_key = ok ? "games.fs.notice_restored"
                                          : nothing ? "games.fs.notice_nothing"
                                          : gate ? "chk.notice.gate_blocked" : "games.fs.notice_failed";
                vm_.games_notice_warning = !ok && !nothing;
                refresh_game_fullscreen_states();
                if (window_) window_->set_view(vm_);
            };
        });
        if (!started_undo) games_job_busy_notice();
        return;
    }
    core::recovery::JournalTx tx;
    tx.id = platform::random_nonce();
    tx.actor_sid = startup_ctx_.actor_sid;
    tx.target_sid = startup_ctx_.target_sid;
    tx.created_unix_ms = now_unix_ms();
    core::recovery::JournalStep step;
    step.op = contracts::SettingOperation{contracts::OperationId::AppCompatLayerSet, contracts::OperationScope::CurrentUser,
                                          target, "disabled", false, false};
    tx.steps.push_back(std::move(step));
    auto* engine = rec_->fixes_engine.get();
    const bool started_apply = run_admin_job([engine, tx = std::move(tx), this]() mutable -> std::function<void()> {
        const auto r = engine->apply(std::move(tx));
        const bool ok = r.ok() && r.value().state == contracts::TxState::Active;
        const bool gate = !r.ok() && r.code() == contracts::ErrorCode::GateBlocked;
        return [this, ok, gate] {
            vm_.games_notice_key = ok ? "games.fs.notice_applied" : gate ? "chk.notice.gate_blocked" : "games.fs.notice_failed";
            vm_.games_notice_warning = !ok;
            refresh_game_fullscreen_states();
            if (window_) window_->set_view(vm_);
        };
    });
    if (!started_apply) games_job_busy_notice();
}

void App::games_job_busy_notice() {
    vm_.games_notice_key = "games.notice.admin_busy";  // engel Performans disinda bir yonetici isi de olabilir
    vm_.games_notice_warning = true;
    if (window_) window_->set_view(vm_);
}

void App::on_game_nvidia(std::size_t k) {
    if (uninstall_phase_ == ui::SettingsView::UninstallPhase::Running) return;  // R20
    if (k >= vm_.games.size()) return;
    if (!gate_or_block(core::games::GuardedAction::SettingMutation)) return;
    if (admin_busy_) return;
    refresh_game_fullscreen_states();
    const auto row = vm_.games[k];
    const bool undo = row.nv_applied;
    const bool keep = row.nv_conflict;
    const std::string exe = undo ? row.nv_record_exe : row.nv_exe;
    if (exe.empty()) return;
    // A3: yeni uygulama icin hedef adaptor dogrulanmali (geri alma/koruma kayitli degeri geri yazar, kapidan bagimsiz).
    if (!undo && !keep && !core::perf::nvidia_adapter_gate(platform::inventory::read_gpus(), std::nullopt, true).allowed()) return;
    if (!undo && (!row.nv_has_profile || row.nv_external)) return;  // profil yok / baska kaynak: sessizce sahiplenilmez
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring question = t(keep ? StringId::GamesNvKeepQuestion : undo ? StringId::GamesNvUndoQuestion : StringId::GamesNvQuestion);
    if (const auto p = question.find(L"{game}"); p != std::wstring::npos) question.replace(p, 6, platform::to_wide(row.name));
    std::wstring detail = t(ac(keep ? StringId::GamesNvKeepDetail : StringId::GamesNvDetail));
    // EF-X25 T01: gozlenen API DX12/Vulkan ise kuyruk kapsami onayda aciklanir; islem seti degismez (yalniz metin).
    if (!keep && !undo && !core::games::driver_queue_note_key(row.guide_renderer).empty()) detail += L"\n\n" + t(StringId::GamesNvDetailGameManaged);
    if (!ui::ask_fix(owner, cat(), question, detail)) return;  // Hayir: hicbir sey degismez
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;
    vm_.games_notice_key.clear();
    vm_.games_notice_warning = false;
    auto ops = core::perf::nvidia_game_profile_ops(exe);
    if (keep) {  // yalniz dis degisiklik bekleyen kayit kapatilir; digeri Etkin kalir
        const auto conflict = fix_targets().conflict;
        std::erase_if(ops, [&](const auto& op) { return std::find(conflict.begin(), conflict.end(), op.target) == conflict.end(); });
    }
    const bool started = run_admin_job([this, undo, keep, ops = std::move(ops)]() -> std::function<void()> {
        const auto action = keep ? core::ipc::RequestAction::KeepCurrent : undo ? core::ipc::RequestAction::UndoFix : core::ipc::RequestAction::Fix;
        const auto b = admin_channel_.run(action, ops);
        std::string key(keep ? core::broker::admin_notice_key(action, b) : core::broker::game_profile_notice_key(undo, b));
        return [this, key = std::move(key)] {
            vm_.games_notice_key = key;
            vm_.games_notice_warning = key != "games.nv.notice_applied" && key != "games.nv.notice_restored" && key != "chk.notice.keep_done";
            refresh_game_fullscreen_states();
            if (window_) window_->set_view(vm_);
            watch_rebuild_ = true;  // surucu degeri yeniden okunsun ("zaten en yuksek" bayat kalmasin)
            {
                std::lock_guard lk(watch_mu_);
                watch_force_ = true;
            }
            watch_cv_.notify_all();
        };
    });
    if (!started) games_job_busy_notice();  // Evet sessizce kaybolmaz: bildirim bu sayfada da gorunur
}

void App::push_history() {
    std::vector<std::pair<std::string, core::recovery::JournalStore::Listing>> ls;
    // Broker makine journal / "fixes" dizinini ilk yazimda olusturur: acilista yoksa burada acilir (salt okuma).
    if (rec_) (void)rec_->refresh_machine_scopes();
    if (rec_)
        for (const auto& sc : rec_->scopes())
            if (sc.store) ls.emplace_back(sc.name, sc.store->scan());  // salt okuma; bozuk dosya silinmez
    auto& h = vm_.history;
    h.view = core::recovery::build_history(ls);
    h.loading = false;
    // R16: yerel test gecmisi (en yeni once; UI'da sinirli satir). Secim yalniz gecerli indekslerde kalir.
    const auto& entries = history_.entries();
    h.tests_total = entries.size();
    const std::size_t shown =
        std::min<std::size_t>(entries.size(), ui::ViewModel::HistoryPageView::kMaxHistoryTests);
    h.tests.assign(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(shown));
    h.export_enabled = !(uninstall_exiting_ || uninstall_phase_ == ui::SettingsView::UninstallPhase::Running);
    h.selected.erase(std::remove_if(h.selected.begin(), h.selected.end(),
                                    [&](std::size_t i) { return i >= h.tests.size(); }),
                     h.selected.end());
    // R15: akran sorgusu en yeni testin kaynak + kosul iziyle kurulur. Yerel akran havuzu YOK (ag/yukleme yok):
    // compare_peers bos havuzda daima Insufficient(N=0) doner; UI gercek veri yokken yuzdelik yazmaz.
    h.peer_query = {};
    if (!h.tests.empty()) {
        h.peer_query.source = h.tests.front().source;
        h.peer_query.fingerprint = h.tests.front().fingerprint;
    }
    h.peer = core::insights::compare_peers(h.peer_query, {});
    refresh_history_compare();
    if (window_) window_->set_view(vm_);
}

void App::refresh_history_compare() {
    auto& h = vm_.history;
    h.selected.erase(std::remove_if(h.selected.begin(), h.selected.end(),
                                    [&](std::size_t i) { return i >= h.tests.size(); }),
                     h.selected.end());
    h.cohort = {};
    h.compare_base_fps.clear();
    h.compare_current_fps.clear();
    if (h.selected.size() != 2) {
        h.comparison.reset();
        return;
    }
    const auto& a = h.tests[h.selected[0]];
    const auto& b = h.tests[h.selected[1]];
    // Yon kayit zamanindan: eski -> yeni (secim sirasi karisik olsa da ayni sonuc).
    const bool a_first = a.when_unix_ms <= b.when_unix_ms;
    h.comparison = core::insights::compare_history(a_first ? a : b, a_first ? b : a);
    if (h.comparison->compatibility.compatible) {
        h.cohort = core::insights::compare_cohort(history_.entries(), a_first ? a : b, a_first ? b : a);
        h.compare_base_fps = (a_first ? a : b).fps_series;
        h.compare_current_fps = (a_first ? b : a).fps_series;
    }
}

void App::select_history(std::size_t index) {
    auto& h = vm_.history;
    if (index >= h.tests.size()) return;
    if (const auto it = std::find(h.selected.begin(), h.selected.end(), index); it != h.selected.end()) {
        h.selected.erase(it);  // ayni kayda ikinci tik: secimi kaldir
    } else {
        if (h.selected.size() >= 2) h.selected.erase(h.selected.begin());  // en eski secim birakilir
        h.selected.push_back(index);
    }
    refresh_history_compare();
    if (window_) window_->set_view(vm_);
}

namespace {
StringId manual_error_id(core::games::ManualExeCheck c) {
    using C = core::games::ManualExeCheck;
    switch (c) {
        case C::NotExe: return StringId::GamesManualErrNotExe;
        case C::DriveRoot:
        case C::SystemDir:
        case C::ProtectedRoot: return StringId::GamesManualErrSystemDir;
        case C::Duplicate: return StringId::GamesManualErrDuplicate;
        case C::Limit: return StringId::GamesManualErrLimit;
        default: return StringId::GamesManualErrInvalidPath;
    }
}
}  // namespace

void App::games_notice(StringId id, bool warning) {
    vm_.games_notice_key = std::string(core::i18n::key_of(id));
    vm_.games_notice_warning = warning;
    if (window_) {
        window_->set_view(vm_);
        window_->announce(t(id), !warning);
    }
}

void App::add_manual_game() {
    using UP = ui::SettingsView::UninstallPhase;
    if (uninstall_exiting_ || uninstall_phase_ == UP::Running) return;  // R20
    const HWND owner = window_ ? window_->hwnd() : host_;
    const std::wstring filter = t(StringId::GamesAddExeFilter);
    const auto picked = platform::ask_open_path(owner, t(StringId::GamesAddExeTitle), filter.c_str(), L"*.exe");
    if (picked.status == platform::OpenPathResult::Status::Canceled) return;  // vazgecti: durum degismez
    if (picked.status != platform::OpenPathResult::Status::Chosen) return games_notice(StringId::GamesManualErrDialog, true);
    const std::string path = platform::to_utf8(picked.path);
    if (const auto c = core::games::check_manual_exe(path, platform::games::manual_exe_context(), prefs_.manual_game_exes);
        c != core::games::ManualExeCheck::Ok)
        return games_notice(manual_error_id(c), true);
    using EF = platform::games::ExeFileCheck;
    if (const auto f = platform::games::check_exe_file(path); f != EF::Ok)
        return games_notice(f == EF::Missing || f == EF::Unreadable ? StringId::GamesManualErrMissing : StringId::GamesManualErrNotExe, true);
    std::wstring question = t(StringId::GamesAddExeQuestion);
    if (const auto p = question.find(L"{game}"); p != std::wstring::npos) question.replace(p, 6, picked.path);
    if (!ui::ask_fix(owner, cat(), question, t(StringId::GamesAddExeDetail))) return;  // Hayir: hicbir sey degismez
    if (uninstall_exiting_ || uninstall_phase_ == UP::Running) return;  // soru sirasinda kaldirma basladi
    prefs_.manual_game_exes.push_back(path);
    if (!save_prefs()) {  // kaydedilemeyen ekleme bellekte de birakilmaz: yeniden baslatmada kaybolacak "Eklendi" denmez
        prefs_.manual_game_exes.pop_back();
        return games_notice(StringId::GamesManualErrSave, true);
    }
    refresh_watch_consent();
    {
        std::lock_guard lk(watch_mu_);
        watch_force_ = true;  // yeni liste hemen taransin
    }
    watch_cv_.notify_all();
    games_notice(StringId::GamesManualNoticeAdded, false);
}

void App::remove_manual_game(std::size_t index) {
    using UP = ui::SettingsView::UninstallPhase;
    if (uninstall_exiting_ || uninstall_phase_ == UP::Running) return;
    if (index >= vm_.games.size() || vm_.games[index].manual_exe.empty()) return;
    const auto row = vm_.games[index];  // kopya: soru sirasinda gorunum yenilenebilir
    if (row.running) return games_notice(StringId::GamesManualErrRunning, true);
    const HWND owner = window_ ? window_->hwnd() : host_;
    std::wstring question = t(StringId::GamesManualRemoveQuestion);
    if (const auto p = question.find(L"{game}"); p != std::wstring::npos) question.replace(p, 6, platform::to_wide(row.name));
    if (!ui::ask_fix(owner, cat(), question, t(StringId::GamesManualRemoveDetail))) return;
    if (uninstall_exiting_ || uninstall_phase_ == UP::Running) return;
    // Bekleyis sirasinda oyun acilmis olabilir: izlenmeyi birakmak calisan oyunu gizlerdi.
    if (!verify_or_block(core::games::GuardedAction::SettingMutation)) return;
    const std::string key = core::games::canonical_manual_exe(row.manual_exe);
    auto& v = prefs_.manual_game_exes;
    const auto before = v;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& e) { return core::games::canonical_manual_exe(e) == key; }), v.end());
    if (!save_prefs()) {  // diskte kalan giris yeniden baslatmada geri gelirdi: liste degismemis sayilir
        v = before;
        return games_notice(StringId::GamesManualErrSave, true);
    }
    refresh_watch_consent();
    {
        std::lock_guard lk(watch_mu_);
        watch_force_ = true;
    }
    watch_cv_.notify_all();
    games_notice(StringId::GamesManualNoticeRemoved, false);
}

void App::export_history() {
    using UP = ui::SettingsView::UninstallPhase;
    if (uninstall_exiting_ || uninstall_phase_ == UP::Running) return;  // R20: silme sonrasi yeni veri yazilmaz
    if (history_.entries().empty()) return;
    auto& h = vm_.history;
    const HWND owner = window_ ? window_->hwnd() : host_;
    const std::wstring filter = t(StringId::HistoryExportFilter);
    const auto picked = platform::ask_save_path(owner, t(StringId::HistoryExportTitle), L"EcuFps-history.json",
                                                filter.c_str(), L"*.json", L"json");
    if (picked.status == platform::SavePathResult::Status::Canceled) return;  // kullanici vazgecti: durum degismez
    if (picked.status != platform::SavePathResult::Status::Chosen) {
        h.export_done = false;
        h.export_failed = true;
        h.export_path.clear();
        h.export_bytes = 0;
        h.export_error_key = std::string(core::i18n::key_of(StringId::HistoryExportFailedPath));
        if (window_) {
            window_->set_view(vm_);
            window_->announce(t(StringId::HistoryExportFailedPath), false);
        }
        return;
    }
    // Yalniz izin listesi alanlari (History::export_json); ham yol/kimlik uretilmez. Yerel yazma, yukleme yok.
    const auto json = history_.export_json(now_unix_ms());
    bool ok = false;
    {
        std::ofstream out(std::filesystem::path(picked.path), std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(json.data(), static_cast<std::streamsize>(json.size()));
            out.flush();
            ok = out.good();
        }
    }
    if (!ok) {
        h.export_done = false;
        h.export_failed = true;
        h.export_path.clear();
        h.export_bytes = 0;
        h.export_error_key = std::string(core::i18n::key_of(StringId::HistoryExportFailedWrite));
        if (window_) {
            window_->set_view(vm_);
            window_->announce(t(StringId::HistoryExportFailedWrite), false);
        }
        return;
    }
    h.export_done = true;
    h.export_failed = false;
    h.export_path = picked.path;
    h.export_bytes = json.size();
    h.export_error_key.clear();
    if (window_) {
        window_->set_view(vm_);  // kaydedilen yol + bayt (sessiz basari yok)
        window_->announce(t(StringId::HistoryExportAnnounce));
    }
}

}  // namespace ef::app
