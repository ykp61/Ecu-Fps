#pragma once
// UI-06: uygulama yasam dongusu. Surec boyunca yasayan gizli ana pencere (tepsi simgesinin sahibi,
// tek ornek aktivasyonu, is parcacigi postalari) + istege bagli arayuz penceresi. Arayuz kapaninca
// kaynaklari birakilir (UI-05), surec tepside surer; "Cik ve geri al" etkin degisiklikleri geri
// yukleyip dogruladiktan sonra cikar.

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <map>
#include <thread>
#include <vector>

#include "app/measure_controller.h"
#include "core/support/diag.h"
#include "app/adapter_set.h"
#include "core/cleanup/session.h"
#include "core/startup/session.h"
#include "core/games/watcher.h"
#include "core/games/watch_state.h"
#include "core/games/detection_coordinator.h"
#include "core/games/protection_gate.h"
#include "core/diagnosis/components.h"
#include "core/diagnostics/health_test.h"
#include "core/perf/system_check.h"
#include "core/insights/history.h"
#include "benchmark/runner/runner.h"
#include "core/games/consent.h"
#include "core/metrics/live.h"
#include "core/settings/autostart.h"
#include "core/settings/preferences.h"
#include "core/i18n/catalog.h"
#include "core/perf/background_plan.h"
#include "core/perf/game_priority_plan.h"
#include "core/perf/journal_backend.h"
#include "core/perf/proof_experiment.h"
#include "core/measure/view.h"
#include "core/readiness/readiness.h"
#include "core/recovery/uninstall.h"
#include "core/roles.h"
#include "platform/windows/background/process_scan.h"
#include "platform/windows/broker_channel.h"
#include "platform/windows/service_channel.h"
#include "platform/windows/cleanup_channel.h"
#include "platform/windows/games/game_icons.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/games/live_permit.h"
#include "platform/windows/recovery_host.h"
#include "ui/shell/main_window.h"
#include "core/measure/balance_view.h"
#include "core/measure/live_view.h"
#include "ui/tray/tray.h"

namespace ef::app {

// Denetim 5.2: kayitli uygulama dili tercihi (preferences.json "ui_language"; okunamazsa "system"). Katalog App'ten once
// secildigi icin main bunu okur.
std::string saved_ui_language();
// Windows GORUNTU dili (GetUserPreferredUILanguages; bolge bicimi GetUserDefaultLocaleName degil). Bos = okunamadi.
std::string windows_ui_language();
// Windows Erisilebilirlik > Metin boyutu orani (HKCU\Software\Microsoft\Accessibility TextScaleFactor, 100-225 -> 1.0-2.25).
// Okunamazsa 1.0.
float windows_text_scale();

class App {
public:
    App(HINSTANCE inst, const core::LaunchArgs& args, int show_cmd, core::i18n::Catalog cat);
    ~App();
    int run();

