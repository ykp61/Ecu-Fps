#include <map>

#include "core/adapters/windows/background_process.h"
#include "core/recovery/conflicts.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
// SAHTE: gercek surece dokunmaz. Birim testi gercek Windows testi degildir.
class FakeBgApi final : public BackgroundProcessApi {
public:
    Status verify(const ProcessId& id) override {
        if (!alive || id.pid != pid || id.creation != creation) return Status::fail(ErrorCode::MissingValue, "exited or reused");
        return ok_status();
    }
    Result<std::string> image_path(const ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<std::string>(v.error());
        return path;
    }
    Result<PriorityClass> get_priority(const ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<PriorityClass>(v.error());
        return prio;
    }
    Status set_priority(const ProcessId& id, PriorityClass p) override {
        if (auto v = verify(id); !v.ok()) return v;
        ++prio_writes;
        prio = p;
        return ok_status();
    }
    Result<ThrottleState> get_throttling(const ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<ThrottleState>(v.error());
        if (!throttle_query) return Result<ThrottleState>::fail(ErrorCode::Unsupported, "query");
        return throttle;
    }
    Status set_throttling(const ProcessId& id, ThrottleState s) override {
        if (auto v = verify(id); !v.ok()) return v;
        if (fail_throttle_set) return Status::fail(ErrorCode::Denied, "set");
        throttle = s;
        return ok_status();
    }
    bool alive = true, throttle_query = true, fail_throttle_set = false;
    std::uint32_t pid = 4242;
    std::uint64_t creation = 133000000000000000ull;
    std::string path = "C:\\Users\\x\\AppData\\Local\\Discord\\Discord.exe";
    PriorityClass prio = PriorityClass::Normal;
    ThrottleState throttle{};
    int prio_writes = 0;
};

struct Rig {
    FakeBgApi api;
    BackgroundProcessAdapter adapter{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() { reg.add(OperationId::BackgroundProcessLower, &adapter); }
    std::string target() const { return format_bg_process_target({{api.pid, api.creation}, bg_path_hash(api.path)}); }
    JournalTx tx(const std::string& target) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcdef";
        JournalStep s;
        s.op = SettingOperation{OperationId::BackgroundProcessLower, OperationScope::Process, target, "low", false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(bgp_target_codec) {
    const auto h = bg_path_hash("C:/Foo/Bar.exe");
    EF_CHECK(h.size() == 16 && h == bg_path_hash("c:\\foo\\bar.exe"));  // buyuk/kucuk harf + egik cizgi normalize
    BgProcessTarget t{{7, 99}, h};
    auto p = parse_bg_process_target(format_bg_process_target(t));
    EF_CHECK(p.ok() && p.value().id.pid == 7 && p.value().id.creation == 99 && p.value().path_hash == h);
    EF_CHECK(!parse_bg_process_target("bgproc:0:5:" + h).ok());
    EF_CHECK(!parse_bg_process_target("bgproc:7:5:short").ok());
    EF_CHECK(!parse_bg_process_target("bgproc:7:5:ZZZZZZZZZZZZZZZZ").ok());
    EF_CHECK(!parse_bg_process_target("proc:7:5:priority").ok());
}

EF_TEST(bgp_apply_readback_and_undo) {
    Rig r;
    auto active = r.engine.apply(r.tx(r.target()));
    EF_CHECK(active.ok() && active.value().state == TxState::Active);
    EF_CHECK(r.api.prio == PriorityClass::Idle);
    EF_CHECK(r.api.throttle.control & kThrottleExecutionSpeed);
    EF_CHECK(r.api.throttle.state & kThrottleExecutionSpeed);
    // Yazma oncesi kalici baslangic kaydi ve onceki deger journal'da.
    auto loaded = r.store.load(active.value().id);
    EF_CHECK(loaded.ok() && loaded.value().steps[0].snapshot.old_value == "prio=normal;thr=0:0");
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.prio == PriorityClass::Normal && r.api.throttle == ThrottleState{});
    EF_CHECK(rb.value().steps[0].state == StepState::RolledBack);
}

EF_TEST(bgp_restores_previous_below_normal_and_other_throttle_bits) {
    Rig r;
    r.api.prio = PriorityClass::BelowNormal;
    r.api.throttle = {0x5, 0x4};  // baska bit(ler) kullanicinin; EXECUTION_SPEED kapali degil, kontrol disi
    auto active = r.engine.apply(r.tx(r.target()));
    EF_CHECK(active.ok());
    EF_CHECK(r.api.throttle.state & 0x4);  // baska bit korunur
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.prio == PriorityClass::BelowNormal && r.api.throttle == (ThrottleState{0x5, 0x4}));
}

EF_TEST(bgp_refuses_user_chosen_and_unverifiable_states) {
    for (auto p : {PriorityClass::High, PriorityClass::Realtime, PriorityClass::Idle}) {
        Rig r;
        r.api.prio = p;
        auto res = r.engine.apply(r.tx(r.target()));
        EF_CHECK(!res.ok() || res.value().state != TxState::Active);
        EF_CHECK(r.api.prio == p && r.api.prio_writes == 0);
    }
    Rig eco;
    eco.api.throttle = {kThrottleExecutionSpeed, kThrottleExecutionSpeed};  // zaten EcoQoS
    EF_CHECK(!eco.engine.apply(eco.tx(eco.target())).ok() || eco.api.prio_writes == 0);
    EF_CHECK(eco.api.prio == PriorityClass::Normal);
    Rig nq;
    nq.api.throttle_query = false;  // geri alinabilirlik kanitlanamaz: yazma yok
    auto res = nq.engine.apply(nq.tx(nq.target()));
    EF_CHECK(!res.ok() || res.value().state != TxState::Active);
    EF_CHECK(nq.api.prio == PriorityClass::Normal && nq.api.prio_writes == 0);
}

EF_TEST(bgp_half_write_is_undone_immediately) {
    Rig r;
    r.api.fail_throttle_set = true;
    auto res = r.engine.apply(r.tx(r.target()));
    EF_CHECK(!res.ok() || res.value().state != TxState::Active);
    EF_CHECK(r.api.prio == PriorityClass::Normal);  // oncelik de eski haline
}

EF_TEST(bgp_wrong_instance_or_path_never_touched) {
    Rig r;
    auto t = r.target();
    r.api.creation += 1;  // PID yeniden kullanildi: baska ornek
    auto res = r.engine.apply(r.tx(t));
    EF_CHECK(!res.ok() || res.value().state != TxState::Active);
    EF_CHECK(r.api.prio == PriorityClass::Normal && r.api.prio_writes == 0);
    Rig q;
    auto t2 = q.target();
    q.api.path = "C:\\Evil\\Discord.exe";  // ayni PID+zaman, farkli goruntu
    auto res2 = q.engine.apply(q.tx(t2));
    EF_CHECK(!res2.ok() || res2.value().state != TxState::Active);
    EF_CHECK(q.api.prio_writes == 0);
}

EF_TEST(bgp_process_gone_means_nothing_to_undo) {
    Rig r;
    auto active = r.engine.apply(r.tx(r.target()));
    EF_CHECK(active.ok());
    r.api.alive = false;
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.prio_writes == 1);  // geri alma bitmis surece yazmadi
}

