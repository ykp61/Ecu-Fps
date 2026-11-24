// T11 (ADR-035): Defender tarama dislamasi. Sahte DefenderApi; gercek Defender DEGISMEZ.
#include <string>
#include <vector>

#include "core/adapters/windows/defender_exclusion.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;
namespace cw = ef::core::adapters::windows;

namespace {
const std::string kGame = "D:\\SteamLibrary\\steamapps\\common\\Portal";

SettingOperation op_for(const std::string& path, const char* v = "1", OperationScope sc = OperationScope::Machine) {
    return SettingOperation{OperationId::DefenderExclusionSet, sc, cw::defender_target_for_path(path), v, false, false};
}

struct DefRig {
    cw::FakeDefenderApi api;
    cw::DefenderExclusionAdapter adapter{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    DefRig() { reg.add(OperationId::DefenderExclusionSet, &adapter); }
    JournalTx tx(const std::string& path) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = op_for(path);
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(defender_op_id_roundtrip_and_target_format) {
    EF_CHECK(to_string(OperationId::DefenderExclusionSet) == "defender_exclusion_set");
    EF_CHECK(parse_operation_id("defender_exclusion_set") == OperationId::DefenderExclusionSet);
    EF_CHECK(parse_operation_id("service_start_type_set") == OperationId::ServiceStartTypeSet);  // onceki adlar degismedi
    const auto t = cw::defender_target_for_path("D:/SteamLibrary/steamapps/common/Portal/");
    EF_CHECK(t.rfind("defx:", 0) == 0);
    EF_CHECK(cw::defender_path_from_target(t) == std::optional<std::string>("d:\\steamlibrary\\steamapps\\common\\portal"));
    EF_CHECK(t == cw::defender_target_for_path(kGame));  // tek gosterim
    EF_CHECK(!cw::defender_path_from_target("defx:zz").has_value());
    EF_CHECK(!cw::defender_path_from_target("defx:").has_value());
}

EF_TEST(defender_round_trip_adds_then_removes_only_what_it_added) {
    DefRig r;
    auto a = r.engine.apply(r.tx(kGame));
    EF_CHECK(a.ok() && a.value().state == TxState::Active);
    EF_CHECK(r.api.paths.size() == 1 && r.api.adds == 1);
    EF_CHECK(!a.value().steps[0].snapshot.existed);
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.paths.empty() && r.api.removes == 1);
}

EF_TEST(defender_pre_existing_exclusion_is_never_removed_on_restore) {
    DefRig r;
    r.api.paths = {"D:\\SteamLibrary\\steamapps\\common\\PORTAL"};  // kullanicinin kendi dislamasi (harf farki onemsiz)
    auto a = r.engine.apply(r.tx(kGame));
    EF_CHECK(a.ok() && a.value().steps[0].snapshot.existed);
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.paths.size() == 1 && r.api.removes == 0);  // dokunulmadi
}

EF_TEST(defender_other_exclusions_untouched) {
    DefRig r;
    r.api.paths = {"C:\\Tools", "E:\\Other"};
    auto a = r.engine.apply(r.tx(kGame));
    EF_CHECK(a.ok() && r.api.paths.size() == 3);
    EF_CHECK(r.engine.rollback(a.value()).ok());
    EF_CHECK(r.api.paths.size() == 2 && r.api.paths[0] == "C:\\Tools" && r.api.paths[1] == "E:\\Other");
}

EF_TEST(defender_not_usable_or_hidden_list_blocks_before_any_write) {
    for (const auto u : {std::optional<bool>(false), std::optional<bool>()}) {
        DefRig r;
        r.api.usable_value = u;
        EF_CHECK(!r.engine.apply(r.tx(kGame)).ok());
        EF_CHECK(r.api.adds == 0 && r.api.paths.empty());
    }
    DefRig hidden;
    hidden.api.deny_list = true;  // yonetici degil: liste gizli, "onceden var mi" bilinmez
    EF_CHECK(!hidden.engine.apply(hidden.tx(kGame)).ok());
    EF_CHECK(hidden.api.adds == 0);
}

EF_TEST(defender_adapter_rejects_wrong_scope_value_and_target) {
    DefRig r;
    EF_CHECK(!r.adapter.apply(op_for(kGame, "0")).ok());
    EF_CHECK(!r.adapter.apply(op_for(kGame, "1", OperationScope::CurrentUser)).ok());
    EF_CHECK(!r.adapter.capture(op_for(kGame, "1", OperationScope::CurrentUser)).ok());
    auto bad = op_for(kGame);
    bad.target = "defx:" + std::string("43") + "3a";  // "C:" kok degil
    EF_CHECK(!r.adapter.apply(bad).ok());
    EF_CHECK(r.api.adds == 0);
}

EF_TEST(defender_path_shape_rejects_roots_system_profile_and_library_roots) {
    for (const char* p : {"C:\\", "C:", "D:\\Games", "C:\\Windows", "C:\\Windows\\System32", "c:\\windows\\temp\\x", "C:\\Program Files",
                          "C:\\Program Files (x86)", "C:\\Program Files\\WindowsApps", "C:\\Program Files\\WindowsApps\\Pkg",
                          "C:\\Users", "C:\\Users\\Ali", "C:\\ProgramData\\WindowsApps", "D:\\SteamLibrary\\steamapps",
                          "D:\\SteamLibrary\\steamapps\\common", "D:\\SteamLibrary\\steamapps\\common\\", "\\\\server\\share\\game",
                          "relative\\game", "D:\\a\\..\\b", "D:\\a\\*", "D:\\a\\b%PATH%", "D:\\Games\\x:y", ""})
        EF_CHECK(!cw::defender_path_shape_ok(p));
    for (const char* p : {kGame.c_str(), "D:\\Games\\Epic\\Fortnite", "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life", "E:/Games/Riot Games/VALORANT"})
        EF_CHECK(cw::defender_path_shape_ok(p));
}

EF_TEST(defender_path_allowed_requires_exact_launcher_game_folder) {
    const std::vector<std::string> trusted{kGame, "D:\\Games\\Epic\\Fortnite"};
    EF_CHECK(cw::defender_path_allowed(kGame, trusted));
    EF_CHECK(cw::defender_path_allowed("d:/steamlibrary/steamapps/common/portal/", trusted));
    EF_CHECK(!cw::defender_path_allowed("D:\\SteamLibrary\\steamapps\\common\\Portal\\bin", trusted));  // alt klasor: oyun klasoru degil
    EF_CHECK(!cw::defender_path_allowed("D:\\Manual\\Game", trusted));                                   // elle eklenen / bilinmeyen
    EF_CHECK(!cw::defender_path_allowed("D:\\SteamLibrary\\steamapps\\common", {"D:\\SteamLibrary\\steamapps\\common"}));  // koku guvenilir listede olsa da
    EF_CHECK(!cw::defender_path_allowed("C:\\Windows", {"C:\\Windows"}));
    EF_CHECK(!cw::defender_path_allowed("C:\\Users\\Ali", {"C:\\Users\\Ali"}));
    EF_CHECK(!cw::defender_path_allowed("D:\\", {"D:\\"}));
    EF_CHECK(!cw::defender_path_allowed(kGame, {}));
}

EF_TEST(defender_admin_allowlist_checks_shape_value_and_scope) {
    EF_CHECK(cw::defender_admin_allowed(op_for(kGame)));
    EF_CHECK(!cw::defender_admin_allowed(op_for(kGame, "0")));
    EF_CHECK(!cw::defender_admin_allowed(op_for(kGame, "1", OperationScope::CurrentUser)));
    auto bad = op_for(kGame);
    bad.target = "defx:" + std::string("633a5c77696e646f7773");  // c:\windows
    EF_CHECK(!cw::defender_admin_allowed(bad));
    bad.target = "defx:433a5c47616d6573";  // buyuk harf hex yolu ("C:\Games"): kanonik degil ve derinlik yetersiz
    EF_CHECK(!cw::defender_admin_allowed(bad));
    bad.target = "svcstart:bthserv";
    EF_CHECK(!cw::defender_admin_allowed(bad));
}
