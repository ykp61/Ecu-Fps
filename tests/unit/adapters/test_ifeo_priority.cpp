// T09 (ADR-035): IFEO PerfOptions oyun onceligi. Sahte kayit defteri; gercek HKLM DEGISMEZ.
#include <string>

#include "core/adapters/windows/ifeo_priority.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;
namespace cw = ef::core::adapters::windows;

namespace {
const std::wstring kIfeoRoot = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";

SettingOperation op_for(const std::string& exe, const char* v = "above_normal", OperationScope sc = OperationScope::Machine) {
    return SettingOperation{OperationId::GamePriorityHint, sc, "ifeoprio:" + exe, v, false, false};
}

struct IfeoRig {
    cw::FakeMachineRegistry reg;
    cw::IfeoPriorityAdapter adapter{&reg};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry registry;
    TransactionEngine engine{store, registry};
    int n = 0;
    IfeoRig() { registry.add(OperationId::GamePriorityHint, &adapter); }
    JournalTx tx(const std::string& exe) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = op_for(exe);
        t.steps.push_back(s);
        return t;
    }
    std::wstring perf(const std::wstring& exe) const { return kIfeoRoot + L"\\" + exe + L"\\PerfOptions"; }
    std::wstring top(const std::wstring& exe) const { return kIfeoRoot + L"\\" + exe; }
};
}  // namespace

EF_TEST(ifeo_op_id_roundtrip_and_target_rules) {
    EF_CHECK(to_string(OperationId::GamePriorityHint) == "game_priority_hint");
    EF_CHECK(parse_operation_id("game_priority_hint") == OperationId::GamePriorityHint);
    EF_CHECK(parse_operation_id("defender_exclusion_set") == OperationId::DefenderExclusionSet);  // onceki adlar degismedi
    static_assert(static_cast<int>(OperationId::GamePriorityHint) == static_cast<int>(OperationId::DefenderExclusionSet) + 1,
                  "GamePriorityHint enum sonuna eklenmeli");
    EF_CHECK(cw::ifeo_priority_target("CS2.EXE") == "ifeoprio:cs2.exe");
    EF_CHECK(cw::ifeo_priority_target("Minecraft.Windows.exe") == "ifeoprio:minecraft.windows.exe");
    EF_CHECK(cw::ifeo_priority_target("valorant.exe").empty());
    EF_CHECK(cw::ifeo_priority_target("cs2.exe\\..\\x.exe").empty());
    // Hedeften kanonik yazim (izin listesi buyuk/kucuk harfe duyarsiz).
    EF_CHECK(cw::ifeo_exe_from_target("ifeoprio:minecraft.windows.exe") == std::optional<std::string>("Minecraft.Windows.exe"));
    EF_CHECK(cw::ifeo_exe_from_target("ifeoprio:cs2.exe") == std::optional<std::string>("cs2.exe"));
    EF_CHECK(!cw::ifeo_exe_from_target("ifeoprio:CS2.exe").has_value());  // hedef kucuk harf olmali
    EF_CHECK(!cw::ifeo_exe_from_target("ifeoprio:cs2").has_value());
    EF_CHECK(!cw::ifeo_exe_from_target("ifeoprio:").has_value());
    EF_CHECK(!cw::ifeo_exe_from_target("tweak:cs2.exe").has_value());
}

EF_TEST(ifeo_allowlist_has_no_kernel_anticheat_games) {
    for (const auto& g : cw::priority_games()) {
        const std::string exe(g.exe);
        EF_CHECK(exe != "valorant-win64-shipping.exe" && exe != "fortniteclient-win64-shipping.exe" && exe != "r5apex.exe" &&
                  exe != "pubg.exe" && exe != "rainbowsix.exe");
        EF_CHECK(!cw::ifeo_priority_target(g.exe).empty());
    }
    EF_CHECK(cw::priority_games().size() == 3);
}

