#include "core/adapters/windows/gaming_toggles.h"
#include "core/recovery/journal.h"
#include "ef_test.h"

#include <utility>

using namespace ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using ef::contracts::TxState;

namespace {
SettingOperation op(const char* target, const char* v) {
    return SettingOperation{OperationId::GameModeSet, OperationScope::CurrentUser, target, v, false, false};
}
const std::wstring kDx = FakeUserRegistry::key(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", L"DirectXUserGlobalSettings");
const std::wstring kGm = FakeUserRegistry::key(L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled");
const std::wstring kApp = FakeUserRegistry::key(L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled");
const std::wstring kCfg = FakeUserRegistry::key(L"System\\GameConfigStore", L"GameDVR_Enabled");
const std::wstring kTips = FakeUserRegistry::key(L"Software\\Microsoft\\GameBar", L"ShowStartupPanel");
}  // namespace

EF_TEST(gaming_dx_flag_string_edit_keeps_other_items) {
    EF_CHECK(dx_flag_get(L"AutoHDREnable=1;SwapEffectUpgradeEnable=0;", L"SwapEffectUpgradeEnable") == L"0");
    EF_CHECK(!dx_flag_get(L"AutoHDREnable=1;", L"VRROptimizeEnable"));
    EF_CHECK(dx_flag_set(L"AutoHDREnable=1;SwapEffectUpgradeEnable=0;", L"SwapEffectUpgradeEnable", L"1") ==
              L"AutoHDREnable=1;SwapEffectUpgradeEnable=1;");
    EF_CHECK(dx_flag_set(L"AutoHDREnable=1;", L"VRROptimizeEnable", L"1") == L"AutoHDREnable=1;VRROptimizeEnable=1;");
    EF_CHECK(dx_flag_set(L"AutoHDREnable=1;VRROptimizeEnable=1;", L"VRROptimizeEnable", L"") == L"AutoHDREnable=1;");  // kaldir
    EF_CHECK(dx_flag_set(L"", L"VRROptimizeEnable", L"1") == L"VRROptimizeEnable=1;");
}

EF_TEST(gaming_toggle_dword_apply_readback_restore_exact) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    reg.values[kGm] = dword_raw(0);
    auto snap = a.capture(op("gaming:game_mode", "1"));
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "0");
    EF_CHECK(a.apply(op("gaming:game_mode", "1")).ok());
    EF_CHECK(a.read_back(op("gaming:game_mode", "1")).value() == std::optional<std::string>("1"));
    EF_CHECK(a.restore(op("gaming:game_mode", "1"), snap.value()).ok());
    EF_CHECK(reg.values[kGm] == dword_raw(0));  // birebir
}

EF_TEST(gaming_toggle_missing_value_restored_by_delete) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    auto snap = a.capture(op("gaming:dvr_background", "0"));
    EF_CHECK(snap.ok() && !snap.value().existed);
    EF_CHECK(a.apply(op("gaming:dvr_background", "0")).ok() && reg.values.size() == 1);
    EF_CHECK(a.restore(op("gaming:dvr_background", "0"), snap.value()).ok() && reg.values.empty());  // varsayilan uydurulmadi
}

EF_TEST(gaming_toggle_dx_restore_only_own_item) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    reg.values[kDx] = sz_raw(L"AutoHDREnable=0;");
    auto snap = a.capture(op("gaming:windowed_opt", "1"));
    EF_CHECK(snap.ok() && snap.value().old_value == "<absent>");
    EF_CHECK(a.apply(op("gaming:windowed_opt", "1")).ok());
    EF_CHECK(raw_sz(reg.values[kDx]) == L"AutoHDREnable=0;SwapEffectUpgradeEnable=1;");
    reg.values[kDx] = sz_raw(L"AutoHDREnable=1;SwapEffectUpgradeEnable=1;");  // kullanici sonradan AutoHDR'i acti
    EF_CHECK(a.restore(op("gaming:windowed_opt", "1"), snap.value()).ok());
    EF_CHECK(raw_sz(reg.values[kDx]) == L"AutoHDREnable=1;");  // kullanicinin degisikligi korundu
}

EF_TEST(gaming_toggle_dx_value_created_then_deleted) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    auto snap = a.capture(op("gaming:windows_vrr", "1"));
    EF_CHECK(snap.ok() && !snap.value().existed);
    EF_CHECK(a.apply(op("gaming:windows_vrr", "1")).ok());
    EF_CHECK(a.restore(op("gaming:windows_vrr", "1"), snap.value()).ok() && reg.values.empty());
}

EF_TEST(gaming_toggle_rejects_unknown_target_and_bad_value) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    EF_CHECK(!a.capture(op("gaming:anything", "1")).ok());
    EF_CHECK(!a.apply(op("gaming:game_mode", "2")).ok() && reg.values.empty());
}

