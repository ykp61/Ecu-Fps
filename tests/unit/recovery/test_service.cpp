#include "core/recovery/service.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
JournalTx tx1(const std::string& id, const char* target, std::int64_t t) {
    JournalTx tx;
    tx.id = id;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    tx.created_unix_ms = t;
    JournalStep s;
    s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, target, std::string("new-") + target, false, false};
    tx.steps.push_back(s);
    return tx;
}
const std::string kId1 = "11111111111111111111111111111111";
const std::string kId2 = "22222222222222222222222222222222";
}  // namespace

EF_TEST(restore_all_active_reverts_newest_first) {
    MemoryJournalFs fs;
    JournalStore store(fs, "this-pc");
    FakeAdapter f;
    f.values = {{"x", "orig"}};
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    TransactionEngine eng(store, reg);
    // Iki islem ayni hedefe: once 1 (orig->new-x), sonra 2 (new-x->new-x)
    EF_CHECK(eng.apply(tx1(kId1, "x", 1)).ok());
    auto t2 = tx1(kId2, "x", 2);
    t2.steps[0].op.new_value = "second";
    EF_CHECK(eng.apply(t2).ok());
    EF_CHECK(f.values["x"] == "second");
    const auto r = restore_all_active({{"user", &store, &eng}});
    EF_CHECK(r.restored == 2 && r.failed.empty());
    EF_CHECK(f.values["x"] == "orig");  // dogru sira: once 2, sonra 1
}

EF_TEST(moved_exe_does_not_apply_other_machine_journal) {
    MemoryJournalFs fs;
    FakeAdapter f;
    f.values = {{"x", "orig"}};
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    {
        JournalStore other(fs, "other-pc");
        TransactionEngine eng(other, reg);
        auto t = tx1(kId1, "x", 1);
        JournalTx prepared = t;
        // Diger makinede yarim kalmis islem
        EF_CHECK(other.begin(prepared).ok());
        prepared.state = TxState::Applying;
        EF_CHECK(other.persist(prepared).ok());
    }
    f.values["x"] = "this-pc-value";
    JournalStore mine(fs, "this-pc");
    TransactionEngine eng(mine, reg);
    const auto s = run_startup_recovery({{"user", &mine, &eng}});
    EF_CHECK(s.scopes.at("user").foreign.size() == 1);
    EF_CHECK(s.restored == 0);
    EF_CHECK(f.values["x"] == "this-pc-value");  // bu makineye hicbir sey uygulanmadi
    EF_CHECK(restore_all_active({{"user", &mine, &eng}}).restored == 0);
}

EF_TEST(unknown_newer_schema_blocks_writes) {
    MemoryJournalFs fs;
    JournalStore st(fs, "m");
    auto t = tx1(kId1, "x", 1);
    std::string bytes = serialize(t);
    bytes.replace(bytes.find("\"schema_version\":1"), 18, "\"schema_version\":7");
    fs.files.emplace_back(JournalStore::file_name(kId1), bytes);
    FakeAdapter f;
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    TransactionEngine eng(st, reg);
    const auto s = run_startup_recovery({{"machine", &st, &eng}});
    EF_CHECK(s.writes_blocked);
    EF_CHECK(s.needs_user);
    EF_CHECK(fs.files.size() == 1);  // dokunulmadi
}

EF_TEST(migrator_chains_and_refuses_future) {
    Migrator m;
    m.add(0, [](ef::core::json::Value& tx) {
        tx.set("target_sid", ef::core::json::Value::string("migrated"));
        return ok_status();
    });
    auto doc = ef::core::json::Value::object();
    auto r = m.migrate(doc, 0, 1);
    EF_CHECK(r.ok() && r.value() == 1);
    EF_CHECK(doc.find("target_sid")->as_string() == "migrated");
    EF_CHECK(m.migrate(doc, 1, 1).value() == 1);                         // zaten guncel
    EF_CHECK(m.migrate(doc, 3, 1).code() == ErrorCode::SchemaTooNew);    // gelecek surum
    Migrator empty;
    EF_CHECK(empty.migrate(doc, 0, 1).code() == ErrorCode::Unsupported); // gocu tanimsiz eski surum
}

// --recovery rolu: makine kilidi yoksa makine kapsamlarina dokunulmaz.
EF_TEST(recovery_role_skips_machine_scopes_without_lock) {
    MemoryJournalFs fu, fm, fmf, ff;
    JournalStore su(fu, "this-pc"), sm(fm, "this-pc"), smf(fmf, "this-pc"), sf(ff, "this-pc");
    const std::vector<JournalScope> all = {{"user", &su, nullptr}, {"machine", &sm, nullptr}, {"fixes", &sf, nullptr},
                                           {"machine_fixes", &smf, nullptr}};
    EF_CHECK(is_machine_scope("machine") && is_machine_scope("machine_fixes") && !is_machine_scope("fixes") && !is_machine_scope("user"));
    const auto no_lock = scopes_for_role(all, false);
    EF_CHECK(no_lock.size() == 2 && no_lock[0].name == "user" && no_lock[1].name == "fixes");
    EF_CHECK(scopes_for_role(all, true).size() == 4);
}

// Sonradan olusan journal dizini (broker ilk HAGS duzeltmesinde olusturur): kapsam sonradan acilir, acik olan korunur.
EF_TEST(lazy_scope_opens_when_directory_appears) {
    FakeAdapter f;
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    MemoryJournalFs* backing = nullptr;
    bool exists = false;
    int calls = 0;
    auto make = [&]() -> std::unique_ptr<JournalFs> {
        ++calls;
        if (!exists) return nullptr;
        auto fs = std::make_unique<MemoryJournalFs>();
        backing = fs.get();
        return fs;
    };
    LazyScope sc;
    EF_CHECK(!sc.ensure(make, "this-pc", reg) && !sc.store && !sc.engine);  // dizin yok: kapali, hata degil
    exists = true;
    EF_CHECK(sc.ensure(make, "this-pc", reg) && sc.store && sc.engine && backing);
    // Broker'in yazdigi kayit artik okunur (Islem Gecmisi yeniden baslatma beklemez)
    f.values = {{"x", "orig"}};
    EF_CHECK(sc.engine->apply(tx1(kId1, "x", 1)).ok());
    EF_CHECK(sc.store->scan().transactions.size() == 1);
    auto* before = sc.store.get();
    EF_CHECK(!sc.ensure(make, "this-pc", reg) && sc.store.get() == before && calls == 2);  // yeniden olusturulmaz
}
