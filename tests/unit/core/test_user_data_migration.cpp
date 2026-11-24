// R-F1: marka degisikligi kullanici verisi gocu. Yalniz bellek ici dosya sistemi; gercek %LOCALAPPDATA% / kayit
// defterine dokunulmaz. Eski ad yalniz src/core/legacy ve bu testte gecer.
#include <map>
#include <string>

#include "core/legacy/legacy_names.h"
#include "core/legacy/user_data_migration.h"
#include "core/perf/controller.h"
#include "core/perf/journal_backend.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"
#include "core/recovery/sha256.h"
#include "core/settings/autostart.h"
#include "ef_test.h"

using namespace ef::core::legacy;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
// Turkce karakterli profil yolu (UTF-8).
const std::string kRoot = "C:\\Users\\\xC5\x9E\xC3\xBCkr\xC3\xBC \xC3\x96\xC4\x9F""e\\AppData\\Local";
const std::string kOld = kRoot + "\\iTunexPro";
const std::string kNew = kRoot + "\\EcuFps";
const std::string kStaging = kNew + ".migrating";
const std::string kMachine = "m-test-1";

MigrationEnv env() {
    MigrationEnv e;
    e.now_unix_ms = 1795000000000;
    e.app_version = "0.9.6";
    e.to_name = "EcuFps";
    e.from_name = "iTunexPro";
    e.machine_id = kMachine;
    e.sleep = [](int) {};
    return e;
}

MigrationPaths paths() { return {kOld, kNew}; }

JournalTx tx3(const std::string& id) {
    JournalTx tx;
    tx.id = id;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    for (const char* t : {"a", "b", "c"}) {
        JournalStep s;
        s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, t, std::string("new-") + t, false, false};
        tx.steps.push_back(s);
    }
    return tx;
}

// Eski surumun gercekci verisi: proje kendi JournalStore'u ile yazilmis Active (3 adim) + Restored islem + bozuk dosya.
struct LegacyFixture {
    MemoryJournalFs jfs;
    JournalStore store{jfs, kMachine};
    FakeAdapter fake;  // sistemin "gercek" ayar degerleri
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    std::string active_id = "0123456789abcdef0123456789abcdef";
    std::string restored_id = "fedcba9876543210fedcba9876543210";
    LegacyFixture() {
        reg.add(OperationId::GameModeSet, &fake);
        fake.values = {{"a", "old-a"}, {"b", "old-b"}};  // "c" onceden yok
        auto r1 = engine.apply(tx3(restored_id));
        (void)engine.rollback(r1.value());
        fake.values = {{"a", "old-a"}, {"b", "old-b"}};
        auto r2 = engine.apply(tx3(active_id));
        EF_CHECK(r2.ok() && r2.value().state == TxState::Active);
    }
    void install(MemoryMigrationFs& fs) {
        for (const auto& [n, b] : jfs.files) fs.put(join_path(kOld, "journal/" + n), b);
        fs.put(join_path(kOld, "journal/tx-00000000000000000000000000000bad.json"), "{bozuk");
        fs.put(join_path(kOld, "preferences.json"), "{\"schema_version\":1,\"ui_language\":\"tr-TR\"}");
        fs.put(join_path(kOld, "history.json"), "{\"schema_version\":1,\"entries\":[]}");
        fs.put(join_path(kOld, "logs/restore.log"), "2026-11-01 restore result=restored\n");
        fs.put(join_path(kOld, "crash/dump-1.dmp"), std::string(3000, '\x7f'));
        fs.put(join_path(kOld, "games-consent.json"), "{}");
    }
};

std::map<std::string, std::string> snapshot_tree(MemoryMigrationFs& fs, const std::string& root) {
    std::map<std::string, std::string> out;
    for (const auto& [p, f] : fs.files)
        if (p.rfind(root + "\\", 0) == 0) out[p] = sha256_hex(f.bytes) + (f.read_only ? ":ro" : "");
    return out;
}

bool touched_old(const MemoryMigrationFs& fs) {
    for (const auto& m : fs.mutations) {
        const auto sp = m.find(' ');
        const auto p = m.substr(sp + 1);
        if (p == kOld || p.rfind(kOld + "\\", 0) == 0) return true;
    }
    return false;
}

