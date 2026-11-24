#include <cstdio>
#include <deque>

#include "core/perf/background_plan.h"
#include "core/perf/controller.h"
#include "core/perf/journal_backend.h"
#include "core/perf/power_plan.h"
#include "core/recovery/journal.h"
#include "ef_test.h"

using namespace ef::core::perf;
namespace rec = ef::core::recovery;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
namespace cw = ef::core::adapters::windows;

namespace {

PlanItem item(const char* target, bool quality = false, bool admin = false) {
    PlanItem i;
    i.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, target, "1", false, quality};
    i.title_key = "plan.power_mode";
    i.needs_admin = admin;
    return i;
}

struct FakeBackend final : PerfBackend {
    Plan next_plan;
    bool plan_fails = false;
    OutcomeKind apply_result = OutcomeKind::Active;
    OutcomeKind restore_result = OutcomeKind::Restored;
    std::size_t active = 0;
    bool recovery = false;
    int applies = 0, restores = 0;
    Plan last_applied;
    ef::contracts::Result<Plan> plan() override {
        if (plan_fails) return ef::contracts::Result<Plan>::fail(ef::contracts::ErrorCode::IoError);
        return next_plan;
    }
    Outcome apply(const Plan& p, const ProgressFn& progress, const std::atomic<bool>&) override {
        ++applies;
        last_applied = p;
        for (std::size_t i = 0; i <= p.items.size(); ++i) progress(rec::Progress{rec::Phase::Applying, i, p.items.size()});
        return {apply_result, p.items.size(), {}};
    }
    Outcome restore(const ProgressFn&) override {
        ++restores;
        return {restore_result, 0, {}};
    }
    std::size_t active_changes() override { return active; }
    bool recovery_needed() override { return recovery; }
};

// Arka plan isleri kuyrukta bekler; test adim adim calistirir (gercek asenkronluk benzetimi).
struct Loop {
    std::deque<std::function<void()>> q;
    Executor exec() {
        return [this](std::function<void()> f) { q.push_back(std::move(f)); };
    }
    Poster post() {
        return [this](std::function<void()> f) { q.push_back(std::move(f)); };
    }
    void drain() {
        while (!q.empty()) {
            auto f = std::move(q.front());
            q.pop_front();
            f();
        }
    }
};

}  // namespace

EF_TEST(perf_on_only_after_verified_apply) {
    FakeBackend b;
    b.next_plan.items = {item("a"), item("b")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    EF_CHECK(c.phase() == Phase::Off && !c.switch_on());
    c.toggle();
    EF_CHECK(c.phase() == Phase::Planning && !c.switch_on());
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming && !c.switch_on() && c.plan().items.size() == 2);
    EF_CHECK(b.applies == 0);  // onay olmadan uygulanmadi
    c.answer(true);
    EF_CHECK(c.phase() == Phase::Applying && !c.switch_on());  // is bitmeden Acik yok
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.switch_on() && c.active_changes() == 2);
    EF_CHECK(c.progress().done == 2 && c.progress().total == 2 && c.progress().percent() == 100);
}

EF_TEST(perf_no_answer_changes_nothing) {
    FakeBackend b;
    b.next_plan.items = {item("a")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.toggle();
    loop.drain();
    c.answer(false);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && b.applies == 0 && c.plan().empty());
}

EF_TEST(perf_zero_work_and_denied_and_failed) {
    FakeBackend b;  // bos plan
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && c.notice() == Notice::NoChanges && b.applies == 0);

    b.next_plan.items = {item("a", false, true)};
    b.apply_result = OutcomeKind::Denied;
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && c.notice() == Notice::Denied && !c.switch_on());

    b.apply_result = OutcomeKind::FailedRestored;
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && c.notice() == Notice::FailedRestored);

    b.apply_result = OutcomeKind::RecoveryRequired;
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::RecoveryNeeded && !c.switch_on());

    b.plan_fails = true;
    PerfController d(b, loop.exec(), loop.post());
    d.init();
    d.toggle();
    loop.drain();
    EF_CHECK(d.phase() == Phase::Off && d.notice() == Notice::PlanFailed);
}

EF_TEST(perf_off_requires_verified_restore) {
    FakeBackend b;
    b.active = 3;  // onceki oturumdan Acik kalan islem
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    EF_CHECK(c.phase() == Phase::On && c.active_changes() == 3);
    c.toggle();
    EF_CHECK(c.phase() == Phase::ConfirmRestore && c.switch_on());
    c.answer(false);
    EF_CHECK(c.phase() == Phase::On && b.restores == 0);
    b.restore_result = OutcomeKind::RecoveryRequired;
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == Phase::Restoring && c.switch_on());  // dogrulanmadan Kapali yok
    loop.drain();
    EF_CHECK(c.phase() == Phase::RecoveryNeeded && c.notice() == Notice::RestoreFailed && !c.switch_on());
    b.restore_result = OutcomeKind::Restored;
    c.toggle();  // kurtarma: tekrar dene onayi
    EF_CHECK(c.phase() == Phase::ConfirmRestore);
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && c.active_changes() == 0 && b.restores == 2);
}

