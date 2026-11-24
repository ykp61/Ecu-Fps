// REC-03 Windows testi: gercek surec olumu ile cokme sonrasi uzlastirma.
//   setup <dir>                 -> hedef dosyalari ilk degerlerle olustur
//   apply <dir> <crash_at>      -> N. journal yaziminda TerminateProcess (gercek olum); -1: cokmeden tamamla
//   reconcile <dir>             -> yeni surecte uzlastir; hedefler ilk halinde degilse 1
// Hedefler <dir>\targets\<ad>.val dosyalari (yok = deger yok).
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "core/recovery/reconcile.h"
#include "file_adapter.h"
#include "platform/windows/journal_fs.h"

using namespace ef;
namespace fs = std::filesystem;
using core::recovery::Snapshot;

namespace {
const std::map<std::string, std::string> kOriginal{{"a", "old-a"}, {"b", "old-b"}};  // c yok

class DyingFs final : public core::recovery::JournalFs {
public:
    DyingFs(platform::WinJournalFs& in, int crash_at) : inner(in), at(crash_at) {}
    contracts::Status write_atomic(const std::string& n, const std::string& b) override {
        if (at >= 0 && ++count > at) TerminateProcess(GetCurrentProcess(), 77);  // gercek olum
        return inner.write_atomic(n, b);
    }
    contracts::Result<std::string> read(const std::string& n) override { return inner.read(n); }
    contracts::Result<std::vector<std::string>> list() override { return inner.list(); }
    contracts::Status remove(const std::string& n) override { return inner.remove(n); }
    platform::WinJournalFs& inner;
    int at, count = 0;
};

core::recovery::JournalTx tx3() {
    core::recovery::JournalTx tx;
    tx.id = "0123456789abcdef0123456789abcdef";
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    for (const char* t : {"a", "b", "c"}) {
        core::recovery::JournalStep s;
        s.op = contracts::SettingOperation{contracts::OperationId::GameModeSet, contracts::OperationScope::CurrentUser, t,
                                           std::string("new-") + t, false, false};
        tx.steps.push_back(s);
    }
    return tx;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring mode = argv[1];
    const fs::path dir = argv[2];
    fs::create_directories(dir / "journal");
    ef_test::FileAdapter adapter(dir / "targets");
    core::recovery::AdapterRegistry reg;
    reg.add(contracts::OperationId::GameModeSet, &adapter);
    platform::WinJournalFs wfs(dir / "journal");
    if (mode == L"setup") {
        for (const auto& [k, v] : kOriginal) std::ofstream(adapter.dir / (k + ".val"), std::ios::binary) << v;
        return 0;
    }
    if (mode == L"apply" && argc >= 4) {
        DyingFs dfs(wfs, static_cast<int>(std::wcstol(argv[3], nullptr, 10)));
        core::recovery::JournalStore st(dfs, "m");
        core::recovery::TransactionEngine eng(st, reg);
        auto r = eng.apply(tx3());
        return r.ok() ? 0 : 3;
    }
    if (mode == L"reconcile") {
        core::recovery::JournalStore st(wfs, "m");
        core::recovery::TransactionEngine eng(st, reg);
        const auto rep = core::recovery::reconcile(st, eng);
        for (const char* t : {"a", "b", "c"}) {
            const contracts::SettingOperation op{contracts::OperationId::GameModeSet, contracts::OperationScope::CurrentUser, t, "", false, false};
            const auto v = adapter.read_back(op).value();
            const auto it = kOriginal.find(t);
            const bool ok = it == kOriginal.end() ? !v.has_value() : (v && *v == it->second);
            if (!ok) return 1;
        }
        std::printf("restored=%zu active=%zu recovery=%zu corrupt=%zu\n", rep.restored.size(), rep.active.size(),
                    rep.recovery_required.size(), rep.corrupt.size());
        return rep.recovery_required.empty() && rep.corrupt.empty() ? 0 : 4;
    }
    return 2;
}
