// Saha 2026-11-15 (ikinci arkadas, 0.6.0, masaustu NVIDIA): "Geri donus tamamlanamadi" + kapatilamayan uygulama.
// Sahte adaptorlerle her yanlis-basarisizlik nedeni, gercek uyusmazligin basarisiz kalmasi, cikis karari, kayit okunabilirligi
// ve destek tanisinin redaksiyonu.
#include <string>
#include <vector>

#include "core/adapters/windows/gaming_toggles.h"
#include "core/adapters/windows/power_subs.h"
#include "core/perf/exit_policy.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"
#include "core/recovery/restore_report.h"
#include "core/recovery/sha256.h"
#include "core/serialization/json.h"
#include "ef_test.h"
#include "ui/shell/recovery_text.h"

using namespace ef::core::recovery;
using namespace ef::core::adapters::windows;
using ef::contracts::ErrorCode;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using ef::contracts::TxState;

namespace {
const std::wstring kDx = FakeUserRegistry::key(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", L"DirectXUserGlobalSettings");
const std::string kBalanced = "381b4222-f694-41f0-9685-ff5bb260df2e";
const std::string kHigh = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
constexpr const char* kTx = "0123456789abcdef0123456789abcdef";

// Gercek adaptoru sarar; restore() hicbir sey yazmaz (geri alma gercekten olmadi). Gercek uyusmazligin basarisiz kaldigini
// gostermek icin; okuma ve eslesme kurali sarilan adaptorden gelir (gevsetme yok).
class NoopRestore final : public SettingAdapter {
public:
    explicit NoopRestore(SettingAdapter& a) : a_(a) {}
    ef::contracts::Result<Snapshot> capture(const SettingOperation& op) override { return a_.capture(op); }
    ef::contracts::Status apply(const SettingOperation& op) override { return a_.apply(op); }
    ef::contracts::Result<std::optional<std::string>> read_back(const SettingOperation& op) override { return a_.read_back(op); }
    ef::contracts::Status restore(const SettingOperation&, const Snapshot&) override { return ef::contracts::ok_status(); }
    ef::contracts::Result<std::optional<std::string>> read_for_restore(const SettingOperation& op, const Snapshot& s) override {
        return a_.read_for_restore(op, s);
    }
    bool matches_snapshot(const SettingOperation& op, const Snapshot& s, const std::optional<std::string>& v) override {
        return a_.matches_snapshot(op, s, v);
    }

private:
    SettingAdapter& a_;
};

JournalStep applied_step(SettingAdapter& a, const SettingOperation& op) {
    JournalStep s;
    s.op = op;
    auto snap = a.capture(op);
    EF_CHECK(snap.ok());
    if (snap.ok()) s.snapshot = snap.value();
    EF_CHECK(a.apply(op).ok());
    s.state = StepState::Verified;
    s.last_written = op.new_value;
    return s;
}

SettingOperation dx_op() { return SettingOperation{OperationId::GamingToggleSet, OperationScope::CurrentUser, "gaming:windowed_opt", "1", false, false}; }
SettingOperation boost_op() {
    return SettingOperation{OperationId::PowerSettingSet, OperationScope::Machine, std::string(kPowerBoostAcTarget), "1", false, false};
}

FakePowerSubApi two_plans() {
    FakePowerSubApi api;
    api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] = 4;
    api.schemes[kHigh][std::string(kPowerBoostAcTarget)] = 2;
    api.active = kBalanced;
    return api;
}

JournalTx one_step_tx(TxState st, JournalStep s) {
    JournalTx tx;
    tx.id = kTx;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    tx.state = st;
    tx.steps.push_back(std::move(s));
    return tx;
}
}  // namespace

// (1) Ortak DirectX dizesi vardi ama oge yoktu: geri alma ogeyi kaldirir; dogrulama bunu basari sayar.
EF_TEST(restore_dx_item_absent_in_existing_string_rolls_back) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    reg.values[kDx] = sz_raw(L"AutoHDREnable=0;");
    auto s = applied_step(a, dx_op());
    EF_CHECK(s.snapshot.existed && s.snapshot.old_value == "<absent>");  // existed = dize vardi
    EF_CHECK(rollback_step(a, s));
    EF_CHECK(s.state == StepState::RolledBack && s.error.empty() && s.observed.empty());
    EF_CHECK(raw_sz(reg.values[kDx]) == L"AutoHDREnable=0;");
    // Zaten eski durumdaysa (oge yok) yazmadan basari.
    EF_CHECK(rollback_step(a, s) && s.state == StepState::RolledBack);
}

