// WIN-01 Windows testi.
//   read                 -> gercek mod/plan/plan listesi + capability (salt okuma; ctest'te)
//   roundtrip <journal>  -> GERCEK uygula -> geri oku -> geri al -> ilk degerle karsilastir.
//                           Yalniz EF_ALLOW_MUTATION=1 ortam degiskeniyle calisir (ctest/CI'da calismaz).
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "core/recovery/engine.h"
#include "platform/windows/adapters/win_power.h"
#include "platform/windows/elevation.h"
#include "platform/windows/journal_fs.h"

using namespace ef;
namespace cw = core::adapters::windows;

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    platform::adapters::WinPowerApi api;
    cw::PowerModeAdapter mode(api);
    cw::PowerSchemeAdapter scheme(api);
    const auto m0 = api.get_ac_mode();
    const auto s0 = api.get_active_scheme();
    const auto list = api.list_schemes();
    std::printf("mode=%s scheme=%s schemes=%zu\n", m0.ok() ? m0.value().c_str() : m0.error().detail.c_str(),
                s0.ok() ? s0.value().c_str() : s0.error().detail.c_str(), list.ok() ? list.value().size() : 0);
    // Gap 11: salt okuma DC (pil) modu ve pil varligi.
    const auto dc0 = api.get_dc_mode();
    const auto bat = api.has_battery();
    std::printf("dc_mode=%s battery=%s\n", dc0.ok() ? dc0.value().c_str() : dc0.error().detail.c_str(),
                bat.ok() ? (bat.value() ? "yes" : "no") : bat.error().detail.c_str());
    const auto cm = mode.capability();
    const auto cs = scheme.capability();
    std::printf("W01=%s (%s) W02=%s (%s)\n", contracts::to_string(cm.state).data(), cm.reason.c_str(),
                contracts::to_string(cs.state).data(), cs.reason.c_str());
    if (std::wcscmp(argv[1], L"read") == 0) return (s0.ok() && list.ok()) ? 0 : 1;

    if (std::wcscmp(argv[1], L"roundtrip") == 0 && argc >= 3) {
        char flag[8] = {};
        if (GetEnvironmentVariableA("EF_ALLOW_MUTATION", flag, sizeof flag) == 0 || flag[0] != '1') {
            std::printf("mutation not allowed (set EF_ALLOW_MUTATION=1)\n");
            return 9;
        }
        platform::WinJournalFs fs(argv[2]);
        core::recovery::JournalStore store(fs, platform::machine_id());
        core::recovery::AdapterRegistry reg;
        reg.add(contracts::OperationId::PowerModeSet, &mode);
        reg.add(contracts::OperationId::PowerSchemeSet, &scheme);
        core::recovery::TransactionEngine eng(store, reg);
        core::recovery::JournalTx tx;
        tx.id = platform::random_nonce();
        core::recovery::JournalStep st;
        if (cm.state == contracts::SupportState::Supported) {
            // Dengeli plandayiz: guc modunu "en iyi performans"a al
            st.op = contracts::SettingOperation{contracts::OperationId::PowerModeSet, contracts::OperationScope::Machine, "power:mode:ac",
                                                std::string(cw::kModeBestPerformance), false, false};
        } else {
            // Baska plan etkin: Dengeli plana gec (mevcut bir plan) ve geri don
            st.op = contracts::SettingOperation{contracts::OperationId::PowerSchemeSet, contracts::OperationScope::Machine,
                                                "power:scheme:active", std::string(cw::kSchemeBalanced), false, false};
        }
        tx.steps.push_back(st);
        auto active = eng.apply(tx);
        if (!active.ok()) {
            std::printf("apply failed: %s\n", active.error().detail.c_str());
            return 3;
        }
        const auto mid = st.op.id == contracts::OperationId::PowerModeSet ? api.get_ac_mode() : api.get_active_scheme();
        std::printf("applied %s -> now %s\n", st.op.target.c_str(), mid.ok() ? mid.value().c_str() : "?");
        auto rb = eng.rollback(active.value());
        const auto m1 = api.get_ac_mode();
        const auto s1 = api.get_active_scheme();
        const bool same = s1.ok() && s0.ok() && s1.value() == s0.value() && (!m0.ok() || (m1.ok() && m1.value() == m0.value()));
        std::printf("rolled back: state=%s restored_to_initial=%d\n", contracts::to_string(rb.value().state).data(), same ? 1 : 0);
        return (rb.ok() && rb.value().state == contracts::TxState::Restored && same && mid.ok() && mid.value() == st.op.new_value) ? 0 : 4;
    }
    return 2;
}
