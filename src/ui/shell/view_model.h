#pragma once
// UI-01/02: ekranin gosterdigi veri. Yalniz gercek okumalar doldurur; bos alan "okunamadi" olarak
// gosterilir, uydurma deger yoktur.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/cleanup/session.h"
#include "core/drivers/driver_updates.h"
#include "core/games/protection_gate.h"
#include "core/games/watcher.h"
#include "core/perf/apply_progress.h"
#include "core/games/upscaler.h"
#include "core/startup/session.h"
#include "core/inventory/disk_space.h"
#include "core/insights/history.h"
#include "core/games/game_guide.h"
#include "core/games/vendor_guide.h"
#include "core/games/game_icon.h"
#include "core/insights/history_compare.h"
#include "core/insights/peer.h"
#include "core/measure/view.h"
#include "core/metrics/live.h"
#include "core/net/server_ping.h"
#include "core/net/speed_test.h"
#include "core/perf/controller.h"
#include "core/perf/report_collector.h"
#include "core/diagnosis/components.h"
#include "core/perf/system_check.h"
#include "core/recovery/history_view.h"
#include "core/readiness/readiness.h"
#include "core/settings/autostart.h"
#include "core/update/update_check.h"

namespace ef::ui {

struct FindingVM {
    std::string text_key;  // katalog anahtari (health.*, update.*, diag.*)
    bool warning = true;
};

// Performans anahtari gorunumu (PerfController'dan; UI is parcaciginda guncellenir).
// UI-10: anahtarin degistirebilecegi ayar grubu (panel satiri).
struct GroupVM {
    std::string key, desc_key;
    bool known = false;        // onizleme okundu mu
    bool will_change = false;  // bu sistemde degisecek mi
    bool selected = true;      // kullanicinin Evet/Hayir secimi
    std::vector<std::string> field_keys;  // EF-X25 T03: onayda uygulanacak alanlar (bos = yalniz baslik)
    std::vector<core::perf::FieldDetail> details;  // P2: olculmus program satirlari (ad, islemci %, bellek); panelde ve onayda
    core::perf::Evidence evidence = core::perf::Evidence::Documented;  // T04: durust kanit etiketi
    core::perf::Risk risk = core::perf::Risk::Low;                     // T04: risk etiketi
    bool advanced = false;     // T04: varsayilani Hayir ve kullanici acikca Evet demedi -> "Gelismis" altinda katli
    bool no_effect = false;    // P4: bu PC'deki kanit deneyinde "Fark yok" (Arena); yalniz "Ayrintilar" altinda gosterilir, plan degismez
};

struct PerfView {
    core::perf::Phase phase = core::perf::Phase::Off;
    core::perf::Notice notice = core::perf::Notice::None;
    std::vector<std::string> item_keys;  // plan ogeleri (katalog anahtari)
    bool quality = false, reboot = false, admin = false;
    bool reboot_pending = false;  // OPT-10: HAGS yazildi/geri alindi, yeniden baslatma bekliyor
    std::size_t startup = 0;
    std::size_t done = 0, total = 0;  // gercek adim sayisi
    std::size_t active = 0;
    bool switch_on = false;
    bool external_conflict = false;  // kurtarma: "Mevcut degeri koru" sunulur
    // Koruma Modu (T07): acilista sapmis ayar sayisi; "Yeniden uygula" / "Boyle kalsin" sunulur (sessiz uygulama yok).
    std::size_t drift_n = 0;
    bool drift_update = false;   // Windows yapisi son acilistan beri degisti
    bool drift_confirm = false;  // onay asamasi sapma yeniden uygulamasi icin
    std::vector<GroupVM> groups;  // UI-10
    // Saha 2026-11-15: kurtarma asamasinda geri alinamamis ayarlar (journal'dan; ad + neden katalog anahtari, hizmet adi).
    struct RecoveryItemVM {
        std::string name_key, reason_key, detail;
        bool operator==(const RecoveryItemVM&) const = default;
    };
    std::vector<RecoveryItemVM> recovery_items;
    // T18: uygulama ilerlemesi ("Adim i/n - grup" + asama) ve bitis ozeti; sayilar motorun/Outcome'un gercek degerleridir.
    bool progress_has_step = false;
    std::size_t progress_step = 0, progress_steps = 0;
    std::string progress_group;  // grup katalog anahtari (baslik metni UI'da cozulur)
    core::perf::ApplyPhase progress_phase = core::perf::ApplyPhase::Prepare;
    std::size_t verified = 0;  // bu oturumda uygulanip geri okunarak dogrulanan ayar sayisi (0 = ozet yok)
    bool tailored = false;     // cihaz sinifi bir varsayilani degistirdi (onay ekraninda sade not)
};

// UI-06: Ayarlar sayfasi
struct ConsentRowVM {
    std::string id;    // normallestirilmis oyun kimligi
    std::string name;  // gorunen ad
    bool allow = false;
};
struct SettingsView {
    bool autostart = false;
    bool autostart_known = true;  // okunamadiysa anahtar devre disi
    // ADR-030: son yazma / silme / okuma basarisizsa nedeni (anahtar kapali gorunur ve neden satiri yazilir).
    core::settings::AutostartFailure autostart_failure = core::settings::AutostartFailure::None;
    bool autostart_notice = false;  // spec G: ilk acilis bildirimi bu oturumda gosterilsin
    // UI-13: Performans Windows animasyonlarini kapattiysa kaydedilmis ozgun deger (uygulama hareketi bunu izler).
    std::optional<bool> system_motion_original;
    bool app_animations = true;  // UI-11/R33: varsayilan acik; Windows tercihi kapatirsa yine hareket yok.
    bool strip_enabled = true;   // Zehir yesili: RGB isik seridi hareketi (Preferences::strip_enabled)
    bool cleanup_hint = true;    // R-C2: oyun kapaninca temizlik bildirimi (Preferences::cleanup_hint_after_game)
    std::vector<ConsentRowVM> games;