bool has_prefix(const MemoryMigrationFs& fs, const std::string& root) {
    for (const auto& [p, f] : fs.files)
        if (p == root || p.rfind(root + "\\", 0) == 0) return true;
    return fs.dirs.count(root) != 0;
}

// Yeni klasordeki journal dosyalari yeni uygulamanin JournalStore'una yuklenir.
void load_new_journal(MemoryMigrationFs& fs, MemoryJournalFs& out) {
    const std::string jd = join_path(kNew, "journal") + "\\";
    for (const auto& [p, f] : fs.files)
        if (p.rfind(jd, 0) == 0 && p.find('\\', jd.size()) == std::string::npos) out.files.emplace_back(p.substr(jd.size()), f.bytes);
}
}  // namespace

// ---- Anahtar test (once bos uygulamaya karsi kirmizi goruldu) ----
EF_TEST(migration_copies_realistic_journal_and_kapali_restores_exact_previous_values) {
    MemoryMigrationFs fs;
    LegacyFixture fx;
    fx.install(fs);
    const auto before = snapshot_tree(fs, kOld);

    const auto r = migrate_user_data(fs, paths(), env());
    EF_CHECK(r.outcome == MigrationOutcome::Migrated);
    EF_CHECK(r.marker_written);
    EF_CHECK(!r.legacy_pending());
    EF_CHECK(r.legacy.unresolved == 1 && r.legacy.corrupt == 1 && r.legacy.transactions == 2);
    EF_CHECK(r.files == before.size());
    // Eski klasor bayt bayt ayni ve hic degistirici islem gormedi; hazirlik klasoru kalmadi.
    EF_CHECK(snapshot_tree(fs, kOld) == before);
    EF_CHECK(!touched_old(fs));
    EF_CHECK(!has_prefix(fs, kStaging));
    // Journal dosyalari bayt bayt ayni (sema/surum alanlari yeniden yazilmadi).
    for (const auto& [n, b] : fx.jfs.files) EF_CHECK(fs.files.at(join_path(kNew, "journal/" + n)).bytes == b);
    EF_CHECK(fs.files.count(join_path(kNew, std::string(kMarkerFile))) == 1);
    EF_CHECK(fs.files.count(join_path(kNew, std::string(kStagingSentinel))) == 0);

    // Yeni uygulama ayni Active islemi okur; Kapali onceki degerleri birebir geri yukler.
    MemoryJournalFs njfs;
    load_new_journal(fs, njfs);
    JournalStore nstore(njfs, kMachine);
    const auto l = nstore.scan();
    EF_CHECK(l.corrupt.size() == 1);
    const JournalTx* active = nullptr;
    for (const auto& t : l.transactions)
        if (t.state == TxState::Active) active = &t;
    EF_CHECK(active != nullptr);
    if (!active) return;
    EF_CHECK(active->id == fx.active_id && active->steps.size() == 3);
    EF_CHECK(fx.fake.values["a"] == "new-a" && fx.fake.values["c"] == "new-c");
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &fx.fake);
    TransactionEngine eng(nstore, reg);
    const auto back = eng.rollback(*active);
    EF_CHECK(back.ok() && back.value().state == TxState::Restored);
    EF_CHECK(fx.fake.values["a"] == "old-a");
    EF_CHECK(fx.fake.values["b"] == "old-b");
    EF_CHECK(fx.fake.values.count("c") == 0);  // onceden yoktu: varsayilan uydurulmadi
}

EF_TEST(migration_fresh_install_writes_no_legacy_marker_only) {
    MemoryMigrationFs fs;
    const auto r = migrate_user_data(fs, paths(), env());
    EF_CHECK(r.outcome == MigrationOutcome::NoLegacyData);
    EF_CHECK(r.marker_written);
    EF_CHECK(fs.files.count(join_path(kNew, std::string(kMarkerFile))) == 1);
    EF_CHECK(!has_prefix(fs, kOld));  // eski klasor olusturulmadi
    EF_CHECK(!has_prefix(fs, kStaging));
    // Bos eski klasor de "veri yok" sayilir.
    MemoryMigrationFs fs2;
    fs2.make_dirs(join_path(kOld, "logs"));
    fs2.mutations.clear();
    EF_CHECK(migrate_user_data(fs2, paths(), env()).outcome == MigrationOutcome::NoLegacyData);
    EF_CHECK(!touched_old(fs2));
}

