// REC-06 Windows testi (gecici dizinler; gercek AppData/Run'a dokunulmaz):
//   prepare <root>    -> hedefleri olustur, islemi uygula (Active), test autostart degerini yaz
//   uninstall <root>  -> geri al + sil; 0 basari, 5 kismi basarisizlik (veri korundu)
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "core/recovery/uninstall.h"
#include "file_adapter.h"
#include "platform/windows/data_remover.h"
#include "platform/windows/elevation.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/single_instance.h"

using namespace ef;
namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring mode = argv[1];
    const fs::path root = argv[2];
    const fs::path data = root / L"EcuFps";  // sahte %LOCALAPPDATA%\EcuFps
    const std::wstring run_key = L"Software\\EcuFpsTest\\Run";
    fs::create_directories(data / "journal");
    ef_test::FileAdapter adapter(root / "targets");
    core::recovery::AdapterRegistry reg;
    reg.add(contracts::OperationId::GameModeSet, &adapter);
    platform::WinJournalFs wfs(data / "journal");
    core::recovery::JournalStore store(wfs, platform::machine_id());
    core::recovery::TransactionEngine eng(store, reg);
    if (mode == L"prepare") {
        std::ofstream(adapter.dir / "a.val", std::ios::binary) << "old-a";
        core::recovery::JournalTx tx;
        tx.id = platform::random_nonce();
        tx.target_sid = "S-1-5-21-1-2-3-1001";
        core::recovery::JournalStep s;
        s.op = contracts::SettingOperation{contracts::OperationId::GameModeSet, contracts::OperationScope::CurrentUser,
                                           "a", "new-a", false, false};
        tx.steps.push_back(s);
        auto r = eng.apply(tx);
        if (!r.ok() || r.value().state != contracts::TxState::Active) return 3;
        if (!platform::set_autostart(true, run_key).ok()) return 4;
        return 0;
    }
    if (mode == L"uninstall") {
        platform::FsDataRemover rm(data, root / L"EcuFps-machine-absent", run_key);
        const auto res = core::recovery::run_uninstall({{"user", &store, &eng}}, rm);
        for (const auto& n : res.notes) std::printf("note: %s\n", n.c_str());
        return res.data_removed ? 0 : 5;
    }
    return 2;
}
