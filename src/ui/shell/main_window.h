#pragma once
// UI-01: ana pencere (Win32). Direct2D ile cizer; olay odakli: yalniz degisiklik/animasyon
// varken yeniden cizer, bosta zamanlayici calismaz.

#include <windows.h>

#include <vector>

#include <functional>
#include <optional>
#include <memory>
#include <string>

#include "core/i18n/catalog.h"
#include "ui/accessibility/uia.h"
#include "ui/lifecycle/activity.h"
#include "ui/render/graphics.h"
#include "ui/shell/screen.h"

namespace ef::ui {

// Test modu (EF_TEST_BACKGROUND=1): pencere etkinlestirilmez/one alinmaz. Urun davranisi degisken yokken ayni.
bool test_background_mode() noexcept;


class MainWindow {
public:
    static constexpr UINT kMsgModel = WM_APP + 1;  // lParam: new ViewModel* (sahiplik pencereye gecer)

    struct Options {
        std::wstring window_class;
        std::wstring title;
        UINT activate_message = 0;  // tek ornek: mevcut pencereyi one getir
        bool smoke = false;         // 200 ms sonra kapanir (test)
        bool quit_on_destroy = true;  // tepside: pencere kapanir, surec yasar (UI-06)
    };

    static std::unique_ptr<MainWindow> create(HINSTANCE inst, const Theme& theme, const core::i18n::Catalog& cat,
                                              Options opts);
    ~MainWindow();
    HWND hwnd() const { return hwnd_; }
    // Gercek WM_PAINT maliyeti (ID2D1DCRenderTarget yolu); ekran disi snapshot suresi bunun yerine gecmez.
    struct PaintStats {
        unsigned long long count = 0;
        double total_ms = 0, max_ms = 0;
        unsigned long long live = 0;  // bunlardan kaci canli test gorunumu hizli yolu (yalniz canli bolge cizildi)
    };
    const PaintStats& paint_stats() const { return paint_stats_; }
    void reset_paint_stats() { paint_stats_ = {}; }
    std::size_t image_cache_size() const;  // test/denetim: cizim katmaninin tuttugu hedef bitmap sayisi
    Activity& activity() { return activity_; }

    static constexpr UINT kMsgRun = WM_APP + 2;    // lParam: new std::function<void()>* (UI is parcaciginda calisir)

    // Baska is parcacigindan guvenli: modeli kopyalar ve pencereye postalar (perf gorunumu korunur).
    static void post_model(HWND hwnd, const ViewModel& vm);
    static constexpr UINT kMsgFindings = WM_APP + 3;  // lParam: new std::vector<FindingVM>*
    // Baska is parcacigindan guvenli: yalniz bulgulari yeniler (islem sonrasi tazeleme).
    static void post_findings(HWND hwnd, const std::vector<FindingVM>& f);
    // Baska is parcacigindan guvenli: kapanisi UI is parcaciginda calistirir.
    static void post_run(HWND hwnd, std::function<void()> fn);
    // UI is parcacigi: anahtar gorunumunu gunceller ve gerekli animasyonlari baslatir.
    void set_perf(const PerfView& pv);

