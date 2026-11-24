// ADR-033 / spec 4 ve 9: islemsel kurulum/kaldirma motoru. Yalniz bellek ici sahteler; gercek dosya, kayit defteri,
// hizmet veya kisayol yazilmaz.
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/setup/install_tx.h"
#include "ef_test.h"

using namespace ef::core::setup;
using ef::contracts::ErrorCode;
using ef::contracts::Result;
using ef::contracts::Status;

namespace {

const std::wstring kNewImage = L"\"C:\\Program Files\\Ecu Fps\\EcuFps.exe\" --service";
const std::wstring kNewTarget = L"C:\\Program Files\\Ecu Fps\\EcuFps.exe";

struct Model {
    bool dir = false, exe = false, old = false;
    std::string exe_ver, old_ver;
    std::optional<std::wstring> shortcut;
    std::optional<std::wstring> desktop;  // ortak masaustu "Ecu Fps.lnk" hedefi
    bool desktop_unreadable = false;      // ayni adli dosya var ama hedefi okunamiyor
    std::optional<ServiceConfig> service;
    bool running = false, updates = false, machine_data = false;
    std::optional<ArpValues> arp;
    friend bool operator==(const Model&, const Model&) = default;
};

ArpValues arp_of(const wchar_t* ver) {
    ArpValues a;
    a.display_version = ver;
    a.install_location = L"C:\\Program Files\\Ecu Fps";
    a.uninstall_string = L"\"C:\\Program Files\\Ecu Fps\\EcuFps.exe\" --uninstall";
    a.display_icon = L"C:\\Program Files\\Ecu Fps\\EcuFps.exe,0";
    a.estimated_size_kb = 4096;
    a.install_date = L"20261113";
    return a;
}

Model fresh() { return {}; }
Model existing() {  // onceki surum kurulu (yukseltme)
    Model m;
    m.dir = m.exe = true;
    m.exe_ver = "old";
    m.shortcut = L"C:\\old\\target.exe";
    m.desktop = L"C:\\old\\desktop-target.exe";  // ayni adli onceki masaustu kisayolu korunmali
    m.service = ServiceConfig{L"\"C:\\old\\svc.exe\" --service", false};
    m.running = true;
    m.updates = true;
    m.machine_data = true;
    m.arp = arp_of(L"0.2.9");
    return m;
}

class FakeSetupOps final : public SetupOps {
public:
    Model m;
    std::vector<std::string> log;
    std::set<std::string> fail;     // ilk cagrida etkisiz basarisiz
    std::set<std::string> partial;  // ilk cagrida etkiyi uygula, sonra basarisiz
    bool journals_clean = true;
    bool reboot = true;

    Status call(const std::string& name, const std::function<void()>& effect) {
        log.push_back(name);
        if (fail.erase(name)) return Status::fail(ErrorCode::IoError, name);
        effect();
        if (partial.erase(name)) return Status::fail(ErrorCode::IoError, name + " partial");
        return ef::contracts::ok_status();
    }

    Result<PriorState> probe() override {
        log.push_back("probe");
        if (fail.count("probe")) return Result<PriorState>::fail(ErrorCode::IoError, "probe");
        PriorState p;
        p.dir_existed = m.dir;
        p.exe_existed = m.exe;
        p.shortcut_existed = m.shortcut.has_value();
        p.shortcut_target = m.shortcut;
        p.desktop_existed = m.desktop.has_value();
        p.desktop_target = m.desktop_unreadable ? std::nullopt : m.desktop;
        p.service_existed = m.service.has_value();
        p.service_config = m.service;
        p.service_running = m.running;
        p.updates_existed = m.updates;
        p.arp = m.arp;
        return p;
    }
    Status create_install_dir() override { return call("create_install_dir", [&] { m.dir = true; }); }
    Status remove_install_dir_if_empty() override {
        if (m.exe || m.old) {
            log.push_back("remove_install_dir_if_empty");
            return Status::fail(ErrorCode::IoError, "not empty");
        }
        return call("remove_install_dir_if_empty", [&] { m.dir = false; });
    }
    Status stage_and_swap_exe() override {
        return call("stage_and_swap_exe", [&] {
            if (m.exe) {
                m.old = true;
                m.old_ver = m.exe_ver;
            }
            m.exe = true;
            m.exe_ver = "new";
        });
    }
    Status restore_previous_exe(const PriorState&) override {
        return call("restore_previous_exe", [&] {
            if (m.old) {
                m.exe_ver = m.old_ver;
                m.old = false;
                m.old_ver.clear();
            } else if (m.exe_ver == "new") {  // yedek yok: yalniz yeni kopya kaldirilir, onceki EXE'ye dokunulmaz
                m.exe = false;
                m.exe_ver.clear();
            }
        });
    }
    Status commit_exe() override {
        return call("commit_exe", [&] {
            m.old = false;
            m.old_ver.clear();
        });
    }
    Status write_shortcut() override { return call("write_shortcut", [&] { m.shortcut = kNewTarget; }); }
    Status restore_shortcut(const PriorState& p) override {
        return call("restore_shortcut", [&] { m.shortcut = p.shortcut_existed ? p.shortcut_target : std::nullopt; });
    }
    Status remove_shortcut() override { return call("remove_shortcut", [&] { m.shortcut.reset(); }); }
    Status write_desktop_shortcut() override {
        return call("write_desktop_shortcut", [&] {
            m.desktop = kNewTarget;
            m.desktop_unreadable = false;
        });
    }
    // Platformla ayni saf kural (desktop_restore_action / desktop_remove_action).
    std::optional<std::wstring> desktop_current() const { return m.desktop_unreadable ? std::nullopt : m.desktop; }
    Status restore_desktop_shortcut(const PriorState& p) override {
        const auto a = desktop_restore_action(p, m.desktop.has_value(), desktop_current(), kNewTarget);
        if (a == LinkAction::Fail) {
            log.push_back("restore_desktop_shortcut");
            return Status::fail(ErrorCode::MissingValue, "previous desktop shortcut target unknown");
        }
        return call("restore_desktop_shortcut", [&] {
            if (a == LinkAction::Delete) m.desktop.reset();
            if (a == LinkAction::Write) m.desktop = p.desktop_target;
            if (a != LinkAction::Nothing) m.desktop_unreadable = false;
        });
    }
    Status remove_desktop_shortcut() override {
        return call("remove_desktop_shortcut", [&] {
            if (desktop_remove_action(m.desktop.has_value(), desktop_current(), kNewTarget) == LinkAction::Delete) m.desktop.reset();
        });
    }
    Status write_service() override { return call("write_service", [&] { m.service = ServiceConfig{kNewImage, true}; }); }
    Status restore_service(const PriorState& p) override {
        return call("restore_service", [&] {
            m.service = p.service_existed ? p.service_config : std::nullopt;
            if (!m.service) m.running = false;
        });
    }
    Status delete_service() override {
        return call("delete_service", [&] {
            m.service.reset();
            m.running = false;
        });
    }
    Status start_service() override {
        if (!m.service) {
            log.push_back("start_service");
            return Status::fail(ErrorCode::IoError, "no service");
        }
        return call("start_service", [&] { m.running = true; });
    }
    Status stop_service() override { return call("stop_service", [&] { m.running = false; }); }
    Status create_updates_dir() override { return call("create_updates_dir", [&] { m.updates = true; }); }
    Status remove_updates_dir() override { return call("remove_updates_dir", [&] { m.updates = false; }); }
    Status write_arp(const ArpValues& v) override { return call("write_arp", [&] { m.arp = v; }); }
    Status restore_arp(const PriorState& p) override { return call("restore_arp", [&] { m.arp = p.arp; }); }
    Status remove_arp() override { return call("remove_arp", [&] { m.arp.reset(); }); }
    Result<bool> machine_journals_clean() override {
        log.push_back("machine_journals_clean");
        if (fail.count("machine_journals_clean")) return Result<bool>::fail(ErrorCode::IoError, "x");
        return journals_clean;
    }
    Status remove_machine_data() override {
        return call("remove_machine_data", [&] {
            m.machine_data = false;
            m.updates = false;
        });
    }
    Result<bool> remove_program_files() override {
        log.push_back("remove_program_files");
        if (fail.count("remove_program_files")) return Result<bool>::fail(ErrorCode::IoError, "files");
        m.exe = m.old = m.dir = false;
        m.exe_ver.clear();
        m.old_ver.clear();
        return reboot;
    }
};

// Satirlari kodlayarak saklar: her kayit encode/decode'dan gecer.
class MemoryJournal final : public SetupJournal {
public:
    std::vector<std::string> lines;
    int appends = 0;
    int fail_append_at = -1;  // bu sira numarali append (0 tabanli) ve sonrakiler basarisiz
    int clears = 0;

