// OPT-02 / OPT-10: yonetici yurutme yolu birim testleri. Gercek UAC, pipe, kayit defteri veya NVIDIA surucusu
// KULLANILMAZ: sahte baslatici + bellek ici cerceve tasiyici (ServerSession uzerinden) + FakeHags/FakeDrs.
#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>
#include <string_view>

#include "core/adapters/nvidia/drs.h"
#include "core/adapters/windows/defender_exclusion.h"
#include "core/adapters/windows/fast_startup.h"
#include "core/adapters/windows/hags.h"
#include "core/adapters/windows/ifeo_priority.h"
#include "core/adapters/windows/power_subs.h"
#include "core/adapters/windows/service_start_type.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/broker/admin.h"
#include "core/ipc/protocol.h"
#include "core/perf/admin_plan.h"
#include "core/perf/admin_sequencing.h"
#include "core/perf/controller.h"
#include "core/perf/journal_backend.h"
#include "core/readiness/readiness.h"
#include "core/recovery/conflicts.h"
#include "core/recovery/engine.h"
#include "core/recovery/history_view.h"
#include "core/recovery/journal.h"
#include "core/recovery/restore_point.h"
#include "ef_test.h"

using namespace ef::core;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using ef::contracts::TxState;
namespace rec = ef::core::recovery;
namespace nv = ef::core::adapters::nvidia;

namespace {
// A3: tek NVIDIA donanim GPU'su (MOCK) -> melez kapisi acik. Plan ogesi testlerinin ortak girdisi.
const perf::NvidiaGate kNvOk = [] {
    ef::core::inventory::GpuAdapter g;
    g.index = 0;
    g.vendor_id = ef::contracts::Field<std::uint32_t>::known(ef::core::inventory::kVendorNvidia, ef::contracts::Provenance::Api);
    g.kind = ef::core::inventory::GpuKind::Discrete;
    return perf::nvidia_adapter_gate({g}, std::nullopt, true);
}();
}  // namespace
namespace cw = ef::core::adapters::windows;

namespace {
constexpr std::uint32_t kUiPid = 4242;
const std::string kUserSid = "S-1-5-21-1-2-3-1001";

SettingOperation hags_op(const char* v = "2") { return {OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", v, true, false}; }
// F-48: hedef bicimi "faststartup:hiberboot"; uygulama yalniz "0" (kapat) gonderir.
SettingOperation fast_startup_op(const char* v = "0") {
    return {OperationId::FastStartupSet, OperationScope::Machine, "faststartup:hiberboot", v, false, false};
}
// F-32/F-33/F-42: hedef bicimi "power:<ayar>:ac"; uygulama yalniz katalog hedef degerini gonderir.
SettingOperation power_sub_op(std::string_view target = cw::kPowerBoostAcTarget, const char* v = "1") {
    return {OperationId::PowerSettingSet, OperationScope::Machine, std::string(target), v, false, false};
}
SettingOperation drs_op(nv::DrsSetting s, std::uint32_t v) {
    nv::DrsTarget t;
    t.setting = static_cast<std::uint32_t>(s);
    return {OperationId::VendorProfileValue, OperationScope::Machine, nv::format_drs_target(t), nv::DrsAdapter::encode(v), false, false};
}
// WIN-10: hedef bicimi "svcstop:<ad>"; uygulanabilir tek deger "stopped" (onceki durum geri almada).
SettingOperation svc_op(const char* name = "Spooler", const char* v = "stopped") {
    return {OperationId::ServiceSessionStop, OperationScope::Machine, cw::format_session_stop_target(name), v, false, false};
}
adapters::startup::ServiceInfo spooler_info() {
    adapters::startup::ServiceInfo s;
    s.name = "Spooler";
    s.display_name = "Print Spooler";
    s.image_path = "C:\\Windows\\System32\\spoolsv.exe";
    s.microsoft = true;
    s.own_process = true;
    s.running = true;
    return s;
}

std::string tx_id(int& n) {
    char b[33];
    std::snprintf(b, sizeof b, "%032d", ++n);
    return b;
}

// Yazma aninda journal'da baslangic (Applying) kaydi var mi kontrol eden HAGS sahtesi.
class ProbeHags final : public cw::HagsApi {
public:
    explicit ProbeHags(rec::JournalStore* s) : store(s) {}
    ef::contracts::Result<std::optional<std::uint32_t>> read_pref() override { return inner.read_pref(); }
    ef::contracts::Status write_pref(std::uint32_t v) override {
        record();
        auto s = inner.write_pref(v);
        if (crash_after_write) {  // M1: deger yazildi, surec olur (journal "Intent/Applying"de kalir)
            crash_after_write = false;
            throw SimulatedCrash();
        }
        return s;
    }
    struct SimulatedCrash {};
    bool crash_after_write = false;
    ef::contracts::Status remove_pref() override {
        record();
        return inner.remove_pref();
    }
    std::optional<bool> supported() override { return inner.supported(); }
    void record() {
        bool journaled = false;
        if (store)
            for (const auto& tx : store->scan().transactions)
                if (tx.state != TxState::Prepared && !tx.steps.empty()) journaled = true;
        writes_without_journal += journaled ? 0 : 1;
    }
    cw::FakeHags inner;
    rec::JournalStore* store;
    int writes_without_journal = 0;
};

// Broker tarafi: makine journal'i + izin listesindeki adaptorler.
struct Broker {
    rec::MemoryJournalFs perf_fs, fixes_fs;
    rec::JournalStore perf_store{perf_fs, "machine-1"};
    rec::JournalStore fixes_store{fixes_fs, "machine-1"};
    ProbeHags hags_api{&perf_store};
    cw::HagsAdapter hags{hags_api};
    cw::FakeFastStartup fast_startup_api;
    cw::FastStartupAdapter fast_startup{fast_startup_api};
    cw::FakePowerSubApi power_subs_api;
    cw::PowerSubAdapter power_subs{power_subs_api};
    nv::FakeDrs drs_api;
    nv::DrsAdapter drs{drs_api};
    // WIN-10: oturumluk hizmet durdurma (izin listesi + mesguliyet sahtesi; SCM'ye dokunulmaz).
    adapters::startup::FakeServiceApi svc_api;
    cw::FakeServiceBusyProbe svc_probe;
    cw::ServiceSessionStopAdapter svc_stop{svc_api, svc_probe};
    cw::ServiceStartTypeAdapter svc_start{svc_api, svc_probe};  // T12
    cw::FakeDefenderApi def_api;  // T11
    cw::DefenderExclusionAdapter def_adapter{def_api};
    cw::FakeMachineRegistry ifeo_reg;  // T09
    cw::IfeoPriorityAdapter ifeo_adapter{&ifeo_reg};
    rec::AdapterRegistry reg;
    rec::TransactionEngine perf_engine{perf_store, reg};
    rec::TransactionEngine fixes_engine{fixes_store, reg};
    int ids = 0;
    std::unique_ptr<broker::AdminExecutor> exec;
    // T08: sahte geri yukleme noktasi (SRSetRestorePoint cagrilmaz); sonuc testten ayarlanir.
    rec::RestorePointOutcome rp_outcome = rec::RestorePointOutcome::Created;
    int rp_calls = 0;

    explicit Broker(bool elevated = true, bool lock = true) {
        reg.add(OperationId::HagsSet, &hags);
        reg.add(OperationId::FastStartupSet, &fast_startup);
        reg.add(OperationId::PowerSettingSet, &power_subs);
        reg.add(OperationId::VendorProfileValue, &drs);
        reg.add(OperationId::ServiceSessionStop, &svc_stop);
        reg.add(OperationId::ServiceStartTypeSet, &svc_start);
        reg.add(OperationId::DefenderExclusionSet, &def_adapter);
        reg.add(OperationId::GamePriorityHint, &ifeo_adapter);
        broker::ExecutorDeps d;
        d.perf_store = &perf_store;
        d.perf_engine = &perf_engine;
        d.fixes_store = &fixes_store;
        d.fixes_engine = &fixes_engine;
        d.new_tx_id = [this] { return tx_id(ids); };
        d.create_restore_point = [this]() -> rec::RestorePointRun { ++rp_calls; return {rp_outcome, 7, ""}; };
        d.now_unix_ms = [this] { return std::int64_t{1000} * ids; };
        d.ctx.target_user_sid = kUserSid;
        d.ctx.broker_user_sid = kUserSid;
        d.ctx.elevated = elevated;
        d.ctx.machine_lock_held = lock;
        exec = std::make_unique<broker::AdminExecutor>(d);
    }
    std::size_t active(rec::JournalStore& s) {
        std::size_t n = 0;
        for (const auto& tx : s.scan().transactions)
            if (tx.state == TxState::Active) n += tx.steps.size();
        return n;
    }
};

// Cerceveleri gercek ServerSession'a (nonce + PID el sikismasi) ileten bellek ici tasiyici.
class LoopbackTransport final : public broker::FrameTransport {
public:
    LoopbackTransport(const std::string& nonce, std::uint32_t pid, ipc::ServerSession::Handler h) : session_(nonce, pid, std::move(h)) {}
    ef::contracts::Status send(const ipc::Frame& f) override {
        auto bytes = ipc::encode(f);  // gercek kodlama/cozme yolu
        auto back = ipc::decode(bytes);
        if (!back.ok()) return ef::contracts::Status(back.error());
        replies_.push_back(session_.on_frame(back.value(), kUiPid));
        ++sent;
        return ef::contracts::ok_status();
    }
    ef::contracts::Result<ipc::Frame> receive() override {
        if (replies_.empty()) return ef::contracts::Result<ipc::Frame>::fail(ef::contracts::ErrorCode::IoError, "no reply");
        auto f = replies_.front();
        replies_.pop_front();
        return f;
    }
    int sent = 0;

private:
    ipc::ServerSession session_;
    std::deque<ipc::Frame> replies_;
};

class FakeLauncher final : public ElevationLauncher {
public:
    ElevationOutcome launch_elevated(const LaunchArgs& a, std::string& detail) override {
        ++calls;
        args = a;
        detail = outcome == ElevationOutcome::Denied ? "ERROR_CANCELLED" : "";
        return outcome;
    }
    ElevationOutcome outcome = ElevationOutcome::Started;
    LaunchArgs args;
    int calls = 0;
};

// JournalBackend'in gordugu sahte kanal: her run() bir UAC (sayilir); broker bellek ici.
class MockChannel final : public broker::AdminChannel {
public:
    explicit MockChannel(Broker& b) : b_(b) {}
    broker::BatchResult run(ipc::RequestAction a, const std::vector<SettingOperation>& ops) override {
        ++uac_prompts;
        broker::Connector c = [this]() -> ef::contracts::Result<std::unique_ptr<broker::FrameTransport>> {
            // run_broker ile ayni sira: istemci dogrulandi -> oturum basi uzlastirma -> istekler (M1)
            last_reconcile = b_.exec->reconcile_on_start();
            if (drop_replies) return std::unique_ptr<broker::FrameTransport>(new DropTransport());
            return std::unique_ptr<broker::FrameTransport>(new LoopbackTransport(launcher.args.nonce, kUiPid, b_.exec->handler()));
        };
        return broker::run_admin_batch(launcher, kUiPid, "0123456789abcdef0123456789abcdef", c, a, ops);
    }
    // BrokerChannel ile ayni salt okuma kurallari (core yardimcilari)
    std::vector<SettingOperation> active_ops() override { return broker::journal_active_ops(b_.perf_store.scan(), true); }
    bool recovery_needed() override { return !broker::journal_unreconciled_ops(b_.perf_store.scan(), true).empty(); }
    std::vector<SettingOperation> fix_active_ops() override { return broker::journal_active_ops(b_.fixes_store.scan(), false); }
    std::vector<SettingOperation> fix_recovery_ops() override { return broker::journal_unreconciled_ops(b_.fixes_store.scan(), true); }
    std::vector<SettingOperation> fix_conflict_ops() override { return broker::journal_conflict_ops(b_.fixes_store.scan()); }
    void fix_target_lists(std::vector<std::string>& a, std::vector<std::string>& f, std::vector<std::string>& i,
                          std::vector<std::string>& c) override {
        broker::journal_fix_targets(b_.fixes_store.scan(), a, f, i, c);
    }
    std::vector<SettingOperation> unreconciled_ops() override {
        auto out = broker::journal_unreconciled_ops(b_.perf_store.scan(), true);
        for (auto& op : broker::journal_unreconciled_ops(b_.fixes_store.scan(), true)) {
            const bool dup = std::any_of(out.begin(), out.end(), [&](const SettingOperation& o) { return o.id == op.id && o.target == op.target; });
            if (!dup) out.push_back(op);
        }
        return out;
    }
    // El sikisma tamam, sonra yanit gelmiyor (broker coktu / pipe koptu).
    class DropTransport final : public broker::FrameTransport {
    public:
        ef::contracts::Status send(const ipc::Frame& f) override {
            last = f.type;
            return ef::contracts::ok_status();
        }
        ef::contracts::Result<ipc::Frame> receive() override {
            if (last == ipc::MsgType::Hello) return ipc::Frame{ipc::MsgType::HelloAck, 1, "{}"};
            return ef::contracts::Result<ipc::Frame>::fail(ef::contracts::ErrorCode::IoError, "pipe broken");
        }
        ipc::MsgType last = ipc::MsgType::Error;
    };
    FakeLauncher launcher;
    int uac_prompts = 0;
    bool drop_replies = false;
    broker::SessionReconcile last_reconcile;

private:
    Broker& b_;
};
}  // namespace

EF_TEST(admin_allowlist_only_fixed_operations) {
    EF_CHECK(broker::admin_operation_allowed(hags_op("2")));
    EF_CHECK(!broker::admin_operation_allowed(hags_op("1")));  // L1: yalniz hedef deger 2 (acik)
    EF_CHECK(!broker::admin_operation_allowed(hags_op("0")));
    EF_CHECK(!broker::admin_operation_allowed(hags_op("cmd.exe")));
    auto user_scope = hags_op();
    user_scope.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(user_scope));
    auto other = hags_op();
    other.target = "hags:other";
    EF_CHECK(!broker::admin_operation_allowed(other));
    // F-48: yalniz sabit hedef + "0" (kapat); "1" istemciden kabul edilmez.
    EF_CHECK(broker::admin_operation_allowed(fast_startup_op("0")));
    EF_CHECK(!broker::admin_operation_allowed(fast_startup_op("1")));
    EF_CHECK(!broker::admin_operation_allowed(fast_startup_op("cmd.exe")));
    auto fs_user = fast_startup_op();
    fs_user.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(fs_user));
    auto fs_other = fast_startup_op();
    fs_other.target = "faststartup:other";
    EF_CHECK(!broker::admin_operation_allowed(fs_other));
    // F-32/F-33/F-42: yalniz uc sabit ayar ve katalog hedef degeri; baska deger/hedef kabul edilmez.
    EF_CHECK(broker::admin_operation_allowed(power_sub_op(cw::kPowerBoostAcTarget, "1")));
    EF_CHECK(broker::admin_operation_allowed(power_sub_op(cw::kPowerParkingAcTarget, "100")));
    EF_CHECK(broker::admin_operation_allowed(power_sub_op(cw::kPowerAspmAcTarget, "0")));
    EF_CHECK(!broker::admin_operation_allowed(power_sub_op(cw::kPowerBoostAcTarget, "2")));
    EF_CHECK(!broker::admin_operation_allowed(power_sub_op(cw::kPowerParkingAcTarget, "0")));
    EF_CHECK(!broker::admin_operation_allowed(power_sub_op(cw::kPowerAspmAcTarget, "2")));
    EF_CHECK(!broker::admin_operation_allowed(power_sub_op("power:other:ac", "1")));
    auto ps_user = power_sub_op();
    ps_user.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(ps_user));
    EF_CHECK(broker::admin_operation_allowed(drs_op(nv::DrsSetting::PowerMode, 1)));
    EF_CHECK(broker::admin_operation_allowed(drs_op(nv::DrsSetting::MaxPrerendered, 1)));
    EF_CHECK(broker::admin_operation_allowed(drs_op(nv::DrsSetting::ShaderCacheSize, broker::kShaderCacheTargetMb)));
    // M3: birim dogrulanana kadar yeni yazma kapali (eski kaydin geri alinmasi icin bicim izinli kalir)
    EF_CHECK(!broker::kShaderCacheItemEnabled);
    EF_CHECK(!broker::admin_apply_allowed(drs_op(nv::DrsSetting::ShaderCacheSize, broker::kShaderCacheTargetMb)));
    EF_CHECK(broker::admin_apply_allowed(drs_op(nv::DrsSetting::PowerMode, 1)) && broker::admin_apply_allowed(hags_op("2")));
    {
        Broker b;
        b.drs_api.global[0x00AC8497] = 5120;
        const auto r = b.exec->handle(ipc::RequestAction::Apply, drs_op(nv::DrsSetting::ShaderCacheSize, broker::kShaderCacheTargetMb));
        EF_CHECK(r.state == broker::StepState::Rejected && b.drs_api.global[0x00AC8497] == 5120);
    }
    EF_CHECK(!broker::admin_operation_allowed(drs_op(nv::DrsSetting::PowerMode, 0)));        // yalniz sabit hedef deger
    EF_CHECK(!broker::admin_operation_allowed(drs_op(nv::DrsSetting::ShaderCacheSize, 1)));  // keyfi boyut yok
    EF_CHECK(!broker::admin_operation_allowed(drs_op(nv::DrsSetting::VSync, 1)));            // grupta olmayan ayar
    SettingOperation app{OperationId::VendorProfileValue, OperationScope::Machine, "drs:app:cs2.exe:1057EB71", "00000001", false, false};
    EF_CHECK(broker::admin_operation_allowed(app));  // A1.1: oyun profili yalniz sabit deger ve ayarla (ayrintisi alttaki testte)
    SettingOperation power{OperationId::PowerModeSet, OperationScope::Machine, "overlay:x", "best", false, false};
    EF_CHECK(!broker::admin_operation_allowed(power));
}

