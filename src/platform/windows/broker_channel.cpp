#include "platform/windows/broker_channel.h"

#include "platform/windows/maintenance_win.h"
#include "platform/windows/restore_point_win.h"

#include <windows.h>
#include <objbase.h>
#include <sddl.h>

#include <algorithm>
#include <filesystem>
#include <memory>

#include "core/adapters/nvidia/drs.h"
#include "core/adapters/windows/background.h"
#include "core/adapters/windows/fast_startup.h"
#include "core/adapters/windows/hags.h"
#include "core/adapters/windows/power_subs.h"
#include "core/adapters/windows/service_start_type.h"
#include "core/adapters/windows/defender_exclusion.h"
#include "platform/windows/adapters/win_defender.h"
#include "core/adapters/windows/ifeo_priority.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"
#include "platform/windows/adapters/win_background.h"
#include "platform/windows/adapters/win_fast_startup.h"
#include "platform/windows/adapters/win_boot_options.h"
#include "platform/windows/adapters/win_dns.h"
#include "platform/windows/adapters/win_gaming.h"
#include "platform/windows/adapters/win_hags.h"
#include "platform/windows/adapters/win_machine_registry.h"
#include "platform/windows/adapters/win_power_subs.h"
#include "platform/windows/adapters/win_tasks_services.h"
#include "platform/windows/admin_exit_gate.h"
#include "platform/windows/elevation.h"
#include "platform/windows/games/live_permit.h"
#include "platform/windows/win_env.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/nvidia/nvapi_drs.h"
#include "platform/windows/pipe.h"
#include "platform/windows/wua_install/wua_install.h"
#include "platform/windows/vendor_install/vendor_install_win.h"

namespace ef::platform {

using contracts::ErrorCode;
namespace broker = core::broker;

namespace {
constexpr std::uint32_t kSendTimeoutMs = 5000;
constexpr std::uint32_t kReplyTimeoutMs = 30000;  // DRS kaydi birkac saniye surebilir
constexpr DWORD kConnectWindowMs = 20000;
constexpr wchar_t kMachineLock[] = L"Global\\EcuFps.MachineOps";

// ADR-033: kilidi artik LocalSystem hizmet de olusturabilir. SYSTEM belirtecinin varsayilan DACL'i Yoneticilere yalniz
// GR|GX verir; o zaman yukseltilmis broker/kurulum CreateMutexW (MUTEX_ALL_ACCESS) ile ACCESS_DENIED alir ve beklemek
// yerine kilitsiz kalirdi. Acik DACL: SYSTEM + Yoneticiler tam; baskasi yok (Global\ ad alani zaten yonetici/hizmet ister).
HANDLE create_machine_lock() {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd, nullptr))
        return nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    HANDLE h = CreateMutexW(&sa, FALSE, kMachineLock);
    LocalFree(sd);
    return h;
}

// Restore noktasi anlik goruntu alir: normal yanit suresi yetmez.
constexpr std::uint32_t kRestorePointReplyMs = 300000;
constexpr std::uint32_t kMaintenanceReplyMs = 3600000;  // goruntu onarimi uzun surebilir; servis islemi oldurulmez
constexpr std::uint32_t kReplySliceMs = 1000;           // B2: cikis kapisi yokleme araligi

