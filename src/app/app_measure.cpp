// Olcumler: App <-> MeasureController yapistiricisi (P9/B5). Olcum durumu, is parcaciklari ve test mantigi
// measure_controller.* ve measure_*.cpp'de; burada yalniz App'in MeasureHost uygulamasi (pencere, kapilar, gecmis,
// tercihler) ve Once/Sonra kartinin gonderimi vardir. Davranis degismedi.
#include "app/app_internal.h"
#include "app/measure_controller.h"

namespace ef::app {

using core::i18n::StringId;

// MeasureHost'un App uygulamasi. App'in ozel uyelerine dostluk ile erisir (app.h: friend class AppMeasureHost); denetleyici
// App'i hic gormez. post() her is parcacigindan cagrilabilir; digerleri yalniz UI is parcaciginda.
class AppMeasureHost final : public MeasureHost {
public:
    explicit AppMeasureHost(App& app) : a_(app) {}

    void post(std::function<void()> fn) override { a_.post(std::move(fn)); }

    HWND host_hwnd() const override { return a_.host_; }
    HWND window_hwnd() const override { return a_.window_ ? a_.window_->hwnd() : HWND{}; }
    bool window_visible() const override { return a_.window_ && a_.window_->activity().get() == ui::Visibility::Visible; }
    bool live_view_visible() const override { return a_.window_ && a_.window_->live_view_visible(); }
    void publish_measure(const core::measure::MeasureState& m) override {
        if (a_.window_) a_.window_->set_measure(m);
    }
    void publish_speed(const ui::ViewModel::SpeedView& v) override {
        if (a_.window_) a_.window_->set_speed(v);
    }
    void publish_live_sample(const core::measure::LiveSample& s) override {
        if (a_.window_) a_.window_->set_live_test(s);
    }
    bool ask_speed_consent() override {
        const HWND owner = a_.window_ ? a_.window_->hwnd() : a_.host_;
        return ui::ask_fix(owner, a_.cat(), a_.t(StringId::SpeedQuestion), a_.t(StringId::SpeedDetail));
    }
    std::optional<std::string> ask_server_address(const std::string& initial) override {
        if (!a_.window_) return std::nullopt;  // pencere yok (tepside): sorulamaz -> paket gonderilmez
        return ui::ask_server_address(a_.window_->hwnd(), a_.cat(), initial);
    }
    bool ask_proof_confirm(const ProofConfirmInfo& info) override {
        const auto fill = [&](std::wstring s) {
            for (const auto& [k, v] : {std::pair<std::wstring, std::wstring>{L"{n}", std::to_wstring(info.toggles)},
                                       {L"{m}", std::to_wstring(info.estimate_min)},
                                       {L"{g}", std::to_wstring(info.groups)}})
                for (std::size_t q = s.find(k); q != std::wstring::npos; q = s.find(k, q + v.size())) s.replace(q, k.size(), v);
            return s;
        };
        ui::ModalSpec spec;
        spec.caption = a_.t(StringId::AppTitle);
        spec.title = a_.t(StringId::ProofConfirmTitle);
        spec.body.push_back(fill(a_.t(StringId::ProofConfirmBody1)));
        spec.body.push_back(a_.t(StringId::ProofConfirmBody2));
        if (info.admin) spec.body.push_back(fill(a_.t(StringId::ProofConfirmScopeAdmin)));  // yonetici ayarlar dahil; yardimci hizmet, UAC acilmaz
        else if (info.partial) spec.body.push_back(fill(a_.t(StringId::ProofConfirmScope)));  // yalniz yonetici gerektirmeyen alt kume; UAC acilmaz
        spec.yes = a_.t(StringId::ProofConfirmYes);
        spec.no = a_.t(StringId::ProofConfirmNo);
        if (!a_.window_) return false;  // pencere yok (tepside): onay alinamaz -> hicbir sey baslamaz
        return a_.window_->run_modal(std::move(spec)).answer == ui::ModalAnswer::Yes;
    }

    bool uninstall_running() const override { return a_.uninstall_phase_ == ui::SettingsView::UninstallPhase::Running; }
    bool verify_or_block(core::games::GuardedAction act) override { return a_.verify_or_block(act); }
    bool gate_or_block(core::games::GuardedAction act) override { return a_.gate_or_block(act); }
    core::games::GateDecision gate(core::games::GuardedAction act) const override { return a_.gate(act); }
    void push_gate() override { a_.push_gate(); }
    bool admin_busy() const override { return a_.admin_busy_; }
    bool update_holds_writes() const override { return a_.update_holds_writes(); }
    void refresh_admin_path() override { a_.refresh_admin_path(); }
    void set_admin_service_only(bool v) override { a_.admin_channel_.set_service_only(v); }
    core::perf::PerfController* perf() override { return a_.controller_.get(); }
    core::settings::Preferences& prefs() override { return a_.prefs_; }
    bool save_prefs() override { return a_.save_prefs(); }
    void start_findings_refresh() override { a_.start_findings_refresh(); }
    void check_pending_exit() override { a_.check_pending_exit(); }

    void begin_run_marker(const char* source) override { a_.begin_run_marker(source); }
    void end_run_marker() override { a_.end_run_marker(); }
    core::insights::History& history() override { return a_.history_; }
    void save_history() override { a_.save_history(); }
    void sync_bench_evidence() override { a_.sync_bench_evidence(); }
    void refresh_history_compare() override { a_.refresh_history_compare(); }

    std::optional<MeasureGameTarget> active_game() const override {
        if (!a_.active_game_) return std::nullopt;
        const auto& g = *a_.active_game_;
        return MeasureGameTarget{g.dir_key, g.game_id, g.build, g.name, g.pid, g.create_time};
    }
    std::string game_build(const std::string& dir_key) const override {
        const auto it = a_.game_info_.find(dir_key);
        return it == a_.game_info_.end() ? std::string() : it->second.build;
    }
    void note_game_renderer(const std::string& dir_key, core::games::GuideRenderer r) override { a_.game_renderers_[dir_key] = r; }
    const std::string& capture_os_build() const override { return a_.capture_os_build_; }
    const std::string& capture_collector_commit() const override { return a_.capture_collector_commit_; }

private:
    App& a_;
};

void App::init_measure() {
    measure_host_ = std::make_unique<AppMeasureHost>(*this);
    measure_ = std::make_unique<MeasureController>(*measure_host_, vm_);
}

void App::push_before_after() {
    ui::ViewModel::BeforeAfterView v;
    v.stage = report_.stage();
    v.after_n = report_.after_count();
    v.target = report_.target();
    v.report = report_.report();
    vm_.before_after = v;
    if (window_) window_->set_before_after(v);
}

}  // namespace ef::app
