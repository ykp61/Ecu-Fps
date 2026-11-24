// WIN-09: oturumluk hizmet durdurma (sahte SCM) — gercek hizmet DURDURULMAZ.
#include "core/adapters/windows/background.h"
#include "core/capabilities/registry.h"
#include "ef_test.h"

using namespace ef::core::adapters::windows;
using namespace ef::core::adapters::startup;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
ServiceInfo win_svc(std::string name, std::string path, bool running = true) {
    ServiceInfo s;
    s.name = std::move(name);
    s.display_name = s.name;
    s.image_path = std::move(path);
    s.own_process = true;
    s.microsoft = true;
    s.start = StartType::Automatic;
    s.running = running;
    return s;
}
struct Rig {
    FakeServiceApi scm;
    FakeServiceBusyProbe probe;
    ServiceSessionStopAdapter adapter{scm, probe};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    Rig() {
        reg.add(OperationId::ServiceSessionStop, &adapter);
        scm.services["Spooler"] = win_svc("Spooler", "c:\\windows\\system32\\spoolsv.exe");
        scm.services["Spooler"].dependents = {"Fax"};
        scm.services["Fax"] = win_svc("Fax", "c:\\windows\\system32\\fxssvc.exe", false);
        scm.services["stisvc"] = win_svc("stisvc", "c:\\windows\\system32\\svchost.exe -k imgsvc");
        scm.services["MapsBroker"] = win_svc("MapsBroker", "c:\\windows\\system32\\svchost.exe -k networkservice -p", false);
    }
    JournalTx tx(std::string service, const char* v = "stopped", OperationScope sc = OperationScope::Machine) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = SettingOperation{OperationId::ServiceSessionStop, sc, format_session_stop_target(service), v, false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(operation_id_service_session_stop_roundtrip) {
    EF_CHECK(to_string(OperationId::ServiceSessionStop) == "service_session_stop");
    EF_CHECK(parse_operation_id("service_session_stop") == OperationId::ServiceSessionStop);
    EF_CHECK(parse_operation_id("game_config_field") == OperationId::GameConfigField);  // mevcut adlar degismedi
}

EF_TEST(allow_list_is_strict_and_excludes_critical) {
    for (auto n : {"Spooler", "spooler", "Fax", "MapsBroker", "PcaSvc", "stisvc"}) EF_CHECK(find_session_stoppable(n) != nullptr);
    // Resmi belgede "Do not disable"/"No guidance"/belirsiz olanlar ve kritik roller liste disi.
    for (auto n : {"WSearch", "SysMain", "DiagTrack", "WerSvc", "TabletInputService", "TextInputManagementService", "Audiosrv", "Dnscache",
                   "WinDefend", "mpssvc", "BFE", "wuauserv", "UsoSvc", "BITS", "vgc", "EasyAntiCheat", "BEService", "NVDisplay.ContainerLocalSystem",
                   "AudioEndpointBuilder", "Dhcp", "WlanSvc", "XboxGipSvc", "GamingServices"})
        EF_CHECK(find_session_stoppable(n) == nullptr);
    EF_CHECK(session_stoppable_services().size() == 5);
    for (const auto& e : session_stoppable_services()) EF_CHECK(!e.source.empty() && !e.breaks_key.empty() && !e.image_part.empty());
}

EF_TEST(session_stop_then_rollback_restarts_service) {
    Rig r;
    auto active = r.engine.apply(r.tx("stisvc"));
    EF_CHECK(active.ok());
    EF_CHECK(active.value().state == TxState::Active);
    EF_CHECK(!r.scm.services["stisvc"].running && r.scm.stops == 1);
    EF_CHECK(r.scm.services["stisvc"].start == StartType::Automatic);  // baslangic tipi degismedi
    EF_CHECK(active.value().steps[0].snapshot.old_value == "running");
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.scm.services["stisvc"].running && r.scm.starts == 1);
}

EF_TEST(already_stopped_service_is_noop) {
    Rig r;
    auto active = r.engine.apply(r.tx("MapsBroker"));
    EF_CHECK(active.ok());
    EF_CHECK(r.scm.stops == 0);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.scm.starts == 0);  // onceden durmus: baslatilmaz
}

EF_TEST(running_dependent_blocks_stop) {
    Rig r;
    r.scm.services["Fax"].running = true;
    auto res = r.engine.apply(r.tx("Spooler"));
    EF_CHECK(!res.ok() && res.code() == ErrorCode::Denied);
    EF_CHECK(r.scm.services["Spooler"].running && r.scm.stops == 0);
    r.scm.services["Fax"].running = false;  // bagimli durmus: durdurulabilir
    auto ok = r.engine.apply(r.tx("Spooler"));
    EF_CHECK(ok.ok() && !r.scm.services["Spooler"].running);
    EF_CHECK(r.engine.rollback(ok.value()).value().state == TxState::Restored);
    EF_CHECK(r.scm.services["Spooler"].running);
}