EF_TEST(restore_dx_item_left_at_other_value_stays_failed) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    reg.values[kDx] = sz_raw(L"AutoHDREnable=0;");
    auto s = applied_step(a, dx_op());
    reg.values[kDx] = sz_raw(L"AutoHDREnable=0;SwapEffectUpgradeEnable=0;");  // baska bir program 0 yazdi
    EF_CHECK(!rollback_step(a, s));
    EF_CHECK(s.state == StepState::Failed && s.error == kExternalChange && s.observed == "0");
    EF_CHECK(classify_restore_failure(s, false) == RestoreFailReason::External);

    // Geri alma yazmadi (gercek basarisizlik): oge hala bizim degerimizde -> basarisiz, okunan deger kaydedilir.
    FakeUserRegistry reg2;
    GamingToggleAdapter real(reg2);
    NoopRestore broken(real);
    reg2.values[kDx] = sz_raw(L"AutoHDREnable=0;");
    auto s2 = applied_step(broken, dx_op());
    EF_CHECK(!rollback_step(broken, s2));
    EF_CHECK(s2.state == StepState::Failed && s2.error == "restore verification failed" && s2.observed == "1");
    EF_CHECK(classify_restore_failure(s2, false) == RestoreFailReason::NotVerified);
}

// (2) Kullanici Acik ile Kapali arasinda baska guc planina gecti: geri alma yakalanan PLANA yazar ve onu okur.
EF_TEST(restore_power_sub_after_plan_switch_targets_captured_plan) {
    auto api = two_plans();
    PowerSubAdapter a(api);
    auto s = applied_step(a, boost_op());
    EF_CHECK(s.snapshot.raw == kBalanced && s.snapshot.old_value == "4");
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] == 1);
    api.active = kHigh;  // kullanici plan degistirdi; etkin planda deger 2 (ne eski ne bizim)
    EF_CHECK(rollback_step(a, s));
    EF_CHECK(s.state == StepState::RolledBack);
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] == 4);  // dogru plan geri yuklendi
    EF_CHECK(api.schemes[kHigh][std::string(kPowerBoostAcTarget)] == 2);      // diger plana dokunulmadi
}

EF_TEST(restore_power_sub_wrong_value_in_captured_plan_still_fails) {
    auto api = two_plans();
    PowerSubAdapter a(api);
    auto s = applied_step(a, boost_op());
    api.active = kHigh;
    api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] = 3;  // yakalanan planda dis degisiklik
    EF_CHECK(!rollback_step(a, s) && s.state == StepState::Failed && s.error == kExternalChange && s.observed == "3");
    EF_CHECK(api.schemes[kBalanced][std::string(kPowerBoostAcTarget)] == 3);  // ezilmedi

    auto api2 = two_plans();
    PowerSubAdapter real(api2);
    NoopRestore broken(real);
    auto s2 = applied_step(broken, boost_op());
    api2.active = kHigh;
    EF_CHECK(!rollback_step(broken, s2));
    EF_CHECK(s2.state == StepState::Failed && s2.error == "restore verification failed" && s2.observed == "1");

    auto api3 = two_plans();
    PowerSubAdapter a3(api3);
    auto s3 = applied_step(a3, boost_op());
    api3.fail_write = true;  // fake: Denied
    EF_CHECK(!rollback_step(a3, s3) && s3.state == StepState::Failed && s3.error_code == "denied");
    EF_CHECK(classify_restore_failure(s3, false) == RestoreFailReason::Denied);
}