// WIN-10: oturumluk hizmet durdurma izin listesi dar ve derlenmis; keyfi hizmet/deger kabul edilmez.
EF_TEST(admin_service_stop_allowlist_is_compiled_and_narrow) {
    EF_CHECK(broker::admin_operation_allowed(svc_op("Spooler")));
    EF_CHECK(broker::admin_operation_allowed(svc_op("spooler")));  // SCM adi buyuk/kucuk harf duyarsiz
    auto wrong_value = svc_op();
    wrong_value.new_value = "running";  // geri alma snapshot'tan yapilir; istek degeri yalniz "stopped"
    EF_CHECK(!broker::admin_operation_allowed(wrong_value));
    auto user_scope = svc_op();
    user_scope.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(user_scope));
    auto unlisted = svc_op("Winmgmt");
    EF_CHECK(!broker::admin_operation_allowed(unlisted));
    auto malformed = svc_op();
    malformed.target = "svc:Spooler";
    EF_CHECK(!broker::admin_operation_allowed(malformed));
    malformed.target = "svcstop:";
    EF_CHECK(!broker::admin_operation_allowed(malformed));
    malformed.target = "svcstop:Spooler start";  // bosluk/eylem eklenemez
    EF_CHECK(!broker::admin_operation_allowed(malformed));
}

// WIN-10: plan yalniz izinli + calisan + onkosullari gecen hizmeti alir; bos secim bos plan.
EF_TEST(admin_service_stop_plan_only_for_valid_running_selection) {
    adapters::startup::FakeServiceApi api;
    api.services["Spooler"] = spooler_info();
    auto fax = spooler_info();
    fax.name = "Fax";
    fax.image_path = "C:\\Windows\\System32\\fxssvc.exe";
    fax.running = false;
    api.services["Fax"] = fax;
    cw::FakeServiceBusyProbe probe;
    EF_CHECK(perf::service_stop_plan_items(api, probe, {}).empty());
    const auto items = perf::service_stop_plan_items(api, probe, {"Spooler"});
    EF_CHECK(items.size() == 1 && items[0].needs_admin && items[0].group() == "plan.services");
    EF_CHECK(items[0].op.target == "svcstop:Spooler" && items[0].op.new_value == "stopped");
    EF_CHECK(broker::admin_operation_allowed(items[0].op));
    EF_CHECK(perf::service_stop_plan_items(api, probe, {"Fax"}).empty());      // zaten durmus: is yok
    EF_CHECK(perf::service_stop_plan_items(api, probe, {"Winmgmt"}).empty());  // izin listesi disi
    probe.busy_by_service["Spooler"] = true;                                    // kuyrukta is var
    EF_CHECK(perf::service_stop_plan_items(api, probe, {"Spooler"}).empty());
    probe.busy_by_service["Spooler"] = false;
    api.services["Spooler"].microsoft = false;  // sahte ikili: kimlik dogrulanmaz
    EF_CHECK(perf::service_stop_plan_items(api, probe, {"Spooler"}).empty());
}

EF_TEST(admin_service_stop_applies_and_restores_exact_run_state) {
    Broker b;
    b.svc_api.services["Spooler"] = spooler_info();
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, svc_op()).state == broker::StepState::Applied);
    EF_CHECK(!b.svc_api.services["Spooler"].running && b.svc_api.stops == 1);
    EF_CHECK(b.active(b.perf_store) == 1);
    // Ayni hedefe ikinci uygulama ozgun calisma durumunu ezemez
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, svc_op()).state == broker::StepState::Blocked);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, svc_op()).state == broker::StepState::Restored);
    EF_CHECK(b.svc_api.services["Spooler"].running && b.svc_api.starts == 1);
    EF_CHECK(b.active(b.perf_store) == 0);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, svc_op()).state == broker::StepState::NothingToDo);

    // Onceden durmus hizmet: snapshot "stopped"; geri alma onu BASLATMAZ.
    Broker c;
    auto stopped = spooler_info();
    stopped.running = false;
    c.svc_api.services["Spooler"] = stopped;
    EF_CHECK(c.exec->handle(ipc::RequestAction::Apply, svc_op()).state == broker::StepState::Applied);  // no-op
    EF_CHECK(c.exec->handle(ipc::RequestAction::Restore, svc_op()).state == broker::StepState::Restored);
    EF_CHECK(!c.svc_api.services["Spooler"].running && c.svc_api.starts == 0);

    // Izın listesi disi/yanlis deger dispatcher'da reddedilir; adaptore hic gidilmez.
    Broker d;
    d.svc_api.services["Spooler"] = spooler_info();
    auto wrong = svc_op();
    wrong.new_value = "running";
    EF_CHECK(d.exec->handle(ipc::RequestAction::Apply, wrong).state == broker::StepState::Rejected);
    EF_CHECK(d.svc_api.services["Spooler"].running && d.svc_api.stops == 0);
    EF_CHECK(d.exec->handle(ipc::RequestAction::Apply, svc_op("Winmgmt")).state == broker::StepState::Rejected);
    EF_CHECK(d.svc_api.stops == 0);
}

// T12: ozellik hizmeti baslangic tipi: izin yalniz tablo + "disabled"; journal'li uygula/geri al.
EF_TEST(admin_service_start_type_allowlist_is_compiled_and_narrow) {
    const auto op = [](const char* name, const char* v = "disabled") {
        return SettingOperation{OperationId::ServiceStartTypeSet, OperationScope::Machine, cw::format_service_start_target(name), v, false, false};
    };
    for (auto n : {"bthserv", "BTAGService", "Spooler", "PrintNotify", "XblAuthManager", "XblGameSave", "XboxNetApiSvc", "XboxGipSvc", "WSearch", "wsearch"})
        EF_CHECK(broker::admin_operation_allowed(op(n)));
    for (auto n : {"Audiosrv", "Dnscache", "WinDefend", "wuauserv", "GamingServices", "vgc", "EasyAntiCheat", "Winmgmt", "mpssvc"})
        EF_CHECK(!broker::admin_operation_allowed(op(n)));
    EF_CHECK(!broker::admin_operation_allowed(op("bthserv", "manual")));
    EF_CHECK(!broker::admin_operation_allowed(op("bthserv", "automatic")));
    auto user_scope = op("bthserv");
    user_scope.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(user_scope));
    auto bad = op("bthserv");
    bad.target = "svcstart:bthserv start";
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad.target = "svcstop:bthserv";
    EF_CHECK(!broker::admin_operation_allowed(bad));
}

EF_TEST(admin_service_start_type_applies_and_restores_exact_start_type) {
    const SettingOperation op{OperationId::ServiceStartTypeSet, OperationScope::Machine, "svcstart:bthserv", "disabled", false, false};
    Broker b;
    auto s = spooler_info();
    s.name = "bthserv";
    s.image_path = "C:\\Windows\\System32\\svchost.exe -k bthsvcs";
    s.start = adapters::startup::StartType::AutomaticDelayed;
    b.svc_api.services["bthserv"] = s;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, op).state == broker::StepState::Applied);
    EF_CHECK(b.svc_api.services["bthserv"].start == adapters::startup::StartType::Disabled && !b.svc_api.services["bthserv"].running);
    EF_CHECK(b.active(b.perf_store) == 1);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, op).state == broker::StepState::Restored);
    EF_CHECK(b.svc_api.services["bthserv"].start == adapters::startup::StartType::AutomaticDelayed && b.svc_api.services["bthserv"].running);
    // Tablo disi/yanlis deger dispatcher'da reddedilir; adaptore hic gidilmez.
    SettingOperation wrong = op;
    wrong.new_value = "manual";
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, wrong).state == broker::StepState::Rejected);
    wrong = op;
    wrong.target = "svcstart:Audiosrv";
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, wrong).state == broker::StepState::Rejected);
}

