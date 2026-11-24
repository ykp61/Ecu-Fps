#include "core/games/consent.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(consent_one_time_prompt_and_reset) {
    ConsentStore s;
    const auto id = normalize_game_id("C:/Games/CS2/game/bin/win64/CS2.exe");
    EF_CHECK(id == "c:\\games\\cs2\\game\\bin\\win64\\cs2.exe");
    EF_CHECK(s.needs_prompt(id));
    s.set(id, "Counter-Strike 2", Consent::Allow, 1000);
    EF_CHECK(!s.needs_prompt(id) && s.get(id) == Consent::Allow);  // ikinci kez sorulmaz
    s.set(id, "Counter-Strike 2", Consent::Deny, 2000);              // Ayarlar'dan degisir
    EF_CHECK(s.get(id) == Consent::Deny);
    EF_CHECK(s.clear(id) && s.needs_prompt(id));                    // kaldirilinca yeniden sorulur
    EF_CHECK(!s.clear(id));
    s.set("", "x", Consent::Allow, 1);
    EF_CHECK(s.size() == 0);  // bos kimlik kaydedilmez
}

EF_TEST(consent_roundtrip_and_rejects_bad_files) {
    ConsentStore s;
    s.set(normalize_game_id("D:\\Steam\\Game B\\b.exe"), "Oyun B", Consent::Deny, 5);
    s.set(normalize_game_id("D:\\Steam\\Game A\\a.exe"), "Oyun A", Consent::Allow, 7);
    auto r = ConsentStore::parse(s.serialize());
    EF_CHECK(r.ok());
    const auto l = r.value().list();
    EF_CHECK(l.size() == 2 && l[0].display_name == "Oyun A" && l[0].decision == Consent::Allow && l[0].decided_unix_ms == 7);
    EF_CHECK(r.value().get("d:\\steam\\game b\\b.exe") == Consent::Deny);
    EF_CHECK(!ConsentStore::parse("not json").ok());
    EF_CHECK(!ConsentStore::parse(R"({"schema_version":2,"games":[]})").ok());
    EF_CHECK(!ConsentStore::parse(R"({"schema_version":1,"games":[{"id":"a","name":"A","decision":"maybe"}]})").ok());
    EF_CHECK(!ConsentStore::parse(R"({"schema_version":1})").ok());
}

EF_TEST(consent_store_is_bounded) {
    ConsentStore s;
    for (int i = 0; i < static_cast<int>(ConsentStore::kMaxEntries) + 20; ++i)
        s.set("g" + std::to_string(i), "G", Consent::Allow, i);
    EF_CHECK(s.size() == ConsentStore::kMaxEntries);
    EF_CHECK(s.needs_prompt("g0") && !s.needs_prompt("g519"));  // en eski dusuruldu
}