    Status append(const JournalEntry& e) override {
        const int n = appends++;
        if (fail_append_at >= 0 && n >= fail_append_at) return Status::fail(ErrorCode::IoError, "disk");
        lines.push_back(encode_entry(e));
        return ef::contracts::ok_status();
    }
    Result<std::vector<JournalEntry>> read() override {
        std::vector<JournalEntry> out;
        for (const auto& l : lines) {
            auto e = decode_entry(l);
            if (!e) return e.error();
            out.push_back(e.value());
        }
        return out;
    }
    Status clear() override {
        ++clears;
        lines.clear();
        return ef::contracts::ok_status();
    }
    std::vector<JournalEntry> entries() { return read().value(); }
};

const std::vector<SetupStep> kInstallSteps = {SetupStep::CreateDir,       SetupStep::CopyExe,       SetupStep::Shortcut,
                                              SetupStep::DesktopShortcut, SetupStep::ServiceCreate, SetupStep::ServiceStart,
                                              SetupStep::UpdatesDir,      SetupStep::Arp};
const std::map<SetupStep, std::string> kApplyName = {
    {SetupStep::CreateDir, "create_install_dir"}, {SetupStep::CopyExe, "stage_and_swap_exe"},
    {SetupStep::Shortcut, "write_shortcut"},      {SetupStep::ServiceCreate, "write_service"},
    {SetupStep::ServiceStart, "start_service"},   {SetupStep::UpdatesDir, "create_updates_dir"},
    {SetupStep::Arp, "write_arp"},                {SetupStep::Commit, "commit_exe"},
    {SetupStep::ServiceStop, "stop_service"},     {SetupStep::ArpRemove, "remove_arp"},
    {SetupStep::ShortcutRemove, "remove_shortcut"}, {SetupStep::ServiceDelete, "delete_service"},
    {SetupStep::DesktopShortcut, "write_desktop_shortcut"}, {SetupStep::DesktopShortcutRemove, "remove_desktop_shortcut"}};

Model installed_from(const Model& before) {
    FakeSetupOps ops;
    ops.m = before;
    MemoryJournal j;
    auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx-pre");
    EF_CHECK(r.ok);
    return ops.m;
}

std::vector<std::string> since(const std::vector<std::string>& log, std::size_t from) {
    return {log.begin() + static_cast<std::ptrdiff_t>(from), log.end()};
}

}  // namespace

EF_TEST(setup_tx_install_happy_path_fresh) {
    FakeSetupOps ops;
    MemoryJournal j;
    const auto arp = arp_of(L"0.3.0");
    auto r = run_install(ops, j, arp, "tx1");
    EF_CHECK(r.ok);
    EF_CHECK(!r.failed_at);
    EF_CHECK(r.rollback_clean);
    const std::vector<std::string> want = {"probe",         "create_install_dir", "stage_and_swap_exe", "write_shortcut",
                                           "write_desktop_shortcut", "write_service", "start_service", "create_updates_dir",
                                           "write_arp",     "commit_exe"};
    EF_CHECK(ops.log == want);
    EF_CHECK(ops.m.dir && ops.m.exe && ops.m.exe_ver == "new" && !ops.m.old);
    EF_CHECK(ops.m.shortcut == kNewTarget);
    EF_CHECK(ops.m.desktop == kNewTarget);  // ortak masaustu kisayolu kurulu EXE'yi gosterir
    EF_CHECK(ops.m.service && ops.m.service->image_path == kNewImage && ops.m.running);
    EF_CHECK(ops.m.updates && ops.m.arp == arp);
    EF_CHECK(r.done.size() == 10 && r.done.front() == SetupStep::Probe && r.done.back() == SetupStep::Commit);
    EF_CHECK(j.lines.empty() && j.clears == 1);  // Commit journal'i temizler
    EF_CHECK(j.appends == 2 + 8 * 2 + 2);        // Probe S/D, 8 adim S/D, Commit Started + Committed
}