    // R21: destek paketi oturum durumu (tercih degil; paket yalniz kullanici isterse ve secilen yola yazilir).
    bool bundle_include_dumps = false;  // varsayilan kapali; kullanici acikca isaretler
    bool bundle_busy = false;
    bool bundle_done = false;
    bool bundle_failed = false;
    std::wstring bundle_path;  // basarili kayit yolu (gorunen metin)
    // Uyumluluk B8 (bosluk 12): bu cihaz yapilandirmasi (Windows build + oyun GPU ureticisi) gercek cihazda dogrulanan bir
    // kombinasyon degil. Engellemeyen not; hicbir ozellik kapanmaz. Dogrulama okunmadan (yukleniyor) false kalir.
    bool device_unverified = false;
    bool device_os_unverified = false, device_gpu_unverified = false;

    // El ile Ecu Fps guncelleme denetimi (Ayarlar). Yeni surum bulunursa zorunlu guncelleme karti acilir (ADR-028);
    // burada yalniz "guncel" / "denetlenemedi" / "bu derlemede kapali" sonucu satir olarak yazilir.
    enum class UpdateCheck { Idle, Checking, UpToDate, Failed, Unavailable };
    UpdateCheck update_check = UpdateCheck::Idle;
    // Denetim 5.2: uygulama dili. ui_language = kayitli tercih ("system" | "tr-TR" | "en-US"); active_tr = su an acik
    // windows_tr = Windows goruntu dili Turkce mi ("Windows diline uy (...)" etiketi).
    std::string ui_language = "system";
    bool windows_tr = true;
    enum class LanguageNotice { None, Restart, SaveFailed };
    LanguageNotice language_notice = LanguageNotice::None;
    bool language_restart_pending = false;  // kayitli tercih acik dilden farkli (hata satiriyla birlikte de gosterilir)