EF_TEST(migration_second_run_is_idempotent_and_does_nothing) {
    MemoryMigrationFs fs;
    LegacyFixture fx;
    fx.install(fs);
    EF_CHECK(migrate_user_data(fs, paths(), env()).outcome == MigrationOutcome::Migrated);
    const auto new_before = snapshot_tree(fs, kNew);
    fs.mutations.clear();
    auto e = env();
    e.now_unix_ms += 5000;
    const auto r = migrate_user_data(fs, paths(), e);
    EF_CHECK(r.outcome == MigrationOutcome::AlreadyMigrated);
    EF_CHECK(fs.mutations.empty());
    EF_CHECK(snapshot_tree(fs, kNew) == new_before);
}

EF_TEST(migration_marker_content_is_stable) {
    MemoryMigrationFs a, b;
    a.put(join_path(kOld, "preferences.json"), "{}");
    a.put(join_path(kOld, "journal/tx-0123456789abcdef0123456789abcdef.json"), "x");
    // Ayni icerik, farkli ekleme sirasi -> ayni isaret.
    b.put(join_path(kOld, "journal/tx-0123456789abcdef0123456789abcdef.json"), "x");
    b.put(join_path(kOld, "preferences.json"), "{}");
    const auto ra = migrate_user_data(a, paths(), env());
    const auto rb = migrate_user_data(b, paths(), env());
    EF_CHECK(ra.outcome == MigrationOutcome::Migrated && rb.outcome == MigrationOutcome::Migrated);
    EF_CHECK(ra.marker == rb.marker);
    EF_CHECK(a.files.at(join_path(kNew, std::string(kMarkerFile))).bytes == ra.marker);
    const std::string list = "journal/tx-0123456789abcdef0123456789abcdef.json\t1\t" + sha256_hex("x") + "\n" +
                             "preferences.json\t2\t" + sha256_hex("{}") + "\n";
    const std::string expect = std::string("{\"schema_version\":1,\"result\":\"migrated\",\"from\":\"iTunexPro\",\"to\":\"EcuFps\",") +
                               "\"files\":2,\"bytes\":3,\"files_sha256\":\"" + sha256_hex(list) +
                               "\",\"created_unix_ms\":1795000000000,\"app_version\":\"0.9.6\"}";
    EF_CHECK(ra.marker == expect);
    // Marker yol / kullanici adi icermez.
    EF_CHECK(ra.marker.find("Users") == std::string::npos);
}

