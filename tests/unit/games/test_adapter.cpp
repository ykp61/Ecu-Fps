#include "core/games/adapter.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(game_adapter_default_is_observation_only) {
    GameIdentity g{"steam", "1172470", "Apex Legends", "C:\\x", "17000001", "", ""};
    const auto e = effective(g, {}, false);
    EF_CHECK(e.has(Capability::Observation) && !e.has(Capability::BuiltinBenchmark) && !e.has(Capability::Replay));
    EF_CHECK(e.reasons.size() == 1 && e.reasons[0] == Invalidation::NoAdapter);  // otomatik sahne/replay varsayilmaz
}

EF_TEST(game_adapter_version_and_cloud_invalidation) {
    const std::vector<AdapterRecord> reg{{"steam", "730", {"19837364"}, Capability::BuiltinBenchmark | Capability::ConfigRead | Capability::ConfigWrite,
                                          "artifacts/game-first-support.md"}};
    GameIdentity cs2{"steam", "730", "Counter-Strike 2", "C:\\cs2", "19837364", "", ""};
    auto e = effective(cs2, reg, false);
    EF_CHECK(e.has(Capability::BuiltinBenchmark) && e.has(Capability::ConfigWrite) && e.reasons.empty());
    e = effective(cs2, reg, true);  // bulut esitleme: config yazimi kapali, okuma/benchmark acik
    EF_CHECK(!e.has(Capability::ConfigWrite) && e.has(Capability::ConfigRead) && e.reasons[0] == Invalidation::CloudSyncActive);
    cs2.version = "19900000";  // oyun guncellendi
    e = effective(cs2, reg, false);
    EF_CHECK(!e.has(Capability::BuiltinBenchmark) && e.reasons[0] == Invalidation::VersionChanged);
    cs2.version = "";
    EF_CHECK(effective(cs2, reg, false).reasons[0] == Invalidation::VersionUnknown);
    cs2.version = "19837364";
    cs2.exe_version = "1.40.9.1";
    EF_CHECK(effective(cs2, reg, false, "1.40.9.0").reasons[0] == Invalidation::ExeChanged);
}

EF_TEST(game_manifest_carries_id_and_version) {
    const char* acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"730\"\n\t\"name\"\t\t\"Counter-Strike 2\"\n\t\"installdir\"\t\t\"Counter-Strike Global Offensive\"\n"
                      "\t\"SizeOnDisk\"\t\t\"40000000000\"\n\t\"buildid\"\t\t\"19837364\"\n}\n";
    const auto g = ef::core::inventory::parse_steam_appmanifest(acf, "D:\\SteamLibrary");
    EF_CHECK(g && g->app_id == "730" && g->version == "19837364");
    const auto id = identity_from(*g);
    EF_CHECK(id.store == "steam" && id.app_id == "730" && id.version == "19837364");
    const auto e = ef::core::inventory::parse_epic_manifest(
        R"({"DisplayName":"Fortnite","InstallLocation":"C:\\Fortnite","InstallSize":1,"AppName":"Fortnite","AppVersionString":"++Fortnite+Release-33.10","LaunchExecutable":"FortniteGame/Binaries/Win64/FortniteLauncher.exe"})");
    EF_CHECK(e && e->app_id == "Fortnite" && e->version == "++Fortnite+Release-33.10" && !e->exe_hint.empty());
}
