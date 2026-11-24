// ADR-035 / T02: derlenmis ayar katalogu, TweakAdapter ve broker izin kurali. Gercek kayit defterine dokunulmaz (sahte kovanlar).
#include <set>
#include <string>

#include "contracts/models.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/broker/admin.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
namespace rec = ef::core::recovery;

namespace {
constexpr TweakEntry kMachineDword{"t.machine", TweakHive::Machine, L"SOFTWARE\\EfTest\\Pol", L"Flag", kTweakRegDword, "1", "plan.t"};
constexpr TweakEntry kMachineSz{"t.sz", TweakHive::Machine, L"SOFTWARE\\EfTest\\Pol", L"Cat", kTweakRegSz, "High", "plan.t"};
constexpr TweakEntry kUserDword{"t.user", TweakHive::User, L"Software\\EfTest", L"UFlag", kTweakRegDword, "0", "plan.t"};
constexpr TweakEntry kStrict{"t.strict", TweakHive::Machine, L"SOFTWARE\\EfTest\\Pol", L"Tel", kTweakRegDword, "1", "plan.t",
                             TweakSkip::WhenCurrentLowerOrEqual};

const std::wstring kMKey = FakeMachineRegistry::key(L"SOFTWARE\\EfTest\\Pol", L"Flag");
const std::wstring kUKey = FakeUserRegistry::key(L"Software\\EfTest", L"UFlag");
}  // namespace

EF_TEST(tweak_catalog_entries_are_valid_unique_and_grouped) {
    std::set<std::string_view> ids;
    std::set<std::string_view> groups;
    for (const auto& g : ef::core::perf::plan_groups()) groups.insert(g.key);
    for (const auto& e : tweak_catalog()) {
        EF_CHECK(tweak_entry_valid(e));
        EF_CHECK(ids.insert(e.id).second);  // kimlikler benzersiz
        EF_CHECK(groups.count(e.group_key) == 1);  // grup plan_groups() icinde
        EF_CHECK(find_tweak(e.id) == &e);
    }
}

