#include "platform/windows/recovery_host.h"

#include "platform/windows/journal_fs.h"

namespace ef::platform {

std::vector<core::recovery::JournalScope> RecoveryHost::scopes() const {
    std::vector<core::recovery::JournalScope> s;
    if (user_store) s.push_back({"user", user_store.get(), user_engine.get()});
    if (machine.store) s.push_back({"machine", machine.store.get(), machine.engine.get()});
    if (startup_store) s.push_back({"startup", startup_store.get(), startup_engine.get()});
    if (fixes_store) s.push_back({"fixes", fixes_store.get(), fixes_engine.get()});
    if (machine_fixes.store) s.push_back({"machine_fixes", machine_fixes.store.get(), machine_fixes.engine.get()});
    return s;
}

std::vector<core::recovery::JournalScope> RecoveryHost::user_scopes() const {
    std::vector<core::recovery::JournalScope> s;
    if (user_store) s.push_back({"user", user_store.get(), user_engine.get()});
    if (startup_store) s.push_back({"startup", startup_store.get(), startup_engine.get()});
    if (fixes_store) s.push_back({"fixes", fixes_store.get(), fixes_engine.get()});
    return s;
}

namespace {
// Var olan dizin icin salt okuma JournalFs; yoksa nullptr (olusturulmaz: makine dizinini broker olusturur).
std::unique_ptr<core::recovery::JournalFs> existing_dir_fs(const wchar_t* sub) {
    auto d = machine_journal_dir(false);
    if (!d.ok()) return nullptr;
    const auto dir = sub ? d.value() / sub : d.value();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return nullptr;
    return std::make_unique<WinJournalFs>(dir);
}
}  // namespace

bool RecoveryHost::refresh_machine_scopes() {
    if (!adapters) return false;
    bool opened = machine.ensure([] { return existing_dir_fs(nullptr); }, id, *adapters);
    opened = machine_fixes.ensure([] { return existing_dir_fs(L"fixes"); }, id, *adapters) || opened;
    return opened;
}

std::unique_ptr<RecoveryHost> open_recovery_host(const core::recovery::AdapterRegistry& adapters) {
    auto h = std::make_unique<RecoveryHost>();
    h->adapters = &adapters;
    h->id = machine_id();
    const auto& id = h->id;
    if (auto d = user_journal_dir(); d.ok()) {
        h->user_fs = std::make_unique<WinJournalFs>(d.value());
        h->user_store = std::make_unique<core::recovery::JournalStore>(*h->user_fs, id);
        h->user_engine = std::make_unique<core::recovery::TransactionEngine>(*h->user_store, adapters);
        std::error_code ec;
        const auto sd = d.value() / L"startup";
        std::filesystem::create_directories(sd, ec);
        if (!ec) {
            h->startup_fs = std::make_unique<WinJournalFs>(sd);
            h->startup_store = std::make_unique<core::recovery::JournalStore>(*h->startup_fs, id);
            h->startup_engine = std::make_unique<core::recovery::TransactionEngine>(*h->startup_store, adapters);
        }
        const auto fd = d.value() / L"fixes";
        std::filesystem::create_directories(fd, ec);
        if (!ec) {
            h->fixes_fs = std::make_unique<WinJournalFs>(fd);
            h->fixes_store = std::make_unique<core::recovery::JournalStore>(*h->fixes_fs, id);
            h->fixes_engine = std::make_unique<core::recovery::TransactionEngine>(*h->fixes_store, adapters);
        }
    }
    (void)h->refresh_machine_scopes();  // makine dizinleri varsa (broker olusturur; burada olusturulmaz)
    return h;
}

}  // namespace ef::platform