EF_TEST(migration_corrupt_copy_rolls_back_and_reports_recovery_needed) {
    MemoryMigrationFs fs;
    LegacyFixture fx;
    fx.install(fs);
    const auto before = snapshot_tree(fs, kOld);
    fs.corrupt_copy_of = "preferences.json";
    const auto r = migrate_user_data(fs, paths(), env());
    EF_CHECK(r.outcome == MigrationOutcome::Failed);
    EF_CHECK(r.stage == "verify");
    EF_CHECK(r.legacy_pending());  // eski journal'da Active islem var: temiz "Kapali" gosterilmez
    EF_CHECK(!has_prefix(fs, kStaging));
    EF_CHECK(snapshot_tree(fs, kOld) == before);
    EF_CHECK(!touched_old(fs));
    EF_CHECK(fs.files.count(join_path(kNew, std::string(kMarkerFile))) == 0);
    EF_CHECK(fs.files.count(join_path(kNew, std::string(kFailedFile))) == 1);
    EF_CHECK(migration_log_line(r).find("verify") != std::string::npos);
    // Basarisiz oturumda uygulama yeni klasore varsayilan tercih yazdi. Sonraki acilis (hata giderildi): hata kaydi
    // oldugu icin yeniden dener, uzerine yazmadan birlestirir; journal aktarilir, yeni tercih korunur.
    fs.put(join_path(kNew, "preferences.json"), "{\"varsayilan\":1}");
    fs.corrupt_copy_of.clear();
    const auto r2 = migrate_user_data(fs, paths(), env());
    EF_CHECK(r2.outcome == MigrationOutcome::Migrated);
    EF_CHECK(r2.merged);
    EF_CHECK(r2.skipped_existing == 1);
    EF_CHECK(!r2.legacy_pending());
    EF_CHECK(fs.files.count(join_path(kNew, std::string(kFailedFile))) == 0);
    EF_CHECK(fs.files.at(join_path(kNew, "preferences.json")).bytes == "{\"varsayilan\":1}");
    for (const auto& [n, b] : fx.jfs.files) EF_CHECK(fs.files.at(join_path(kNew, "journal/" + n)).bytes == b);
    EF_CHECK(fs.files.at(join_path(kNew, "history.json")).bytes == fs.files.at(join_path(kOld, "history.json")).bytes);
    EF_CHECK(!has_prefix(fs, kStaging));
    EF_CHECK(snapshot_tree(fs, kOld) == before);
    // Ucuncu acilis: isaret var, hicbir sey yapilmaz.
    EF_CHECK(migrate_user_data(fs, paths(), env()).outcome == MigrationOutcome::AlreadyMigrated);
}

EF_TEST(migration_rename_failure_rolls_back_staging) {
    MemoryMigrationFs fs;
    LegacyFixture fx;
    fx.install(fs);
    const auto before = snapshot_tree(fs, kOld);
    fs.fail_rename = true;
    const auto r = migrate_user_data(fs, paths(), env());
    EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "commit");
    EF_CHECK(r.legacy_pending());
    EF_CHECK(!has_prefix(fs, kStaging));
    EF_CHECK(fs.files.count(join_path(kNew, "preferences.json")) == 0);
    EF_CHECK(snapshot_tree(fs, kOld) == before);
}

EF_TEST(migration_disk_full_rolls_back) {
    {  // yazim sirasinda disk doldu
        MemoryMigrationFs fs;
        LegacyFixture fx;
        fx.install(fs);
        const auto before = snapshot_tree(fs, kOld);
        fs.capacity = fs.written + 1500;  // kurulum yazimlari sayildi; goc 1500 bayttan fazlasini yazamaz
        const auto r = migrate_user_data(fs, paths(), env());
        EF_CHECK(r.outcome == MigrationOutcome::Failed);
        EF_CHECK(r.error.find("disk_full") != std::string::npos);
        EF_CHECK(!has_prefix(fs, kStaging));
        EF_CHECK(snapshot_tree(fs, kOld) == before);
        EF_CHECK(r.legacy_pending());
    }
    {  // on kontrol: bos alan yetmez -> hic kopya baslamaz
        MemoryMigrationFs fs;
        LegacyFixture fx;
        fx.install(fs);
        fs.free = 100;
        fs.mutations.clear();
        const auto r = migrate_user_data(fs, paths(), env());
        EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "preflight");
        for (const auto& m : fs.mutations) EF_CHECK(m.find(kStaging) == std::string::npos);
    }
}

EF_TEST(migration_leaves_populated_new_dir_untouched) {
    MemoryMigrationFs fs;
    LegacyFixture fx;
    fx.install(fs);
    fs.put(join_path(kNew, "preferences.json"), "{\"yeni\":1}");
    fs.put(join_path(kNew, "journal/tx-11111111111111111111111111111111.json"), "yeni");
    const auto new_before = snapshot_tree(fs, kNew);
    fs.mutations.clear();
    const auto r = migrate_user_data(fs, paths(), env());
    EF_CHECK(r.outcome == MigrationOutcome::NewDirInUse);
    EF_CHECK(fs.mutations.empty());
    EF_CHECK(snapshot_tree(fs, kNew) == new_before);
    EF_CHECK(r.legacy_pending());  // eski Active islem aktarilamadi: durust bildirim
    // Eski journal'da bekleyen yoksa bildirim de yok.
    MemoryMigrationFs fs2;
    fs2.put(join_path(kOld, "preferences.json"), "{}");
    fs2.put(join_path(kNew, "preferences.json"), "{}");
    const auto r2 = migrate_user_data(fs2, paths(), env());
    EF_CHECK(r2.outcome == MigrationOutcome::NewDirInUse && !r2.legacy_pending());
}