    // Oyun ilk algilandiginda (GAME/OPT-06 izleyicisi cagirir): tek seferlik Evet/Hayir.
    // Donus: kayitli/yeni karar; cevapsiz kapatilirsa Ask.
    core::games::Consent on_game_detected(const std::string& exe_path, const std::wstring& display_name);
    // Zorunlu guncelleme: yeni surum baslatilirken tek ornek kilidi birakilir; kurulum geri alinirsa yeniden alinir.
    void set_instance_hooks(std::function<void()> release, std::function<void()> reacquire);

private:
    static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT host(UINT m, WPARAM w, LPARAM l);
    void post(std::function<void()> fn);
    void open_window(bool activate);
    void wire_window();
    void close_window_deferred();
    ui::TrayMenu tray_menu() const;
    void update_tray_icon();  // BRAND-01: dogrulanmis duruma + gorev cubugu temasina gore
    void refresh_tray();
    void request_exit_restore();
    void check_pending_exit();
    void load_consent();
    void save_consent();
    void push_settings();
    std::optional<bool> original_client_animation() const;  // UI-13: Performans oncesi Windows animasyon degeri
    void load_prefs();
    // R21: destek paketi (secim UI is parcaciginda, zip yazimi arka planda; gonderim yok).
    void create_support_bundle();
    // Destek paketi isi (create_support_bundle + P3 Denge raporu ortak yolu): diag + redakte log + zip, arka planda.
    // report=true: Masaustune yazilan Denge raporu (Olcumler kart durumu; Ayarlar durumu degismez; ek: Denge gecmis kaydi).
    void run_bundle_job(const std::wstring& save_path, bool dumps, bool report);
    void save_balance_report();
    // Uyumluluk B8: diag.json icin ortam bayraklari ve kararlari (UI is parcacigi).
    std::vector<core::support::DiagFlag> support_env_flags() const;
    std::string os_build_string() const;
    // R20: her seyi geri al ve verileri sil (arka plan; basarida veri yeniden yazilmadan cikilir).
    void start_uninstall();
    std::wstring localize_uninstall_note(const std::string& note) const;
    std::wstring localize_note_key(const std::string& key) const;  // katalog anahtariysa ceviri, degilse ham metin
    void start_live();       // UI-09: ornekleyici is parcacigi (bir kez)
    void push_cleanup();     // WIN-08
    void push_startup();     // WIN-09
    void push_before_after();  // OPT-08
    void set_live_visible(bool v);
    bool save_prefs();  // yazildi ve geri okundu mu
    std::wstring t(core::i18n::StringId id) const;
    // Sahip raporu 2026-11-13: hizmet yolundaki yonetici metni (".svc" esi) yalniz vm_.admin_without_uac iken.
    // Her cagrida refresh_admin_path() (onay penceresinden hemen once guncel hizmet durumu).
    core::i18n::StringId ac(core::i18n::StringId id);
    // vm_.admin_without_uac'i SCM'den yeniden okur (salt okuma, baglanmaz). Degisirse gorunum yenilenir.
    // Acilis, sayfa gosterimi, Performans anahtari ve her yonetici isinin sonunda; surekli yoklama yok.
    void refresh_admin_path();
    static std::int64_t mono_ms();

    HINSTANCE inst_;
    core::LaunchArgs args_;
    int show_cmd_;
    // Uyumluluk #1: dil canli degisir. Yuklenen her katalog surec boyunca yasar (dil basina en cok bir; yeniden yazilmaz);
    // dil degisince yalniz isaretci degisir. Baska is parcaciginda o anda okunan eski katalog gecersizlesmez.
    std::array<std::optional<core::i18n::Catalog>, 2> catalogs_;
    std::atomic<const core::i18n::Catalog*> cat_ptr_{nullptr};
    const core::i18n::Catalog& cat() const { return *cat_ptr_.load(std::memory_order_acquire); }
    // Tercih + Windows dili acik dilden farkliysa katalogu degistirir (UI is parcacigi); tepsi ipucu ve FPS paneli
    // yeni dille yeniden kurulur. Pencereye dokunmaz. Donus: dil degisti mi.
    bool switch_catalog_if_needed();
    const core::i18n::Catalog* catalog_for(core::i18n::Locale l);  // gerekirse yukler; yuklenemezse nullptr
    // Ayarlar'da dil kaydedildikten sonra: katalog degisir, ana pencere ayni yer/boyut ve Ayarlar sayfasiyla yeniden
    // kurulur. Donus: yeni dil uygulandi mi (false: katalog yuklenemedi, eski dil surer).
    bool apply_ui_language();
    std::optional<WINDOWPLACEMENT> reopen_placement_;  // dil degisiminde yeniden kurulan pencerenin onceki yeri
    // Denetim 5.1: Windows "Metin boyutu" ile buyutulmus tema (acilista okunur; degisiklik yeniden baslatinca).
    ui::Theme theme_ = ui::with_text_scale(ui::default_theme(), windows_text_scale());
    bool smoke_;
    HWND host_ = nullptr;
    bool host_destroyed_ = false;  // B2: cikista gizlemeden hemen sonra yok edilir (ikinci baslatma olu pencereye yazmasin)
    ui::Tray tray_;
    std::unique_ptr<ui::MainWindow> window_;
    ui::ViewModel vm_;  // pencere yokken de guncel tutulan gorunum
    bool told_still_running_ = false;
    bool pending_exit_ = false;
    int untrusted_streak_ = 0;  // ardisik guvenilmez tespit turu
    std::int64_t recovery_retry_at_ms_ = 0;
    std::int64_t auto_restore_retry_at_ms_ = 0;  // arka uc reddinden sonra tekrar deneme tabani
    bool state_known_ = false;   // journal'dan baslangic durumu okundu mu
    // CORE-03: acilista bir kullanici journal'inda bilinmeyen yeni sema varsa yeni degisiklik yasak
    // (broker disi UI yolu da ayni sozlesmeye uyar).
    std::atomic<bool> journal_writes_blocked_{false};
    HICON tray_icon_ = nullptr;  // sahiplik bizde
    int tray_variant_ = -1, tray_px_ = 0;

