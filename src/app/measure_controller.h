#pragma once
// P9/B5: Olcumler denetleyicisi. Uygulama Testi, Denge testi, Oyunda Test, uzun test, internet hiz testi ve "Performans
// etkisini olc" (ABBA kanit deneyi) durumu + is parcaciklari artik App'ten ayri burada yasar. App yalniz MeasureHost
// arayuzunu uygular; denetleyici App'in ozel uyelerine dokunmaz.
//
// Is parcacigi siniri (degismez):
//   * MeasureController'in her genel islevi UI is parcacigindan cagrilir (App'in mesaj dongusu), asagida "her is
//     parcacigi" diye isaretlenenler haric.
//   * Olcum is parcaciklari (bench_thread / endurance_thread / speed_thread) vm()'ye HIC dokunmaz; sonucu
//     MeasureHost::post ile UI is parcacigina birakir. vm() ve MeasureHost'un diger islevleri yalniz UI is parcaciginda.
//   * Iptal bayraklari std::atomic'tir; her is parcacigindan yazilabilir (request_* islevleri).
//   * Is parcacigi nesneleri kapanista App tarafindan beklenir (bench_thread() vb.): denetleyici destructor'inda join
//     YAPMAZ; App ~App'i once kendi sirasiyla join eder (yazma isleri sonuna kadar, salt okuma isleri 3 sn sinirli).
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/measure_sampling.h"
#include "benchmark/balance/balance_runner.h"
#include "benchmark/runner/runner.h"
#include "core/games/game_guide.h"
#include "core/games/game_icon.h"
#include "core/games/protection_gate.h"
#include "core/i18n/catalog.h"
#include "core/insights/history.h"
#include "core/measure/live_view.h"
#include "core/measure/view.h"
#include "core/perf/controller.h"
#include "core/perf/proof_experiment.h"
#include "core/settings/preferences.h"
#include "ui/shell/view_model.h"

namespace ef::app {

// Oyunda Test'in hedefi: izleyicinin su an gordugu tek oyun (App::ActiveGame'in duz kopyasi).
struct MeasureGameTarget {
    std::string dir_key, game_id, build, name;
    std::uint32_t pid = 0;
    std::uint64_t create_time = 0;
};

// "Performans etkisini olc" onay penceresinin sayilari (metin App'te katalogdan yerellestirilir).
struct ProofConfirmInfo {
    std::size_t toggles = 0;    // Acik/Kapali gecis sayisi
    int estimate_min = 0;       // tahmini sure (dk)
    std::size_t groups = 0;     // olculecek ayar grubu sayisi
    bool admin = false;         // yonetici ayarlar dahil (hizmet yolu)
    bool partial = false;       // yalniz yonetici gerektirmeyen alt kume
};

// Denetleyicinin App'ten ihtiyaci olan her sey. Aksi belirtilmedikce yalniz UI is parcaciginda cagrilir.
class MeasureHost {
public:
    virtual ~MeasureHost() = default;

    // Her is parcacigi: isi UI is parcacigina birakir (pencere yok edilmisse kopya silinir).
    virtual void post(std::function<void()> fn) = 0;

    // --- pencere ---
    virtual HWND host_hwnd() const = 0;      // gizli ana pencere (zamanlayici + onizleme bildirimi); null olabilir
    virtual HWND window_hwnd() const = 0;    // arayuz penceresi; yoksa null
    virtual bool window_visible() const = 0;           // arayuz penceresi var ve gorunur
    virtual bool live_view_visible() const = 0;        // canli test ekrani su an ekranda
    virtual void publish_measure(const core::measure::MeasureState& m) = 0;  // pencere yoksa no-op
    virtual void publish_speed(const ui::ViewModel::SpeedView& v) = 0;       // pencere yoksa no-op
    virtual void publish_live_sample(const core::measure::LiveSample& s) = 0;
    virtual bool ask_speed_consent() = 0;                        // internet hiz testi Evet/Hayir
    // R-C4: oyun sunucusu ping adresi (kip penceresi, yazi alani; initial = kayitli adres veya bos). Evet + gecerli
    // adres: kanonik "a.b.c.d"; Hayir / kapatildi / pencere yok: nullopt (paket gonderilmez).
    virtual std::optional<std::string> ask_server_address(const std::string& initial) = 0;
    virtual bool ask_proof_confirm(const ProofConfirmInfo& info) = 0;  // pencere yoksa false