// (3) Cikis karari (owner 2026-11-17): Cik ayarlari geri ALMAZ. Is suruyorsa bekler, onay bekleyen plan Hayir sayilir,
// diger tum asamalarda ayarlara dokunmadan kapanir (tuzak yok, geri alma denemesi yok).
EF_TEST(exit_action_never_restores_and_never_traps_user) {
    using ef::core::perf::ExitAction;
    using ef::core::perf::Phase;
    using ef::core::perf::exit_action;
    EF_CHECK(exit_action(Phase::Off) == ExitAction::Quit);
    EF_CHECK(exit_action(Phase::On) == ExitAction::Quit);  // eskiden StartRestore: artik geri alma yok
    EF_CHECK(exit_action(Phase::ConfirmRestore) == ExitAction::Quit);
    EF_CHECK(exit_action(Phase::RecoveryNeeded) == ExitAction::Quit);  // eskiden "Yine de kapatilsin mi?" sorusu
    EF_CHECK(exit_action(Phase::Confirming) == ExitAction::DeclinePlan);
    EF_CHECK(exit_action(Phase::Planning) == ExitAction::Wait);
    EF_CHECK(exit_action(Phase::Applying) == ExitAction::Wait);
    EF_CHECK(exit_action(Phase::Restoring) == ExitAction::Wait);
}

// P1 deneyi suruyorsa Cik bekler (deney iptal + Kapali'ya donus bitmeden cikilmaz); deney yokken kural degismez.
EF_TEST(exit_action_waits_while_proof_experiment_runs) {
    using ef::core::perf::ExitAction;
    using ef::core::perf::Phase;
    using ef::core::perf::exit_action;
    for (const auto ph : {Phase::Off, Phase::On, Phase::Confirming, Phase::ConfirmRestore, Phase::RecoveryNeeded, Phase::Planning,
                          Phase::Applying, Phase::Restoring})
        EF_CHECK(exit_action(ph, true) == ExitAction::Wait);
    for (const auto ph : {Phase::Off, Phase::On, Phase::Confirming, Phase::Restoring})
        EF_CHECK(exit_action(ph, false) == exit_action(ph));  // deney yok: Cik Performans'i geri almaz (Acik -> Quit)
    EF_CHECK(exit_action(Phase::On, false) == ExitAction::Quit);
}

// (4) Geri okunamayacak kayit diske yazilmaz; revizyon geri alinir.
EF_TEST(journal_persist_rejects_record_that_will_not_read_back) {
    MemoryJournalFs fs;
    JournalStore store{fs, "m1"};
    JournalStep s;
    s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, "a", "1", false, false};
    s.snapshot.raw_type = std::string(40, 'x');  // okuma siniri 32
    auto tx = one_step_tx(TxState::Prepared, s);
    EF_CHECK(!store.begin(tx).ok());
    EF_CHECK(fs.files.empty() && tx.revision == 0);
    tx.machine_id = "m1";
    tx.revision = 5;
    const auto st = store.persist(tx);
    EF_CHECK(!st.ok() && st.error().code == ErrorCode::ParseLimit && tx.revision == 5 && fs.files.empty());
    tx.steps[0].snapshot.raw_type = "dword";
    EF_CHECK(store.persist(tx).ok() && tx.revision == 6 && fs.files.size() == 1);
}

// 0.6.0 hata metnini sinirsiz yaziyordu: 600 baytlik hata iceren kayit okunur (kesilir) ve geri alinabilir.
EF_TEST(journal_old_record_with_long_error_reads_and_reconciles) {
    MemoryJournalFs fs;
    JournalStore store{fs, "m1"};
    FakeAdapter fake;
    fake.values["a"] = "1";  // bizim degerimiz
    JournalStep s;
    s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, "a", "1", false, false};
    s.snapshot.existed = true;
    s.snapshot.old_value = "0";
    s.state = StepState::Failed;
    s.last_written = "1";
    s.error = std::string(kStepErrorMax, 'E');
    auto tx = one_step_tx(TxState::RecoveryRequired, s);
    tx.machine_id = "m1";
    tx.revision = 3;
    std::string body = ef::core::json::write(to_json(tx));
    const auto p = body.find(std::string(kStepErrorMax, 'E'));
    EF_CHECK(p != std::string::npos);
    body.replace(p, kStepErrorMax, std::string(600, 'E'));
    fs.files.emplace_back(std::string("tx-") + kTx + ".json", std::string(R"({"schema_version":)") + std::to_string(kJournalSchema) +
                                                                   R"(,"sha256":")" + sha256_hex(body) + R"(","tx":)" + body + "}");
    auto l = store.scan();
    EF_CHECK(l.corrupt.empty() && l.transactions.size() == 1);
    if (l.transactions.size() != 1) return;
    EF_CHECK(l.transactions[0].steps[0].error.size() == kStepErrorMax);
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &fake);
    TransactionEngine engine{store, reg};
    auto rb = engine.rollback(l.transactions[0]);
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored && fake.values["a"] == "0");
}