class PipeTransport final : public broker::FrameTransport {
public:
    PipeTransport(std::unique_ptr<PipeClient> c, HANDLE proc, std::uint32_t reply_ms) : c_(std::move(c)), proc_(proc), reply_ms_(reply_ms) {}
    ~PipeTransport() override {
        c_.reset();
        if (proc_) {
            // Broker Bye sonrasi kendiliginden biter. Cikista birakildiysa (B2) beklenmez: broker isini bitirip journal'a
            // yazar, pipe kapandigini gorup kendisi cikar.
            if (!admin_exit_gate().abandoned()) WaitForSingleObject(proc_, 5000);
            CloseHandle(proc_);
        }
    }
    contracts::Status send(const core::ipc::Frame& f) override {
        if (admin_exit_gate().abandoned()) return contracts::Status::fail(ErrorCode::IoError, "abandoned at exit");
        return c_->write_frame(f, kSendTimeoutMs);
    }
    contracts::Result<core::ipc::Frame> receive() override {
        // B2: yanit dilimlerle beklenir; her dilimde cikis kapisi bakilir (zorunlu cikis en cok ~1 sn icinde birakir).
        // Kisa islerde toplam sure reply_ms_; uzun bakim isinde sabit sure dolunca "bitti" sayilmaz, broker sureci
        // yasadikca beklenir (makine kilidi onda), ust sinir 6 saat.
        const auto start = GetTickCount64();
        for (;;) {
            if (admin_exit_gate().abandoned()) return contracts::Result<core::ipc::Frame>::fail(ErrorCode::IoError, "abandoned at exit");
            auto f = c_->read_frame(kReplySliceMs);
            if (f.ok() || f.error().detail != "timeout") return f;
            const auto elapsed = GetTickCount64() - start;
            if (reply_ms_ <= kReplyTimeoutMs) {
                if (elapsed >= reply_ms_) return f;
                continue;
            }
            if (proc_ && WaitForSingleObject(proc_, 0) != WAIT_TIMEOUT) return f;
            if (elapsed > 6ull * 3600ull * 1000ull) return f;  // ust sinir: 6 saat
        }
    }

private:
    std::unique_ptr<PipeClient> c_;
    std::uint32_t reply_ms_ = kReplyTimeoutMs;
    HANDLE proc_;
};

std::int64_t unix_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

struct MachineJournal {
    std::unique_ptr<core::recovery::JournalFs> fs;
    std::unique_ptr<core::recovery::JournalStore> store;
    std::string error;  // CORE-01: guvenlik dogrulamasi reddinin tanilama kaydi (istemciye tasinir)
};

MachineJournal open_machine_journal(bool create, const wchar_t* sub = nullptr) {
    MachineJournal j;
    auto d = machine_journal_dir(create);
    if (!d.ok()) {
        j.error = d.error().detail;
        return j;
    }
    auto dir = d.value();
    if (sub) {
        dir /= sub;
        std::error_code ec;
        if (create) std::filesystem::create_directories(dir, ec);  // ust klasorun korumali ACL'ini miras alir
        if (!std::filesystem::exists(dir, ec)) return j;           // henuz yok: kapsam kapali
        if (auto s = verify_machine_journal_dir(dir); !s.ok()) {   // alt dizin de sahiplik/ACL dogrulanir
            j.error = s.error().detail;
            return j;
        }
    }
    j.fs = std::make_unique<WinJournalFs>(dir);
    j.store = std::make_unique<core::recovery::JournalStore>(*j.fs, machine_id());
    return j;
}
}  // namespace

broker::BatchResult BrokerChannel::run(core::ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops) {
    AdminExitGate::Call in_call(admin_exit_gate());
    if (admin_exit_gate().abandoned()) {  // B2: cikis basladi; yeni UAC/broker yok (hicbir istek gonderilmedi)
        broker::BatchResult r;
        r.detail = "abandoned at exit";
        return r;
    }
    ShellElevationLauncher launcher;
    const std::string nonce = random_nonce();
    const auto self = static_cast<std::uint32_t>(GetCurrentProcessId());
    // T08: Apply toplu isteginde geri yukleme noktasi varsa yanit payi uzun (RestorePoint ile ayni).
    bool has_rp = action == core::ipc::RequestAction::RestorePoint;
    for (const auto& op : ops) has_rp = has_rp || broker::request_action_for(action, op) == core::ipc::RequestAction::RestorePoint;
    broker::Connector connect = [&]() -> contracts::Result<std::unique_ptr<broker::FrameTransport>> {
        using R = contracts::Result<std::unique_ptr<broker::FrameTransport>>;
        HANDLE proc = static_cast<HANDLE>(launcher.take_process_handle());
        if (!proc) return R::fail(ErrorCode::IoError, "broker process handle missing");
        const DWORD pid = GetProcessId(proc);
        const DWORD start = GetTickCount();
        for (;;) {
            auto c = PipeClient::connect(pipe_name_for(nonce), pid, 250);
            if (c.ok()) return std::unique_ptr<broker::FrameTransport>(new PipeTransport(std::move(c).value(), proc, has_rp ? kRestorePointReplyMs : action == core::ipc::RequestAction::Maintenance || action == core::ipc::RequestAction::DriverInstall || action == core::ipc::RequestAction::VendorInstall ? kMaintenanceReplyMs : kReplyTimeoutMs));
            if (c.code() == ErrorCode::Denied) {  // pipe baska surece ait (ad gaspi): veri gonderilmez
                CloseHandle(proc);
                return R::fail(ErrorCode::Denied, c.error().detail);
            }
            if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
                DWORD code = 0;
                GetExitCodeProcess(proc, &code);
                CloseHandle(proc);
                return R::fail(ErrorCode::IoError, "broker exited early (" + std::to_string(code) + ")");
            }
            if (GetTickCount() - start > kConnectWindowMs) {
                CloseHandle(proc);
                return R::fail(ErrorCode::IoError, "broker pipe timeout");
            }
            if (admin_exit_gate().abandoned()) {  // B2: broker baglanmadan cikis; istek gonderilmedi, broker ust sureci kaybeder
                CloseHandle(proc);
                return R::fail(ErrorCode::IoError, "abandoned at exit");
            }
        }
    };
    return broker::run_admin_batch(launcher, self, nonce, connect, action, ops);
}