EF_TEST(migration_odd_names_read_only_and_locked_files) {
    MemoryMigrationFs fs;
    const std::string long_name = "logs/" + std::string(200, 'x') + " \xC3\xA7\xC4\xB1kt\xC4\xB1 (1).log";
    fs.put(join_path(kOld, long_name), "uzun");
    fs.put(join_path(kOld, "preferences.json"), "{}", true);  // salt okunur
    fs.put(join_path(kOld, "history.json"), "{\"h\":1}");
    fs.busy["history.json"] = 2;  // iki kez kilitli, sonra acilir
    int sleeps = 0;
    auto e = env();
    e.sleep = [&](int) { ++sleeps; };
    const auto r = migrate_user_data(fs, paths(), e);
    EF_CHECK(r.outcome == MigrationOutcome::Migrated);
    EF_CHECK(sleeps >= 2);
    EF_CHECK(fs.files.at(join_path(kNew, long_name)).bytes == "uzun");
    EF_CHECK(fs.files.at(join_path(kOld, "preferences.json")).read_only);  // kaynak ozniteligi korunur
    EF_CHECK(!fs.files.at(join_path(kNew, "preferences.json")).read_only);

    // Surekli kilitli dosya: sinirli deneme, sonra temiz hata (asilma yok, hazirlik silindi).
    MemoryMigrationFs fs2;
    fs2.put(join_path(kOld, "preferences.json"), "{}");
    fs2.put(join_path(kOld, "journal/tx-0123456789abcdef0123456789abcdef.json"), "x");
    fs2.busy["tx-0123456789abcdef0123456789abcdef.json"] = -1;
    int sleeps2 = 0;
    auto e2 = env();
    e2.sleep = [&](int) { ++sleeps2; };
    const auto r2 = migrate_user_data(fs2, paths(), e2);
    EF_CHECK(r2.outcome == MigrationOutcome::Failed && r2.stage == "copy");
    EF_CHECK(r2.error.find("busy") != std::string::npos);
    EF_CHECK(sleeps2 == e2.busy_retries);
    EF_CHECK(!has_prefix(fs2, kStaging));
    EF_CHECK(r2.legacy_pending());  // okunamayan eski journal da "bekleyen" sayilir (temiz Kapali yok)
}

EF_TEST(migration_staging_leftovers_and_guards) {
    {  // onceki cokmeden kalan bizim hazirlik klasorumuz silinir, goc tamamlanir
        MemoryMigrationFs fs;
        fs.put(join_path(kOld, "preferences.json"), "{}");
        fs.put(join_path(kStaging, std::string(kStagingSentinel)), "{}");
        fs.put(join_path(kStaging, "preferences.json"), "yarim");
        const auto r = migrate_user_data(fs, paths(), env());
        EF_CHECK(r.outcome == MigrationOutcome::Migrated);
        EF_CHECK(fs.files.at(join_path(kNew, "preferences.json")).bytes == "{}");
        EF_CHECK(!has_prefix(fs, kStaging));
    }
    {  // bizim olmayan (isaretsiz) hazirlik klasoru: dokunulmaz, goc yapilmaz
        MemoryMigrationFs fs;
        fs.put(join_path(kOld, "preferences.json"), "{}");
        fs.put(join_path(kStaging, "baska.txt"), "kullanicinin");
        const auto r = migrate_user_data(fs, paths(), env());
        EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "staging");
        EF_CHECK(fs.files.at(join_path(kStaging, "baska.txt")).bytes == "kullanicinin");
    }
    {  // eski surum acik: kopya denenmez
        MemoryMigrationFs fs;
        LegacyFixture fx;
        fx.install(fs);
        auto e = env();
        e.old_app_running = true;
        const auto r = migrate_user_data(fs, paths(), e);
        EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "old_app_running");
        EF_CHECK(r.legacy_pending());
        EF_CHECK(!has_prefix(fs, kStaging));
    }
    {  // eski klasor junction iceriyor: izlenmez, goc reddedilir
        MemoryMigrationFs fs;
        fs.put(join_path(kOld, "preferences.json"), "{}");
        fs.dirs.insert(join_path(kOld, "baglanti"));
        fs.reparse.insert(join_path(kOld, "baglanti"));
        const auto r = migrate_user_data(fs, paths(), env());
        EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "scan");
    }
}