EF_TEST(spooler_with_pending_jobs_or_unknown_state_is_refused) {
    Rig r;
    r.probe.busy_by_service["Spooler"] = true;
    EF_CHECK(!r.engine.apply(r.tx("Spooler")).ok());
    r.probe.busy_by_service["Spooler"] = false;
    r.probe.fail = true;  // belirsizlik mesgul degil sayilmaz
    EF_CHECK(!r.engine.apply(r.tx("Spooler")).ok());
    EF_CHECK(r.scm.services["Spooler"].running && r.scm.stops == 0);
    // Is denetimi istemeyen hizmet probe'a sorulmaz
    r.probe.calls = 0;
    EF_CHECK(r.engine.apply(r.tx("stisvc")).ok());
    EF_CHECK(r.probe.calls == 0);
}

EF_TEST(session_stop_rejects_unlisted_fake_or_bad_values) {
    Rig r;
    r.scm.services["Audiosrv"] = win_svc("Audiosrv", "c:\\windows\\system32\\svchost.exe -k localservicenetworkrestricted");
    EF_CHECK(!r.engine.apply(r.tx("Audiosrv")).ok());
    // Ayni adli ama farkli ikili / Microsoft disi hizmet reddedilir
    r.scm.services["PcaSvc"] = win_svc("PcaSvc", "c:\\program files\\evil\\pca.exe");
    r.scm.services["PcaSvc"].microsoft = false;
    EF_CHECK(!r.engine.apply(r.tx("PcaSvc")).ok());
    r.scm.services["PcaSvc"] = win_svc("PcaSvc", "c:\\windows\\system32\\evil.exe");
    EF_CHECK(!r.engine.apply(r.tx("PcaSvc")).ok());
    // Yalniz "stopped" uygulanabilir; makine kapsami zorunlu; hedef bicimi dogrulanir
    EF_CHECK(!r.engine.apply(r.tx("stisvc", "running")).ok());
    EF_CHECK(!r.engine.apply(r.tx("stisvc", "stopped", OperationScope::CurrentUser)).ok());
    EF_CHECK(!parse_session_stop_target("svc:stisvc:start").ok());
    EF_CHECK(!parse_session_stop_target("svcstop:a b").ok());
    EF_CHECK(!parse_session_stop_target("svcstop:").ok());
    EF_CHECK(parse_session_stop_target("svcstop:stisvc").value() == "stisvc");
    EF_CHECK(r.scm.stops == 0);
}

EF_TEST(scm_denial_without_elevation_rolls_back_cleanly) {
    Rig r;
    r.scm.deny = true;  // yukseltmesiz SCM: SERVICE_STOP erisimi reddedilir
    auto res = r.engine.apply(r.tx("stisvc"));
    EF_CHECK(!res.ok());
    EF_CHECK(r.scm.services["stisvc"].running && r.scm.stops == 0);
    const auto cap = session_stop_capability(false);
    EF_CHECK(cap.state == SupportState::Denied && capability_is_well_formed(cap) && cap.required == Privilege::Admin);
    const auto cap2 = session_stop_capability(true);
    EF_CHECK(cap2.state == SupportState::Supported && capability_is_well_formed(cap2));
    EF_CHECK(session_stop_needs_admin());
}

EF_TEST(external_restart_during_session_is_not_overwritten) {
    Rig r;
    auto active = r.engine.apply(r.tx("stisvc"));
    EF_CHECK(active.ok());
    r.scm.services["stisvc"].running = true;  // kullanici/tetikleyici yeniden baslatti = anlik goruntuyle ayni
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.scm.starts == 0);
}

EF_TEST(failed_restart_marks_recovery_required) {
    Rig r;
    auto active = r.engine.apply(r.tx("stisvc"));
    EF_CHECK(active.ok());
    r.scm.services.erase("stisvc");  // hizmet okunamiyor: dogrulanamayan geri alma
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::RecoveryRequired);
}

EF_TEST(session_stop_capability_is_recordable_in_catalog) {
    ef::core::capabilities::Registry reg(ef::core::capabilities::embedded_features());
    EF_CHECK(reg.feature("B07") != nullptr);
    EF_CHECK(reg.record(session_stop_capability(false), "env1").ok());
    EF_CHECK(reg.query("B07", "system", "env1").state == SupportState::Denied);
}