EF_TEST(perf_toggle_ignored_while_busy) {
    FakeBackend b;
    b.next_plan.items = {item("a")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.toggle();
    c.toggle();  // Planning sirasinda ikinci basis yok sayilir
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming);
    c.toggle();  // onay acikken anahtar yok sayilir
    EF_CHECK(c.phase() == Phase::Confirming && b.applies == 0);
}

EF_TEST(perf_journal_backend_real_engine_roundtrip) {
    rec::MemoryJournalFs fs;
    rec::JournalStore store(fs, "machine-1");
    rec::FakeAdapter fake;
    fake.values["t1"] = "0";
    fake.values["t2"] = "0";
    rec::AdapterRegistry reg;
    reg.add(OperationId::VisualEffectsSet, &fake);
    rec::TransactionEngine eng(store, reg);
    int ids = 0;
    Plan plan;
    plan.items = {item("t1"), item("t2")};
    JournalBackendDeps deps;
    deps.scope = {"user", &store, &eng};
    deps.build_plan = [&] { return ef::contracts::Result<Plan>(plan); };
    deps.new_tx_id = [&] {
        char b[33];
        std::snprintf(b, sizeof b, "%032d", ++ids);
        return std::string(b);
    };
    deps.now_unix_ms = [&] { return std::int64_t{1000} * ids; };
    JournalBackend be(deps);
    EF_CHECK(be.active_changes() == 0 && !be.recovery_needed());
    std::size_t last_done = 0, last_total = 0;
    std::atomic<bool> cancel{false};
    const auto out = be.apply(be.plan().value(), [&](const rec::Progress& p) { last_done = p.done; last_total = p.total; }, cancel);
    EF_CHECK(out.kind == OutcomeKind::Active && out.changes == 2);
    EF_CHECK(fake.values["t1"] == "1" && fake.values["t2"] == "1");
    EF_CHECK(last_total > 0 && last_done == last_total);  // gercek adim sayisi
    EF_CHECK(be.active_changes() == 2);
    const auto r = be.restore([](const rec::Progress&) {});
    EF_CHECK(r.kind == OutcomeKind::Restored);
    EF_CHECK(fake.values["t1"] == "0" && fake.values["t2"] == "0" && be.active_changes() == 0);
    EF_CHECK(be.restore([](const rec::Progress&) {}).kind == OutcomeKind::NothingToDo);

    // Uygulama hatasi -> geri yuklendi, Acik yok
    fake.fail_apply_target = "t2";
    EF_CHECK(be.apply(plan, {}, cancel).kind == OutcomeKind::FailedRestored);
    EF_CHECK(fake.values["t1"] == "0" && be.active_changes() == 0);
    fake.fail_apply_target.clear();

    // Geri alma dogrulanamazsa kurtarma gerekir
    EF_CHECK(be.apply(plan, {}, cancel).kind == OutcomeKind::Active);
    fake.fail_restore_target = "t1";
    EF_CHECK(be.restore({}).kind == OutcomeKind::RecoveryRequired && be.recovery_needed());

    // Yonetici adimi broker olmadan calismaz
    Plan admin;
    admin.items = {item("t9", false, true)};
    EF_CHECK(be.apply(admin, {}, cancel).kind == OutcomeKind::Denied);
}

EF_TEST(perf_journal_write_failure_applies_nothing) {
    rec::MemoryJournalFs fs;
    fs.fail_write_after = 0;
    rec::JournalStore store(fs, "m");
    rec::FakeAdapter fake;
    fake.values["t1"] = "0";
    rec::AdapterRegistry reg;
    reg.add(OperationId::VisualEffectsSet, &fake);
    rec::TransactionEngine eng(store, reg);
    JournalBackendDeps deps;
    deps.scope = {"user", &store, &eng};
    deps.new_tx_id = [] { return std::string(32, 'a'); };
    JournalBackend be(deps);
    Plan p;
    p.items = {item("t1")};
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(p, {}, cancel).kind == OutcomeKind::FailedRestored && fake.values["t1"] == "0" && fake.applies == 0);
}