    std::function<void()> on_toggle;        // anahtar
    std::function<void(bool)> on_answer;    // Evet (true) / Hayir (false)
    std::function<void(core::measure::TestSource)> on_test_start;  // UI-03: MEAS/BENCH baglanir
    std::function<void(std::size_t)> on_storage_select;             // BENCH-05: depolama birimi secimi (0 = yapma)
    std::function<void(core::measure::TestMode)> on_test_mode;      // BENCH v2: Hizli / Tam
    std::function<void(core::measure::BuiltinScope)> on_test_scope;  // spec 2026-11-12 A5: Genel test / Denge testi
    std::function<void()> on_test_stop;
    // R14/INS-04: hedef FPS tercihi (0 = hedef yok; 30..480). Yalniz olcumu yorumlar.
    // R13/INS-03: uzun test (isinma) kullanici eylemleri; otomatik baslatma yok.
    std::function<void()> on_endurance_start;
    std::function<void()> on_endurance_stop;
    std::function<void()> on_proof_start;  // P1: Performans etkisini olc (App tek Evet/Hayir sorar)
    std::function<void()> on_proof_stop;
    std::function<void()> on_balance_report;  // P3: Darbogaz "Raporu kaydet" (destek paketi yolu; Masaustune yazar)
    std::function<void(bool)> on_live_view;   // spec 2026-11-12 A4: "Canli test ekrani" istenen yeni deger (kalici)
    std::function<void()> on_speed_start;     // Task 12: internet hiz testi (Evet/Hayir + oyun kapisi App'te)
    std::function<void()> on_speed_server_ping;  // R-C4: oyun sunucusu ping (adres kip penceresinde sorulur; App'te)
    std::function<void()> on_speed_stop;
    // Spec 2026-11-12 H: Guncellemeler — Windows Update surucu aramasi (oyun kapisi App'te), durdurma ve resmi uretici
    // sayfasi (App URL izin listesini uygular; yalniz kullanici tiklayinca).
    std::function<void()> on_updates_check;
    std::function<void()> on_updates_cancel;
    std::function<void(const std::string&)> on_updates_link;
    std::function<void(std::size_t)> on_updates_install;  // spec H2: scan.updates indeksi
    std::function<void(std::size_t)> on_updates_local;    // ADR-032: gpus indeksi ("Dosyadan kur" / kopya surerken "Durdur")
    void set_updates(const ViewModel::UpdatesView& v);
    // Zorunlu guncelleme karti (Evet/Hayir ve oyun kapisi App'te; geri alma guncellemeye bagli degildir).
    std::function<void()> on_update_now;
    std::function<void()> on_update_quit;
    std::function<void()> on_update_restore;
    std::function<void()> on_update_open_page;
    std::function<void()> on_update_repair_service;  // ADR-033: Evet/Hayir + tek UAC (App)
    // UI-06: Ayarlar
    std::function<void(bool)> on_autostart;                       // istenen yeni deger
    std::function<void()> on_install_offer;  // ADR-033 spec 8: "Kurulum gerekli · Kur" (App: --setup baslatir, Ui cikar)
    std::function<void(bool)> on_app_animations;                  // UI-11/R33: uygulama animasyonlari
    std::function<void(bool)> on_strip_enabled;                   // RGB isik seridi hareketi (Ayarlar)
    std::function<void(bool)> on_cleanup_hint;                    // R-C2: oyun kapaninca temizlik bildirimi (Ayarlar)
    // R21: destek paketi (dump'lari ekleme onayi + paket olusturma)
    std::function<void(bool)> on_bundle_dumps;
    std::function<void()> on_bundle_create;
    std::function<void()> on_update_check;  // Ayarlar: el ile Ecu Fps guncelleme denetimi
    std::function<void()> on_language;      // Ayarlar: uygulama dili secimi (denetim 5.2)
    // R20: her seyi geri al ve verileri sil (Evet/Hayir)
    enum class UninstallAction { Begin, Yes, No };
    std::function<void(UninstallAction)> on_uninstall;
    enum class ReadinessAction { Rescan, NetTest, FixAll };
    std::function<void(ReadinessAction)> on_readiness;            // CHK
    std::function<void(std::size_t)> on_readiness_fix;           // CHK: bulgu indeksi (dugme)
    std::function<void(std::size_t)> on_readiness_undo;          // CHK: "Geri al" (bulgu indeksi; Evet/Hayir App'te)
    std::function<void(std::size_t)> on_readiness_keep;          // CHK: "Mevcut degeri koru" (ayar yazmadan kaydi kapatir)
    std::function<void(std::size_t)> on_game_guide;              // GUIDE-01: oyun satiri
    std::function<void(std::size_t)> on_game_fullscreen;         // F-47: oyun satiri tam ekran iyilestirmeleri
    // R16: Islem Gecmisi sayfasi test gecmisi secimi ve kullanici kontrollu disa aktarma
    std::function<void(std::size_t)> on_history_select;
    std::function<void()> on_history_export;
    std::wstring check_text(const core::readiness::Check& c) const { return screen_.check_text(c); }
    // Spec H2: kurulum onayinda gosterilen oge satiri (ad · tur · surum · tarih · boyut); k = scan.updates indeksi.
    std::wstring updates_install_what(std::size_t k) const { return screen_.updates_install_what(vm_, k); }
    std::function<void(const std::string& id, bool allow)> on_consent_set;
    std::function<void(const std::string& id)> on_consent_reset;
    // Pencere kapandi (WM_DESTROY sonrasi; sahip nesneyi baska bir mesajda birakmali).
    std::function<void()> on_closed;
    std::function<void()> on_welcome_dismiss;  // UI-08
    std::function<void(std::size_t)> on_game_nvidia;  // A1.1
    std::function<void(std::size_t)> on_game_manual_remove;  // EF-X11
    std::function<void()> on_games_add_exe;                  // EF-X11
    std::function<void()> on_perf_drift_reapply;  // Koruma Modu: Yeniden uygula (onay sonrasi Evet/Hayir)
    std::function<void()> on_perf_drift_keep;     // Koruma Modu: Boyle kalsin
    std::function<void()> on_perf_keep;  // kurtarma: dis degisiklikte mevcut degeri koru
    std::function<void()> on_detection_retry;  // oyun tespiti: kitaplik + surec taramasini simdi yenile
    std::function<void(const std::string& group, bool selected)> on_group_select;  // UI-10
    // WIN-08: temizlik eylemleri ve sayfa gosterimi
    enum class CleanAction { Scan, Run, Yes, No, Stop, ScanCancel, StorageSettings };
    std::function<void(CleanAction)> on_cleanup;
    std::function<void(core::cleanup::CleanupCategory, bool)> on_cleanup_select;
    std::function<void(const std::wstring&)> on_cleanup_volume;               // EXT-CLN-01
    std::function<void(core::cleanup::CleanupCategory)> on_cleanup_files;     // EXT-CLN-03
    std::function<void(Page)> on_page_shown;
    void set_cleanup(const ViewModel::CleanupView& c);
    // WIN-09: baslangic sayfasi eylemleri
    enum class StartupAction { Apply, Yes, No, Rescan };
    std::function<void(StartupAction)> on_startup;
    std::function<void(std::size_t row, bool enabled)> on_startup_select;
    std::function<void(std::size_t row, bool selected)> on_startup_service_select;  // WIN-10
    void set_startup(const ViewModel::StartupView& v);
    // OPT-07: sistem kontrolu
    enum class CheckAction { Start, Yes, No };
    std::function<void(CheckAction)> on_check;
    std::function<void(std::size_t row, bool on)> on_check_select;
    void set_check(const ViewModel::CheckView& v);
    void set_before_after(const ViewModel::BeforeAfterView& v);  // OPT-08
    void set_settings(const SettingsView& s);
    // UI is parcacigi: ust menu sekmesiyle ayni gecis (ADR-033: --uninstall Ayarlar sayfasinda acilir).
    void show_page(Page p);
    void set_live(const core::metrics::LiveSnapshot& l);  // UI-09
    void set_speed(const ViewModel::SpeedView& v);        // Task 12: hiz testi ilerleme/sonuc
    // Ilk acilista onbellekteki tum gorunum (sistem, bulgular, ayarlar, anahtar).
    void set_view(const ViewModel& vm);
    // UI is parcacigi: olcum ekrani durumunu gunceller (kaynak secimi pencerede korunur).
    void set_measure(const core::measure::MeasureState& m);
    // Canli test ekrani (spec 2026-11-12 A4). UI is parcacigi: en yeni onizleme karesi + canli sayilar. Yalniz canli
    // gorunum bolgesi yeniden cizilir (ust menu ve diger sayfalar cizilmez); gorunum acik degilse yalniz saklanir.
    void set_live_test(const core::measure::LiveSample& v);
    // Canli gorunum su an ekranda mi (kosu canli ekranla suruyor, Olcumler sayfasi acik, pencere gorunur). Onizleme
    // ureticisi yalniz bu dogruyken kare kopyalar.
    bool live_view_visible() const;
    // UI is parcacigi: ekran okuyucuya kisa duyuru (UIA Notification; ornek "Test tamamlandi", ilerleme adimi).
    // completed=true islem sonucu, false durum/ilerleme. Ekran okuyucu yoksa hicbir sey yapmaz.
    bool announce(const std::wstring& text, bool completed = true);
    // TH-04: secim degistiren yol (tema/kaynak/sure/birim/sekme) UIA ogesini isaretler; olay bir sonraki
    // cizimde, isabet haritasi yeni Selected durumunu tasirken yayinlanir.
    void note_selection(Hit h) { pending_selection_ = h; }