EF_TEST(ifeo_round_trip_creates_then_removes_only_our_keys) {
    IfeoRig r;
    auto a = r.engine.apply(r.tx("cs2.exe"));
    EF_CHECK(a.ok() && a.value().state == TxState::Active);
    const auto v = r.reg.values.find(cw::FakeMachineRegistry::key(r.perf(L"cs2.exe"), L"CpuPriorityClass"));
    EF_CHECK(v != r.reg.values.end() && cw::raw_dword(v->second) == std::optional<std::uint32_t>(6));
    EF_CHECK(!a.value().steps[0].snapshot.existed && a.value().steps[0].snapshot.source_version == "exe_new");
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.reg.values.empty());
    EF_CHECK(r.reg.keys.count(r.perf(L"cs2.exe")) == 0 && r.reg.keys.count(r.top(L"cs2.exe")) == 0);  // bizim actigimiz anahtarlar gitti
}

EF_TEST(ifeo_preexisting_exe_key_with_foreign_value_is_untouched) {
    IfeoRig r;
    // Baska bir yazilimin ayari: <exe> anahtari + Debugger degeri + bos PerfOptions yok.
    r.reg.keys.insert(r.top(L"cs2.exe"));
    r.reg.values[cw::FakeMachineRegistry::key(r.top(L"cs2.exe"), L"Debugger")] = cw::sz_raw(L"C:\\tool.exe");
    auto a = r.engine.apply(r.tx("cs2.exe"));
    EF_CHECK(a.ok() && a.value().steps[0].snapshot.source_version == "perf_new");
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    // <exe> anahtari ve yabanci deger kalir; yalniz bizim actigimiz PerfOptions kalkar.
    EF_CHECK(r.reg.keys.count(r.top(L"cs2.exe")) == 1);
    EF_CHECK(r.reg.values.count(cw::FakeMachineRegistry::key(r.top(L"cs2.exe"), L"Debugger")) == 1);
    EF_CHECK(r.reg.keys.count(r.perf(L"cs2.exe")) == 0);
}

EF_TEST(ifeo_preexisting_perf_key_with_other_value_keeps_key_and_foreign_value) {
    IfeoRig r;
    r.reg.keys.insert(r.top(L"dota2.exe"));
    r.reg.keys.insert(r.perf(L"dota2.exe"));
    r.reg.values[cw::FakeMachineRegistry::key(r.perf(L"dota2.exe"), L"IoPriority")] = cw::dword_raw(3);
    auto a = r.engine.apply(r.tx("dota2.exe"));
    EF_CHECK(a.ok() && a.value().steps[0].snapshot.source_version.empty() && !a.value().steps[0].snapshot.existed);
    EF_CHECK(r.engine.rollback(a.value()).ok());
    EF_CHECK(r.reg.keys.count(r.perf(L"dota2.exe")) == 1 && r.reg.keys.count(r.top(L"dota2.exe")) == 1);
    EF_CHECK(r.reg.values.size() == 1 && r.reg.values.count(cw::FakeMachineRegistry::key(r.perf(L"dota2.exe"), L"IoPriority")) == 1);
}

EF_TEST(ifeo_restore_writes_back_previous_value_exactly) {
    IfeoRig r;
    r.reg.keys.insert(r.top(L"cs2.exe"));
    r.reg.keys.insert(r.perf(L"cs2.exe"));
    r.reg.values[cw::FakeMachineRegistry::key(r.perf(L"cs2.exe"), L"CpuPriorityClass")] = cw::dword_raw(3);  // kullanicinin High'i
    auto a = r.engine.apply(r.tx("cs2.exe"));
    EF_CHECK(a.ok() && a.value().steps[0].snapshot.existed && a.value().steps[0].snapshot.old_value == "3");
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    const auto it = r.reg.values.find(cw::FakeMachineRegistry::key(r.perf(L"cs2.exe"), L"CpuPriorityClass"));
    EF_CHECK(it != r.reg.values.end() && cw::raw_dword(it->second) == std::optional<std::uint32_t>(3));
}

