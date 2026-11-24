#include "core/perf/journal_backend.h"

#include <algorithm>

#include "core/perf/admin_plan.h"
#include "core/recovery/conflicts.h"

namespace ef::core::perf {

using contracts::TxState;
using core::ElevationOutcome;

Outcome JournalBackend::apply(const Plan& plan, const ProgressFn& progress, const std::atomic<bool>& cancel) {
    if (plan.empty()) return {OutcomeKind::NothingToDo, 0, "empty plan"};
    // CORE-03: bilinmeyen yeni sema varken yeni degisiklik yasak (acilista run_startup_recovery'nin bildirdigi
    // sozlesme; broker disi UI yolu da ayni kurala uyar). Geri alma bu kapidan gecmez.
    if (d_.writes_blocked && d_.writes_blocked())
        return {OutcomeKind::Denied, 0, "journal written by a newer version; new changes are blocked"};
    if (d_.permit) {
        if (const auto v = d_.permit(); !v.allowed) {
            Outcome o{OutcomeKind::GateBlocked, 0, "gate blocked before apply"};
            o.gate_reason = v.reason;
            return o;
        }
    }
    std::vector<PlanItem> user, admin;
    for (const auto& i : plan.items) (i.needs_admin ? admin : user).push_back(i);
    drop_lone_restore_point(admin);  // T08: baska yonetici ogesi yoksa geri yukleme noktasi icin UAC acilmaz
    if (!admin.empty() && !d_.admin) {
        if (!d_.elevated) return {OutcomeKind::Denied, 0, "admin step requires broker (OPT-02)"};
        user = plan.items;  // surec zaten yukseltilmis ve kanal yok: tum adimlar ayni journal'da
        user.erase(std::remove_if(user.begin(), user.end(), is_restore_point_item), user.end());  // nokta journal adimi degildir
        admin.clear();
    }
    // T18: ilerlemeye adim sayisi ve grup anahtari eklenir (motor grubu bilmez). Kullanici adimlari once, sonra tek UAC'li yonetici grubu.
    const std::size_t total_steps = user.size() + admin.size();
    recovery::Progress last_progress;
    const ProgressFn user_progress = [&](const recovery::Progress& p) {
        last_progress = p;
        last_progress.steps = total_steps;
        if (p.step >= 1 && p.step <= user.size()) last_progress.group = user[p.step - 1].group();
        if (progress) progress(last_progress);
    };
    std::size_t applied = 0;
    if (!user.empty()) {
        if (!d_.scope.store || !d_.scope.engine) return {OutcomeKind::FailedRestored, 0, "journal unavailable"};
        recovery::JournalTx tx;
        tx.id = d_.new_tx_id();
        tx.actor_sid = d_.actor_sid;
        tx.target_sid = d_.target_sid;
        tx.created_unix_ms = d_.now_unix_ms ? d_.now_unix_ms() : 0;
        for (const auto& i : user) {
            recovery::JournalStep st;
            st.op = i.op;
            tx.steps.push_back(std::move(st));
        }
        recovery::EngineOptions opt;
        opt.on_progress = user_progress;
        opt.cancel = &cancel;
        auto r = d_.scope.engine->apply(std::move(tx), opt);
        // Journal yazilamadiysa motor hicbir sey uygulamaz. Yeni sema reddi "geri alindi" diye gosterilmez.
        if (!r.ok()) {
            if (r.code() == contracts::ErrorCode::SchemaTooNew) return {OutcomeKind::Denied, 0, r.error().detail};
            if (r.code() == contracts::ErrorCode::GateBlocked) {
                Outcome o{OutcomeKind::GateBlocked, 0, "gate blocked; rolled back"};
                o.gate_reason = r.error().detail;
                return o;
            }
            return {OutcomeKind::FailedRestored, 0, r.error().detail};
        }
        switch (r.value().state) {
            case TxState::Active: applied = user.size(); break;
            case TxState::Restored: return {OutcomeKind::FailedRestored, 0, "apply failed or cancelled; restored"};
            default: return {OutcomeKind::RecoveryRequired, 0, std::string(contracts::to_string(r.value().state))};
        }
    }
    if (admin.empty()) return {OutcomeKind::Active, applied, {}};
    if (cancel) {  // kullanici iptal etti: UAC hic istenmez
        // L2: iptal "yonetici izni verilmedi" degildir; admin_skipped bildirimi gosterilmez.
        return {applied ? OutcomeKind::Active : OutcomeKind::FailedRestored, applied, "cancelled before admin group"};
    }
    if (d_.permit) {  // UAC istemi acilmadan hemen once: kullanici adimlari surerken oyun acilmis olabilir
        if (const auto v = d_.permit(); !v.allowed) {
            Outcome o{applied ? OutcomeKind::Active : OutcomeKind::GateBlocked, applied, "gate blocked before admin group"};
            o.gate_blocked = applied != 0;
            o.gate_reason = v.reason;
            return o;
        }
    }
    // Tek UAC: yonetici grubunun tum islemleri tek broker oturumunda.
    restore_point_first(admin);  // T08: nokta ilk adim; ayni broker oturumunda (run_admin_batch ayri RestorePoint istegi gonderir)
    if (progress) {  // T18: UAC'li grup tek istekte calisir; adim ilk yonetici grubundan baslar (ilerleme sayaci user ile ayni birimde kalir)
        auto p = last_progress;
        p.phase = recovery::Phase::Applying;
        p.step = user.size() + 1;
        p.steps = total_steps;
        p.group = admin.front().group();
        progress(p);
    }
    std::vector<contracts::SettingOperation> ops;
    for (const auto& i : admin) ops.push_back(i.op);
    const bool rp_in_batch = std::any_of(admin.begin(), admin.end(), is_restore_point_item);
    const std::size_t setting_steps = admin.size() - (rp_in_batch ? 1 : 0);
    auto b = d_.admin->run(ipc::RequestAction::Apply, ops);
    Outcome::RestorePoint rp = Outcome::RestorePoint::NotRun;
    if (rp_in_batch && b.elevation == ElevationOutcome::Started) {
        // Nokta ayar degildir: adim sayisina girmez, basarisizligi plani durdurmaz (yalniz durust not).
        const auto it = std::find_if(b.steps.begin(), b.steps.end(), [](const broker::StepResult& s) { return s.target == recovery::kRestorePointTarget; });
        if (it != b.steps.end()) {
            rp = it->state == broker::StepState::Applied ? Outcome::RestorePoint::Created
                 : it->state == broker::StepState::NothingToDo ? Outcome::RestorePoint::Skipped
                                                               : Outcome::RestorePoint::Failed;
            b.steps.erase(it);
        } else rp = Outcome::RestorePoint::Failed;
    }
    if (b.elevation != ElevationOutcome::Started) {
        if (applied == 0) return {OutcomeKind::Denied, 0, "admin group not started: " + b.detail};
        Outcome o{OutcomeKind::Active, applied, "admin group skipped: " + b.detail};
        o.admin_skipped = true;  // gerisi uygulandi; kismi sonuc acik
        return o;
    }
    const std::size_t ok = b.count(broker::StepState::Applied);
    Outcome o;
    o.pending_reboot = b.any_pending_reboot();
    o.restore_point = rp;
    o.changes = applied + ok;
    if (b.count(broker::StepState::RecoveryRequired)) {
        o.kind = OutcomeKind::RecoveryRequired;
        o.detail = "admin step rollback not verified";
        return o;
    }
    const std::size_t gate = b.count(broker::StepState::GateBlocked);
    if (gate) {
        for (const auto& st : b.steps)
            if (st.state == broker::StepState::GateBlocked) o.gate_reason = st.detail;
    }
    if (o.changes == 0) {
        o.kind = gate ? OutcomeKind::GateBlocked : OutcomeKind::FailedRestored;
        o.detail = gate ? "broker gate blocked" : "admin steps failed; nothing changed";
        return o;
    }
    o.gate_blocked = gate != 0;
    o.kind = OutcomeKind::Active;
    if (ok < setting_steps) o.detail = "some admin steps failed or were rejected (broker restored them)";
    return o;
}

namespace {
Outcome restore_scope(const recovery::JournalScope& scope, const ProgressFn& progress) {
    if (!scope.store || !scope.engine) return {OutcomeKind::RecoveryRequired, 0, "journal unavailable"};
    auto list = scope.store->scan();
    std::vector<recovery::JournalTx> todo;
    for (auto& tx : list.transactions)
        if (tx.state == TxState::Active || tx.state == TxState::RecoveryRequired) todo.push_back(std::move(tx));
    if (todo.empty()) return {OutcomeKind::NothingToDo, 0, {}};
    // En yeni islem once geri alinir (ters sira).
    std::sort(todo.begin(), todo.end(), [](const auto& a, const auto& b) { return a.created_unix_ms > b.created_unix_ms; });
    std::size_t total = 0, base = 0;
    for (const auto& t : todo) total += t.steps.size();
    bool all_ok = true;
    for (auto& t : todo) {
        recovery::EngineOptions opt;
        const std::size_t n = t.steps.size();
        opt.on_progress = [&](const recovery::Progress& p) {
            if (!progress) return;
            recovery::Progress agg = p;
            agg.total = total;
            agg.done = base + std::min(p.done, n);
            progress(agg);
        };
        auto r = scope.engine->rollback(std::move(t), opt);
        if (!r.ok() && r.code() == contracts::ErrorCode::GateBlocked) {
            // Kayit etkin kaldi (durum degismedi); kalan islemler de baslatilmaz.
            Outcome o{OutcomeKind::GateBlocked, total - base, "gate blocked before rollback"};  // tasinmis kayit sayilmaz
            o.gate_reason = r.error().detail;
            return o;
        }
        if (!r.ok() || r.value().state != TxState::Restored) all_ok = false;
        base += n;
    }
    return all_ok ? Outcome{OutcomeKind::Restored, 0, {}} : Outcome{OutcomeKind::RecoveryRequired, 0, "rollback not verified"};
}
}  // namespace

namespace {
std::string_view outcome_text(OutcomeKind k) noexcept {
    switch (k) {
        case OutcomeKind::Active: return "active";
        case OutcomeKind::NothingToDo: return "nothing_to_do";
        case OutcomeKind::Restored: return "restored";
        case OutcomeKind::FailedRestored: return "failed_restored";
        case OutcomeKind::RecoveryRequired: return "recovery_required";
        case OutcomeKind::Denied: return "denied";
        case OutcomeKind::GateBlocked: return "gate_blocked";
    }
    return "?";
}
}  // namespace

Outcome JournalBackend::restore(const ProgressFn& progress) {
    admin_ran_ = false;
    last_admin_ = {};
    const Outcome out = restore_impl(progress);
    {
        std::vector<std::string> reboot;
        for (const auto& st : last_admin_.steps)
            if (st.pending_reboot) reboot.push_back(st.target);
        const std::lock_guard<std::mutex> lk(reboot_mu_);
        reboot_targets_ = std::move(reboot);
    }
    if (!d_.restore_log) return out;
    // Saha 2026-11-15: arkadasin gonderecegi logs/restore.log. Kisisel veri yok (redact_for_support); kisa kimlikler.
    if (admin_ran_) {
        log("admin elevation=" + std::string(core::to_string(last_admin_.elevation)) + " steps=" + std::to_string(last_admin_.steps.size()) +
            (last_admin_.detail.empty() ? std::string() : " detail=\"" + recovery::sanitize_support_text(last_admin_.detail, 160) + "\""));
        for (const auto& st : last_admin_.steps)
            log("admin step setting=" + recovery::sanitize_support_text(st.target, 96) + " state=" + std::string(broker::to_string(st.state)) +
                (st.pending_reboot ? " reboot=1" : "") + (st.unknown ? " unknown=1" : "") +
                (st.detail.empty() ? std::string() : " detail=\"" + recovery::sanitize_support_text(st.detail, 160) + "\""));
    }
    const auto failed = out.kind == OutcomeKind::Restored || out.kind == OutcomeKind::NothingToDo || out.kind == OutcomeKind::GateBlocked
                            ? std::vector<recovery::FailedRestoreItem>{}
                            : failed_items();
    log("restore result=" + std::string(outcome_text(out.kind)) + " failed_items=" + std::to_string(failed.size()) +
        (out.pending_reboot ? " reboot=1" : "") +
        (out.detail.empty() ? std::string() : " detail=\"" + recovery::sanitize_support_text(out.detail, 200) + "\""));
    for (const auto& i : failed) log(recovery::restore_log_line(i));
    return out;
}

std::vector<recovery::FailedRestoreItem> JournalBackend::failed_items() {
    std::vector<recovery::FailedRestoreItem> out;
    if (d_.scope.store) out = recovery::failed_restore_items(d_.scope.name, d_.scope.store->scan());
    if (d_.admin)
        for (auto& i : d_.admin->machine_failed_items())
            if (i.scope == "machine") out.push_back(std::move(i));  // makine duzeltmeleri Performans'a ait degil
    // Yapisal "yeniden baslatma bekliyor": son yonetici partisi ayni hedef icin pending_reboot bildirdiyse (HAGS).
    const std::lock_guard<std::mutex> lk(reboot_mu_);
    for (auto& i : out) {
        if (i.journal_unreadable || i.reason == recovery::RestoreFailReason::External || i.reason == recovery::RestoreFailReason::Denied) continue;
        if (std::find(reboot_targets_.begin(), reboot_targets_.end(), i.target) != reboot_targets_.end())
            i.reason = recovery::RestoreFailReason::NeedsRestart;
    }
    return out;
}

Outcome JournalBackend::restore_impl(const ProgressFn& progress) {
    if (d_.permit) {  // Onay bekleyisinden sonra: oyun acildiysa geri alma baslamaz, etkin kayitlar korunur.
        if (const auto v = d_.permit(); !v.allowed) {
            Outcome o{OutcomeKind::GateBlocked, 0, "gate blocked before restore"};
            o.gate_reason = v.reason;
            return o;
        }
    }
    // Kullanici kapsami (UAC yok) once; sonra makine journal'indaki etkin yonetici islemleri (tek UAC).
    const Outcome user = (d_.scope.store || !d_.admin) ? restore_scope(d_.scope, progress) : Outcome{OutcomeKind::NothingToDo, 0, {}};
    if (!d_.admin) return user;
    if (user.kind == OutcomeKind::GateBlocked) return user;
    auto ops = d_.admin->active_ops();
    // Yeniden deneme: dogrulanamamis makine kayitlari da gonderilir; aksi halde "yapacak is yok" sanilir.
    for (auto& op : d_.admin->perf_recovery_ops()) {
        const bool dup = std::any_of(ops.begin(), ops.end(), [&](const contracts::SettingOperation& o) { return o.id == op.id && o.target == op.target; });
        if (!dup) ops.push_back(std::move(op));
    }
    if (ops.empty()) return user;
    const auto b = d_.admin->run(ipc::RequestAction::Restore, ops);
    last_admin_ = b;
    admin_ran_ = true;
    if (user.kind == OutcomeKind::RecoveryRequired) {
        // Dogrulanamayan kullanici adimi oncelikli; ama UAC altinda calismis yonetici geri almanin sonucu
        // (yeniden baslatma bekliyor / dogrulanamadi) atilmaz (L3).
        Outcome o = user;
        if (b.elevation == ElevationOutcome::Started) {
            o.pending_reboot = b.any_pending_reboot();
            const std::size_t good = b.count(broker::StepState::Restored) + b.count(broker::StepState::NothingToDo);
            o.detail += b.steps.size() == ops.size() && good == ops.size() ? "; admin restored" : "; admin rollback not verified";
        } else {
            o.detail += "; admin restore not started: " + b.detail;
        }
        return o;
    }
    if (b.elevation != ElevationOutcome::Started) {
        // UAC reddi: yonetici ayarlari etkin kalir (Acik); kullanici kapsami geri alinmis olabilir.
        return {OutcomeKind::Denied, ops.size(), "admin restore not started: " + b.detail};
    }
    Outcome o;
    o.pending_reboot = b.any_pending_reboot();
    const std::size_t good = b.count(broker::StepState::Restored) + b.count(broker::StepState::NothingToDo);
    if (const auto gate = b.count(broker::StepState::GateBlocked)) {
        // UAC sonrasi kapi kapandi: bu adimlar yazilmadi, etkin kalir; oyun kapaninca yeniden denenir.
        o.kind = OutcomeKind::GateBlocked;
        o.changes = gate;
        for (const auto& st : b.steps)
            if (st.state == broker::StepState::GateBlocked) o.gate_reason = st.detail;
        return o;
    }
    if (b.steps.size() == ops.size() && good == ops.size()) {
        o.kind = OutcomeKind::Restored;
    } else {
        o.kind = OutcomeKind::RecoveryRequired;
        o.detail = "admin rollback not verified";
    }
    return o;
}

bool JournalBackend::external_conflict() {
    if (d_.scope.store)
        for (const auto& tx : d_.scope.store->scan().transactions)
            if (recovery::has_external_conflict(tx)) return true;
    return d_.admin && !d_.admin->perf_conflict_ops().empty();
}

Outcome JournalBackend::keep_external() {
    if (d_.scope.store)
        for (auto& tx : d_.scope.store->scan().transactions)
            if (recovery::has_external_conflict(tx)) (void)recovery::keep_external_values(std::move(tx), *d_.scope.store);
    if (!d_.admin) return {OutcomeKind::Restored, 0, {}};
    const auto ops = d_.admin->perf_conflict_ops();
    if (ops.empty()) return {OutcomeKind::Restored, 0, {}};
    const auto b = d_.admin->run(ipc::RequestAction::KeepCurrent, ops);
    if (b.elevation != ElevationOutcome::Started) return {OutcomeKind::Denied, 0, "admin keep not started: " + b.detail};
    return {OutcomeKind::Restored, 0, {}};  // cagiran journal'dan gercek durumu yeniden okur
}

std::size_t JournalBackend::active_changes() {
    std::size_t n = 0;
    if (d_.scope.store)
        for (const auto& tx : d_.scope.store->scan().transactions)
            if (tx.state == TxState::Active)
                for (const auto& st : tx.steps)
                    if (st.state != recovery::StepState::KeptExternal) ++n;  // kullanicinin korudugu (artik bizim olmayan) adim etkin sayilmaz
    if (d_.admin) n += d_.admin->active_ops().size();
    return n;
}

namespace {
// Acilis denetiminde atlanan makine adimlari: oturum/surec sonunda kendiliginden donen veya ayar olmayan islemler.
// needs_reboot (HAGS) ATLANMAZ: tercih degeri dogrudan okunur, baska program degistirdiyse gercek degisikliktir.
bool startup_check_ignored(const contracts::SettingOperation& op) noexcept {
    switch (op.id) {
        case contracts::OperationId::ServiceSessionStop:
        case contracts::OperationId::UpdatePauseSession:
        case contracts::OperationId::BackgroundProcessLower:
        case contracts::OperationId::RestorePointCreate:
        case contracts::OperationId::MaintenanceJob: return true;
        default: return false;
    }
}
}  // namespace

std::vector<recovery::FailedRestoreItem> JournalBackend::check_live() {
    std::vector<recovery::FailedRestoreItem> out;
    if (!d_.scope.store || !d_.scope.engine) return out;
    for (const auto& tx : d_.scope.store->scan().transactions) {
        if (tx.state != TxState::Active) continue;
        for (const auto& s : tx.steps) {
            if (s.state != recovery::StepState::Applied && s.state != recovery::StepState::Verified) continue;
            std::string observed;
            const auto live = d_.scope.engine->check_step(s, &observed);
            if (live != recovery::LiveState::Changed) continue;  // okunamayan adim kapali SAYILMAZ (bilinmeyen != kapali); kayit korunur
            recovery::FailedRestoreItem i;
            i.scope = d_.scope.name;
            i.tx_id = tx.id;
            i.op = s.op.id;
            i.target = s.op.target;
            i.expected = s.op.new_value;
            i.observed = observed;
            i.written = s.last_written.empty() ? s.op.new_value : s.last_written;
            i.reason = recovery::RestoreFailReason::External;
            out.push_back(std::move(i));
        }
    }
    // R-E2: MAKINE kapsamli (yonetici) etkin ayarlar da gercek degerleriyle denetlenir (salt okuma; kullanici surecinde
    // kayitli okuyucu adaptorlerle). Onceden yalniz kullanici kaydi okunuyor, yonetici ayarlari baska program/surucu
    // guncellemesiyle geri cevrilse bile "Acik" gorunuyordu. Okunamayan ayar degismis SAYILMAZ (bilinmeyen != degisti).
    if (d_.admin && d_.scope.engine) {
        for (const auto& op : d_.admin->active_ops()) {
            if (startup_check_ignored(op)) continue;
            const auto cur = d_.scope.engine->read_live(op);
            if (!cur.ok()) continue;
            const auto& v = cur.value();
            if (v && *v == op.new_value) continue;
            recovery::FailedRestoreItem i;
            i.scope = "machine";
            i.op = op.id;
            i.target = op.target;
            i.expected = op.new_value;
            i.observed = v ? *v : std::string();
            i.written = op.new_value;
            i.reason = recovery::RestoreFailReason::External;
            out.push_back(std::move(i));
        }
    }
    return out;
}

std::vector<DriftItem> JournalBackend::drift() {
    std::vector<ActiveStep> steps;
    if (!d_.scope.store || !d_.scope.engine) return {};
    for (const auto& tx : d_.scope.store->scan().transactions) {
        if (tx.state != TxState::Active) continue;
        for (const auto& s : tx.steps) {
            if (s.state != recovery::StepState::Applied && s.state != recovery::StepState::Verified) continue;
            steps.push_back({s.op, drift_group_key(s.op), s.last_written});
        }
    }
    const auto* eng = d_.scope.engine;
    return find_drift(steps, [eng](const contracts::SettingOperation& op) { return eng->read_live(op); });
}

Outcome JournalBackend::reapply_drift(const std::vector<DriftItem>& items, const ProgressFn& progress) {
    if (items.empty()) return {OutcomeKind::NothingToDo, 0, "no drift"};
    if (d_.writes_blocked && d_.writes_blocked()) return {OutcomeKind::Denied, 0, "journal written by a newer version; writes are blocked"};
    if (!d_.scope.store || !d_.scope.engine) return {OutcomeKind::FailedRestored, 0, "journal unavailable"};
    const auto txs = d_.scope.store->scan();  // yalniz okunur: kayit YAZILMAZ (anlik goruntu ve durum oldugu gibi kalir)
    std::size_t ok = 0, failed = 0, i = 0;
    std::string gate_reason;
    for (const auto& it : items) {
        if (d_.permit) {  // her yazmadan once: oyun acildiysa kalan adimlar yazilmaz
            if (const auto v = d_.permit(); !v.allowed) {
                gate_reason = v.reason;
                break;
            }
        }
        const recovery::JournalStep* step = nullptr;
        for (const auto& tx : txs.transactions) {
            if (tx.state != TxState::Active) continue;
            for (const auto& s : tx.steps)
                if ((s.state == recovery::StepState::Applied || s.state == recovery::StepState::Verified) && s.op.id == it.op.id &&
                    s.op.target == it.op.target)
                    step = &s;
        }
        if (progress) progress(recovery::Progress{recovery::Phase::Applying, i, items.size()});
        ++i;
        if (!step || !d_.scope.engine->reapply_step(*step).ok()) {
            ++failed;
            continue;
        }
        ++ok;
    }
    if (progress) progress(recovery::Progress{recovery::Phase::Done, items.size(), items.size()});
    if (!gate_reason.empty() && ok == 0 && failed == 0) {
        Outcome o{OutcomeKind::GateBlocked, 0, "gate blocked before reapply"};
        o.gate_reason = gate_reason;
        return o;
    }
    if (failed > 0 || !gate_reason.empty()) {
        Outcome o{OutcomeKind::FailedRestored, ok, "reapply incomplete: ok=" + std::to_string(ok) + " failed=" + std::to_string(failed)};
        o.gate_blocked = !gate_reason.empty();
        o.gate_reason = gate_reason;
        return o;
    }
    return {OutcomeKind::Active, ok, {}};
}

void JournalBackend::retire_off(const std::vector<recovery::FailedRestoreItem>& items) {
    if (!d_.scope.store || items.empty()) return;
    for (auto& tx : d_.scope.store->scan().transactions) {
        if (tx.state != TxState::Active) continue;
        bool changed = false;
        for (auto& s : tx.steps) {
            if (s.state != recovery::StepState::Applied && s.state != recovery::StepState::Verified) continue;
            const bool off = std::any_of(items.begin(), items.end(), [&](const recovery::FailedRestoreItem& i) {
                return i.tx_id == tx.id && i.op == s.op.id && i.target == s.op.target;
            });
            if (!off) continue;
            s.state = recovery::StepState::KeptExternal;  // ayar yazilmaz; kullanicinin mevcut degeri kalir
            s.error = "no longer applied at startup; user value kept";
            changed = true;
        }
        if (!changed) continue;
        const bool live = std::any_of(tx.steps.begin(), tx.steps.end(), [](const recovery::JournalStep& s) {
            return s.state == recovery::StepState::Applied || s.state == recovery::StepState::Verified;
        });
        if (!live) tx.state = TxState::Restored;  // etkin adim kalmadi: kayit kapanir (KeptExternal => "kismen")
        (void)d_.scope.store->persist(tx);        // kalici yazilamazsa kayit Active kalir; sonraki acilis yeniden uzlastirir
    }
}

bool JournalBackend::recovery_needed() {
    if (legacy_unmigrated()) return true;  // R-F1: aktarilamayan eski kayitlar: durum bilinmiyor, temiz Kapali yok
    if (d_.admin && d_.admin->recovery_needed()) return true;
    if (!d_.scope.store) return false;
    const auto l = d_.scope.store->scan();
    if (!l.corrupt.empty()) return true;
    for (const auto& tx : l.transactions)
        if (tx.state == TxState::RecoveryRequired) return true;
    return false;
}

}  // namespace ef::core::perf
