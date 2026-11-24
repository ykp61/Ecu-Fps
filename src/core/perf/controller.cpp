#include "core/perf/controller.h"

#include "core/perf/admin_plan.h"

#include <algorithm>

#include "core/service/channel_choice.h"

namespace ef::core::perf {

bool Plan::affects_quality() const {
    for (const auto& i : items)
        if (i.op.affects_quality) return true;
    return false;
}
bool Plan::needs_reboot() const {
    for (const auto& i : items)
        if (i.op.needs_reboot) return true;
    return false;
}
bool Plan::needs_admin() const {
    for (const auto& i : items)
        if (i.needs_admin) return true;
    return false;
}
std::size_t Plan::startup_changes() const {
    std::size_t n = 0;
    for (const auto& i : items)
        if (i.changes_startup) ++n;
    return n;
}

std::string_view evidence_key(Evidence e) noexcept {
    switch (e) {
        case Evidence::Measured: return "plan.evidence.measured";
        case Evidence::Documented: return "plan.evidence.documented";
        case Evidence::Disputed: return "plan.evidence.disputed";
    }
    return "plan.evidence.documented";
}
std::string_view risk_key(Risk r) noexcept {
    switch (r) {
        case Risk::Low: return "plan.risk.low";
        case Risk::Medium: return "plan.risk.medium";
        case Risk::High: return "plan.risk.high";
    }
    return "plan.risk.low";
}

const std::vector<PlanGroup>& plan_groups() {
    using E = Evidence;
    using R = Risk;
    static const std::vector<PlanGroup> g{
        {"plan.power_mode", "plan.power_mode.desc", true, E::Documented, R::Low},
        {"plan.power_scheme", "plan.power_scheme.desc", true, E::Documented, R::Low},
        {"plan.vfx", "plan.vfx.desc", true, E::Documented, R::Low},
        {"plan.gaming", "plan.gaming.desc", true, E::Documented, R::Low},
        {"plan.windowed", "plan.windowed.desc", true, E::Documented, R::Low},
        {"plan.power_cpu", "plan.power_cpu.desc", true, E::Documented, R::Medium},    // F-32/F-33: yonetici (broker, UAC), islemci boost/parking
        {"plan.power_pcie", "plan.power_pcie.desc", true, E::Documented, R::Medium},  // F-42: yonetici (broker, UAC), PCIe ASPM AC
        {"plan.hags", "plan.hags.desc", true, E::Documented, R::Medium},      // OPT-10: yonetici (broker, UAC), yeniden baslatma
        {"plan.nvidia", "plan.nvidia.desc", true, E::Documented, R::Low},     // OPT-10: yonetici (broker, UAC), NVIDIA DRS global
        {"plan.services", "plan.services.desc", true, E::Documented, R::Medium},  // WIN-10: secili hizmetler oturumluk durdurulur (broker)
        {"plan.update_hold", "plan.update_hold.desc", true, E::Documented, R::Low},  // T03: yonetici (broker); kurumsal politikada plana girmez
        {"plan.gpu_route", "plan.gpu_route.desc", true, E::Documented, R::Low},  // T01: yalniz hibrit dizustu; HKCU, UAC yok; FPS vaadi yok
        {"plan.privacy", "plan.privacy.desc", true, E::Documented, R::Low},  // T14: HKCU, UAC yok; FPS etkisi beklenmez
        // T14: HKLM politika degerleri (UAC); baska yonetici ogesi yokken ek UAC acmamak icin varsayilan Hayir (Gelismis).
        {"plan.privacy_system", "plan.privacy_system.desc", false, E::Documented, R::Low},
        {"plan.restore_point", "plan.restore_point.desc", true, E::Documented, R::Low},  // T08: yalniz baska yonetici ogesi varsa; ayni UAC'de ilk adim
        // P2: arka plan programlarini dusuk onceligie al (kapatma yok). Kullanici acikca secmeden uygulanmaz.
        {"plan.background", "plan.background.desc", false, E::Disputed, R::Low},
        // T03: otomatik Windows Update'i kapatir (guvenlik guncellemeleri Kapali'ya kadar gelmez); acikca secilmeden uygulanmaz.
        {"plan.update_off", "plan.update_off.desc", false, E::Documented, R::High},
        // T12: ozellik sihirbazi sorulari ("Kullanmiyorsaniz Evet deyin"); hepsi varsayilan Hayir (Gelismis), yonetici (broker, UAC).
        {"plan.feature.bluetooth", "plan.feature.bluetooth.desc", false, E::Documented, R::Medium},
        {"plan.feature.print", "plan.feature.print.desc", false, E::Documented, R::Medium},
        {"plan.feature.xbox", "plan.feature.xbox.desc", false, E::Documented, R::Medium},
        {"plan.feature.search", "plan.feature.search.desc", false, E::Documented, R::Medium},
        {"plan.feature.store", "plan.feature.store.desc", false, E::Documented, R::Medium},
        // T11: secmeli Defender tarama dislamasi (guvenlik odunu); Defender kapatilmaz. Yalniz acikca Evet denirse.
        {"plan.defender", "plan.defender.desc", false, E::Disputed, R::High},
        // T09: secili oyunlar icin imaj-basina CPU onceligi (IFEO PerfOptions); oyun surecine dokunulmaz. Gelismis, varsayilan Hayir.
        {"plan.game_priority", "plan.game_priority.desc", false, E::Disputed, R::Low},
        // T13: geri alinabilir temizlik: Edge/OneDrive yalniz ilke ile kisitlanir (kaldirilmaz); medya uygulamalari yeniden kaydedilebilir.
        {"plan.debloat.edge", "plan.debloat.edge.desc", false, E::Documented, R::Low},
        {"plan.debloat.onedrive", "plan.debloat.onedrive.desc", false, E::Documented, R::Medium},
        {"plan.debloat.apps", "plan.debloat.apps.desc", false, E::Documented, R::Medium},
        // T21: Gelismis, HKCU, UAC yok, varsayilan Hayir. Yalniz gorunum sadelestirme / ivme tercihi; FPS vaadi yok.
        {"plan.win11_cleanup", "plan.win11_cleanup.desc", false, E::Documented, R::Low},
        {"plan.input", "plan.input.desc", false, E::Disputed, R::Low},
        // R-C3: Widgets/Haberler paneli ilkesi (HKLM, UAC). FPS etkisi belgesiz; yalniz acikca Evet.
        {"plan.widgets", "plan.widgets.desc", false, E::Documented, R::Low},
        // ADR-040 (B0): yeni gruplar ogesiz gelir (bu surumde hicbiri oge uretmez) ve ogesizken listelenmez.
        // B1: etkin planin AC USB secici askiya alma ayari (yonetici, broker). Surucu belgesi (S109) kapatmamayi onerir
        // (karsi kanit) -> Tartismali, varsayilan Hayir (Gelismis); plan metnindeki "Evet" karari bu sebeple degisti.
        {"plan.power_usb", "plan.power_usb.desc", false, E::Disputed, R::Low, false},
        // B2: on plan programina islemci onceligi (Win32PrioritySeparation). Etki tartismali -> Hayir (Gelismis).
        {"plan.foreground_boost", "plan.foreground_boost.desc", false, E::Disputed, R::Low, false},
        // B3: hizmet ana bilgisayari bolme esigi; hizmet yalitimi azalir, yeniden baslatma ister -> Hayir (Gelismis).
        {"plan.svchost_split", "plan.svchost_split.desc", false, E::Disputed, R::Medium, false},
        // B4: dinamik tick kapali (BCD); bayrakla KAPALI, yeniden baslatma ister -> Hayir (Gelismis).
        {"plan.boot_tick", "plan.boot_tick.desc", false, E::Disputed, R::High, false},
        // B6: ek NVIDIA surucu ayarlari; her ayar ayrica bayrakla KAPALI -> Hayir (Gelismis).
        {"plan.nvidia_extra", "plan.nvidia_extra.desc", false, E::Documented, R::Medium, false},
    };
    return g;
}

bool experiment_scope_item(const PlanItem& i, bool admin_ok) {
    if (i.needs_admin && !(admin_ok && service::service_handles_op(ipc::RequestAction::Apply, i.op.id) &&
                           service::service_handles_op(ipc::RequestAction::Restore, i.op.id)))
        return false;
    return !i.op.needs_reboot;  // P11: arka plan grubu kapsamda (parmak izi surec kimliginden bagimsiz)
}

std::vector<GroupView> visible_groups(const Plan& p, const std::map<std::string, bool>& selection, const DeviceClass& dc) {
    std::vector<GroupView> out;
    for (const auto& g : plan_groups()) {
        bool has_item = false;
        for (const auto& i : p.items)
            if (i.group() == g.key) {
                has_item = true;
                break;
            }
        if (!has_item) continue;
        const auto it = selection.find(std::string(g.key));
        const bool def = default_on_for(g, dc);  // T18
        const bool sel = it != selection.end() ? it->second : def;
        out.push_back({g.key, g.desc_key, g.evidence, g.risk, !def && !sel});
    }
    return out;
}

std::vector<std::string> group_field_keys(const Plan& p, std::string_view group) {
    std::vector<std::string> out;
    for (const auto& i : p.items)
        if (i.group() == group && !i.field_key.empty() && std::find(out.begin(), out.end(), i.field_key) == out.end())
            out.push_back(i.field_key);
    return out;
}

std::vector<FieldDetail> group_field_details(const Plan& p, std::string_view group) {
    std::vector<FieldDetail> out;
    for (const auto& i : p.items)
        if (i.group() == group && i.detail && std::find(out.begin(), out.end(), *i.detail) == out.end()) out.push_back(*i.detail);
    return out;
}

void PerfController::refresh_preview() {
    run_([this] {
        auto r = backend_.plan();
        post_([this, r = std::move(r)]() mutable {
            if (r.ok()) {
                preview_ = std::move(r).value();
                preview_ready_ = true;
            }
            if (on_change) on_change();
        });
    });
}

void PerfController::set_selected(const std::string& group, bool on) {
    if (phase_ != Phase::Off && phase_ != Phase::Confirming) return;  // is surerken secim degismez
    selection_[group] = on;
    if (on_selection_changed) on_selection_changed(group, on);
    if (on_change) on_change();
}

bool PerfController::selected(const std::string& group) const {
    const auto it = selection_.find(group);
    if (it != selection_.end()) return it->second;
    for (const auto& g : plan_groups())
        if (g.key == group) return default_on_for(g, device_);
    return true;
}

bool PerfController::tailored() const {
    for (const auto& g : plan_groups()) {
        if (default_on_for(g, device_) == g.default_on || selection_.count(std::string(g.key))) continue;
        for (const auto& i : plan_.items)
            if (i.group() == g.key) return true;
    }
    return false;
}

std::string_view to_string(Phase p) noexcept {
    switch (p) {
        case Phase::Off: return "off";
        case Phase::Planning: return "planning";
        case Phase::Confirming: return "confirming";
        case Phase::Applying: return "applying";
        case Phase::On: return "on";
        case Phase::ConfirmRestore: return "confirm_restore";
        case Phase::Restoring: return "restoring";
        case Phase::RecoveryNeeded: return "recovery_needed";
    }
    return "unknown";
}

PerfController::PerfController(PerfBackend& backend, Executor run_background, Poster post_ui)
    : backend_(backend), run_(std::move(run_background)), post_(std::move(post_ui)) {}

void PerfController::set(Phase p, Notice n) {
    // Saha 2026-11-15: kurtarma ekrani hangi ayarin neden geri alinamadigini gosterir (journal'dan; uydurma yok).
    if (p == Phase::RecoveryNeeded) {
        // R-F1: aktarilamayan onceki surum kayitlari varken her kurtarma gosteriminde ayni durust neden kalir.
        if ((n == Notice::None || n == Notice::RestoreFailed) && backend_.legacy_unmigrated()) n = Notice::LegacyUnmigrated;
        if (phase_ != Phase::RecoveryNeeded || n != Notice::AdminBusy) failed_items_ = backend_.failed_items();
    } else if (p == Phase::Off || p == Phase::On) {
        failed_items_.clear();
    }
    phase_ = p;
    notice_ = n;
    if (on_change) on_change();
}

void PerfController::init() {
    drift_confirm_ = false;
    drift_items_.clear();
    if (backend_.recovery_needed()) {
        external_conflict_ = backend_.external_conflict();
        set(Phase::RecoveryNeeded, backend_.legacy_unmigrated() ? Notice::LegacyUnmigrated : Notice::None);
        return;
    }
    external_conflict_ = false;
    active_changes_ = backend_.active_changes();
    // Acilis uzlastirmasi: kayit bayragina degil GERCEK hedeflere bakilir. Kayitta etkin ama hedefi artik yazdigimiz
    // degerde olmayan ayarlar "kapali" sayilir ve listelenir; kalanlar Acik. Hicbiri etkin degilse Kapali.
    off_items_ = active_changes_ > 0 ? backend_.check_live() : std::vector<recovery::FailedRestoreItem>{};
    if (off_items_.size() >= active_changes_) active_changes_ = 0;
    else active_changes_ -= off_items_.size();
    // Koruma Modu (T07): sapmis ayarlar (Windows guncellemesi vb.) ayrica adlandirilir; yeniden uygulama Evet/Hayir ister.
    if (!off_items_.empty()) drift_items_ = backend_.drift();
    set(active_changes_ > 0 ? Phase::On : Phase::Off, off_items_.empty() ? Notice::None : Notice::SomeOff);
}

void PerfController::toggle() {
    switch (phase_) {
        case Phase::On:
            if (!off_items_.empty()) {  // bazi ayarlar gercekte kapali: ayni plan/Evet-Hayir akisiyla yalniz eksikler yeniden uygulanir
                start_plan();
                return;
            }
            set(Phase::ConfirmRestore, Notice::None);
            return;
        case Phase::Off: {
            start_plan();
            return;
        }
        case Phase::RecoveryNeeded: set(Phase::ConfirmRestore, Notice::None); return;
        default: return;  // mesgulken veya onay acikken anahtar yok sayilir
    }
}

void PerfController::start_plan() {
    set(Phase::Planning, Notice::None);
    run_([this] {
        auto r = backend_.plan();
        post_([this, r = std::move(r)]() mutable {
            if (phase_ != Phase::Planning) return;
            const auto idle = idle_phase();  // gercekte kapanmis ayarlar varsa kalan Acik asamasi korunur
            const auto idle_notice = off_items_.empty() ? Notice::None : Notice::SomeOff;
            if (!r.ok()) {
                set(idle, Notice::PlanFailed);
                return;
            }
            plan_ = std::move(r).value();
            preview_ = plan_;
            preview_ready_ = true;
            if (plan_.empty()) {
                // Sifir is: "Acik" gosterilmez; kullaniciya not dusulur.
                set(idle, idle == Phase::On ? idle_notice : Notice::NoChanges);
                return;
            }
            set(Phase::Confirming, Notice::None);
        });
    });
}

void PerfController::answer(bool yes) {
    if (phase_ == Phase::Confirming) {
        if (yes) {
            if (drift_confirm_) start_drift_apply();
            else start_apply();
        } else {
            drift_confirm_ = false;
            plan_ = {};
            set(idle_phase(), off_items_.empty() ? Notice::None : Notice::SomeOff);  // Hayir: hicbir sey degismedi
        }
    } else if (phase_ == Phase::ConfirmRestore) {
        if (yes) start_restore();
        else set(backend_.recovery_needed() ? Phase::RecoveryNeeded : Phase::On, Notice::None);
    }
}

void PerfController::notify_busy() {
    if (phase_ == Phase::Planning || phase_ == Phase::Applying || phase_ == Phase::Restoring) return;
    set(phase_, Notice::AdminBusy);
}

std::string plan_fingerprint(const Plan& p) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::string_view s) {
        for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
        h = (h ^ 0xFF) * 1099511628211ull;
    };
    // P11: arka plan hedefi "bgproc:<pid>:<olusturma>:<yol ozeti>" her gecista degisir; parmak izine yalniz yol ozeti
    // (benzersiz, siralı) girer. Program sayisi/surec kimligi degisse de ayni onayli plan sayilir.
    std::vector<std::string> bg_hashes;
    std::size_t stable_count = 0;
    for (const auto& i : p.items) {
        if (i.op.id == contracts::OperationId::BackgroundProcessLower) {
            const auto pos = i.op.target.rfind(':');
            bg_hashes.push_back(pos == std::string::npos ? i.op.target : i.op.target.substr(pos + 1));
            continue;
        }
        ++stable_count;
        mix(contracts::to_string(i.op.id));
        mix(i.op.target);
        mix(i.op.new_value);
    }
    std::sort(bg_hashes.begin(), bg_hashes.end());
    bg_hashes.erase(std::unique(bg_hashes.begin(), bg_hashes.end()), bg_hashes.end());
    for (const auto& hs : bg_hashes) mix(hs);
    return std::to_string(h) + ":" + std::to_string(stable_count) + ":" + std::to_string(bg_hashes.size());
}

