#include "core/setup/service_policy.h"
#include "ef_test.h"

using namespace ef::core::setup;

EF_TEST(service_policy_required_is_three_restarts_short_reset) {
    const auto c = required_failure_config();
    EF_CHECK(failure_config_ok(c));
    EF_CHECK(c.action_count == 3 && c.reset_seconds <= 3600 && c.on_noncrash_failures);
    for (const auto& a : c.actions) EF_CHECK(a.kind == FailureActionKind::Restart);
}

EF_TEST(service_policy_rejects_old_field_config) {  // 2026-11-17 sahada: 2 RESTART + NONE, sifirlama 24 saat
    auto c = required_failure_config();
    c.actions[2] = {FailureActionKind::None, 0};
    EF_CHECK(!failure_config_ok(c));
    c = required_failure_config();
    c.reset_seconds = 86400;
    EF_CHECK(!failure_config_ok(c));
    c = required_failure_config();
    c.on_noncrash_failures = false;
    EF_CHECK(!failure_config_ok(c));
    c = required_failure_config();
    c.action_count = 2;
    EF_CHECK(!failure_config_ok(c));
    c = required_failure_config();
    c.reset_seconds = 0;
    EF_CHECK(!failure_config_ok(c));
    c = required_failure_config();
    c.actions[0].kind = FailureActionKind::Reboot;
    EF_CHECK(!failure_config_ok(c));
}
