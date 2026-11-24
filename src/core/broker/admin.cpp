#include "core/broker/admin.h"

#include <algorithm>

#include "core/adapters/nvidia/drs.h"
#include "core/adapters/windows/background.h"
#include "core/adapters/windows/boot_options.h"
#include "core/adapters/windows/dns.h"
#include "core/adapters/windows/defender_exclusion.h"
#include "core/adapters/windows/fast_startup.h"
#include "core/adapters/windows/hags.h"
#include "core/adapters/windows/ifeo_priority.h"
#include "core/adapters/windows/power_subs.h"
#include "core/adapters/windows/service_start_type.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/recovery/conflicts.h"
#include "core/serialization/json.h"

namespace ef::core::broker {

using contracts::ErrorCode;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::SettingOperation;
using contracts::TxState;
namespace nv = adapters::nvidia;

namespace {
bool drs_value_allowed(std::uint32_t setting, std::string_view value) {
    const auto v = nv::DrsAdapter::decode(value);
    if (!v) return false;
    switch (static_cast<nv::DrsSetting>(setting)) {
        case nv::DrsSetting::PowerMode: return *v == 1;       // PREFERRED_PSTATE_PREFER_MAX
        case nv::DrsSetting::MaxPrerendered: return *v == 1;  // "Low Latency Mode: On" (on islenmis kare 1)
        case nv::DrsSetting::ShaderCacheSize: return *v == kShaderCacheTargetMb;
        // B6: tek hedef deger; bayrak burada DEGIL admin_apply_allowed'da (geri alma istegi bicimle sinirlanir).
        case nv::DrsSetting::ThreadedOptimization: return *v == kThreadedOptimizationTarget;
        case nv::DrsSetting::FxaaEnable: return *v == kFxaaOffTarget;
        default: return false;  // VSync/FPS siniri vb. yonetici grubunda yok
    }
}

bool same_target(const SettingOperation& a, const SettingOperation& b) { return a.id == b.id && a.target == b.target; }

json::Limits result_limits() {
    json::Limits l;
    l.max_bytes = 4096;
    l.max_depth = 4;
    l.max_string = 512;
    l.max_items = 16;
    return l;
}
}  // namespace

bool admin_operation_allowed(const SettingOperation& op) noexcept {
    if (op.scope != OperationScope::Machine) return false;
    if (op.id == OperationId::DnsServersSet)  // EF-X14: yalniz GUID hedefi + hazir sunucu listesi
        return adapters::windows::parse_dns_target(op.target).has_value() && adapters::windows::dns_preset_value(op.new_value);
    if (op.id == OperationId::MaintenanceJob)
        return (op.target == maintenance::kImageCheckTarget || op.target == maintenance::kImageRepairTarget ||
                op.target == maintenance::kStandbyPurgeTarget) &&
               op.new_value == maintenance::kMaintenanceValue;
    if (op.id == OperationId::RestorePointCreate) return op.target == recovery::kRestorePointTarget && op.new_value == recovery::kRestorePointValue;
    // Spec H2: yalniz "wuinst:<UpdateID>:<rev>" + "install"; ogenin kendisi broker'da yeniden aranir ve dogrulanir.
    if (op.id == OperationId::DriverInstall) return drivers::parse_install_target(op.target).has_value() && op.new_value == drivers::kInstallValue;
    // ADR-032: yalniz "vdrv:<nvidia|amd|intel>:<16 hex>" + "install" (yol yok; dosya adi kimlikten kurulur). Dosya, imza ve
    // imzalayan broker'da yeniden dogrulanir.
    if (op.id == OperationId::VendorDriverInstall)
        return drivers::parse_vendor_target(op.target).has_value() && op.new_value == drivers::kVendorInstallValue;
    // ADR-035/T02: yalniz derlenmis katalogdaki "tweak:<id>" girdisi, hive'i Machine olan ve girdinin on_value'su. Anahtar yolu/ad/tur
    // IPC'den gelmez, tablodan okunur; HKCU girdileri uygulama icinde uygulanir, broker'a hic gelmez.
    if (op.id == OperationId::TweakValueSet)
        return adapters::windows::tweak_admin_allowed(adapters::windows::find_tweak_target(op.target), op.new_value,
                                                      adapters::windows::current_tweak_runtime());
    // ADR-040 m.4: yalniz tablodaki tek BCD secenegi + tek deger; bicim denetimi boot_options.* icinde (B4 doldurur).
    // Bayrak (kBootOptionItemEnabled) yeni yazmayi admin_apply_allowed'da kapatir; geri alma istegi bicimle sinirlanir.
    if (op.id == OperationId::BootOptionSet) return adapters::windows::boot_option_admin_allowed(op, true);
    // ADR-035/T12: yalniz derlenmis ozellik hizmeti tablosundaki ad + "svcstart:<ad>" bicimi + yalniz "disabled".
    if (op.id == OperationId::ServiceStartTypeSet) return adapters::windows::service_start_admin_allowed(op);
    // ADR-035/T11: yalniz "defx:<hex>" (gecerli oyun klasoru bicimi) + deger "1"; kok/sistem/profil yollari reddedilir.
    if (op.id == OperationId::DefenderExclusionSet) return adapters::windows::defender_admin_allowed(op);
    // ADR-035/T09: yalniz derlenmis izin listesindeki "ifeoprio:<exe>" + "above_normal"; alt anahtar ve deger adi sabittir.
    if (op.id == OperationId::GamePriorityHint) return adapters::windows::ifeo_priority_admin_allowed(op);
    // L1: yalniz "2" (acik); uygulama "1" gondermez, broker da kabul etmez.
    if (op.id == OperationId::HagsSet) return op.target == adapters::windows::kHagsTarget && op.new_value == "2";
    // F-48: yalniz "0" (hizli baslatmayi kapat); "1" istemciden kabul edilmez.
    if (op.id == OperationId::FastStartupSet)
        return op.target == adapters::windows::kFastStartupTarget && op.new_value == "0";
    // F-32/F-33/F-42: yalniz uc sabit plan ayari ve katalogun hedef degeri (boost=1, parking=100, ASPM=0).
    if (op.id == OperationId::PowerSettingSet)
        return adapters::windows::admin_power_sub_value_allowed(op.target, op.new_value);
    if (op.id == OperationId::VendorProfileValue) {
        auto t = nv::parse_drs_target(op.target);
        if (!t.ok()) return false;
        // A1.1: oyun profili yalniz guc modu ve hazirlanan kare; shader onbellegi yalniz global (ve dogrulanana kadar kapali).
        if (!t.value().global && static_cast<nv::DrsSetting>(t.value().setting) != nv::DrsSetting::PowerMode &&
            static_cast<nv::DrsSetting>(t.value().setting) != nv::DrsSetting::MaxPrerendered)
            return false;
        return drs_value_allowed(t.value().setting, op.new_value);
    }
    // WIN-10: yalniz derlenmis izin listesindeki hizmet + "svcstop:<ad>" bicimi + yalniz "stopped".
    // Geri alma kayitli islemi yeniden kullanir (snapshot onceki calisma durumunu tasir).
    if (op.id == OperationId::ServiceSessionStop) {
        if (op.new_value != "stopped") return false;
        auto name = adapters::windows::parse_session_stop_target(op.target);
        return name.ok() && adapters::windows::find_session_stoppable(name.value()) != nullptr;
    }
    return false;
}

bool admin_apply_allowed(const SettingOperation& op) noexcept { return admin_apply_allowed(op, DrsExtraFlags{}); }

bool admin_apply_allowed(const SettingOperation& op, const DrsExtraFlags& extra) noexcept {
    if (!admin_operation_allowed(op)) return false;
    if (op.id == OperationId::BootOptionSet && !adapters::windows::kBootOptionItemEnabled) return false;  // ADR-040 m.4
    // R-B3 onerisi: kapali bayrakli katalog girdisi (or. Win32PrioritySeparation) motora girmeden reddedilir. Geri alma
    // (Restore/UndoFix/Recover) admin_operation_allowed'dan gectigi icin bu kapidan etkilenmez.
    if (op.id == OperationId::TweakValueSet &&
        !adapters::windows::tweak_apply_allowed(adapters::windows::find_tweak_target(op.target), op.new_value,
                                                adapters::windows::current_tweak_runtime()))
        return false;
    if (op.id == OperationId::VendorProfileValue) {
        auto t = nv::parse_drs_target(op.target);
        if (!t.ok()) return false;
        switch (static_cast<nv::DrsSetting>(t.value().setting)) {
            case nv::DrsSetting::ShaderCacheSize: return kShaderCacheItemEnabled;  // M3
            case nv::DrsSetting::ThreadedOptimization: return extra.threaded_optimization;  // B6
            case nv::DrsSetting::FxaaEnable: return extra.fxaa_off;                         // B6
            default: break;
        }
    }
    return true;
}

bool admin_needs_reboot(const SettingOperation& op) noexcept {
    // Istemcinin op.needs_reboot alani kullanilmaz: karar tablodan (HAGS, BCD secenegi, katalog girdisi).
    if (op.id == OperationId::HagsSet || op.id == OperationId::BootOptionSet) return true;
    if (op.id == OperationId::TweakValueSet) {
        const auto* e = adapters::windows::find_tweak_target(op.target);
        return e && e->needs_reboot;
    }
    return false;
}

bool tx_unreconciled(TxState s) noexcept {
    return s != TxState::Active && s != TxState::Restored && s != TxState::RecoveryRequired;
}

namespace {
void add_unique(std::vector<SettingOperation>& out, const SettingOperation& op) {
    if (!admin_operation_allowed(op)) return;
    for (const auto& o : out)
        if (same_target(o, op)) return;
    out.push_back(op);
}
}  // namespace

std::vector<SettingOperation> journal_active_ops(const recovery::JournalStore::Listing& l, bool include_unreconciled) {
    std::vector<SettingOperation> out;
    for (const auto& tx : l.transactions)
        if (tx.state == TxState::Active || (include_unreconciled && tx_unreconciled(tx.state)))
            for (const auto& st : tx.steps) add_unique(out, st.op);
    return out;
}

std::vector<SettingOperation> journal_unreconciled_ops(const recovery::JournalStore::Listing& l, bool include_recovery_required) {
    std::vector<SettingOperation> out;
    for (const auto& tx : l.transactions)
        if ((include_recovery_required && tx.state == TxState::RecoveryRequired) || tx_unreconciled(tx.state))
            for (const auto& st : tx.steps) add_unique(out, st.op);
    return out;
}

namespace {
void add_target(std::vector<std::string>& out, const std::string& t) {
    if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
}
}  // namespace

void journal_fix_targets(const recovery::JournalStore::Listing& l, std::vector<std::string>& active, std::vector<std::string>& recovery) {
    std::vector<std::string> interrupted, conflict;
    journal_fix_targets(l, active, recovery, interrupted, conflict);
    for (const auto& t : interrupted) add_target(recovery, t);
    for (const auto& t : conflict) add_target(recovery, t);
}

void journal_fix_targets(const recovery::JournalStore::Listing& l, std::vector<std::string>& active, std::vector<std::string>& failed,
                         std::vector<std::string>& interrupted, std::vector<std::string>& conflict) {
    for (const auto& tx : l.transactions) {
        if (tx.state == TxState::Restored) continue;
        if (tx.state == TxState::Active) {
            for (const auto& st : tx.steps) add_target(active, st.op.target);
        } else if (tx.state == TxState::RollingBack) {  // geri alma basladi ama dogrulanmadan kesildi
            for (const auto& st : tx.steps) add_target(failed, st.op.target);
        } else if (tx_unreconciled(tx.state)) {
            for (const auto& st : tx.steps) add_target(interrupted, st.op.target);
        } else {  // RecoveryRequired: dis degisiklik bekleyen adim catisma, digerleri dogrulanamamis geri alma
            for (const auto& st : tx.steps)
                add_target(st.state == recovery::StepState::Failed && st.error == recovery::kExternalChange ? conflict : failed, st.op.target);
        }
    }
}

std::vector<SettingOperation> journal_conflict_ops(const recovery::JournalStore::Listing& l) {
    std::vector<SettingOperation> out;
    for (const auto& tx : l.transactions)
        if (recovery::has_external_conflict(tx))
            for (const auto& st : tx.steps)
                if (st.state == recovery::StepState::Failed && st.error == recovery::kExternalChange) add_unique(out, st.op);
    return out;
}

std::string_view to_string(StepState s) noexcept {
    switch (s) {
        case StepState::Applied: return "applied";
        case StepState::Restored: return "restored";
        case StepState::NothingToDo: return "nothing_to_do";
        case StepState::Failed: return "failed";
        case StepState::RecoveryRequired: return "recovery_required";
        case StepState::Rejected: return "rejected";
        case StepState::Blocked: return "blocked";
        case StepState::Kept: return "kept";
        case StepState::GateBlocked: return "gate_blocked";
    }
    return "failed";
}

std::string encode_result(std::string_view request_id, const StepResult& r) {
    auto o = json::Value::object();
    o.set("request_id", json::Value::string(std::string(request_id)));
    o.set("target", json::Value::string(r.target));
    o.set("state", json::Value::string(std::string(to_string(r.state))));
    o.set("pending_reboot", json::Value::boolean(r.pending_reboot));
    o.set("detail", json::Value::string(r.detail.substr(0, 256)));
    return json::write(o);
}

contracts::Result<StepResult> decode_result(std::string_view payload) {
    using R = contracts::Result<StepResult>;
    auto doc = json::parse(payload, result_limits());
    if (!doc.ok()) return R(doc.error());
    StepResult r;
    auto target = json::get_string(doc.value(), "target", 128);
    auto state = json::get_string(doc.value(), "state", 32);
    auto reboot = json::get_bool(doc.value(), "pending_reboot");
    if (!target.ok() || !state.ok() || !reboot.ok()) return R::fail(ErrorCode::ParseError, "result fields");
    r.target = target.value();
    r.pending_reboot = reboot.value();
    if (auto d = json::get_string(doc.value(), "detail", 256); d.ok()) r.detail = d.value();
    bool known = false;
    for (auto s : {StepState::Applied, StepState::Restored, StepState::NothingToDo, StepState::Failed, StepState::RecoveryRequired,
                   StepState::Rejected, StepState::Blocked, StepState::Kept, StepState::GateBlocked})
        if (to_string(s) == state.value()) r.state = s, known = true;
    if (!known) return R::fail(ErrorCode::InvalidEnum, "result state");
    return r;
}

// ---- Broker ----
AdminExecutor::AdminExecutor(ExecutorDeps deps) : d_(std::move(deps)) {
    auto run = [this](const SettingOperation& op, const security::DispatchContext&) -> contracts::Status {
        (void)op;
        return contracts::ok_status();  // kontroller gecti; asil is handle() icinde (journal'li motor)
    };
    for (auto id : {OperationId::HagsSet, OperationId::VendorProfileValue, OperationId::ServiceSessionStop,
                    OperationId::FastStartupSet, OperationId::PowerSettingSet, OperationId::RestorePointCreate, OperationId::DnsServersSet, OperationId::MaintenanceJob,
                    OperationId::DriverInstall, OperationId::VendorDriverInstall, OperationId::TweakValueSet, OperationId::ServiceStartTypeSet, OperationId::DefenderExclusionSet, OperationId::GamePriorityHint,
                    OperationId::BootOptionSet}) {  // ADR-040: izin listesi + bayrak once reddeder; kayit dispatcher icin
        security::HandlerSpec spec;
        spec.scopes = {OperationScope::Machine};
        spec.privilege = contracts::Privilege::Admin;
        spec.fn = run;
        (void)dispatcher_.register_handler(id, std::move(spec));
    }
}

StepResult AdminExecutor::handle(ipc::RequestAction action, const SettingOperation& op) {
    StepResult r;
    r.target = op.target;
    if (!admin_operation_allowed(op)) {
        r.state = StepState::Rejected;
        r.detail = "operation not in admin allowlist";
        return r;
    }
    // SEC-03: kapsam, yetki (yukseltilmis), guvenli hedef, makine kilidi.
    if (auto s = dispatcher_.dispatch(op, d_.ctx); !s.ok()) {
        r.state = StepState::Rejected;
        r.detail = s.error().detail;
        return r;
    }
    const bool writes_new = action == ipc::RequestAction::Apply || action == ipc::RequestAction::Fix;
    if (writes_new && !admin_apply_allowed(op)) {  // M3: dogrulanmamis oge yeni yazilmaz
        r.state = StepState::Rejected;
        r.detail = "operation not enabled (unverified)";
        return r;
    }
    // UAC ve broker baslangicindan sonra, her gercek yazimdan hemen once: UI'daki eski izne guvenilmez.
    // Bakim isi ayar yolundan gecmez, ayar da bakim yolundan.
    if ((op.id == OperationId::RestorePointCreate) != (action == ipc::RequestAction::RestorePoint) ||
        (op.id == OperationId::MaintenanceJob) != (action == ipc::RequestAction::Maintenance) ||
        (op.id == OperationId::DriverInstall) != (action == ipc::RequestAction::DriverInstall) ||
        (op.id == OperationId::VendorDriverInstall) != (action == ipc::RequestAction::VendorInstall)) {
        r.state = StepState::Rejected;
        r.detail = "action does not match operation";
        return r;
    }
    const bool mutates = writes_new || action == ipc::RequestAction::Restore || action == ipc::RequestAction::UndoFix || action == ipc::RequestAction::RestorePoint ||
                         action == ipc::RequestAction::Maintenance || action == ipc::RequestAction::DriverInstall ||
                         action == ipc::RequestAction::VendorInstall;
    if (mutates && d_.permit) {
        if (const auto v = d_.permit(); !v.allowed) {
            r.state = StepState::GateBlocked;
            r.detail = v.reason;
            return r;
        }
    }
    if (action == ipc::RequestAction::Recover && reconcile_deferred_) {
        r.state = StepState::GateBlocked;
        r.detail = "gate.game_running";
        return r;
    }
    switch (action) {
        case ipc::RequestAction::Apply: return apply_in(d_.perf_store, d_.perf_engine, op);
        case ipc::RequestAction::Fix: return apply_in(d_.fixes_store, d_.fixes_engine, op);
        case ipc::RequestAction::Restore: return restore_in(d_.perf_store, d_.perf_engine, op);
        case ipc::RequestAction::UndoFix: return restore_in(d_.fixes_store, d_.fixes_engine, op);
        case ipc::RequestAction::Recover: return recover(op);
        case ipc::RequestAction::KeepCurrent: return keep_current(op);
        case ipc::RequestAction::Maintenance: {
            if (!d_.maintenance) {
                r.state = StepState::Failed;
                r.detail = "mj:unsupported";
                return r;
            }
            const auto run = d_.maintenance(op.target);
            if (op.target == maintenance::kStandbyPurgeTarget) {  // T15: tek seferlik bakim; geri alinacak kayit yok
                r.state = run.outcome == maintenance::MaintOutcome::Purged ? StepState::Applied : StepState::Failed;
                r.detail = run.detail;  // "mj:purged:b=..:a=.." | "mj:failed:purge"
                return r;
            }
            using maintenance::MaintOutcome;
            const bool done = run.outcome == MaintOutcome::Healthy || run.outcome == MaintOutcome::Repairable ||
                              run.outcome == MaintOutcome::NonRepairable || run.outcome == MaintOutcome::Repaired;
            r.state = done ? StepState::Applied : StepState::Failed;
            r.detail = "mj:" + std::string(maintenance::to_string(run.outcome)) + (run.previous_interrupted ? ":prev_interrupted" : "");
            return r;
        }
        case ipc::RequestAction::DriverInstall: {
            drivers::InstallRun run;
            run.outcome = drivers::InstallOutcome::JournalFailed;  // yardimci baglanmadi: hicbir sey baslamaz
            if (d_.driver_install) run = d_.driver_install(op.target);
            using drivers::InstallOutcome;
            switch (run.outcome) {
                case InstallOutcome::Installed:
                case InstallOutcome::InstalledWithErrors: r.state = StepState::Applied; break;
                case InstallOutcome::AlreadyInstalled: r.state = StepState::NothingToDo; break;
                case InstallOutcome::GateBlocked: r.state = StepState::GateBlocked; break;
                default: r.state = drivers::is_rejection(run.outcome) ? StepState::Rejected : StepState::Failed; break;
            }
            r.pending_reboot = r.state == StepState::Applied && run.reboot_required;
            r.detail = drivers::encode_install_detail(run);
            return r;
        }
        case ipc::RequestAction::VendorInstall: {
            drivers::VendorRun run;
            run.outcome = drivers::VendorOutcome::JournalFailed;  // yardimci baglanmadi: hicbir sey calismaz
            if (d_.vendor_install) run = d_.vendor_install(op.target);
            using drivers::VendorOutcome;
            switch (run.outcome) {
                case VendorOutcome::Installed: r.state = StepState::Applied; break;
                case VendorOutcome::SameVersion: r.state = StepState::NothingToDo; break;
                case VendorOutcome::GateBlocked: r.state = StepState::GateBlocked; break;
                default: r.state = drivers::is_rejection(run.outcome) ? StepState::Rejected : StepState::Failed; break;
            }
            r.pending_reboot = r.state == StepState::Applied && run.reboot_required;
            r.detail = drivers::encode_vendor_detail(run);
            return r;
        }
        case ipc::RequestAction::RestorePoint: {
            if (!d_.create_restore_point) {
                r.state = StepState::Failed;
                r.detail = "rp:api_failed";
                return r;
            }
            const auto run = d_.create_restore_point();
            r.state = run.outcome == recovery::RestorePointOutcome::Created ? StepState::Applied
                      : run.outcome == recovery::RestorePointOutcome::SkippedByFrequency ? StepState::NothingToDo : StepState::Failed;
            r.detail = "rp:" + std::string(recovery::to_string(run.outcome));
            return r;
        }
    }
    r.state = StepState::Rejected;
    return r;
}

SessionReconcile AdminExecutor::reconcile_on_start() {
    SessionReconcile out;
    // Yalniz yukseltilmis broker + makine kilidi (SEC-03 ile ayni kosul); aksi halde hicbir sey yazilmaz.
    if (!d_.ctx.elevated || !d_.ctx.machine_lock_held) return out;
    if (d_.permit && !d_.permit().allowed) {
        // Yarim kayit geri alma da yazmadir: oyun acikken yapilmaz, kayitlar korunur.
        out.deferred = reconcile_deferred_ = true;
        return out;
    }
    out.ran = true;
    if (d_.perf_store && d_.perf_engine) out.perf = recovery::reconcile(*d_.perf_store, *d_.perf_engine);
    if (d_.fixes_store && d_.fixes_engine) out.fixes = recovery::reconcile(*d_.fixes_store, *d_.fixes_engine);
    for (const auto* rep : {&out.perf, &out.fixes}) session_restored_.insert(session_restored_.end(), rep->restored.begin(), rep->restored.end());
    return out;
}

bool AdminExecutor::restored_this_session(recovery::JournalStore* store, const SettingOperation& op) {
    if (!store || session_restored_.empty()) return false;
    for (const auto& tx : store->scan().transactions) {
        if (tx.state != TxState::Restored) continue;
        if (std::find(session_restored_.begin(), session_restored_.end(), tx.id) == session_restored_.end()) continue;
        if (std::any_of(tx.steps.begin(), tx.steps.end(), [&](const recovery::JournalStep& s) { return same_target(s.op, op); })) return true;
    }
    return false;
}

StepResult AdminExecutor::recover(const SettingOperation& op) {
    // Uzlastirma oturum basinda yapildi (reconcile_on_start); burada yalniz hedefin son durumu bildirilir.
    StepResult r;
    r.target = op.target;
    bool pending = false, any = false, active = false;
    for (auto* store : {d_.perf_store, d_.fixes_store}) {
        if (!store) {
            pending = true;
            continue;
        }
        for (const auto& tx : store->scan().transactions) {
            const bool match = std::any_of(tx.steps.begin(), tx.steps.end(), [&](const recovery::JournalStep& s) { return same_target(s.op, op); });
            if (!match) continue;
            any = true;
            if (tx.state == TxState::RecoveryRequired || tx_unreconciled(tx.state)) pending = true;
            if (tx.state == TxState::Active) active = true;  // diger journal'daki etkin degisiklik: deger hala uygulanmis
        }
    }
    if (pending) {
        r.state = StepState::RecoveryRequired;
        r.detail = "rollback not verified";
    } else if (active) {
        // Yarim kalmis kayit kapandi ama hedef hala etkin bir degisiklik altinda: "geri yuklendi" denmez.
        r.state = StepState::Applied;
        r.detail = "target still has an active change";
    } else {
        r.state = any ? StepState::Restored : StepState::NothingToDo;
        r.pending_reboot = any && admin_needs_reboot(op);
    }
    return r;
}

StepResult AdminExecutor::keep_current(const SettingOperation& op) {
    // Yalniz makine fixes journal'i (Oyun Hazirligi duzeltmeleri); ayar okunmaz/yazilmaz, kayit kapatilir.
    auto r = keep_matching(d_.fixes_store, [&](const SettingOperation& o) { return same_target(o, op); }, op.target);
    if (r.state == StepState::NothingToDo)  // Performans journal'indaki dis degisiklik de ayni kuralla kapanir
        r = keep_matching(d_.perf_store, [&](const SettingOperation& o) { return same_target(o, op); }, op.target);
    if (r.state == StepState::NothingToDo && restored_this_session(d_.fixes_store, op)) {
        // Oturum basi uzlastirma kaydi zaten geri aldi (deger bu arada onceki degere/yazdigimiz degere donmus).
        r.state = StepState::Restored;
        r.pending_reboot = admin_needs_reboot(op);
        r.detail = "restored by session reconcile";
    }
    return r;
}

ipc::ServerSession::Handler AdminExecutor::handler() {
    return [this](const std::string& rid, ipc::RequestAction a, const SettingOperation& op) { return encode_result(rid, handle(a, op)); };
}

namespace {
// R-E2: Performans journal'inda Etkin ama gercek degeri baska program/surucu tarafindan degistirilmis (bayat) adimlari kapatir.
// Hicbir ayar YAZILMAZ ve geri okunamayan adima dokunulmaz (bilinmeyen != degisti). Adim KeptExternal olur: kullanicinin
// mevcut degeri kalir; ayni hedefe yeni uygulama o mevcut degeri ozgun sayar. Hala bizim yazdigimiz deger bayat degildir.
void retire_stale_active(recovery::JournalStore& store, const recovery::TransactionEngine& engine, const SettingOperation& op) {
    for (auto tx : store.scan().transactions) {
        if (tx.state != TxState::Active) continue;
        bool changed = false;
        for (auto& st : tx.steps) {
            if (!same_target(st.op, op)) continue;
            if (st.state != recovery::StepState::Applied && st.state != recovery::StepState::Verified) continue;
            if (engine.check_step(st, nullptr) != recovery::LiveState::Changed) continue;
            st.state = recovery::StepState::KeptExternal;
            st.error = "no longer applied at apply time; current value kept";
            changed = true;
        }
        if (!changed) continue;
        const bool live = std::any_of(tx.steps.begin(), tx.steps.end(), [](const recovery::JournalStep& s) {
            return s.state == recovery::StepState::Applied || s.state == recovery::StepState::Verified;
        });
        if (!live) tx.state = TxState::Restored;  // etkin adim kalmadi: kayit kapanir
        (void)store.persist(tx);                  // yazilamazsa kayit Active kalir ve yeni yazma Blocked olur (guvenli taraf)
    }
}
}  // namespace

StepResult AdminExecutor::apply_in(recovery::JournalStore* store, recovery::TransactionEngine* engine, const SettingOperation& op) {
    StepResult r;
    r.target = op.target;
    if (!store || !engine || !d_.new_tx_id) {
        r.state = StepState::Failed;
        r.detail = "machine journal unavailable";  // journal yoksa yazma YOK
        if (!d_.journal_error.empty()) r.detail += ": " + d_.journal_error;  // tanilama tahmin edilmez, oldugu gibi
        return r;
    }
    if (store == d_.perf_store) retire_stale_active(*store, *engine, op);  // R-E2: bayat Etkin adim yeni uygulamayi engellemez
    // Ayni hedefte geri alinmamis kayit varsa (Active, RecoveryRequired veya yarim kalmis; iki makine journal'inda da)
    // yeni anlik goruntu zaten degismis degeri "eski" sanir ve ozgun degeri kaybeder: reddedilir (M1).
    for (auto* s : {d_.perf_store, d_.fixes_store}) {
        if (!s) continue;
        const auto listing = s->scan();
        if (!listing.newer.empty()) {
            r.state = StepState::Rejected;
            r.detail = "journal written by a newer version; writes blocked";
            return r;
        }
        for (const auto& tx : listing.transactions)
            if (tx.state != TxState::Restored)
                for (const auto& st : tx.steps)
                    if (same_target(st.op, op)) {
                        r.state = StepState::Blocked;
                        r.detail = tx_unreconciled(tx.state) ? "target has an unreconciled change" : "target already has an active change";
                        return r;
                    }
    }
    recovery::JournalTx tx;
    tx.id = d_.new_tx_id();
    tx.actor_sid = d_.ctx.broker_user_sid;
    tx.target_sid = d_.ctx.target_user_sid;
    tx.created_unix_ms = d_.now_unix_ms ? d_.now_unix_ms() : 0;
    recovery::JournalStep step;
    step.op = op;
    step.op.needs_reboot = admin_needs_reboot(op);
    tx.steps.push_back(std::move(step));
    auto out = engine->apply(std::move(tx));  // baslangic kaydi -> uygula -> geri oku; hata -> geri al
    if (!out.ok()) {
        r.state = StepState::Failed;
        r.detail = out.error().detail;
        return r;
    }
    switch (out.value().state) {
        case TxState::Active:
            r.state = StepState::Applied;
            r.pending_reboot = admin_needs_reboot(op);
            break;
        case TxState::Restored:
            r.state = StepState::Failed;
            r.detail = out.value().steps.empty() ? "apply failed; restored" : "apply failed; restored: " + out.value().steps.front().error;
            break;
        default:
            r.state = StepState::RecoveryRequired;
            r.detail = std::string(contracts::to_string(out.value().state));
            break;
    }
    return r;
}

StepResult restore_matching(recovery::JournalStore* store, recovery::TransactionEngine* engine,
                            const std::function<bool(const SettingOperation&)>& match, std::string_view target) {
    StepResult r;
    r.target = std::string(target);
    if (!store || !engine) {
        r.state = StepState::RecoveryRequired;
        r.detail = "journal unavailable";
        return r;
    }
    std::vector<recovery::JournalTx> todo;
    bool reboot = false;
    for (auto& tx : store->scan().transactions) {
        if (tx.state == TxState::Restored) continue;  // Active, RecoveryRequired ve yarim kalmis: geri al
        const bool hit = std::any_of(tx.steps.begin(), tx.steps.end(), [&](const recovery::JournalStep& s) { return match(s.op); });
        if (!hit) continue;
        for (const auto& st : tx.steps)
            if (match(st.op) && admin_needs_reboot(st.op)) reboot = true;
        todo.push_back(std::move(tx));
    }
    if (todo.empty()) {
        r.state = StepState::NothingToDo;
        return r;
    }
    std::sort(todo.begin(), todo.end(), [](const auto& a, const auto& b) { return a.created_unix_ms > b.created_unix_ms; });
    bool ok = true;
    for (auto& tx : todo) {
        auto out = engine->rollback(std::move(tx));
        if (!out.ok() && out.code() == ErrorCode::GateBlocked) {  // kayit degismedi: "kurtarma gerekli" denmez
            r.state = StepState::GateBlocked;
            r.detail = out.error().detail;
            return r;
        }
        if (!out.ok() || out.value().state != TxState::Restored) {
            ok = false;
            if (!out.ok()) r.detail = out.error().detail;
        }
    }
    r.state = ok ? StepState::Restored : StepState::RecoveryRequired;
    r.pending_reboot = ok && reboot;
    return r;
}

StepResult keep_matching(recovery::JournalStore* store, const std::function<bool(const SettingOperation&)>& match, std::string_view target) {
    StepResult r;
    r.target = std::string(target);
    if (!store) {
        r.state = StepState::Failed;
        r.detail = "journal unavailable";
        return r;
    }
    std::size_t found = 0, closed = 0;
    for (auto& tx : store->scan().transactions) {
        if (!recovery::has_external_conflict(tx)) continue;
        const bool hit = std::any_of(tx.steps.begin(), tx.steps.end(), [&](const recovery::JournalStep& s) {
            return s.state == recovery::StepState::Failed && s.error == recovery::kExternalChange && match(s.op);
        });
        if (!hit) continue;
        ++found;
        auto out = recovery::keep_external_values(std::move(tx), *store, match);
        if (out.ok() && out.value().state == TxState::Restored) ++closed;
        else if (!out.ok()) r.detail = out.error().detail;
    }
    if (found == 0) r.state = StepState::NothingToDo;
    else if (closed == found) r.state = StepState::Kept;
    else {
        r.state = StepState::RecoveryRequired;
        if (r.detail.empty()) r.detail = "other steps still unverified";
    }
    return r;
}

StepResult AdminExecutor::restore_in(recovery::JournalStore* store, recovery::TransactionEngine* engine, const SettingOperation& op) {
    if (!store || !engine) {
        StepResult r;
        r.target = op.target;
        r.state = StepState::RecoveryRequired;
        r.detail = "machine journal unavailable";
        if (!d_.journal_error.empty()) r.detail += ": " + d_.journal_error;
        return r;
    }
    auto r = restore_matching(store, engine, [&](const SettingOperation& o) { return same_target(o, op); }, op.target);
    if (r.state == StepState::NothingToDo && restored_this_session(store, op)) {
        // Kayit (RecoveryRequired / yarim kalmis) bu oturumun basindaki uzlastirmada geri alindi ve dogrulandi.
        r.state = StepState::Restored;
        r.pending_reboot = admin_needs_reboot(op);
        r.detail = "restored by session reconcile";
    }
    return r;
}

// ---- Istemci ----
std::size_t BatchResult::count(StepState s) const {
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [s](const StepResult& r) { return r.state == s; }));
}