// T11: Defender dislamasi: broker izni yalniz gecerli oyun klasoru bicimi + "1"; kok/sistem/profil reddedilir; uygula/geri al journal'li.
EF_TEST(admin_defender_exclusion_allowlist_rejects_roots_system_and_profile) {
    const auto op = [](const char* path, const char* v = "1") {
        return SettingOperation{OperationId::DefenderExclusionSet, OperationScope::Machine, cw::defender_target_for_path(path), v, false, false};
    };
    EF_CHECK(broker::admin_operation_allowed(op("D:\\SteamLibrary\\steamapps\\common\\Portal")));
    EF_CHECK(!broker::admin_operation_allowed(op("D:\\SteamLibrary\\steamapps\\common\\Portal", "0")));
    // Gecersiz yollar icin hedef bile uretilemez (bos hedef) ve broker reddeder.
    for (const char* p : {"C:\\", "D:\\Games", "C:\\Windows", "C:\\Windows\\System32", "C:\\Users\\Ali", "C:\\Program Files", "D:\\Lib\\steamapps\\common"}) {
        EF_CHECK(cw::defender_target_for_path(p).empty());
        EF_CHECK(!broker::admin_operation_allowed(op(p)));
    }
    auto user_scope = op("D:\\Games\\Epic\\Fortnite");
    user_scope.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(user_scope));
    auto raw = op("D:\\Games\\Epic\\Fortnite");
    raw.target = "defx:633a5c77696e646f7773";  // "c:\windows" hex'i elle verilse de
    EF_CHECK(!broker::admin_operation_allowed(raw));
}

EF_TEST(admin_defender_exclusion_applies_and_restores_through_broker) {
    const SettingOperation op{OperationId::DefenderExclusionSet, OperationScope::Machine,
                              cw::defender_target_for_path("D:\\Games\\Epic\\Fortnite"), "1", false, false};
    {
        Broker b;
        EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, op).state == broker::StepState::Applied);
        EF_CHECK(b.def_api.paths.size() == 1 && b.active(b.perf_store) == 1);
        EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, op).state == broker::StepState::Restored);
        EF_CHECK(b.def_api.paths.empty());
    }
    {
        Broker b;  // onceden dislanmis: geri alma kaldirmaz
        b.def_api.paths = {"d:\\games\\epic\\fortnite"};
        EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, op).state == broker::StepState::Applied);
        EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, op).state == broker::StepState::Restored);
        EF_CHECK(b.def_api.paths.size() == 1 && b.def_api.removes == 0);
    }
    {
        Broker b;  // Defender etkin antivirus degil: yazilmaz
        b.def_api.usable_value = false;
        EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, op).state != broker::StepState::Applied);
        EF_CHECK(b.def_api.adds == 0);
    }
}

EF_TEST(admin_hags_adapter_exact_rollback) {
    for (const bool existed : {false, true}) {
        Broker b;
        if (existed) b.hags_api.inner.value = 1;
        const auto r = b.exec->handle(ipc::RequestAction::Apply, hags_op());
        EF_CHECK(r.state == broker::StepState::Applied && r.pending_reboot);
        EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2));
        EF_CHECK(b.hags_api.writes_without_journal == 0);  // once kalici baslangic kaydi, sonra yazma
        EF_CHECK(b.active(b.perf_store) == 1);
        // Ayni hedefe ikinci uygulama ozgun anlik goruntuyu ezemez (Blocked: geri alinmamis kayit var)
        EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Blocked);
        const auto back = b.exec->handle(ipc::RequestAction::Restore, hags_op());
        EF_CHECK(back.state == broker::StepState::Restored && back.pending_reboot);
        if (existed) EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));
        else EF_CHECK(!b.hags_api.inner.value.has_value() && b.hags_api.inner.removes == 1);  // varsayilan uydurulmaz
        EF_CHECK(b.active(b.perf_store) == 0);
        EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, hags_op()).state == broker::StepState::NothingToDo);
    }
}

EF_TEST(admin_fast_startup_apply_restore_exact) {
    EF_CHECK(perf::admin_fix_target("hags:hwschmode") && perf::admin_fix_target("faststartup:hiberboot"));
    EF_CHECK(!perf::admin_fix_target("gaming:game_mode"));
    for (const bool existed : {false, true}) {
        Broker b;
        if (existed) b.fast_startup_api.value = 1;
        auto r = b.exec->handle(ipc::RequestAction::Apply, fast_startup_op());
        EF_CHECK(r.state == broker::StepState::Applied && !r.pending_reboot);  // yeniden baslatma gerekmez
        EF_CHECK(b.fast_startup_api.value == std::optional<std::uint32_t>(0));
        EF_CHECK(b.active(b.perf_store) == 1);  // once kalici baslangic kaydi, sonra yazma
        // Ayni hedefe ikinci uygulama ozgun anlik goruntuyu ezemez
        EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, fast_startup_op()).state == broker::StepState::Blocked);
        const auto back = b.exec->handle(ipc::RequestAction::Restore, fast_startup_op());
        EF_CHECK(back.state == broker::StepState::Restored && !back.pending_reboot);
        if (existed) EF_CHECK(b.fast_startup_api.value == std::optional<std::uint32_t>(1));
        else EF_CHECK(!b.fast_startup_api.value.has_value() && b.fast_startup_api.removes == 1);  // varsayilan uydurulmaz
        EF_CHECK(b.active(b.perf_store) == 0);
        EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, fast_startup_op()).state == broker::StepState::NothingToDo);
    }
}

EF_TEST(admin_power_subs_apply_and_restore_exact) {
    Broker b;
    b.power_subs_api.schemes[b.power_subs_api.active][std::string(cw::kPowerBoostAcTarget)] = 4;
    auto r = b.exec->handle(ipc::RequestAction::Apply, power_sub_op());
    EF_CHECK(r.state == broker::StepState::Applied && !r.pending_reboot);  // guc plani degeri hemen gecerli
    EF_CHECK(b.power_subs_api.schemes[b.power_subs_api.active][std::string(cw::kPowerBoostAcTarget)] == 1);
    EF_CHECK(b.active(b.perf_store) == 1);  // once kalici baslangic kaydi, sonra yazma
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, power_sub_op()).state == broker::StepState::Blocked);
    const auto back = b.exec->handle(ipc::RequestAction::Restore, power_sub_op());
    EF_CHECK(back.state == broker::StepState::Restored && !back.pending_reboot);
    EF_CHECK(b.power_subs_api.schemes[b.power_subs_api.active][std::string(cw::kPowerBoostAcTarget)] == 4);
    EF_CHECK(b.active(b.perf_store) == 0);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, power_sub_op()).state == broker::StepState::NothingToDo);

    Broker c;
    c.power_subs_api.fail_write = true;
    EF_CHECK(c.exec->handle(ipc::RequestAction::Apply, power_sub_op()).state != broker::StepState::Applied);
    EF_CHECK(c.active(c.perf_store) == 0);
}

EF_TEST(admin_fast_startup_fix_undo_and_write_failure) {
    Broker b;
    b.fast_startup_api.value = 1;
    MockChannel ch(b);
    (void)ch.run(ipc::RequestAction::Fix, {perf::fast_startup_off_operation()});
    EF_CHECK(ch.fix_active_ops().size() == 1 && ch.active_ops().empty());  // Performans kapsami degil
    EF_CHECK(b.fast_startup_api.value == std::optional<std::uint32_t>(0));
    const auto un = ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
    EF_CHECK(un.steps.size() == 1 && un.steps[0].state == broker::StepState::Restored && !un.steps[0].pending_reboot);
    EF_CHECK(b.fast_startup_api.value == std::optional<std::uint32_t>(1));
    EF_CHECK(ch.fix_active_ops().empty());

    Broker c;
    c.fast_startup_api.value = 1;
    c.fast_startup_api.fail_write = true;
    EF_CHECK(c.exec->handle(ipc::RequestAction::Apply, fast_startup_op()).state != broker::StepState::Applied);
    EF_CHECK(c.fast_startup_api.value == std::optional<std::uint32_t>(1) && c.active(c.perf_store) == 0);
}

EF_TEST(admin_hags_unsupported_or_write_failure_changes_nothing) {
    Broker b;
    b.hags_api.inner.is_supported = false;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Failed);
    EF_CHECK(!b.hags_api.inner.value.has_value() && b.active(b.perf_store) == 0);
    Broker c;
    c.hags_api.inner.value = 1;
    c.hags_api.inner.fail_write = true;
    EF_CHECK(c.exec->handle(ipc::RequestAction::Apply, hags_op()).state != broker::StepState::Applied);
    EF_CHECK(c.hags_api.inner.value == std::optional<std::uint32_t>(1) && c.active(c.perf_store) == 0);
}

EF_TEST(admin_journal_failure_blocks_write) {
    Broker b;
    b.perf_fs.fail_write_after = 0;  // baslangic kaydi yazilamaz
    const auto r = b.exec->handle(ipc::RequestAction::Apply, hags_op());
    EF_CHECK(r.state == broker::StepState::Failed);
    EF_CHECK(b.hags_api.inner.writes == 0 && !b.hags_api.inner.value.has_value());
}

EF_TEST(admin_dispatcher_requires_elevation_and_machine_lock) {
    Broker unelevated(false, true);
    EF_CHECK(unelevated.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Rejected);
    EF_CHECK(unelevated.hags_api.inner.writes == 0);
    Broker unlocked(true, false);
    EF_CHECK(unlocked.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Rejected);
    EF_CHECK(unlocked.hags_api.inner.writes == 0);
    Broker b;
    SettingOperation vsync = drs_op(nv::DrsSetting::VSync, 1);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, vsync).state == broker::StepState::Rejected);
    EF_CHECK(b.drs_api.global.empty());
}

EF_TEST(admin_drs_global_apply_and_inherited_rollback) {
    Broker b;
    b.drs_api.defaults[static_cast<std::uint32_t>(nv::DrsSetting::PowerMode)] = 5;        // OPTIMAL_POWER (miras)
    b.drs_api.global[static_cast<std::uint32_t>(nv::DrsSetting::MaxPrerendered)] = 3;     // acik deger
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, drs_op(nv::DrsSetting::PowerMode, 1)).state == broker::StepState::Applied);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, drs_op(nv::DrsSetting::MaxPrerendered, 1)).state == broker::StepState::Applied);
    EF_CHECK(b.drs_api.global[0x1057EB71] == 1 && b.drs_api.global[0x007BA09E] == 1);
    const auto r1 = b.exec->handle(ipc::RequestAction::Restore, drs_op(nv::DrsSetting::PowerMode, 1));
    const auto r2 = b.exec->handle(ipc::RequestAction::Restore, drs_op(nv::DrsSetting::MaxPrerendered, 1));
    EF_CHECK(r1.state == broker::StepState::Restored && !r1.pending_reboot);
    EF_CHECK(r2.state == broker::StepState::Restored);
    EF_CHECK(b.drs_api.global.count(0x1057EB71) == 0);  // miras geri geldi (acik override silindi)
    EF_CHECK(b.drs_api.global[0x007BA09E] == 3);         // acik eski deger birebir
}

EF_TEST(admin_fix_journal_is_separate_from_performance) {
    Broker b;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Fix, hags_op()).state == broker::StepState::Applied);
    EF_CHECK(b.active(b.fixes_store) == 1 && b.active(b.perf_store) == 0);
    // Performans Kapali (Restore) duzeltmeyi geri almaz
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, hags_op()).state == broker::StepState::NothingToDo);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2));
}

EF_TEST(admin_ipc_action_roundtrip_and_strict_fields) {
    std::vector<ipc::RequestAction> seen;
    ipc::ServerSession s("0123456789abcdef0123456789abcdef", kUiPid,
                         [&](const std::string&, ipc::RequestAction a, const SettingOperation&) {
                             seen.push_back(a);
                             return std::string("{}");
                         });
    EF_CHECK(s.on_frame(ipc::make_hello("0123456789abcdef0123456789abcdef", kUiPid, 1), kUiPid).type == ipc::MsgType::HelloAck);
    EF_CHECK(s.on_frame(ipc::make_request("a", hags_op(), 2), kUiPid).type == ipc::MsgType::Response);
    EF_CHECK(s.on_frame(ipc::make_request("b", hags_op(), 3, ipc::RequestAction::Restore), kUiPid).type == ipc::MsgType::Response);
    EF_CHECK(s.on_frame(ipc::make_request("c", hags_op(), 4, ipc::RequestAction::Fix), kUiPid).type == ipc::MsgType::Response);
    EF_CHECK(seen.size() == 3 && seen[0] == ipc::RequestAction::Apply && seen[1] == ipc::RequestAction::Restore &&
              seen[2] == ipc::RequestAction::Fix);
    ipc::Frame bad{ipc::MsgType::Request, 5,
                   R"({"request_id":"d","action":"shell","op":{"op":"hags_set","scope":"machine","target":"hags:hwschmode","value":"2"}})"};
    EF_CHECK(s.on_frame(bad, kUiPid).type == ipc::MsgType::Error);  // bilinmeyen eylem -> oturum kapanir
    EF_CHECK(s.closed());
}