    // --- kapilar / durum ---
    virtual bool uninstall_running() const = 0;  // R20: veri silinirken test baslamaz
    virtual bool verify_or_block(core::games::GuardedAction a) = 0;
    virtual bool gate_or_block(core::games::GuardedAction a) = 0;
    virtual core::games::GateDecision gate(core::games::GuardedAction a) const = 0;
    virtual void push_gate() = 0;
    virtual bool admin_busy() const = 0;
    virtual bool update_holds_writes() const = 0;
    virtual void refresh_admin_path() = 0;
    virtual void set_admin_service_only(bool v) = 0;  // deney boyunca UAC broker'ina dusulmez
    virtual core::perf::PerfController* perf() = 0;   // Performans anahtari; null olabilir
    virtual core::settings::Preferences& prefs() = 0;
    virtual bool save_prefs() = 0;
    virtual void start_findings_refresh() = 0;
    virtual void check_pending_exit() = 0;

    // --- kosu isareti, gecmis, kanit ---
    virtual void begin_run_marker(const char* source) = 0;
    virtual void end_run_marker() = 0;
    virtual core::insights::History& history() = 0;
    virtual void save_history() = 0;
    virtual void sync_bench_evidence() = 0;
    virtual void refresh_history_compare() = 0;

    // --- oyun baglami (Oyunda Test) ---
    virtual std::optional<MeasureGameTarget> active_game() const = 0;
    virtual std::string game_build(const std::string& dir_key) const = 0;  // bilinmiyorsa bos
    virtual void note_game_renderer(const std::string& dir_key, core::games::GuideRenderer r) = 0;
    virtual const std::string& capture_os_build() const = 0;
    virtual const std::string& capture_collector_commit() const = 0;
};

// Bir deney kosusunun degerleri + kosul parmak izi (saf; birim testli). Performans'in kendisinin degistirebildigi alanlar
// (guc plani / overlay) parmak izine GIRMEZ; sebeke/pil ve pil tasarrufu ise deney boyunca ayni olmali.
core::perf::ProofRun proof_run_from_report(const benchmark::balance::BalanceReport& rep, const BalanceEnvironment& e0,
                                           const BalanceEnvironment& e1, bool crashed, const std::string& app_version,
                                           const std::string& driver);

class MeasureController {
public:
    MeasureController(MeasureHost& env, ui::ViewModel& vm);
    MeasureController(const MeasureController&) = delete;
    MeasureController& operator=(const MeasureController&) = delete;

    // ---- testler (UI is parcacigi) ----
    void start_builtin_test();    // Uygulama Testi (Genel kapsam)
    void start_balance_test();    // Uygulama Testi (Darbogaz kapsami)
    void start_ingame_test();     // Oyunda Test
    void start_endurance_test();  // uzun test (kullanici baslatir)
    void stop_endurance_test();
    void start_speed_test();      // internet hiz testi (Evet/Hayir sonrasi)
    void stop_speed_test();
    void start_server_ping();     // R-C4: oyun sunucusu ping (adres sorulur; ~4 sn; hiz testiyle ayni is parcacigi/iptal)

    // ---- "Performans etkisini olc" (ABBA kanit deneyi) ----
    void start_proof_experiment();
    void stop_proof_experiment();
    bool proof_running() const { return proof_ != nullptr; }
    void proof_refresh_estimate();
    void proof_refresh_precheck();
    void proof_on_perf_change();  // Performans controller geri cagrisindan

    // ---- canli test ekrani (UI is parcacigi) ----
    void stop_live_view();
    void live_view_push();           // WM_TIMER / onizleme bildirimi
    void on_live_frame_posted();     // kMsgLiveFrame: bekleyen bildirim tuketildi + gorunumu tazele
    static constexpr UINT_PTR kTimerLiveView = 0x58F2;

    // ---- iptal (her is parcacigi) ----
    void request_bench_cancel() { bench_cancel_ = true; }
    void request_speed_cancel() { speed_cancel_ = true; }
    void request_endurance_cancel() { endurance_cancel_ = true; }
    void request_shutdown_cancel() { speed_cancel_ = true, bench_cancel_ = true, endurance_cancel_ = true; }
    void set_speed_gate_blocked(bool v) { speed_gate_blocked_ = v; }  // UI is parcacigi yazar, hiz testi okur