void PerfController::start_apply() {
    // UI-10: yalniz kullanicinin Evet dedigi gruplar uygulanir; secilmeyen oge journal'a da yazilmaz.
    // OPT-10: otomatik (oyun basladi) uygulamada yonetici gruplari atlanir: oyun acilirken surpriz UAC yok.
    const bool skip_admin = auto_pending_;
    const bool scope = experiment_scope_;
    const bool admin_ok = experiment_admin_ok_;  // P1: deney yalniz yonetici gerektirmeyen alt kumeyi uygular
    Plan chosen;
    for (const auto& i : plan_.items)
        if (selected(i.group()) && !(skip_admin && i.needs_admin) && !(scope && !experiment_scope_item(i, admin_ok))) chosen.items.push_back(i);
    if (chosen.empty()) {
        set(idle_phase(), Notice::NoneSelected);
        return;
    }
    drop_lone_restore_point(chosen.items);  // T08: yalniz geri yukleme noktasi icin UAC acilmaz
    if (chosen.empty()) {
        set(idle_phase(), Notice::NoneSelected);
        return;
    }
    plan_ = std::move(chosen);
    const auto approved = plan_fingerprint(plan_);
    cancel_ = false;
    progress_ = {};
    verified_count_ = 0;
    set(Phase::Applying, Notice::None);
    run_([this, approved, skip_admin, scope, admin_ok] {
        // Acilis uzlastirmasi: gercekte kapanmis eski adimlar onayli yeni islemden ONCE kalici KeptExternal olur.
        if (!off_items_.empty()) backend_.retire_off(off_items_);
        // OPT-02: onaydan sonra sistem degistiyse (ornek baska arac ayari degistirdi) eski onayla yazma yok.
        if (auto fresh = backend_.plan(); fresh.ok()) {
            Plan fresh_chosen;
            for (const auto& i : fresh.value().items)
                if (selected(i.group()) && !(skip_admin && i.needs_admin) && !(scope && !experiment_scope_item(i, admin_ok))) fresh_chosen.items.push_back(i);
            drop_lone_restore_point(fresh_chosen.items);
            if (plan_fingerprint(fresh_chosen) != approved) {
                post_([this, full = std::move(fresh).value()]() mutable {
                    plan_ = full;
                    preview_ = std::move(full);
                    if (plan_.empty()) set(idle_phase(), Notice::NoChanges);
                    else set(Phase::Confirming, Notice::PlanChanged);  // guncel liste yeniden onaya
                });
                return;
            }
        }
        const auto out = backend_.apply(plan_, [this](const recovery::Progress& p) {
            post_([this, p] {
                progress_ = p;
                if (on_change) on_change();
            });
        }, cancel_);
        post_([this, out] {
            switch (out.kind) {
                case OutcomeKind::Active:
                    active_changes_ = off_items_.empty() ? out.changes : backend_.active_changes();
                    off_items_.clear();
                    drift_items_.clear();
                    auto_active_ = auto_pending_;
                    auto_pending_ = false;
                    if (out.pending_reboot) reboot_pending_ = true;
                    verified_count_ = out.changes;  // T18: gercek dogrulanan adim sayisi (geri yukleme noktasi dahil degil)
                    // Onaylanan ve uygulanan islem sayisi eslesmeli; eslesmezse kismi sonuc gizlenmez.
                    // UAC reddi ayri ve acik bildirilir: yonetici gruplari atlandi, gerisi etkin.
                    if (out.gate_blocked) set(Phase::On, Notice::GameBlocked);
                    else if (out.admin_skipped) set(Phase::On, Notice::AdminSkipped);
                    else if (out.changes != static_cast<std::size_t>(std::count_if(plan_.items.begin(), plan_.items.end(), [](const PlanItem& i) { return !is_restore_point_item(i); })))
                        set(Phase::On, Notice::PartialApplied);  // geri yukleme noktasi ayar sayisina girmez (T08)
                    else set(Phase::On, out.restore_point == Outcome::RestorePoint::Failed ? Notice::RestorePointFailed : Notice::None);
                    break;
                case OutcomeKind::NothingToDo: set(idle_phase(), Notice::NoChanges); break;
                case OutcomeKind::Denied: set(idle_phase(), Notice::Denied); break;
                case OutcomeKind::GateBlocked: set(idle_phase(), Notice::GameBlocked); break;
                case OutcomeKind::FailedRestored:
                case OutcomeKind::Restored: set(idle_phase(), Notice::FailedRestored); break;
                case OutcomeKind::RecoveryRequired: set(Phase::RecoveryNeeded, Notice::None); break;
            }
            auto_pending_ = false;
        });
    });
}