EF_TEST(setup_tx_install_journal_contents_before_clear) {
    FakeSetupOps ops;
    MemoryJournal j;
    auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx1", SetupStep::Commit);
    EF_CHECK(!r.ok);  // Committed yazildi, temizleme oncesi durdu
    auto es = j.entries();
    EF_CHECK(es.size() == 20);
    for (std::size_t i = 0; i < es.size(); ++i) {
        EF_CHECK(es[i].seq == i + 1);
        EF_CHECK(es[i].tx_id == "tx1");
    }
    EF_CHECK(es[0].step == SetupStep::Probe && es[0].rec == StepRecord::Started);
    EF_CHECK(es[1].step == SetupStep::Probe && es[1].rec == StepRecord::Done);
    auto prior = decode_prior(es[1].prior_json);
    EF_CHECK(prior.ok() && prior.value() == PriorState{});
    for (std::size_t k = 0; k < kInstallSteps.size(); ++k) {
        EF_CHECK(es[2 + 2 * k].step == kInstallSteps[k] && es[2 + 2 * k].rec == StepRecord::Started);
        EF_CHECK(es[3 + 2 * k].step == kInstallSteps[k] && es[3 + 2 * k].rec == StepRecord::Done);
    }
    EF_CHECK(es[18].step == SetupStep::Commit && es[18].rec == StepRecord::Started);
    EF_CHECK(es[19].step == SetupStep::Commit && es[19].rec == StepRecord::Committed);
    // Committed sonrasi kurtarma hicbir seyi geri almaz, yalniz temizler.
    const Model after = ops.m;
    ops.log.clear();
    auto rec = recover_interrupted(ops, j);
    EF_CHECK(rec.ok && ops.log.empty() && ops.m == after && j.lines.empty());
}

EF_TEST(setup_tx_install_failure_at_each_step_rolls_back_exactly) {
    for (const Model& start : {fresh(), existing()}) {
        for (std::size_t k = 0; k <= kInstallSteps.size(); ++k) {
            const SetupStep s = k < kInstallSteps.size() ? kInstallSteps[k] : SetupStep::Commit;
            for (bool part : {false, true}) {
                if (part && s == SetupStep::Commit) continue;  // yedegi silip basarisiz olan commit geri alinamaz
                FakeSetupOps ops;
                ops.m = start;
                (part ? ops.partial : ops.fail).insert(kApplyName.at(s));
                MemoryJournal j;
                auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
                EF_CHECK(!r.ok);
                EF_CHECK(r.failed_at == s);
                EF_CHECK(r.rollback_clean);
                EF_CHECK(r.undo_failed.empty());
                EF_CHECK(ops.m == start);                EF_CHECK(j.lines.empty());  // temiz geri alma -> journal temiz
                // Ters sira: uygulama cagrilarindan sonra gelen geri alma cagrilari.
                const auto apply_end = std::find(ops.log.begin(), ops.log.end(), kApplyName.at(s));
                EF_CHECK(apply_end != ops.log.end());
                const auto undo = since(ops.log, static_cast<std::size_t>(apply_end - ops.log.begin()) + 1);
                std::vector<std::string> want;
                const bool running = start.running;
                const std::size_t last = k < kInstallSteps.size() ? k : kInstallSteps.size() - 1;
                for (std::size_t i = last + 1; i-- > 0;) {
                    switch (kInstallSteps[i]) {
                        case SetupStep::CreateDir: if (!start.dir) want.push_back("remove_install_dir_if_empty"); break;
                        case SetupStep::CopyExe: want.push_back("restore_previous_exe"); break;
                        case SetupStep::Shortcut: want.push_back("restore_shortcut"); break;
                        case SetupStep::DesktopShortcut: want.push_back("restore_desktop_shortcut"); break;
                        case SetupStep::ServiceCreate: want.push_back("restore_service"); break;
                        case SetupStep::ServiceStart: want.push_back("stop_service"); break;
                        case SetupStep::UpdatesDir: if (!start.updates) want.push_back("remove_updates_dir"); break;
                        case SetupStep::Arp: want.push_back("restore_arp"); break;
                        default: break;
                    }
                }
                if (running && last >= 5) want.push_back("start_service");  // ServiceStart sirasi 5  // onceki hizmet geri gelince yeniden
                EF_CHECK(undo == want);
            }
        }
    }
}

EF_TEST(setup_tx_install_undo_failure_reported) {
    FakeSetupOps ops;
    ops.fail = {"write_arp", "restore_shortcut"};
    MemoryJournal j;
    auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
    EF_CHECK(!r.ok && r.failed_at == SetupStep::Arp);
    EF_CHECK(!r.rollback_clean);
    EF_CHECK(r.undo_failed == std::vector<SetupStep>{SetupStep::Shortcut});
    // Geri kalan adimlar yine geri alindi; journal ekranda gostermek icin korunur.
    EF_CHECK(!ops.m.service && !ops.m.exe && !ops.m.updates && !ops.m.arp && !ops.m.desktop);
    EF_CHECK(ops.m.shortcut == kNewTarget);
    auto es = j.entries();
    EF_CHECK(std::any_of(es.begin(), es.end(), [](const JournalEntry& e) {
        return e.step == SetupStep::Shortcut && e.rec == StepRecord::UndoFailed;
    }));
    EF_CHECK(std::count_if(es.begin(), es.end(), [](const JournalEntry& e) { return e.rec == StepRecord::Undone; }) == 7);
}

EF_TEST(setup_tx_append_failure_applies_nothing) {
    {  // ilk kayit yazilamaz -> probe bile yok
        FakeSetupOps ops;
        MemoryJournal j;
        j.fail_append_at = 0;
        auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
        EF_CHECK(!r.ok && r.failed_at == SetupStep::Probe && ops.log.empty());
    }
    // Her adimin Started kaydi yazilamadiginda o adim uygulanmaz; oncekiler geri alinir.
    for (const Model& start : {fresh(), existing()}) {
        for (std::size_t k = 0; k <= kInstallSteps.size(); ++k) {
            const SetupStep s = k < kInstallSteps.size() ? kInstallSteps[k] : SetupStep::Commit;
            FakeSetupOps ops;
            ops.m = start;
            MemoryJournal j;
            j.fail_append_at = static_cast<int>(2 + 2 * k);
            auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
            EF_CHECK(!r.ok && r.failed_at == s);
            EF_CHECK(std::find(ops.log.begin(), ops.log.end(), kApplyName.at(s)) == ops.log.end());
            EF_CHECK(ops.m == start);
        }
    }
}