EF_TEST(admin_result_codec_roundtrip) {
    broker::StepResult r{"hags:hwschmode", broker::StepState::Applied, true, "ok"};
    auto d = broker::decode_result(broker::encode_result("r1", r));
    EF_CHECK(d.ok() && d.value().state == broker::StepState::Applied && d.value().pending_reboot && d.value().target == r.target);
    EF_CHECK(!broker::decode_result(R"({"target":"x","state":"owned","pending_reboot":false})").ok());
    EF_CHECK(!broker::decode_result("not json").ok());
}

EF_TEST(admin_batch_uac_denied_writes_nothing) {
    Broker b;
    FakeLauncher l;
    l.outcome = ElevationOutcome::Denied;
    int connects = 0;
    broker::Connector c = [&]() -> ef::contracts::Result<std::unique_ptr<broker::FrameTransport>> {
        ++connects;
        return std::unique_ptr<broker::FrameTransport>(new LoopbackTransport(l.args.nonce, kUiPid, b.exec->handler()));
    };
    const auto r = broker::run_admin_batch(l, kUiPid, "0123456789abcdef0123456789abcdef", c, ipc::RequestAction::Apply, {hags_op()});
    EF_CHECK(r.elevation == ElevationOutcome::Denied && r.steps.empty() && connects == 0);
    EF_CHECK(b.hags_api.inner.writes == 0 && b.active(b.perf_store) == 0);
}

EF_TEST(admin_batch_one_uac_for_whole_group) {
    Broker b;
    FakeLauncher l;
    broker::Connector c = [&]() -> ef::contracts::Result<std::unique_ptr<broker::FrameTransport>> {
        return std::unique_ptr<broker::FrameTransport>(new LoopbackTransport(l.args.nonce, kUiPid, b.exec->handler()));
    };
    const std::vector<SettingOperation> ops{hags_op(), drs_op(nv::DrsSetting::PowerMode, 1), drs_op(nv::DrsSetting::VSync, 1)};
    const auto r = broker::run_admin_batch(l, kUiPid, "0123456789abcdef0123456789abcdef", c, ipc::RequestAction::Apply, ops);
    EF_CHECK(l.calls == 1 && l.args.role == Role::Broker && l.args.parent_pid == kUiPid);  // tek UAC
    EF_CHECK(r.elevation == ElevationOutcome::Started && r.steps.size() == 3);
    EF_CHECK(r.steps[0].state == broker::StepState::Applied && r.steps[0].pending_reboot);
    EF_CHECK(r.steps[1].state == broker::StepState::Applied);
    EF_CHECK(r.steps[2].state == broker::StepState::Rejected);  // izin listesi disi: gonderilmedi bile
    EF_CHECK(b.drs_api.global.count(static_cast<std::uint32_t>(nv::DrsSetting::VSync)) == 0);
}

EF_TEST(admin_batch_wrong_nonce_is_rejected_by_broker) {
    Broker b;
    FakeLauncher l;
    broker::Connector c = [&]() -> ef::contracts::Result<std::unique_ptr<broker::FrameTransport>> {
        // Broker baska bir nonce bekliyor (gasp edilmis/bayat oturum): el sikisma reddedilir, yazma yok.
        return std::unique_ptr<broker::FrameTransport>(new LoopbackTransport("ffffffffffffffffffffffffffffffff", kUiPid, b.exec->handler()));
    };
    const auto r = broker::run_admin_batch(l, kUiPid, "0123456789abcdef0123456789abcdef", c, ipc::RequestAction::Apply, {hags_op()});
    EF_CHECK(r.elevation == ElevationOutcome::Started);
    EF_CHECK(r.steps.size() == 1 && r.steps[0].state == broker::StepState::Failed);
    EF_CHECK(b.hags_api.inner.writes == 0);
}

EF_TEST(admin_plan_items_only_when_needed_and_supported) {
    cw::FakeHags h;
    h.is_supported = std::nullopt;
    EF_CHECK(perf::hags_plan_items(h).empty());  // destek bilinmiyor: onerilmez
    h.is_supported = false;
    EF_CHECK(perf::hags_plan_items(h).empty());
    h.is_supported = true;
    h.value = 2;
    EF_CHECK(perf::hags_plan_items(h).empty());  // zaten acik
    h.value = 1;
    auto items = perf::hags_plan_items(h);
    EF_CHECK(items.size() == 1 && items[0].needs_admin && items[0].op.needs_reboot && items[0].group() == "plan.hags");
    EF_CHECK(broker::admin_operation_allowed(items[0].op));

    EF_CHECK(perf::nvidia_plan_items(nullptr, kNvOk).empty());  // NVIDIA yok: grup yok
    nv::FakeDrs d;
    d.defaults[0x1057EB71] = 5;
    d.defaults[0x007BA09E] = 0;
    d.defaults[0x00AC8497] = 0x4000;  // surucu varsayilani (basliktaki DEFAULT) >= 10 GB: dokunulmaz
    auto nvi = perf::nvidia_plan_items(&d, kNvOk);
    EF_CHECK(nvi.size() == 2);
    for (const auto& i : nvi) EF_CHECK(i.needs_admin && i.group() == "plan.nvidia" && broker::admin_operation_allowed(i.op));
    d.global[0x00AC8497] = 5120;  // 5 GB: M3 — birim dogrulanana kadar shader ogesi plana GIRMEZ
    EF_CHECK(perf::nvidia_plan_items(&d, kNvOk).size() == 2);
    for (const auto& i : perf::nvidia_plan_items(&d, kNvOk)) EF_CHECK(i.op.target.find("00AC8497") == std::string::npos);
    d.global[0x00AC8497] = 0;  // anlami belgesiz: dokunulmaz
    EF_CHECK(perf::nvidia_plan_items(&d, kNvOk).size() == 2);
    d.global[0x1057EB71] = 1;
    d.global[0x007BA09E] = 1;
    EF_CHECK(perf::nvidia_plan_items(&d, kNvOk).empty());
}

namespace {
struct UserSide {
    rec::MemoryJournalFs fs;
    rec::JournalStore store{fs, "machine-1"};
    rec::FakeAdapter fake;
    rec::AdapterRegistry reg;
    rec::TransactionEngine eng{store, reg};
    int ids = 100;
    UserSide() {
        fake.values["anim"] = "0";
        reg.add(OperationId::VisualEffectsSet, &fake);
    }
    perf::JournalBackendDeps deps(broker::AdminChannel* admin) {
        perf::JournalBackendDeps d;
        d.scope = {"user", &store, &eng};
        d.new_tx_id = [this] { return tx_id(ids); };
        d.now_unix_ms = [this] { return std::int64_t{1000} * ids; };
        d.admin = admin;
        return d;
    }
};

perf::Plan mixed_plan() {
    perf::Plan p;
    perf::PlanItem u;
    u.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, "anim", "1", false, false};
    u.title_key = u.group_key = "plan.vfx";
    p.items.push_back(u);
    perf::PlanItem h;
    h.op = perf::hags_on_operation();
    h.title_key = h.group_key = "plan.hags";
    h.needs_admin = true;
    p.items.push_back(h);
    perf::PlanItem n;
    n.op = drs_op(nv::DrsSetting::PowerMode, 1);
    n.title_key = n.group_key = "plan.nvidia";
    n.needs_admin = true;
    p.items.push_back(n);
    return p;
}
}  // namespace

EF_TEST(admin_backend_uac_refused_applies_rest_and_reports_partial) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    ch.launcher.outcome = ElevationOutcome::Denied;
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    const auto out = be.apply(mixed_plan(), {}, cancel);
    EF_CHECK(out.kind == perf::OutcomeKind::Active && out.admin_skipped && out.changes == 1);
    EF_CHECK(u.fake.values["anim"] == "1");                                       // kullanici kapsami uygulandi
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));           // yonetici grubu dokunulmadi
    EF_CHECK(b.drs_api.global.empty() && ch.uac_prompts == 1);
    EF_CHECK(be.active_changes() == 1);
    // Kapali: yalniz kullanici adimi vardi; UAC istenmez
    EF_CHECK(be.restore({}).kind == perf::OutcomeKind::Restored && u.fake.values["anim"] == "0" && ch.uac_prompts == 1);

    // Yalniz yonetici grubu secilmis ve UAC reddedildi: hicbir sey degismedi -> Denied
    perf::Plan only_admin = mixed_plan();
    only_admin.items.erase(only_admin.items.begin());
    EF_CHECK(be.apply(only_admin, {}, cancel).kind == perf::OutcomeKind::Denied);
}

EF_TEST(admin_backend_single_uac_apply_readback_and_exact_restore) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.drs_api.defaults[0x1057EB71] = 5;
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    const auto out = be.apply(mixed_plan(), {}, cancel);
    EF_CHECK(out.kind == perf::OutcomeKind::Active && out.changes == 3 && !out.admin_skipped && out.pending_reboot);
    EF_CHECK(ch.uac_prompts == 1 && ch.launcher.calls == 1);  // tek Evet -> tek UAC
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2) && b.drs_api.global[0x1057EB71] == 1);
    EF_CHECK(be.active_changes() == 3);

    // Kapali, ama bu kez UAC reddedildi: kullanici adimi geri alinir, yonetici ayarlari etkin kalir (Acik + Denied)
    ch.launcher.outcome = ElevationOutcome::Denied;
    const auto denied = be.restore({});
    EF_CHECK(denied.kind == perf::OutcomeKind::Denied && denied.changes == 2);
    EF_CHECK(u.fake.values["anim"] == "0" && b.hags_api.inner.value == std::optional<std::uint32_t>(2));

    // Tekrar Kapali, UAC kabul: birebir geri (HwSchMode=1, DRS mirasa doner), yeniden baslatma bekliyor
    ch.launcher.outcome = ElevationOutcome::Started;
    const auto back = be.restore({});
    EF_CHECK(back.kind == perf::OutcomeKind::Restored && back.pending_reboot);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1) && b.drs_api.global.count(0x1057EB71) == 0);
    EF_CHECK(be.active_changes() == 0);
    EF_CHECK(be.restore({}).kind == perf::OutcomeKind::NothingToDo);
}

EF_TEST(admin_backend_without_channel_keeps_denied_contract) {
    UserSide u;
    perf::JournalBackend be(u.deps(nullptr));
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(mixed_plan(), {}, cancel).kind == perf::OutcomeKind::Denied);
    EF_CHECK(u.fake.values["anim"] == "0");
}

EF_TEST(admin_controller_shows_admin_skipped_and_reboot) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    UserSide u;
    auto deps = u.deps(&ch);
    deps.build_plan = [] { return ef::contracts::Result<perf::Plan>(mixed_plan()); };
    perf::JournalBackend be(deps);
    perf::PerfController c(be, [](std::function<void()> w) { w(); }, [](std::function<void()> f) { f(); });
    c.init();
    ch.launcher.outcome = ElevationOutcome::Denied;
    c.toggle();
    EF_CHECK(c.phase() == perf::Phase::Confirming && c.plan().needs_admin());
    c.answer(true);
    EF_CHECK(c.phase() == perf::Phase::On && c.notice() == perf::Notice::AdminSkipped && !c.reboot_pending());
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == perf::Phase::Off);

    ch.launcher.outcome = ElevationOutcome::Started;
    c.toggle();
    c.answer(true);
    EF_CHECK(c.phase() == perf::Phase::On && c.notice() == perf::Notice::None && c.reboot_pending());
    EF_CHECK(c.active_changes() == 3);
}

EF_TEST(admin_auto_apply_never_prompts_uac) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    UserSide u;
    auto deps = u.deps(&ch);
    deps.build_plan = [] { return ef::contracts::Result<perf::Plan>(mixed_plan()); };
    perf::JournalBackend be(deps);
    perf::PerfController c(be, [](std::function<void()> w) { w(); }, [](std::function<void()> f) { f(); });
    c.init();
    c.auto_apply();  // OPT-06: onayli oyun basladi
    EF_CHECK(c.phase() == perf::Phase::On && c.auto_active());
    EF_CHECK(ch.uac_prompts == 0 && b.hags_api.inner.value == std::optional<std::uint32_t>(1));
    EF_CHECK(u.fake.values["anim"] == "1" && c.active_changes() == 1);
    c.auto_restore();
    EF_CHECK(c.phase() == perf::Phase::Off && u.fake.values["anim"] == "0" && ch.uac_prompts == 0);
}