std::vector<contracts::SettingOperation> BrokerChannel::active_ops() {
    auto j = open_machine_journal(false);  // salt okuma; yoksa etkin islem yok
    if (!j.store) return {};
    // M1: yarim kalmis kayitlar da dahil (Kapali bunlari broker'a gonderir; broker oturum basinda uzlastirir).
    return broker::journal_active_ops(j.store->scan(), true);
}

std::vector<contracts::SettingOperation> BrokerChannel::perf_recovery_ops() {
    auto j = open_machine_journal(false);
    if (!j.store) return {};
    return broker::journal_unreconciled_ops(j.store->scan(), true);
}

std::vector<contracts::SettingOperation> BrokerChannel::perf_conflict_ops() {
    auto j = open_machine_journal(false);
    if (!j.store) return {};
    return broker::journal_conflict_ops(j.store->scan());
}

std::vector<core::recovery::FailedRestoreItem> BrokerChannel::machine_failed_items() {
    std::vector<core::recovery::FailedRestoreItem> out;
    if (auto j = open_machine_journal(false); j.store) out = core::recovery::failed_restore_items("machine", j.store->scan());
    if (auto j = open_machine_journal(false, L"fixes"); j.store)
        for (auto& i : core::recovery::failed_restore_items("machine_fixes", j.store->scan())) out.push_back(std::move(i));
    return out;
}

bool BrokerChannel::recovery_needed() {
    auto j = open_machine_journal(false);
    if (!j.store) return false;
    return !broker::journal_unreconciled_ops(j.store->scan(), true).empty();
}

std::vector<contracts::SettingOperation> BrokerChannel::fix_active_ops() {
    auto j = open_machine_journal(false, L"fixes");
    if (!j.store) return {};
    return broker::journal_active_ops(j.store->scan(), false);
}

std::vector<contracts::SettingOperation> BrokerChannel::fix_recovery_ops() {
    auto j = open_machine_journal(false, L"fixes");
    if (!j.store) return {};
    return broker::journal_unreconciled_ops(j.store->scan(), true);  // RecoveryRequired + yarim kalmis
}

std::vector<contracts::SettingOperation> BrokerChannel::fix_conflict_ops() {
    auto j = open_machine_journal(false, L"fixes");
    if (!j.store) return {};
    return broker::journal_conflict_ops(j.store->scan());  // RecoveryRequired + dis degisiklik
}

void BrokerChannel::fix_target_lists(std::vector<std::string>& active, std::vector<std::string>& failed, std::vector<std::string>& interrupted,
                                     std::vector<std::string>& conflict) {
    auto j = open_machine_journal(false, L"fixes");  // salt okuma; UAC yok
    if (!j.store) return;
    broker::journal_fix_targets(j.store->scan(), active, failed, interrupted, conflict);
}