EF_TEST(ifeo_rejects_non_allowlisted_exe_wrong_value_scope_and_never_touches_other_ifeo_values) {
    IfeoRig r;
    r.reg.keys.insert(r.top(L"cs2.exe"));
    r.reg.values[cw::FakeMachineRegistry::key(r.top(L"cs2.exe"), L"Debugger")] = cw::sz_raw(L"C:\\tool.exe");
    // Izin listesinde olmayan exe.
    EF_CHECK(!r.adapter.apply(op_for("valorant.exe")).ok());
    EF_CHECK(!r.adapter.apply(op_for("notepad.exe")).ok());
    EF_CHECK(!r.adapter.apply(op_for("..\\cs2.exe")).ok());
    EF_CHECK(!r.adapter.capture(op_for("notepad.exe")).ok());
    // Baska deger.
    EF_CHECK(!r.adapter.apply(op_for("cs2.exe", "high")).ok());
    EF_CHECK(!r.adapter.apply(op_for("cs2.exe", "6")).ok());
    // Yanlis kapsam.
    EF_CHECK(!r.adapter.apply(op_for("cs2.exe", "above_normal", OperationScope::CurrentUser)).ok());
    EF_CHECK(r.reg.writes == 0 && r.reg.removes == 0);
    // Tam tur: Debugger degeri uygulama ve geri alma boyunca ne okunur ne yazilir ne silinir.
    auto a = r.engine.apply(r.tx("cs2.exe"));
    EF_CHECK(a.ok());
    EF_CHECK(r.engine.rollback(a.value()).ok());
    EF_CHECK(r.reg.values.size() == 1);
    const auto dbg = r.reg.values.find(cw::FakeMachineRegistry::key(r.top(L"cs2.exe"), L"Debugger"));
    EF_CHECK(dbg != r.reg.values.end() && cw::raw_sz(dbg->second) == std::optional<std::wstring>(L"C:\\tool.exe"));
    // Restore: yalniz CpuPriorityClass silinir (kayit defteri yazma/silme cagrilarini denetle).
    cw::FakeMachineRegistry reg2;
    cw::IfeoPriorityAdapter ad2{&reg2};
    reg2.values[cw::FakeMachineRegistry::key(L"x", L"Debugger")] = cw::sz_raw(L"d");
    Snapshot snap;  // onceden yok
    EF_CHECK(ad2.apply(op_for("cs2.exe")).ok());
    EF_CHECK(ad2.restore(op_for("cs2.exe"), snap).ok());
    EF_CHECK(reg2.values.size() == 1 && reg2.values.count(cw::FakeMachineRegistry::key(L"x", L"Debugger")) == 1);
}

EF_TEST(ifeo_minecraft_uses_canonical_key_name_case_insensitive_allowlist) {
    IfeoRig r;
    auto a = r.engine.apply(r.tx("minecraft.windows.exe"));
    EF_CHECK(a.ok());
    EF_CHECK(r.reg.values.count(cw::FakeMachineRegistry::key(r.perf(L"Minecraft.Windows.exe"), L"CpuPriorityClass")) == 1);
    EF_CHECK(r.engine.rollback(a.value()).ok() && r.reg.values.empty());
}

EF_TEST(ifeo_write_failure_is_reported_and_unreadable_registry_blocks_capture) {
    IfeoRig r;
    r.reg.fail_write = true;
    auto a = r.engine.apply(r.tx("cs2.exe"));
    EF_CHECK(!a.ok() || a.value().state != TxState::Active);
    EF_CHECK(r.reg.values.empty());
    cw::IfeoPriorityAdapter none{nullptr};
    EF_CHECK(!none.capture(op_for("cs2.exe")).ok());
    EF_CHECK(!none.apply(op_for("cs2.exe")).ok());
}
