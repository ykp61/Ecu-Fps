#include "core/adapters/windows/fast_startup.h"
#include "ef_test.h"

#include <utility>

using namespace ef::core::adapters::windows;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;

namespace {
SettingOperation op(const char* target = "faststartup:hiberboot", const char* v = "0") {
    return SettingOperation{OperationId::FastStartupSet, OperationScope::Machine, target, v, false, false};
}
}  // namespace

EF_TEST(fast_startup_apply_readback_restore_exact) {
    FakeFastStartup api;
    FastStartupAdapter a(api);
    api.value = 1;
    auto snap = a.capture(op());
    EF_CHECK(snap.ok() && snap.value().existed && snap.value().old_value == "1");
    EF_CHECK(a.apply(op()).ok());
    EF_CHECK(api.value == std::optional<std::uint32_t>(0));
    EF_CHECK(a.read_back(op()).value() == std::optional<std::string>("0"));
    EF_CHECK(a.restore(op(), snap.value()).ok());
    EF_CHECK(api.value == std::optional<std::uint32_t>(1));  // birebir
}

EF_TEST(fast_startup_missing_value_restored_by_delete) {
    FakeFastStartup api;
    FastStartupAdapter a(api);
    auto snap = a.capture(op());
    EF_CHECK(snap.ok() && !snap.value().existed);
    EF_CHECK(a.apply(op()).ok() && api.value == std::optional<std::uint32_t>(0));
    EF_CHECK(a.restore(op(), snap.value()).ok());
    EF_CHECK(!api.value.has_value() && api.removes == 1);  // varsayilan uydurulmadi
}

EF_TEST(fast_startup_nonstandard_old_value_restored) {
    FakeFastStartup api;
    FastStartupAdapter a(api);
    api.value = 7;  // 0/1 disi kayit (baska arac): yine birebir geri yazilir
    auto snap = a.capture(op());
    EF_CHECK(snap.ok() && snap.value().old_value == "7");
    EF_CHECK(a.apply(op()).ok());
    EF_CHECK(a.read_back(op()).value() == std::optional<std::string>("0"));
    EF_CHECK(a.restore(op(), snap.value()).ok());
    EF_CHECK(api.value == std::optional<std::uint32_t>(7));
}

EF_TEST(fast_startup_rejects_wrong_target_value_and_failure) {
    FakeFastStartup api;
    FastStartupAdapter a(api);
    EF_CHECK(!a.capture(op("faststartup:other")).ok());
    EF_CHECK(!a.apply(op("faststartup:other")).ok());
    auto wrong_id = op();
    wrong_id.id = OperationId::HagsSet;
    EF_CHECK(!a.capture(wrong_id).ok());
    EF_CHECK(!a.apply(op("faststartup:hiberboot", "2")).ok());
    EF_CHECK(api.writes == 0);
    api.fail_write = true;
    EF_CHECK(!a.apply(op()).ok() && !a.restore(op(), ef::core::recovery::Snapshot{}).ok());
    EF_CHECK(a.read_back(op()).ok() && !a.read_back(op()).value().has_value());
}