EF_TEST(setup_tx_power_loss_then_recover_restores_prior) {
    for (const Model& start : {fresh(), existing()}) {
        std::vector<SetupStep> cut = {SetupStep::Probe};
        cut.insert(cut.end(), kInstallSteps.begin(), kInstallSteps.end());
        for (SetupStep s : cut) {
            FakeSetupOps ops;
            ops.m = start;
            MemoryJournal j;
            auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx", s);
            EF_CHECK(!r.ok && !r.failed_at);  // geri alma yapmadan durdu
            EF_CHECK(!j.lines.empty());
            auto rec = recover_interrupted(ops, j);
            EF_CHECK(rec.ok && rec.rollback_clean);
            EF_CHECK(ops.m == start);
            EF_CHECK(j.lines.empty());
            // Ikinci kurtarma bos journal ile hicbir sey yapmaz.
            const auto n = ops.log.size();
            EF_CHECK(recover_interrupted(ops, j).ok && ops.log.size() == n);
        }
        // Commit: Committed yazilmis -> kurulum kalir.
        FakeSetupOps ops;
        ops.m = start;
        MemoryJournal j;
        run_install(ops, j, arp_of(L"0.3.0"), "tx", SetupStep::Commit);
        EF_CHECK(recover_interrupted(ops, j).ok);
        EF_CHECK(ops.m == installed_from(start));
    }
}

EF_TEST(setup_tx_power_loss_inside_commit_rolls_forward) {
    // Commit Started yazildi; commit_exe .old'u silmis (veya henuz calismamis), Committed yazilamadan guc kesildi.
    // Geri alma eski EXE'yi geri getiremez; kurtarma kurulumu ileri tamamlamali.
    for (const Model& start : {fresh(), existing()}) {
        for (bool commit_ran : {true, false}) {
            FakeSetupOps ops;
            ops.m = start;
            MemoryJournal j;
            run_install(ops, j, arp_of(L"0.3.0"), "tx", SetupStep::Arp);
            JournalEntry e;
            e.seq = 99;
            e.tx_id = "tx";
            e.step = SetupStep::Commit;
            e.rec = StepRecord::Started;
            EF_CHECK(j.append(e).ok());
            if (commit_ran) EF_CHECK(ops.commit_exe().ok());
            ops.log.clear();
            auto rec = recover_interrupted(ops, j);
            EF_CHECK(rec.ok && rec.rollback_clean && !rec.failed_at);
            EF_CHECK(ops.log == std::vector<std::string>{"commit_exe"});
            EF_CHECK(ops.m == installed_from(start));
            EF_CHECK(j.lines.empty());
        }
    }
    {  // commit_exe kurtarmada da basarisiz: journal korunur, sonraki kurtarma tamamlar
        FakeSetupOps ops;
        MemoryJournal j;
        run_install(ops, j, arp_of(L"0.3.0"), "tx", SetupStep::Arp);
        JournalEntry e;
        e.seq = 99;
        e.tx_id = "tx";
        e.step = SetupStep::Commit;
        e.rec = StepRecord::Started;
        EF_CHECK(j.append(e).ok());
        ops.fail.insert("commit_exe");
        auto rec = recover_interrupted(ops, j);
        EF_CHECK(!rec.ok && !rec.rollback_clean && rec.failed_at == SetupStep::Commit && !j.lines.empty());
        EF_CHECK(recover_interrupted(ops, j).ok && ops.m == installed_from(fresh()) && j.lines.empty());
    }
    {  // Commit hatasi sonrasi geri alma baslamis ve kesilmis: kurtarma geri almaya devam eder
        FakeSetupOps ops;
        MemoryJournal j;
        run_install(ops, j, arp_of(L"0.3.0"), "tx", SetupStep::Arp);
        JournalEntry e;
        e.seq = 99;
        e.tx_id = "tx";
        e.step = SetupStep::Commit;
        e.rec = StepRecord::Started;
        EF_CHECK(j.append(e).ok());
        e.seq = 100;
        e.step = SetupStep::Arp;
        e.rec = StepRecord::Undone;
        EF_CHECK(j.append(e).ok());
        EF_CHECK(ops.restore_arp(PriorState{}).ok());
        auto rec = recover_interrupted(ops, j);
        EF_CHECK(rec.ok && ops.m == fresh() && j.lines.empty());
    }
}

EF_TEST(setup_tx_recover_mid_step_started_without_done) {
    // Started yazildi, uygulama yarim kaldi (Done yok): kismi etki de geri alinir.
    FakeSetupOps ops;
    MemoryJournal j;
    run_install(ops, j, arp_of(L"0.3.0"), "tx", SetupStep::Shortcut);
    JournalEntry e;
    e.seq = 99;
    e.tx_id = "tx";
    e.step = SetupStep::ServiceCreate;
    e.rec = StepRecord::Started;
    EF_CHECK(j.append(e).ok());
    ops.m.service = ServiceConfig{kNewImage, true};  // yarim etki
    auto rec = recover_interrupted(ops, j);
    EF_CHECK(rec.ok && ops.m == fresh());
}

EF_TEST(setup_tx_recover_rejects_bad_prior_and_keeps_journal) {
    FakeSetupOps ops;
    MemoryJournal j;
    JournalEntry e;
    e.seq = 1;
    e.tx_id = "tx";
    e.step = SetupStep::Probe;
    e.rec = StepRecord::Done;
    e.prior_json = "{bozuk";
    EF_CHECK(j.append(e).ok());
    auto r = recover_interrupted(ops, j);
    EF_CHECK(!r.ok && !r.rollback_clean && ops.log.empty() && j.lines.size() == 1);
}

EF_TEST(setup_tx_repair_reapplies_service_and_updates) {
    Model m = installed_from(fresh());
    m.service.reset();
    m.running = false;
    m.updates = false;
    m.desktop.reset();  // kullanici masaustu kisayolunu silmis: onarim yeniden yazar
    FakeSetupOps ops;
    ops.m = m;
    MemoryJournal j;
    auto r = run_repair(ops, j, "rep");
    EF_CHECK(r.ok);
    const std::vector<std::string> want = {"probe", "write_service", "start_service", "create_updates_dir", "write_desktop_shortcut"};
    EF_CHECK(ops.log == want);
    EF_CHECK(ops.m == installed_from(fresh()));
    EF_CHECK(j.lines.empty());
    // Hizmet baslatilamazsa onarim geri alinir.
    FakeSetupOps bad;
    bad.m = m;
    bad.fail = {"start_service"};
    MemoryJournal j2;
    auto r2 = run_repair(bad, j2, "rep2");
    EF_CHECK(!r2.ok && r2.failed_at == SetupStep::ServiceStart && r2.rollback_clean && bad.m == m);
}

