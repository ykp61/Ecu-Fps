#include <cstdio>
#include <map>

#include "core/adapters/windows/display_process.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using namespace ef::core::recovery;
using namespace ef::contracts;
using ef::core::Duration;
using ef::core::MonoTime;

namespace {
class FakeDisplay final : public DisplayApi {
public:
    Result<DisplayMode> current(const DisplayRef& r) override { seen.push_back(r.gdi); return cur; }
    Result<std::vector<DisplayMode>> modes(const DisplayRef& r) override { seen.push_back(r.gdi); return list; }
    Status test(const DisplayRef& r, const DisplayMode& m) override {
        seen.push_back(r.gdi);
        return m.hz == broken_hz ? Status::fail(ErrorCode::Unsupported, "driver") : ok_status();
    }
    Status set(const DisplayRef& r, const DisplayMode& m) override { seen.push_back(r.gdi); cur = m; ++sets; return ok_status(); }
    std::vector<std::string> seen;  // her cagrinin hedef GDI adi
    DisplayMode cur{2560, 1440, 60};
    std::vector<DisplayMode> list{{2560, 1440, 60}, {2560, 1440, 144}, {2560, 1440, 240}, {1920, 1080, 360}};
    std::uint32_t broken_hz = 0;
    int sets = 0;
};

class FakeProcesses final : public ProcessApi {
public:
    Status verify(const ProcessId& id) override {
        if (!alive || id.creation != creation) return Status::fail(ErrorCode::MissingValue, "exited or reused");
        return ok_status();
    }
    Result<PriorityClass> get_priority(const ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<PriorityClass>(v.error());
        return prio;
    }
    Status set_priority(const ProcessId& id, PriorityClass p) override {
        if (auto v = verify(id); !v.ok()) return v;
        prio = p;
        return ok_status();
    }
    Result<ThrottleState> get_throttling(const ProcessId&) override { return throttle; }
    Status set_throttling(const ProcessId& id, ThrottleState s) override {
        if (auto v = verify(id); !v.ok()) return v;
        throttle = s;
        return ok_status();
    }
    bool alive = true;
    std::uint64_t creation = 133000000000000000ull;
    PriorityClass prio = PriorityClass::Normal;
    ThrottleState throttle{};
};

struct Rig {
    FakeDisplay disp;
    FakeProcesses procs;
    RefreshRateAdapter refresh{disp};
    ProcessAdapter process{procs};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    Rig() {
        reg.add(OperationId::RefreshRateSet, &refresh);
        reg.add(OperationId::ProcessPriority, &process);
        reg.add(OperationId::ProcessPowerThrottling, &process);
    }
    JournalTx tx(OperationId id, std::string target, std::string v) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = SettingOperation{id, OperationScope::Process, std::move(target), std::move(v), false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(refresh_highest_same_resolution_only) {
    FakeDisplay d;
    EF_CHECK(highest_refresh(d.list, d.cur) == 240u);  // 360 Hz farkli cozunurlukte: secilmez
}

EF_TEST(invalid_display_mode_not_applied) {
    Rig r;
    auto bad = r.engine.apply(r.tx(OperationId::RefreshRateSet, "display:0:refresh", "165"));  // listede yok
    EF_CHECK(!bad.ok() && r.disp.sets == 0);
    r.disp.broken_hz = 240;  // listede var ama surucu testi reddediyor
    auto rej = r.engine.apply(r.tx(OperationId::RefreshRateSet, "display:0:refresh", "240"));
    EF_CHECK(!rej.ok() && r.disp.cur.hz == 60);
    r.disp.broken_hz = 0;
    auto ok = r.engine.apply(r.tx(OperationId::RefreshRateSet, "display:0:refresh", "240"));
    EF_CHECK(ok.ok() && r.disp.cur.hz == 240);
    auto rb = r.engine.rollback(ok.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.disp.cur.hz == 60);
}

// ---- Uyumluluk B6 (bosluk 10): hedef ekran GDI aygit adiyla; geri okuma ayni aygit ----
EF_TEST(display_target_carries_gdi_device_name) {
    EF_CHECK(gdi_display_token("\\\\.\\DISPLAY2") == "DISPLAY2");
    EF_CHECK(gdi_display_token("\\\\.\\DISPLAY12") == "DISPLAY12");
    EF_CHECK(gdi_display_token("DISPLAY2").empty());
    EF_CHECK(gdi_display_token("\\\\.\\DISPLAY").empty());
    EF_CHECK(gdi_display_token("\\\\.\\DISPLAY1:x").empty());
    EF_CHECK(gdi_display_token("\\\\.\\DISPLAY1234").empty());
    EF_CHECK(format_display_target(1, "DISPLAY2") == "display:1:refresh:DISPLAY2");
    EF_CHECK(format_display_target(1, "DISPLAY").empty());
    EF_CHECK(format_display_target(1, "").empty());
    auto legacy = parse_display_target("display:3:refresh");
    EF_CHECK(legacy.ok() && legacy.value().index == 3 && legacy.value().gdi.empty());
    auto dev = parse_display_target("display:1:refresh:DISPLAY2");
    EF_CHECK(dev.ok() && dev.value().index == 1 && dev.value().gdi == "\\\\.\\DISPLAY2");
    for (const char* bad : {"display:1:refresh:", "display:1:refresh:DISPLAYx", "display:1:refresh:\\\\.\\DISPLAY2",
                            "display:1:refreshDISPLAY2", "display::refresh", "screen:1:refresh"})
        EF_CHECK(!parse_display_target(bad).ok());
}

namespace {
// Iki ekran (GDI adiyla); bilinmeyen ad MissingValue: sira numarasina dusulmez.
class TwoDisplays final : public DisplayApi {
public:
    Result<DisplayMode> current(const DisplayRef& r) override {
        auto* m = find(r);
        if (!m) return Result<DisplayMode>::fail(ErrorCode::MissingValue, "display not found");
        return *m;
    }
    Result<std::vector<DisplayMode>> modes(const DisplayRef& r) override {
        if (!find(r)) return Result<std::vector<DisplayMode>>::fail(ErrorCode::MissingValue, "display not found");
        return std::vector<DisplayMode>{{1920, 1080, 60}, {1920, 1080, 144}};
    }
    Status test(const DisplayRef& r, const DisplayMode&) override {
        return find(r) ? ok_status() : Status::fail(ErrorCode::MissingValue, "display not found");
    }
    Status set(const DisplayRef& r, const DisplayMode& m) override {
        auto* x = find(r);
        if (!x) return Status::fail(ErrorCode::MissingValue, "display not found");
        *x = m;
        return ok_status();
    }
    DisplayMode* find(const DisplayRef& r) {
        auto it = by_name.find(r.gdi);
        return it == by_name.end() ? nullptr : &it->second;
    }
    std::map<std::string, DisplayMode> by_name{{"\\\\.\\DISPLAY1", {1920, 1080, 60}}, {"\\\\.\\DISPLAY2", {1920, 1080, 60}}};
};
}  // namespace

EF_TEST(refresh_fix_changes_only_the_named_display_and_rolls_back) {
    TwoDisplays d;
    RefreshRateAdapter a{d};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    reg.add(OperationId::RefreshRateSet, &a);
    TransactionEngine engine{store, reg};
    JournalTx t;
    t.id = "0123456789abcdef0123456789abcdef";
    JournalStep s;
    // Sira 0 ama aygit DISPLAY2: sira numarasina gore degil, ada gore yazilir.
    s.op = SettingOperation{OperationId::RefreshRateSet, OperationScope::CurrentUser, format_display_target(0, "DISPLAY2"), "144", false, false};
    t.steps.push_back(s);
    auto r = engine.apply(t);
    EF_CHECK(r.ok() &&r.value().state == TxState::Active);
    EF_CHECK(d.by_name["\\\\.\\DISPLAY2"].hz == 144 && d.by_name["\\\\.\\DISPLAY1"].hz == 60);
    if (!r.ok()) return;
    auto rb = engine.rollback(r.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored && d.by_name["\\\\.\\DISPLAY2"].hz == 60);

    // Aygit artik yok (cikarildi): baska ekran degismez, islem basarisiz.
    JournalTx t2 = t;
    t2.id = "0123456789abcdef0123456789abcde0";
    t2.steps[0].op.target = format_display_target(0, "DISPLAY7");
    auto gone = engine.apply(t2);
    EF_CHECK(!gone.ok() || gone.value().state != TxState::Active);
    EF_CHECK(d.by_name["\\\\.\\DISPLAY1"].hz == 60 && d.by_name["\\\\.\\DISPLAY2"].hz == 60);
}

EF_TEST(refresh_adapter_passes_gdi_name_on_every_call) {
    Rig r;
    auto ok = r.engine.apply(r.tx(OperationId::RefreshRateSet, "display:0:refresh:DISPLAY3", "240"));
    EF_CHECK(ok.ok() && r.disp.cur.hz == 240 && !r.disp.seen.empty());
    for (const auto& g : r.disp.seen) EF_CHECK(g == "\\\\.\\DISPLAY3");  // capture, modes, test, set, read-back
}

EF_TEST(unconfirmed_display_mode_reverts_after_timeout) {
    DisplayConfirmation c(Duration::from_ms(15000));
    const MonoTime t0{1'000'000'000};
    c.start(t0);
    EF_CHECK(!c.should_revert(t0 + Duration::from_ms(14999)));
    EF_CHECK(c.remaining_ms(t0 + Duration::from_ms(5000)) == 10000);
    EF_CHECK(c.should_revert(t0 + Duration::from_ms(15000)));
    DisplayConfirmation k;
    k.start(t0);
    k.confirm();
    EF_CHECK(!k.should_revert(t0 + Duration::from_ms(60000)));  // onaylandi: kalir
}

EF_TEST(visual_effects_never_touch_font_smoothing) {
    EF_CHECK(!parse_visual_effect("fontsmoothing").has_value());
    EF_CHECK(!parse_visual_effect("font_smoothing").has_value());
    EF_CHECK(parse_visual_effect("menu_animation").has_value());
}

EF_TEST(process_priority_above_normal_and_restore) {
    Rig r;
    ProcessTarget t{{4242, r.procs.creation}, true};
    auto active = r.engine.apply(r.tx(OperationId::ProcessPriority, format_process_target(t), "above_normal"));
    if (!active.ok()) std::printf("  apply error: %s\n", active.error().detail.c_str());
    EF_CHECK(active.ok() && r.procs.prio == PriorityClass::AboveNormal);
    if (!active.ok()) return;
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.procs.prio == PriorityClass::Normal);
}

EF_TEST(realtime_and_high_never_written) {
    Rig r;
    ProcessTarget t{{4242, r.procs.creation}, true};
    for (const char* p : {"realtime", "high"}) {
        auto res = r.engine.apply(r.tx(OperationId::ProcessPriority, format_process_target(t), p));
        EF_CHECK(!res.ok());
        EF_CHECK(r.procs.prio == PriorityClass::Normal);
    }
}

EF_TEST(pid_reuse_does_not_change_wrong_process) {
    Rig r;
    ProcessTarget stale{{4242, r.procs.creation - 1}, true};  // ayni PID, farkli olusturulma zamani
    auto res = r.engine.apply(r.tx(OperationId::ProcessPriority, format_process_target(stale), "above_normal"));
    EF_CHECK(!res.ok() && res.code() == ErrorCode::MissingValue);
    EF_CHECK(r.procs.prio == PriorityClass::Normal);
}

EF_TEST(exited_process_rollback_is_safe_noop) {
    Rig r;
    ProcessTarget t{{4242, r.procs.creation}, false};
    auto active = r.engine.apply(r.tx(OperationId::ProcessPowerThrottling, format_process_target(t), "off"));
    EF_CHECK(active.ok());
    EF_CHECK(r.procs.throttle == (ThrottleState{kThrottleExecutionSpeed, 0}));
    r.procs.alive = false;  // oyun kapandi
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);  // geri alinacak hedef yok; basarisizlik sayilmaz
}

// ---- Uyumluluk B3 (bosluk 25): test suresince kendi surecimiz EcoQoS disi, sonra onceki durum ----
namespace {
class FakeSelfThrottle final : public SelfThrottleApi {
public:
    std::optional<ThrottleState> get() override {
        if (!readable) return std::nullopt;
        return cur;
    }
    bool set(ThrottleState s) override {
        writes.push_back(s);
        if (fail_set) return false;
        cur = s;
        return true;
    }
    ThrottleState cur{};
    bool readable = true, fail_set = false;
    std::vector<ThrottleState> writes;
};
constexpr std::uint32_t kIgnoreTimer = 0x4;  // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
}  // namespace

EF_TEST(self_throttle_opt_out_is_scoped_and_restores_previous) {
    EF_CHECK(execution_speed_opt_out({}) == (ThrottleState{kThrottleExecutionSpeed, 0}));
    // EcoQoS acik (kontrol + durum biti) ve baska bit: yalniz EXECUTION_SPEED durumu kapanir.
    EF_CHECK(execution_speed_opt_out({kThrottleExecutionSpeed | kIgnoreTimer, kThrottleExecutionSpeed | kIgnoreTimer}) ==
              (ThrottleState{kThrottleExecutionSpeed | kIgnoreTimer, kIgnoreTimer}));

    FakeSelfThrottle api;
    api.cur = {kThrottleExecutionSpeed, kThrottleExecutionSpeed};  // onceden EcoQoS
    {
        ScopedExecutionSpeedOptOut g(api);
        EF_CHECK(g.active());
        EF_CHECK(api.cur == (ThrottleState{kThrottleExecutionSpeed, 0}));  // test sirasinda kisitlama yok
    }
    EF_CHECK(api.cur == (ThrottleState{kThrottleExecutionSpeed, kThrottleExecutionSpeed}));  // aynen geri
    EF_CHECK(api.writes.size() == 2);

    // Varsayilan (Windows yonetir) -> opt-out -> yine varsayilan.
    FakeSelfThrottle def;
    {
        ScopedExecutionSpeedOptOut g(def);
        EF_CHECK(g.active() && def.cur == (ThrottleState{kThrottleExecutionSpeed, 0}));
        EF_CHECK(g.restore());
        EF_CHECK(def.cur == ThrottleState{});
        EF_CHECK(!g.restore());  // ikinci geri yukleme yazmaz
    }
    EF_CHECK(def.writes.size() == 2);

    // Zaten kisitlamasiz: hic yazilmaz.
    FakeSelfThrottle off;
    off.cur = {kThrottleExecutionSpeed, 0};
    {
        ScopedExecutionSpeedOptOut g(off);
        EF_CHECK(!g.active());
    }
    EF_CHECK(off.writes.empty());

    // Yazma reddedildi: geri yazma da denenmez (durum degismedi).
    FakeSelfThrottle denied;
    denied.fail_set = true;
    {
        ScopedExecutionSpeedOptOut g(denied);
        EF_CHECK(!g.active());
    }
    EF_CHECK(denied.writes.size() == 1 && denied.cur == ThrottleState{});

    // Okunamayan onceki durum (Windows 10 sorgusu): sistem varsayilani sayilir ve ona donulur.
    FakeSelfThrottle unreadable;
    unreadable.readable = false;
    {
        ScopedExecutionSpeedOptOut g(unreadable);
        EF_CHECK(g.active());
    }
    EF_CHECK(unreadable.cur == ThrottleState{});
}
