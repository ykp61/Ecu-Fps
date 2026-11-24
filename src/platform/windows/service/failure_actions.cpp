#include "platform/windows/service/failure_actions.h"

#include <vector>

namespace ef::platform {
namespace c = ef::core::setup;

namespace {
c::FailureActionKind kind_of(SC_ACTION_TYPE t) {
    switch (t) {
        case SC_ACTION_RESTART: return c::FailureActionKind::Restart;
        case SC_ACTION_REBOOT: return c::FailureActionKind::Reboot;
        case SC_ACTION_RUN_COMMAND: return c::FailureActionKind::RunCommand;
        default: return c::FailureActionKind::None;
    }
}
}  // namespace

bool read_failure_config(SC_HANDLE svc, c::FailureConfig& out, DWORD* err) {
    out = {};
    DWORD n = 0;
    (void)QueryServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, nullptr, 0, &n);
    if (n == 0 || n > 64 * 1024) {
        if (err) *err = GetLastError();
        return false;
    }
    std::vector<BYTE> buf(n);
    if (!QueryServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, buf.data(), n, &n)) {
        if (err) *err = GetLastError();
        return false;
    }
    const auto* fa = reinterpret_cast<const SERVICE_FAILURE_ACTIONSW*>(buf.data());
    out.reset_seconds = fa->dwResetPeriod;
    out.action_count = fa->cActions;
    for (DWORD i = 0; i < fa->cActions && i < 3; ++i) out.actions[i] = {kind_of(fa->lpsaActions[i].Type), fa->lpsaActions[i].Delay};
    SERVICE_FAILURE_ACTIONS_FLAG flag{};
    DWORD m = 0;
    if (!QueryServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, reinterpret_cast<LPBYTE>(&flag), sizeof flag, &m)) {
        if (err) *err = GetLastError();
        return false;
    }
    out.on_noncrash_failures = flag.fFailureActionsOnNonCrashFailures != FALSE;
    return true;
}

bool write_required_failure_config(SC_HANDLE svc, DWORD* err) {
    const auto want = c::required_failure_config();
    SC_ACTION acts[3];
    for (int i = 0; i < 3; ++i) acts[i] = {SC_ACTION_RESTART, want.actions[i].delay_ms};
    SERVICE_FAILURE_ACTIONSW fa{};
    fa.dwResetPeriod = want.reset_seconds;
    fa.cActions = 3;
    fa.lpsaActions = acts;
    SERVICE_FAILURE_ACTIONS_FLAG flag{TRUE};
    if (!ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa) ||
        !ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag)) {
        if (err) *err = GetLastError();
        return false;
    }
    return true;
}

bool ensure_failure_config(SC_HANDLE svc, bool* repaired, DWORD* err) {
    if (repaired) *repaired = false;
    c::FailureConfig cur;
    if (read_failure_config(svc, cur, err) && c::failure_config_ok(cur)) return true;
    if (!write_required_failure_config(svc, err)) return false;
    if (repaired) *repaired = true;
    return read_failure_config(svc, cur, err) && c::failure_config_ok(cur);
}

}  // namespace ef::platform