EF_TEST(setup_tx_uninstall_happy_path) {
    FakeSetupOps ops;
    ops.m = installed_from(fresh());
    MemoryJournal j;
    auto r = run_uninstall_program(ops, j, "un");
    EF_CHECK(r.ok && r.reboot_pending && !r.machine_data_kept);
    const std::vector<std::string> want = {"probe",          "stop_service",           "remove_arp",          "remove_shortcut",
                                           "remove_desktop_shortcut", "delete_service", "machine_journals_clean",
                                           "remove_machine_data", "remove_program_files"};
    EF_CHECK(ops.log == want);
    EF_CHECK(ops.m == fresh());
    EF_CHECK(j.lines.empty());
}

EF_TEST(setup_tx_uninstall_failure_at_each_reversible_step_restores) {
    const Model inst = installed_from(fresh());
    for (SetupStep s : {SetupStep::ServiceStop, SetupStep::ArpRemove, SetupStep::ShortcutRemove, SetupStep::DesktopShortcutRemove,
                        SetupStep::ServiceDelete}) {
        for (bool part : {false, true}) {
            FakeSetupOps ops;
            ops.m = inst;
            (part ? ops.partial : ops.fail).insert(kApplyName.at(s));
            MemoryJournal j;
            auto r = run_uninstall_program(ops, j, "un");
            EF_CHECK(!r.ok && r.failed_at == s && r.rollback_clean);
            EF_CHECK(ops.m == inst);
            EF_CHECK(std::find(ops.log.begin(), ops.log.end(), "remove_machine_data") == ops.log.end());
            EF_CHECK(std::find(ops.log.begin(), ops.log.end(), "remove_program_files") == ops.log.end());
        }
    }
    // Guc kaybi + kurtarma: geri alinabilir adimlar geri gelir.
    for (SetupStep s : {SetupStep::ServiceStop, SetupStep::ArpRemove, SetupStep::ShortcutRemove, SetupStep::DesktopShortcutRemove,
                        SetupStep::ServiceDelete}) {
        FakeSetupOps ops;
        ops.m = inst;
        MemoryJournal j;
        run_uninstall_program(ops, j, "un", s);
        EF_CHECK(recover_interrupted(ops, j).ok);
        EF_CHECK(ops.m == inst);
    }
}

EF_TEST(setup_tx_uninstall_keeps_machine_data_when_journals_not_clean) {
    for (bool read_error : {false, true}) {
        FakeSetupOps ops;
        ops.m = installed_from(fresh());
        if (read_error) ops.fail.insert("machine_journals_clean");
        else ops.journals_clean = false;
        MemoryJournal j;
        auto r = run_uninstall_program(ops, j, "un");
        EF_CHECK(r.ok && r.machine_data_kept);
        EF_CHECK(ops.m.updates);
        EF_CHECK(std::find(ops.log.begin(), ops.log.end(), "remove_machine_data") == ops.log.end());
        EF_CHECK(!ops.m.exe && !ops.m.service && !ops.m.arp && !ops.m.shortcut && !ops.m.desktop);
    }
}

EF_TEST(setup_tx_uninstall_point_of_no_return_failures_stop_without_rollback) {
    for (const char* f : {"remove_machine_data", "remove_program_files"}) {
        FakeSetupOps ops;
        ops.m = installed_from(fresh());
        ops.fail.insert(f);
        MemoryJournal j;
        auto r = run_uninstall_program(ops, j, "un");
        EF_CHECK(!r.ok && !r.rollback_clean);
        EF_CHECK(r.failed_at == (std::string(f) == "remove_machine_data" ? SetupStep::MachineDataRemove : SetupStep::FilesRemove));
        EF_CHECK(!ops.m.service && !ops.m.arp);  // geri alma yok
        EF_CHECK(!j.lines.empty());              // journal korunur
        auto rec = recover_interrupted(ops, j);   // kurtarma da geri almaz
        EF_CHECK(!rec.ok && !ops.m.service);
    }
    {  // MachineDataRemove Started yazilamazsa henuz silinen yok -> geri al
        FakeSetupOps ops;
        const Model inst = installed_from(fresh());
        ops.m = inst;
        MemoryJournal j;
        j.fail_append_at = 12;  // Probe S/D + 5 adim S/D = 12 kayit
        auto r = run_uninstall_program(ops, j, "un");
        EF_CHECK(!r.ok && r.failed_at == SetupStep::MachineDataRemove);
        EF_CHECK(ops.m == inst);
    }
}

EF_TEST(setup_tx_entry_and_prior_round_trip) {
    PriorState p;
    p.dir_existed = p.exe_existed = p.shortcut_existed = p.service_existed = p.service_running = true;
    p.shortcut_target = L"C:\\Yol\\\x00C7\x0131kt\x0131 \"q\"\\a.exe";  // Turkce + tirnak
    p.desktop_existed = true;
    p.desktop_target = L"D:\\Ta\x015F\x0131nabilir\\EcuFps.exe";
    p.service_config = ServiceConfig{L"\"C:\\x\" --service", false};
    p.arp = arp_of(L"0.3.0");
    p.arp->display_name = std::wstring{static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00)} + L" emoji";  // vekil cift
    auto d = decode_prior(encode_prior(p));
    EF_CHECK(d.ok() && d.value() == p);
    auto d0 = decode_prior(encode_prior(PriorState{}));
    EF_CHECK(d0.ok() && d0.value() == PriorState{});

    JournalEntry e;
    e.seq = 42;
    e.tx_id = "tx-9";
    e.step = SetupStep::FilesRemove;
    e.rec = StepRecord::UndoFailed;
    e.detail = "satir\nsonu";
    e.prior_json = encode_prior(p);
    const auto line = encode_entry(e);
    EF_CHECK(line.find('\n') == std::string::npos);
    auto de = decode_entry(line);
    EF_CHECK(de.ok() && de.value() == e);
    for (int s = 0; s <= static_cast<int>(SetupStep::DesktopShortcutRemove); ++s) {
        e.step = static_cast<SetupStep>(s);
        EF_CHECK(decode_entry(encode_entry(e)).value().step == e.step);
    }
}