// ---- M1: broker yazarken coktu (journal yarim kaldi) -> sonraki broker oturumu basinda uzlastirma ----
EF_TEST(admin_broker_crash_mid_write_is_reconciled_at_session_start) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.hags_api.crash_after_write = true;
    bool crashed = false;
    try {
        (void)b.exec->handle(ipc::RequestAction::Apply, hags_op());
    } catch (const ProbeHags::SimulatedCrash&) {
        crashed = true;
    }
    EF_CHECK(crashed);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2));  // deger yazilmisti
    auto l = b.perf_store.scan();
    EF_CHECK(l.transactions.size() == 1 && broker::tx_unreconciled(l.transactions[0].state));
    // Kapali ve kurtarma listelerinde gorunur (eskiden yalniz Active/RecoveryRequired bakiliyordu)
    EF_CHECK(broker::journal_active_ops(l, true).size() == 1 && broker::journal_active_ops(l, false).empty());
    EF_CHECK(broker::journal_unreconciled_ops(l, false).size() == 1);
    // Uzlastirilmamis hedefe yeni yazma reddedilir: "2" ozgun deger sanilip "1" kaybolmaz (iki makine journal'i)
    const int writes = b.hags_api.inner.writes;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Blocked);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Fix, hags_op()).state == broker::StepState::Blocked);
    EF_CHECK(b.hags_api.inner.writes == writes && b.active(b.fixes_store) == 0);
    // Sonraki (yukseltilmis + kilitli) broker oturumu: once uzlastirma, gercek hedef okunarak geri alinir
    const auto rc = b.exec->reconcile_on_start();
    EF_CHECK(rc.ran && rc.perf.restored.size() == 1 && rc.perf.recovery_required.empty() && rc.fixes.restored.empty());
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));  // onceki deger birebir
    EF_CHECK(b.perf_store.scan().transactions[0].state == TxState::Restored);
    EF_CHECK(broker::journal_unreconciled_ops(b.perf_store.scan(), true).empty());
    // Artik yeni uygulama dogru anlik goruntuyle (1) baslar ve birebir geri alinir
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Applied);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, hags_op()).state == broker::StepState::Restored);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));
}

EF_TEST(admin_broker_crash_unverifiable_rollback_stays_recovery_required) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.hags_api.crash_after_write = true;
    try {
        (void)b.exec->handle(ipc::RequestAction::Fix, hags_op());
    } catch (const ProbeHags::SimulatedCrash&) {
    }
    b.hags_api.inner.fail_write = true;  // geri yazma basarisiz: basari uydurulmaz
    const auto rc = b.exec->reconcile_on_start();
    EF_CHECK(rc.ran && rc.fixes.recovery_required.size() == 1 && rc.fixes.restored.empty());
    EF_CHECK(b.fixes_store.scan().transactions[0].state == TxState::RecoveryRequired);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Recover, hags_op()).state == broker::StepState::RecoveryRequired);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Blocked);
    b.hags_api.inner.fail_write = false;  // sonraki oturum yeniden dener
    const auto rc2 = b.exec->reconcile_on_start();
    EF_CHECK(rc2.fixes.restored.size() == 1 && b.hags_api.inner.value == std::optional<std::uint32_t>(1));
    const auto rec = b.exec->handle(ipc::RequestAction::Recover, hags_op());
    EF_CHECK(rec.state == broker::StepState::Restored && rec.pending_reboot);
}

EF_TEST(admin_reconcile_requires_elevation_and_lock) {
    Broker unelevated(false, true);
    EF_CHECK(!unelevated.exec->reconcile_on_start().ran);
    Broker unlocked(true, false);
    EF_CHECK(!unlocked.exec->reconcile_on_start().ran);
}

EF_TEST(admin_backend_restore_reconciles_interrupted_machine_record) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.hags_api.crash_after_write = true;
    try {
        (void)b.exec->handle(ipc::RequestAction::Apply, hags_op());
    } catch (const ProbeHags::SimulatedCrash&) {
    }
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    EF_CHECK(be.recovery_needed());      // Performans "Kurtarma gerekli" (sessiz degil)
    EF_CHECK(be.active_changes() == 1);  // Kapali bunu geri alinacaklar arasinda sayar
    const auto out = be.restore({});
    EF_CHECK(out.kind == perf::OutcomeKind::Restored && ch.uac_prompts == 1);
    EF_CHECK(ch.last_reconcile.ran && ch.last_reconcile.perf.restored.size() == 1);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1) && !be.recovery_needed());
}

EF_TEST(admin_startup_recover_offer_uses_one_uac_and_reports) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.hags_api.crash_after_write = true;
    try {
        (void)b.exec->handle(ipc::RequestAction::Fix, hags_op());
    } catch (const ProbeHags::SimulatedCrash&) {
    }
    MockChannel ch(b);
    const auto ops = ch.unreconciled_ops();
    EF_CHECK(ops.size() == 1 && ops[0].target == "hags:hwschmode");
    ch.launcher.outcome = ElevationOutcome::Denied;  // Evet dedi ama UAC reddedildi: hicbir sey yazilmaz
    const auto denied = ch.run(ipc::RequestAction::Recover, ops);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Recover, denied) == "recovery.machine_denied");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2) && ch.unreconciled_ops().size() == 1);
    ch.launcher.outcome = ElevationOutcome::Started;
    const auto ok = ch.run(ipc::RequestAction::Recover, ops);
    EF_CHECK(ch.launcher.calls == 2 && ok.steps.size() == 1 && ok.steps[0].state == broker::StepState::Restored);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Recover, ok) == "recovery.machine_done");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1) && ch.unreconciled_ops().empty());
}

// ---- Urun eksigi: Oyun Hazirligi "Geri al" (HAGS, broker UndoFix) ----
EF_TEST(admin_fix_undo_restores_exact_previous_value) {
    for (const bool existed : {true, false}) {
        Broker b;
        if (existed) b.hags_api.inner.value = 1;
        MockChannel ch(b);
        const auto f = ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
        EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, f) == "chk.notice.fix_reboot");
        EF_CHECK(ch.fix_active_ops().size() == 1 && ch.active_ops().empty());  // Performans kapsami degil
        ch.launcher.outcome = ElevationOutcome::Denied;  // UAC reddi: duzeltme etkin kalir
        const auto d = ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
        EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, d) == "chk.notice.undo_denied");
        EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2) && ch.fix_active_ops().size() == 1);
        ch.launcher.outcome = ElevationOutcome::Started;
        const auto un = ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
        EF_CHECK(un.steps.size() == 1 && un.steps[0].state == broker::StepState::Restored && un.steps[0].pending_reboot);
        EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, un) == "chk.notice.undo_reboot");
        // Kapali = onceki deger (fabrika sifirlamasi degil): vardiysa 1, yoksa deger silinir
        if (existed) EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));
        else EF_CHECK(!b.hags_api.inner.value.has_value() && b.hags_api.inner.removes == 1);
        EF_CHECK(ch.fix_active_ops().empty());
        const auto again = ch.run(ipc::RequestAction::UndoFix, {perf::hags_on_operation()});
        EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, again) == "chk.notice.nothing");
    }
}

EF_TEST(admin_fix_undo_does_not_overwrite_external_change) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    (void)ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    b.hags_api.inner.value = 0;  // kullanici/baska arac degistirdi
    const auto un = ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
    EF_CHECK(un.steps[0].state == broker::StepState::RecoveryRequired);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, un) == "chk.notice.undo_failed");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(0));  // ezilmedi
}

EF_TEST(admin_fix_result_notices_are_never_silent) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    ch.launcher.outcome = ElevationOutcome::Denied;
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()})) ==
              "chk.notice.fix_denied");
    ch.launcher.outcome = ElevationOutcome::Failed;
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()})) ==
              "chk.notice.admin_start_failed");
    EF_CHECK(b.hags_api.inner.writes == 0);
    // Istek gitti, yanit gelmedi: "degismedi" denmez, bilinmiyor
    ch.launcher.outcome = ElevationOutcome::Started;
    ch.drop_replies = true;
    const auto lost = ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    EF_CHECK(lost.steps.size() == 1 && lost.steps[0].unknown && lost.steps[0].state == broker::StepState::Failed);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, lost) == "chk.notice.unknown");
    // Hedefte zaten etkin duzeltme: broker reddeder, yazma yok; genel "uygulanamadi" degil ozel bildirim
    ch.drop_replies = false;
    (void)ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    const int w = b.hags_api.inner.writes;
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()})) ==
              "chk.notice.fix_blocked");
    EF_CHECK(b.hags_api.inner.writes == w);
}

EF_TEST(admin_ipc_new_actions_roundtrip) {
    std::vector<ipc::RequestAction> seen;
    ipc::ServerSession s("0123456789abcdef0123456789abcdef", kUiPid, [&](const std::string&, ipc::RequestAction a, const SettingOperation&) {
        seen.push_back(a);
        return std::string("{}");
    });
    EF_CHECK(s.on_frame(ipc::make_hello("0123456789abcdef0123456789abcdef", kUiPid, 1), kUiPid).type == ipc::MsgType::HelloAck);
    EF_CHECK(s.on_frame(ipc::make_request("a", hags_op(), 2, ipc::RequestAction::UndoFix), kUiPid).type == ipc::MsgType::Response);
    EF_CHECK(s.on_frame(ipc::make_request("b", hags_op(), 3, ipc::RequestAction::Recover), kUiPid).type == ipc::MsgType::Response);
    EF_CHECK(seen.size() == 2 && seen[0] == ipc::RequestAction::UndoFix && seen[1] == ipc::RequestAction::Recover);
}

// ---- L2 / L3 ----
EF_TEST(admin_backend_user_cancel_is_not_admin_skipped) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    // Kullanici adimi tamamlandiktan sonra (son ilerleme) iptal: yonetici grubuna gecilmez
    const auto out = be.apply(
        mixed_plan(),
        [&](const rec::Progress& p) {
            if (p.total && p.done == p.total) cancel = true;
        },
        cancel);
    EF_CHECK(out.kind == perf::OutcomeKind::Active && out.changes == 1);
    EF_CHECK(!out.admin_skipped);  // "yonetici izni verilmedi" bildirimi gosterilmez
    EF_CHECK(ch.uac_prompts == 0 && b.hags_api.inner.value == std::optional<std::uint32_t>(1));
}

EF_TEST(admin_backend_keeps_admin_result_when_user_restore_fails) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.drs_api.defaults[0x1057EB71] = 5;
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    EF_CHECK(be.apply(mixed_plan(), {}, cancel).kind == perf::OutcomeKind::Active);
    u.fake.fail_restore_target = "anim";
    const auto out = be.restore({});
    EF_CHECK(out.kind == perf::OutcomeKind::RecoveryRequired);  // dogrulanamayan kullanici adimi oncelikli
    EF_CHECK(out.pending_reboot);                                // UAC altindaki HAGS geri alma sonucu atilmadi
    EF_CHECK(out.detail.find("admin restored") != std::string::npos);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1) && b.drs_api.global.count(0x1057EB71) == 0);
}

// ---- v3.5 round 3: RecoveryRequired duzeltme kaydi takili kalmaz ----
EF_TEST(admin_fix_recovery_required_shows_retry_and_is_retryable) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    (void)ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    b.hags_api.inner.value = 0;  // baska arac degistirdi: geri alma dogrulanamaz (ezilmez)
    const auto un = ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
    EF_CHECK(un.steps.size() == 1 && un.steps[0].state == broker::StepState::RecoveryRequired);
    // Etkin degil ama takili da degil: "Geri al / Yeniden dene" satiri (Retry) ve kurtarma listesi
    EF_CHECK(ch.fix_active_ops().empty() && ch.fix_recovery_ops().size() == 1);
    std::vector<std::string> active, recovery;
    broker::journal_fix_targets(b.fixes_store.scan(), active, recovery);
    EF_CHECK(active.empty() && recovery.size() == 1 && recovery[0] == "hags:hwschmode");
    std::vector<readiness::Check> checks(2);
    checks[0].id = "windows.hags";
    checks[1].id = "windows.game_mode";
    const auto st = readiness::undo_states(checks, readiness::FixTargets{active, recovery});
    EF_CHECK(st[0] == readiness::UndoState::Retry && st[1] == readiness::UndoState::None);
    // Acilis sorusu RecoveryRequired kaydi da listeler (yalniz yarim kalmis degil)
    EF_CHECK(ch.unreconciled_ops().size() == 1);
    // Yeni Duzelt: broker reddeder, ozel bildirim (genel fix_failed degil), yazma yok
    const int w = b.hags_api.inner.writes;
    const auto f = ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    EF_CHECK(f.steps.size() == 1 && f.steps[0].state == broker::StepState::Blocked);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, f) == "chk.notice.fix_blocked");
    EF_CHECK(b.hags_api.inner.writes == w && b.hags_api.inner.value == std::optional<std::uint32_t>(0));
    // Hala dogrulanamiyor: yeniden dene -> yine RecoveryRequired (basari uydurulmaz)
    const auto again = ch.run(ipc::RequestAction::UndoFix, ch.fix_recovery_ops());
    EF_CHECK(again.steps[0].state == broker::StepState::RecoveryRequired);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, again) == "chk.notice.undo_failed");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(0));
    // Deger duzeltmenin yazdigi degere donerse: oturum basi uzlastirma geri alir; UndoFix "bulunamadi" demez
    b.hags_api.inner.value = 2;
    const auto ok = ch.run(ipc::RequestAction::UndoFix, ch.fix_recovery_ops());
    EF_CHECK(ch.last_reconcile.fixes.restored.size() == 1);
    EF_CHECK(ok.steps.size() == 1 && ok.steps[0].state == broker::StepState::Restored && ok.steps[0].pending_reboot);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, ok) == "chk.notice.undo_reboot");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));  // kaydedilen onceki deger birebir
    EF_CHECK(ch.fix_recovery_ops().empty() && ch.unreconciled_ops().empty());
    // Artik yeni Duzelt kabul edilir
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()})) ==
              "chk.notice.fix_reboot");
}