std::vector<contracts::SettingOperation> BrokerChannel::unreconciled_ops() {
    std::vector<contracts::SettingOperation> out;
    for (const wchar_t* sub : {static_cast<const wchar_t*>(nullptr), L"fixes"}) {
        auto j = open_machine_journal(false, sub);
        if (!j.store) continue;
        // Yarim kalmis + dogrulanamamis geri alma (RecoveryRequired): sessizce takili kalmaz, acilista sorulur.
        for (auto& op : broker::journal_unreconciled_ops(j.store->scan(), true)) {
            const bool dup = std::any_of(out.begin(), out.end(), [&](const contracts::SettingOperation& o) { return o.id == op.id && o.target == op.target; });
            if (!dup) out.push_back(std::move(op));
        }
    }
    return out;
}

std::unique_ptr<MachineOpsLock> MachineOpsLock::try_acquire(unsigned timeout_ms) {
    if (!is_process_elevated()) return nullptr;  // makine kapsami yalniz yukseltilmis yazicida
    HANDLE h = create_machine_lock();
    if (!h) return nullptr;
    const DWORD w = WaitForSingleObject(h, timeout_ms);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        CloseHandle(h);
        return nullptr;
    }
    return std::unique_ptr<MachineOpsLock>(new MachineOpsLock(h));
}

MachineOpsLock::~MachineOpsLock() {
    if (h_) {
        ReleaseMutex(static_cast<HANDLE>(h_));
        CloseHandle(static_cast<HANDLE>(h_));
    }
}

// SRSetRestorePoint icin Microsoft'un istedigi kurulum: MTA + hizmet hesaplarina geri cagri izni.
AdminCom::AdminCom() {
    owned_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    PSECURITY_DESCRIPTOR rel = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:(A;;0x3;;;BA)(A;;0x3;;;LS)(A;;0x3;;;NS)(A;;0x3;;;SY)", SDDL_REVISION_1, &rel, nullptr)) return;
    SECURITY_DESCRIPTOR abs_sd{};
    DWORD asz = sizeof(abs_sd), dsz = 0, ssz = 0, osz = 0, gsz = 0;
    (void)MakeAbsoluteSD(rel, nullptr, &asz, nullptr, &dsz, nullptr, &ssz, nullptr, &osz, nullptr, &gsz);
    std::vector<std::uint8_t> sd(asz), dacl(dsz), sacl(ssz), owner(osz), group(gsz);
    if (MakeAbsoluteSD(rel, sd.data(), &asz, reinterpret_cast<PACL>(dacl.data()), &dsz, reinterpret_cast<PACL>(sacl.data()), &ssz, owner.data(), &osz, group.data(), &gsz))
        (void)CoInitializeSecurity(sd.data(), -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY, nullptr,
                                   EOAC_DISABLE_AAA | EOAC_NO_CUSTOM_MARSHAL, nullptr);
    LocalFree(rel);
}

AdminCom::~AdminCom() {
    if (owned_) CoUninitialize();
}