void PerfController::auto_apply() {
    if (phase_ != Phase::Off) return;
    auto_pending_ = true;
    set(Phase::Planning, Notice::None);
    run_([this] {
        auto r = backend_.plan();
        post_([this, r = std::move(r)]() mutable {
            if (phase_ != Phase::Planning) return;
            if (!r.ok()) {
                auto_pending_ = false;
                set(Phase::Off, Notice::PlanFailed);
                return;
            }
            plan_ = std::move(r).value();
            preview_ = plan_;
            preview_ready_ = true;
            if (plan_.empty()) {
                auto_pending_ = false;
                set(Phase::Off, Notice::NoChanges);
                return;
            }
            start_apply();  // secim filtresi ve bayatlik kontrolu ayni yoldan
        });
    });
}

void PerfController::auto_restore() {
    if (!auto_active_ || phase_ != Phase::On) return;
    start_restore();
}

void PerfController::start_restore() {
    progress_ = {};
    set(Phase::Restoring, Notice::None);
    run_([this] {
        const auto out = backend_.restore([this](const recovery::Progress& p) {
            post_([this, p] {
                progress_ = p;
                if (on_change) on_change();
            });
        });
        post_([this, out] {
            if (out.pending_reboot) reboot_pending_ = true;
            // "Yapacak is yok" tek basina Kapali kaniti degildir: dogrulanmamis kayit kaldiysa yanlis basari gosterilmez.
            if ((out.kind == OutcomeKind::Restored || out.kind == OutcomeKind::NothingToDo) && backend_.recovery_needed()) {
                external_conflict_ = backend_.external_conflict();
                set(Phase::RecoveryNeeded, Notice::RestoreFailed);
            } else if (out.kind == OutcomeKind::Restored || out.kind == OutcomeKind::NothingToDo) {
                external_conflict_ = false;
                active_changes_ = 0;
                off_items_.clear();
                drift_items_.clear();
                plan_ = {};
                verified_count_ = 0;
                auto_active_ = false;
                set(Phase::Off, Notice::None);  // Kapali: geri yukleme dogrulandi
            } else if (out.kind == OutcomeKind::Denied) {
                // OPT-10: UAC reddi -> kullanici kapsami geri alinmis olabilir; kalan (yonetici) degisiklik sayisi.
                if (out.changes) active_changes_ = out.changes;
                const bool rec = backend_.recovery_needed();  // UAC reddi kurtarma durumunu "Acik"a cevirmez
                if (rec) external_conflict_ = backend_.external_conflict();
                set(rec ? Phase::RecoveryNeeded : Phase::On, Notice::Denied);
            } else if (out.kind == OutcomeKind::GateBlocked) {
                // Geri alma baslamadi veya kalan kayitlar etkin: oyun kapaninca yeniden denenir (bozuk "kurtarma" gosterilmez).
                if (out.changes) active_changes_ = out.changes;
                set(backend_.recovery_needed() ? Phase::RecoveryNeeded : Phase::On, Notice::GameBlocked);
            } else {
                external_conflict_ = backend_.external_conflict();
                set(Phase::RecoveryNeeded, Notice::RestoreFailed);
            }
        });
    });
}

