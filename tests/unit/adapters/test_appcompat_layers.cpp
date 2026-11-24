#include "core/adapters/windows/appcompat_layers.h"
#include "core/recovery/journal.h"
#include "ef_test.h"

#include <utility>

using namespace ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using ef::contracts::TxState;

namespace {
const std::wstring kExe = L"c:\\games\\test game\\game.exe";
std::string hex_of(std::string_view s) {
    static constexpr char d[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : s) out.push_back(d[c >> 4]), out.push_back(d[c & 15]);
    return out;
}
std::string target_of(std::string_view path) { return "appcompat:" + hex_of(path); }
SettingOperation op(std::string_view target, std::string_view v = "disabled") {
    return SettingOperation{OperationId::AppCompatLayerSet, OperationScope::CurrentUser, std::string(target), std::string(v), false, false};
}
std::wstring key() { return FakeUserRegistry::key(kAppCompatLayersSubkey, kExe); }
std::wstring layer(FakeUserRegistry& reg) {
    const auto it = reg.values.find(key());
    return it == reg.values.end() ? std::wstring() : raw_sz(it->second).value_or(L"");
}
}  // namespace

EF_TEST(appcompat_target_shape_and_layer_tokens) {
    EF_CHECK(appcompat_exe_from_target(appcompat_target_for_exe("C:\\Games\\Test\\game.exe")) == L"c:\\games\\test\\game.exe");
    EF_CHECK(appcompat_target_for_exe("C:/Games/Test/Game.EXE") == target_of("c:\\games\\test\\game.exe"));
    // UTF-8 yol hedefte hex olarak tasinir ve birebir cozulur (ASCII disi karakterler korunur).
    const auto unicode = appcompat_target_for_exe("C:\\Games\\Türkçe Oyun\\Oyun.exe");
    EF_CHECK(!unicode.empty() && appcompat_exe_from_target(unicode) == L"c:\\games\\türkçe oyun\\oyun.exe");
    // Uzun ama gecerli yol: uretici ile cozucunun uzunluk siniri tutarli.
    std::string long_exe = "C:\\Games\\";
    long_exe += std::string(900, 'a');
    long_exe += ".exe";
    const auto long_target = appcompat_target_for_exe(long_exe);
    EF_CHECK(!long_target.empty() && appcompat_exe_from_target(long_target).has_value());
    EF_CHECK(appcompat_canonical_target(target_of("C:/Games/Test/Game.EXE")) ==
              std::optional<std::string>(target_of("c:\\games\\test\\game.exe")));
    for (const std::string& bad : {std::string("appcompat:"), std::string("appcompat:zz"), std::string("appcompat:abc"),
                                   target_of("game.exe"), target_of("C:\\Games\\a.txt"), target_of("C:\\Games\\..\\evil.exe"),
                                   target_of("C:\\Games\\sub\\\\game.exe"), target_of("C:\\Games\\dir. \\game.exe"),
                                   "other:" + hex_of("C:\\Games\\a.exe")}) {
        EF_CHECK(!appcompat_exe_from_target(bad));
    }
    EF_CHECK(layer_has_token(L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE", kDisableFsOptToken));
    EF_CHECK(layer_has_token(L"~ disabledxmaximizedwindowedmode", kDisableFsOptToken));
    EF_CHECK(!layer_has_token(L"~ HIGHDPIAWARE", kDisableFsOptToken));
    EF_CHECK(layer_add_token(L"", kDisableFsOptToken) == L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(layer_add_token(L"~ HIGHDPIAWARE", kDisableFsOptToken) == L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(layer_add_token(L"HIGHDPIAWARE", kDisableFsOptToken) == L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(layer_add_token(L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE", kDisableFsOptToken) == L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(layer_remove_token(L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE", kDisableFsOptToken) == L"~ HIGHDPIAWARE");
    EF_CHECK(layer_remove_token(L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE", kDisableFsOptToken).empty());
}

EF_TEST(appcompat_apply_readback_restore_absent_value) {
    FakeUserRegistry reg;
    AppCompatLayerAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto target = appcompat_target_for_exe("C:\\Games\\Test Game\\game.exe");
    auto snap = a.capture(op(target));
    EF_CHECK(snap.ok() && !snap.value().existed && snap.value().old_value == "enabled");
    EF_CHECK(a.apply(op(target)).ok());
    EF_CHECK(layer(reg) == L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(a.read_back(op(target)).value() == std::optional<std::string>("disabled"));
    EF_CHECK(a.token_state(target).value() == std::optional<bool>(true));
    EF_CHECK(a.restore(op(target), snap.value()).ok());
    EF_CHECK(reg.values.empty());  // onceden yoktu: varsayilan deger yazilmadi
}

EF_TEST(appcompat_existing_layers_preserved_apply_and_restore) {
    FakeUserRegistry reg;
    AppCompatLayerAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto target = appcompat_target_for_exe("C:\\Games\\Test Game\\game.exe");
    reg.values[key()] = sz_raw(L"~ HIGHDPIAWARE");
    auto snap = a.capture(op(target));
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "enabled");
    EF_CHECK(a.apply(op(target)).ok());
    EF_CHECK(layer(reg) == L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(a.restore(op(target), snap.value()).ok());
    EF_CHECK(layer(reg) == L"~ HIGHDPIAWARE");  // birebir eski deger
}

EF_TEST(appcompat_restore_keeps_layers_added_after_apply) {
    FakeUserRegistry reg;
    AppCompatLayerAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto target = appcompat_target_for_exe("C:\\Games\\Test Game\\game.exe");
    auto snap = a.capture(op(target));
    EF_CHECK(snap.ok() && !snap.value().existed);
    EF_CHECK(a.apply(op(target)).ok());
    reg.values[key()] = sz_raw(L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE");  // kullanici sonradan katman ekledi
    EF_CHECK(a.restore(op(target), snap.value()).ok());
    EF_CHECK(layer(reg) == L"~ HIGHDPIAWARE");  // yalniz bizim jeton kaldirildi
}

EF_TEST(appcompat_preexisting_token_restored) {
    FakeUserRegistry reg;
    AppCompatLayerAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto target = appcompat_target_for_exe("C:\\Games\\Test Game\\game.exe");
    reg.values[key()] = sz_raw(L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE");
    auto snap = a.capture(op(target));
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "disabled");
    EF_CHECK(a.apply(op(target)).ok() && layer(reg) == L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE");
    EF_CHECK(a.restore(op(target), snap.value()).ok() && layer(reg) == L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE");
}

EF_TEST(appcompat_rejects_invalid_target_value_and_unlisted_folder) {
    FakeUserRegistry reg;
    AppCompatLayerAdapter a(reg);
    const auto target = appcompat_target_for_exe("C:\\Games\\Test Game\\game.exe");
    EF_CHECK(!a.capture(op(target_of("C:\\Games\\a.txt"))).ok());
    EF_CHECK(!a.capture(op(target_of("C:\\Games\\..\\evil.exe"))).ok());
    EF_CHECK(!a.capture(op("appcompat:zz")).ok());
    EF_CHECK(!a.apply(op(target)).ok());  // izinli klasor listesi bos: yazma reddedilir
    EF_CHECK(!a.apply(op(target, "enabled")).ok());  // yalniz "disabled" kabul edilir
    a.set_allowed_roots({"C:\\Other"});
    EF_CHECK(!a.apply(op(target)).ok());  // kurulu oyun klasoru disinda
    a.set_allowed_roots({"C:\\Games"});
    reg.values[key()] = dword_raw(1);  // yanlis tur: sessizce yazilmaz
    EF_CHECK(!a.capture(op(target)).ok() && !a.apply(op(target)).ok() && !a.read_back(op(target)).ok());
}

EF_TEST(appcompat_engine_transaction_apply_and_rollback) {
    FakeUserRegistry reg;
    AppCompatLayerAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto target = appcompat_target_for_exe("C:\\Games\\Test Game\\game.exe");
    reg.values[key()] = sz_raw(L"~ HIGHDPIAWARE");
    ef::core::recovery::MemoryJournalFs fs;
    ef::core::recovery::JournalStore store{fs, "user-1"};
    ef::core::recovery::AdapterRegistry ar;
    ar.add(OperationId::AppCompatLayerSet, &a);
    ef::core::recovery::TransactionEngine eng{store, ar};
    ef::core::recovery::JournalTx tx;
    tx.id = "0123456789abcdef0123456789abcdef";
    tx.actor_sid = tx.target_sid = "S-1-5-21-test";
    tx.created_unix_ms = 1;
    ef::core::recovery::JournalStep st;
    st.op = op(target);
    tx.steps.push_back(std::move(st));
    auto applied = eng.apply(std::move(tx));
    EF_CHECK(applied.ok() && applied.value().state == TxState::Active);
    EF_CHECK(layer(reg) == L"~ HIGHDPIAWARE DISABLEDXMAXIMIZEDWINDOWEDMODE");
    const auto list = store.scan();
    EF_CHECK(list.transactions.size() == 1);
    auto rolled = eng.rollback(list.transactions[0]);
    EF_CHECK(rolled.ok() && rolled.value().state == TxState::Restored);
    EF_CHECK(layer(reg) == L"~ HIGHDPIAWARE");
}
