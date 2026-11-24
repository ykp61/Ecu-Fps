#include "core/startup/session.h"

#include <algorithm>

#include "core/recovery/conflicts.h"

namespace ef::core::startup {

using sa::Advice;
using sa::AdviceRule;

bool row_editable(const StartupRow& r) noexcept {
    if (sa::requires_admin(r.entry.location)) return false;  // broker (OPT-02) gerekir
    switch (r.rec.rule) {
        case AdviceRule::Security:
        case AdviceRule::AntiCheat:
        case AdviceRule::WindowsComponent: return false;  // korunur
        default: return true;
    }
}

void sort_rows(std::vector<StartupRow>& rows) {
    auto rank = [](Advice a) { return a == Advice::RecommendDisable ? 0 : a == Advice::NoAdvice ? 1 : 2; };
    std::stable_sort(rows.begin(), rows.end(), [&](const StartupRow& a, const StartupRow& b) {
        const int ra = rank(a.rec.advice), rb = rank(b.rec.advice);
        if (ra != rb) return ra < rb;
        return a.entry.name < b.entry.name;
    });
}

void StartupSession::set(StartupPhase p) {
    phase_ = p;
    if (on_change) on_change();
}

void StartupSession::finish_scan(contracts::Result<ScanResult> r) {
    if (!r.ok()) {
        set(StartupPhase::Failed);  // last_ korunur: uygulanan degisiklikler "hic degismedi" gibi gosterilmez
        return;
    }
    rows_ = std::move(r.value().rows);
    sort_rows(rows_);
    services_ = std::move(r.value().services);
    desired_.clear();
    for (const auto& row : rows_) desired_.push_back(row.entry.approval == sa::Approval::Enabled);
    set(StartupPhase::Ready);
}

void StartupSession::scan() {
    if (phase_ == StartupPhase::Scanning || phase_ == StartupPhase::Applying || phase_ == StartupPhase::Confirming) return;
    set(StartupPhase::Scanning);
    d_.run_background([this] {
        auto r = d_.scan();
        d_.post_ui([this, r = std::move(r)]() mutable { finish_scan(std::move(r)); });
    });
}

bool StartupSession::editable(std::size_t i) const { return i < rows_.size() && row_editable(rows_[i]); }

bool StartupSession::desired(std::size_t i) const { return i < desired_.size() && desired_[i]; }

void StartupSession::set_desired(std::size_t i, bool enabled) {
    if (phase_ != StartupPhase::Ready || !editable(i)) return;
    desired_[i] = enabled;
    if (on_change) on_change();
}

std::size_t StartupSession::pending() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (desired_[i] != (rows_[i].entry.approval == sa::Approval::Enabled)) ++n;
    return n;
}

void StartupSession::request_apply() {
    if (phase_ != StartupPhase::Ready || pending() == 0) return;
    set(StartupPhase::Confirming);
}

void StartupSession::answer(bool yes) {
    if (phase_ != StartupPhase::Confirming) return;
    if (!yes) {
        set(StartupPhase::Ready);  // Hayir: hicbir sey yazilmaz, secim korunur
        return;
    }
    std::vector<std::pair<sa::Entry, bool>> todo;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (desired_[i] != (rows_[i].entry.approval == sa::Approval::Enabled)) todo.push_back({rows_[i].entry, desired_[i]});
    set(StartupPhase::Applying);
    d_.run_background([this, todo = std::move(todo)] {
        ChangeResult cr;
        for (const auto& [e, en] : todo) {
            if (d_.gate_denied && cr.gate_reason.empty()) cr.gate_reason = d_.gate_denied();
            if (!cr.gate_reason.empty()) {
                ++cr.skipped;  // Evet'ten sonra oyun acildi/algilama bozuldu: yeni girdi degistirilmez
                continue;
            }
            const auto st = d_.change(e, en);
            if (st.ok()) ++cr.applied;
            else if (st.error().code == contracts::ErrorCode::GateBlocked) {
                cr.gate_reason = st.error().detail;
                ++cr.skipped;
            } else {
                ++cr.failed;
            }
        }
        auto r = d_.scan();  // sonuc tahmin edilmez: yeniden okunur
        d_.post_ui([this, cr, r = std::move(r)]() mutable {
            last_ = cr;
            finish_scan(std::move(r));
        });
    });
}

}  // namespace ef::core::startup