EF_TEST(gaming_toggle_f73_extra_dvr_targets_roundtrip) {
    struct Case {
        const char* id;
        const wchar_t* subkey;
        const wchar_t* name;
    };
    const Case cases[] = {
        {"gaming:app_capture", L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled"},
        {"gaming:dvr_enabled", L"System\\GameConfigStore", L"GameDVR_Enabled"},
        {"gaming:gamebar_startup", L"Software\\Microsoft\\GameBar", L"ShowStartupPanel"},
    };
    for (const auto& k : cases) {
        const auto* t = find_gaming_target(k.id);
        EF_CHECK(t != nullptr && t->subkey == k.subkey && t->name == k.name && !t->is_dx_flag);
        FakeUserRegistry reg;
        GamingToggleAdapter a(reg);
        reg.values[FakeUserRegistry::key(k.subkey, k.name)] = dword_raw(1);
        auto snap = a.capture(op(k.id, "0"));
        EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "1");
        EF_CHECK(a.apply(op(k.id, "0")).ok());
        EF_CHECK(a.read_back(op(k.id, "0")).value() == std::optional<std::string>("0"));
        EF_CHECK(a.restore(op(k.id, "0"), snap.value()).ok());
        EF_CHECK(reg.values[FakeUserRegistry::key(k.subkey, k.name)] == dword_raw(1));  // birebir
    }
}

// F-73: tek "Duzelt" islemi uc anahtari AYNI journal transaction'inda kapatir; geri alma ucuunu birebir eski
// degerine dondurur (gercek HKCU'ya dokunmadan, bellek ici kayit defteri + kayit saglayici ile).
EF_TEST(gaming_f73_three_keys_one_transaction_apply_and_rollback) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    const std::wstring kHist = FakeUserRegistry::key(L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"HistoricalCaptureEnabled");
    reg.values[kHist] = dword_raw(1);
    reg.values[kApp] = dword_raw(1);
    reg.values[kCfg] = dword_raw(1);
    ef::core::recovery::MemoryJournalFs fs;
    ef::core::recovery::JournalStore store{fs, "machine-1"};
    ef::core::recovery::AdapterRegistry ar;
    ar.add(OperationId::GamingToggleSet, &a);
    ef::core::recovery::TransactionEngine eng{store, ar};
    ef::core::recovery::JournalTx tx;
    tx.id = "0123456789abcdef0123456789abcdef";
    tx.actor_sid = tx.target_sid = "S-1-5-21-test";
    tx.created_unix_ms = 1;
    const std::pair<const char*, const char*> steps[] = {
        {"gaming:dvr_background", "0"},
        {"gaming:app_capture", "0"},
        {"gaming:dvr_enabled", "0"},
    };
    for (const auto& [target, value] : steps) {
        ef::core::recovery::JournalStep st;
        st.op = SettingOperation{OperationId::GamingToggleSet, OperationScope::CurrentUser, target, value, false, false};
        tx.steps.push_back(std::move(st));
    }
    auto applied = eng.apply(std::move(tx));
    EF_CHECK(applied.ok() && applied.value().state == TxState::Active);
    EF_CHECK(reg.values[kHist] == dword_raw(0));
    EF_CHECK(reg.values[kApp] == dword_raw(0));
    EF_CHECK(reg.values[kCfg] == dword_raw(0));
    const auto list = store.scan();
    EF_CHECK(list.transactions.size() == 1 && list.transactions[0].steps.size() == 3);
    auto rolled = eng.rollback(list.transactions[0]);
    EF_CHECK(rolled.ok() && rolled.value().state == TxState::Restored);
    EF_CHECK(reg.values[kHist] == dword_raw(1));
    EF_CHECK(reg.values[kApp] == dword_raw(1));
    EF_CHECK(reg.values[kCfg] == dword_raw(1));
}

#include "core/perf/gaming_plan.h"

EF_TEST(gaming_plan_only_items_that_differ_with_windows_defaults) {
    FakeUserRegistry reg;
    GamingToggleAdapter a(reg);
    // Hic kayit yok: Oyun Modu (varsayilan acik) ve arka plan kaydi (varsayilan kapali) zaten uygun -> plana
    // girmez; yakalama anahtarlari ve Game Bar ipucu paneli varsayilan acik -> kapatilir; pencereli iyilestirme
    // ve VRR bilinmiyor -> acik yazilir.
    auto items = ef::core::perf::gaming_plan_items(a);
    EF_CHECK(items.size() == 5);
    EF_CHECK(items[0].op.target == "gaming:app_capture" && items[0].op.new_value == "0");
    EF_CHECK(items[1].op.target == "gaming:dvr_enabled" && items[1].op.new_value == "0");
    EF_CHECK(items[2].op.target == "gaming:gamebar_startup" && items[2].op.new_value == "0");
    EF_CHECK(items[3].group_key == "plan.windowed" && items[3].op.target == "gaming:windowed_opt");
    EF_CHECK(items[4].group_key == "plan.windowed" && items[4].op.target == "gaming:windows_vrr");
    reg.values[kGm] = dword_raw(0);  // kullanici Oyun Modu'nu kapatmis
    reg.values[FakeUserRegistry::key(L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"HistoricalCaptureEnabled")] = dword_raw(1);
    reg.values[kApp] = dword_raw(0);
    reg.values[kCfg] = dword_raw(0);
    reg.values[kTips] = dword_raw(1);  // ipucu paneli acik -> kapatilir
    reg.values[kDx] = sz_raw(L"SwapEffectUpgradeEnable=1;VRROptimizeEnable=1;");
    items = ef::core::perf::gaming_plan_items(a);
    EF_CHECK(items.size() == 3);
    EF_CHECK(items[0].op.target == "gaming:game_mode" && items[0].op.new_value == "1");
    EF_CHECK(items[1].op.target == "gaming:dvr_background" && items[1].op.new_value == "0");
    EF_CHECK(items[2].op.target == "gaming:gamebar_startup" && items[2].op.new_value == "0");
}