EF_TEST(migration_skips_download_caches_and_retries_changing_source) {
    MemoryMigrationFs fs;
    fs.put(join_path(kOld, "preferences.json"), "{}");
    fs.put(join_path(kOld, "drivers/0123456789abcdef.exe"), std::string(5000, 'd'));
    fs.put(join_path(kOld, "setup/eski-kurulum.exe"), std::string(5000, 's'));
    fs.put(join_path(kOld, "history.json"), "v1");
    fs.mutate_source_during_copy["history.json"] = "v2";  // eski surum ayni anda yaziyor (bir kez)
    const auto r = migrate_user_data(fs, paths(), env());
    EF_CHECK(r.outcome == MigrationOutcome::Migrated);
    EF_CHECK(r.files == 2);
    EF_CHECK(fs.files.count(join_path(kNew, "drivers/0123456789abcdef.exe")) == 0);
    EF_CHECK(fs.files.count(join_path(kNew, "setup/eski-kurulum.exe")) == 0);
    EF_CHECK(fs.files.at(join_path(kNew, "history.json")).bytes == fs.files.at(join_path(kOld, "history.json")).bytes);
    EF_CHECK(excluded_from_migration("drivers/x.exe") && excluded_from_migration("Setup/y.exe"));
    EF_CHECK(!excluded_from_migration("journal/tx-1.json") && !excluded_from_migration("driverlog.txt"));
}

// ---- Eski adli Windows ile baslatma degeri (journal'da degil; ADR-030 tercih + Run degeri) ----
namespace {
class RunKey final : public ef::core::settings::RunKeyStore {
public:
    Result<std::optional<std::wstring>> read(std::wstring_view name) override {
        const auto it = values.find(std::wstring(name));
        if (it == values.end()) return std::optional<std::wstring>{};
        return std::optional<std::wstring>{it->second};
    }
    Status write(std::wstring_view name, std::wstring_view command) override {
        values[std::wstring(name)] = std::wstring(command);
        return ok_status();
    }
    Status remove(std::wstring_view name) override {
        values.erase(std::wstring(name));
        return ok_status();
    }
    std::map<std::wstring, std::wstring> values;
};
}  // namespace

EF_TEST(autostart_old_brand_run_value_is_recognised_and_repointed) {
    using namespace ef::core::settings;
    const std::wstring cmd = autostart_command(L"C:\\Program Files\\Ecu Fps\\EcuFps.exe");
    {  // gocle gelen tercih: acik + baslatildi; yeni deger yok, eski adli deger var -> disaridan silindi SAYILMAZ
        RunKey k;
        k.values[std::wstring(kOldAutostartValueName)] = L"\"C:\\Program Files\\iTunex Pro\\iTunexPro.exe\" --tray";
        Preferences p;
        p.autostart_initialized = true;
        p.start_with_windows = true;
        const auto r = autostart_on_launch(k, p, cmd);
        EF_CHECK(r.state.on);
        EF_CHECK(p.start_with_windows);
        EF_CHECK(r.branch == AutostartBranch::Rewritten);
        EF_CHECK(k.values.count(std::wstring(kAutostartValueName)) == 1 && k.values.at(std::wstring(kAutostartValueName)) == cmd);
        EF_CHECK(k.values.count(std::wstring(kOldAutostartValueName)) == 0);
    }
    {  // tercih kapali: eski adli deger de silinir, yenisi yazilmaz
        RunKey k;
        k.values[std::wstring(kOldAutostartLegacyValueName)] = L"\"x.exe\" --tray";
        Preferences p;
        p.autostart_initialized = true;
        p.start_with_windows = false;
        (void)autostart_on_launch(k, p, cmd);
        EF_CHECK(k.values.empty());
    }
    {  // kaldirma akisi eski adlari da siler
        RunKey k;
        k.values[std::wstring(kOldAutostartValueName)] = L"a";
        k.values[std::wstring(kOldAutostartLegacyValueName)] = L"b";
        k.values[std::wstring(kAutostartValueName)] = L"c";
        EF_CHECK(autostart_remove_all(k).ok());
        EF_CHECK(k.values.empty());
    }
}

