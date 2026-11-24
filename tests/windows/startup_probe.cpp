// WIN-03 Windows testi.
//   list                 -> gercek baslangic girdilerini listeler (salt okuma; ctest)
//   roundtrip <journal>  -> KENDI sahte HKCU Run girdisini (Unicode ad) olusturur, calisan sureci olusturur,
//                           devre disi birakir/geri alir, surecin hala yasadigini ve kaydin iz birakmadigini
//                           dogrular. Yalniz EF_ALLOW_MUTATION=1.
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "core/recovery/engine.h"
#include "platform/windows/adapters/win_startup.h"
#include "platform/windows/elevation.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/win_env.h"

using namespace ef;
namespace cs = core::adapters::startup;

namespace {
constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApproved[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

int list_all() {
    platform::adapters::WinStartupApi api;
    int total = 0;
    for (auto l : {cs::Location::UserRun, cs::Location::MachineRun64, cs::Location::MachineRun32, cs::Location::UserFolder,
                   cs::Location::CommonFolder}) {
        auto e = api.list(l);
        if (!e.ok()) {
            std::printf("%s: error %s\n", cs::to_string(l).data(), e.error().detail.c_str());
            continue;
        }
        for (const auto& x : e.value()) {
            ++total;
            std::printf("%s | %s | %s\n", cs::to_string(l).data(), x.name.c_str(), x.approval == cs::Approval::Disabled ? "disabled" : "enabled");
        }
    }
    std::printf("total=%d\n", total);
    return 0;
}

int roundtrip(const wchar_t* dir) {
    char flag[8] = {};
    if (GetEnvironmentVariableA("EF_ALLOW_MUTATION", flag, sizeof flag) == 0 || flag[0] != '1') return 9;
    const std::string name = "EcuFps Test Girdisi Ğüşİ";
    const std::wstring wname = platform::to_wide(name);
    // 1) Sahte Run girdisi (zararsiz komut; yeniden baslatma yapilmaz)
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRun, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return 10;
    const std::wstring cmd = L"cmd.exe /c exit";
    RegSetValueExW(k, wname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * 2));
    RegCloseKey(k);
    // 2) "Calisan uygulama" temsili surec
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    wchar_t pc[] = L"cmd.exe /c ping -n 20 127.0.0.1 >nul";
    CreateProcessW(nullptr, pc, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    int rc = 0;
    {
        platform::adapters::WinStartupApi api;
        cs::StartupAdapter adapter(api);
        platform::WinJournalFs fs(dir);
        core::recovery::JournalStore store(fs, platform::machine_id());
        core::recovery::AdapterRegistry reg;
        reg.add(contracts::OperationId::StartupRunEntry, &adapter);
        core::recovery::TransactionEngine eng(store, reg);
        core::recovery::JournalTx tx;
        tx.id = platform::random_nonce();
        core::recovery::JournalStep s;
        s.op = {contracts::OperationId::StartupRunEntry, contracts::OperationScope::CurrentUser, cs::format_target(cs::Location::UserRun, name),
                "disabled", false, false};
        tx.steps.push_back(s);
        auto active = eng.apply(tx);
        if (!active.ok()) { std::printf("apply: %s\n", active.error().detail.c_str()); rc = 11; }
        else {
            auto raw = api.get_raw(cs::Location::UserRun, name);
            std::printf("approved after disable=%s\n", raw.ok() && raw.value() ? raw.value()->c_str() : "(none)");
            if (!raw.ok() || !raw.value() || cs::approval_from_raw(*raw.value()) != cs::Approval::Disabled) rc = 12;
            if (WaitForSingleObject(pi.hProcess, 0) != WAIT_TIMEOUT) rc = 13;  // calisan surec oldurulmemeli
            auto rb = eng.rollback(active.value());
            auto after = api.get_raw(cs::Location::UserRun, name);
            std::printf("rollback=%s approved after restore=%s\n", contracts::to_string(rb.value().state).data(),
                        after.ok() && after.value() ? after.value()->c_str() : "(none)");
            if (rb.value().state != contracts::TxState::Restored || !after.ok() || after.value()) rc = 14;
            auto ex = api.exists(cs::Location::UserRun, name);
            if (!ex.ok() || !ex.value()) rc = 15;  // Run kaydi silinmemis olmali
        }
    }
    // 3) Temizlik: yalniz kendi girdimiz
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRun, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { RegDeleteValueW(k, wname.c_str()); RegCloseKey(k); }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApproved, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { RegDeleteValueW(k, wname.c_str()); RegCloseKey(k); }
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    std::printf("roundtrip rc=%d\n", rc);
    return rc;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    if (std::wcscmp(argv[1], L"list") == 0) return list_all();
    if (std::wcscmp(argv[1], L"roundtrip") == 0 && argc >= 3) return roundtrip(argv[2]);
    return 2;
}
