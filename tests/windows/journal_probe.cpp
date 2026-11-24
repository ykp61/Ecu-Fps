// REC-01 Windows testi:
//   write <dir>      -> gercek dizinde begin/persist/scan; 0 basari
//   blocked <dir>    -> yazma izni olmayan dizinde begin basarisiz olmali; 0 = dogru davranis
//   machine          -> makine journal dizini: yukseltmesizse acik Denied, yukseltilmisse olusturma
#include <windows.h>

#include <cstdio>

#include "core/recovery/journal.h"
#include "platform/windows/elevation.h"
#include "platform/windows/journal_fs.h"

using namespace ef;

namespace {
core::recovery::JournalTx tx_with(const std::string& id) {
    core::recovery::JournalTx tx;
    tx.id = id;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    core::recovery::JournalStep s;
    s.op = contracts::SettingOperation{contracts::OperationId::PowerModeSet, contracts::OperationScope::Machine,
                                       "overlay", "best_performance", false, false};
    tx.steps.push_back(s);
    return tx;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const std::wstring mode = argv[1];
    if ((mode == L"write" || mode == L"blocked") && argc >= 3) {
        platform::WinJournalFs fs(argv[2]);
        core::recovery::JournalStore store(fs, platform::machine_id());
        auto tx = tx_with(platform::random_nonce());
        const auto s = store.begin(tx);
        if (mode == L"blocked") {
            std::printf("begin on read-only dir: %s\n", s.ok() ? "ok (WRONG)" : s.error().detail.c_str());
            return s.ok() ? 1 : 0;
        }
        if (!s.ok()) return 3;
        tx.state = contracts::TxState::Applying;
        if (!store.persist(tx).ok()) return 4;
        const auto l = store.scan();
        if (l.transactions.size() != 1 || l.transactions[0].state != contracts::TxState::Applying) return 5;
        std::printf("journal ok rev=%llu machine=%s\n", static_cast<unsigned long long>(l.transactions[0].revision),
                    platform::machine_id().substr(0, 8).c_str());
        return 0;
    }
    if (mode == L"machine") {
        auto d = platform::machine_journal_dir(true);
        const bool elevated = platform::is_process_elevated();
        if (d.ok()) {
            std::printf("machine journal dir available (elevated=%d)\n", elevated);
            return 0;
        }
        std::printf("machine journal: %s (elevated=%d)\n", d.error().detail.c_str(), elevated);
        // Yukseltmesiz: acik engel (Denied) beklenir; sessiz basari veya belirsiz hata degil.
        return (!elevated && d.code() == contracts::ErrorCode::Denied) ? 0 : 6;
    }
    return 2;
}