// CORE-03: acilista "yeni sema" gorulduyse UI arka ucu yeni degisiklik istemez (broker disi yol).
EF_TEST(perf_journal_backend_blocks_when_startup_recovery_reported_newer_schema) {
    rec::MemoryJournalFs fs;
    rec::JournalStore store(fs, "machine-1");
    rec::FakeAdapter fake;
    fake.values["t1"] = "0";
    rec::AdapterRegistry reg;
    reg.add(OperationId::VisualEffectsSet, &fake);
    rec::TransactionEngine eng(store, reg);
    JournalBackendDeps deps;
    deps.scope = {"user", &store, &eng};
    deps.new_tx_id = [] { return std::string(32, 'a'); };
    deps.writes_blocked = [] { return true; };  // run_startup_recovery().writes_blocked
    JournalBackend be(deps);
    Plan p;
    p.items = {item("t1")};
    std::atomic<bool> cancel{false};
    const auto out = be.apply(p, {}, cancel);
    EF_CHECK(out.kind == OutcomeKind::Denied);
    EF_CHECK(fake.applies == 0 && fake.values["t1"] == "0");
    EF_CHECK(fs.files.empty());  // hicbir journal kaydi yazilmadi
}

// CORE-03: koruma yalniz UI bayragina bagli degil; journal'da yeni sema varsa motor da reddeder ve
// JournalBackend bunu "geri alindi" degil Denied olarak gosterir.
EF_TEST(perf_journal_backend_newer_schema_in_journal_is_denied_not_failed_restored) {
    rec::MemoryJournalFs fs;
    fs.files.emplace_back("tx-" + std::string(32, 'b') + ".json", R"({"schema_version":2,"sha256":"00","tx":{}})");
    rec::JournalStore store(fs, "machine-1");
    rec::FakeAdapter fake;
    fake.values["t1"] = "0";
    rec::AdapterRegistry reg;
    reg.add(OperationId::VisualEffectsSet, &fake);
    rec::TransactionEngine eng(store, reg);
    JournalBackendDeps deps;
    deps.scope = {"user", &store, &eng};
    deps.new_tx_id = [] { return std::string(32, 'c'); };
    JournalBackend be(deps);
    Plan p;
    p.items = {item("t1")};
    std::atomic<bool> cancel{false};
    const auto out = be.apply(p, {}, cancel);
    EF_CHECK(out.kind == OutcomeKind::Denied);
    EF_CHECK(fake.applies == 0 && fake.values["t1"] == "0");
    EF_CHECK(fs.files.size() == 1);  // yalniz yeni sema dosyasi; yeni kayit yok
}

EF_TEST(perf_power_plan_rules) {
    cw::FakePowerApi api;
    // Dengeli + mod destekli -> en iyi performans modu
    auto items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].op.id == OperationId::PowerModeSet &&
              items[0].op.new_value == cw::kModeBestPerformance && !items[0].op.affects_quality);
    api.mode = std::string(cw::kModeBestPerformance);
    EF_CHECK(power_plan_items(api).empty());  // zaten en iyi: sifir is
    // Mod desteklenmiyor -> var olan Yuksek performans planina gecis
    api.modes_supported = false;
    items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].op.id == OperationId::PowerSchemeSet && items[0].op.new_value == kSchemeHighPerformance);
    // Yuksek performans plani yoksa olusturulmaz
    api.schemes = {std::string(cw::kSchemeBalanced)};
    EF_CHECK(power_plan_items(api).empty());
    // Zaten yuksek performans veya kullanicinin ozel plani: dokunulmaz
    api.schemes = {std::string(cw::kSchemeBalanced), std::string(kSchemeHighPerformance), "e9a42b02-d5df-448d-aa00-03f14749eb61"};
    api.scheme = std::string(kSchemeHighPerformance);
    EF_CHECK(power_plan_items(api).empty());
    api.scheme = "e9a42b02-d5df-448d-aa00-03f14749eb61";
    EF_CHECK(power_plan_items(api).empty());
}

EF_TEST(perf_power_plan_battery_adds_dc_step) {
    // Gap 11: pilli cihaz -> AC ve DC ayri adimlar, ayni grup; alan anahtarlari kaynagi gosterir.
    cw::FakePowerApi api;
    api.battery = true;
    auto items = power_plan_items(api);
    EF_CHECK(items.size() == 2);
    EF_CHECK(items[0].op.target == cw::kTargetPowerModeAc && items[0].field_key == "plan.field.power_mode_ac");
    EF_CHECK(items[1].op.target == cw::kTargetPowerModeDc && items[1].field_key == "plan.field.power_mode_dc");
    EF_CHECK(items[1].op.id == OperationId::PowerModeSet && items[1].op.new_value == cw::kModeBestPerformance);
    EF_CHECK(items[0].group() == items[1].group());
    // AC zaten en iyi, DC degil -> yalniz DC adimi
    api.mode = std::string(cw::kModeBestPerformance);
    items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].op.target == cw::kTargetPowerModeDc);
    // Ikisi de en iyi -> sifir is
    api.dc_mode = std::string(cw::kModeBestPerformance);
    EF_CHECK(power_plan_items(api).empty());
}

