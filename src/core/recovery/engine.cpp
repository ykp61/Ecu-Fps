#include "core/recovery/engine.h"

namespace ef::core::recovery {

using contracts::ErrorCode;
using contracts::Status;
using contracts::TxState;

std::string_view to_string(Phase p) noexcept {
    switch (p) {
        case Phase::Capturing: return "capturing";
        case Phase::Applying: return "applying";
        case Phase::Verifying: return "verifying";
        case Phase::RollingBack: return "rolling_back";
        case Phase::Done: return "done";
    }
    return "?";
}

Status TransactionEngine::set_state(JournalTx& tx, TxState s) {
    if (tx.state == s) return contracts::ok_status();
    if (!contracts::can_transition(tx.state, s))
        return Status::fail(ErrorCode::InvalidTransition,
                            std::string(contracts::to_string(tx.state)) + "->" + std::string(contracts::to_string(s)));
    const auto prev = tx.state;
    tx.state = s;
    auto st = store_.persist(tx);
    if (!st.ok()) tx.state = prev;
    return st;
}

contracts::Result<JournalTx> TransactionEngine::apply(JournalTx tx, const EngineOptions& opt) {
    using R = contracts::Result<JournalTx>;
    const std::size_t n = tx.steps.size();
    Progress pr{Phase::Capturing, 0, n * 3};  // capture + apply + verify
    auto report = [&] { if (opt.on_progress) opt.on_progress(pr); };
    if (n == 0) return R::fail(ErrorCode::MissingValue, "no steps");

    // 1) Tum adimlarin adaptoru ve anlik goruntusu (henuz hicbir sey yazilmadi)
    for (auto& s : tx.steps) {
        auto* a = adapters_.find(s.op.id);
        if (!a) return R::fail(ErrorCode::Unsupported, "no adapter for operation");
        auto snap = a->capture(s.op);
        if (!snap.ok()) return R(snap.error());
        s.snapshot = snap.value();
        s.state = StepState::Planned;
        ++pr.done;
        report();
    }
    // Onay/UAC bekleyisinden sonra ilk gercek yazimdan once: durum bozulduysa hicbir sey baslamaz (journal da yazilmaz).
    if (guard_) {
        if (const auto d = guard_(); !d.allowed) return R::fail(ErrorCode::GateBlocked, d.reason);
    }
    // 2) Dayanikli baslangic kaydi; basarisizsa hicbir sey uygulanmaz
    tx.state = TxState::Prepared;
    if (auto b = store_.begin(tx); !b.ok()) return R(b.error());
    if (auto s = set_state(tx, TxState::Applying); !s.ok()) return R(s.error());

    // 3) Adim adim: intent kaydi -> uygula -> geri oku
    pr.phase = Phase::Applying;
    std::string failure, gate_reason;
    bool first_step = true;
    std::size_t step_index = 0;
    pr.steps = n;
    for (auto& s : tx.steps) {
        pr.step = ++step_index;
        report();  // T18: adim basinda (done < total); UI "Adim i/n" adimi yazilmadan once gosterir
        if (opt.cancel && opt.cancel->load()) { failure = "cancelled"; break; }
        if (guard_ && !first_step) {  // ilk adim yukarida az once dogrulandi
            if (const auto d = guard_(); !d.allowed) {
                gate_reason = d.reason;
                failure = "gate blocked during transaction: " + d.reason;
                break;
            }
        }
        first_step = false;
        auto* a = adapters_.find(s.op.id);
        s.state = StepState::Intent;
        if (auto p = store_.persist(tx); !p.ok()) { failure = "journal write failed before apply"; s.state = StepState::Planned; break; }
        if (auto ap = a->apply(s.op); !ap.ok()) {
            s.state = StepState::Failed;
            s.error = ap.error().detail;
            s.error_code = std::string(contracts::to_string(ap.error().code));
            failure = "apply failed: " + s.error;
            (void)store_.persist(tx);
            break;
        }
        s.state = StepState::Applied;
        s.last_written = s.op.new_value;
        ++pr.done;
        report();
        auto rb = a->read_back(s.op);
        if (!rb.ok() || !rb.value() || *rb.value() != s.op.new_value) {
            s.state = StepState::Failed;
            s.error = "read-back mismatch";
            s.observed = rb.ok() ? observed_text(rb.value()) : std::string();
            s.error_code = rb.ok() ? std::string() : std::string(contracts::to_string(rb.error().code));
            failure = "verification failed";
            (void)store_.persist(tx);
            break;
        }
        s.state = StepState::Verified;
        if (auto p = store_.persist(tx); !p.ok()) { failure = "journal write failed after apply"; break; }
        ++pr.done;
        report();
    }

    if (!failure.empty()) {
        // Kismi hata/iptal: tamamlanan adimlari ters sirada telafi et.
        // Dahili telafi: kapi kapansa bile yarim islem birakilmaz; onceki degerler dogrulanarak geri yuklenir.
        auto rb = rollback_impl(std::move(tx), opt);
        if (!rb.ok()) return rb;
        auto out = rb.value();
        if (out.state == TxState::Restored && !gate_reason.empty()) return R::fail(ErrorCode::GateBlocked, gate_reason);
        if (out.state == TxState::Restored) return R::fail(ErrorCode::IoError, failure + " (rolled back)");
        return out;  // RecoveryRequired: cagiran kullaniciya gosterir
    }

    pr.phase = Phase::Verifying;
    pr.step = 0;
    report();  // T18: "Son dogrulama" asamasi
    // CORE-02: son durum kaydi yazilamazsa uygulanan degisiklikler sessizce birakilmaz. Her basarisiz
    // kalici gecis geri alma (ya da en az RecoveryRequired) ile kapatilir.
    if (auto s = set_state(tx, TxState::Applied); !s.ok()) return finalize_failure(std::move(tx), opt, s.error());
    if (auto s = set_state(tx, TxState::Verifying); !s.ok()) return finalize_failure(std::move(tx), opt, s.error());
    if (auto s = set_state(tx, TxState::Active); !s.ok()) return finalize_failure(std::move(tx), opt, s.error());
    pr.phase = Phase::Done;
    pr.done = pr.total;
    report();
    return tx;
}

contracts::Result<JournalTx> TransactionEngine::finalize_failure(JournalTx tx, const EngineOptions& opt, const Status& cause) {
    using R = contracts::Result<JournalTx>;
    // 1) Tam geri alma: hedefler okunur, anlik goruntu geri yazilir ve dogrulanir. Journal hala yazilabiliyorsa
    //    islem Restored (temiz) ya da RecoveryRequired (dogrulanamayan adim) olarak kapatilir; "Verifying" kalmaz.
    auto rb = rollback_impl(tx, opt);
    if (rb.ok()) return rb;
    // 2) Journal yazilamiyor: en azindan hedef degerleri geri yukle; sonucu oldugu gibi bildir (basarisiz diye gizleme).
    const bool restored = restore_applied_without_journal(tx);
    return R::fail(cause.error().code, cause.error().detail + (restored ? "; journal unwritable, applied changes restored"
                                                                        : "; journal unwritable, applied changes NOT fully restored"));
}

bool TransactionEngine::restore_applied_without_journal(JournalTx& tx) {
    bool all_ok = true;
    for (auto it = tx.steps.rbegin(); it != tx.steps.rend(); ++it) {
        auto& s = *it;
        if (s.state != StepState::Applied && s.state != StepState::Verified) continue;
        auto* a = adapters_.find(s.op.id);
        if (!a) {
            all_ok = false;
            s.state = StepState::Failed;
            s.error = "adapter missing for fallback restore";
            continue;
        }
        if (a->target_gone(s.op)) {
            s.state = StepState::RolledBack;
            s.error = "target no longer exists";
            continue;
        }
        if (!rollback_step(*a, s)) all_ok = false;
    }
    return all_ok;
}

bool rollback_step(SettingAdapter& a, JournalStep& s) {
    const auto fail = [&s](std::string error, std::string observed, std::string code) {
        s.state = StepState::Failed;
        s.error = std::move(error);
        s.observed = std::move(observed);
        s.error_code = std::move(code);
        return false;
    };
    // Once GERCEK hedefi oku (cokme sonrasi son kayda guvenilmez, docs/04):
    //  - zaten anlik goruntudeyse yazma yok
    //  - bizim yazdigimiz degerdeyse geri yukle
    //  - ikisi de degilse dis degisiklik: ezme, catisma olarak isaretle (REC-04 Evet/Hayir)
    auto cur = a.read_for_restore(s.op, s.snapshot);
    if (!cur.ok()) return fail("cannot read target: " + cur.error().detail, {}, std::string(contracts::to_string(cur.error().code)));
    const auto& v = cur.value();
    if (a.matches_snapshot(s.op, s.snapshot, v)) {
        s.state = StepState::RolledBack;
        s.error.clear();
        s.observed.clear();
        s.error_code.clear();
        return true;
    }
    const bool ours = v && (*v == s.op.new_value || (!s.last_written.empty() && *v == s.last_written));
    if (!ours) return fail(std::string(kExternalChange), observed_text(v), {});  // dis degisiklik: ezme (REC-04)
    auto r = a.restore(s.op, s.snapshot);
    auto now = a.read_for_restore(s.op, s.snapshot);
    if (r.ok() && now.ok() && a.matches_snapshot(s.op, s.snapshot, now.value())) {
        s.state = StepState::RolledBack;
        s.error.clear();
        s.observed.clear();
        s.error_code.clear();
        return true;
    }
    if (!r.ok()) return fail(r.error().detail, now.ok() ? observed_text(now.value()) : std::string(), std::string(contracts::to_string(r.error().code)));
    if (!now.ok()) return fail("restore verification failed", {}, std::string(contracts::to_string(now.error().code)));
    return fail("restore verification failed", observed_text(now.value()), {});
}

LiveState TransactionEngine::check_step(const JournalStep& s, std::string* observed) const {
    auto* a = adapters_.find(s.op.id);
    if (!a) return LiveState::Unreadable;
    auto cur = a->read_back(s.op);
    if (!cur.ok()) return LiveState::Unreadable;
    const auto& v = cur.value();
    if (observed) *observed = observed_text(v);
    const bool ours = v && (*v == s.op.new_value || (!s.last_written.empty() && *v == s.last_written));
    return ours ? LiveState::Applied : LiveState::Changed;
}

contracts::Result<std::optional<std::string>> TransactionEngine::read_live(const contracts::SettingOperation& op) const {
    using R = contracts::Result<std::optional<std::string>>;
    auto* a = adapters_.find(op.id);
    if (!a) return R::fail(ErrorCode::Unsupported, "adapter missing");
    return a->read_back(op);
}

contracts::Status TransactionEngine::reapply_step(const JournalStep& s) const {
    auto* a = adapters_.find(s.op.id);
    if (!a) return contracts::Status::fail(ErrorCode::Unsupported, "adapter missing");
    // Yazilacak deger: adimin yazdigimiz son degeri (yoksa planlanan). Anlik goruntu burada kullanilmaz.
    contracts::SettingOperation op = s.op;
    if (!s.last_written.empty()) op.new_value = s.last_written;
    if (auto ap = a->apply(op); !ap.ok()) return ap;
    auto rb = a->read_back(op);
    if (!rb.ok()) return contracts::Status::fail(rb.error().code, "read-back failed after reapply");
    if (!rb.value() || *rb.value() != op.new_value) return contracts::Status::fail(ErrorCode::IoError, "read-back mismatch after reapply");
    return contracts::ok_status();
}

contracts::Result<JournalTx> TransactionEngine::rollback(JournalTx tx, const EngineOptions& opt) {
    using R = contracts::Result<JournalTx>;
    if (tx.state == TxState::Restored) return tx;  // idempotent
    if (guard_ && !opt.bypass_guard) {
        // Kullanici/otomatik geri alma da bir yazmadir: oyun acildiysa durum degismez, kayit etkin kalir ve sonra denenir.
        if (const auto d = guard_(); !d.allowed) return R::fail(ErrorCode::GateBlocked, d.reason);
    }
    return rollback_impl(std::move(tx), opt);
}

contracts::Result<JournalTx> TransactionEngine::rollback_impl(JournalTx tx, const EngineOptions& opt) {
    using R = contracts::Result<JournalTx>;
    if (tx.state == TxState::Restored) return tx;
    std::size_t todo = 0;
    for (const auto& s : tx.steps)
        if (s.state == StepState::Intent || s.state == StepState::Applied || s.state == StepState::Verified ||
            s.state == StepState::Failed)
            ++todo;
    Progress pr{Phase::RollingBack, 0, todo};
    auto report = [&] { if (opt.on_progress) opt.on_progress(pr); };
    if (auto s = set_state(tx, TxState::RollingBack); !s.ok()) return R(s.error());

    bool all_ok = true;
    for (auto it = tx.steps.rbegin(); it != tx.steps.rend(); ++it) {  // ters bagimlilik sirasi
        auto& s = *it;
        if (s.state == StepState::Planned || s.state == StepState::RolledBack || s.state == StepState::KeptExternal) continue;
        auto* a = adapters_.find(s.op.id);
        if (!a) { all_ok = false; s.error = "adapter missing for rollback"; continue; }
        if (a->target_gone(s.op)) {  // ornek: surec kapandi -> geri alinacak bir sey yok
            s.state = StepState::RolledBack;
            s.error = "target no longer exists";
            (void)store_.persist(tx);
            ++pr.done;
            report();
            continue;
        }
        if (!rollback_step(*a, s)) all_ok = false;
        (void)store_.persist(tx);
        ++pr.done;
        report();
    }
    if (auto s = set_state(tx, all_ok ? TxState::Restored : TxState::RecoveryRequired); !s.ok()) return R(s.error());
    return tx;
}

// ---- FakeAdapter ----
contracts::Result<Snapshot> FakeAdapter::capture(const contracts::SettingOperation& op) {
    Snapshot s;
    const auto it = values.find(op.target);
    s.existed = it != values.end();
    if (s.existed) s.old_value = it->second;
    s.raw_type = "string";
    return s;
}
Status FakeAdapter::apply(const contracts::SettingOperation& op) {
    ++applies;
    if (op.target == fail_apply_target) return Status::fail(ErrorCode::Denied, "injected apply failure");
    values[op.target] = op.new_value;
    return contracts::ok_status();
}
contracts::Result<std::optional<std::string>> FakeAdapter::read_back(const contracts::SettingOperation& op) {
    const auto it = values.find(op.target);
    if (it == values.end()) return std::optional<std::string>{};
    if (!corrupt_readback_target.empty() && op.target == corrupt_readback_target) {
        corrupt_readback_target.clear();  // gecici okuma hatasi
        return std::optional<std::string>{it->second + "-x"};
    }
    return std::optional<std::string>{it->second};
}
Status FakeAdapter::restore(const contracts::SettingOperation& op, const Snapshot& snap) {
    ++restores;
    if (op.target == fail_restore_target) return Status::fail(ErrorCode::IoError, "injected restore failure");
    if (snap.existed) values[op.target] = snap.old_value;
    else values.erase(op.target);
    return contracts::ok_status();
}

}  // namespace ef::core::recovery