namespace {

core::drivers::RestorePointState driver_restore_point(core::recovery::JournalFs* rp) {
    if (!rp) return core::drivers::RestorePointState::Failed;
    WinRestorePointApi rp_api;
    const auto run = core::recovery::run_restore_point(rp_api, *rp, "Ecu Fps");
    using core::recovery::RestorePointOutcome;
    return run.outcome == RestorePointOutcome::Created              ? core::drivers::RestorePointState::Created
           : run.outcome == RestorePointOutcome::SkippedByFrequency ? core::drivers::RestorePointState::Recent
                                                                    : core::drivers::RestorePointState::Failed;
}

// run_broker ile yardimci hizmetin ortak yurutucu ortami. Uyeler bildirim sirasinda kurulur, ters sirada yikilir
// (yurutucu en son kurulur, ilk yikilir). Lambdalar uyelere isaretci ile baglanir; nesne tasinmaz (yigin uzerinde).
class AdminRuntimeImpl final : public AdminRuntime {
public:
    explicit AdminRuntimeImpl(const AdminIdentity& id) : client_proc_(static_cast<HANDLE>(id.client_process)) {
        // Izin listesindeki yonetici adaptorleri (baska hicbir yazici kayitli degil).
        registry_.add(contracts::OperationId::ServiceSessionStop, &service_stop_);  // WIN-10: plan broker'a gonderir
        registry_.add(contracts::OperationId::HagsSet, &hags_);
        registry_.add(contracts::OperationId::FastStartupSet, &fast_startup_);
        registry_.add(contracts::OperationId::BootOptionSet, &boot_option_);  // ADR-040 m.4: apply bayrakla kapali, restore acik
        registry_.add(contracts::OperationId::DnsServersSet, &dns_);
        registry_.add(contracts::OperationId::PowerSettingSet, &power_subs_);
        registry_.add(contracts::OperationId::VendorProfileValue, &drs_);
        // ADR-035/T02 + T12: katalog degerleri (yalniz Machine girdileri broker izninden gecer) ve ozellik hizmeti baslangic tipi.
        registry_.add(contracts::OperationId::TweakValueSet, &tweak_);
        registry_.add(contracts::OperationId::ServiceStartTypeSet, &service_start_);
        registry_.add(contracts::OperationId::DefenderExclusionSet, &defender_);  // T11: liste okuma/yazma yalniz yukseltilmis broker
        registry_.add(contracts::OperationId::GamePriorityHint, &ifeo_priority_);  // T09: yalniz izin listesindeki exe + above_normal

        perf_ = open_machine_journal(true);
        fixes_ = open_machine_journal(true, L"fixes");
        if (perf_.store) perf_engine_ = std::make_unique<core::recovery::TransactionEngine>(*perf_.store, registry_);
        if (fixes_.store) fixes_engine_ = std::make_unique<core::recovery::TransactionEngine>(*fixes_.store, registry_);

        deps_.perf_store = perf_.store.get();
        deps_.perf_engine = perf_engine_.get();
        deps_.fixes_store = fixes_.store.get();
        deps_.fixes_engine = fixes_engine_.get();
        deps_.journal_error = !perf_.error.empty() ? perf_.error : fixes_.error;  // CORE-01 tanilama kaydi istemciye
        deps_.new_tx_id = [] { return random_nonce(); };
        deps_.now_unix_ms = unix_ms;
        deps_.ctx.broker_user_sid = id.actor_sid;
        deps_.ctx.target_user_sid = id.target_user_sid;
        deps_.ctx.elevated = is_process_elevated();
        deps_.ctx.machine_lock_held = id.machine_lock_held;
        // Yurutucu izni: istemciden gelen bilgiye degil kendi gozlemine dayanir. Kitaplik hedef kullanicinin kovanindan.
        permit_ = std::make_shared<games::LiveGamePermit>(to_wide(deps_.ctx.target_user_sid));
        deps_.permit = [permit = permit_]() -> core::recovery::WriteDecision {
            const auto d = permit->verify(core::games::GuardedAction::SettingMutation);
            return {d.allowed, d.reason_key};
        };
        // EF-X16: is kaydi ayri alt dizinde; journal taramasi bu dosyayi islem sanmaz.
        rp_jobs_ = open_machine_journal(true, L"restore_point");
        deps_.create_restore_point = [fs = rp_jobs_.fs.get()]() -> core::recovery::RestorePointRun {
            if (!fs) return {core::recovery::RestorePointOutcome::ApiFailed, 0, "job directory unavailable"};
            WinRestorePointApi api;
            return core::recovery::run_restore_point(api, *fs, "Ecu Fps");
        };
        maint_jobs_ = open_machine_journal(true, L"maintenance");
        deps_.maintenance = [fs = maint_jobs_.fs.get(), rp = rp_jobs_.fs.get()](std::string_view target) -> core::maintenance::MaintRun {
            if (target == core::maintenance::kStandbyPurgeTarget) {  // T15: tek seferlik; journal/is kaydi yok
                WinMemoryList mem;
                const auto pr = core::maintenance::run_standby_purge(mem);
                return {pr.ok ? core::maintenance::MaintOutcome::Purged : core::maintenance::MaintOutcome::Failed, false,
                        core::maintenance::encode_purge_detail(pr)};
            }
            WinImageServicing api(rp);
            if (target == core::maintenance::kImageCheckTarget) return core::maintenance::run_image_check(api, fs);
            if (!fs) return {core::maintenance::MaintOutcome::Failed, false, "job directory unavailable"};
            return core::maintenance::run_image_repair(api, *fs, unix_ms());
        };
        // Spec H2: surucu kurulumu. Kayitlar ayri "drivers" makine journal'inda (Performans / duzeltme uzlastirmasi bunlara
        // dokunmaz). Iptal: istemci (uygulama) kapandiysa kurulum baslamadan birakilir; baslamis kurulum kesilmez.
        drv_jobs_ = open_machine_journal(true, L"drivers");
        deps_.driver_install = [store = drv_jobs_.store.get(), rp = rp_jobs_.fs.get(), permit = permit_, proc = client_proc_,
                                actor = deps_.ctx.broker_user_sid, target = deps_.ctx.target_user_sid](std::string_view tgt) -> core::drivers::InstallRun {
            auto api = wua::make_driver_install([rp]() { return driver_restore_point(rp); });
            const auto started = GetTickCount64();
            core::drivers::InstallDeps d;
            d.api = api.get();
            d.journal = store;
            d.new_tx_id = [] { return random_nonce(); };
            d.now_unix_ms = unix_ms;
            d.cancel = [proc, started] {
                if (proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) return true;
                return GetTickCount64() - started > 2ull * 3600ull * 1000ull;  // ust sinir: 2 saat
            };
            d.gate_ok = [permit] { return permit->verify(core::games::GuardedAction::SettingMutation).allowed; };
            d.actor_sid = actor;
            d.target_sid = target;
            return core::drivers::run_driver_install(d, tgt);
        };
        // ADR-032: imzasi dogrulanan resmi ekran karti paketi. Kayitlar ayri "vendor_drivers" makine journal'inda; paket
        // kopyasi makine journal kokunun "vendor_pkg" alt klasorunde (yonetici ACL'i). Iptal yalniz paket calismadan once.
        vdrv_jobs_ = open_machine_journal(true, L"vendor_drivers");
        machine_root_ = std::make_unique<contracts::Result<std::filesystem::path>>(machine_journal_dir(false));
        deps_.vendor_install = [store = vdrv_jobs_.store.get(), root = machine_root_.get(), rp = rp_jobs_.fs.get(), permit = permit_,
                                proc = client_proc_, actor = deps_.ctx.broker_user_sid,
                                target = deps_.ctx.target_user_sid](std::string_view tgt) -> core::drivers::VendorRun {
            if (!root->ok()) {
                core::drivers::VendorRun r;
                r.outcome = core::drivers::VendorOutcome::StageFailed;
                return r;
            }
            auto api = vendor::make_vendor_install(root->value(), [rp]() { return driver_restore_point(rp); });
            core::drivers::VendorInstallDeps d;
            d.api = api.get();
            d.journal = store;
            d.new_tx_id = [] { return random_nonce(); };
            d.now_unix_ms = unix_ms;
            d.cancel = [proc] { return proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0; };
            d.gate_ok = [permit] { return permit->verify(core::games::GuardedAction::SettingMutation).allowed; };
            d.actor_sid = actor;
            d.target_sid = target;
            return core::drivers::run_vendor_install(d, tgt);
        };
        executor_ = std::make_unique<broker::AdminExecutor>(deps_);
    }