EF_TEST(perf_power_plan_dc_unsupported_marks_ac_only) {
    cw::FakePowerApi api;
    api.battery = true;
    api.dc_supported = false;
    auto items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].op.target == cw::kTargetPowerModeAc &&
              items[0].field_key == "plan.field.power_mode_ac_only");
    // Pil durumu okunamaz veya pil yok -> eski davranis: yalniz AC, alan anahtari yok (uydurma yok)
    api.battery_readable = false;
    items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].field_key.empty());
    api.battery_readable = true;
    api.battery = false;
    api.dc_supported = true;
    items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].field_key.empty());
}

EF_TEST(perf_power_plan_dc_read_error_is_not_unsupported) {
    // B7 inceleme: yalniz Unsupported "degistirilemiyor" der; okuma hatasi/bilinmeyen GUID "okunamadi".
    cw::FakePowerApi api;
    api.battery = true;
    for (const auto e : {ef::contracts::ErrorCode::IoError, ef::contracts::ErrorCode::Denied}) {
        api.dc_read_error = e;
        const auto items = power_plan_items(api);
        EF_CHECK(items.size() == 1 && items[0].op.target == cw::kTargetPowerModeAc &&
                  items[0].field_key == "plan.field.power_mode_ac_dc_unknown");
    }
    api.dc_read_error.reset();
    api.dc_mode = "11111111-2222-3333-4444-555555555555";  // bilinen dort modun disinda
    auto items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].op.target == cw::kTargetPowerModeAc &&
              items[0].field_key == "plan.field.power_mode_ac_dc_unknown");
    api.dc_mode = std::string(cw::kModeBalanced);
    api.dc_supported = false;
    items = power_plan_items(api);
    EF_CHECK(items.size() == 1 && items[0].field_key == "plan.field.power_mode_ac_only");
}

EF_TEST(perf_stale_confirmation_is_not_applied) {
    // OPT-02: onaydan sonra sistem degisirse (plan farkli) eski onayla hicbir sey yazilmaz.
    FakeBackend b;
    b.next_plan.items = {item("a"), item("b")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming);
    b.next_plan.items = {item("a"), item("c")};  // baska bir arac "b"yi zaten uygulamis, "c" eklenmis
    c.answer(true);
    loop.drain();
    EF_CHECK(b.applies == 0 && c.phase() == Phase::Confirming && c.notice() == Notice::PlanChanged);
    EF_CHECK(c.plan().items.size() == 2 && c.plan().items[1].op.target == "c");  // guncel liste onayda
    c.answer(true);  // yeni liste onaylandi
    loop.drain();
    EF_CHECK(b.applies == 1 && c.phase() == Phase::On && c.notice() == Notice::None);
    EF_CHECK(plan_fingerprint(b.next_plan) != plan_fingerprint(Plan{{item("a"), item("b")}}));
}

EF_TEST(perf_auto_mode_applies_and_restores_only_auto) {
    FakeBackend b;
    b.next_plan.items = {item("a")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.auto_apply();  // onayli oyun basladi
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.auto_active() && b.applies == 1);
    c.auto_restore();  // oyun kapandi
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && !c.auto_active() && b.restores == 1);
    // Elle acilan Performans, oyun kapaninca otomatik kapatilmaz.
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && !c.auto_active());
    c.auto_restore();
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && b.restores == 1);
    c.auto_apply();  // zaten acik: yok sayilir
    loop.drain();
    EF_CHECK(b.applies == 2);
}

// Yonetici islemi (Oyun Hazirligi UAC/broker) surerken: asama degismez, uygulama/geri yukleme baslamaz, bildirim var.
EF_TEST(perf_notify_busy_keeps_phase_and_starts_nothing) {
    FakeBackend b;
    b.next_plan.items = {item("a")};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.notify_busy();
    EF_CHECK(c.phase() == Phase::Off && c.notice() == Notice::AdminBusy && b.applies == 0 && loop.q.empty());
    c.toggle();
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming);
    c.notify_busy();  // onay asamasi korunur; Evet sonra yine denenebilir
    EF_CHECK(c.phase() == Phase::Confirming && c.notice() == Notice::AdminBusy && b.applies == 0);
    c.answer(true);
    EF_CHECK(c.phase() == Phase::Applying);
    c.notify_busy();  // is suren asamada yok sayilir
    EF_CHECK(c.phase() == Phase::Applying && c.notice() == Notice::None);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && b.applies == 1 && b.restores == 0);
}

