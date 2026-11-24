// Spec 2026-11-12 H2 (ADR-031): surucu kurulumu cekirdegi. Sahte Windows Update arabirimi; gercek arama / indirme /
// kurulum YAPILMAZ, yukseltme yok. Kabul / red kurallari, sonuc kodu eslemesi, iptal, journal kaydi, broker izin listesi ve
// istemci bildirimi.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "core/drivers/driver_install.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::drivers;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::TxState;

namespace {

constexpr const char* kId = "0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6";
constexpr int kRev = 201;

InstallCandidate good() {
    InstallCandidate c;
    c.update.title = "Realtek Semiconductor Corp. - Net - 10.72.1203.2025";
    c.update.driver_class = "Net";
    c.update.hardware_id = "PCI\\VEN_10EC&DEV_8126";
    c.update.update_id = "0F6F7E1A-1B2C-4D5E-8F90-A1B2C3D4E5F6";  // WUA buyuk harf dondurebilir
    c.update.revision = kRev;
    c.type_driver = true;
    c.from_windows_update = true;
    return c;
}

struct Fake final : DriverInstallApi {
    FindOutcome found{true, false, 0, {good()}};
    StageOutcome dl{kWuaSucceeded, 0, false};
    StageOutcome in{kWuaSucceeded, 0, false};
    RestorePointState rp = RestorePointState::Created;
    int finds = 0, downloads = 0, installs = 0, rps = 0;
    std::vector<InstallRef> asked;
    std::function<void()> during_download;
    FindOutcome find(const InstallRef& ref, const std::function<bool()>&) override {
        ++finds;
        asked.push_back(ref);
        return found;
    }
    bool select(std::size_t index) override { return index < found.found.size(); }
    RestorePointState restore_point() override {
        ++rps;
        return rp;
    }
    StageOutcome download(const std::function<bool()>& stop) override {
        ++downloads;
        if (during_download) during_download();
        if (stop && stop()) return {kWuaAborted, 0, false};
        return dl;
    }
    StageOutcome install() override {
        ++installs;
        return in;
    }
};

struct Rig {
    recovery::MemoryJournalFs fs;
    recovery::JournalStore store{fs, "machine-1"};
    Fake api;
    bool cancel = false, gate = true;
    int ids = 0;
    InstallDeps deps() {
        InstallDeps d;
        d.api = &api;
        d.journal = &store;
        d.new_tx_id = [this] {
            char b[33];
            std::snprintf(b, sizeof b, "%032x", ++ids);
            return std::string(b);
        };
        d.now_unix_ms = [] { return std::int64_t{1000}; };
        d.cancel = [this] { return cancel; };
        d.gate_ok = [this] { return gate; };
        d.actor_sid = d.target_sid = "S-1-5-21-1";
        return d;
    }
    InstallRun run() { return run_driver_install(deps(), install_target(kId, kRev)); }
    std::vector<recovery::JournalTx> txs() { return store.scan().transactions; }
};

}  // namespace

EF_TEST(h2_install_target_format_is_exact) {
    const auto t = install_target(kId, kRev);
    EF_CHECK(t == std::string("wuinst:") + kId + ":201");
    EF_CHECK(install_target("0F6F7E1A-1B2C-4D5E-8F90-A1B2C3D4E5F6", 1) == "wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6:1");
    const auto p = parse_install_target(t);
    EF_CHECK(p && p->update_id == kId && p->revision == kRev);
    for (const char* bad : {"wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6", "wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6:0",
                            "wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6:01", "wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6:1a",
                            "wuinst:{0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6}:1", "wuinst:0f6f7e1a1b2c4d5e8f90a1b2c3d4e5f6:1",
                            "wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5fg:1", "wuinst:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6:1234567890",
                            "drv:0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6:1", "wuinst:../../x:1", ""})
        EF_CHECK(!parse_install_target(bad));
    EF_CHECK(install_target("not-a-guid", 1).empty() && install_target(kId, 0).empty());
    EF_CHECK(install_criteria({kId, 7}) == std::wstring(L"UpdateID='") + L"0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6" + L"' and RevisionNumber=7");
}