void PerfController::keep_external() {
    if (phase_ != Phase::RecoveryNeeded || !external_conflict_) return;
    progress_ = {};
    set(Phase::Restoring, Notice::None);
    run_([this] {
        const auto out = backend_.keep_external();
        post_([this, out] {
            if (out.kind == OutcomeKind::Denied) {
                set(Phase::RecoveryNeeded, Notice::Denied);
                return;
            }
            init();  // gercek durum journal'dan: kalan kayit varsa kurtarma, yoksa Kapali/Acik
        });
    });
}

void PerfController::reapply_drift() {
    if ((phase_ != Phase::On && phase_ != Phase::Off) || drift_items_.empty()) return;
    // Sessiz yeniden uygulama yok (ADR-035): once standart Evet/Hayir onayi; liste sapan ayarlarin grup adlari.
    Plan p;
    for (const auto& d : drift_items_) {
        PlanItem i;
        i.op = d.op;
        i.title_key = d.group_key;
        i.group_key = d.group_key;
        p.items.push_back(std::move(i));
    }
    plan_ = std::move(p);
    drift_confirm_ = true;
    set(Phase::Confirming, Notice::None);
}

void PerfController::start_drift_apply() {
    const auto items = drift_items_;
    drift_confirm_ = false;
    cancel_ = false;
    progress_ = {};
    set(Phase::Applying, Notice::None);
    run_([this, items] {
        const auto out = backend_.reapply_drift(items, [this](const recovery::Progress& p) {
            post_([this, p] {
                progress_ = p;
                if (on_change) on_change();
            });
        });
        post_([this, out] {
            plan_ = {};
            init();  // gercek durum yeniden okunur (sapma kalmadiysa temiz Acik)
            switch (out.kind) {
                case OutcomeKind::Active: break;
                case OutcomeKind::Denied: set(phase_, Notice::Denied); break;
                case OutcomeKind::GateBlocked: set(phase_, Notice::GameBlocked); break;
                default: set(phase_, out.gate_blocked ? Notice::GameBlocked : Notice::DriftFailed); break;
            }
        });
    });
}

void PerfController::keep_drift() {
    if ((phase_ != Phase::On && phase_ != Phase::Off) || drift_items_.empty()) return;
    const auto off = off_items_;
    run_([this, off] {
        backend_.retire_off(off);  // ayar yazilmaz; kullanicinin mevcut degeri kalir (keep_external ile ayni KeptExternal)
        post_([this] { init(); });
    });
}

}  // namespace ef::core::perf