// Acilis uzlastirmasi: kayit "Active" dese bile hedefler okunur. Hepsi etkinse Acik; bazilari baska program tarafindan
// geri cevrildiyse Acik + hangi ayarlar kapali listelenir; hicbiri etkin degilse Kapali. Cik ayar yazmaz.
namespace {
struct LiveRig {
    rec::MemoryJournalFs fs;
    rec::JournalStore store{fs, "machine-1"};
    rec::FakeAdapter fake;
    rec::AdapterRegistry reg;
    rec::TransactionEngine eng{store, reg};
    Plan plan;
    int ids = 0;
    JournalBackendDeps deps;
    LiveRig() {
        fake.values["t1"] = "0";
        fake.values["t2"] = "0";
        reg.add(OperationId::VisualEffectsSet, &fake);
        plan.items = {item("t1"), item("t2")};
        deps.scope = {"user", &store, &eng};
        deps.build_plan = [this] { return ef::contracts::Result<Plan>(plan); };
        deps.new_tx_id = [this] {
            char b[33];
            std::snprintf(b, sizeof b, "%032d", ++ids);
            return std::string(b);
        };
        deps.now_unix_ms = [this] { return std::int64_t{1000} * ids; };
    }
};
}  // namespace

EF_TEST(perf_startup_reconcile_reads_real_targets) {
    LiveRig r;
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    EF_CHECK(r.fake.values["t1"] == "1" && r.fake.values["t2"] == "1");
    const int restores_before = r.fake.restores;

    // Yeniden acilis (yeni denetleyici, ayni kayit): hepsi hala uygulanmis -> Acik, kapali yok.
    {
        Loop loop;
        PerfController c(be, loop.exec(), loop.post());
        c.init();
        EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::None && c.off_items().empty() && c.active_changes() == 2);
    }
    // Biri disaridan geri cevrildi: kayit hala Active ama gercek durum kismi -> Acik + yalniz o ayar kapali listelenir.
    r.fake.values["t2"] = "0";
    {
        Loop loop;
        PerfController c(be, loop.exec(), loop.post());
        c.init();
        EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::SomeOff);
        EF_CHECK(c.active_changes() == 1 && c.off_items().size() == 1 && c.off_items()[0].target == "t2");
        EF_CHECK(c.off_items()[0].reason == rec::RestoreFailReason::External && c.off_items()[0].observed == "0");
    }
    // Hepsi geri cevrildi: Kapali (bayrak degil gercek durum), yine de hangileri kapali yazar.
    r.fake.values["t1"] = "0";
    {
        Loop loop;
        PerfController c(be, loop.exec(), loop.post());
        c.init();
        EF_CHECK(c.phase() == Phase::Off && c.notice() == Notice::SomeOff && c.off_items().size() == 2 && c.active_changes() == 0);
    }
    EF_CHECK(r.fake.restores == restores_before);  // uzlastirma salt okuma: hicbir ayar yazilmadi/geri alinmadi
}

EF_TEST(perf_reapply_after_drift_retires_stale_steps_and_restores_original) {
    LiveRig r;
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    r.fake.values["t2"] = "0";  // disaridan geri cevrildi
    r.plan.items = {item("t2")};  // plan yalniz eksik olani yeniden uygular ("zaten uygun" olanlar planda yok)

    Loop loop;
    PerfController c(be, loop.exec(), loop.post());
    c.init();
    EF_CHECK(c.phase() == Phase::On && c.off_items().size() == 1);
    c.toggle();  // Acik + kapali ayar var: geri alma onayi degil, yeniden uygulama plani
    EF_CHECK(c.phase() == Phase::Planning);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Confirming);
    c.answer(false);  // Hayir: hicbir sey degismez, Acik (kismi) kalir
    EF_CHECK(c.phase() == Phase::On && c.off_items().size() == 1 && r.fake.values["t2"] == "0");
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::None && c.off_items().empty() && c.active_changes() == 2);
    EF_CHECK(r.fake.values["t1"] == "1" && r.fake.values["t2"] == "1");

    // Kapali: cakisma yok; ilk (uygulama oncesi) degerler geri gelir.
    c.toggle();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off);
    EF_CHECK(r.fake.values["t1"] == "0" && r.fake.values["t2"] == "0");
}