EF_TEST(h2_accepts_only_that_exact_windows_update_driver) {
    const InstallRef ref{kId, kRev};
    std::size_t at = 99;
    EF_CHECK(!check_candidates(ref, {good()}, &at) && at == 0);
    auto c = good();
    EF_CHECK(check_candidates(ref, {}, &at) == InstallOutcome::NotFound);
    c.update.revision = kRev + 1;  // baska revizyon kabul edilmez
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::NotFound);
    c = good();
    c.update.update_id = "11111111-2222-3333-4444-555555555555";
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::NotFound);
    EF_CHECK(check_candidates(ref, {good(), good()}, &at) == InstallOutcome::Ambiguous);
    EF_CHECK(check_candidates(ref, {good(), c}, &at) == InstallOutcome::Ambiguous);  // yalniz tek oge
    c = good();
    c.type_driver = false;
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::NotDriver);
    c = good();
    c.from_windows_update = false;  // WSUS / baska hizmet
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::NotWindowsUpdate);
    c = good();
    c.hidden = true;
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::Hidden);
    c = good();
    c.eula_accepted = false;  // lisans onayi kullanici adina verilmez
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::NeedsUserInput);
    c = good();
    c.needs_user_input = true;
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::NeedsUserInput);
    c = good();
    c.installed = true;
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::AlreadyInstalled);
}

EF_TEST(h2_firmware_is_always_rejected) {
    const InstallRef ref{kId, kRev};
    std::size_t at = 0;
    for (const char* cls : {"Firmware", "firmware", "{F2E7DD72-6468-4E36-B6F1-6488F42C1B52}", "f2e7dd72-6468-4e36-b6f1-6488f42c1b52"}) {
        auto c = good();
        c.update.driver_class = cls;
        EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::Firmware);
    }
    auto c = good();
    c.update.hardware_id = "UEFI\\RES_{11112222-3333-4444-5555-666677778888}";
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::Firmware);
    c.type_driver = false, c.from_windows_update = false;  // firmware nedeni once gelir
    EF_CHECK(check_candidates(ref, {c}, &at) == InstallOutcome::Firmware);
    Rig rig;
    rig.api.found.found = {c};
    const auto r = rig.run();
    EF_CHECK(r.outcome == InstallOutcome::Firmware && rig.api.downloads == 0 && rig.api.installs == 0 && rig.api.rps == 0 && rig.txs().empty());
}

EF_TEST(h2_result_code_mapping) {
    EF_CHECK(!map_download({kWuaSucceeded, 0, false}) && !map_download({kWuaSucceededWithErrors, 0, false}));
    EF_CHECK(map_download({kWuaAborted, 0, false}) == InstallOutcome::Cancelled);
    for (int rc : {kWuaNotStarted, kWuaInProgress, kWuaFailed, 9}) EF_CHECK(map_download({rc, 0, false}) == InstallOutcome::DownloadFailed);
    EF_CHECK(map_install({kWuaSucceeded, 0, true}) == InstallOutcome::Installed);
    EF_CHECK(map_install({kWuaSucceededWithErrors, 0, false}) == InstallOutcome::InstalledWithErrors);
    EF_CHECK(map_install({kWuaAborted, 0, false}) == InstallOutcome::Cancelled);
    for (int rc : {kWuaNotStarted, kWuaInProgress, kWuaFailed}) EF_CHECK(map_install({rc, 0, false}) == InstallOutcome::InstallFailed);
    InstallRun r;
    r.outcome = InstallOutcome::InstallFailed;
    r.result_code = kWuaFailed;
    r.hresult = static_cast<std::int32_t>(0x80240022u);
    r.restore_point = RestorePointState::Failed;
    r.previous_interrupted = true;
    const auto enc = encode_install_detail(r);
    EF_CHECK(enc == "di:install_failed:rc=4:hr=80240022:rp=failed:prev");
    const auto dec = decode_install_detail(enc);
    EF_CHECK(dec && dec->outcome == r.outcome && dec->result_code == 4 && dec->hresult == r.hresult && dec->restore_point == RestorePointState::Failed &&
              dec->previous_interrupted && !dec->reboot_required);
    for (const char* bad : {"", "rp:created", "di:bogus:rc=2:hr=00000000:rp=none", "di:installed:rc=7:hr=00000000:rp=none",
                            "di:installed:rc=2:hr=0000000:rp=none", "di:installed:rc=2:hr=00000000:rp=maybe", "di:installed:rc=2:hr=00000000:rp=none:x"})
        EF_CHECK(!decode_install_detail(bad));
}