EF_TEST(sha256_stream_matches_one_shot) {
    std::string data;
    for (int i = 0; i < 1000; ++i) data += static_cast<char>(i * 7);
    for (std::size_t cut : {0u, 1u, 55u, 56u, 63u, 64u, 65u, 500u, 1000u}) {
        Sha256Stream s;
        s.update(std::string_view(data).substr(0, cut));
        s.update(std::string_view(data).substr(cut));
        EF_CHECK(s.hex_final() == sha256_hex(data));
    }
    Sha256Stream e;
    EF_CHECK(e.hex_final() == sha256_hex(""));
}

// Gocun aktaramadigi etkin eski kayit: anahtar temiz "Kapali" gostermez; kurtarma durumu + durust bildirim, geri alma
// denemesi de (yeni journal bos olsa bile) "Kapali"ya donmez.
EF_TEST(perf_switch_reports_legacy_unmigrated_instead_of_clean_off) {
    namespace perf = ef::core::perf;
    MemoryJournalFs jfs;
    JournalStore store(jfs, kMachine);
    FakeAdapter fake;
    AdapterRegistry reg;
    TransactionEngine eng(store, reg);
    bool pending = true;
    perf::JournalBackendDeps d;
    d.scope = {"user", &store, &eng};
    d.build_plan = [] { return Result<perf::Plan>(perf::Plan{}); };
    d.legacy_pending = [&] { return pending; };
    perf::JournalBackend backend(d);
    perf::PerfController c(backend, [](std::function<void()> w) { w(); }, [](std::function<void()> f) { f(); });
    c.init();
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded);
    EF_CHECK(c.notice() == perf::Notice::LegacyUnmigrated);
    EF_CHECK(!c.switch_on());
    c.toggle();     // kurtarma -> geri alma onayi
    c.answer(true); // bos yeni journal: "yapacak is yok" ama eski kayit hala bekliyor
    EF_CHECK(c.phase() == perf::Phase::RecoveryNeeded);
    EF_CHECK(c.notice() == perf::Notice::LegacyUnmigrated);
    // Goc tamamlaninca (sonraki acilis) normal durum.
    pending = false;
    c.init();
    EF_CHECK(c.phase() == perf::Phase::Off && c.notice() == perf::Notice::None);
}

EF_TEST(migration_pending_includes_unmigrated_machine_journal) {
    MigrationReport r;
    r.outcome = MigrationOutcome::Migrated;
    EF_CHECK(!r.legacy_pending());
    r.machine_legacy.scanned = true;
    r.machine_legacy.unresolved = 1;  // eski yonetici adimi hala etkin: yeni makine journal'inda yok
    EF_CHECK(r.legacy_pending());
    EF_CHECK(migration_log_line(r).find("machine_legacy_unresolved=1") != std::string::npos);
    // scan_legacy_journal makine duzeninde (journal + journal/fixes) okur, yazmaz.
    MemoryMigrationFs fs;
    LegacyFixture fx;
    const std::string pd = join_path(join_path("C:", "ProgramData"), "iTunexPro");
    for (const auto& [n, b] : fx.jfs.files) fs.put(join_path(pd, "journal/fixes/" + n), b);
    fs.mutations.clear();
    const auto st = scan_legacy_journal(fs, pd, kMachine);
    EF_CHECK(st.unresolved == 1 && st.transactions == 2);
    EF_CHECK(fs.mutations.empty());
}