namespace ef::core::startup {

using contracts::ErrorCode;
using contracts::Status;
using contracts::TxState;

contracts::OperationId operation_for(sa::Location l) noexcept {
    return (l == sa::Location::UserFolder || l == sa::Location::CommonFolder) ? contracts::OperationId::StartupFolderItem
                                                                              : contracts::OperationId::StartupRunEntry;
}

std::vector<std::string> active_targets(recovery::JournalStore& store) {
    std::vector<std::string> out;
    for (const auto& tx : store.scan().transactions)
        if (tx.state == TxState::Active)
            for (const auto& st : tx.steps) out.push_back(st.op.target);
    return out;
}

Status change_entry(ChangeContext& c, const sa::Entry& e, bool enable) {
    if (!c.store || !c.engine || !c.api) return Status::fail(ErrorCode::IoError, "startup journal unavailable");
    if (sa::requires_admin(e.location)) return Status::fail(ErrorCode::Denied, "admin location requires broker (OPT-02)");
    const auto target = sa::format_target(e.location, e.name);
    const std::string want = enable ? "enabled" : "disabled";
    // 1) Bu girdinin etkin islemi varsa geri al (onceki deger birebir).
    const auto same_target = [&target](const contracts::SettingOperation& o) { return o.target == target; };
    for (auto& tx : c.store->scan().transactions) {
        const bool hit = std::any_of(tx.steps.begin(), tx.steps.end(), [&](const auto& st) { return st.op.target == target; });
        if (!hit) continue;
        // Bu girdi disaridan degismis ve eski kayit takiliysa: kullanici simdi acik karar verdi; eski kayit kapanir, deger ezilmez.
        if (recovery::has_external_conflict(tx)) {
            (void)recovery::keep_external_values(std::move(tx), *c.store, same_target);
            continue;
        }
        if (tx.state != TxState::Active) continue;
        auto r = c.engine->rollback(std::move(tx));
        if (!r.ok()) return Status(r.error());
        if (r.value().state != TxState::Restored) {
            if (!recovery::has_external_conflict(r.value())) return Status::fail(ErrorCode::IoError, "rollback not verified");
            (void)recovery::keep_external_values(std::move(r).value(), *c.store, same_target);
        }
    }
    // 2) Simdiki durum istenen mi? Degilse yeni islem.
    auto raw = c.api->get_raw(e.location, e.name);
    if (!raw.ok()) return Status(raw.error());
    const bool now_enabled = !raw.value() || sa::approval_from_raw(*raw.value()) == sa::Approval::Enabled;
    if (now_enabled == enable) return contracts::ok_status();
    recovery::JournalTx tx;
    tx.id = c.new_tx_id ? c.new_tx_id() : std::string(32, '0');
    tx.actor_sid = c.actor_sid;
    tx.target_sid = c.target_sid;
    tx.created_unix_ms = c.now_unix_ms ? c.now_unix_ms() : 0;
    recovery::JournalStep st;
    st.op = contracts::SettingOperation{operation_for(e.location), contracts::OperationScope::CurrentUser, target, want, false, false};
    tx.steps.push_back(std::move(st));
    auto r = c.engine->apply(std::move(tx));
    if (!r.ok()) return Status(r.error());
    if (r.value().state != TxState::Active) return Status::fail(ErrorCode::IoError, "apply not verified; restored");
    return contracts::ok_status();
}

}  // namespace ef::core::startup