    broker::AdminExecutor& executor() override { return *executor_; }
    const broker::ExecutorDeps& deps() const override { return deps_; }

    broker::SessionReconcile begin_session() override {
        auto r = executor_->reconcile_on_start();
        if (rp_jobs_.fs && permit_->verify(core::games::GuardedAction::SettingMutation).allowed) {
            WinRestorePointApi rp_api;  // onceki oturumdan yarim kalan nokta kapatilir
            (void)core::recovery::recover_pending_restore_point(rp_api, *rp_jobs_.fs);
        }
        return r;
    }

private:
    HANDLE client_proc_ = nullptr;  // sahibi cagiran
    adapters::WinHagsApi hags_api_;
    core::adapters::windows::HagsAdapter hags_{hags_api_};
    adapters::WinFastStartupApi fast_startup_api_;
    adapters::WinDnsApi dns_api_;
    core::adapters::windows::DnsAdapter dns_{dns_api_, adapters::read_dns_policy_facts};  // yazma aninda ilke yeniden okunur
    core::adapters::windows::FastStartupAdapter fast_startup_{fast_startup_api_};
    adapters::WinBootOptionApi boot_option_api_;
    core::adapters::windows::BootOptionAdapter boot_option_{boot_option_api_};
    adapters::WinPowerSubApi power_subs_api_;
    core::adapters::windows::PowerSubAdapter power_subs_{power_subs_api_};
    nvidia::LazyDrs drs_api_;
    core::adapters::nvidia::DrsAdapter drs_{drs_api_};
    adapters::WinServiceApi service_api_;
    adapters::WinServiceBusyProbe service_busy_;
    core::adapters::windows::ServiceSessionStopAdapter service_stop_{service_api_, service_busy_};
    core::adapters::windows::ServiceStartTypeAdapter service_start_{service_api_, service_busy_};
    adapters::WinDefenderApi defender_api_;
    core::adapters::windows::DefenderExclusionAdapter defender_{defender_api_};
    adapters::WinUserRegistryApi user_reg_;  // yalniz TweakAdapter kurucusu icin; broker yalniz Machine girdilerini kabul eder
    adapters::WinMachineRegistryApi machine_reg_;
    core::adapters::windows::TweakAdapter tweak_{user_reg_, &machine_reg_};
    core::adapters::windows::IfeoPriorityAdapter ifeo_priority_{&machine_reg_};  // T09
    core::recovery::AdapterRegistry registry_;
    MachineJournal perf_, fixes_, rp_jobs_, maint_jobs_, drv_jobs_, vdrv_jobs_;
    std::unique_ptr<contracts::Result<std::filesystem::path>> machine_root_;
    std::unique_ptr<core::recovery::TransactionEngine> perf_engine_, fixes_engine_;
    std::shared_ptr<games::LiveGamePermit> permit_;
    broker::ExecutorDeps deps_;
    std::unique_ptr<broker::AdminExecutor> executor_;
};

}  // namespace

