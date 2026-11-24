#include "core/perf/system_check.h"

namespace ef::core::perf {

std::string_view to_string(FixKind k) noexcept {
    switch (k) {
        case FixKind::Auto: return "auto";
        case FixKind::GuidedBios: return "guided_bios";
        case FixKind::GuidedDriver: return "guided_driver";
        case FixKind::GuidedHardware: return "guided_hardware";
        case FixKind::GuidedAdmin: return "guided_admin";
        case FixKind::GuidedWindowsSetting: return "guided_windows_setting";
    }
    return "guided_hardware";
}

void SystemCheck::set(CheckPhase p) {
    phase_ = p;
    if (on_change) on_change();
}

void SystemCheck::start() {
    if (phase_ != CheckPhase::Idle && phase_ != CheckPhase::Report && phase_ != CheckPhase::Failed) return;
    report_.reset();
    set(CheckPhase::Scanning);
    d_.run_background([this] {
        auto f = d_.scan();
        d_.post_ui([this, f = std::move(f)]() mutable {
            findings_ = std::move(f);
            selected_.clear();
            for (auto& x : findings_) {
                // Guvenlik: otomatik isaretli ama plan ogesi yoksa veya yonetici isterse rehberliye dusurulur.
                if (x.kind == FixKind::Auto && (!x.item || x.item->needs_admin)) x.kind = FixKind::GuidedAdmin;
                selected_.push_back(x.kind == FixKind::Auto);  // varsayilan secili; kullanici Hayir diyebilir
            }
            set(CheckPhase::Review);
        });
    });
}

void SystemCheck::set_selected(std::size_t i, bool on) {
    if (phase_ != CheckPhase::Review || i >= findings_.size() || findings_[i].kind != FixKind::Auto) return;
    selected_[i] = on;
    if (on_change) on_change();
}

void SystemCheck::answer(bool yes, bool measure) {
    if (phase_ != CheckPhase::Review) return;
    CheckReport rep;
    Plan plan;
    for (std::size_t i = 0; i < findings_.size(); ++i) {
        const auto& f = findings_[i];
        if (f.kind != FixKind::Auto) rep.guided.push_back(f);
        else if (yes && selected_[i]) {
            plan.items.push_back(*f.item);
            for (const auto& x : f.extra_items) plan.items.push_back(x);
        }
        else ++rep.auto_skipped_by_user;
    }
    if (plan.empty()) {
        rep.outcome = OutcomeKind::NothingToDo;  // Hayir veya secim yok: hicbir sey yazilmadi
        report_ = rep;
        set(CheckPhase::Report);
        return;
    }
    done_ = 0;
    total_ = plan.items.size();
    set(CheckPhase::Applying);
    d_.run_background([this, plan = std::move(plan), rep, measure]() mutable {
        const auto out = d_.apply(plan, [this](std::size_t d, std::size_t t) {
            d_.post_ui([this, d, t] {
                done_ = d, total_ = t;
                if (on_change) on_change();
            });
        });
        rep.outcome = out.kind;
        rep.auto_applied = out.kind == OutcomeKind::Active ? out.changes : 0;
        if (out.kind == OutcomeKind::Active && measure && d_.measure_ab) {
            d_.post_ui([this] { set(CheckPhase::Measuring); });
            rep.ab_delta = d_.measure_ab();
        }
        d_.post_ui([this, rep] {
            report_ = rep;
            set(rep.outcome == OutcomeKind::Active || rep.outcome == OutcomeKind::NothingToDo ? CheckPhase::Report : CheckPhase::Failed);
        });
    });
}

}  // namespace ef::core::perf