EF_TEST(h2_successful_install_searches_again_and_is_journalled) {
    Rig rig;
    rig.api.in = {kWuaSucceeded, 0, true};
    const auto r = rig.run();
    EF_CHECK(r.outcome == InstallOutcome::Installed && r.reboot_required && r.restore_point == RestorePointState::Created);
    EF_CHECK(rig.api.finds == 1 && rig.api.asked.size() == 1 && rig.api.asked[0] == (InstallRef{kId, kRev}));  // yeniden arama
    EF_CHECK(r.title == good().update.title);  // yardimcinin buldugu baslik (arayuzden gelen degil)
    EF_CHECK(rig.api.rps == 1 && rig.api.downloads == 1 && rig.api.installs == 1);
    const auto txs = rig.txs();
    EF_CHECK(txs.size() == 1);
    if (txs.size() == 1) {
        EF_CHECK(txs[0].state == TxState::Active && txs[0].steps.size() == 1);
        EF_CHECK(txs[0].steps[0].op.id == OperationId::DriverInstall && txs[0].steps[0].op.target == install_target(kId, kRev));
        EF_CHECK(txs[0].steps[0].state == recovery::StepState::Verified && txs[0].steps[0].last_written == "installed:reboot");
    }
}

EF_TEST(h2_no_restore_point_is_reported_not_hidden) {
    Rig rig;
    rig.api.rp = RestorePointState::Failed;
    const auto r = rig.run();
    EF_CHECK(r.outcome == InstallOutcome::Installed && r.restore_point == RestorePointState::Failed);
    EF_CHECK(encode_install_detail(r).find(":rp=failed") != std::string::npos);
}

EF_TEST(h2_journal_record_comes_before_any_download) {
    Rig rig;
    rig.fs.fail_write_after = 0;  // kalici kayit yazilamiyor
    const auto r = rig.run();
    EF_CHECK(r.outcome == InstallOutcome::JournalFailed && rig.api.rps == 0 && rig.api.downloads == 0 && rig.api.installs == 0);
    InstallDeps d = rig.deps();
    d.journal = nullptr;
    EF_CHECK(run_driver_install(d, install_target(kId, kRev)).outcome == InstallOutcome::JournalFailed && rig.api.finds == 1);
}

EF_TEST(h2_bad_target_and_search_failure_start_nothing) {
    Rig rig;
    EF_CHECK(run_driver_install(rig.deps(), "wuinst:x:1").outcome == InstallOutcome::BadTarget && rig.api.finds == 0);
    rig.api.found = FindOutcome{false, false, static_cast<std::int32_t>(0x8024402Cu), {}};
    const auto r = rig.run();
    EF_CHECK(r.outcome == InstallOutcome::SearchFailed && r.hresult == static_cast<std::int32_t>(0x8024402Cu) && rig.api.downloads == 0);
    EF_CHECK(rig.txs().empty());
}

EF_TEST(h2_download_and_install_failures_are_distinct) {
    Rig dl;
    dl.api.dl = {kWuaFailed, static_cast<std::int32_t>(0x80240022u), false};
    auto r = dl.run();
    EF_CHECK(r.outcome == InstallOutcome::DownloadFailed && r.result_code == kWuaFailed && dl.api.installs == 0);
    EF_CHECK(dl.txs().size() == 1 && dl.txs()[0].state == TxState::Restored);  // kurulum baslamadi: surucu degismedi
    Rig in;
    in.api.in = {kWuaFailed, static_cast<std::int32_t>(0x80070005u), false};
    r = in.run();
    EF_CHECK(r.outcome == InstallOutcome::InstallFailed && r.hresult == static_cast<std::int32_t>(0x80070005u) && !r.reboot_required);
    EF_CHECK(in.txs().size() == 1 && in.txs()[0].state == TxState::RecoveryRequired);  // denendi: son durum dogrulanmadi
    Rig warn;
    warn.api.in = {kWuaSucceededWithErrors, 0, false};
    EF_CHECK(warn.run().outcome == InstallOutcome::InstalledWithErrors && warn.txs()[0].state == TxState::Active);
}

