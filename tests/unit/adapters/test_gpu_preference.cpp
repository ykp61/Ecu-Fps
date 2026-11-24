#include "core/adapters/windows/gpu_preference.h"
#include "ef_test.h"

#include <string>

using namespace ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
const std::string kExeUtf8 = "C:\\Games\\Test Game\\game.exe";
const std::wstring kExeW = L"c:\\games\\test game\\game.exe";

SettingOperation op(const std::string& target, const std::string& v = "2") {
    return SettingOperation{OperationId::GpuPreferenceSet, OperationScope::CurrentUser, target, v, false, false};
}
std::wstring key() { return FakeUserRegistry::key(kGpuPrefSubkey, kExeW); }
std::optional<std::wstring> value(FakeUserRegistry& reg) {
    const auto it = reg.values.find(key());
    if (it == reg.values.end()) return std::nullopt;
    return raw_sz(it->second);
}
}  // namespace

EF_TEST(gpupref_round_trip_absent_before) {
    FakeUserRegistry reg;
    GpuPreferenceAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto target = gpu_pref_target_for_exe(kExeUtf8);
    EF_CHECK(!target.empty() && target.rfind("gpupref:", 0) == 0);
    const auto o = op(target);
    const auto snap = a.capture(o);
    EF_CHECK(snap.ok() && !snap.value().existed);
    EF_CHECK(a.apply(o).ok());
    EF_CHECK(value(reg) == std::optional<std::wstring>(L"GpuPreference=2;"));  // sondaki ';' bicimi korunur
    const auto rb = a.read_back(o);
    EF_CHECK(rb.ok() && rb.value() == std::optional<std::string>("2"));
    EF_CHECK(a.restore(o, snap.value()).ok());
    EF_CHECK(!value(reg));  // onceden yoktu: deger silinir
    const auto after = a.read_back(o);
    EF_CHECK(after.ok() && !after.value());
}

EF_TEST(gpupref_preserves_other_tokens_and_restores_raw) {
    FakeUserRegistry reg;
    GpuPreferenceAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    reg.values[key()] = sz_raw(L"GpuPreference=1;FooBar=7;");
    const auto o = op(gpu_pref_target_for_exe(kExeUtf8));
    const auto snap = a.capture(o);
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "1");
    EF_CHECK(a.apply(o).ok());
    EF_CHECK(value(reg) == std::optional<std::wstring>(L"GpuPreference=2;FooBar=7;"));  // bilinmeyen jeton korunur
    EF_CHECK(a.apply(o).ok());  // tekrar uygulama degisiklik yapmaz
    EF_CHECK(value(reg) == std::optional<std::wstring>(L"GpuPreference=2;FooBar=7;"));
    EF_CHECK(a.restore(o, snap.value()).ok());
    EF_CHECK(value(reg) == std::optional<std::wstring>(L"GpuPreference=1;FooBar=7;"));
}

EF_TEST(gpupref_rejects_outside_roots_bad_target_and_bad_value) {
    FakeUserRegistry reg;
    GpuPreferenceAdapter a(reg);
    const auto target = gpu_pref_target_for_exe(kExeUtf8);
    // Kok yok: yazma reddedilir.
    EF_CHECK(!a.apply(op(target)).ok());
    EF_CHECK(!value(reg));
    a.set_allowed_roots({"D:\\Other"});
    EF_CHECK(!a.apply(op(target)).ok());
    EF_CHECK(!a.exe_writable(kExeUtf8));
    a.set_allowed_roots({"C:\\Games"});
    EF_CHECK(a.exe_writable(kExeUtf8));
    EF_CHECK(!a.exe_writable("C:\\Gamesx\\a.exe"));  // onek eslesmesi kok sayilmaz
    // Bozuk hedefler.
    for (const std::string& bad : {std::string("gpupref:"), std::string("gpupref:zz"), std::string("gpupref:abc"),
                                   std::string("appcompat:") + target.substr(8), std::string("c:\\games\\game.exe")}) {
        EF_CHECK(!a.apply(op(bad)).ok());
        EF_CHECK(!a.capture(op(bad)).ok());
        EF_CHECK(!a.read_back(op(bad)).ok());
        EF_CHECK(!a.restore(op(bad), ef::core::recovery::Snapshot{}).ok());
    }
    // Yalniz "2" kabul edilir.
    EF_CHECK(!a.apply(op(target, "1")).ok());
    EF_CHECK(!a.apply(op(target, "0")).ok());
    EF_CHECK(!a.apply(op(target, "")).ok());
    EF_CHECK(!value(reg));
    // Dize olmayan mevcut deger: yazma yok, hata.
    reg.values[key()] = dword_raw(5);
    EF_CHECK(!a.apply(op(target)).ok());
    EF_CHECK(raw_dword(reg.values[key()]) == std::optional<std::uint32_t>(5u));
}

EF_TEST(gpupref_restore_needs_no_roots) {
    FakeUserRegistry reg;
    GpuPreferenceAdapter a(reg);
    a.set_allowed_roots({"C:\\Games"});
    const auto o = op(gpu_pref_target_for_exe(kExeUtf8));
    const auto snap = a.capture(o);
    EF_CHECK(snap.ok() && a.apply(o).ok());
    a.set_allowed_roots({});  // kitaplik degisti: kurtarma yine de calisir
    EF_CHECK(a.restore(o, snap.value()).ok());
    EF_CHECK(!value(reg));
}