    // ---- kapanista App'in bekledigi is parcaciklari (join/WaitForSingleObject App'te) ----
    std::thread& bench_thread() { return bench_worker_; }
    std::thread& endurance_thread() { return endurance_worker_; }
    std::thread& speed_thread() { return speed_worker_; }

    // ---- gozlem (birim testler + App) ----
    bool bench_cancel_requested() const { return bench_cancel_.load(); }
    bool endurance_cancel_requested() const { return endurance_cancel_.load(); }
    bool speed_cancel_requested() const { return speed_cancel_.load(); }
    benchmark::FrameTap& frame_tap() { return frame_tap_; }
    // Gecmis kaydinin calisma kosullari: yalniz gercekten okunan degerler (eksik alan yazilmaz, uydurulmaz).
    std::map<std::string, std::string> run_conditions() const;

private:
    enum class ProofWait { None, Plan, Apply, RestoreConfirm, Restore, Run };

    core::perf::PerfController* perf() const { return env_.perf(); }
    void publish_measure() { env_.publish_measure(vm_.measure); }
    void publish_speed() { env_.publish_speed(vm_.speed); }
    void post(std::function<void()> fn) { env_.post(std::move(fn)); }
    void start_live_view();  // kosu canli ekranla basliyor

    // Deney adimlari
    core::perf::ProofRefusal proof_preconditions(bool* quality_tradeoff, bool gate = true, std::size_t* scope_groups = nullptr,
                                                  bool* scope_partial = nullptr, bool* scope_admin = nullptr,
                                                  std::vector<std::string>* scope_keys = nullptr);
    void proof_step();
    void proof_start_run(const core::perf::ProofAction& a);
    void proof_run_done(core::perf::ProofRun run, bool cancelled, bool hard_fail);
    void proof_publish(const char* stage);
    void proof_finish();

    MeasureHost& env_;
    ui::ViewModel& vm_;

    // Uygulama Testi / Denge / Oyunda Test / deney olcum kosusu (tek is parcacigi, iptal bayragi)
    std::thread bench_worker_;
    std::atomic<bool> bench_cancel_{false};
    // Spec 2026-11-12 A4: canli test ekrani. Cizim is parcacigi onizleme karelerini posta kutusuna birakir (en yeni kazanir;
    // beklemez) ve host penceresine tek bir bildirim postalar; UI is parcacigi en yeni kareyi + FrameTap anlik goruntusunu
    // pencereye iletir. Pencere gorunmuyorsa / Olcumler sayfasi acik degilse kare kopyalanmaz.
    benchmark::FrameTap frame_tap_;
    std::shared_ptr<core::measure::PreviewMailbox> live_mailbox_ = std::make_shared<core::measure::PreviewMailbox>();
    benchmark::PreviewCallback live_preview_cb_;  // kosu boyunca yasar (onceki is parcacigi join edilmeden degismez)
    std::atomic<bool> live_frame_posted_{false};  // bekleyen bildirim var: ikinci mesaj postalanmaz
    core::games::ImageRef live_image_;            // son iletilen onizleme (yalniz sayi tazelenirken goruntu korunur)
    bool live_view_timer_ = false;
    // R13/INS-03: uzun test
    std::thread endurance_worker_;
    std::atomic<bool> endurance_cancel_{false};
    // Task 12: internet hiz testi. Sonuc yalniz oturumda (vm_.speed); diske yazilmaz.
    std::thread speed_worker_;
    std::atomic<bool> speed_cancel_{false};
    std::atomic<bool> speed_gate_blocked_{false};
    // P1: ABBA kanit deneyi (hepsi UI is parcaciginda; olcum is parcacigi sonucu post eder)
    std::unique_ptr<core::perf::ProofSession> proof_;  // dolu = deney suruyor
    ProofWait proof_wait_ = ProofWait::None;
    std::string proof_plan_fp_;                  // ilk onay planinin parmak izi; sonraki planlar ayni olmali
    std::vector<std::string> proof_scope_keys_;  // olculen alt kumenin grup anahtarlari
    bool proof_cancel_ = false;
    core::measure::TestPhase proof_prev_phase_ = core::measure::TestPhase::Idle;
};

}  // namespace ef::app