bool BatchResult::any_pending_reboot() const {
    return std::any_of(steps.begin(), steps.end(), [](const StepResult& r) { return r.pending_reboot; });
}

ipc::RequestAction request_action_for(ipc::RequestAction batch_action, const SettingOperation& op) noexcept {
    // T08: Performans Apply toplu isteginde geri yukleme noktasi ayri bir eylemdir (broker eylem/islem eslesmesini zorunlu kilar).
    if (batch_action == ipc::RequestAction::Apply && op.id == OperationId::RestorePointCreate) return ipc::RequestAction::RestorePoint;
    return batch_action;
}

BatchResult run_admin_batch(ElevationLauncher& launcher, std::uint32_t self_pid, const std::string& nonce, const Connector& connect,
                            ipc::RequestAction action, const std::vector<SettingOperation>& ops) {
    BatchResult out;
    const bool writes_new = action == ipc::RequestAction::Apply || action == ipc::RequestAction::Fix;
    auto sendable = [writes_new](const SettingOperation& op) { return writes_new ? admin_apply_allowed(op) : admin_operation_allowed(op); };
    std::vector<SettingOperation> allowed;
    for (const auto& op : ops) {
        if (sendable(op)) allowed.push_back(op);
    }
    if (allowed.empty()) {  // gonderilecek izinli is yok: UAC hic istenmez
        out.elevation = ElevationOutcome::Failed;
        out.detail = "no allowed admin operation";
        for (const auto& op : ops) out.steps.push_back({op.target, StepState::Rejected, false, "operation not in admin allowlist"});
        return out;
    }
    const auto er = run_elevated_request(launcher, self_pid, nonce, [&]() -> bool {
        auto t = connect ? connect() : contracts::Result<std::unique_ptr<FrameTransport>>::fail(ErrorCode::Unsupported, "no connector");
        if (!t.ok()) {
            out.detail = "broker connect: " + t.error().detail;
            return false;
        }
        auto& tr = *t.value();
        std::uint64_t seq = 0;
        if (!tr.send(ipc::make_hello(nonce, self_pid, ++seq)).ok()) {
            out.detail = "hello send failed";
            return false;
        }
        auto ack = tr.receive();
        if (!ack.ok() || ack.value().type != ipc::MsgType::HelloAck) {
            out.detail = "handshake rejected";
            return false;
        }
        std::size_t k = 0;
        for (const auto& op : ops) {
            if (!sendable(op)) {
                out.steps.push_back({op.target, StepState::Rejected, false, "operation not in admin allowlist"});
                continue;
            }
            const std::string rid = "r" + std::to_string(++k);
            StepResult sr{op.target, StepState::Failed, false, "no response"};
            sr.unknown = true;  // istek yola cikti: gecerli yanit gelene kadar broker'daki sonuc bilinmiyor
            if (tr.send(ipc::make_request(rid, op, ++seq, request_action_for(action, op))).ok()) {
                auto resp = tr.receive();
                if (resp.ok() && resp.value().type == ipc::MsgType::Response) {
                    if (auto d = decode_result(resp.value().payload); d.ok()) sr = d.value();
                    else sr.detail = "malformed response";
                } else if (resp.ok()) {
                    sr.detail = "broker error: " + resp.value().payload.substr(0, 128);
                    sr.unknown = false;  // oturum istegi reddetti (islenmedi)
                }
            }
            out.steps.push_back(std::move(sr));
        }
        (void)tr.send(ipc::Frame{ipc::MsgType::Bye, ++seq, "{}"});
        (void)tr.receive();
        return true;
    });
    out.elevation = er.outcome;
    if (out.detail.empty()) out.detail = er.detail;
    // Yanit alinamayan izinli islemler acikca basarisiz (sessiz basari yok).
    if (er.outcome == ElevationOutcome::Started && out.steps.size() < ops.size()) {
        for (std::size_t i = out.steps.size(); i < ops.size(); ++i) out.steps.push_back({ops[i].target, StepState::Failed, false, out.detail});
    }
    return out;
}