// (5) UTF-8 sinirinda kesme.
EF_TEST(clamp_utf8_respects_code_point_boundaries) {
    EF_CHECK(clamp_utf8("abc", 5) == "abc");
    EF_CHECK(clamp_utf8("abcdef", 3) == "abc");
    const std::string tr = "a\xC3\xA7\xC4\x9F";  // a + c-cedilla + g-breve (2 bayt + 2 bayt)
    EF_CHECK(clamp_utf8(tr, 2) == "a");           // c-cedilla'nin ortasinda kesilmez
    EF_CHECK(clamp_utf8(tr, 3) == "a\xC3\xA7");
    EF_CHECK(clamp_utf8(tr, 4) == "a\xC3\xA7");
    EF_CHECK(clamp_utf8("\xE2\x82\xAC", 2).empty());  // 3 baytlik euro
}

// (6) Destek redaksiyonu: yol/komut satiri tasiyabilen islemlerde hedef ve degerler bos; yol ve SID metinden atilir.
EF_TEST(redact_for_support_clears_unsafe_ops_and_paths) {
    FailedRestoreItem i;
    i.op = OperationId::StartupRunEntry;
    i.target = "run:Steam";
    i.expected = "\"C:\\Users\\Ayse\\steam.exe\" -silent";
    i.observed = "<absent>";
    i.written = "x";
    i.error = "cannot read C:\\Users\\Ayse\\AppData\\Local\\EcuFps\\journal\\tx.json for S-1-5-21-111-222-333-1001 now";
    auto r = redact_for_support(i);
    EF_CHECK(r.target.empty() && r.expected.empty() && r.observed.empty() && r.written.empty());
    EF_CHECK(r.error.find("Ayse") == std::string::npos && r.error.find("S-1-5-21") == std::string::npos);
    EF_CHECK(r.error == "cannot read <path> for <sid> now");
    i.op = OperationId::AppCompatLayerSet;
    i.target = "compat:C:\\Games\\x.exe";
    r = redact_for_support(i);
    EF_CHECK(r.target.empty());
    // Guvenli islem: degerler kalir.
    FailedRestoreItem g;
    g.op = OperationId::PowerSettingSet;
    g.target = "power:boost:ac";
    g.expected = "4";
    g.observed = "1";
    r = redact_for_support(g);
    EF_CHECK(r.target == "power:boost:ac" && r.expected == "4" && r.observed == "1");
    EF_CHECK(sanitize_support_text("\\\\server\\share\\a b", 64) == "<path> b");
    EF_CHECK(sanitize_support_text("D:/x/y.log;ok", 64) == "<path>;ok");
    EF_CHECK(sanitize_support_text("ratio 1:2 ok", 64) == "ratio 1:2 ok");
    const auto line = restore_log_line(i);
    EF_CHECK(line.find("Ayse") == std::string::npos && line.find("Games") == std::string::npos);
}

// (7) Basarisiz ogeler: RecoveryRequired, yarim kalmis ve bozuk kayitlar; etkin ve geri alinmis kayitlar degil.
EF_TEST(failed_restore_items_across_states) {
    JournalStore::Listing l;
    l.corrupt.push_back(std::string("tx-") + kTx + ".json");
    JournalStep failed;
    failed.op = boost_op();
    failed.snapshot.existed = true;
    failed.snapshot.old_value = "4";
    failed.state = StepState::Failed;
    failed.last_written = "1";
    failed.observed = "1";
    failed.error = "restore verification failed";
    JournalStep done = failed;
    done.state = StepState::RolledBack;
    JournalStep applied = failed;
    applied.state = StepState::Applied;
    applied.error.clear();
    applied.observed.clear();
    auto rr = one_step_tx(TxState::RecoveryRequired, failed);
    rr.steps.push_back(done);
    l.transactions.push_back(rr);
    l.transactions.push_back(one_step_tx(TxState::Applying, applied));
    l.transactions.push_back(one_step_tx(TxState::Active, applied));
    l.transactions.push_back(one_step_tx(TxState::Restored, done));
    const auto items = failed_restore_items("machine", l);
    EF_CHECK(items.size() == 3);
    if (items.size() != 3) return;
    EF_CHECK(items[0].journal_unreadable && restore_item_name_key(items[0]) == "restore.item.journal_file");
    EF_CHECK(!items[1].interrupted && items[1].expected == "4" && items[1].observed == "1" && items[1].written == "1");
    EF_CHECK(items[1].reason == RestoreFailReason::NotVerified && restore_item_name_key(items[1]) == "restore.item.power_boost");
    EF_CHECK(items[2].interrupted && items[2].scope == "machine");
    const auto j = ef::core::json::write(to_json(items));
    EF_CHECK(j.find("\"setting\":\"power:boost:ac\"") != std::string::npos && j.find("\"expected\":\"4\"") != std::string::npos);
}

