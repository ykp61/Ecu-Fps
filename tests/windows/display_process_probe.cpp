// WIN-02 Windows testi.
//   process            -> gercek cocuk surecte oncelik + guc kisitlamasi uygula/geri al, PID reuse reddi (ctest)
//   display            -> gercek ekran modlari, en yuksek Hz, gecersiz mod CDS_TEST reddi (salt okuma, ctest)
//   vfx <journal>      -> GERCEK gorsel efekt turu (yalniz EF_ALLOW_MUTATION=1)
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "core/recovery/engine.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/elevation.h"
#include "platform/windows/journal_fs.h"

using namespace ef;
namespace cw = core::adapters::windows;

namespace {
int process_test() {
    // Zararsiz bekleyen cocuk surec
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"cmd.exe /c ping -n 30 127.0.0.1 >nul";
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 10;
    int rc = 0;
    platform::adapters::WinProcessApi api;
    cw::ProcessAdapter adapter(api);
    const cw::ProcessId id{pi.dwProcessId, platform::adapters::process_creation_time(pi.dwProcessId)};
    const DWORD initial = GetPriorityClass(pi.hProcess);  // CI'da ust surecten miras farkli olabilir
    core::recovery::MemoryJournalFs mem;
    core::recovery::JournalStore store(mem, "m");
    core::recovery::AdapterRegistry reg;
    reg.add(contracts::OperationId::ProcessPriority, &adapter);
    reg.add(contracts::OperationId::ProcessPowerThrottling, &adapter);
    core::recovery::TransactionEngine eng(store, reg);
    core::recovery::JournalTx tx;
    tx.id = platform::random_nonce();
    core::recovery::JournalStep a, b;
    a.op = {contracts::OperationId::ProcessPriority, contracts::OperationScope::Process, cw::format_process_target({id, true}),
            "above_normal", false, false};
    b.op = {contracts::OperationId::ProcessPowerThrottling, contracts::OperationScope::Process,
            cw::format_process_target({id, false}), "off", false, false};
    tx.steps = {a, b};
    auto active = eng.apply(tx);
    if (!active.ok()) { std::printf("apply: %s\n", active.error().detail.c_str()); rc = 11; }
    else {
        const DWORD pc = GetPriorityClass(pi.hProcess);
        std::printf("child priority=0x%lx (ABOVE_NORMAL=0x%x)\n", pc, ABOVE_NORMAL_PRIORITY_CLASS);
        if (pc != ABOVE_NORMAL_PRIORITY_CLASS) rc = 12;
        auto rb = eng.rollback(active.value());
        if (!rb.ok() || rb.value().state != contracts::TxState::Restored || GetPriorityClass(pi.hProcess) != initial) rc = 13;
        std::printf("after rollback priority=0x%lx\n", GetPriorityClass(pi.hProcess));
    }
    // PID yeniden kullanimi: ayni PID, yanlis olusturulma zamani -> hedef reddedilir
    const cw::ProcessId stale{pi.dwProcessId, id.creation - 10};
    if (api.set_priority(stale, cw::PriorityClass::AboveNormal).ok()) rc = 14;
    if (GetPriorityClass(pi.hProcess) != initial) rc = 15;
    // Korumali surec (System, PID 4): zorlanmaz
    if (api.set_priority({4, 1}, cw::PriorityClass::AboveNormal).ok()) rc = 16;
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    // Bitmis surec: geri alma guvenli no-op
    if (!adapter.target_gone(a.op)) rc = 17;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::printf("process test rc=%d\n", rc);
    return rc;
}

int display_test() {
    platform::adapters::WinDisplayApi api;
    auto cur = api.current(cw::DisplayRef{});
    auto modes = api.modes(cw::DisplayRef{});
    if (!cur.ok() || !modes.ok()) {
        std::printf("no display: %s\n", cur.ok() ? modes.error().detail.c_str() : cur.error().detail.c_str());
        return 0;  // gorunur masaustu olmayan ortam (CI) kontrollu
    }
    const auto best = cw::highest_refresh(modes.value(), cur.value());
    std::printf("display0 %ux%u@%u modes=%zu highest_same_res=%u\n", cur.value().width, cur.value().height, cur.value().hz,
                modes.value().size(), best.value_or(0));
    // Gecersiz mod: surucu testinde reddedilmeli; mevcut mod degismemeli
    const cw::DisplayMode bogus{cur.value().width, cur.value().height, 997};
    if (api.test(cw::DisplayRef{}, bogus).ok()) return 20;
    auto after = api.current(cw::DisplayRef{});
    if (!after.ok() || !(after.value() == cur.value())) return 21;
    return 0;
}

int vfx_test(const wchar_t* dir) {
    char flag[8] = {};
    if (GetEnvironmentVariableA("EF_ALLOW_MUTATION", flag, sizeof flag) == 0 || flag[0] != '1') return 9;
    platform::adapters::WinVisualEffectsApi api;
    cw::VisualEffectAdapter adapter(api);
    const auto before = api.get(cw::VisualEffect::MenuAnimation);
    if (!before.ok()) return 30;
    platform::WinJournalFs fs(dir);
    core::recovery::JournalStore store(fs, platform::machine_id());
    core::recovery::AdapterRegistry reg;
    reg.add(contracts::OperationId::VisualEffectsSet, &adapter);
    core::recovery::TransactionEngine eng(store, reg);
    core::recovery::JournalTx tx;
    tx.id = platform::random_nonce();
    core::recovery::JournalStep s;
    s.op = {contracts::OperationId::VisualEffectsSet, contracts::OperationScope::CurrentUser, "vfx:menu_animation",
            before.value() ? "0" : "1", false, false};
    tx.steps.push_back(s);
    auto active = eng.apply(tx);
    if (!active.ok()) return 31;
    const auto mid = api.get(cw::VisualEffect::MenuAnimation);
    auto rb = eng.rollback(active.value());
    const auto after = api.get(cw::VisualEffect::MenuAnimation);
    std::printf("menu_animation before=%d applied=%d after=%d state=%s\n", before.value() ? 1 : 0,
                mid.ok() ? (mid.value() ? 1 : 0) : -1, after.ok() ? (after.value() ? 1 : 0) : -1,
                contracts::to_string(rb.value().state).data());
    return (mid.ok() && mid.value() != before.value() && after.ok() && after.value() == before.value()) ? 0 : 32;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    if (std::wcscmp(argv[1], L"process") == 0) return process_test();
    if (std::wcscmp(argv[1], L"display") == 0) return display_test();
    if (std::wcscmp(argv[1], L"vfx") == 0 && argc >= 3) return vfx_test(argv[2]);
    return 2;
}