std::string_view game_profile_notice_key(bool undo, const BatchResult& b) {
    if (b.elevation == ElevationOutcome::Failed) return "chk.notice.admin_start_failed";  // broker hic calismadi; UAC reddi degil
    if (b.elevation != ElevationOutcome::Started) return "games.nv.notice_denied";
    if (std::any_of(b.steps.begin(), b.steps.end(), [](const StepResult& r) { return r.unknown; })) return "chk.notice.unknown";  // yanit kayip: sonuc iddia edilmez
    const std::size_t n = b.steps.size();
    if (undo && n && b.count(StepState::Restored) + b.count(StepState::NothingToDo) == n) return "games.nv.notice_restored";
    if (b.count(StepState::RecoveryRequired)) return "games.nv.notice_review";  // yeniden denemek cozmez
    if (!undo && n && b.count(StepState::Applied) == n) return "games.nv.notice_applied";
    // Biri yazildiysa "yazilmadi" denmez: iki islem ayri kayittir, satirdaki Geri al kalani tamamlar.
    if (b.count(undo ? StepState::Restored : StepState::Applied)) return "games.nv.notice_partial";
    if (b.count(StepState::GateBlocked)) return "chk.notice.gate_blocked";
    return "games.nv.notice_failed";
}

std::string_view maintenance_outcome_key(const BatchResult& b) {
    if (b.elevation != ElevationOutcome::Started || b.steps.empty() || b.steps.front().unknown) return {};
    const auto& d = b.steps.front().detail;
    if (d.rfind("mj:", 0) != 0) return {};
    const auto rest = std::string_view(d).substr(3);
    return rest.substr(0, rest.find(':'));
}