    // R20: her seyi geri al ve verileri sil. Onay olmadan hicbir sey yapilmaz; veri ancak dogrulanmis
    // geri yuklemeden sonra silinir.
    enum class UninstallPhase { Idle, Confirming, Running, Done };
    UninstallPhase uninstall_phase = UninstallPhase::Idle;
    bool uninstall_restored_all = false;
    bool uninstall_data_removed = false;
    std::size_t uninstall_unrestored = 0;
    std::wstring uninstall_note;                // yalniz Idle: oyun calisiyor / baska islem suruyor nedeni
    std::vector<std::wstring> uninstall_notes;  // sonuc notlari (yerellestirilmis)
};

struct ViewModel {
    bool system_loading = true;
    std::string os, cpu, gpu, ram, board;  // utf-8; bos = okunamadi
    std::string gpu_driver;  // gecmis kaydi kosulu; yalniz okunabildiyse dolu
    // EF-X24.02: `gpu` satirlariyla ayni sira; ad + LUID (canli kutucukta kimlik eslesmesi). Bos = envanter yok.
    std::vector<core::metrics::LiveGpuCard> gpu_cards;
    // Panel Sistem karti: birincil ekran ozeti ("AD · 2560×1440 · 165 Hz"; core::readiness::display_summary). Bos = okunamadi.
    std::string display;
    bool findings_loading = true;
    std::vector<FindingVM> findings;
    // OPT-07: tek tik sistem kontrolu
    struct CheckRow {
        std::string text_key;  // bulgu metni (otomatik: plan basligi; rehberli: health.* vb.)
        bool automatic = false, selected = false;
    };
    struct CheckView {
        core::perf::CheckPhase phase = core::perf::CheckPhase::Idle;
        std::vector<CheckRow> rows;
        std::size_t done = 0, total = 0;
        std::optional<core::perf::CheckReport> report;
    } check;
    bool components_loading = true;                          // DIAG-05 bilesen raporu
    std::vector<core::diagnosis::ComponentFinding> components;
    std::string diagnosis_gate_key;  // BM-02: yayin kapisi durumu (diag.gate.*; bos = gosterilmez)
    std::string version;  // urun surumu
    std::string build_id;  // derleme kimligi (ayni surumde derlemeleri ayirir)
    // ADR-033 / spec 8: Ui kurulum yeri disinda calisiyor (eski tasinabilir Run kaydi, 0.2.x'ten zorunlu guncelleme):
    // ust menunun altinda engellemeyen "Kurulum gerekli · Kur" seridi.
    bool install_offer = false;
    // Sahip raporu 2026-11-13: yonetici isleri yardimci hizmetle UAC'siz yurur (kurulu kopya + yonetici hesabi + hizmet
    // calisiyor/dogru; platform::helper_service_ready, baglanmaz). true iken hizmet yolundaki metinlerin ".svc" esi
    // (core::service::admin_copy) gosterilir; UAC-yalniz roller (temizlik, yakalama, kurulum, Dosyadan kur) degismez.
    bool admin_without_uac = false;
    bool game_running = false;  // EXT-AC-02: oyun calisirken mutasyon/temizlik/agir test baslatilmaz
    // Oyun tespiti: Unknown/Failed/Incomplete iken "oyun yok" denemez; korumali islemler kapali.
    core::games::DetectionState game_detection = core::games::DetectionState::Unknown;
    bool game_gate_blocked = false;              // oyun VAR veya tespit guvenilir degil
    std::string game_block_reason_key;           // gate.game_running / gate.detection_* / gate.library_error (bos = engel yok)
    std::string game_block_title_key;
    bool game_block_can_retry = false;           // nedeni yeniden taranabilir (kitaplik/surec/bayat)
    bool library_untrusted = false;              // kitaplik okunamadi: bos liste "oyun yok" yazilmaz
    bool detection_retrying = false;             // "Yeniden dene" gercek taramayi bekliyor
    struct LibraryIssueView {
        std::string key;   // gate.library.*
        std::string path;  // ilgili yol (bos olabilir)
    };
    std::vector<LibraryIssueView> library_issues;  // yalniz kitaplik hatasinda dolu
    struct LibraryProviderView {
        std::string provider;    // steam/epic/gog/ubisoft/ea/battlenet/riot/xbox/manual
        std::string status_key;  // games.source.* (EF-X11)
        std::size_t games = 0;
    };
    std::vector<LibraryProviderView> library_providers;  // bos = henuz taranmadi
    // Oyunlar sayfasi: gercek kurulu oyunlar + uyumluluk kapisi (canli olcum nedeni)
    struct GameRowView {
        std::string name, launcher, version;
        std::string dir_key;      // normalize kurulum klasoru (izleyici eslesmesi)
        bool running = false, config_read = false;
        std::string capture_key;  // gate.capture_* (canli olcum neden kapali)
        std::string pro_slug;     // GUIDE-01: prosettings.net/games/<slug>/ (tanınan oyun; bos = yok)
        std::string protection;   // F-60: bilinen koruma sistemleri, ", " ile (bos = matriste yok)
        bool risk_scanned = false;              // F-59: baslatma secenegi/ayar dosyasi okunabildi mi
        std::vector<std::string> risk_keys;     // risk.* kodlari (bos + risk_scanned = temiz)
        bool upscalers_scanned = false;                          // F-57: oyun klasoru okunabildi mi
        std::vector<core::games::UpscalerAsset> upscalers;       // bulunan DLSS/FSR/XeSS DLL'leri
        // F-47: oyun EXE'si basina tam ekran iyilestirmeleri (HKCU AppCompatFlags\Layers)
        // A1.1: NVIDIA oyun profili. Bos nv_exe: EXE bilinmiyor veya NVIDIA yok (satir yazilmaz).
        std::string guide_store, guide_app_id, guide_build;  // EF-X12: rehber girdisi (salt okuma)
        std::vector<std::string> guide_verified_builds;
        core::games::GuideRenderer guide_renderer = core::games::GuideRenderer::Unknown;  // yalniz bu oturumda gozlenen
        std::string nv_exe;
        bool nv_has_profile = false;   // surucude bu oyunun profili var
        bool nv_external = false;      // deger zaten acikca en yuksek performans (bu uygulamadan degil)
        bool nv_applied = false;       // bu uygulamanin kaydi var (Geri al sunulur)
        bool nv_partial = false;       // iki islemden yalniz biri etkin
        bool nv_attention = false;     // kesilmis / dogrulanamamis kayit
        bool nv_conflict = false;      // deger sonradan degismis: "Mevcut degeri koru"
        std::string nv_record_exe;     // kaydin ait oldugu EXE (Geri al/Koru); Uygula yalniz nv_exe kullanir
        bool nv_read_failed = false;   // surucu okunamadi ("profil yok" degil)
        std::vector<std::string> nv_candidates;  // bu oyun icin gorulen EXE adlari: kayit baska adla ise Geri al kaybolmaz
        bool fs_known = false;      // oynanabilir EXE yolu cozuldu
        bool fs_read_ok = true;     // kayit degeri okunabildi
        bool fs_disabled = false;   // DISABLEDXMAXIMIZEDWINDOWEDMODE jetonu kayitli
        bool fs_applied = false;    // fixes journal'inda bu hedef icin kapatilmamis kayit var
        std::string fs_target;      // "appcompat:<tam exe yolu>"
        std::string manual_exe;     // EF-X11: elle eklenen oyunun kayitli EXE yolu (bos = launcher kaynakli)
        bool manual_missing = false;  // EF-X11: kayitli EXE artik yok (satir yine de kaldirilabilir)
        // Spec 2026-11-12 §4: oyunun gercek ikonu (EXE ikon kaynagi / Steam kitaplik onbellegi). Bos = ikon yok ->
        // harf kutusu cizilir (genel/kabuk ikonu asla). Izleme is parcaciginda yuklenir; pikseller degismezdir.
        core::games::ImageRef icon;
    };
    bool games_deep_deferred = false;  // oyun acikken klasor/surucu okumasi ertelendi
    bool games_loading = true;
    bool protections_loading = true;                       // F-60: servis sorgusu
    std::vector<core::games::ProtectionStatus> protections;  // yalnız kurulu/çalışan servisler
    // B-012 (R-B7): AMD/Intel dusuk gecikme rehberi girdileri (derin taramada gozlenen runtime durumu; NVIDIA yok).
    // Bos = AMD/Intel bagdastirici yok ya da henuz okunmadi -> satir cizilmez.
    std::vector<core::games::LowLatencyProbe> vendor_ll;
    // CHK: Oyun Hazirligi
    struct ReadinessView {
        bool loading = true, net_running = false;
        bool dns_running = false;    // T10: DNS karsilastirma olcumu suruyor (salt okuma; yalniz DNS sorgusu)
        bool health_running = false;  // T16: saglik testi suruyor (CPU 60 sn + RAM; ayar yazilmaz)
        bool mouse_running = false;  // T06: fare rapor hizi olcumu suruyor (5 sn; salt okuma)
        bool net_available = false;  // ag olcumu bagli mi (degilse dugme gosterilmez)
        std::vector<core::readiness::Check> checks;
        // bulgu basina geri alma durumu: Undo = fixes journal'inda ETKIN duzeltme ("Geri al"); Retry = geri alinmasi
        // dogrulanamamis / yarim kalmis kayit ("Geri al / Yeniden dene" + durum satiri)
        std::vector<core::readiness::UndoState> undo;
        bool fix_running = false;    // yonetici duzeltmesi/geri alma/kurtarma suruyor (UAC + broker; arka planda)
        std::string notice_key;      // son duzeltme/geri alma sonucu (katalog anahtari; bos = yok)
        bool notice_warning = false; // basarisiz/dogrulanamadi/izin yok/yapilacak sey yok: uyari rengi
        std::string notice_check_id; // RDY-08: bildirimin ait oldugu bulgu (Check::id); bos = genel (yalniz ozet karti)
        int notice_n = 0, notice_failed = 0;  // P12-B1: sonuc cumlesindeki {n} (duzeltilen) ve {m} (duzeltilemeyen)
    } readiness;
    // Islem Gecmisi (REC-01/02): kalici journal kayitlari
    struct HistoryPageView {
        bool loading = true;
        core::recovery::HistoryView view;
        // R16: yerel test gecmisi (en yeni once; secim/compare icin sinirli) + kullanici kontrollu disa aktarma
        static constexpr std::size_t kMaxHistoryTests = 20;  // kMaxHistoryTestRows ile ayni olmali
        std::vector<core::insights::HistoryEntry> tests;
        std::size_t tests_total = 0;
        std::vector<std::size_t> selected;  // tests indeksleri; en fazla 2 (tiklanma sirasi)
        std::optional<core::insights::HistoryComparison> comparison;
        core::insights::CohortComparison cohort;      // EF-X19: tekrarli kosu farki
        std::vector<double> compare_base_fps, compare_current_fps;  // ayni eksen; yalniz karsilastirilabilir kosu
        bool export_enabled = true;  // veri yokken veya kaldirma surerken devre disi
        bool export_done = false, export_failed = false;
        std::wstring export_path;    // basarili kayit yolu (gorunen metin)
        std::size_t export_bytes = 0;
        std::string export_error_key;  // basarisizlik nedeni (katalog anahtari; bos = yok)
        // R15: akran karsilastirmasi. Uygulama bugun bos yerel havuz verir; durum daima Insufficient'dir.
        core::insights::PeerQuery peer_query;
        core::insights::PeerComparison peer;
        // B13 gun basliklari (Bugun / Dun) icin "simdi" (Unix ms). 0 = sistem saati; anlik goruntu testi sabit zaman verir.
        std::int64_t now_unix_ms = 0;
    } history;
    std::vector<GameRowView> games;
    std::string games_notice_key;      // F-47: son oyun islemi sonucu (katalog anahtari; bos = yok)
    bool games_notice_warning = false; // basarisiz/dogrulanamadi: uyari rengi
    PerfView perf;
    core::measure::MeasureState measure;  // UI-03
    // P3: Darbogaz kartindaki "Raporu kaydet" oturum durumu (tercih degil; rapor yalniz kullanici isterse Masaustune yazilir).
    struct BalanceReportView {
        bool busy = false, done = false, failed = false;
        std::wstring path;  // basarili kayit yolu (gorunen metin)
    } balance_report;
    SettingsView settings;                // UI-06
    bool show_welcome = false;            // UI-08: ilk acilis karsilamasi
    core::metrics::LiveSnapshot live;     // UI-09: canli kaynaklar (eksik deger = olculemedi + neden)
    // LED tepe izi, kutucuk sirasiyla (CPU, RAM, GPU, Disk, Ag) yuzde; < 0 = iz yok. Yalniz MainWindow yazar
    // (update_live_peaks); uygulamanin modelinde bos kalir ve set_view onu silmez.
    static constexpr std::size_t kLivePeakCount = 5;
    std::array<double, kLivePeakCount> live_peak_pct{-1, -1, -1, -1, -1};
    // Panel kutucuk ince grafigi: son kLiveHistoryMax canli ornegin kutucuk yuzdesi (ayni sira; ag: indirme / baglanti).
    // Olculemeyen ornek NaN'dir (grafikte bosluk; sayi uydurulmaz). Yalniz MainWindow yazar (push_live_history, set_live);
    // set_view onu silmez. 2'den az gecerli ornek varken grafik cizilmez.
    static constexpr std::size_t kLiveHistoryMax = 60;
    std::array<std::vector<float>, kLivePeakCount> live_history{};
    // Zehir yesili Task 12: internet hiz testi (yalniz oturum; diske yazilmaz). Sayilar yalniz phase == Done iken
    // sonuctur; calisirken live_mbps o anki parcanin olculen hizidir (sahte deger yok).
    struct SpeedView {
        core::net::SpeedPhase phase = core::net::SpeedPhase::Idle;
        core::net::SpeedFail fail = core::net::SpeedFail::None;
        double live_mbps = 0;
        std::optional<double> down, up, latency;
        std::optional<double> down_loaded, up_loaded;  // R-C1: yuk altinda medyan gecikme (ms); yetersiz ornekte yok
        std::uint64_t bytes = 0;
        bool running = false;
        // R-C4: oyun sunucusu ping (kullanicinin yazdigi IPv4; yalniz olculen deger). Hiz testi sifirlamasindan etkilenmez.
        std::string server_ip;  // bos = adres girilmedi
        core::net::ServerPingState server_state = core::net::ServerPingState::None;
        bool server_running = false;
        int server_sent = 0, server_received = 0;
        double server_avg_ms = 0, server_jitter_ms = 0;
    } speed;
    // Acilista zorunlu guncelleme (spec 2026-11-11-zorunlu-guncelleme-design.md). Varsayilan UpToDate: serit ve
    // guncelleme ekrani yok (smoke/test ve kontrol yapmayan roller). Checking/CheckFailed/VerifyFailed: ust serit;
    // Mandatory/NotWritable: tam ekran guncelleme karti. Ilerleme yalniz gercek indirilen bayttan.
    struct UpdateView {
        enum class Step { Idle, Downloading, Installing, Repairing };
        core::update::UpdateOutcome outcome = core::update::UpdateOutcome::UpToDate;
        std::string new_version;
        double progress01 = 0;
        std::uint64_t bytes = 0, total = 0;
        bool rollback_available = false;  // Performans acik veya etkin degisiklik kaydi: "Onceki ayarlara don"
        Step step = Step::Idle;
        std::string notice_key;  // serit/kart bildirimi (update.rolled_back, update.updated, update.download_failed...)
        std::string notice_version;  // notice_key icindeki {v}
        // ADR-033 / spec 7: kurulu surumde yardimci hizmet yok/bozuk -> ana eylem "Yardimci hizmeti onar" (tek UAC).
        bool service_unavailable = false;
        // K27: kurulu surum + NotWritable (standart hesap): govde "dosyayi degistir" degil "indirip kurulumu calistir" der.
        bool installed = false;
    } update;
    bool storage_loading = true;          // DIAG-08
    std::vector<core::inventory::VolumeAdvice> storage;
    // WIN-08: temizlik sayfasi (CleanupSession gorunumu)
    struct CleanupRow {
        core::cleanup::CleanupCategory category;
        core::cleanup::PreviewStatus status = core::cleanup::PreviewStatus::Error;
        std::size_t files = 0, reparse = 0;
        std::uint64_t bytes = 0;
        bool needs_admin = false, selectable = false, selected = false;
        std::uint64_t allocated = 0;  // EXT-CLN-01 (sonda: mevcut kume baslatmalarinin sirasi korunur)
        bool partial = false;         // EXT-CLN: kategori taramasi butce/iptal ile yarim kaldi
        bool admin_gate = false;      // WIN-08 ek: tiklaninca UAC istenir (henuz yukseltilmis tarama yok)
    };
    static constexpr std::size_t kMaxCleanupVolumes = 8;
    static constexpr std::size_t kMaxListedFiles = 30;
    struct CleanupView {
        core::cleanup::SessionPhase phase = core::cleanup::SessionPhase::Idle;
        std::vector<CleanupRow> rows;
        std::uint64_t selected_bytes = 0, selected_allocated = 0;
        std::size_t done = 0, total = 0;
        std::optional<core::cleanup::CleanupReport> report;
        // EXT-CLN-01/03: birim secimi + kimlik, silmeden once dosya listesi
        std::vector<std::wstring> volumes;           // "C:\" ... (yerel sabit, en fazla kMaxCleanupVolumes)
        std::vector<std::optional<std::uint64_t>> volume_free;  // ayni sirada bos alan (bilinmiyorsa nullopt)
        std::wstring volume;                         // secilen birim
        core::cleanup::VolumeId volume_id;           // planin okudugu kimlik
        std::optional<core::cleanup::CleanupCategory> files_open;
        std::vector<core::cleanup::ListedFile> files;  // en buyukler once
        std::size_t files_total = 0;
        bool volume_changed = false;                 // Failed nedeni: birim degisti/dogrulanamadi
        bool scan_partial = false;                   // EXT-CLN: tarama butcesi doldu (sayilar kismi)
        bool scan_cancelled = false;                 // EXT-CLN: kullanici taramayi iptal etti
        bool admin_pending = false;                  // WIN-08 ek: UAC/yukseltilmis tarama suruyor
        std::string admin_notice_key;                // WIN-08 ek: UAC reddi/baslatma hatasi (katalog anahtari)
        std::string gate_reason_key;                 // son calistirma oyun/algilama kapisina takildi
        bool maybe_partial = false;                  // yardimci yarida kaldi: bazi dosyalar silinmis olabilir
        bool any_selected = false;                   // T15: bellek satiri dahil herhangi bir secim (Temizle dugmesi kapisi)
    } cleanup;
    // WIN-09: baslangic ve arka plan sayfasi (StartupSession gorunumu)
    struct StartupRowView {
        std::string name;
        std::string_view reason_key;
        core::adapters::startup::Advice advice = core::adapters::startup::Advice::NoAdvice;
        bool enabled_now = true, desired = true, editable = false, admin = false, ours = false;
    };
    struct ServiceRowView {
        std::string name;  // kanonik SCM adi (secim anahtari; WIN-10)
        std::string display_name;
        std::string_view breaks_key;
        bool present = false, running = false;
        bool selected = false;  // Performans acikken oturumluk durdurulsun mu (kalici tercih)
        bool editable = false;  // su an durdurulabilir (var + calisiyor; asama uygun)
    };
    // OPT-08: Performans Acik oncesi/sonrasi kaynak raporu
    struct BeforeAfterView {
        core::perf::ReportCollector::Stage stage = core::perf::ReportCollector::Stage::Idle;
        std::size_t after_n = 0, target = 10;
        std::optional<core::perf::BeforeAfterReport> report;
    } before_after;
    struct StartupView {
        core::startup::StartupPhase phase = core::startup::StartupPhase::Idle;
        std::vector<StartupRowView> rows;
        std::vector<ServiceRowView> services;
        std::size_t pending = 0;
        std::optional<core::startup::ChangeResult> last;
    } startup;
    // Spec 2026-11-12 H (tespit): Guncellemeler sayfasi. scan: son Windows Update surucu aramasinin sonucu (oturum ici;
    // diske yazilmaz). Uretici satirlari envanterden (salt okuma); okunamayan alan bos -> "—" (uydurma yok).
    struct UpdatesGpu {
        std::string name;             // UTF-8
        std::uint32_t vendor_id = 0;  // PCI
        std::string driver_version;   // kurulu surucu (UMD); bos = okunamadi
    };
    struct UpdatesView {
        core::drivers::ScanView scan;
        std::string last_check;  // son tamamlanan denetimin yerel tarih + saati ("15.11.2026 14:32"); bos = bu oturumda denetlenmedi
        std::vector<UpdatesGpu> gpus;  // yazilim / uzak bagdastiricilar haric; ayni uretici bir kez
        std::string cpu_vendor;        // CPUID ("GenuineIntel" / "AuthenticAMD")
        std::string board_vendor, board_model, bios_version, bios_date;  // SMBIOS (bios_date "MM/DD/YYYY")
        // T17: Aygit Yoneticisi sorun kodlu aygitlar (salt okuma). enumerated=false -> "okunamadi" (sorun yok denmez).
        core::inventory::ProblemDeviceReport problem_devices;
        bool problem_devices_loaded = false;  // okuma denendi (false: henuz denenmedi -> blok yok)
        // Spec H2: tek surucu kurulumu (yukseltilmis yardimci). Bir seferde tek kurulum; sonuc bir sonraki kuruluma ya da
        // oturum sonuna kadar ust kartin altinda durur. Sonuc yalniz yardimcinin yanitindan gelir (yanit yoksa "unknown").
        struct InstallView {
            bool running = false;
            std::string target;      // "wuinst:<UpdateID>:<rev>" (kurulan / son kurulan satir)
            std::string name;        // satir adi (UTF-8)
            std::string notice_key;  // updates.install.*; bos = sonuc yok
            bool warn = false, reboot = false, no_restore_point = false, previous_interrupted = false;
            std::int32_t hresult = 0;
            // ADR-032 (hedef "vdrv:..."): paketin cikis kodu ve geri okunan kurulu surum (bos = okunamadi).
            std::uint32_t exit_code = 0;
            std::string version;
        } install;
        // ADR-032: "Dosyadan kur" hazirligi (kullanicinin sectigi resmi paketin uygulama klasorune kopyasi ve imza on
        // denetimi). Kurulum basladiginda install.running devralir; hazirlik basarisizsa sonuc install.notice_key'dedir.
        struct LocalView {
            enum class Stage { None, Copying, Verifying };
            Stage stage = Stage::None;
            std::size_t gpu = 0;              // gpus indeksi (satir)
            std::uint64_t done = 0, total = 0;  // kopyalanan / toplam bayt (total 0 = bilinmiyor)
            std::string name;                 // secilen dosyanin adi (UTF-8)
        } local;
    } updates;
};

// EF-X20 K15 bulgusu: arka plan is parcaciginda doldurulan bulgu/bilesen raporu ana modele tek yerden aktarilir.
// Yayin kapisi anahtari da tasinir; aksi halde "Bilesen raporu" karti kapi kapali metnini gostermez (BM-02).
inline void adopt_component_report(ViewModel& vm, std::vector<FindingVM> findings,
                                   std::vector<core::diagnosis::ComponentFinding> components, std::string gate_key) {
    vm.findings = std::move(findings);
    vm.components = std::move(components);
    vm.diagnosis_gate_key = std::move(gate_key);
    vm.components_loading = false;
    vm.findings_loading = false;
}

PerfView make_perf_view(const core::perf::PerfController& c);

}  // namespace ef::ui