EF_TEST(admin_startup_recover_retries_recovery_required_record) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    (void)ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    b.hags_api.inner.value = 0;
    (void)ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
    EF_CHECK(b.fixes_store.scan().transactions[0].state == TxState::RecoveryRequired);
    const auto ops = ch.unreconciled_ops();
    EF_CHECK(ops.size() == 1 && ops[0].target == "hags:hwschmode");
    const auto still = ch.run(ipc::RequestAction::Recover, ops);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Recover, still) == "recovery.machine_failed");
    b.hags_api.inner.value = 2;
    const auto ok = ch.run(ipc::RequestAction::Recover, ch.unreconciled_ops());
    EF_CHECK(ok.steps.size() == 1 && ok.steps[0].state == broker::StepState::Restored);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Recover, ok) == "recovery.machine_done");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1) && ch.unreconciled_ops().empty());
}

EF_TEST(admin_recover_not_restored_while_other_journal_active) {
    Broker b;
    b.hags_api.inner.value = 1;
    // Eski surumden kalma durum: fixes journal'inda kapanmis (Restored) kayit + Performans journal'inda ETKIN kayit
    rec::JournalTx old;
    old.id = tx_id(b.ids);
    old.target_sid = kUserSid;
    rec::JournalStep st;
    st.op = hags_op();
    old.steps.push_back(st);
    EF_CHECK(b.fixes_store.begin(old).ok());
    old.state = TxState::Restored;
    EF_CHECK(b.fixes_store.persist(old).ok());
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Applied);
    const auto r = b.exec->handle(ipc::RequestAction::Recover, hags_op());
    EF_CHECK(r.state != broker::StepState::Restored && r.state == broker::StepState::Applied && !r.pending_reboot);
    broker::BatchResult br;
    br.elevation = ElevationOutcome::Started;
    br.steps.push_back(r);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Recover, br) == "recovery.machine_active");
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2));  // deger hala uygulanmis (dogru bildirim)
    // Performans Kapali sonrasi gercekten geri yuklendi
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, hags_op()).state == broker::StepState::Restored);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Recover, hags_op()).state == broker::StepState::Restored);
}

EF_TEST(admin_step_state_blocked_roundtrips) {
    broker::StepResult r{"hags:hwschmode", broker::StepState::Blocked, false, "target already has an active change"};
    const auto d = broker::decode_result(broker::encode_result("r1", r));
    EF_CHECK(d.ok() && d.value().state == broker::StepState::Blocked && broker::to_string(broker::StepState::Blocked) == "blocked");
}

// Kullanici kapsami "Geri al / Yeniden dene": ayni kural (restore_matching), RecoveryRequired kayit takili kalmaz.
EF_TEST(user_fix_restore_matching_retries_recovery_required) {
    UserSide u;
    u.fake.values["anim"] = "0";
    rec::JournalTx tx;
    tx.id = tx_id(u.ids);
    tx.target_sid = kUserSid;
    tx.created_unix_ms = 1;
    rec::JournalStep st;
    st.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, "anim", "1", false, false};
    tx.steps.push_back(st);
    auto applied = u.eng.apply(std::move(tx));
    EF_CHECK(applied.ok() && applied.value().state == TxState::Active && u.fake.values["anim"] == "1");
    u.fake.values["anim"] = "7";  // dis degisiklik: geri alma dogrulanamaz, ezilmez
    auto match = [](const SettingOperation& o) { return o.target == "anim"; };
    const auto r1 = broker::restore_matching(&u.store, &u.eng, match, "anim");
    EF_CHECK(r1.state == broker::StepState::RecoveryRequired && u.fake.values["anim"] == "7");
    std::vector<std::string> active, recovery;
    broker::journal_fix_targets(u.store.scan(), active, recovery);
    EF_CHECK(active.empty() && recovery.size() == 1 && recovery[0] == "anim");
    broker::BatchResult b1;
    b1.elevation = ElevationOutcome::Started;
    b1.steps.push_back(r1);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, b1) == "chk.notice.undo_failed");
    u.fake.values["anim"] = "1";  // deger duzeltmenin yazdigi degere dondu
    const auto r2 = broker::restore_matching(&u.store, &u.eng, match, "anim");
    EF_CHECK(r2.state == broker::StepState::Restored && !r2.pending_reboot && u.fake.values["anim"] == "0");
    broker::BatchResult b2;
    b2.elevation = ElevationOutcome::Started;
    b2.steps.push_back(r2);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, b2) == "chk.notice.undo_done");
    EF_CHECK(broker::restore_matching(&u.store, &u.eng, match, "anim").state == broker::StepState::NothingToDo);
    EF_CHECK(broker::restore_matching(nullptr, nullptr, match, "anim").state == broker::StepState::RecoveryRequired);
}

// ---- v3.5 round 3 (2): dis degisiklik bekleyen kayit "Mevcut degeri koru" ile ayar yazmadan kapatilir ----
namespace {
// Makine HAGS duzeltmesi -> dis degisiklik -> geri alma dogrulanamadi (RecoveryRequired + kExternalChange).
void make_machine_conflict(Broker& b, MockChannel& ch) {
    b.hags_api.inner.value = 1;
    (void)ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()});
    b.hags_api.inner.value = 0;  // baska arac degistirdi
    (void)ch.run(ipc::RequestAction::UndoFix, ch.fix_active_ops());
}
}  // namespace

EF_TEST(admin_keep_current_closes_external_change_without_write) {
    Broker b;
    MockChannel ch(b);
    make_machine_conflict(b, ch);
    EF_CHECK(b.fixes_store.scan().transactions.size() == 1 && rec::has_external_conflict(b.fixes_store.scan().transactions[0]));
    // Satir durumu: catisma (Conflict) -> "Geri al / Yeniden dene" + "Mevcut degeri koru"
    std::vector<std::string> a, f, i, cf;
    ch.fix_target_lists(a, f, i, cf);
    EF_CHECK(a.empty() && f.empty() && i.empty() && cf.size() == 1 && cf[0] == "hags:hwschmode");
    std::vector<readiness::Check> checks(1);
    checks[0].id = "windows.hags";
    EF_CHECK(readiness::undo_states(checks, readiness::FixTargets{a, f, i, cf})[0] == readiness::UndoState::Conflict);
    const auto ops = ch.fix_conflict_ops();
    EF_CHECK(ops.size() == 1 && ops[0].target == "hags:hwschmode");
    EF_CHECK(ch.unreconciled_ops().size() == 1);  // once acilista soruluyordu

    // UAC reddi: hicbir sey yazilmaz, kayit oldugu gibi
    ch.launcher.outcome = ElevationOutcome::Denied;
    const auto den = ch.run(ipc::RequestAction::KeepCurrent, ops);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, den) == "chk.notice.keep_denied");
    EF_CHECK(b.fixes_store.scan().transactions[0].state == TxState::RecoveryRequired);
    ch.launcher.outcome = ElevationOutcome::Started;

    const int writes = b.hags_api.inner.writes;
    const auto k = ch.run(ipc::RequestAction::KeepCurrent, ops);
    EF_CHECK(k.steps.size() == 1 && k.steps[0].state == broker::StepState::Kept);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, k) == "chk.notice.keep_done");
    // Ayar YAZILMADI: kullanicinin degeri oldugu gibi
    EF_CHECK(b.hags_api.inner.writes == writes && b.hags_api.inner.value == std::optional<std::uint32_t>(0));
    // Kayit mevcut yasam dongusuyle kapandi: Restored + KeptExternal; gecmis "kullanici mevcut degeri korudu"
    const auto tx = b.fixes_store.scan().transactions[0];
    EF_CHECK(tx.state == TxState::Restored && tx.steps[0].state == rec::StepState::KeptExternal);
    EF_CHECK(rec::status_of(tx) == rec::HistoryStatus::KeptCurrent);
    // Acilis sorusu artik sormaz; satir temiz; Duzelt engellenmez
    EF_CHECK(ch.unreconciled_ops().empty() && ch.fix_recovery_ops().empty() && ch.fix_conflict_ops().empty());
    a.clear(), f.clear(), i.clear(), cf.clear();
    ch.fix_target_lists(a, f, i, cf);
    EF_CHECK(a.empty() && f.empty() && i.empty() && cf.empty());
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::Fix, ch.run(ipc::RequestAction::Fix, {perf::hags_on_operation()})) ==
              "chk.notice.fix_reboot");
    // Ikinci kez: catisma yok -> NothingToDo
    const auto again = b.exec->handle(ipc::RequestAction::KeepCurrent, hags_op());
    EF_CHECK(again.state == broker::StepState::NothingToDo);
}

EF_TEST(admin_keep_current_is_allowlisted_and_never_writes) {
    Broker b;
    // Izin listesi disi hedef: reddedilir
    SettingOperation bad{OperationId::VisualEffectsSet, OperationScope::Machine, "anim", "1", false, false};
    EF_CHECK(b.exec->handle(ipc::RequestAction::KeepCurrent, bad).state == broker::StepState::Rejected);
    // Yukseltilmemis broker: SEC-03 reddeder
    Broker nb(false, true);
    EF_CHECK(nb.exec->handle(ipc::RequestAction::KeepCurrent, hags_op()).state == broker::StepState::Rejected);
    // Etkin (catismasiz) duzeltme KeepCurrent ile kapanmaz
    b.hags_api.inner.value = 1;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Fix, hags_op()).state == broker::StepState::Applied);
    EF_CHECK(b.exec->handle(ipc::RequestAction::KeepCurrent, hags_op()).state == broker::StepState::NothingToDo);
    EF_CHECK(b.fixes_store.scan().transactions[0].state == TxState::Active);
}

EF_TEST(admin_keep_current_reports_restore_when_value_came_back) {
    Broker b;
    MockChannel ch(b);
    make_machine_conflict(b, ch);
    b.hags_api.inner.value = 2;  // deger duzeltmenin yazdigina dondu: oturum basi uzlastirma geri alir
    const auto k = ch.run(ipc::RequestAction::KeepCurrent, ch.fix_conflict_ops());
    EF_CHECK(k.steps.size() == 1 && k.steps[0].state == broker::StepState::Restored);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, k) == "chk.notice.undo_reboot");  // "korundu" denmez; HAGS geri yuklendi -> yeniden baslatma
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));
}

EF_TEST(keep_current_protocol_and_result_roundtrip) {
    const auto f = ipc::make_request("r1", hags_op(), 2, ipc::RequestAction::KeepCurrent);
    EF_CHECK(f.payload.find("\"keep_current\"") != std::string::npos);
    ipc::RequestAction seen = ipc::RequestAction::Apply;
    ipc::ServerSession s("0123456789abcdef0123456789abcdef", kUiPid, [&](const std::string&, ipc::RequestAction a, const SettingOperation&) {
        seen = a;
        return std::string("{}");
    });
    (void)s.on_frame(ipc::make_hello("0123456789abcdef0123456789abcdef", kUiPid, 1), kUiPid);
    const auto r = s.on_frame(f, kUiPid);
    EF_CHECK(r.type == ipc::MsgType::Response && seen == ipc::RequestAction::KeepCurrent);
    EF_CHECK(ipc::to_string(ipc::RequestAction::KeepCurrent) == "keep_current");
    broker::StepResult kr{"hags:hwschmode", broker::StepState::Kept, false, ""};
    const auto d = broker::decode_result(broker::encode_result("r1", kr));
    EF_CHECK(d.ok() && d.value().state == broker::StepState::Kept && broker::to_string(broker::StepState::Kept) == "kept");
    // Bildirimler
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Started;
    b.steps.push_back({"x", broker::StepState::NothingToDo, false, ""});
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, b) == "chk.notice.keep_nothing");  // RDY-06
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::UndoFix, b) == "chk.notice.nothing");
    b.steps[0].state = broker::StepState::RecoveryRequired;
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, b) == "chk.notice.keep_failed");
    b.steps[0].unknown = true;
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, b) == "chk.notice.unknown");
}