std::string_view maintenance_notice_key(const BatchResult& b) {
    if (b.elevation == ElevationOutcome::Failed) return "chk.notice.admin_start_failed";
    if (b.elevation != ElevationOutcome::Started) return "chk.notice.maint_denied";
    if (b.steps.empty()) return "chk.notice.maint_failed";
    const auto& s = b.steps.front();
    if (s.unknown) return "chk.notice.unknown";
    if (s.state == StepState::GateBlocked) return "chk.notice.gate_blocked";
    if (s.detail.find(":prev_interrupted") != std::string::npos) return "chk.notice.maint_prev_interrupted";  // once bildirilir
    const auto out = maintenance_outcome_key(b);
    const auto has = [&out](std::string_view k) { return out == k; };
    if (has("healthy")) return "chk.notice.maint_healthy";
    if (has("repairable")) return "chk.notice.maint_repairable";
    if (has("non_repairable")) return "chk.notice.maint_non_repairable";
    if (has("repaired")) return "chk.notice.maint_repaired";
    if (has("repair_not_verified")) return "chk.notice.maint_not_verified";
    if (has("source_missing")) return "chk.notice.maint_source_missing";
    if (has("reboot_required")) return "chk.notice.maint_reboot_required";
    if (has("pending_reboot")) return "chk.notice.maint_pending_reboot";
    if (has("unsupported")) return "chk.notice.maint_unsupported";
    if (has("no_restore_point")) return "chk.notice.maint_no_restore_point";
    return "chk.notice.maint_failed";
}

