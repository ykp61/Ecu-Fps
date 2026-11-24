// REL-04: guncelleme ve recovery uyumu (saf; sahte dosya sistemi, gercek EXE/journal/ProgramData yok).
//  1) Bekleyen (etkin) journal kaydi EXE degisiminde (run_swap / recover_swap, saglikli ve geri donus) silinmez;
//     degisim yalniz EXE yanindaki .new/.old/.updated/.failed yollarina dokunur; yeni EXE kaydi okuyup geri alabilir.
//  2) Eski EXE yeni journal semasini uygulamaz: kayit bozuk sayilip silinmez, yeni degisiklik baslamaz, dosya degismez.
#include <map>
#include <set>
#include <string>

#include "core/catalog/envelope.h"
#include "core/recovery/service.h"
#include "core/update/update_check.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {

const std::string kTxId = "abcdef0123456789abcdef0123456789";
const std::string kExe = "C:/PF/Ecu Fps/EcuFps.exe";
const std::string kJournalDir = "C:/PD/EcuFps/journal/";

JournalTx pending_tx(const std::string& id) {
    JournalTx tx;
    tx.id = id;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    tx.created_unix_ms = 1;
    JournalStep s;
    s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, "x", "performance", false, false};
    tx.steps.push_back(s);
    return tx;
}

// EXE klasoru + journal klasoru ayni "disk"te; her dokunulan yol kaydedilir.
struct TrackingDisk {
    std::map<std::string, std::string> files;
    std::set<std::string> touched;
    catalog::SwapFs fs() {
        catalog::SwapFs f;
        f.move = [this](const std::string& from, const std::string& to) {
            touched.insert(from);
            touched.insert(to);
            auto it = files.find(from);
            if (it == files.end()) return false;
            files[to] = it->second;
            files.erase(from);
            return true;
        };
        f.exists = [this](const std::string& p) {
            touched.insert(p);
            return files.count(p) > 0;
        };
        f.remove = [this](const std::string& p) {
            touched.insert(p);
            files.erase(p);
            return true;
        };
        return f;
    }
};

// Uygulanmis (Performans acik) islemi olan journal: dosya adi -> bayt.
std::pair<std::string, std::string> applied_journal_file(FakeAdapter& f) {
    MemoryJournalFs mem;
    JournalStore store(mem, "this-pc");
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    TransactionEngine eng(store, reg);
    EF_CHECK(eng.apply(pending_tx(kTxId)).ok());
    EF_CHECK(mem.files.size() == 1);
    return mem.files.front();
}

}  // namespace

EF_TEST(update_swap_paths_stay_beside_exe) {
    const auto p = update::update_swap_paths(kExe);
    for (const auto* s : {&p.staged, &p.backup, &p.marker}) {
        EF_CHECK(s->rfind(kExe, 0) == 0);                      // EXE ile ayni klasor ve onek
        EF_CHECK(s->find("journal") == std::string::npos);
    }
}

EF_TEST(update_pending_journal_survives_swap_commit_and_rollback) {
    for (const bool healthy : {true, false}) {
        FakeAdapter f;
        f.values = {{"x", "orig"}};
        const auto jf = applied_journal_file(f);
        EF_CHECK(f.values["x"] == "performance");

        TrackingDisk disk;
        const auto paths = update::update_swap_paths(kExe);
        disk.files[kExe] = "OLD-EXE";
        disk.files[paths.staged] = "NEW-EXE";
        const std::string journal_path = kJournalDir + jf.first;
        disk.files[journal_path] = jf.second;

        EF_CHECK(catalog::run_swap(disk.fs(), paths) == catalog::SwapStep::Replaced);
        EF_CHECK(catalog::recover_swap(disk.fs(), paths, healthy));
        EF_CHECK(disk.files[kExe] == (healthy ? "NEW-EXE" : "OLD-EXE"));

        // Journal bayt bayt ayni; degisim journal klasorune hic dokunmadi.
        EF_CHECK(disk.files.count(journal_path) == 1 && disk.files[journal_path] == jf.second);
        for (const auto& t : disk.touched) {
            EF_CHECK(t.find(kJournalDir) == std::string::npos);
            EF_CHECK(t == kExe || t == paths.staged || t == paths.backup || t == paths.marker || t == paths.staged + ".failed");
        }

        // Degisimden sonra acilan EXE (yeni ya da geri donulen eski): kayit etkin, yazma serbest, Kapali geri alir.
        MemoryJournalFs after;
        after.files.emplace_back(jf.first, disk.files[journal_path]);
        JournalStore store(after, "this-pc");
        AdapterRegistry reg;
        reg.add(OperationId::GameModeSet, &f);
        TransactionEngine eng(store, reg);
        const auto s = run_startup_recovery({{"user", &store, &eng}});
        EF_CHECK(!s.writes_blocked);
        EF_CHECK(s.active == 1);
        EF_CHECK(s.scopes.at("user").corrupt.empty());
        EF_CHECK(f.values["x"] == "performance");  // acilista sessizce geri alinmaz
        const auto r = restore_all_active({{"user", &store, &eng}});
        EF_CHECK(r.restored == 1 && r.failed.empty());
        EF_CHECK(f.values["x"] == "orig");
    }
}

EF_TEST(update_old_exe_refuses_newer_journal_schema) {
    FakeAdapter f;
    f.values = {{"x", "orig"}};
    auto jf = applied_journal_file(f);
    // Daha yeni EXE'nin yazdigi kayit: sema bu EXE'nin bildiginden buyuk.
    const std::string cur = "\"schema_version\":" + std::to_string(kJournalSchema);
    const auto pos = jf.second.find(cur);
    EF_CHECK(pos != std::string::npos);
    jf.second.replace(pos, cur.size(), "\"schema_version\":" + std::to_string(kJournalSchema + 1));
    EF_CHECK(deserialize(jf.second).code() == ErrorCode::SchemaTooNew);

    MemoryJournalFs old_view;
    old_view.files.emplace_back(jf.first, jf.second);
    JournalStore store(old_view, "this-pc");
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &f);
    TransactionEngine eng(store, reg);

    const auto l = store.scan();
    EF_CHECK(l.newer.size() == 1 && l.corrupt.empty() && l.transactions.empty());  // bozuk sayilip atilmaz

    const auto s = run_startup_recovery({{"user", &store, &eng}});
    EF_CHECK(s.writes_blocked && s.needs_user);
    EF_CHECK(s.restored == 0);

    const int restores_before = f.restores, applies_before = f.applies;
    EF_CHECK(restore_all_active({{"user", &store, &eng}}).restored == 0);  // yanlis yorumla geri alma yok
    auto next = pending_tx("0123456789abcdef0123456789abcdef");
    EF_CHECK(!eng.apply(next).ok());                                     // yeni degisiklik baslamaz
    EF_CHECK(f.restores == restores_before && f.applies == applies_before);
    EF_CHECK(f.values["x"] == "performance");
    EF_CHECK(old_view.files.size() == 1 && old_view.files.front().second == jf.second);  // dosya degismedi
}