    // ---- Uygulama ici kip penceresi (spec 2026-11-12 E) ----
    // UI is parcacigi: kip penceresini acar ve yanit gelene kadar ic ice ileti dongusunde bekler (TaskDialog ile ayni
    // cagri bicimi: cagiran yanitla devam eder). Acikken fare / klavye / UIA yalniz kip ogelerine ulasir; Esc ve
    // pencereyi kapatma dugmesi = Hayir (dismissed). Ilk odak Hayir'dadir; kapaninca odak onceki ogeye doner.
    // Pencere gosterilemiyorsa (pencere yok, gizli / tepside, simge durumunda) hicbir sey acilmaz ve sessiz varsayilan
    // doner: Hayir (dismissed) — cagiran islemi baslatmaz. Bekleyis sirasinda pencere yok edilirse yanit yine odur.
    // Ic ice acilabilir (ustteki gosterilir; alttaki ustteki kapaninca geri gelir).
    ModalResult run_modal(ModalSpec spec);
    bool modal_active() const { return state_.modal != nullptr; }
    // HWND -> yasayan ana pencere (UI is parcacigi); baska pencere / nullptr -> nullptr. Onay islevleri (ask_fix ...)
    // sahip HWND'sinden pencereyi bununla bulur: tepside (ana pencere yok) soru sorulmaz.
    static MainWindow* from_hwnd(HWND hwnd);

private:
    MainWindow(const Theme& theme, const core::i18n::Catalog& cat) : theme_(theme), screen_(theme, cat) {}
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    PaintStats paint_stats_;
    void invalidate();
    void activate(Hit h);
    void move_focus(int delta);
    float scale() const { return static_cast<float>(dpi_) / 96.f; }
    static std::int64_t now_ms();
    void read_motion_pref();
    std::optional<bool> motion_original_;       // UI-13
    bool app_animations_ = true;                // UI-11/R33 (tercih; Preferences'tan gelir)
    void apply_settings_prefs(const SettingsView& s);
    void read_contrast_pref();
    void update_measure_timer();
    void set_focus(Hit h, bool visible);
    void update_window_icons();  // BRAND-01: DPI'ye uygun kucuk/buyuk ikon (WM_SETICON)
    static constexpr UINT kMsgUiaActivate = WM_APP + 4;  // wParam: Hit (UIA Invoke/Toggle/Select)
    static constexpr UINT kMsgVisibility = WM_APP + 5;
    // B10 inceleme: WM_APP + 6 (eski UIA Value.SetValue, denetim 5.3) hedef FPS alaniyla kaldirildi; pencerede yazilabilir
    // alan yok. UiaModel::set_value bagli degil: Edit rolu yoksa UIA ValuePattern de sunulmaz.