std::string_view restore_point_notice_key(const BatchResult& b) {
    if (b.elevation == ElevationOutcome::Failed) return "chk.notice.admin_start_failed";
    if (b.elevation != ElevationOutcome::Started) return "chk.notice.rp_denied";
    if (b.steps.empty()) return "chk.notice.rp_failed";
    const auto& s = b.steps.front();
    if (s.unknown) return "chk.notice.unknown";
    if (s.state == StepState::GateBlocked) return "chk.notice.gate_blocked";
    if (s.detail == "rp:created") return "chk.notice.rp_created";
    if (s.detail == "rp:skipped_frequency") return "chk.notice.rp_skipped";
    if (s.detail == "rp:protection_off") return "chk.notice.rp_protection_off";
    if (s.detail == "rp:disk_full") return "chk.notice.rp_disk_full";
    if (s.detail == "rp:denied") return "chk.notice.rp_denied";
    if (s.detail == "rp:unverified") return "chk.notice.rp_unverified";
    return "chk.notice.rp_failed";
}

DriverInstallNotice driver_install_notice(const BatchResult& b) {
    DriverInstallNotice n;
    if (b.elevation == ElevationOutcome::Failed) return n.key = "updates.install.start_failed", n;  // UAC reddi degil
    if (b.elevation != ElevationOutcome::Started) return n.key = "updates.install.denied", n;
    if (b.steps.empty() || b.steps.front().unknown) return n.key = "updates.install.unknown", n;  // sonuc iddia edilmez
    const auto& s = b.steps.front();
    const auto run = drivers::decode_install_detail(s.detail);
    if (!run) {
        n.key = s.state == StepState::GateBlocked ? "updates.install.gate" : s.state == StepState::Rejected ? "updates.install.rejected"
                                                                                                              : "updates.install.failed";
        return n;
    }
    using drivers::InstallOutcome;
    n.reboot = run->reboot_required;
    n.previous_interrupted = run->previous_interrupted;
    const bool attempted = run->restore_point != drivers::RestorePointState::NotAttempted;
    n.no_restore_point = attempted && run->restore_point == drivers::RestorePointState::Failed;
    switch (run->outcome) {
        case InstallOutcome::Installed:
            n.key = "updates.install.done", n.installed = true, n.warn = n.no_restore_point;
            break;
        case InstallOutcome::InstalledWithErrors: n.key = "updates.install.done_errors", n.installed = true; break;
        case InstallOutcome::AlreadyInstalled: n.key = "updates.install.already", n.installed = true; break;
        case InstallOutcome::NotFound:
        case InstallOutcome::Ambiguous: n.key = "updates.install.not_found"; break;
        case InstallOutcome::Firmware: n.key = "updates.install.firmware"; break;
        case InstallOutcome::BadTarget:
        case InstallOutcome::NotDriver:
        case InstallOutcome::NotWindowsUpdate:
        case InstallOutcome::Hidden:
        case InstallOutcome::NeedsUserInput: n.key = "updates.install.rejected"; break;
        case InstallOutcome::SearchFailed: n.key = "updates.install.search_failed", n.hresult = run->hresult; break;
        case InstallOutcome::JournalFailed: n.key = "updates.install.journal_failed"; break;
        case InstallOutcome::GateBlocked: n.key = "updates.install.gate"; break;
        case InstallOutcome::Cancelled: n.key = "updates.install.cancelled"; break;
        case InstallOutcome::DownloadFailed: n.key = "updates.install.download_failed", n.hresult = run->hresult; break;
        case InstallOutcome::InstallFailed: n.key = "updates.install.failed", n.hresult = run->hresult; break;
    }
    if (!n.installed) n.reboot = false;
    return n;
}

