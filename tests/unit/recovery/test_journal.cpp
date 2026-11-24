#include <string>

#include "core/recovery/journal.h"
#include "core/recovery/sha256.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
JournalTx make_tx(const std::string& id = "0123456789abcdef0123456789abcdef", std::int64_t t = 1000) {
    JournalTx tx;
    tx.id = id;
    tx.actor_sid = "S-1-5-21-1-2-3-500";
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    tx.created_unix_ms = t;
    JournalStep s;
    s.op = SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, "gamemode.auto", "1", false, false};
    s.snapshot = Snapshot{true, "0", "REG_DWORD", false, "26300.9711"};
    tx.steps.push_back(s);
    return tx;
}
std::string hexid(int i) {
    std::string s = std::to_string(i);
    return std::string(32 - s.size(), 'a') + s;
}
}  // namespace

EF_TEST(sha256_known_vectors) {
    EF_CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EF_CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EF_CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EF_CHECK(sha256_hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

EF_TEST(journal_round_trip_and_integrity) {
    const auto tx = make_tx();
    const auto bytes = serialize(tx);
    auto back = deserialize(bytes);
    EF_CHECK(back.ok());
    EF_CHECK(back.value().steps.size() == 1);
    EF_CHECK(back.value().steps[0].snapshot.old_value == "0");
    EF_CHECK(back.value().target_sid == tx.target_sid);
    // Tek bayt degisikligi -> butunluk hatasi
    std::string tampered = bytes;
    const auto pos = tampered.find("\"old_value\":\"0\"");
    tampered[pos + 13] = '1';
    EF_CHECK(!deserialize(tampered).ok());
    // Kesik dosya
    EF_CHECK(!deserialize(bytes.substr(0, bytes.size() / 2)).ok());
    // Yeni sema
    std::string newer = bytes;
    newer.replace(newer.find("\"schema_version\":1"), 18, "\"schema_version\":9");
    EF_CHECK(deserialize(newer).code() == ErrorCode::SchemaTooNew);
}

EF_TEST(journal_unwritable_blocks_mutation) {
    MemoryJournalFs fs;
    fs.fail_write_after = 0;  // ilk yazim bile basarisiz (ACL/disk dolu)
    JournalStore store(fs, "machine-a");
    auto tx = make_tx();
    int mutations = 0;
    const auto s = store.begin(tx);
    if (s.ok()) ++mutations;  // urun deseni: yalniz begin basariliysa uygula
    EF_CHECK(!s.ok());
    EF_CHECK(mutations == 0);
    EF_CHECK(fs.files.empty());
}

EF_TEST(journal_persist_revision_and_scan) {
    MemoryJournalFs fs;
    JournalStore store(fs, "machine-a");
    auto tx = make_tx();
    EF_CHECK(store.begin(tx).ok());
    EF_CHECK(tx.revision == 1);
    tx.state = TxState::Applying;
    tx.steps[0].state = StepState::Intent;
    EF_CHECK(store.persist(tx).ok());
    EF_CHECK(tx.revision == 2);
    auto loaded = store.load(tx.id);
    EF_CHECK(loaded.ok() && loaded.value().state == TxState::Applying && loaded.value().steps[0].state == StepState::Intent);
    // Basarisiz persist revision'i geri alir, eski kayit korunur
    fs.fail_write_after = fs.writes;
    tx.state = TxState::Applied;
    EF_CHECK(!store.persist(tx).ok());
    EF_CHECK(tx.revision == 2);
    EF_CHECK(store.load(tx.id).value().state == TxState::Applying);
}

EF_TEST(journal_foreign_corrupt_newer_are_separated) {
    MemoryJournalFs fs;
    JournalStore a(fs, "machine-a");
    JournalStore b(fs, "machine-b");
    auto t1 = make_tx(hexid(1));
    EF_CHECK(a.begin(t1).ok());
    auto t2 = make_tx(hexid(2));
    EF_CHECK(b.begin(t2).ok());
    fs.files.emplace_back("tx-" + hexid(3) + ".json", "{garbage");
    const auto l = a.scan();
    EF_CHECK(l.transactions.size() == 1);
    EF_CHECK(l.foreign.size() == 1);   // baska makine: uygulanmaz
    EF_CHECK(l.corrupt.size() == 1);   // bozuk: silinmez, basari sayilmaz
    EF_CHECK(fs.files.size() == 3);
    EF_CHECK(a.load(t2.id).code() == ErrorCode::Denied);
}

// CORE-03: bilinmeyen yeni sema varken yeni degisiklik baslamaz (broker disi yollar da ayni sozlesmeye uyar).
EF_TEST(journal_newer_schema_blocks_new_changes) {
    MemoryJournalFs fs;
    JournalStore store(fs, "machine-a");
    fs.files.emplace_back(JournalStore::file_name(hexid(7)), R"({"schema_version":2,"sha256":"00","tx":{}})");
    const auto l = store.scan();
    EF_CHECK(l.newer.size() == 1);
    auto tx = make_tx(hexid(9));
    const auto s = store.begin(tx);
    EF_CHECK(s.code() == ErrorCode::SchemaTooNew);
    EF_CHECK(fs.files.size() == 1);  // yeni kayit yazilmadi
    EF_CHECK(store.scan().transactions.empty());
}

// CORE-05: cokme sonrasi kalan *.json.tmp commit edilmemis gecici dosyadir; rapor edilir ve yeni yazimdan
// once silinir (bozuk journal diye siniflandirilmaz, kota/kayit sayilmaz).
EF_TEST(journal_stale_temp_reported_and_discarded_before_new_change) {
    MemoryJournalFs fs;
    JournalStore store(fs, "machine-a");
    fs.files.emplace_back("tx-" + hexid(8) + ".json.tmp", "partial-bytes");
    const auto before = store.scan();
    EF_CHECK(before.transactions.empty());
    EF_CHECK(before.corrupt.empty());
    EF_CHECK(before.stale_temp.size() == 1);
    auto tx = make_tx(hexid(9));
    EF_CHECK(store.begin(tx).ok());
    const auto after = store.scan();
    EF_CHECK(after.stale_temp.empty());
    EF_CHECK(fs.files.size() == 1);
    EF_CHECK(fs.files[0].first == JournalStore::file_name(tx.id));
}

EF_TEST(journal_quota_never_drops_active) {
    MemoryJournalFs fs;
    StoreLimits lim;
    lim.max_transactions = 3;
    JournalStore store(fs, "m", lim);
    for (int i = 0; i < 3; ++i) {
        auto t = make_tx(hexid(i), 1000 + i);
        EF_CHECK(store.begin(t).ok());
        if (i == 0) {  // en eskisi tamamlanmis
            t.state = TxState::Applying; EF_CHECK(store.persist(t).ok());
            t.state = TxState::RollingBack; EF_CHECK(store.persist(t).ok());
            t.state = TxState::Restored; EF_CHECK(store.persist(t).ok());
        }
    }
    auto t4 = make_tx(hexid(9), 2000);
    EF_CHECK(store.begin(t4).ok());  // restored olan silinerek yer acildi
    EF_CHECK(fs.files.size() == 3);
    auto t5 = make_tx(hexid(10), 3000);
    EF_CHECK(store.begin(t5).code() == ErrorCode::ParseLimit);  // hepsi aktif: yer acilmaz, mutation baslamaz
}