// Kullanici kapsami: ayni kural (keep_matching), yerel journal; ayar okunmaz/yazilmaz.
EF_TEST(user_fix_keep_matching_closes_conflict_without_write) {
    UserSide u;
    rec::JournalTx tx;
    tx.id = tx_id(u.ids);
    tx.target_sid = kUserSid;
    tx.created_unix_ms = 1;
    rec::JournalStep st;
    st.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, "anim", "1", false, false};
    tx.steps.push_back(st);
    EF_CHECK(u.eng.apply(std::move(tx)).ok());
    auto match = [](const SettingOperation& o) { return o.target == "anim"; };
    EF_CHECK(broker::keep_matching(&u.store, match, "anim").state == broker::StepState::NothingToDo);  // etkin: catisma yok
    u.fake.values["anim"] = "7";
    EF_CHECK(broker::restore_matching(&u.store, &u.eng, match, "anim").state == broker::StepState::RecoveryRequired);
    std::vector<std::string> a, f, i, cf;
    broker::journal_fix_targets(u.store.scan(), a, f, i, cf);
    EF_CHECK(cf.size() == 1 && f.empty() && i.empty() && a.empty());
    const int restores = u.fake.restores, applies = u.fake.applies;
    const auto k = broker::keep_matching(&u.store, match, "anim");
    EF_CHECK(k.state == broker::StepState::Kept);
    EF_CHECK(u.fake.values["anim"] == "7" && u.fake.restores == restores && u.fake.applies == applies);
    EF_CHECK(rec::status_of(u.store.scan().transactions[0]) == rec::HistoryStatus::KeptCurrent);
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Started;
    b.steps.push_back(k);
    EF_CHECK(broker::admin_notice_key(ipc::RequestAction::KeepCurrent, b) == "chk.notice.keep_done");
    EF_CHECK(broker::restore_matching(&u.store, &u.eng, match, "anim").state == broker::StepState::NothingToDo);
    EF_CHECK(broker::keep_matching(nullptr, match, "anim").state == broker::StepState::Failed);
}

// Durum metni durust: yarim kalmis (geri alma denenmedi) ile dogrulanamamis geri alma ayri.
EF_TEST(fix_targets_separate_interrupted_failed_and_conflict) {
    UserSide u;
    u.fake.values["a"] = "0";
    u.fake.values["b"] = "0";
    u.reg.add(OperationId::GamingToggleSet, &u.fake);
    auto mk = [&](const char* target, TxState state, rec::StepState ss, const std::string& err) {
        rec::JournalTx tx;
        tx.id = tx_id(u.ids);
        tx.target_sid = kUserSid;
        rec::JournalStep st;
        st.op = SettingOperation{OperationId::GamingToggleSet, OperationScope::CurrentUser, target, "1", false, false};
        st.state = ss;
        st.error = err;
        tx.steps.push_back(st);
        EF_CHECK(u.store.begin(tx).ok());
        tx.state = state;
        EF_CHECK(u.store.persist(tx).ok());
    };
    mk("a", TxState::Applying, rec::StepState::Intent, "");                               // yarim kaldi
    mk("b", TxState::RecoveryRequired, rec::StepState::Failed, "restore verification failed");  // geri alma denendi
    std::vector<std::string> a, f, i, cf;
    broker::journal_fix_targets(u.store.scan(), a, f, i, cf);
    EF_CHECK(i.size() == 1 && i[0] == "a" && f.size() == 1 && f[0] == "b" && cf.empty());
    std::vector<std::string> a2, r2;  // eski iki listeli bicim: hepsi "recovery"
    broker::journal_fix_targets(u.store.scan(), a2, r2);
    EF_CHECK(r2.size() == 2);
    EF_CHECK(broker::journal_conflict_ops(u.store.scan()).empty());
}

// Ertelenen islerin sirasi (App glue icin saf karar).
EF_TEST(deferred_work_respects_game_admin_and_perf) {
    using perf::DeferredRun;
    EF_CHECK(perf::auto_restore_when(false, false) == DeferredRun::Now);
    EF_CHECK(perf::auto_restore_when(false, true) == DeferredRun::AfterAdminJob);
    EF_CHECK(perf::auto_restore_when(true, false) == DeferredRun::AfterGameExit);  // oyun calisirken asla
    EF_CHECK(perf::auto_restore_when(true, true) == DeferredRun::AfterGameExit);   // yonetici bitse de oyun beklenir
    EF_CHECK(perf::machine_recovery_when(false, false, false) == DeferredRun::Now);
    EF_CHECK(perf::machine_recovery_when(false, false, true) == DeferredRun::AfterPerf);  // Evet perf_busy ile reddedilmez
    EF_CHECK(perf::machine_recovery_when(false, true, true) == DeferredRun::AfterAdminJob);
    EF_CHECK(perf::machine_recovery_when(true, false, false) == DeferredRun::AfterGameExit);
}

// RDY-06: "yapilacak bir sey bulunamadi" bildirimleri uyari; yalniz dogrulanmis basari normal renkte.
EF_TEST(readiness_notice_warning_classification) {
    for (const char* ok : {"", "chk.notice.fix_reboot", "chk.notice.undo_done", "chk.notice.undo_reboot", "chk.notice.keep_done",
                           "recovery.machine_done"})
        EF_CHECK(!broker::admin_notice_warning(ok));
    for (const char* warn : {"chk.notice.nothing", "chk.notice.keep_nothing", "chk.notice.keep_failed", "chk.notice.keep_denied",
                             "chk.notice.undo_failed", "chk.notice.fix_denied", "chk.notice.unknown", "chk.notice.perf_busy",
                             "chk.notice.admin_start_failed", "recovery.machine_failed"})
        EF_CHECK(broker::admin_notice_warning(warn));
}

// A1.1: oyun profili izin listesi dar: yalniz guc modu/hazirlanan kare ve yalniz hedef deger.
EF_TEST(admin_game_profile_allowlist_is_narrow) {
    const auto ops = perf::nvidia_game_profile_ops("cs2.exe");
    EF_CHECK(ops.size() == 2);
    for (const auto& op : ops) EF_CHECK(broker::admin_operation_allowed(op) && broker::admin_apply_allowed(op));
    auto bad_value = ops[0];
    bad_value.new_value = "00000002";
    EF_CHECK(!broker::admin_operation_allowed(bad_value));
    auto shader = ops[0];
    shader.target = "drs:app:cs2.exe:00AC8497";
    shader.new_value = "00002800";
    EF_CHECK(!broker::admin_operation_allowed(shader));
    auto traversal = ops[0];
    traversal.target = "drs:app:a/evil.exe:1057EB71";
    EF_CHECK(!broker::admin_operation_allowed(traversal));
}

EF_TEST(game_profile_notice_keys) {
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Denied;
    EF_CHECK(broker::game_profile_notice_key(false, b) == "games.nv.notice_denied");
    b.elevation = ElevationOutcome::Started;
    b.steps = {{"a", broker::StepState::Applied, false, ""}, {"b", broker::StepState::Applied, false, ""}};
    EF_CHECK(broker::game_profile_notice_key(false, b) == "games.nv.notice_applied");
    b.steps[1].state = broker::StepState::Failed;
    EF_CHECK(broker::game_profile_notice_key(false, b) == "games.nv.notice_partial");  // kismi sonuc basari denmez
    b.steps[1].state = broker::StepState::GateBlocked;
    EF_CHECK(broker::game_profile_notice_key(false, b) == "games.nv.notice_partial");  // ilki yazildi: "yazilmadi" denmez
    b.steps[0].state = broker::StepState::GateBlocked;
    EF_CHECK(broker::game_profile_notice_key(false, b) == "chk.notice.gate_blocked");
    b.steps[0].state = broker::StepState::Failed;
    b.steps[1].state = broker::StepState::Failed;
    EF_CHECK(broker::game_profile_notice_key(false, b) == "games.nv.notice_failed");
    b.elevation = ElevationOutcome::Failed;
    EF_CHECK(broker::game_profile_notice_key(false, b) == "chk.notice.admin_start_failed");  // baslatma hatasi UAC reddi degil
    b.elevation = ElevationOutcome::Started;
    b.steps = {{"a", broker::StepState::Restored, false, ""}, {"b", broker::StepState::RecoveryRequired, false, ""}};
    EF_CHECK(broker::game_profile_notice_key(true, b) == "games.nv.notice_review");  // dis degisiklik: "Geri al ile tamamla" denmez
    b.steps = {{"a", broker::StepState::Applied, false, ""}, {"b", broker::StepState::Failed, false, ""}};
    b.steps[1].unknown = true;  // yanit kayboldu: "yalniz bir kismi degisti" denemez
    EF_CHECK(broker::game_profile_notice_key(false, b) == "chk.notice.unknown");
    b.steps = {{"a", broker::StepState::Restored, false, ""}, {"b", broker::StepState::NothingToDo, false, ""}};
    EF_CHECK(broker::game_profile_notice_key(true, b) == "games.nv.notice_restored");
}

// ---- T08: otomatik geri yukleme noktasi, Performans plani ile ayni UAC/broker oturumunda ----
namespace {
perf::Plan plan_with_restore_point() {
    perf::Plan p = mixed_plan();
    p.items.push_back(*perf::restore_point_plan_item(true, std::nullopt, std::nullopt));  // sirayi backend duzeltir
    return p;
}
}  // namespace

EF_TEST(t08_restore_point_runs_first_in_same_uac_session) {
    Broker b;
    b.hags_api.inner.value = 1;
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    const auto out = be.apply(plan_with_restore_point(), {}, cancel);
    EF_CHECK(out.kind == perf::OutcomeKind::Active && out.changes == 3);  // nokta ayar sayisina girmez
    EF_CHECK(out.restore_point == perf::Outcome::RestorePoint::Created);
    EF_CHECK(ch.uac_prompts == 1 && ch.launcher.calls == 1 && b.rp_calls == 1);  // tek UAC, tek oturum
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2));
    EF_CHECK(be.active_changes() == 3);  // nokta journal adimi degil
    // Kapali: nokta silinmez/geri alinmaz; ek geri yukleme noktasi istenmez
    EF_CHECK(be.restore({}).kind == perf::OutcomeKind::Restored && b.rp_calls == 1);
}

EF_TEST(t08_restore_point_batch_order_and_action_mapping) {
    EF_CHECK(broker::request_action_for(ipc::RequestAction::Apply, perf::restore_point_operation()) == ipc::RequestAction::RestorePoint);
    EF_CHECK(broker::request_action_for(ipc::RequestAction::Apply, hags_op()) == ipc::RequestAction::Apply);
    EF_CHECK(broker::request_action_for(ipc::RequestAction::Restore, perf::restore_point_operation()) == ipc::RequestAction::Restore);
    Broker b;
    FakeLauncher l;
    broker::Connector c = [&]() -> ef::contracts::Result<std::unique_ptr<broker::FrameTransport>> {
        return std::unique_ptr<broker::FrameTransport>(new LoopbackTransport(l.args.nonce, kUiPid, b.exec->handler()));
    };
    const auto r = broker::run_admin_batch(l, kUiPid, "0123456789abcdef0123456789abcdef", c, ipc::RequestAction::Apply,
                                           {perf::restore_point_operation(), hags_op()});
    EF_CHECK(l.calls == 1 && r.steps.size() == 2);
    EF_CHECK(r.steps[0].target == rec::kRestorePointTarget && r.steps[0].state == broker::StepState::Applied);
    EF_CHECK(r.steps[1].state == broker::StepState::Applied);
    // Ayar eylemiyle (Fix) gonderilen nokta brokerda hala reddedilir.
    EF_CHECK(b.exec->handle(ipc::RequestAction::Fix, perf::restore_point_operation()).state == broker::StepState::Rejected);
}

EF_TEST(t08_failed_restore_point_does_not_abort_plan_and_sets_note) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.rp_outcome = rec::RestorePointOutcome::ApiFailed;
    MockChannel ch(b);
    UserSide u;
    auto deps = u.deps(&ch);
    deps.build_plan = [] { return ef::contracts::Result<perf::Plan>(plan_with_restore_point()); };
    perf::JournalBackend be(deps);
    perf::PerfController c(be, [](std::function<void()> w) { w(); }, [](std::function<void()> f) { f(); });
    c.init();
    c.toggle();
    EF_CHECK(c.phase() == perf::Phase::Confirming);
    c.answer(true);
    EF_CHECK(c.phase() == perf::Phase::On && c.notice() == perf::Notice::RestorePointFailed);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2) && be.active_changes() == 3);  // plan uygulandi
    EF_CHECK(b.rp_calls == 1);
}

EF_TEST(t08_throttled_restore_point_is_skipped_not_failed) {
    Broker b;
    b.hags_api.inner.value = 1;
    b.rp_outcome = rec::RestorePointOutcome::SkippedByFrequency;
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    const auto out = be.apply(plan_with_restore_point(), {}, cancel);
    EF_CHECK(out.kind == perf::OutcomeKind::Active && out.restore_point == perf::Outcome::RestorePoint::Skipped && out.changes == 3);
}

EF_TEST(t08_lone_restore_point_never_triggers_uac) {
    Broker b;
    MockChannel ch(b);
    UserSide u;
    perf::JournalBackend be(u.deps(&ch));
    std::atomic<bool> cancel{false};
    perf::Plan p;
    p.items.push_back(*perf::restore_point_plan_item(true, std::nullopt, std::nullopt));
    perf::PlanItem v;  // yalniz kullanici ogesi + nokta: yonetici ogesi yok -> nokta dusurulur, UAC yok
    v.op = SettingOperation{OperationId::VisualEffectsSet, OperationScope::CurrentUser, "anim", "1", false, false};
    v.title_key = v.group_key = "plan.vfx";
    p.items.push_back(v);
    const auto out = be.apply(p, {}, cancel);
    EF_CHECK(out.kind == perf::OutcomeKind::Active && ch.uac_prompts == 0 && b.rp_calls == 0);
}