EF_TEST(setup_tx_malformed_lines_rejected) {
    const char* bad[] = {
        "",
        "{",
        "[]",
        R"({"seq":1,"tx":"a","step":"Nope","rec":"Done","detail":"","prior":""})",
        R"({"seq":1,"tx":"a","step":"Probe","rec":"Maybe","detail":"","prior":""})",
        R"({"seq":-1,"tx":"a","step":"Probe","rec":"Done","detail":"","prior":""})",
        R"({"seq":1,"tx":"","step":"Probe","rec":"Done","detail":"","prior":""})",
        R"({"seq":1,"step":"Probe","rec":"Done","detail":"","prior":""})",
        R"({"seq":1,"tx":"a","step":"Probe","rec":"Done","detail":""})",
        R"({"seq":"1","tx":"a","step":"Probe","rec":"Done","detail":"","prior":""})",
    };
    for (const char* l : bad) EF_CHECK(!decode_entry(l).ok());
    EF_CHECK(decode_entry(R"({"seq":1,"tx":"a","step":"Probe","rec":"Done","detail":"","prior":""})").ok());
    EF_CHECK(!decode_prior("{}").ok());
    EF_CHECK(!decode_prior(R"({"dir":true})").ok());
    EF_CHECK(!decode_prior(R"({"dir":false,"exe":false,"shortcut":false,"service":false,"running":false,"updates":false})").ok());
    EF_CHECK(!decode_prior(
        R"({"dir":false,"exe":false,"shortcut":false,"service":false,"running":false,"updates":false,"shortcut_target":null,"service_config":5,"arp":null})")
                   .ok());
    EF_CHECK(decode_prior(
        R"({"dir":false,"exe":false,"shortcut":false,"service":false,"running":false,"updates":false,"shortcut_target":null,"service_config":null,"arp":null})")
                  .ok());
    // Gecersiz UTF-8 reddedilir.
    EF_CHECK(!decode_prior(
        "{\"dir\":false,\"exe\":false,\"shortcut\":true,\"service\":false,\"running\":false,\"updates\":false,"
        "\"shortcut_target\":\"\xC3\",\"service_config\":null,\"arp\":null}")
                   .ok());
}

EF_TEST(setup_tx_make_arp_values) {
    KnownRoots roots;
    roots.program_files = L"C:\\Program Files";
    roots.program_data = L"C:\\ProgramData";
    roots.common_programs = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    roots.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    const auto l = make_layout(roots);
    auto a = make_arp_values(l, L"0.3.0", 1025, L"20261113");
    EF_CHECK(a.display_name == L"Ecu Fps" && a.publisher == L"Ecu Fps");
    EF_CHECK(a.display_version == L"0.3.0");
    EF_CHECK(a.display_icon == L"C:\\Program Files\\Ecu Fps\\EcuFps.exe,0");
    EF_CHECK(a.install_location == L"C:\\Program Files\\Ecu Fps");
    EF_CHECK(a.uninstall_string == L"\"C:\\Program Files\\Ecu Fps\\EcuFps.exe\" --uninstall");
    EF_CHECK(a.estimated_size_kb == 2);
    EF_CHECK(make_arp_values(l, L"0.3.0", 2048, L"x").estimated_size_kb == 2);
    EF_CHECK(make_arp_values(l, L"0.3.0", 0, L"x").estimated_size_kb == 0);
    EF_CHECK(a.install_date == L"20261113" && a.no_modify && a.no_repair);
}

EF_TEST(setup_arp_display_version_refresh_plan) {
    KnownRoots roots;
    roots.program_files = L"C:\\Program Files";
    roots.program_data = L"C:\\ProgramData";
    roots.common_programs = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    roots.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    const auto l = make_layout(roots);
    const auto old = make_arp_values(l, L"0.3.0", 4096, L"20261113");
    // Bayat surum: yalniz DisplayVersion degisir, diger alanlar ayni kalir.
    auto p = plan_display_version_refresh(old, l, L"0.7.1");
    EF_CHECK(p.has_value() && p->display_version == L"0.7.1");
    auto want = old;
    want.display_version = L"0.7.1";
    EF_CHECK(*p == want);
    // Ayni surum, kayit yok, bos surum: degisiklik yok.
    EF_CHECK(!plan_display_version_refresh(old, l, L"0.3.0").has_value());
    EF_CHECK(!plan_display_version_refresh(std::nullopt, l, L"0.7.1").has_value());
    EF_CHECK(!plan_display_version_refresh(old, l, L"").has_value());
    // Baska konuma ait kayit bizim degil: dokunulmaz.
    auto other = old;
    other.install_location = L"D:\\elsewhere";
    EF_CHECK(!plan_display_version_refresh(other, l, L"0.7.1").has_value());
}

namespace {
InstallLayout test_layout() {
    KnownRoots roots;
    roots.program_files = L"C:\\Program Files";
    roots.program_data = L"C:\\ProgramData";
    roots.common_programs = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    roots.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    return make_layout(roots);
}
}  // namespace

// P9/B4: InstallLocation ayni klasorun farkli yazimi (buyuk/kucuk harf, sonda ayirac) ise kayit yine BIZIMDIR; tam esitlik
// yuzunden tazeleme sessizce atlanirdi ve Uygulamalar listesi eski surumu gosterirdi. Baska klasor yine reddedilir.
EF_TEST(setup_arp_display_version_refresh_tolerates_path_spelling) {
    const auto l = test_layout();
    auto old = make_arp_values(l, L"0.9.1", 4096, L"20261113");
    for (const wchar_t* spelled : {L"C:\\Program Files\\Ecu Fps\\", L"c:\\program files\\ecu fps", L"C:/Program Files/Ecu Fps"}) {
        auto cur = old;
        cur.install_location = spelled;
        auto p = plan_display_version_refresh(cur, l, L"0.9.2");
        EF_CHECK(p.has_value() && p->display_version == L"0.9.2" && p->install_location == spelled);
    }
    auto other = old;
    other.install_location = L"C:\\Program Files\\Ecu Fps Old";
    EF_CHECK(!plan_display_version_refresh(other, l, L"0.9.2").has_value());
}