    AdapterSet adapters_;
    std::unique_ptr<platform::RecoveryHost> rec_;
    // OPT-10 / ADR-033: once yardimci hizmet (UAC yok), olmazsa tek UAC ile broker. Evet/Hayir run() oncesinde kalir.
    platform::ServiceFirstChannel admin_channel_;
    std::unique_ptr<core::perf::JournalBackend> backend_;
    std::unique_ptr<core::perf::PerfController> controller_;
    core::perf::Phase last_phase_ = core::perf::Phase::Off;
    std::thread worker_, refresher_, loader_;
    // Denetim 4.2: bulgu tazeleme UI is parcacigini bekletmez. Suren tazeleme varken yeni istek yalniz "yeniden calistir"
    // isaretler; sonuc gelince (is parcacigi bitmek uzereyken) bir kez daha baslatilir. Yalniz UI is parcacigi okur/yazar.
    bool refresher_busy_ = false, refresher_rerun_ = false;
    std::atomic<bool> refresher_done_{true};  // is parcacigi govdesi bitti (sonuc post'u kaybolsa da busy takilmaz)
    void start_findings_refresh();
    core::games::ConsentStore consent_;
    core::settings::Preferences prefs_;
    // ADR-030 / spec G: Windows ile baslatma durumu (geri okunmus) ve ilk acilis bildirimi (yalniz bu oturum).
    core::settings::AutostartState autostart_state_;
    bool autostart_notice_ = false;
    void reconcile_autostart();
    void set_autostart(bool on);
    // R21: destek paketi oturum durumu (tercih degil; dump'lar varsayilan kapali).
    std::thread bundle_worker_;
    bool bundle_include_dumps_ = false, bundle_busy_ = false, bundle_done_ = false, bundle_failed_ = false;
    std::wstring bundle_path_;
    // R20: kaldirma/veri silme durumu. uninstall_exiting_=true iken prefs/history/consent yeniden YAZILMAZ
    // (silinen veri geri olusturulmaz).
    std::thread uninstall_worker_;
    ui::SettingsView::UninstallPhase uninstall_phase_ = ui::SettingsView::UninstallPhase::Idle;
    bool uninstall_restored_all_ = false, uninstall_data_removed_ = false, uninstall_exiting_ = false;
    std::size_t uninstall_unrestored_ = 0;
    // Uyumluluk #1: notlar katalog anahtari / ham kaynak metin olarak tutulur, push_settings her seferinde guncel dille
    // cevirir (canli dil degisiminde eski dilde metin kalmaz).
    std::string uninstall_note_;
    std::vector<std::string> uninstall_notes_;
    // Uyumluluk #26 (tepsi tarafi): son yuklenen/kaydedilen tercihler. Baslatma alanlari bundan farkli degilse bu kayit
    // onlara dokunmaz (kurulumun diske yazdigi deger korunur).
    core::settings::Preferences prefs_saved_;
    // UI-09 canli ornekleme: yalniz pencere gorunurken 1 Hz; ornekleyici bir kez acilir.
    std::thread live_thread_;
    std::mutex live_mu_;
    std::condition_variable live_cv_;
    bool live_visible_ = false, live_stop_ = false;
    core::metrics::Smoother sm_cpu_, sm_gpu_, sm_disk_;
    // WIN-08: temizlik oturumu (onizleme/calistirma kendi is parcaciginda)
    std::unique_ptr<core::cleanup::CleanupSession> cleanup_;
    std::thread cleanup_worker_;
    // WIN-08 ek: yonetici kategoriler icin UAC yalniz kullanici sectiginde; yardimci oturumu yeniden kullanilir.
    std::shared_ptr<platform::CleanupAdminClient> cleanup_admin_;
    std::thread cleanup_admin_worker_;
    bool cleanup_admin_pending_ = false;
    // R-C2: oyun kapaninca salt okuma temizlik taramasi + tepsi bildirimi (silme YOK). Yalniz UI is parcacigi.
    bool prev_game_running_ = false;       // onceki push_gate'te oyun calisiyordu (kapanis gecisi icin)
    bool cleanup_hint_pending_ = false;    // oyun kapanisi tarama baslatti; sonuc bildirimi bekliyor
    std::int64_t cleanup_hint_last_ms_ = 0;  // son oneri (mono_ms); 0 = hic (ayni oturumda sik bildirim yok)
    void on_game_exited();
    void maybe_cleanup_hint();
    bool cleanup_standby_job_ = false;  // T15: bellek bosaltma temizlik calismasinda; admin_busy_ onunla tutulur
    std::string cleanup_admin_notice_;
    void on_cleanup_select(core::cleanup::CleanupCategory c, bool on);
    bool claim_standby_job();
    void start_admin_cleanup_scan(core::cleanup::CleanupCategory c);
    void close_admin_cleanup();
    std::unique_ptr<core::startup::StartupSession> startup_;  // WIN-09
    core::startup::ChangeContext startup_ctx_;
    std::thread startup_worker_;
    // WIN-10: kullanicinin isaretledigi oturumluk durdurulacak hizmetler (kanonik SCM adi). UI yazar;
    // plan okumasi is parcaciginda kopya alir (kilit). Kalici tercih prefs_ ile ayni anda guncellenir.
    // T01: hazirlik taramasinin son Grafik tercihi verisi (tarama is parcacigi yazar, plan okur). Tarama yoksa bos: plan ogesi yok.
    std::mutex gpu_pref_mu_;
    core::readiness::GpuPrefInfo gpu_pref_cache_;
    std::mutex defender_mu_;
    std::vector<std::pair<std::string, std::string>> defender_folders_;  // T11: guvenilir oyun klasorleri (defender_mu_)
    core::perf::GamePriorityInput game_priority_;  // T09: guvenilir tarama oyunlari + FACEIT durumu (defender_mu_)
    std::mutex service_sel_mu_;
    std::vector<std::string> session_stop_services_;
    // P2: arka plan programi adaylari. Gercek surec goruntusu (salt okuma) kisa sure onbellekte tutulur: plan onizlemesi
    // sik yenilenir, imza/CPU taramasi her seferinde yapilmaz. Is parcaciklari okur/yazar (kilit).
    std::vector<core::perf::PlanItem> background_plan_items();
    platform::background::ProcessScanner bg_scanner_;
    std::mutex bg_mu_;
    std::int64_t bg_scan_ms_ = 0;  // son tarama (mono_ms); 0 = hic
    std::vector<core::perf::PlanItem> bg_items_;
    // P9/B5: Olcumler (Uygulama/Denge/Oyunda Test, uzun test, hiz testi, ABBA deneyi, canli test ekrani) durumu ve is
    // parcaciklari MeasureController'da (measure_controller.h). App yalniz MeasureHost'u uygular (app_measure.cpp).
    friend class AppMeasureHost;
    void init_measure();
    std::unique_ptr<MeasureHost> measure_host_;
    std::unique_ptr<MeasureController> measure_;
    std::optional<core::measure::BalanceView> latest_balance_view() const;
    // Spec 2026-11-12 H: Guncellemeler — salt okuma Windows Update surucu aramasi (arka plan, iptal + zaman asimi).
    std::unique_ptr<core::drivers::DriverScanner> driver_scanner_;
    bool updates_auto_pending_ = false;  // sayfa ilk acildi; kapi acilinca ilk denetim baslar
    void start_driver_scan();
    // Spec H2: Windows Update onerisini (scan.updates indeksi) uygulama ici Evet/Hayir -> UAC -> yardimci ile kurar.
    void run_driver_install(std::size_t k);
    // ADR-032: ekran karti satirinin "Dosyadan kur"u (gpus indeksi). Kullanici resmi paketi secer -> uygulama klasorune
    // kopya (iptal edilebilir) -> imza + yayinci on denetimi -> Evet/Hayir -> UAC -> yardimci yeniden dogrular ve kurar.
    // Kopya surerken ayni dugme "Durdur"dur.
    void run_vendor_package(std::size_t k);
    void on_vendor_package_ready(std::size_t k, core::drivers::GpuVendor vendor, std::string id, std::string name, std::uint64_t bytes,
                                 core::drivers::SignatureCheck sig);
    std::thread local_worker_;
    std::atomic<bool> local_cancel_{false};
    // Wave 2A: Oyunda Test — kapinin izin verdigi calisan oyun icin yukseltilmis yardimciyla kare yakalama.
    // Gercek UAC/yardimci yalniz kullanici Baslat dediginde; test ortami bunu calistirmaz.
    void start_ingame_test();
    // OPT-06: oyun baslatma izleyicisi (tepsideyken de calisir; 3 sn'de bir surec listesi, yalniz kurulu oyun varsa)
    std::thread watch_thread_;
    std::atomic<bool> watch_stop_{false};
    std::map<std::string, std::wstring> game_names_;  // kurulum klasoru -> gorunen ad
    struct InstalledGameInfo {
        std::string name, app_id, build;  // gorunen ad + uyumluluk kimligi + kurulu surum
    };
    std::map<std::string, core::games::GuideRenderer> game_renderers_;  // bu oturumda gozlenen (EF-X12)
    std::map<std::string, InstalledGameInfo> game_info_;  // normalize kurulum klasoru -> kimlik
    struct ActiveGame {
        std::string dir_key, game_id, build, name;
        std::uint32_t pid = 0;
        std::uint64_t create_time = 0;
    };
    std::optional<ActiveGame> active_game_;  // izleyicinin su an algiladigi tek oyun
    // EF-X18: gozlenebilen canli olcum kanit kapsami (UI is parcacigi). Bos = bilinmiyor -> kapi kapali.
    // Koruma surumu gozlenmez; GateContext'te bos kalir (fail-closed). capture_os_build_: '?' iceren deger bos.
    std::string capture_collector_commit_, capture_os_driver_scope_, capture_os_build_;
    // Oyun tespit durumu, tazelik, engel nedeni ve yan etkiler tek yerde (saf, birim testli).
    core::games::DetectionCoordinator detection_;
    // Islem sinirinda O ANKI durum (son karara degil guncel gozleme dayanir); her is parcacigindan cagrilabilir.
    platform::games::LiveGamePermit permit_;
    std::uint64_t watch_generation_ = 0;
    std::atomic<bool> watch_rebuild_{false};  // islem sonrasi satir verisi (surucu degeri) yeniden okunsun
    core::games::ObservedExeTracker observed_tracker_;  // UI is parcacigi
    std::atomic<bool> watch_force_{false};  // kullanici "Yeniden dene" / reddedilen islem: hemen yeniden tara
    std::mutex watch_mu_;
    std::condition_variable watch_cv_;
    std::vector<std::string> watch_consent_ids_;  // watch_mu_ ile; izleyici is parcacigi okur
    std::vector<std::string> watch_manual_exes_;  // EF-X11: watch_mu_ ile; elle eklenen oyun EXE'leri
    bool startup_recovery_deferred_ = false;      // oyun/algilama nedeniyle acilis kurtarmasi bekliyor
    std::vector<contracts::SettingOperation> deferred_machine_offer_;  // ilk guvenilir tespite kadar bekleyen soru
    // Izleyici is parcaciginda toplanan kitaplik gorunumu (agir okumalar UI is parcaciginda yapilmaz).
    struct LibraryFacts {
        platform::games::LibraryScanResult library;
        bool has_rows = false;
        bool deep = true;  // oyun klasoru ve surucu profili okundu
        std::vector<ui::ViewModel::GameRowView> rows;
        std::vector<core::games::ProtectionStatus> protections;
        std::vector<core::games::LowLatencyProbe> vendor_ll;  // B-012: yalniz derin taramada okunur
        bool vendor_ll_read = false;
        std::vector<std::string> dirs, exes;  // dirs: izleyici anahtarlari (game_watch_key)
        std::vector<std::string> write_roots;  // F-47 yazma kokleri: yalniz launcher kurulum klasorleri
        std::vector<std::pair<std::string, std::string>> defender_folders;  // T11: (klasor, oyun adi); yalniz launcher kaynakli
        core::perf::GamePriorityInput game_priority;  // T09: scan_trusted/oyunlar/FACEIT
        std::map<std::string, std::wstring> names;
        std::map<std::string, InstalledGameInfo> info;
    };
    void start_game_watch();
    void watch_loop(std::uint64_t generation);
    std::shared_ptr<LibraryFacts> collect_library_facts(platform::games::LibraryScanResult library, const std::vector<std::string>& consent_ids, bool deep,
                                                        const std::function<bool()>& still_no_game,
                                                        platform::games::GameIconCache* icons);
    void apply_library_facts(const LibraryFacts& facts);
    void on_watch_result(std::uint64_t generation, core::games::WatchResult result);
    void refresh_watch_consent();
    void request_detection_rescan();
    void run_deferred_startup_recovery();
    void on_game_appeared();
    // F-47: oyun basina tam ekran iyilestirmeleri (kullanici kapsami, journal'li). Gercek durum her islemden
    // sonra registry + fixes journal'dan yeniden okunur; tahmini durum gosterilmez.
    void on_game_fullscreen(std::size_t index);
    void add_manual_game();                       // EF-X11: dosya secici + dogrulama + Evet/Hayir + tercih
    void remove_manual_game(std::size_t index);   // EF-X11: yalniz elle eklenen satir; oyun acikken yok
    void games_notice(core::i18n::StringId id, bool warning);
    void on_game_nvidia(std::size_t index);  // A1.1: oyun profili (broker, tek UAC, makine fixes journal'i)
    void refresh_game_fullscreen_states();
    void games_job_busy_notice();
    bool last_auto_active_ = false;
    // EXT-AC-02: calisan oyun varken ayar/temizlik/agir test baslatilmaz (docs handoff 07_ANTICHEAT)
    core::games::GateDecision gate(core::games::GuardedAction a) const;
    bool gate_or_block(core::games::GuardedAction a);  // reddedilirse UI'ya neden yazilir
    // Evet/Hayir sonrasi: guncel surec listesi + kitaplikla yeniden dogrula (onbellekteki son karara guvenmez).
    bool verify_or_block(core::games::GuardedAction a);
    std::string action_denied_key_;  // islem sinirinda reddin nedeni; sonraki guvenilir taramada temizlenir
    void push_gate();
    void push_history();  // REC-01/02: Islem Gecmisi sayfasi
    // R16: test gecmisi secimi (en fazla 2), karsilastirma ve kullanici baslatmali yerel disa aktarma.
    void select_history(std::size_t index);
    void export_history();
    void refresh_history_compare();
    // CHK: Oyun Hazirligi
    std::thread readiness_worker_;
    std::atomic<bool> readiness_cancel_{false};
    std::vector<core::readiness::PingStats> last_pings_;  // son ag testi (yeniden taramada korunur)
    void start_network_test();
    // T10: DNS cozucu karsilastirmasi (salt okuma; yalniz kullanici baslatir; oyun/baska test suruyorsa baslamaz).
    std::optional<core::net::DnsBenchReport> dns_report_;
    std::atomic<bool> dns_cancel_{false};
    void start_dns_bench();
    void refresh_dns_bench_rows();  // satirlari yerinde yeniler (tam tarama gerekmez)
    // T16: saglik testi (CPU 60 sn yuk + RAM deseni; ayar yazilmaz; yalniz kullanici baslatir; oyun/baska test suruyorsa baslamaz).
    std::optional<core::diagnostics::HealthReport> health_report_;
    std::atomic<bool> health_cancel_{false};
    int health_phase_ = 0, health_sec_ = 0;  // yalniz ana is parcacigi
    void start_health_test();
    void refresh_health_rows();
    // T06: fare rapor hizi olcumu (salt okuma; Performans/ayar yazmaz). Sonuc yalniz bellekte, kalici iddia degil.
    std::optional<core::measure::PollingResult> mouse_result_;
    void start_mouse_measure();
    void refresh_mouse_check();  // satiri yerinde yeniler (tam tarama gerekmez)
    void start_readiness_scan();
    void on_readiness_fix(std::size_t index);
    bool run_auto_fix(const core::readiness::Check& c);  // true: islendi (uygulandi veya kullanici Hayir dedi)
    // P12-B1: Panel "Duzelt" — otomatik duzeltilebilen TUM bulgular tek Evet/Hayir'da (sayfa degismez).
    void run_fix_all();
    core::recovery::JournalTx new_fix_tx() const;  // fixes motoru icin bos islem (kimlik, sid, zaman)
    // OPT-10 / F-48: fix.hags, fix.fast_startup -> broker (tek UAC), makine fixes journal'i
    void begin_run_marker(const char* source);  // EF-X22: kalici kosu-suruyor isareti
    void end_run_marker();
    bool recover_run_marker();  // eklendiyse true
    bool run_restore_point_job(const core::readiness::Check& c);
    bool run_standby_purge_job(const core::readiness::Check& c);  // B5: esik bulgusunun Duzelt'i (tek Evet/Hayir bakim isi)
    bool run_image_job(const core::readiness::Check& c, bool repair);  // EF-X02: bakim isi; tam geri alinamaz  // EF-X16: bakim isi; ayar geri alma degildir
    bool run_admin_fix(const core::readiness::Check& c);
    // Oyun Hazirligi "Geri al": kullanici fixes journal'i (yerel motor) veya HAGS (broker UndoFix). Evet/Hayir.
    void on_readiness_undo(std::size_t index);
    // "Mevcut degeri koru": dis degisiklik bekleyen fixes kaydi ayar YAZMADAN kapatilir (kullanici: yerel journal;
    // HAGS: broker KeepCurrent, tek UAC). Evet/Hayir.
    void on_readiness_keep(std::size_t index);
    // M2/L6: yonetici islemi (Fix/UndoFix/Recover) arka planda; UI thread'i UAC/pipe beklemesiyle bloklanmaz.
    // Sonuc katalog anahtariyla Oyun Hazirligi bildirimine yazilir. Ayni anda tek yonetici islemi.
    // check_id: islemin ait oldugu Oyun Hazirligi bulgusu (sonuc o satirda da gosterilir; RDY-08). Bos = genel.
    bool run_admin_async(core::ipc::RequestAction action, std::vector<contracts::SettingOperation> ops, std::string check_id = {});
    // Ayni yonetici is parcaciginda (admin_worker_) kullanici kapsamli is (ornek: kullanici fixes geri alma); sonuc
    // UI'da on_done ile islenir. admin_busy_ ayni kapidir (es zamanli ikinci is/UAC yok).
    bool run_admin_job(std::function<std::function<void()>()> job);
    void finish_admin_job();
    // Sonuc bildirimi: ozet kartinda + (check_id varsa) ilgili satirda; ekran okuyucuya UIA bildirimiyle duyurulur.
    // n / failed: sonuc cumlesindeki {n} / {m} (toplu duzeltme).
    void set_readiness_notice(std::string key, bool warning, std::string check_id = {}, int n = 0, int failed = 0);
    // Kullanici + makine fixes journal'i: ETKIN hedefler ve geri alinmasi dogrulanamamis / yarim kalmis hedefler.
    core::readiness::FixTargets fix_targets();
    void offer_machine_recovery(std::vector<contracts::SettingOperation> ops);  // M2: acilista Evet/Hayir
    // Spec 2026-11-12 E: uygulama ici kip penceresi yalniz ana pencere gorunurken sorulabilir (tepside / simge
    // durumunda soru sorulmaz, sessiz varsayilan Hayir'dir). Kullanicinin baslatmadigi sorular bununla ertelenir.
    bool can_ask() const;
    // Evet denmis ama henuz baslatilmamis kurtarma (oyun / yonetici isi / Performans islemi bitince baslar).
    std::vector<contracts::SettingOperation> pending_recovery_ops_;
    void run_pending_recovery();
    std::thread admin_worker_;
    bool admin_busy_ = false;
    // Performans anahtari/onayi: yonetici islemi surerken ikinci UAC/broker baslatilmaz (bildirim gosterilir).
    void perf_toggle();
    void perf_answer(bool yes);
    void perf_auto_restore();  // otomatik geri alma; yonetici islemi suruyorsa ertelenir
    bool perf_auto_restore_deferred_ = false;
    // OPT-07: tek tik sistem kontrolu
    std::unique_ptr<core::perf::SystemCheck> check_;
    std::thread check_worker_;
    std::atomic<bool> check_cancel_{false};
    void push_check();
    // INS-01: kalici test gecmisi (history.json; journal'dan ayri, sinirli)
    core::insights::History history_;
    void load_history();
    void save_history();
    // DIAG-11: en son Uygulama Testi sahnesinden calisma ani sinifi. Yalniz yayin kapisi acikken
    // doldurulur (kapaliyken bos -> bilesen raporu CPU/GPU icin cekimser kalir).
    core::diagnosis::BenchmarkRuntime bench_runtime_;
    core::diagnosis::BenchmarkRuntime latest_benchmark_runtime() const;
    void sync_bench_evidence();  // B10 inceleme: yeni gecmis kaydindan sonra kanit + gerekirse bilesen raporu tazeleme
    // Spec 2026-11-12 C2: en yeni denge kaydinin S3 adiminda (GPU yuk altinda) okunan PCIe linki; kayit yoksa /
    // link yuk altinda okunmadiysa bos (bilesen raporu "yuk altinda olculmedi" der).
    // Okuma yalniz kayit bugunku ekran kartiyla (vm_.gpu) alindiysa kullanilir; acilista envanter okunana kadar bostur
    // ve envanter gelince yeniden hesaplanir (core::measure::balance_pcie_for_gpu).
    std::optional<core::diagnosis::PcieLoadReading> bench_pcie_;
    core::measure::BalancePcieRecord latest_balance_pcie_record() const;
    std::optional<core::diagnosis::PcieLoadReading> latest_balance_pcie() const;
    std::vector<bool> check_selected_;  // UI gorunumu icin secim yansimasi
    core::perf::ReportCollector report_{10};  // OPT-08 (UI is parcacigi)
    // Acilista zorunlu guncelleme (spec 2026-11-11-zorunlu-guncelleme-design.md, ADR-028). Yalniz Ui rolunde ve surec
    // basina bir kez; gomulu yayin anahtari yoksa ag kullanilmaz. Kontrol bitene ve zorunlu guncelleme beklenirken sistem
    // yazan eylemler gate_or_block/verify_or_block'ta bekler; geri alma (update_restore_scope_) bu kurala bagli degildir.
    std::function<void()> release_instance_, reacquire_instance_;
    std::thread update_worker_, update_wait_, update_cleanup_, service_check_;
    std::atomic<bool> update_cancel_{false};
    std::atomic<bool> update_swap_pending_{false};  // run_swap Replaced; yeni surum henuz baslatilmadi
    bool update_commit_after_window_ = false;         // normal acilista onaylanmamis yedek: pencere acilinca onay
    bool update_started_ = false;
    bool update_restore_scope_ = false;
    std::optional<core::catalog::ReleaseManifest> update_manifest_;
    std::string update_envelope_;  // imzali zarf (ham): kurulu surumde hizmet her seyi yeniden dogrular
    struct ScopedFlag {
        bool& f;
        bool old;
        ScopedFlag(bool& flag, bool v) : f(flag), old(flag) { f = v; }
        ~ScopedFlag() { f = old; }
        ScopedFlag(const ScopedFlag&) = delete;
        ScopedFlag& operator=(const ScopedFlag&) = delete;
    };
    bool update_holds_writes() const;
    void update_startup_recover();      // onceki degisimin kalintilari (yedek/yarim dosya)
    void start_update_check();          // bir kez: manifest -> karar -> post
    void on_language_choice();  // denetim 5.2: Ayarlar "Dil · Degistir" (kip penceresi -> tercih -> hemen uygulanir)
    ui::SettingsView::LanguageNotice language_notice_ = ui::SettingsView::LanguageNotice::None;
    bool language_restart_pending_ = false;
    void on_manual_update_check();      // Ayarlar "Denetle": ayni denetim, el ile (yeni surum -> zorunlu kart)
    void run_update_check(bool manual);
    void on_update_now();               // indir, boyut + SHA-256, yaz + geri oku, degistir, yeni surumu baslat
    void launch_updated_exe();          // UI is parcacigi: kilidi birak, yeni EXE'yi baslat, saglik bekle
    void launch_service_updated_exe();  // kurulu surum: hizmet degistirdi; yeni EXE'yi baslat ve kapan (hizmet gozetir)
    void on_update_restore();           // Onceki ayarlara don (Evet/Hayir; mevcut Kapali akisi)
    void on_update_quit();
    void on_update_open_page();
    void on_update_repair_service();    // ADR-033: Evet/Hayir -> tek UAC (SetupKind::Repair) -> hizmet uzerinden yeniden dene
    void check_helper_service_running();  // kurulu surum: hizmet durmussa Evet/Hayir -> tek UAC Repair (politika + baslatma)
    void signal_update_healthy();       // yeni surum: pencere acildi -> saglik isareti + .old temizligi
    void push_update();
    bool update_fix_records_pending();  // geri alinmamis Oyun Hazirligi / oyun duzeltme kaydi var mi (salt okuma)
    void stop_update_threads();
};

}  // namespace ef::app