// P2: arka plan grubu kullanici acikca Evet demeden uygulanmaz; Evet ile yalniz secilen gruplar gider.
EF_TEST(perf_background_group_is_off_until_user_selects_it) {
    BgCandidate c;
    c.display_name = "Discord";
    c.cpu_pct = 2.0;
    c.working_set_bytes = 300ull << 20;
    c.processes.push_back({10, 5010, "C:\\Users\\x\\AppData\\Local\\Discord\\Discord.exe"});
    FakeBackend b;
    b.next_plan.items = {item("a")};
    for (auto& i : background_plan_items({c})) b.next_plan.items.push_back(std::move(i));
    EF_CHECK(b.next_plan.items.size() == 2);
    Loop loop;
    PerfController c1(b, loop.exec(), loop.post());
    c1.init();
    EF_CHECK(!c1.selected("plan.background") && c1.selected("plan.power_mode"));
    c1.toggle();
    loop.drain();
    EF_CHECK(c1.phase() == Phase::Confirming);
    c1.answer(true);
    loop.drain();
    EF_CHECK(c1.phase() == Phase::On && c1.active_changes() == 1);  // yalniz varsayilan gruplar; arka plan yok

    FakeBackend b2;
    b2.next_plan = b.next_plan;
    Loop loop2;
    PerfController c2(b2, loop2.exec(), loop2.post());
    c2.init();
    c2.set_selected("plan.background", true);
    c2.toggle();
    loop2.drain();
    c2.answer(true);
    loop2.drain();
    EF_CHECK(c2.phase() == Phase::On && c2.active_changes() == 2);
    // Hayir: hicbir sey uygulanmaz.
    FakeBackend b3;
    b3.next_plan = b.next_plan;
    Loop loop3;
    PerfController c3(b3, loop3.exec(), loop3.post());
    c3.init();
    c3.set_selected("plan.background", true);
    c3.toggle();
    loop3.drain();
    c3.answer(false);
    loop3.drain();
    EF_CHECK(b3.applies == 0 && c3.phase() == Phase::Off);
}

// P11: arka plan hedefindeki surec kimligi/olusturma zamani parmak izini degistirmez; yol ozeti degisirse degisir.
EF_TEST(perf_plan_fingerprint_background_stable_across_pids) {
    auto bgitem = [](const char* t) {
        PlanItem i = item(t);
        i.op.id = ef::contracts::OperationId::BackgroundProcessLower;
        i.title_key = i.group_key = kBackgroundGroup;
        return i;
    };
    Plan a, b, c;
    a.items = {item("user"), bgitem("bgproc:100:5:aaaa"), bgitem("bgproc:101:6:aaaa")};
    b.items = {item("user"), bgitem("bgproc:777:9:aaaa")};  // baska pid, ayni program
    c.items = {item("user"), bgitem("bgproc:100:5:bbbb")};  // baska program
    EF_CHECK(plan_fingerprint(a) == plan_fingerprint(b));
    EF_CHECK(plan_fingerprint(a) != plan_fingerprint(c));
}

// P1/P11: deney kapsami yalniz yonetici/yeniden baslatma istemeyen ogeleri (arka plan dahil) uygular; UAC'li ogeler backend'e HIC gitmez.
EF_TEST(perf_experiment_scope_applies_only_non_admin_subset) {
    FakeBackend b;
    PlanItem user = item("user");
    PlanItem adm = item("adm", false, true);
    PlanItem reb = item("reb");
    reb.op.needs_reboot = true;
    PlanItem bg = item("bg");
    bg.title_key = bg.group_key = kBackgroundGroup;
    b.next_plan.items = {user, adm, reb, bg};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    EF_CHECK(experiment_scope_item(user) && !experiment_scope_item(adm) && !experiment_scope_item(reb) && experiment_scope_item(bg));
    c.set_selected(kBackgroundGroup, true);  // kullanici secimi: arka plan Evet
    c.set_experiment_scope(true);
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && b.applies == 1);
    EF_CHECK(b.last_applied.items.size() == 2 && b.last_applied.items[0].op.target == "user" && b.last_applied.items[1].op.target == "bg");
    EF_CHECK(c.selected(kBackgroundGroup));  // kayitli secim degismedi
    // Kapsam kapaliyken (normal Performans) tum secili ogeler uygulanir.
    FakeBackend b2;
    b2.next_plan.items = {user, adm, reb, bg};
    Loop loop2;
    PerfController c2(b2, loop2.exec(), loop2.post());
    c2.init();
    c2.set_selected(kBackgroundGroup, true);
    c2.toggle();
    loop2.drain();
    c2.answer(true);
    loop2.drain();
    EF_CHECK(b2.last_applied.items.size() == 4);
}