EF_TEST(h2_cancel_before_install_never_installs) {
    Rig before;
    before.cancel = true;
    EF_CHECK(before.run().outcome == InstallOutcome::Cancelled && before.api.finds == 0 && before.api.installs == 0);
    Rig mid;
    mid.api.during_download = [&mid] { mid.cancel = true; };  // indirme sirasinda durduruldu
    const auto r = mid.run();
    EF_CHECK(r.outcome == InstallOutcome::Cancelled && mid.api.downloads == 1 && mid.api.installs == 0);
    EF_CHECK(mid.txs().size() == 1 && mid.txs()[0].state == TxState::Restored);
    Rig search;
    search.api.found = FindOutcome{false, true, 0, {}};
    EF_CHECK(search.run().outcome == InstallOutcome::Cancelled && search.api.downloads == 0);
}

EF_TEST(h2_game_gate_is_rechecked_before_download_and_install) {
    Rig closed;
    closed.gate = false;
    EF_CHECK(closed.run().outcome == InstallOutcome::GateBlocked && closed.api.downloads == 0 && closed.txs().empty());
    Rig late;
    late.api.during_download = [&late] { late.gate = false; };  // indirme sirasinda oyun acildi
    EF_CHECK(late.run().outcome == InstallOutcome::GateBlocked && late.api.installs == 0);
    EF_CHECK(late.txs().size() == 1 && late.txs()[0].state == TxState::Restored);
}

EF_TEST(h2_interrupted_previous_install_is_reported_and_closed) {
    Rig rig;
    recovery::JournalTx old;
    old.id = std::string(32, 'a');
    old.created_unix_ms = 5;
    recovery::JournalStep st;
    st.op = {OperationId::DriverInstall, OperationScope::Machine, install_target(kId, kRev), std::string(kInstallValue), false, false};
    old.steps.push_back(st);
    EF_CHECK(rig.store.begin(old).ok());
    old.state = TxState::Applying;  // onceki oturumda indirme / kurulum sirasinda kesildi
    EF_CHECK(rig.store.persist(old).ok());
    const auto r = rig.run();
    EF_CHECK(r.previous_interrupted && r.outcome == InstallOutcome::Installed);
    bool closed = false;
    for (const auto& tx : rig.txs())
        if (tx.id == old.id) closed = tx.state == TxState::RecoveryRequired && tx.steps[0].error == "interrupted";
    EF_CHECK(closed);
}

EF_TEST(h2_broker_allowlist_and_action_binding) {
    ef::contracts::SettingOperation op{OperationId::DriverInstall, OperationScope::Machine, install_target(kId, kRev), std::string(kInstallValue), false, false};
    EF_CHECK(broker::admin_operation_allowed(op) && broker::admin_apply_allowed(op));
    auto bad = op;
    bad.new_value = "uninstall";
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad = op;
    bad.target = "wuinst:C:\\drivers\\x.inf:1";  // yol / INF yok
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad = op;
    bad.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(bad));

    broker::ExecutorDeps d;
    d.ctx.elevated = true;
    d.ctx.machine_lock_held = true;
    d.ctx.broker_user_sid = d.ctx.target_user_sid = "S-1-5-21-1";
    int calls = 0;
    std::string seen;
    d.driver_install = [&](std::string_view t) {
        ++calls;
        seen = std::string(t);
        InstallRun r;
        r.outcome = InstallOutcome::Installed;
        r.result_code = kWuaSucceeded;
        r.reboot_required = true;
        r.restore_point = RestorePointState::Recent;
        return r;
    };
    broker::AdminExecutor ex(d);
    const auto r = ex.handle(ipc::RequestAction::DriverInstall, op);
    EF_CHECK(calls == 1 && seen == op.target && r.state == broker::StepState::Applied && r.pending_reboot);
    EF_CHECK(r.detail == "di:installed:rc=2:hr=00000000:rp=recent:reboot");
    EF_CHECK(ex.handle(ipc::RequestAction::Fix, op).state == broker::StepState::Rejected && calls == 1);  // ayar yolundan gecmez
    const ef::contracts::SettingOperation maint{OperationId::MaintenanceJob, OperationScope::Machine, maintenance::kImageCheckTarget,
                                                 maintenance::kMaintenanceValue, false, false};
    EF_CHECK(ex.handle(ipc::RequestAction::DriverInstall, maint).state == broker::StepState::Rejected);
    d.permit = []() -> recovery::WriteDecision { return {false, "gate.game_running"}; };
    broker::AdminExecutor closed(d);
    EF_CHECK(closed.handle(ipc::RequestAction::DriverInstall, op).state == broker::StepState::GateBlocked && calls == 1);
    d.permit = nullptr;
    d.driver_install = [](std::string_view) {
        InstallRun x;
        x.outcome = InstallOutcome::Firmware;
        return x;
    };
    broker::AdminExecutor fw(d);
    EF_CHECK(fw.handle(ipc::RequestAction::DriverInstall, op).state == broker::StepState::Rejected);
    // Protokol: eylem adi gidip gelir.
    EF_CHECK(ipc::to_string(ipc::RequestAction::DriverInstall) == "driver_install");
}