EF_TEST(tweak_catalog_forbidden_paths_rejected) {
    auto bad = [](const wchar_t* subkey) {
        TweakEntry e{"x.y", TweakHive::Machine, subkey, L"V", kTweakRegDword, "1", "plan.t"};
        return !tweak_entry_valid(e);
    };
    EF_CHECK(bad(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\game.exe"));
    EF_CHECK(bad(L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection"));
    EF_CHECK(bad(L"SOFTWARE\\Policies\\Microsoft\\Windows Defender"));
    EF_CHECK(bad(L"SYSTEM\\CurrentControlSet\\Control\\Lsa"));
    EF_CHECK(bad(L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State"));
    EF_CHECK(bad(L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard"));
    EF_CHECK(bad(L"SYSTEM\\CurrentControlSet\\Control\\CI\\Config"));
    EF_CHECK(bad(L"SOFTWARE\\a\\..\\b"));
    EF_CHECK(!bad(L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection"));
    // Gecersiz tur/deger/kimlik
    EF_CHECK(!tweak_entry_valid(TweakEntry{"Bad Id", TweakHive::User, L"a", L"b", kTweakRegDword, "1", "plan.t"}));
    EF_CHECK(!tweak_entry_valid(TweakEntry{"a.b", TweakHive::User, L"a", L"b", kTweakRegDword, "abc", "plan.t"}));
    EF_CHECK(!tweak_entry_valid(TweakEntry{"a.b", TweakHive::User, L"a", L"b", 3, "1", "plan.t"}));
    EF_CHECK(!tweak_entry_valid(TweakEntry{"a.b", TweakHive::User, L"a", L"b", kTweakRegSz, "x", "plan.t", TweakSkip::WhenCurrentLowerOrEqual}));
    EF_CHECK(tweak_entry_valid(kMachineDword) && tweak_entry_valid(kMachineSz) && tweak_entry_valid(kUserDword) && tweak_entry_valid(kStrict));
}

EF_TEST(tweak_adapter_roundtrip_machine_dword_absent_before_deleted_on_restore) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    auto snap = a.capture_entry(kMachineDword);
    EF_CHECK(snap.ok() && !snap.value().existed && snap.value().source_version == "key_new");
    EF_CHECK(a.apply_entry(kMachineDword).ok());
    EF_CHECK(m.values[kMKey] == dword_raw(1));
    EF_CHECK(a.read_back_entry(kMachineDword).value() == std::optional<std::string>("1"));
    EF_CHECK(a.restore_entry(kMachineDword, snap.value()).ok());
    EF_CHECK(m.values.empty());  // varsayilan uydurulmadi
    EF_CHECK(m.keys.empty());    // bizim actigimiz bos anahtar kaldirildi
    EF_CHECK(!a.read_back_entry(kMachineDword).value().has_value());
}

EF_TEST(tweak_adapter_roundtrip_restores_exact_raw_and_keeps_existing_key) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.keys.insert(L"SOFTWARE\\EfTest\\Pol");
    m.values[kMKey] = dword_raw(7);
    m.values[FakeMachineRegistry::key(L"SOFTWARE\\EfTest\\Pol", L"Other")] = dword_raw(9);
    auto snap = a.capture_entry(kMachineDword);
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "7" && snap.value().source_version.empty());
    EF_CHECK(a.apply_entry(kMachineDword).ok() && m.values[kMKey] == dword_raw(1));
    EF_CHECK(a.restore_entry(kMachineDword, snap.value()).ok());
    EF_CHECK(m.values[kMKey] == dword_raw(7));
    EF_CHECK(m.values.size() == 2 && m.keys.size() == 1);
}

EF_TEST(tweak_adapter_sz_and_unexpected_type) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto kcat = FakeMachineRegistry::key(L"SOFTWARE\\EfTest\\Pol", L"Cat");
    m.keys.insert(L"SOFTWARE\\EfTest\\Pol");
    m.values[kcat] = sz_raw(L"Low");
    auto snap = a.capture_entry(kMachineSz);
    EF_CHECK(snap.ok() && snap.value().old_value == "Low");
    EF_CHECK(a.apply_entry(kMachineSz).ok() && a.read_back_entry(kMachineSz).value() == std::optional<std::string>("High"));
    EF_CHECK(a.restore_entry(kMachineSz, snap.value()).ok() && m.values[kcat] == sz_raw(L"Low"));
    // Beklenmeyen tur (QWORD): ham bicim aynen korunur ve geri yazilir.
    m.values[kcat] = RegRaw{11, {1, 2, 3, 4, 5, 6, 7, 8}};
    auto s2 = a.capture_entry(kMachineSz);
    EF_CHECK(s2.ok() && s2.value().old_value.rfind("raw:11:", 0) == 0);
    EF_CHECK(a.apply_entry(kMachineSz).ok());
    EF_CHECK(a.restore_entry(kMachineSz, s2.value()).ok() && m.values[kcat].type == 11 && m.values[kcat].bytes.size() == 8);
}

EF_TEST(tweak_adapter_user_hive_uses_hkcu_only) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    u.values[kUKey] = dword_raw(5);
    auto snap = a.capture_entry(kUserDword);
    EF_CHECK(snap.ok() && snap.value().old_value == "5");
    EF_CHECK(a.apply_entry(kUserDword).ok() && u.values[kUKey] == dword_raw(0) && m.writes == 0);
    EF_CHECK(a.restore_entry(kUserDword, snap.value()).ok() && u.values[kUKey] == dword_raw(5));
    // Onceden yok -> silinir
    u.values.clear();
    auto s0 = a.capture_entry(kUserDword);
    EF_CHECK(a.apply_entry(kUserDword).ok());
    EF_CHECK(a.restore_entry(kUserDword, s0.value()).ok() && u.values.empty());
}

EF_TEST(tweak_adapter_machine_unavailable_and_failures) {
    FakeUserRegistry u;
    TweakAdapter none(u, nullptr);
    EF_CHECK(!none.capture_entry(kMachineDword).ok());
    EF_CHECK(!none.apply_entry(kMachineDword).ok());
    EF_CHECK(none.capture_entry(kUserDword).ok());
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.fail_write = true;
    EF_CHECK(!a.apply_entry(kMachineDword).ok() && m.values.empty());
    m.fail_write = false;
    m.fail_read = true;
    EF_CHECK(!a.read_back_entry(kMachineDword).ok());
    // Bozuk anlik goruntu
    rec::Snapshot bad;
    bad.existed = true;
    bad.raw_type = "4";
    bad.raw = "zz";
    EF_CHECK(!a.restore_entry(kMachineDword, bad).ok());
}

EF_TEST(tweak_adapter_rejects_unknown_id_wrong_value_and_wrong_scope) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const SettingOperation unknown{OperationId::TweakValueSet, OperationScope::Machine, "tweak:no.such", "1", false, false};
    EF_CHECK(!a.capture(unknown).ok() && !a.apply(unknown).ok() && !a.read_back(unknown).ok());
    EF_CHECK(!a.restore(unknown, rec::Snapshot{}).ok());
    const SettingOperation path{OperationId::TweakValueSet, OperationScope::Machine, "HKLM\\SOFTWARE\\x|y", "1", false, false};
    EF_CHECK(!a.apply(path).ok());
    const SettingOperation wrong_id{OperationId::GamingToggleSet, OperationScope::Machine, "tweak:no.such", "1", false, false};
    EF_CHECK(!a.apply(wrong_id).ok());
    EF_CHECK(m.values.empty() && u.values.empty());
}

EF_TEST(tweak_skip_if_is_data_driven) {
    // Mevcut deger on_value'dan siki (kucuk): atlanir. Yoksa veya daha gevsekse atlanmaz.
    EF_CHECK(tweak_should_skip(kStrict, dword_raw(0)));
    EF_CHECK(tweak_should_skip(kStrict, dword_raw(1)));
    EF_CHECK(!tweak_should_skip(kStrict, dword_raw(3)));
    EF_CHECK(!tweak_should_skip(kStrict, std::nullopt));
    EF_CHECK(!tweak_should_skip(kMachineDword, dword_raw(0)));  // skip_if yok
    EF_CHECK(!tweak_should_skip(kStrict, sz_raw(L"x")));        // DWORD degil: atlama kosulu uygulanmaz
}

EF_TEST(tweak_broker_rule_machine_only_exact_value_table_derived) {
    namespace bk = ef::core::broker;
    EF_CHECK(tweak_admin_allowed(&kMachineDword, "1"));
    EF_CHECK(!tweak_admin_allowed(&kMachineDword, "0"));          // yanlis deger
    EF_CHECK(!tweak_admin_allowed(&kMachineDword, "1 "));         // bicim farki
    EF_CHECK(!tweak_admin_allowed(&kUserDword, "0"));             // HKCU girdisi broker'a gelmez
    EF_CHECK(!tweak_admin_allowed(nullptr, "1"));                 // bilinmeyen kimlik
    EF_CHECK(tweak_admin_allowed(&kMachineSz, "High") && !tweak_admin_allowed(&kMachineSz, "high"));
    // Gercek broker: katalogda olmayan kimlik / yol / kullanici kapsami reddedilir.
    EF_CHECK(!bk::admin_operation_allowed({OperationId::TweakValueSet, OperationScope::Machine, "tweak:no.such", "1", false, false}));
    EF_CHECK(!bk::admin_operation_allowed({OperationId::TweakValueSet, OperationScope::Machine, "HKLM\\SOFTWARE\\Policies|x", "1", false, false}));
    EF_CHECK(!bk::admin_operation_allowed({OperationId::TweakValueSet, OperationScope::CurrentUser, "tweak:no.such", "1", false, false}));
}

EF_TEST(tweak_operation_id_roundtrips_and_is_last) {
    using ef::contracts::parse_operation_id;
    using ef::contracts::to_string;
    EF_CHECK(to_string(OperationId::TweakValueSet) == "tweak_value_set");
    EF_CHECK(parse_operation_id("tweak_value_set") == std::optional<OperationId>(OperationId::TweakValueSet));
    static_assert(static_cast<int>(OperationId::TweakValueSet) == static_cast<int>(OperationId::BackgroundProcessLower) + 1,
                  "TweakValueSet enum sonuna eklenmeli");
}

// T03: Windows Update tutma/kapatma girdileri (HKLM, politika anahtarlari; Defender/Lsa vb. degil).
EF_TEST(t03_update_entries_present_machine_and_expected_values) {
    struct Want { const char* id; const wchar_t* name; const char* val; const char* group; };
    const Want w[] = {{"update.no_reboot", L"NoAutoRebootWithLoggedOnUsers", "1", "plan.update_hold"},
                      {"update.notify", L"AUOptions", "2", "plan.update_hold"},
                      {"update.do_off", L"DODownloadMode", "0", "plan.update_hold"},
                      {"update.auto_off", L"NoAutoUpdate", "1", "plan.update_off"},
                      {"update.off_notify", L"AUOptions", "2", "plan.update_off"}};
    for (const auto& x : w) {
        const auto* e = find_tweak(x.id);
        EF_CHECK(e != nullptr);
        if (!e) continue;
        EF_CHECK(e->hive == TweakHive::Machine && e->name == x.name && e->on_value == x.val && e->group_key == x.group);
        EF_CHECK(e->reg_type == kTweakRegDword && tweak_entry_valid(*e));
        EF_CHECK(tweak_admin_allowed(e, x.val));
    }
    EF_CHECK(tweaks_in_group("plan.update_hold").size() == 3 && tweaks_in_group("plan.update_off").size() == 2);
}

EF_TEST(t03_update_off_after_hold_restores_in_reverse_incl_absence) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    const auto au = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
    const auto k_opt = FakeMachineRegistry::key(au, L"AUOptions");
    const auto k_off = FakeMachineRegistry::key(au, L"NoAutoUpdate");
    const auto* hold = find_tweak("update.notify");
    const auto* off1 = find_tweak("update.auto_off");
    const auto* off2 = find_tweak("update.off_notify");
    EF_CHECK(hold && off1 && off2);
    if (!hold || !off1 || !off2) return;
    // Ayni AUOptions degerini iki grup yazar; Kapali ters sirada geri alir ve yoklugu da geri getirir.
    auto s1 = a.capture_entry(*hold);
    EF_CHECK(a.apply_entry(*hold).ok() && m.values[k_opt] == dword_raw(2));
    auto s2 = a.capture_entry(*off1);
    EF_CHECK(a.apply_entry(*off1).ok() && m.values[k_off] == dword_raw(1));
    auto s3 = a.capture_entry(*off2);
    EF_CHECK(s3.ok() && s3.value().existed && s3.value().old_value == "2");
    EF_CHECK(a.apply_entry(*off2).ok());
    EF_CHECK(a.restore_entry(*off2, s3.value()).ok() && a.restore_entry(*off1, s2.value()).ok() && a.restore_entry(*hold, s1.value()).ok());
    EF_CHECK(m.values.empty() && m.keys.empty());  // deger ve bizim actigimiz anahtar yok
}

// T14: gizlilik girdileri.
EF_TEST(t14_privacy_entries_valid_and_split_by_hive) {
    EF_CHECK(tweaks_in_group("plan.privacy").size() == 5 && tweaks_in_group("plan.privacy_system").size() == 2);
    for (const auto* e : tweaks_in_group("plan.privacy")) EF_CHECK(e->hive == TweakHive::User && e->on_value == "0" && tweak_entry_valid(*e));
    for (const auto* e : tweaks_in_group("plan.privacy_system")) EF_CHECK(e->hive == TweakHive::Machine && tweak_entry_valid(*e));
    const auto* t = find_tweak("privacy.telemetry");
    EF_CHECK(t && t->on_value == "1" && t->skip_if == TweakSkip::WhenPresent);  // 0 hicbir zaman yazilmaz
    const auto* act = find_tweak("privacy.activities");
    EF_CHECK(act && act->name == L"PublishUserActivities" && act->on_value == "0");
}

// T13: Edge/OneDrive politika girdileri (hicbir sey kaldirilmaz; yalniz ilke; kurum degeri varsa dokunulmaz).
EF_TEST(t13_debloat_policy_entries_valid_machine_whenpresent_and_exact) {
    EF_CHECK(tweaks_in_group("plan.debloat.edge").size() == 2 && tweaks_in_group("plan.debloat.onedrive").size() == 1);
    for (const auto* g : {"plan.debloat.edge", "plan.debloat.onedrive"})
        for (const auto* e : tweaks_in_group(g)) {
            EF_CHECK(e->hive == TweakHive::Machine && e->skip_if == TweakSkip::WhenPresent && tweak_entry_valid(*e));
            EF_CHECK(tweak_admin_allowed(e, e->on_value) && !tweak_admin_allowed(e, e->on_value == "0" ? "1" : "0"));
        }
    const auto* sb = find_tweak("debloat.edge_startup_boost");
    EF_CHECK(sb && sb->subkey == L"SOFTWARE\\Policies\\Microsoft\\Edge" && sb->name == L"StartupBoostEnabled" && sb->on_value == "0");
    const auto* bg = find_tweak("debloat.edge_background");
    EF_CHECK(bg && bg->name == L"BackgroundModeEnabled" && bg->on_value == "0");
    const auto* od = find_tweak("debloat.onedrive_sync");
    EF_CHECK(od && od->subkey == L"SOFTWARE\\Policies\\Microsoft\\Windows\\OneDrive" && od->name == L"DisableFileSyncNGSC" && od->on_value == "1");
}

// T21: Windows 11 arayuz temizligi + fare ivmesi (HKCU; UCPD korumali anahtar yok).
EF_TEST(t21_win11_cleanup_and_input_entries_valid_user_scope) {
    EF_CHECK(tweaks_in_group("plan.win11_cleanup").size() == 4 && tweaks_in_group("plan.input").size() == 3);
    for (const auto* g : {"plan.win11_cleanup", "plan.input"})
        for (const auto* e : tweaks_in_group(g)) {
            EF_CHECK(e->hive == TweakHive::User && tweak_entry_valid(*e) && !tweak_admin_allowed(e, e->on_value));
            EF_CHECK(e->name != L"TaskbarDa");  // UCPD korumali; geri okuma basarisiz olur
        }
    for (const char* id : {"input.mouse_speed", "input.mouse_thr1", "input.mouse_thr2"}) {
        const auto* e = find_tweak(id);
        EF_CHECK(e && e->subkey == L"Control Panel\\Mouse" && e->reg_type == kTweakRegSz && e->on_value == "0");
    }
}

// R-C3: Widgets ilkesi (HKLM Dsh, broker izinli, kurum degerine dokunmaz). Copilot/Recall girdisi BILEREK yok.
EF_TEST(rc3_widgets_entry_machine_policy_when_present_skip) {
    const auto* e = find_tweak("widgets.news_feed");
    EF_CHECK(e && e->hive == TweakHive::Machine && tweak_entry_valid(*e));
    EF_CHECK(e->subkey == L"SOFTWARE\\Policies\\Microsoft\\Dsh" && e->name == L"AllowNewsAndInterests" && e->on_value == "0");
    EF_CHECK(e->skip_if == TweakSkip::WhenPresent && tweaks_in_group("plan.widgets").size() == 1);
    EF_CHECK(tweak_admin_allowed(e, "0") && !tweak_admin_allowed(e, "1"));
    EF_CHECK(!find_tweak("widgets.copilot") && !find_tweak("widgets.recall"));
}

// ---- B2 (ADR-040 m.2): sched.priority_separation. Gercek kayit defterine dokunulmaz (MOCK: sahte kovanlar).
// Adim 0: resmi belge varsayilan 2'yi ve dusuk iki biti (None/Min/Max) gosterir; 38'in 2 ile davranis denkligi belgelenmedi
// -> girdi tabloda, ama kPrioritySeparationItemEnabled = false (plan ve broker kapali), atlama kurali eklenmedi.
namespace {
constexpr const wchar_t* kPrioKey = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
}

EF_TEST(b2_priority_separation_entry_is_exact_machine_dword_38_in_foreground_group) {
    const auto* e = find_tweak("sched.priority_separation");
    EF_CHECK(e != nullptr);
    if (!e) return;
    EF_CHECK(e->hive == TweakHive::Machine && e->subkey == kPrioKey && e->name == L"Win32PrioritySeparation");
    EF_CHECK(e->reg_type == kTweakRegDword && e->on_value == "38" && e->group_key == "plan.foreground_boost");
    EF_CHECK(e->skip_if == TweakSkip::None && !e->needs_reboot);  // atlama kurali yok (denklik dogrulanamadi)
    EF_CHECK(tweak_entry_valid(*e) && tweak_on_raw(*e) == std::optional<RegRaw>(dword_raw(38)));
    EF_CHECK(find_tweak_target("tweak:sched.priority_separation") == e);
}

EF_TEST(b2_priority_separation_group_default_is_no_disputed_and_flag_is_closed) {
    EF_CHECK(!kPrioritySeparationItemEnabled);  // dogrulanmadan acilmaz (kanit dosyasi: R-B2)
    const ef::core::perf::PlanGroup* g = nullptr;
    for (const auto& x : ef::core::perf::plan_groups())
        if (x.key == "plan.foreground_boost") g = &x;
    EF_CHECK(g != nullptr);
    if (!g) return;
    EF_CHECK(!g->default_on && g->evidence == ef::core::perf::Evidence::Disputed);
}

EF_TEST(b2_priority_separation_closed_flag_blocks_plan_group_and_broker) {
    const auto* e = find_tweak("sched.priority_separation");
    EF_CHECK(e != nullptr);
    if (!e) return;
    EF_CHECK(tweaks_in_group("plan.foreground_boost").empty());              // plan uretimi kapali
    EF_CHECK(!tweak_item_enabled(*e));
    // B3 apply-only kapi: bayrak yalniz YENI yazmayi kapatir (tweak_apply_allowed + TweakAdapter capture/apply);
    // izin listesi bicimi (tweak_admin_allowed) kayitli geri alma icin acik kalir.
    EF_CHECK(!tweak_apply_allowed(e, "38", TweakRuntime{}));
    EF_CHECK(tweak_admin_allowed(e, "38") && tweak_admin_allowed(e, "38", TweakRuntime{}));
    EF_CHECK(!tweak_admin_allowed(e, "26"));
    EF_CHECK(tweak_item_enabled(*find_tweak("update.notify")));              // diger girdiler etkilenmez
    // Bayrak ACIK varsayimiyla (yalniz test): hedef kimligi yine tek degerdir.
    EF_CHECK(tweak_item_enabled(*e, true) && tweaks_in_group("plan.foreground_boost", true).size() == 1);
    EF_CHECK(tweak_admin_allowed_gated(e, "38", true));
    EF_CHECK(!tweak_admin_allowed_gated(e, "26", true) && !tweak_admin_allowed_gated(e, "2", true));
    EF_CHECK(!tweak_admin_allowed_gated(e, "", true) && !tweak_admin_allowed_gated(e, "038", true) && !tweak_admin_allowed_gated(e, "38 ", true));
    EF_CHECK(!tweak_admin_allowed_gated(e, "38", false));
    EF_CHECK(!tweak_admin_allowed_gated(nullptr, "38", true));
}

EF_TEST(b2_priority_separation_restores_previous_value_or_absence) {
    const auto* e = find_tweak("sched.priority_separation");
    EF_CHECK(e != nullptr);
    if (!e) return;
    const auto key = FakeMachineRegistry::key(kPrioKey, L"Win32PrioritySeparation");
    {  // onceki deger 24 -> uygula 38 -> Kapali: 24 geri
        FakeUserRegistry u;
        FakeMachineRegistry m;
        TweakAdapter a(u, &m);
        m.keys.insert(kPrioKey);
        m.values[key] = dword_raw(24);
        auto snap = a.capture_entry(*e);
        EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "24");
        EF_CHECK(a.apply_entry(*e).ok() && m.values[key] == dword_raw(38));
        EF_CHECK(a.read_back_entry(*e).value() == std::optional<std::string>("38"));
        EF_CHECK(a.restore_entry(*e, snap.value()).ok() && m.values[key] == dword_raw(24));
        EF_CHECK(m.keys.count(kPrioKey) == 1);
    }
    {  // deger yoktu -> Kapali: silinir, varsayilan uydurulmaz
        FakeUserRegistry u;
        FakeMachineRegistry m;
        TweakAdapter a(u, &m);
        m.keys.insert(kPrioKey);
        auto snap = a.capture_entry(*e);
        EF_CHECK(snap.ok() && !snap.value().existed);
        EF_CHECK(a.apply_entry(*e).ok() && m.values[key] == dword_raw(38));
        EF_CHECK(a.restore_entry(*e, snap.value()).ok() && m.values.empty() && m.keys.count(kPrioKey) == 1);
    }
}

EF_TEST(b2_priority_separation_reboot_decision_comes_from_table) {
    const auto* e = find_tweak("sched.priority_separation");
    EF_CHECK(e != nullptr);
    if (!e) return;
    // Istemci needs_reboot=true dese de karar tablodan (girdide needs_reboot=false: etkinin anlik oldugu DOGRULANMADI, vaat yok).
    const SettingOperation op{OperationId::TweakValueSet, OperationScope::Machine, "tweak:sched.priority_separation", "38", true, false};
    EF_CHECK(ef::core::broker::admin_needs_reboot(op) == e->needs_reboot);
    // B3: izin listesi bicimi kayitli geri alma icin kabul eder; yeni yazma adaptorde (capture/apply) reddedilir (asagidaki test).
    EF_CHECK(ef::core::broker::admin_operation_allowed(op));
    SettingOperation other = op;
    other.new_value = "26";
    EF_CHECK(!ef::core::broker::admin_operation_allowed(other));
}

// B3 apply-only kapi (MOCK sahte kovan): bayrak kapaliyken yeni uygulama yazmaz, kayitli geri alma onceki degeri yazar.
EF_TEST(b3_closed_flag_blocks_new_apply_but_allows_recorded_restore) {
    const auto* e = find_tweak("sched.priority_separation");
    EF_CHECK(e != nullptr);
    if (!e) return;
    const auto key = FakeMachineRegistry::key(kPrioKey, L"Win32PrioritySeparation");
    const SettingOperation op{OperationId::TweakValueSet, OperationScope::Machine, "tweak:sched.priority_separation", "38", false, false};
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    m.keys.insert(kPrioKey);
    m.values[key] = dword_raw(24);
    EF_CHECK(!a.capture(op).ok());  // motor capture'da durur: journal yazilmaz, deger yazilmaz
    EF_CHECK(!a.apply(op).ok());
    EF_CHECK(m.writes == 0 && m.values[key] == dword_raw(24));
    // Bayrak acikken alinmis kayit (onceki 24) -> bayrak simdi kapali -> geri alma yine calisir.
    rec::Snapshot snap;
    snap.existed = true;
    snap.raw_type = "4";
    snap.raw = "18000000";  // 24 (little-endian DWORD)
    snap.old_value = "24";
    m.values[key] = dword_raw(38);
    EF_CHECK(a.read_back(op).ok());
    EF_CHECK(a.restore(op, snap).ok() && m.values[key] == dword_raw(24));
    // Kayitta deger yoktu -> geri alma siler.
    m.values[key] = dword_raw(38);
    EF_CHECK(a.restore(op, rec::Snapshot{}).ok() && m.values.count(key) == 0);
}

// ---- B3 (ADR-040 m.3): hesaplanan deger turu (InstalledRamKb). MOCK: TweakRuntime elle verilir, sahte kovanlar.
// Adim 0: SvcHostSplitThresholdInKB deger adi/birimi Microsoft Learn'de bulunamadi -> ADR-040 dogrulama 4: girdi KATALOGA
// EKLENMEZ. Altyapi asagidaki test girdisiyle (katalog disi) sinanir.
namespace {
constexpr const wchar_t* kCtl = L"SYSTEM\\CurrentControlSet\\Control";
constexpr TweakEntry kSvcSplit{"t.svc_split", TweakHive::Machine, kCtl, L"SvcHostSplitThresholdInKB", kTweakRegDword, "",
                               "plan.svchost_split", TweakSkip::WhenCurrentHigherOrEqual, true, TweakValueSource::InstalledRamKb};
TweakRuntime ram(std::uint64_t kb) {
    TweakRuntime rt;
    rt.installed_ram_kb = kb;
    return rt;
}
const std::wstring kSvcKey = FakeMachineRegistry::key(kCtl, L"SvcHostSplitThresholdInKB");
}  // namespace

EF_TEST(b3_svchost_entry_not_in_catalog_until_verified) {
    EF_CHECK(find_tweak("svc.split_threshold") == nullptr);
    EF_CHECK(tweaks_in_group("plan.svchost_split").empty());
    for (const auto& e : tweak_catalog()) EF_CHECK(e.value_source == TweakValueSource::Fixed);
    EF_CHECK(tweak_entry_valid(kSvcSplit));
    // Hesaplanan girdi sabit deger tasiyamaz, DWORD olmak zorunda.
    TweakEntry withv = kSvcSplit;
    withv.on_value = "1";
    EF_CHECK(!tweak_entry_valid(withv));
    TweakEntry sz = kSvcSplit;
    sz.reg_type = kTweakRegSz;
    sz.skip_if = TweakSkip::None;
    EF_CHECK(!tweak_entry_valid(sz));
}

EF_TEST(b3_svchost_value_from_ram) {
    EF_CHECK(tweak_on_raw(kSvcSplit, ram(16777216)) == std::optional<RegRaw>(dword_raw(16777216)));  // 16 GB
    EF_CHECK(tweak_on_text(kSvcSplit, ram(8388608)) == std::optional<std::string>("8388608"));        // 8 GB
    EF_CHECK(!tweak_on_raw(kSvcSplit, TweakRuntime{}));          // runtime yok
    EF_CHECK(!tweak_on_raw(kSvcSplit, ram(0)));                  // okunamadi/sifir
    EF_CHECK(!tweak_on_raw(kSvcSplit, ram(0x100000000ull)));     // DWORD'a sigmaz
    EF_CHECK(tweak_on_raw(kSvcSplit, ram(0xFFFFFFFFull)) == std::optional<RegRaw>(dword_raw(0xFFFFFFFFu)));
    EF_CHECK(!tweak_on_raw(kSvcSplit));                          // runtime'siz imza hesaplanan deger vermez
    // Sabit girdiler runtime'dan etkilenmez.
    EF_CHECK(tweak_on_raw(kMachineDword, ram(16777216)) == std::optional<RegRaw>(dword_raw(1)));
    EF_CHECK(tweak_on_text(kMachineSz, TweakRuntime{}) == std::optional<std::string>("High"));
}

EF_TEST(b3_svchost_admin_rejects_client_value) {
    const auto rt = ram(16777216);
    EF_CHECK(tweak_admin_allowed(&kSvcSplit, "16777216", rt));
    EF_CHECK(!tweak_admin_allowed(&kSvcSplit, "8388608", rt));   // istemcinin dayattigi deger
    EF_CHECK(!tweak_admin_allowed(&kSvcSplit, "016777216", rt) && !tweak_admin_allowed(&kSvcSplit, "16777216 ", rt));
    EF_CHECK(!tweak_admin_allowed(&kSvcSplit, "16777216", TweakRuntime{}));  // broker RAM okuyamadi
    EF_CHECK(!tweak_admin_allowed(&kSvcSplit, "", rt) && !tweak_admin_allowed(&kSvcSplit, ""));
    EF_CHECK(!tweak_admin_allowed(&kSvcSplit, "16777216"));     // runtime'siz imza hesaplanan girdiyi kabul etmez
    EF_CHECK(tweak_apply_allowed(&kSvcSplit, "16777216", rt) && !tweak_apply_allowed(&kSvcSplit, "8388608", rt));
    // Varsayilan runtime saglayicisi baglanmamis: bos runtime (fail-closed).
    EF_CHECK(!current_tweak_runtime().installed_ram_kb.has_value());
}

EF_TEST(b3_svchost_adapter_ignores_client_value_and_uses_own_runtime) {
    FakeUserRegistry u;
    FakeMachineRegistry m;
    TweakAdapter a(u, &m);
    a.set_runtime(ram(16777216));
    EF_CHECK(!a.apply_value(kSvcSplit, "8388608").ok() && m.writes == 0);  // istemci degeri reddedilir
    EF_CHECK(a.apply_value(kSvcSplit, "16777216").ok() && m.values[kSvcKey] == dword_raw(16777216));
    TweakAdapter none(u, &m);  // runtime yok (saglayici baglanmamis)
    m.values.clear();
    EF_CHECK(!none.apply_value(kSvcSplit, "16777216").ok() && !none.apply_entry(kSvcSplit).ok() && m.values.empty());
}

EF_TEST(b3_svchost_skip_when_already_higher) {
    const auto rt = ram(16777216);
    EF_CHECK(tweak_should_skip(kSvcSplit, dword_raw(33554432), rt));
    EF_CHECK(tweak_should_skip(kSvcSplit, dword_raw(16777216), rt));
    EF_CHECK(!tweak_should_skip(kSvcSplit, dword_raw(3670016), rt));  // 3.5 GB esigi
    EF_CHECK(!tweak_should_skip(kSvcSplit, std::nullopt, rt));
    EF_CHECK(!tweak_should_skip(kSvcSplit, dword_raw(33554432), TweakRuntime{}));  // hedef yok: kural uygulanmaz (oge zaten yok)
}

EF_TEST(b3_svchost_restore_deletes_when_absent_or_writes_previous) {
    {  // deger yoktu: uygula -> Kapali: silinir, anahtar (vardi) korunur
        FakeUserRegistry u;
        FakeMachineRegistry m;
        TweakAdapter a(u, &m);
        a.set_runtime(ram(16777216));
        m.keys.insert(kCtl);
        auto snap = a.capture_entry(kSvcSplit);
        EF_CHECK(snap.ok() && !snap.value().existed && snap.value().source_version.empty());
        EF_CHECK(a.apply_entry(kSvcSplit).ok() && m.values[kSvcKey] == dword_raw(16777216));
        EF_CHECK(a.read_back_entry(kSvcSplit).value() == std::optional<std::string>("16777216"));
        EF_CHECK(a.restore_entry(kSvcSplit, snap.value()).ok() && m.values.empty() && m.keys.count(kCtl) == 1);
    }
    {  // onceki 3670016: Kapali ayni degeri geri yazar
        FakeUserRegistry u;
        FakeMachineRegistry m;
        TweakAdapter a(u, &m);
        a.set_runtime(ram(8388608));
        m.keys.insert(kCtl);
        m.values[kSvcKey] = dword_raw(3670016);
        auto snap = a.capture_entry(kSvcSplit);
        EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "3670016");
        EF_CHECK(a.apply_entry(kSvcSplit).ok() && m.values[kSvcKey] == dword_raw(8388608));
        EF_CHECK(a.restore_entry(kSvcSplit, snap.value()).ok() && m.values[kSvcKey] == dword_raw(3670016));
    }
}

EF_TEST(b3_fixed_entries_unchanged_with_runtime) {
    const auto rt = ram(16777216);
    for (const auto& e : tweak_catalog())
        for (std::string_view v : {e.on_value, std::string_view("0"), std::string_view("1"), std::string_view(""), std::string_view("16777216")}) {
            EF_CHECK(tweak_admin_allowed(&e, v, rt) == tweak_admin_allowed(&e, v));
            EF_CHECK(tweak_apply_allowed(&e, v, rt) == tweak_admin_allowed_gated(&e, v, kPrioritySeparationItemEnabled));
        }
}