// P1 (hizmet yolu hazir): yonetici ogeler de kapsamda; yalniz hizmetin yurutebildigi islemler. Reboot yine disarida.
EF_TEST(perf_experiment_scope_includes_service_handled_admin_when_admin_ok) {
    FakeBackend b;
    PlanItem user = item("user");
    PlanItem adm = item("adm", false, true);
    adm.op.id = ef::contracts::OperationId::PowerSettingSet;
    PlanItem dns = item("dns", false, true);
    dns.op.id = ef::contracts::OperationId::DnsServersSet;  // hizmet yolu degil (her zaman UAC broker)
    PlanItem reb = item("reb", false, true);
    reb.op.id = ef::contracts::OperationId::PowerSettingSet;
    reb.op.needs_reboot = true;
    EF_CHECK(experiment_scope_item(adm, true) && !experiment_scope_item(adm, false));
    EF_CHECK(!experiment_scope_item(dns, true) && !experiment_scope_item(reb, true));
    b.next_plan.items = {user, adm, dns, reb};
    Loop loop;
    PerfController c(b, loop.exec(), loop.post());
    c.init();
    c.set_experiment_scope(true, true);
    c.toggle();
    loop.drain();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && b.applies == 1);
    EF_CHECK(b.last_applied.items.size() == 2 && b.last_applied.items[0].op.target == "user" && b.last_applied.items[1].op.target == "adm");
    c.set_experiment_scope(false);
    EF_CHECK(!c.experiment_scope());
}

// ---- Koruma Modu (T07): sapma tespiti, Evet/Hayir ile yeniden uygulama, ilk degerlerin korunmasi ----
namespace {
std::string tx_bytes(LiveRig& r) {
    std::string out;
    for (const auto& tx : r.store.scan().transactions) out += rec::serialize(tx);
    return out;
}
}  // namespace

EF_TEST(perf_drift_prompt_yes_reapplies_and_restore_keeps_original_values) {
    LiveRig r;
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    r.fake.values["t2"] = "5";  // Windows guncellemesi / baska program degeri degistirdi
    const std::string journal_before = tx_bytes(r);

    Loop loop;
    PerfController c(be, loop.exec(), loop.post());
    c.init();
    EF_CHECK(c.phase() == Phase::On && c.drift_items().size() == 1 && c.drift_items()[0].op.target == "t2");
    EF_CHECK(c.drift_items()[0].current && *c.drift_items()[0].current == "5" && c.drift_items()[0].group_key == "plan.vfx");
    EF_CHECK(r.fake.values["t2"] == "5");  // tespit salt okuma: sessiz yeniden uygulama yok

    c.reapply_drift();  // "Yeniden uygula": once standart Evet/Hayir onayi
    EF_CHECK(c.phase() == Phase::Confirming && c.drift_confirm() && c.plan().items.size() == 1 && c.plan().items[0].group() == "plan.vfx");
    EF_CHECK(r.fake.values["t2"] == "5");
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::None && c.drift_items().empty() && c.off_items().empty());
    EF_CHECK(r.fake.values["t1"] == "1" && r.fake.values["t2"] == "1" && c.active_changes() == 2);
    EF_CHECK(tx_bytes(r) == journal_before);  // onceki-deger anlik goruntusu ve kayit birebir ayni

    // Kapali: DRIFT degeri ("5") degil, uygulama oncesi ilk degerler geri gelir.
    c.toggle();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off);
    EF_CHECK(r.fake.values["t1"] == "0" && r.fake.values["t2"] == "0");
}

EF_TEST(perf_drift_prompt_no_changes_nothing_and_keep_retires_step) {
    LiveRig r;
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    r.fake.values["t2"] = "5";
    Loop loop;
    PerfController c(be, loop.exec(), loop.post());
    c.init();
    c.reapply_drift();
    c.answer(false);  // Hayir: hicbir sey yazilmaz, sapma bildirimi kalir
    EF_CHECK(c.phase() == Phase::On && !c.drift_confirm() && c.drift_items().size() == 1 && r.fake.values["t2"] == "5");
    const int applies = r.fake.applies;

    c.keep_drift();  // "Boyle kalsin": mevcut deger korunur, ayar yazilmaz
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.drift_items().empty() && c.off_items().empty() && c.active_changes() == 1);
    EF_CHECK(r.fake.values["t2"] == "5" && r.fake.applies == applies);
    c.toggle();  // Kapali: yalniz hala bizim olan t1 geri alinir; kullanicinin korudugu t2 ezilmez
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::Off && r.fake.values["t1"] == "0" && r.fake.values["t2"] == "5");
}

EF_TEST(perf_drift_reapply_blocked_by_game_gate_writes_nothing) {
    LiveRig r;
    bool allowed = true;
    r.deps.permit = [&]() { return rec::WriteDecision{allowed, allowed ? "" : "gate.game_running"}; };
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    r.fake.values["t2"] = "5";
    const std::string journal_before = tx_bytes(r);
    Loop loop;
    PerfController c(be, loop.exec(), loop.post());
    c.init();
    c.reapply_drift();
    allowed = false;  // onay beklerken oyun basladi
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::GameBlocked);
    EF_CHECK(r.fake.values["t2"] == "5" && c.drift_items().size() == 1 && tx_bytes(r) == journal_before);
}