    const Theme& theme_;
    Screen screen_;
    std::unique_ptr<Graphics> gfx_;
    std::wstring locale_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    Options opts_;
    UiState state_;
    ViewModel vm_;
    std::array<LedPeak, kLiveTileCount> live_peaks_{};  // LED tepe izi durumu (update_live_peaks)
    HitMap hits_;
    bool tracking_ = false;
    bool anim_timer_ = false;
    bool tick_timer_ = false;  // M04: 1 Hz gecen sure zamanlayicisi (yalniz test islerken)
    bool strip_timer_ = false;  // RGB serit: ~20 kare/sn yalniz strip_should_animate iken
    bool breathe_timer_ = false;  // Panel Performans dugmesi nefes halkasi / kalp atisi: yalniz breathe_should_animate iken
    UINT breathe_interval_ = 0;   // calisan zamanlayicinin araligi (acik: nefes ~15 kare/sn; kapali: kalp atisi ~30 kare/sn)
    bool beat_rest_drawn_ = false;  // kalp atisinin dinlenme karesi cizildi mi (dinlenmede yeniden cizilmez)
    bool pulse_timer_ = false;  // Oyunlar "Calisiyor" noktasinin nabzi: yalniz pulse_should_animate iken (yalniz noktalar gecersiz kilinir)
    bool anim_live_only_ = true;  // animasyon zamanlayicisi yalniz canli kutucuk gecisi icin mi calisiyor (bolgesel cizim)
    unsigned anim_live_tick_ = 0;  // kutucuk gecisi kareleri icin tik sayaci (her ikinci tikte cizilir)
    std::optional<Palette> hc_palette_;          // yuksek kontrast etkinse sistem renkleri
    Hit pressed_ = Hit::None;
    Hit pending_selection_ = Hit::None;  // TH-04: sonraki uygun cizimde UIA SelectionItem olayi
    HINSTANCE inst_ = nullptr;
    HICON icon_small_ = nullptr, icon_big_ = nullptr;  // sahiplik bizde (DestroyIcon)                    // sol tus basili oldugu oge (tikla = bas + birak ayni oge)
    std::shared_ptr<UiaModel> uia_model_ = std::make_shared<UiaModel>();
    std::unique_ptr<UiaHost> uia_;
    Activity activity_;
    void update_visibility();
    StripInputs strip_inputs() const;
    void update_strip_timer();  // serit hareketi kosullari degisince zamanlayiciyi baslatir/durdurur
    void invalidate_strip();    // yalniz serit dikdortgeni (piksel)
    // Panel hizli yollari (spec 2026-11-12 F: yalniz hareket eden bolge gecersiz kilinir). DIP bolgesi -> disa
    // yuvarlanmis piksel dikdortgeni (istemci alanina kirpilir); bos bolge -> false.
    bool dip_region_px(const RectF& dip, RECT& out) const;
    // Canli kutucuk bolgesi (bolge yoksa / Panel degilse tam cizim). new_sample: vm_.live degisti — satir boyu da
    // degistiyse tam cizim.
    void invalidate_tiles(bool new_sample = false);
    // Guncelleme bolgesi yalniz hizli yol bolgelerinden (RGB serit, Performans dugmesi, canli kutucuklar) olusuyorsa
    // onlari cizer ve true doner; aksi halde hicbir sey cizmez (cagiran tam kareyi cizer). rects: BeginPaint'ten ONCE
    // okunan guncelleme dikdortgenleri.
    bool paint_panel_fast(HDC dc, const std::vector<RECT>& rects);
    // Oyunlar hizli yolu (spec 2026-11-12 B2): guncelleme bolgesi yalniz "Calisiyor" noktalarindan (ve RGB seritten)
    // olusuyorsa onlari cizer ve true doner; aksi halde hicbir sey cizmez (cagiran tam kareyi cizer).
    bool paint_pulse_fast(HDC dc, const std::vector<RECT>& rects);
    // Nabiz noktalarini gecersiz kilar (bolge yoksa hicbir sey yapmaz).
    void invalidate_pulse();
    // Canli test gorunumu bolgesi (piksel; son tam cizimde yazilan state_.live_region). false: bolge yok.
    bool live_region_px(RECT& out) const;
    // Canli gorunum yeni acildiysa (kosu canli ekranla basladi / pencere kosu surerken acildi) Olcumler sayfasina gecer.
    void sync_live_view(bool was_shown);
    // Ust menude Sol/Sag: true -> tus islendi (odak bir ust sekme veya Sistem araclari alt sekmesindeydi).
    bool arrow_tabs(int delta);
    // Oyunlar (spec 2026-11-12 B2): odak bir oyun kartindayken ok tuslari kartlar arasinda gezer (secimi degistirmez;
    // Enter secer). true -> tus islendi.
    bool arrow_games(int dx, int dy);
    // Kip penceresi cercevesi: run_modal'in yiginindadir; modals_ en ustteki (son) cerceveyi gosterir.
    struct ModalFrame {
        ModalState st;
        Hit prev_focus = Hit::None;   // kapaninca odagin donecegi oge
        bool prev_focus_visible = false;
        bool gone = false;            // pencere yok edildi: run_modal this'e dokunmadan doner
    };
    std::vector<ModalFrame*> modals_;
    bool modal_uia_event_ = false;  // kip penceresi acildi / kapandi: sonraki cizimde UIA yapi + odak olaylari
    static constexpr UINT_PTR kModalTimer = 6;  // geri sayimli kip penceresi (1 Hz)
    ModalKeyGuard modal_key_guard_;     // kip penceresini kapatan basili tusun tekrarlari sayfaya ulasmaz
    void modal_key_input(ModalKey k);   // klavye -> ustteki kip penceresi
    void modal_sync();                  // durum degisti: odak + yeniden cizim
    void abandon_modals();              // pencere yok ediliyor: bekleyen butun kip pencereleri Hayir (dismissed)
};

}  // namespace ef::ui