VendorInstallNotice vendor_install_notice(const BatchResult& b) {
    VendorInstallNotice n;
    if (b.elevation == ElevationOutcome::Failed) return n.key = "updates.install.start_failed", n;  // UAC reddi degil
    if (b.elevation != ElevationOutcome::Started) return n.key = "updates.install.denied", n;
    if (b.steps.empty() || b.steps.front().unknown) return n.key = "updates.install.unknown", n;  // sonuc iddia edilmez
    const auto& s = b.steps.front();
    const auto run = drivers::decode_vendor_detail(s.detail);
    if (!run) {
        n.key = s.state == StepState::GateBlocked ? "updates.install.gate" : s.state == StepState::Rejected ? "updates.local.rejected"
                                                                                                              : "updates.install.failed";
        return n;
    }
    using drivers::VendorOutcome;
    n.previous_interrupted = run->previous_interrupted;
    n.no_restore_point = run->restore_point == drivers::RestorePointState::Failed;
    n.exit_code = run->exit_code;
    n.version = run->version_after;
    switch (run->outcome) {
        case VendorOutcome::Installed:
            n.installed = true, n.reboot = run->reboot_required, n.warn = n.no_restore_point;
            n.key = run->version_after.empty() || run->reboot_required ? "updates.local.done_unread" : "updates.local.done";
            break;
        case VendorOutcome::SameVersion: n.key = "updates.local.same_version", n.installed = true; break;
        case VendorOutcome::NotFound: n.key = "updates.local.not_found"; break;
        case VendorOutcome::TooSmall:
        case VendorOutcome::TooLarge: n.key = "updates.local.size"; break;
        case VendorOutcome::Unsigned:
        case VendorOutcome::BadSignature:
        case VendorOutcome::Untrusted: n.key = "updates.local.bad_signature"; break;
        case VendorOutcome::WrongSigner: n.key = "updates.local.wrong_signer"; break;
        case VendorOutcome::BadTarget: n.key = "updates.local.rejected"; break;
        case VendorOutcome::StageFailed: n.key = "updates.local.stage_failed"; break;
        case VendorOutcome::JournalFailed: n.key = "updates.install.journal_failed"; break;
        case VendorOutcome::GateBlocked: n.key = "updates.install.gate"; break;
        case VendorOutcome::Cancelled:
        case VendorOutcome::InstallerCancelled: n.key = "updates.install.cancelled"; break;
        case VendorOutcome::LaunchFailed: n.key = "updates.local.launch_failed"; break;
        case VendorOutcome::Timeout: n.key = "updates.local.timeout"; break;
        case VendorOutcome::NotAdmin:
        case VendorOutcome::InstallerFailed: n.key = "updates.local.failed"; break;
        case VendorOutcome::NoDevice: n.key = "updates.local.no_device"; break;
        case VendorOutcome::Downgrade: n.key = "updates.local.downgrade"; break;
    }
    return n;
}