// Neden sinifi yapisal veriden; eski kayitlarda yalniz erisim reddi metni.
EF_TEST(classify_restore_failure_uses_structured_data) {
    JournalStep s;
    s.state = StepState::Failed;
    s.error = "restore verification failed";
    EF_CHECK(classify_restore_failure(s, false) == RestoreFailReason::NotVerified);
    EF_CHECK(classify_restore_failure(s, true) == RestoreFailReason::NeedsRestart);
    s.error_code = "denied";
    EF_CHECK(classify_restore_failure(s, true) == RestoreFailReason::Denied);
    s.error_code = "unsupported";
    s.error = "privilege not held; restart later";  // metin artik sinif belirlemez
    EF_CHECK(classify_restore_failure(s, false) == RestoreFailReason::NotVerified);
    s.error_code.clear();  // 0.6.0 kaydi
    s.error = "RegSetValueEx: Access is denied.";
    EF_CHECK(classify_restore_failure(s, false) == RestoreFailReason::Denied);
    s.error = std::string(kExternalChange);
    EF_CHECK(classify_restore_failure(s, true) == RestoreFailReason::External);
}

// (8) Kurtarma panel metni: ad + neden, tekrar eden satir tek, "n ayar daha".
EF_TEST(recovery_items_text_lists_names_reasons_and_more_count) {
    using VM = ef::ui::PerfView::RecoveryItemVM;
    const auto text = [](std::string_view k) -> std::wstring {
        if (k == "perf.recovery_items") return L"Not restored: {items}";
        if (k == "perf.recovery_items_more") return L"{n} more";
        if (k == "a") return L"A";
        if (k == "b") return L"B";
        if (k == "c") return L"C";
        if (k == "d") return L"D";
        if (k == "r") return L"denied";
        return {};
    };
    EF_CHECK(ef::ui::recovery_items_text({}, text).empty());
    EF_CHECK(ef::ui::recovery_items_text({VM{"a", "r", ""}}, text) == L"Not restored: A (denied)");
    EF_CHECK(ef::ui::recovery_items_text({VM{"a", "r", "svc"}}, text) == L"Not restored: A \x2014 svc (denied)");
    const std::vector<VM> many{{"a", "r", ""}, {"b", "", ""}, {"c", "r", ""}, {"d", "r", ""}, {"zz", "r", ""}};
    EF_CHECK(ef::ui::recovery_items_text(many, text) == L"Not restored: A (denied), B, C (denied), 2 more");
    EF_CHECK(ef::ui::recovery_items_text({VM{"zz", "r", ""}}, text).empty());  // adsiz oge yazilmaz
    FailedRestoreItem x;
    x.op = OperationId::PowerSettingSet;
    x.target = "power:aspm:ac";
    x.reason = RestoreFailReason::Denied;
    const auto vm = ef::ui::recovery_items_vm({x, x});
    EF_CHECK(vm.size() == 1 && vm[0].name_key == "restore.item.power_aspm" && vm[0].reason_key == "restore.reason.denied");
}

// B9 (R-B1 notu): USB secici askiya alma kendi dizesini tasir (genel history.op.* degil).
EF_TEST(restore_item_name_key_usb_selective_suspend_has_its_own_string) {
    FailedRestoreItem x;
    x.op = OperationId::PowerSettingSet;
    x.target = "power:usbss:ac";
    EF_CHECK(restore_item_name_key(x) == "restore.item.power_usb");
}
