#include <algorithm>
#include "core/startup/session.h"
#include "ef_test.h"

using namespace ef::core::startup;
using namespace ef::core::recovery;
using namespace ef::contracts;
namespace sa = ef::core::adapters::startup;

namespace {
struct Rig {
    sa::FakeStartupApi api;
    sa::StartupAdapter adapter{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    ChangeContext ctx;
    Rig() {
        reg.add(OperationId::StartupRunEntry, &adapter);
        reg.add(OperationId::StartupFolderItem, &adapter);
        ctx.store = &store;
        ctx.engine = &engine;
        ctx.api = &api;
        ctx.new_tx_id = [this] { return "0123456789abcdef0123456789abcd" + std::to_string(10 + n++ % 90); };
    }
    void add(sa::Location l, const std::string& name, const std::string& cmd) { api.entries[{static_cast<int>(l), name}] = cmd; }
    bool enabled(sa::Location l, const std::string& name) {
        auto r = api.get_raw(l, name);
        return !r.value() || sa::approval_from_raw(*r.value()) == sa::Approval::Enabled;
    }
    ScanResult scan() {
        ScanResult s;
        const auto act = active_targets(store);
        for (auto l : {sa::Location::UserRun, sa::Location::MachineRun64, sa::Location::UserFolder}) {
            auto lst = api.list(l);
            for (auto& e : lst.value()) {
                StartupRow r{e, sa::recommend(sa::from_entry(e)), false};
                r.ours = std::find(act.begin(), act.end(), sa::format_target(e.location, e.name)) != act.end();
                s.rows.push_back(std::move(r));
            }
        }
        return s;
    }
};
}  // namespace

EF_TEST(startup_change_is_journaled_and_reverts_exact_bytes) {
    Rig r;
    r.add(sa::Location::UserRun, "Updater", "C:\\Program Files\\Vendor\\updater.exe");
    sa::Entry e{sa::Location::UserRun, "Updater", "", sa::Approval::Enabled};
    EF_CHECK(change_entry(r.ctx, e, false).ok());
    EF_CHECK(!r.enabled(sa::Location::UserRun, "Updater"));
    EF_CHECK(active_targets(r.store).size() == 1);
    // Tekrar ac: etkin islem geri alinir; onceden StartupApproved degeri yoktu -> deger silinir (birebir eski durum).
    EF_CHECK(change_entry(r.ctx, e, true).ok());
    EF_CHECK(r.enabled(sa::Location::UserRun, "Updater"));
    EF_CHECK(!r.api.get_raw(sa::Location::UserRun, "Updater").value().has_value());
    EF_CHECK(active_targets(r.store).empty());
}

EF_TEST(startup_change_admin_location_denied) {
    Rig r;
    r.add(sa::Location::MachineRun64, "Vendor", "C:\\x.exe");
    sa::Entry e{sa::Location::MachineRun64, "Vendor", "", sa::Approval::Enabled};
    EF_CHECK(!change_entry(r.ctx, e, false).ok());
    EF_CHECK(r.enabled(sa::Location::MachineRun64, "Vendor") && active_targets(r.store).empty());
}

EF_TEST(startup_session_per_item_yes_no_and_confirm) {
    Rig r;
    r.add(sa::Location::UserRun, "Updater", "\"C:\\Program Files\\Vendor\\VendorUpdate.exe\" /silent");
    r.add(sa::Location::UserRun, "Steam", "\"C:\\Program Files (x86)\\Steam\\steam.exe\" -silent");
    r.add(sa::Location::MachineRun64, "SecurityHealth", "%windir%\\system32\\SecurityHealthSystray.exe");
    std::vector<std::function<void()>> q;
    int changes = 0;
    StartupDeps d;
    d.scan = [&]() -> Result<ScanResult> { return r.scan(); };
    d.change = [&](const sa::Entry& e, bool en) {
        ++changes;
        return change_entry(r.ctx, e, en);
    };
    d.run_background = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    auto drain = [&] { while (!q.empty()) { auto f = std::move(q.front()); q.erase(q.begin()); f(); } };
    StartupSession s(d);
    s.scan();
    drain();
    EF_CHECK(s.phase() == StartupPhase::Ready && s.rows().size() == 3);
    std::size_t sec = 99, steam = 99;
    for (std::size_t i = 0; i < s.rows().size(); ++i) {
        if (s.rows()[i].entry.name == "SecurityHealth") sec = i;
        if (s.rows()[i].entry.name == "Steam") steam = i;
    }
    EF_CHECK(sec < 3 && steam < 3);
    EF_CHECK(!s.editable(sec));  // HKLM + guvenlik: bu sayfadan degismez
    EF_CHECK(s.rows()[sec].rec.advice != sa::Advice::RecommendDisable);  // guvenlik onerilmez
    s.set_desired(sec, false);
    EF_CHECK(s.desired(sec) && s.pending() == 0);
    s.request_apply();
    EF_CHECK(s.phase() == StartupPhase::Ready);  // degisiklik yoksa onay yok
    s.set_desired(steam, false);
    EF_CHECK(s.pending() == 1);
    s.request_apply();
    EF_CHECK(s.phase() == StartupPhase::Confirming);
    s.answer(false);  // Hayir: hicbir sey yazilmaz
    EF_CHECK(s.phase() == StartupPhase::Ready && changes == 0 && r.enabled(sa::Location::UserRun, "Steam"));
    s.request_apply();
    s.answer(true);
    EF_CHECK(s.phase() == StartupPhase::Applying);
    drain();
    EF_CHECK(s.phase() == StartupPhase::Ready && changes == 1 && s.last() && s.last()->applied == 1 && s.last()->failed == 0);
    EF_CHECK(!r.enabled(sa::Location::UserRun, "Steam"));
    bool ours = false;
    for (const auto& row : s.rows())
        if (row.entry.name == "Steam") ours = row.ours && row.entry.approval == sa::Approval::Disabled;
    EF_CHECK(ours && s.pending() == 0);  // yeniden okunan durum gosterilir
}

EF_TEST(startup_rows_sorted_recommend_first) {
    std::vector<StartupRow> v(3);
    v[0].entry.name = "b", v[0].rec.advice = sa::Advice::Keep;
    v[1].entry.name = "c", v[1].rec.advice = sa::Advice::NoAdvice;
    v[2].entry.name = "a", v[2].rec.advice = sa::Advice::RecommendDisable;
    sort_rows(v);
    EF_CHECK(v[0].entry.name == "a" && v[1].entry.name == "c" && v[2].entry.name == "b");
}