// P9/B4 (0.8.x vakasi): uygulama ici guncellemeden sonra hizmet DURMUS kaldiysa ARP tazelemesi (hizmet acilisinda) hic
// calismaz; DisplayVersion eski surumde kalirdi. Yukseltilmis onarim artik kendi kaydinin yalniz DisplayVersion'unu calisan
// surume ceker (journal'li Arp adimi, geri alma = onceki ARP). Baska konumun kaydina ve ayni surume dokunulmaz.
EF_TEST(setup_tx_repair_refreshes_stale_arp_display_version) {
    const auto l = test_layout();
    Model m = installed_from(fresh());  // ARP 0.3.0
    m.running = false;                   // guncelleme sonrasi hizmet durmus
    RepairOptions opt;
    opt.layout = &l;
    opt.running_version = L"0.3.1";
    {
        FakeSetupOps ops;
        ops.m = m;
        MemoryJournal j;
        auto r = run_repair(ops, j, "rep-arp", opt);
        EF_CHECK(r.ok && ops.m.running);
        EF_CHECK(ops.m.arp && ops.m.arp->display_version == L"0.3.1");
        auto want = *m.arp;
        want.display_version = L"0.3.1";
        EF_CHECK(*ops.m.arp == want);  // yalniz DisplayVersion degisti
        EF_CHECK(std::find(ops.log.begin(), ops.log.end(), "write_arp") != ops.log.end());
        EF_CHECK(std::find(r.done.begin(), r.done.end(), SetupStep::Arp) != r.done.end());
    }
    {   // ARP yazimi basarisiz: onarim tamamen geri alinir, ARP onceki degerinde.
        FakeSetupOps ops;
        ops.m = m;
        ops.fail = {"write_arp"};
        MemoryJournal j;
        auto r = run_repair(ops, j, "rep-arp2", opt);
        EF_CHECK(!r.ok && r.failed_at == SetupStep::Arp && r.rollback_clean && ops.m == m);
    }
    {   // Ayni surum: ARP yazilmaz.
        FakeSetupOps ops;
        ops.m = m;
        MemoryJournal j;
        RepairOptions same = opt;
        same.running_version = L"0.3.0";
        auto r = run_repair(ops, j, "rep-arp3", same);
        EF_CHECK(r.ok && std::find(ops.log.begin(), ops.log.end(), "write_arp") == ops.log.end());
    }
    {   // Baska konumun kaydi: dokunulmaz.
        FakeSetupOps ops;
        ops.m = m;
        ops.m.arp->install_location = L"D:\\elsewhere";
        MemoryJournal j;
        auto r = run_repair(ops, j, "rep-arp4", opt);
        EF_CHECK(r.ok && ops.m.arp->display_version == L"0.3.0");
        EF_CHECK(std::find(ops.log.begin(), ops.log.end(), "write_arp") == ops.log.end());
    }
    {   // Arp adimi Done, Commit kaydi yazilamadi: onarim geri alinir, ARP onceki degerinde.
        FakeSetupOps probe_ops;
        probe_ops.m = m;
        MemoryJournal count;
        (void)run_repair(probe_ops, count, "rep-count", opt);
        FakeSetupOps ops;
        ops.m = m;
        MemoryJournal j;
        j.fail_append_at = count.appends - 2;  // Commit Started (sonra Committed)
        auto r = run_repair(ops, j, "rep-arp5", opt);
        EF_CHECK(!r.ok && r.failed_at == SetupStep::Commit && ops.m.arp == m.arp);
    }
}

// ---- Ortak masaustu kisayolu (sahip 2026-11-13: "kurulunca masaustune ikon gelmedi") ----

EF_TEST(setup_tx_desktop_shortcut_prior_same_named_preserved_on_failure) {
    // Ayni adli onceki masaustu kisayolu (baska hedef) vardi; sonraki adim basarisiz -> onceki hedef geri gelir.
    Model start = fresh();
    start.desktop = L"D:\\portable\\EcuFps.exe";
    FakeSetupOps ops;
    ops.m = start;
    ops.fail.insert("write_service");
    MemoryJournal j;
    auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
    EF_CHECK(!r.ok && r.failed_at == SetupStep::ServiceCreate && r.rollback_clean);
    EF_CHECK(ops.m.desktop == std::optional<std::wstring>(L"D:\\portable\\EcuFps.exe"));
    EF_CHECK(ops.m == start);
    // Basarili kurulum onu kurulu EXE'ye cevirir; kaldirma yalniz kaldirir (onceki hedef geri yazilmaz).
    FakeSetupOps ok;
    ok.m = start;
    MemoryJournal j2;
    EF_CHECK(run_install(ok, j2, arp_of(L"0.3.0"), "tx2").ok);
    EF_CHECK(ok.m.desktop == kNewTarget);
    MemoryJournal j3;
    EF_CHECK(run_uninstall_program(ok, j3, "un").ok);
    EF_CHECK(!ok.m.desktop);
    EF_CHECK(std::count(ok.log.begin(), ok.log.end(), "restore_desktop_shortcut") == 0);
}

EF_TEST(setup_tx_desktop_shortcut_failure_and_partial_roll_back) {
    for (bool part : {false, true}) {
        FakeSetupOps ops;
        ops.m = fresh();
        (part ? ops.partial : ops.fail).insert("write_desktop_shortcut");
        MemoryJournal j;
        auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
        EF_CHECK(!r.ok && r.failed_at == SetupStep::DesktopShortcut && r.rollback_clean);
        EF_CHECK(ops.m == fresh() && !ops.m.desktop && !ops.m.shortcut);
        EF_CHECK(std::find(ops.log.begin(), ops.log.end(), "write_service") == ops.log.end());
    }
    {  // Geri alinamayan masaustu kisayolu raporlanir
        FakeSetupOps ops;
        ops.fail = {"write_arp", "restore_desktop_shortcut"};
        MemoryJournal j;
        auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
        EF_CHECK(!r.ok && !r.rollback_clean && r.undo_failed == std::vector<SetupStep>{SetupStep::DesktopShortcut});
        EF_CHECK(ops.m.desktop == kNewTarget && !ops.m.shortcut);
    }
}

EF_TEST(setup_tx_desktop_shortcut_power_loss_recovers_prior) {
    const Model start = existing();
    for (SetupStep cut : {SetupStep::DesktopShortcut, SetupStep::ServiceCreate}) {
        FakeSetupOps ops;
        ops.m = start;
        MemoryJournal j;
        run_install(ops, j, arp_of(L"0.3.0"), "tx", cut);
        EF_CHECK(ops.m.desktop == kNewTarget);
        auto rec = recover_interrupted(ops, j);
        EF_CHECK(rec.ok && rec.rollback_clean && ops.m == start && j.lines.empty());
    }
    {  // Started yazildi, Done yok (yarim yazilmis kisayol) -> kurtarma onceki durumu getirir
        FakeSetupOps ops;
        MemoryJournal j;
        run_install(ops, j, arp_of(L"0.3.0"), "tx", SetupStep::Shortcut);
        JournalEntry e;
        e.seq = 99;
        e.tx_id = "tx";
        e.step = SetupStep::DesktopShortcut;
        e.rec = StepRecord::Started;
        EF_CHECK(j.append(e).ok());
        ops.m.desktop = kNewTarget;
        EF_CHECK(recover_interrupted(ops, j).ok && ops.m == fresh());
    }
}