std::unique_ptr<AdminRuntime> make_admin_runtime(const AdminIdentity& id) { return std::make_unique<AdminRuntimeImpl>(id); }

int run_broker(const core::LaunchArgs& a, const ParentIdentity& parent) {
    if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;
    const AdminCom com;  // SRSetRestorePointW belgelenen COM guvenligi; surec omru boyunca

    // Makine islemleri kilidi (ayni anda tek makine yazicisi).
    HANDLE lock = create_machine_lock();
    bool locked = false;
    if (lock) {
        const DWORD w = WaitForSingleObject(lock, 10000);
        locked = w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
    }
    const auto user = inventory::read_user();
    HANDLE parent_proc = OpenProcess(SYNCHRONIZE, FALSE, parent.pid);
    AdminIdentity id;
    id.actor_sid = user.process_user_sid.value.value_or("");
    id.target_user_sid = user.target_user_sid.value.value_or(user.session_user_sid.value.value_or(""));
    id.client_process = parent_proc;
    id.machine_lock_held = locked;
    auto runtime = make_admin_runtime(id);

    int code = core::kExitHandshakeFailed;
    auto srv = PipeServer::create(pipe_name_for(a.nonce), runtime->deps().ctx.target_user_sid);
    if (srv.ok()) {
        auto& p = *srv.value();
        if (p.accept(kConnectWindowMs).ok()) {
            auto peer = p.client_identity();
            if (peer.ok() && peer_matches(peer.value(), parent.pid, parent.create_time, parent.session, parent.image)) {
                // M1: istemci dogrulandi; ilk istekten once yarim kalmis makine kayitlari uzlastirilir (REC-03).
                (void)runtime->begin_session();
                core::ipc::ServerSession sess(a.nonce, *a.parent_pid, runtime->executor().handler());
                for (int i = 0; i < 64 && !sess.closed(); ++i) {
                    auto f = p.read_frame(kReplyTimeoutMs);
                    if (!f.ok()) break;
                    (void)p.write_frame(sess.on_frame(f.value(), peer.value().pid), kSendTimeoutMs);
                }
                code = sess.closed() && sess.last_error().empty() ? core::kExitOk : core::kExitHandshakeFailed;
            } else {
                code = core::kExitParentInvalid;
            }
        }
    }
    runtime.reset();
    if (parent_proc) CloseHandle(parent_proc);
    if (lock) {
        if (locked) ReleaseMutex(lock);
        CloseHandle(lock);
    }
    return code;
}

}  // namespace ef::platform