EF_TEST(bgp_pid_reused_after_apply_not_touched_on_undo) {
    Rig r;
    auto active = r.engine.apply(r.tx(r.target()));
    EF_CHECK(active.ok());
    r.api.creation += 7;  // ayni PID, yeni surec; Idle'a alinmis degil, yazma olmamali
    r.api.prio = PriorityClass::Normal;
    const int before = r.api.prio_writes;
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.prio_writes == before && r.api.prio == PriorityClass::Normal);
}

EF_TEST(bgp_user_changed_meanwhile_is_kept_external) {
    Rig r;
    auto active = r.engine.apply(r.tx(r.target()));
    EF_CHECK(active.ok());
    r.api.prio = PriorityClass::AboveNormal;  // kullanici Gorev Yoneticisi'nden degistirdi
    const int before = r.api.prio_writes;
    auto rb = r.engine.rollback(active.value());
    // Dis degisiklik ezilmez: adim catisma olarak bekler, kullanici "mevcut degeri koru" derse KeptExternal olur.
    EF_CHECK(rb.ok() && rb.value().state == TxState::RecoveryRequired);
    EF_CHECK(has_external_conflict(rb.value()));
    EF_CHECK(r.api.prio == PriorityClass::AboveNormal && r.api.prio_writes == before);
    auto kept = keep_external_values(rb.value(), r.store);
    EF_CHECK(kept.ok() && kept.value().state == TxState::Restored);
    EF_CHECK(kept.value().steps[0].state == StepState::KeptExternal);
    EF_CHECK(restore_outcome(kept.value()) == RestoreOutcome::PartialKeptUserChanges);
    EF_CHECK(r.api.prio == PriorityClass::AboveNormal && r.api.prio_writes == before);  // hic yazilmadi
}

EF_TEST(bgp_restart_replays_journal_and_restores) {
    MemoryJournalFs fs;
    FakeBgApi api;
    std::string id;
    {
        BackgroundProcessAdapter ad(api);
        JournalStore store(fs, "m");
        AdapterRegistry reg;
        reg.add(OperationId::BackgroundProcessLower, &ad);
        TransactionEngine eng(store, reg);
        JournalTx t;
        t.id = "fedcba9876543210fedcba9876543210";
        JournalStep s;
        s.op = SettingOperation{OperationId::BackgroundProcessLower, OperationScope::Process,
                                format_bg_process_target({{api.pid, api.creation}, bg_path_hash(api.path)}), "low", false, false};
        t.steps.push_back(s);
        auto a = eng.apply(t);
        EF_CHECK(a.ok() && a.value().state == TxState::Active);
        id = a.value().id;
    }  // "uygulama kapandi": bellekteki her sey gitti, yalniz journal ve surec kaldi
    EF_CHECK(api.prio == PriorityClass::Idle);
    BackgroundProcessAdapter ad2(api);
    JournalStore store2(fs, "m");
    AdapterRegistry reg2;
    reg2.add(OperationId::BackgroundProcessLower, &ad2);
    TransactionEngine eng2(store2, reg2);
    auto listing = store2.scan();
    EF_CHECK(listing.transactions.size() == 1 && listing.transactions[0].id == id);
    EF_CHECK(eng2.check_step(listing.transactions[0].steps[0]) == LiveState::Applied);
    auto rb = eng2.rollback(listing.transactions[0]);
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(api.prio == PriorityClass::Normal && api.throttle == ThrottleState{});
    // Yeniden calistirma idempotent
    auto again = eng2.rollback(rb.value());
    EF_CHECK(again.ok() && again.value().state == TxState::Restored);
}