std::string_view admin_notice_key(ipc::RequestAction action, const BatchResult& b) {
    using A = ipc::RequestAction;
    const bool started = b.elevation == ElevationOutcome::Started;
    const bool unknown = std::any_of(b.steps.begin(), b.steps.end(), [](const StepResult& r) { return r.unknown; });
    const std::size_t n = b.steps.size();
    // UAC reddi degil, baslatma hatasi (ornek: broker baslatilamadi): broker hic calismadi, hicbir sey yazilmadi.
    if (b.elevation == ElevationOutcome::Failed && action != A::Apply && action != A::Restore) return "chk.notice.admin_start_failed";
    if (started && b.count(StepState::GateBlocked)) return "chk.notice.gate_blocked";  // UAC sonrasi kapi kapandi: yazilmadi
    switch (action) {
        case A::Fix:
            if (!started) return "chk.notice.fix_denied";  // UAC reddi/baslatma hatasi: broker hic calismadi
            if (unknown) return "chk.notice.unknown";
            if (b.count(StepState::RecoveryRequired)) return "chk.notice.fix_recovery";
            if (b.count(StepState::Blocked)) return "chk.notice.fix_blocked";  // hedefte geri alinmamis kayit: yazilmadi
            if (n && b.count(StepState::Applied) == n) return b.any_pending_reboot() ? "chk.notice.fix_reboot" : "";
            return "chk.notice.fix_failed";  // Rejected / Failed (broker geri aldi veya hic yazmadi)
        case A::UndoFix:
            if (!started) return "chk.notice.undo_denied";
            if (unknown) return "chk.notice.unknown";
            if (n && b.count(StepState::Restored) == n) return b.any_pending_reboot() ? "chk.notice.undo_reboot" : "chk.notice.undo_done";
            if (n && b.count(StepState::NothingToDo) == n) return "chk.notice.nothing";
            return "chk.notice.undo_failed";
        case A::Recover:
            if (!started) return "recovery.machine_denied";
            if (unknown) return "chk.notice.unknown";
            if (n && b.count(StepState::Restored) + b.count(StepState::NothingToDo) == n) return "recovery.machine_done";
            if (n && b.count(StepState::Applied) && b.count(StepState::Restored) + b.count(StepState::NothingToDo) + b.count(StepState::Applied) == n)
                return "recovery.machine_active";
            return "recovery.machine_failed";
        case A::KeepCurrent:
            if (!started) return "chk.notice.keep_denied";
            if (unknown) return "chk.notice.unknown";
            if (n && b.count(StepState::Kept) == n) return "chk.notice.keep_done";
            if (n && b.count(StepState::Kept) + b.count(StepState::Restored) == n)
                return b.any_pending_reboot() ? "chk.notice.undo_reboot" : "chk.notice.undo_done";
            if (n && b.count(StepState::NothingToDo) == n) return "chk.notice.keep_nothing";  // RDY-06: Koru'ya ozel metin
            return "chk.notice.keep_failed";
        case A::Apply:
        case A::Restore: break;  // Performans akisi kendi bildirimlerini kullanir (perf.*)
    }
    return "";
}

bool admin_notice_warning(std::string_view key) {
    // RDY-06: yalniz dogrulanmis basari (veya yeniden baslatma bekleyen basari) normal renkte; "yapilacak bir sey
    // bulunamadi" dahil digerleri uyaridir (kullanicinin bekledigi islem olmadi).
    for (std::string_view ok : {"", "chk.notice.fix_reboot", "chk.notice.undo_done", "chk.notice.undo_reboot", "chk.notice.keep_done",
                                "recovery.machine_done"})
        if (key == ok) return false;
    return true;
}

}  // namespace ef::core::broker
