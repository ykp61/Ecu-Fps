// T13 (ADR-035): yeniden kaydedilebilir medya uygulamasi kaldirma. Sahte AppxApi; gercek paket YOKTUR.
#include <string>

#include "core/adapters/windows/appx_remove.h"
#include "core/broker/admin.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;
namespace cw = ef::core::adapters::windows;

namespace {
const std::string kFam = "Microsoft.BingNews_8wekyb3d8bbwe";
const std::string kFull = "Microsoft.BingNews_4.55.1_x64__8wekyb3d8bbwe";

SettingOperation op_for(const std::string& family, const char* v = "removed", OperationScope sc = OperationScope::CurrentUser) {
    return SettingOperation{OperationId::AppxPackageRemove, sc, cw::appx_target_for_family(family), v, false, false};
}

struct AppxRig {
    cw::FakeAppxApi api;
    cw::AppxRemoveAdapter adapter{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "u"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    AppxRig() {
        reg.add(OperationId::AppxPackageRemove, &adapter);
        api.installed.emplace_back(kFam, kFull);
        api.provisioned.push_back(kFam);
    }
    JournalTx tx(const std::string& family = kFam) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = op_for(family);
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(appx_op_id_roundtrip_is_last_and_target_format) {
    EF_CHECK(to_string(OperationId::AppxPackageRemove) == "appx_package_remove");
    EF_CHECK(parse_operation_id("appx_package_remove") == OperationId::AppxPackageRemove);
    EF_CHECK(parse_operation_id("game_priority_hint") == OperationId::GamePriorityHint);  // onceki adlar degismedi
    static_assert(static_cast<int>(OperationId::AppxPackageRemove) == static_cast<int>(OperationId::GamePriorityHint) + 1,
                  "AppxPackageRemove enum sonuna eklenmeli");
    EF_CHECK(cw::appx_target_for_family(kFam) == "appx:" + kFam);
    EF_CHECK(cw::appx_family_from_target("appx:" + kFam) == std::optional<std::string>(kFam));
    EF_CHECK(!cw::appx_family_from_target("appx:").has_value());
    EF_CHECK(!cw::appx_family_from_target("appx:Microsoft.WindowsStore_8wekyb3d8bbwe").has_value());  // listede yok
    EF_CHECK(!cw::appx_family_from_target("appx:microsoft.bingnews_8wekyb3d8bbwe").has_value());      // tek gosterim: buyuk/kucuk harf duyarli
    EF_CHECK(cw::appx_target_for_family("Microsoft.MicrosoftEdge.Stable_8wekyb3d8bbwe").empty());       // Edge asla listede degil
}

EF_TEST(appx_compiled_list_is_six_and_never_contains_edge_onedrive_or_store) {
    EF_CHECK(cw::appx_packages().size() == 6);
    for (const auto& p : cw::appx_packages()) {
        const std::string f(p.family);
        EF_CHECK(f.find("Edge") == std::string::npos && f.find("OneDrive") == std::string::npos && f.find("WindowsStore") == std::string::npos);
        EF_CHECK(!p.display_name.empty() && f.find('_') != std::string::npos);
    }
}

EF_TEST(appx_round_trip_removes_for_user_then_reregisters) {
    AppxRig r;
    auto a = r.engine.apply(r.tx());
    EF_CHECK(a.ok() && a.value().state == TxState::Active);
    EF_CHECK(r.api.installed.empty() && r.api.removes == 1);
    EF_CHECK(a.value().steps[0].snapshot.existed && a.value().steps[0].snapshot.raw == kFull);  // yakalanan tam ad
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.api.registers == 1 && r.api.installed.size() == 1 && r.api.installed[0].first == kFam);
}

EF_TEST(appx_not_restorable_capture_and_apply_refused_before_any_removal) {
    AppxRig r;
    r.api.provisioned.clear();  // yalniz kullanici icin yuklu: yeniden kayit garanti degil
    EF_CHECK(!r.adapter.capture(op_for(kFam)).ok());
    EF_CHECK(!r.adapter.apply(op_for(kFam)).ok());
    EF_CHECK(!r.engine.apply(r.tx()).ok());
    EF_CHECK(r.api.removes == 0 && r.api.installed.size() == 1);
    r.api.provisioned.push_back(kFam);
    r.api.fail_restorable = true;  // okunamadi: uydurma "geri alinabilir" yok
    EF_CHECK(!r.adapter.capture(op_for(kFam)).ok());
    EF_CHECK(!r.adapter.apply(op_for(kFam)).ok());
    EF_CHECK(r.api.removes == 0);
}

EF_TEST(appx_not_installed_is_refused) {
    AppxRig r;
    r.api.installed.clear();
    EF_CHECK(!r.adapter.capture(op_for(kFam)).ok());
    EF_CHECK(!r.adapter.apply(op_for(kFam)).ok());
    EF_CHECK(r.api.removes == 0);
}

EF_TEST(appx_family_outside_compiled_list_wrong_scope_and_value_rejected) {
    AppxRig r;
    SettingOperation bad{OperationId::AppxPackageRemove, OperationScope::CurrentUser, "appx:Microsoft.WindowsStore_8wekyb3d8bbwe", "removed", false, false};
    r.api.installed.emplace_back("Microsoft.WindowsStore_8wekyb3d8bbwe", "Microsoft.WindowsStore_1_x64__8wekyb3d8bbwe");
    r.api.provisioned.push_back("Microsoft.WindowsStore_8wekyb3d8bbwe");
    EF_CHECK(!r.adapter.capture(bad).ok() && !r.adapter.apply(bad).ok() && !r.adapter.read_back(bad).ok());
    EF_CHECK(!r.adapter.apply(op_for(kFam, "installed")).ok());
    EF_CHECK(!r.adapter.apply(op_for(kFam, "removed", OperationScope::Machine)).ok());
    EF_CHECK(!r.adapter.capture(op_for(kFam, "removed", OperationScope::Machine)).ok());
    EF_CHECK(r.api.removes == 0 && r.api.installed.size() == 2);
}

EF_TEST(appx_broker_rejects_appx_op_even_if_machine_scoped) {
    // Kullanici kapsami ve UAC'siz: yukseltilmis brokerin izin listesinde YOKTUR.
    EF_CHECK(!ef::core::broker::admin_operation_allowed(op_for(kFam)));
    EF_CHECK(!ef::core::broker::admin_operation_allowed(op_for(kFam, "removed", OperationScope::Machine)));
}

EF_TEST(appx_restore_failure_is_reported_not_hidden) {
    AppxRig r;
    auto a = r.engine.apply(r.tx());
    EF_CHECK(a.ok());
    r.api.fail_register = true;  // yeniden kayit basarisiz
    auto rb = r.engine.rollback(a.value());
    EF_CHECK(rb.ok() ? rb.value().state != TxState::Restored : true);  // Restored DENMEZ (RecoveryRequired yolu)
    EF_CHECK(r.api.installed.empty());
    // Kayit "basarili" doner ama paket kurulmaz: yine basari sayilmaz.
    AppxRig r2;
    auto a2 = r2.engine.apply(r2.tx());
    EF_CHECK(a2.ok());
    r2.api.register_is_noop = true;
    auto rb2 = r2.engine.rollback(a2.value());
    EF_CHECK(rb2.ok() ? rb2.value().state != TxState::Restored : true);
    EF_CHECK(r2.api.registers == 1 && r2.api.installed.empty());
    // Sonradan duzelirse ayni kayit yeniden denenebilir ve geri alma tamamlanir.
    r2.api.register_is_noop = false;
    const auto again = r2.engine.rollback(a2.value());
    EF_CHECK(again.ok() && again.value().state == TxState::Restored && r2.api.installed.size() == 1);
}

EF_TEST(appx_already_installed_at_restore_is_not_registered_again) {
    AppxRig r;
    auto a = r.engine.apply(r.tx());
    EF_CHECK(a.ok());
    r.api.installed.emplace_back(kFam, kFull);  // kullanici Store'dan kendisi kurdu
    SettingOperation op = op_for(kFam);
    EF_CHECK(r.adapter.restore(op, a.value().steps[0].snapshot).ok());
    EF_CHECK(r.api.registers == 0);
}