// T09: oyun onceligi (IFEO PerfOptions): broker izni yalniz izin listesindeki exe + "above_normal"; baska deger/anahtar/exe reddedilir.
EF_TEST(admin_game_priority_allowlist_is_compiled_and_narrow) {
    const auto op = [](const char* target, const char* v = "above_normal") {
        return SettingOperation{OperationId::GamePriorityHint, OperationScope::Machine, target, v, false, false};
    };
    for (auto t : {"ifeoprio:cs2.exe", "ifeoprio:dota2.exe", "ifeoprio:minecraft.windows.exe"})
        EF_CHECK(broker::admin_operation_allowed(op(t)));
    for (auto t : {"ifeoprio:valorant.exe", "ifeoprio:notepad.exe", "ifeoprio:r5apex.exe", "ifeoprio:CS2.exe", "ifeoprio:cs2.exe\\perfoptions",
                   "ifeoprio:..\\cs2.exe", "ifeoprio:", "ifeoprio:cs2", "tweak:cs2.exe", "cs2.exe"})
        EF_CHECK(!broker::admin_operation_allowed(op(t)));
    EF_CHECK(!broker::admin_operation_allowed(op("ifeoprio:cs2.exe", "high")));
    EF_CHECK(!broker::admin_operation_allowed(op("ifeoprio:cs2.exe", "6")));
    EF_CHECK(!broker::admin_operation_allowed(op("ifeoprio:cs2.exe", "")));
    auto user_scope = op("ifeoprio:cs2.exe");
    user_scope.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(user_scope));
}

EF_TEST(admin_game_priority_applies_and_restores_through_broker) {
    const SettingOperation op{OperationId::GamePriorityHint, OperationScope::Machine, "ifeoprio:cs2.exe", "above_normal", false, false};
    Broker b;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, op).state == broker::StepState::Applied);
    EF_CHECK(b.ifeo_reg.values.size() == 1 && b.active(b.perf_store) == 1);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, op).state == broker::StepState::Restored);
    EF_CHECK(b.ifeo_reg.values.empty());
    // Izin listesi disi / yanlis deger dispatcher'da reddedilir; adaptore hic gidilmez.
    SettingOperation wrong = op;
    wrong.target = "ifeoprio:valorant.exe";
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, wrong).state == broker::StepState::Rejected);
    wrong = op;
    wrong.new_value = "high";
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, wrong).state == broker::StepState::Rejected);
    EF_CHECK(b.ifeo_reg.writes == 1);
}

// ---- B6 (ADR-040 m.8): G13/G14 izin listesi, kapali bayrak, geri alma her zaman izinli. Tum DRS SAHTE (MOCK). ----
EF_TEST(drs_new_value_allowlist) {
    // Yalniz tek hedef deger: is parcacikli iyilestirme 1 (ENABLE), FXAA 0 (OFF). Komsu degerler ret.
    EF_CHECK(broker::admin_operation_allowed(drs_op(nv::DrsSetting::ThreadedOptimization, broker::kThreadedOptimizationTarget)));
    EF_CHECK(broker::admin_operation_allowed(drs_op(nv::DrsSetting::FxaaEnable, broker::kFxaaOffTarget)));
    static_assert(broker::kThreadedOptimizationTarget == 1 && broker::kFxaaOffTarget == 0, "NVAPI ENABLE / OFF");
    for (std::uint32_t v : {0u, 2u, 3u, 0xFFFFFFFFu}) EF_CHECK(!broker::admin_operation_allowed(drs_op(nv::DrsSetting::ThreadedOptimization, v)));
    for (std::uint32_t v : {1u, 2u, 0xFFFFFFFFu}) EF_CHECK(!broker::admin_operation_allowed(drs_op(nv::DrsSetting::FxaaEnable, v)));
    // Yalniz global profil; oyun profili ve kullanici kapsami ret.
    SettingOperation app{OperationId::VendorProfileValue, OperationScope::Machine, "drs:app:cs2.exe:20C1221E", "00000001", false, false};
    EF_CHECK(!broker::admin_operation_allowed(app));
    auto user = drs_op(nv::DrsSetting::FxaaEnable, 0);
    user.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(user));
    // Tabloda olmayan komsu NVAPI kimlikleri (FXAA_ALLOW, FXAA_INDICATOR) ret.
    SettingOperation allow{OperationId::VendorProfileValue, OperationScope::Machine, "drs:global:1034CB89", "00000000", false, false};
    EF_CHECK(!broker::admin_operation_allowed(allow));
}

EF_TEST(drs_new_items_flag_off) {
    static_assert(!broker::kThreadedOptimizationItemEnabled, "B6: gercek NVIDIA GPU turu gelene kadar kapali");
    static_assert(!broker::kFxaaOffItemEnabled, "B6: gercek NVIDIA GPU turu gelene kadar kapali");
    const auto th = drs_op(nv::DrsSetting::ThreadedOptimization, 1);
    const auto fx = drs_op(nv::DrsSetting::FxaaEnable, 0);
    EF_CHECK(!broker::admin_apply_allowed(th) && !broker::admin_apply_allowed(fx));
    // Bayrak disaridan acilirsa yalniz ilgili oge acilir (diger bayrak ve G07 etkilenmez).
    broker::DrsExtraFlags on_th;
    on_th.threaded_optimization = true;
    EF_CHECK(broker::admin_apply_allowed(th, on_th) && !broker::admin_apply_allowed(fx, on_th));
    EF_CHECK(!broker::admin_apply_allowed(drs_op(nv::DrsSetting::ShaderCacheSize, broker::kShaderCacheTargetMb), on_th));
    EF_CHECK(!broker::admin_apply_allowed(drs_op(nv::DrsSetting::ThreadedOptimization, 2), on_th));  // bayrak izin listesini genisletmez
    // Mevcut ogeler degismedi.
    EF_CHECK(broker::admin_apply_allowed(drs_op(nv::DrsSetting::PowerMode, 1)) && broker::admin_apply_allowed(drs_op(nv::DrsSetting::MaxPrerendered, 1)));
    // Plan: bayraklar kapali -> ek oge yok (FXAA acik ve is parcacikli iyilestirme varsayilan olsa bile).
    nv::FakeDrs d;
    d.defaults[0x1057EB71] = 1;
    d.defaults[0x007BA09E] = 1;
    d.defaults[0x20C1221E] = 0;
    d.global[0x1074C972] = 1;
    EF_CHECK(perf::nvidia_plan_items(&d, kNvOk).empty());
    // Broker: Apply/Fix reddedilir ve hicbir sey yazilmaz.
    Broker b;
    b.drs_api.global[0x1074C972] = 1;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, th).state == broker::StepState::Rejected);
    EF_CHECK(b.exec->handle(ipc::RequestAction::Fix, fx).state == broker::StepState::Rejected);
    EF_CHECK(b.drs_api.global.count(0x20C1221E) == 0 && b.drs_api.global[0x1074C972] == 1 && b.active(b.perf_store) == 0);
}

EF_TEST(drs_new_restore_allowed_when_flag_off) {
    // B2 bulgusu: bayrak sonradan kapatilirsa kayitli GERI ALMA reddedilmemeli. Bayrak acikken yazilmis kaydi temsil
    // etmek icin journal motorla dogrudan (broker kapisi disinda) doldurulur; sonra broker Restore ile geri alir.
    for (const bool inherited : {true, false}) {
        Broker b;
        if (!inherited) b.drs_api.global[0x1074C972] = 1;  // acik deger
        else b.drs_api.defaults[0x20C1221E] = 0;           // miras
        const auto op = inherited ? drs_op(nv::DrsSetting::ThreadedOptimization, 1) : drs_op(nv::DrsSetting::FxaaEnable, 0);
        rec::JournalTx tx;
        tx.id = tx_id(b.ids);
        tx.target_sid = kUserSid;
        rec::JournalStep st;
        st.op = op;
        tx.steps.push_back(st);
        const auto applied = b.perf_engine.apply(tx);
        EF_CHECK(applied.ok() && b.active(b.perf_store) == 1);
        EF_CHECK(!broker::admin_apply_allowed(op));      // yeni yazma kapali
        EF_CHECK(broker::admin_operation_allowed(op));   // bicim izinli: geri alma istegi kabul edilir
        const auto r = b.exec->handle(ipc::RequestAction::Restore, op);
        EF_CHECK(r.state == broker::StepState::Restored);
        if (inherited) EF_CHECK(b.drs_api.global.count(0x20C1221E) == 0);  // acik override silindi, varsayilan yazilmadi
        else EF_CHECK(b.drs_api.global[0x1074C972] == 1);                 // eski acik deger birebir
        EF_CHECK(b.active(b.perf_store) == 0);
        // Oyun Hazirligi geri alma yolu (UndoFix) da bayrakla kapanmaz: kayit yoksa NothingToDo, Rejected DEGIL.
        EF_CHECK(b.exec->handle(ipc::RequestAction::UndoFix, op).state != broker::StepState::Rejected);
    }
    // BootOptionSet satirinin mevcut davranisi degismedi (B0 iskeleti: bicim tanimsiz, yeni yazma kapali).
    SettingOperation boot{OperationId::BootOptionSet, OperationScope::Machine, "boot:dynamictick", "off", false, false};
    EF_CHECK(!broker::admin_apply_allowed(boot));
}

// B9 (R-B3 onerisi): kapali bayrakli katalog girdisi motora hic girmez; geri alma bicimi her zaman izinli kalir.
EF_TEST(tweak_value_set_apply_gate_closed_entry_rejected_but_restore_shape_allowed) {
    using adapters::windows::tweak_apply_allowed;
    SettingOperation closed{OperationId::TweakValueSet, OperationScope::Machine, "tweak:sched.priority_separation", "38", false, false};
    EF_CHECK(broker::admin_operation_allowed(closed));  // geri alma istegi bicimi: izinli
    EF_CHECK(!broker::admin_apply_allowed(closed));     // yeni yazma: bayrak kapali, motora girmez
    // Bayragi acik (kapali olmayan) bir girdi etkilenmez: yeni yazma yine izinli, yanlis deger yine reddedilir.
    SettingOperation open{OperationId::TweakValueSet, OperationScope::Machine, "tweak:update.no_reboot", "1", false, false};
    EF_CHECK(broker::admin_operation_allowed(open) && broker::admin_apply_allowed(open));
    open.new_value = "0";
    EF_CHECK(!broker::admin_operation_allowed(open) && !broker::admin_apply_allowed(open));
    // Tabloda olmayan hedef hicbir yoldan gecmez.
    SettingOperation unknown{OperationId::TweakValueSet, OperationScope::Machine, "tweak:no.such", "1", false, false};
    EF_CHECK(!broker::admin_operation_allowed(unknown) && !broker::admin_apply_allowed(unknown));
    // Kapi yalniz TweakValueSet'e baglidir: boot ve DRS satirlari degismedi.
    SettingOperation boot{OperationId::BootOptionSet, OperationScope::Machine, "boot:dynamictick", "off", false, false};
    EF_CHECK(!broker::admin_apply_allowed(boot));
    EF_CHECK(tweak_apply_allowed(adapters::windows::find_tweak_target("tweak:update.no_reboot"), "1",
                                  adapters::windows::current_tweak_runtime()));
}

// R-E2: kayitta Etkin ama gercek deger baska program/surucu tarafindan degistirilmisse (bayat adim), yeniden uygulama
// "Blocked" olmaz: bayat adim KeptExternal ile kapanir (ayar yazilmaz), sonra yeni kayitla uygulanir. Degeri hala bizim
// yazdigimiz adim eskisi gibi Blocked kalir (ozgun anlik goruntu ezilmez).
EF_TEST(admin_apply_after_external_change_retires_stale_step_then_applies) {
    Broker b;
    b.hags_api.inner.value = 1;
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Applied);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(2));
    // Deger hala bizim: engel kalir.
    EF_CHECK(b.exec->handle(ipc::RequestAction::Apply, hags_op()).state == broker::StepState::Blocked);
    // Baska program geri cevirdi: bayat adim kapanir, yeni uygulama yapilir.
    b.hags_api.inner.value = 1;
    const int writes = b.hags_api.inner.writes;
    const auto r = b.exec->handle(ipc::RequestAction::Apply, hags_op());
    EF_CHECK(r.state == broker::StepState::Applied);
    EF_CHECK(b.hags_api.inner.writes == writes + 1 && b.hags_api.inner.value == std::optional<std::uint32_t>(2));
    EF_CHECK(b.active(b.perf_store) == 1);  // eski kayit kapandi, tek etkin kayit
    // Yeni kayit mevcut (1) degeri ozgun sayar: geri alma 1'e doner.
    EF_CHECK(b.exec->handle(ipc::RequestAction::Restore, hags_op()).state == broker::StepState::Restored);
    EF_CHECK(b.hags_api.inner.value == std::optional<std::uint32_t>(1));
}
