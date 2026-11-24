#include "core/perf/proof_experiment.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/perf/experiment.h"

namespace ef::core::perf {

namespace {

std::optional<double> sd_of(const std::vector<double>& v) {
    if (v.size() < 2) return std::nullopt;
    double m = 0;
    for (double x : v) m += x;
    m /= static_cast<double>(v.size());
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

ProofMetric make_metric(const char* key, bool higher_is_better, const std::vector<double>& off, const std::vector<double>& on) {
    ProofMetric m;
    m.key = key;
    m.higher_is_better = higher_is_better;
    const auto c = compare_metric(key, higher_is_better ? Direction::HigherIsBetter : Direction::LowerIsBetter, off, on);
    m.n_off = c.n_before;
    m.n_on = c.n_after;
    m.mean_off = c.mean_before;
    m.mean_on = c.mean_after;
    m.sd_off = sd_of(off);
    m.sd_on = sd_of(on);
    m.diff = c.diff;
    m.ci_low = c.ci_low;
    m.ci_high = c.ci_high;
    m.stat = c.verdict;
    m.improved = c.improved;
    return m;
}

std::string join(const std::set<std::string>& keys) {
    std::string out;
    for (const auto& k : keys) {
        if (!out.empty()) out += ',';
        out += k;
    }
    return out;
}

}  // namespace

std::string_view to_string(ProofVerdict v) noexcept {
    switch (v) {
        case ProofVerdict::Better: return "better";
        case ProofVerdict::Worse: return "worse";
        case ProofVerdict::NoDifference: return "no_difference";
        case ProofVerdict::Mixed: return "mixed";
        case ProofVerdict::InsufficientData: return "insufficient_data";
        case ProofVerdict::ConditionsDiffer: return "conditions_differ";
        case ProofVerdict::InvalidRun: return "invalid_run";
        case ProofVerdict::Aborted: return "aborted";
        case ProofVerdict::RestoreFailed: return "restore_failed";
    }
    return "aborted";
}

bool has_verdict(ProofVerdict v) noexcept {
    return v == ProofVerdict::Better || v == ProofVerdict::Worse || v == ProofVerdict::NoDifference || v == ProofVerdict::Mixed;
}

std::vector<std::string> update_no_effect_groups(std::vector<std::string> current, const std::vector<std::string>& scope_groups,
                                                 ProofVerdict verdict) {
    if (!has_verdict(verdict)) return current;  // hukum yok: kayit degismez
    const auto in = [](const std::vector<std::string>& v, const std::string& k) { return std::find(v.begin(), v.end(), k) != v.end(); };
    if (verdict != ProofVerdict::NoDifference) {
        current.erase(std::remove_if(current.begin(), current.end(), [&](const std::string& k) { return in(scope_groups, k); }), current.end());
        return current;
    }
    for (const auto& k : scope_groups)
        if (!k.empty() && k.size() <= 128 && !in(current, k) && current.size() < kMaxNoEffectGroups) current.push_back(k);
    return current;
}

ProofResult evaluate_proof(const std::vector<ProofRun>& runs, bool quality_tradeoff) {
    ProofResult r;
    r.quality_tradeoff = quality_tradeoff;
    std::vector<double> avg_off, avg_on, low_off, low_on, p99_off, p99_on;
    for (const auto& x : runs) {
        if (!x.valid) continue;
        const bool on = x.arm == ProofArm::On;
        (on ? r.valid_on : r.valid_off) += 1;
        (on ? avg_on : avg_off).push_back(x.avg_fps);
        if (x.low1_fps) (on ? low_on : low_off).push_back(*x.low1_fps);
        if (x.p99_ms) (on ? p99_on : p99_off).push_back(*x.p99_ms);
    }
    r.avg_fps = make_metric("avg_fps", true, avg_off, avg_on);
    r.low1_fps = make_metric("low1_fps", true, low_off, low_on);
    r.p99_ms = make_metric("p99_ms", false, p99_off, p99_on);

    // 1) gecersiz kosu: hukum yok (gecersizi atip hesaplamak ABBA dengesini bozar).
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (runs[i].valid) continue;
        r.verdict = ProofVerdict::InvalidRun;
        r.reason = runs[i].invalid_reason;
        r.bad_run = i + 1;
        return r;
    }
    // 2) parmak izi: ilk kosuyla farkli alanlar.
    std::set<std::string> differing;
    if (!runs.empty()) {
        const auto& base = runs.front().conditions;
        for (const auto& x : runs) {
            for (const auto& [k, v] : base) {
                const auto it = x.conditions.find(k);
                if (it == x.conditions.end() || it->second != v) differing.insert(k);
            }
            for (const auto& [k, v] : x.conditions)
                if (!base.count(k)) differing.insert(k);
        }
    }
    if (!differing.empty()) {
        r.verdict = ProofVerdict::ConditionsDiffer;
        r.reason = join(differing);
        return r;
    }
    // 3) yeterli kosu.
    if (r.valid_off < kProofMinRunsPerArm || r.valid_on < kProofMinRunsPerArm) {
        r.verdict = ProofVerdict::InsufficientData;
        return r;
    }
    // 4) hukum: yalniz ortalama FPS araligi; %1 dusuk FPS ters yonde anlamliysa "karisik".
    const auto& a = r.avg_fps;
    const auto& l = r.low1_fps;
    const bool low_sig = l.stat == Verdict::Significant && l.improved;
    if (a.stat == Verdict::Significant && a.improved) {
        if (low_sig && *l.improved != *a.improved) r.verdict = ProofVerdict::Mixed;
        else r.verdict = *a.improved ? ProofVerdict::Better : ProofVerdict::Worse;
        return r;
    }
    if (a.stat == Verdict::NoSignificantDifference) {
        r.verdict = ProofVerdict::NoDifference;
        r.low1_differs = low_sig;
        return r;
    }
    r.verdict = ProofVerdict::InsufficientData;  // ortalama FPS karsilastirilamadi (ornek yetersiz)
    return r;
}

std::string_view to_string(ProofRefusal r) noexcept {
    switch (r) {
        case ProofRefusal::None: return "none";
        case ProofRefusal::GameRunning: return "game_running";
        case ProofRefusal::OtherTestBusy: return "other_test_busy";
        case ProofRefusal::PerfNotOff: return "perf_not_off";
        case ProofRefusal::PlanNotReady: return "plan_not_ready";
        case ProofRefusal::PlanEmpty: return "plan_empty";
        case ProofRefusal::PlanNeedsAdmin: return "plan_needs_admin";
        case ProofRefusal::PlanNeedsReboot: return "plan_needs_reboot";
        case ProofRefusal::PlanBackgroundOnly: return "plan_background_only";
        case ProofRefusal::NoHardwareAdapter: return "no_hardware_adapter";
    }
    return "none";
}

ProofRefusal check_proof_preconditions(const ProofPreconditions& p) noexcept {
    if (p.game_blocked) return ProofRefusal::GameRunning;
    if (p.other_test_busy) return ProofRefusal::OtherTestBusy;
    if (!p.perf_off) return ProofRefusal::PerfNotOff;
    if (!p.plan_ready) return ProofRefusal::PlanNotReady;
    if (p.plan_empty) return ProofRefusal::PlanEmpty;
    if (p.scope_groups == 0) {  // olculecek alt kume bos: neden acik soylenir
        if (p.excluded_admin) return ProofRefusal::PlanNeedsAdmin;
        if (p.excluded_reboot) return ProofRefusal::PlanNeedsReboot;
        return ProofRefusal::PlanBackgroundOnly;
    }
    if (!p.hardware_adapter) return ProofRefusal::NoHardwareAdapter;
    return ProofRefusal::None;
}

namespace {
std::vector<std::optional<ProofArm>> make_schedule(int pairs, int warmup) {
    std::vector<std::optional<ProofArm>> s;
    for (const auto& x : abba_schedule(pairs, warmup)) {
        if (!x) s.push_back(std::nullopt);
        else s.push_back(*x == Arm::A ? ProofArm::Off : ProofArm::On);  // A = Kapali, B = Acik
    }
    return s;
}
}  // namespace

ProofPlanSize proof_plan_size(int pairs, int warmup) {
    const auto s = make_schedule(pairs, warmup);
    ProofPlanSize z;
    z.runs_total = s.size();
    ProofArm cur = ProofArm::Off;
    for (const auto& x : s) {
        if (x) ++z.runs_scored;
        const ProofArm want = x ? *x : ProofArm::Off;
        if (want != cur) ++z.toggles, cur = want;
    }
    if (cur == ProofArm::On) ++z.toggles;  // bitis: ilk duruma (Kapali) donus
    return z;
}

double estimate_proof_seconds(double planned_step_seconds, int pairs, int warmup) {
    const auto z = proof_plan_size(pairs, warmup);
    return static_cast<double>(z.runs_total) * (planned_step_seconds + kProofRunOverheadS) + static_cast<double>(z.toggles) * kProofToggleBudgetS;
}

int estimate_proof_minutes(double planned_step_seconds, int pairs, int warmup) {
    const double m = std::ceil(estimate_proof_seconds(planned_step_seconds, pairs, warmup) / 60.0);
    return std::max(1, static_cast<int>(m));
}

std::string_view to_string(ProofPhase p) noexcept {
    switch (p) {
        case ProofPhase::Idle: return "idle";
        case ProofPhase::AwaitConsent: return "await_consent";
        case ProofPhase::Running: return "running";
        case ProofPhase::Restoring: return "restoring";
        case ProofPhase::Done: return "done";
        case ProofPhase::Aborted: return "aborted";
    }
    return "idle";
}

ProofSession::ProofSession(int pairs, int warmup, bool quality_tradeoff)
    : schedule_(make_schedule(pairs, warmup)), quality_tradeoff_(quality_tradeoff) {}

void ProofSession::begin() {
    if (phase_ == ProofPhase::Idle) phase_ = ProofPhase::AwaitConsent;
}

void ProofSession::consent(bool yes) {
    if (phase_ != ProofPhase::AwaitConsent) return;
    phase_ = yes ? ProofPhase::Running : ProofPhase::Idle;  // Hayir: hicbir sey degismedi
}

std::optional<ProofAction> ProofSession::next() const {
    if (phase_ == ProofPhase::Restoring) return ProofAction{ProofActionKind::RestoreOriginal, ProofArm::Off, false, 0};
    if (phase_ != ProofPhase::Running || idx_ >= schedule_.size()) return std::nullopt;
    const bool scored = schedule_[idx_].has_value();
    const ProofArm want = scored ? *schedule_[idx_] : ProofArm::Off;
    if (want != arm_) return ProofAction{ProofActionKind::SetArm, want, scored, idx_};
    return ProofAction{ProofActionKind::MeasureRun, want, scored, idx_};
}

void ProofSession::on_arm_set(ProofArmOutcome o) {
    const auto a = next();
    if (!a || a->kind != ProofActionKind::SetArm) return;
    if (o == ProofArmOutcome::Applied) {
        arm_ = a->arm;
        return;
    }
    if (o == ProofArmOutcome::Unverified) {
        stop(3);  // geri alma dogrulanamadi: bu modul daha fazla bir sey deneyemez
        return;
    }
    if (stop_ == 0) stop_reason_ = "arm_switch_failed";
    stop(4);  // gecis uygulanmadi: kalan kosular atilir, mevcut kol korunur
}

void ProofSession::on_run(ProofRun run) {
    const auto a = next();
    if (!a || a->kind != ProofActionKind::MeasureRun) return;
    if (a->scored) {
        run.arm = a->arm;  // kol zamanlamadan gelir; cagiran yanlis yazamaz
        runs_.push_back(std::move(run));
        if (!runs_.back().valid) stop(2);
    }
    ++idx_;
    settle();
}

void ProofSession::on_restore(ProofArmOutcome o) {
    if (phase_ != ProofPhase::Restoring) return;
    if (o == ProofArmOutcome::Applied) arm_ = ProofArm::Off;
    else stop_ = 3;
    finish_terminal();
}

void ProofSession::cancel() { abort("cancelled"); }

void ProofSession::abort(std::string reason) {
    if (phase_ == ProofPhase::AwaitConsent) {
        phase_ = ProofPhase::Idle;
        return;
    }
    if (phase_ != ProofPhase::Running) return;
    if (stop_ == 0) stop_reason_ = std::move(reason);
    stop(1);
}

void ProofSession::stop(int why) {
    if (stop_ == 0 || why == 3) stop_ = why;
    settle();
}

void ProofSession::settle() {
    if (terminal() || phase_ == ProofPhase::Restoring) return;
    if (stop_ == 3) return finish_terminal();
    if (stop_ != 0 || idx_ >= schedule_.size()) {
        if (arm_ == ProofArm::On) phase_ = ProofPhase::Restoring;  // Acik kaldiysa ilk duruma don
        else finish_terminal();
    }
}

void ProofSession::finish_terminal() {
    ProofResult r;
    if (stop_ == 3) {
        r = evaluate_proof(runs_, quality_tradeoff_);
        r.verdict = ProofVerdict::RestoreFailed;
    } else if (stop_ == 1 || stop_ == 4) {
        r = evaluate_proof(runs_, quality_tradeoff_);
        r.verdict = ProofVerdict::Aborted;
        r.reason = stop_reason_;
    } else {
        r = evaluate_proof(runs_, quality_tradeoff_);  // stop_ == 2 ise InvalidRun
    }
    result_ = std::move(r);
    phase_ = stop_ == 0 ? ProofPhase::Done : ProofPhase::Aborted;
}

ProofProgress ProofSession::progress() const {
    ProofProgress p;
    for (const auto& x : schedule_)
        if (x) ++p.scored_total;
    p.scored_done = runs_.size();
    p.run_index = std::min(idx_, schedule_.empty() ? std::size_t{0} : schedule_.size() - 1);
    p.in_warmup = idx_ < schedule_.size() && !schedule_[idx_].has_value();
    p.arm = arm_;
    return p;
}

}  // namespace ef::core::perf