EF_TEST(h2_client_notice_mapping) {
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Denied;
    EF_CHECK(broker::driver_install_notice(b).key == "updates.install.denied");
    b.elevation = ElevationOutcome::Failed;
    EF_CHECK(broker::driver_install_notice(b).key == "updates.install.start_failed");
    b.elevation = ElevationOutcome::Started;
    EF_CHECK(broker::driver_install_notice(b).key == "updates.install.unknown");
    broker::StepResult s{install_target(kId, kRev), broker::StepState::Failed, false, "no response"};
    s.unknown = true;
    b.steps = {s};
    EF_CHECK(broker::driver_install_notice(b).key == "updates.install.unknown");
    const auto with = [&](InstallOutcome o, RestorePointState rp, bool reboot, std::int32_t hr = 0) {
        InstallRun r;
        r.outcome = o, r.restore_point = rp, r.reboot_required = reboot, r.hresult = hr;
        broker::StepResult st{install_target(kId, kRev), broker::StepState::Applied, reboot, encode_install_detail(r)};
        b.steps = {st};
        return broker::driver_install_notice(b);
    };
    auto n = with(InstallOutcome::Installed, RestorePointState::Created, true);
    EF_CHECK(n.key == "updates.install.done" && n.installed && n.reboot && !n.warn && !n.no_restore_point);
    n = with(InstallOutcome::Installed, RestorePointState::Failed, false);
    EF_CHECK(n.key == "updates.install.done" && n.no_restore_point && n.warn);
    n = with(InstallOutcome::InstallFailed, RestorePointState::Created, true, static_cast<std::int32_t>(0x80070005u));
    EF_CHECK(n.key == "updates.install.failed" && n.warn && !n.reboot && !n.installed && n.hresult == static_cast<std::int32_t>(0x80070005u));
    EF_CHECK(with(InstallOutcome::Firmware, RestorePointState::NotAttempted, false).key == "updates.install.firmware");
    EF_CHECK(with(InstallOutcome::NotWindowsUpdate, RestorePointState::NotAttempted, false).key == "updates.install.rejected");
    EF_CHECK(with(InstallOutcome::Cancelled, RestorePointState::Created, false).key == "updates.install.cancelled");
    EF_CHECK(with(InstallOutcome::AlreadyInstalled, RestorePointState::NotAttempted, false).key == "updates.install.already");
    EF_CHECK(with(InstallOutcome::DownloadFailed, RestorePointState::Created, false, 5).hresult == 5);
    EF_CHECK(with(InstallOutcome::GateBlocked, RestorePointState::NotAttempted, false).key == "updates.install.gate");
    b.steps = {{install_target(kId, kRev), broker::StepState::Rejected, false, "operation not in admin allowlist"}};
    EF_CHECK(broker::driver_install_notice(b).key == "updates.install.rejected");
}