EF_TEST(perf_drift_reapply_failure_reports_honestly_and_keeps_journal) {
    LiveRig r;
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    r.fake.values["t2"] = "5";
    r.fake.fail_apply_target = "t2";  // yeniden yazma basarisiz
    const std::string journal_before = tx_bytes(r);
    Loop loop;
    PerfController c(be, loop.exec(), loop.post());
    c.init();
    c.reapply_drift();
    c.answer(true);
    loop.drain();
    EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::DriftFailed && c.drift_items().size() == 1);
    EF_CHECK(r.fake.values["t2"] == "5" && tx_bytes(r) == journal_before);
}

EF_TEST(perf_no_drift_when_nothing_changed) {
    LiveRig r;
    JournalBackend be(r.deps);
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(r.plan, {}, cancel).kind == OutcomeKind::Active);
    Loop loop;
    PerfController c(be, loop.exec(), loop.post());
    c.init();
    EF_CHECK(c.drift_items().empty());
    c.reapply_drift();  // sapma yok: onay acilmaz
    EF_CHECK(c.phase() == Phase::On);
}

// R-E2: yeniden acilista MAKINE kapsamli (yonetici) etkin ayarlar da gercek degerleriyle denetlenir (salt okuma).
// Onceden yalniz kullanici kapsamindaki kayit okunuyor, yonetici ayarlari (HAGS, guc alt ayarlari, NVIDIA profil degeri...)
// baska program/surucu guncellemesi ile geri cevrilse bile "Acik" gorunuyordu.
namespace {
struct UnreadableAdapter final : rec::SettingAdapter {  // okuma her zaman hata verir (yazma yolu yok)
    ef::contracts::Result<rec::Snapshot> capture(const SettingOperation&) override { return ef::contracts::Result<rec::Snapshot>::fail(ef::contracts::ErrorCode::IoError, "x"); }
    ef::contracts::Status apply(const SettingOperation&) override { return ef::contracts::Status::fail(ef::contracts::ErrorCode::IoError, "x"); }
    ef::contracts::Result<std::optional<std::string>> read_back(const SettingOperation&) override {
        return ef::contracts::Result<std::optional<std::string>>::fail(ef::contracts::ErrorCode::IoError, "unreadable");
    }
    ef::contracts::Status restore(const SettingOperation&, const rec::Snapshot&) override { return ef::contracts::Status::fail(ef::contracts::ErrorCode::IoError, "x"); }
};
struct MachineAdmin final : ef::core::broker::AdminChannel {
    std::vector<SettingOperation> ops;
    ef::core::broker::BatchResult run(ef::core::ipc::RequestAction, const std::vector<SettingOperation>&) override { return {}; }
    std::vector<SettingOperation> active_ops() override { return ops; }
};
}  // namespace

EF_TEST(perf_startup_reconcile_checks_machine_scope_targets_too) {
    LiveRig r;
    rec::FakeAdapter machine_fake;
    machine_fake.values["hags:hwschmode"] = "2";
    machine_fake.values["power:boost:ac"] = "1";
    r.reg.add(OperationId::HagsSet, &machine_fake);
    r.reg.add(OperationId::PowerSettingSet, &machine_fake);
    MachineAdmin admin;
    admin.ops = {SettingOperation{OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", true, false},
                 SettingOperation{OperationId::PowerSettingSet, OperationScope::Machine, "power:boost:ac", "1", false, false}};
    r.deps.admin = &admin;
    JournalBackend be(r.deps);

    // Hepsi yazdigimiz degerde: Acik, uyari yok.
    {
        Loop loop;
        PerfController c(be, loop.exec(), loop.post());
        c.init();
        EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::None && c.off_items().empty() && c.active_changes() == 2);
    }
    // Biri disaridan degisti (HAGS yeniden baslatma bekleyen ayar olsa da okunan deger yazdigimiz degil): uyari + o ayar listelenir.
    machine_fake.values["power:boost:ac"] = "0";
    {
        Loop loop;
        PerfController c(be, loop.exec(), loop.post());
        c.init();
        EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::SomeOff && c.active_changes() == 1);
        EF_CHECK(c.off_items().size() == 1 && c.off_items()[0].target == "power:boost:ac" && c.off_items()[0].observed == "0");
        EF_CHECK(c.off_items()[0].expected == "1");
    }
    // Okunamayan makine ayari "degisti" sayilmaz (bilinmeyen != degisti); deger uydurulmaz.
    UnreadableAdapter unreadable;
    r.reg.add(OperationId::PowerSettingSet, &unreadable);
    r.reg.add(OperationId::HagsSet, &unreadable);
    {
        Loop loop;
        PerfController c(be, loop.exec(), loop.post());
        c.init();
        EF_CHECK(c.phase() == Phase::On && c.notice() == Notice::None && c.off_items().empty());
    }
    EF_CHECK(machine_fake.restores == 0);  // salt okuma
}