EF_TEST(setup_tx_desktop_prior_decodes_from_030_journal_without_fields) {
    // 0.3.0 Probe kaydinda masaustu alanlari yok: "yoktu" sayilir (kurtarma yalniz bizim kisayolumuzu siler).
    const char* old_prior =
        R"({"dir":false,"exe":false,"shortcut":false,"service":false,"running":false,"updates":false,"shortcut_target":null,"service_config":null,"arp":null})";
    auto p = decode_prior(old_prior);
    EF_CHECK(p.ok() && !p.value().desktop_existed && !p.value().desktop_target);
    EF_CHECK(!decode_prior(
                   R"({"dir":false,"exe":false,"shortcut":false,"service":false,"running":false,"updates":false,"shortcut_target":null,"service_config":null,"arp":null,"desktop":"x"})")
                   .ok());
    FakeSetupOps ops;
    MemoryJournal j;
    JournalEntry e;
    e.seq = 1;
    e.tx_id = "old";
    e.step = SetupStep::Probe;
    e.rec = StepRecord::Done;
    e.prior_json = old_prior;
    EF_CHECK(j.append(e).ok());
    e.seq = 2;
    e.step = SetupStep::DesktopShortcut;
    e.rec = StepRecord::Started;
    e.prior_json.clear();
    EF_CHECK(j.append(e).ok());
    ops.m.desktop = kNewTarget;
    EF_CHECK(recover_interrupted(ops, j).ok && !ops.m.desktop);
}

EF_TEST(setup_tx_uninstall_removes_desktop_shortcut_and_restores_on_failure) {
    const Model inst = installed_from(existing());
    EF_CHECK(inst.desktop == kNewTarget);
    FakeSetupOps ops;
    ops.m = inst;
    ops.fail.insert("delete_service");  // masaustu kaldirildiktan sonra hata -> geri yazilir
    MemoryJournal j;
    auto r = run_uninstall_program(ops, j, "un");
    EF_CHECK(!r.ok && r.failed_at == SetupStep::ServiceDelete && r.rollback_clean);
    EF_CHECK(ops.m.desktop == kNewTarget && ops.m == inst);
}

EF_TEST(setup_tx_desktop_link_actions) {
    const std::wstring exe = kNewTarget;
    const std::optional<std::wstring> ours = L"c:\\program files\\ecu fps\\ECUFPS.EXE", other = L"D:\\Games\\x.exe", none;
    EF_CHECK(desktop_remove_action(false, none, exe) == LinkAction::Nothing);
    EF_CHECK(desktop_remove_action(true, ours, exe) == LinkAction::Delete);
    EF_CHECK(desktop_remove_action(true, other, exe) == LinkAction::Nothing);  // kullanicinin ayni adli kisayolu
    EF_CHECK(desktop_remove_action(true, none, exe) == LinkAction::Nothing);   // okunamayan dosya: dokunulmaz
    PriorState p;
    EF_CHECK(desktop_restore_action(p, false, none, exe) == LinkAction::Nothing);
    EF_CHECK(desktop_restore_action(p, true, ours, exe) == LinkAction::Delete);
    EF_CHECK(desktop_restore_action(p, true, none, exe) == LinkAction::Delete);  // Probe'dan sonra yazilan yarim dosya
    p.desktop_existed = true;
    p.desktop_target = other;
    EF_CHECK(desktop_restore_action(p, true, ours, exe) == LinkAction::Write);
    EF_CHECK(desktop_restore_action(p, false, none, exe) == LinkAction::Write);
    EF_CHECK(desktop_restore_action(p, true, none, exe) == LinkAction::Write);
    EF_CHECK(desktop_restore_action(p, true, L"d:\\games\\X.EXE", exe) == LinkAction::Nothing);  // zaten yerinde
    p.desktop_target.reset();  // onceki dosyanin hedefi okunamamisti
    EF_CHECK(desktop_restore_action(p, true, none, exe) == LinkAction::Nothing);
    EF_CHECK(desktop_restore_action(p, true, other, exe) == LinkAction::Nothing);
    EF_CHECK(desktop_restore_action(p, true, ours, exe) == LinkAction::Fail);  // ustune yazdik, geri getiremeyiz
    EF_CHECK(desktop_restore_action(p, false, none, exe) == LinkAction::Fail);
}

EF_TEST(setup_tx_uninstall_keeps_foreign_same_named_desktop_shortcut) {
    // Kurulumdan sonra kullanici ayni adla baska bir hedefe kisayol koydu / okunamayan dosya birakti: kaldirma silmez,
    // kaldirma geri alinirsa da yeniden yazilmaz.
    for (bool unreadable : {false, true}) {
        Model inst = installed_from(fresh());
        inst.desktop = L"D:\\Games\\other.exe";
        inst.desktop_unreadable = unreadable;
        {
            FakeSetupOps ops;
            ops.m = inst;
            ops.journals_clean = false;
            MemoryJournal j;
            auto r = run_uninstall_program(ops, j, "un");
            EF_CHECK(r.ok && !ops.m.shortcut);
            EF_CHECK(ops.m.desktop == std::optional<std::wstring>(L"D:\\Games\\other.exe") && ops.m.desktop_unreadable == unreadable);
        }
        {
            FakeSetupOps ops;
            ops.m = inst;
            ops.fail.insert("delete_service");
            MemoryJournal j;
            auto r = run_uninstall_program(ops, j, "un");
            EF_CHECK(!r.ok && r.rollback_clean && ops.m == inst);
        }
    }
}

EF_TEST(setup_tx_desktop_unreadable_prior_overwritten_reports_undo_failed) {
    // Ayni adli okunamayan onceki dosya: yazma basarisizsa (dokunulmadi) geri alma temiz; ustune yazildiysa geri
    // getirilemez ve bu UndoFailed olarak raporlanir (sessizce "temiz" denmez).
    Model start = fresh();
    start.desktop = L"?";
    start.desktop_unreadable = true;
    {
        FakeSetupOps ops;
        ops.m = start;
        ops.fail.insert("write_desktop_shortcut");
        MemoryJournal j;
        auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
        EF_CHECK(!r.ok && r.rollback_clean && ops.m == start);
    }
    {
        FakeSetupOps ops;
        ops.m = start;
        ops.fail.insert("write_service");
        MemoryJournal j;
        auto r = run_install(ops, j, arp_of(L"0.3.0"), "tx");
        EF_CHECK(!r.ok && !r.rollback_clean && r.undo_failed == std::vector<SetupStep>{SetupStep::DesktopShortcut});
    }
}
