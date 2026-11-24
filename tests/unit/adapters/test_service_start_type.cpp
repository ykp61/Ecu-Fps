// T12 (ADR-035): ozellik hizmeti baslangic tipi (ServiceStartTypeAdapter). Sahte SCM; gercek hizmet DEGISMEZ.
#include <string>
#include <vector>

#include "core/adapters/startup/tasks_services.h"
#include "core/adapters/windows/service_start_type.h"
#include "ef_test.h"

using namespace ef::core::adapters::startup;
using namespace ef::core::recovery;
using namespace ef::contracts;
namespace cw = ef::core::adapters::windows;

namespace {
ServiceInfo ms_svc(std::string name, std::string path, StartType start = StartType::Automatic, bool running = true) {
    ServiceInfo s;
    s.name = name;
    s.display_name = name;
    s.image_path = std::move(path);
    s.microsoft = true;
    s.start = start;
    s.running = running;
    return s;
}
const std::string kSvc = "c:\\windows\\system32\\svchost.exe -k netsvcs";

struct StartRig {
    FakeServiceApi scm;
    cw::FakeServiceBusyProbe probe;
    cw::ServiceStartTypeAdapter adapter{scm, probe};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    StartRig() {
        reg.add(OperationId::ServiceStartTypeSet, &adapter);
        scm.services["bthserv"] = ms_svc("bthserv", "c:\\windows\\system32\\svchost.exe -k bthsvcs");
        scm.services["Spooler"] = ms_svc("Spooler", "c:\\windows\\system32\\spoolsv.exe");
        scm.services["XblAuthManager"] = ms_svc("XblAuthManager", kSvc);
        scm.services["XblGameSave"] = ms_svc("XblGameSave", kSvc);
        scm.services["XblAuthManager"].dependents = {"XblGameSave"};
    }
    JournalTx tx(std::vector<std::string> services, const char* v = "disabled", OperationScope sc = OperationScope::Machine) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        for (const auto& name : services) {
            JournalStep s;
            s.op = SettingOperation{OperationId::ServiceStartTypeSet, sc, cw::format_service_start_target(name), v, false, false};
            t.steps.push_back(s);
        }
        return t;
    }
};
}  // namespace

EF_TEST(service_start_type_op_id_roundtrip_and_target_format) {
    EF_CHECK(to_string(OperationId::ServiceStartTypeSet) == "service_start_type_set");
    EF_CHECK(parse_operation_id("service_start_type_set") == OperationId::ServiceStartTypeSet);
    EF_CHECK(parse_operation_id("tweak_value_set") == OperationId::TweakValueSet);  // onceki adlar degismedi
    EF_CHECK(cw::parse_service_start_target("svcstart:bthserv").ok() && !cw::parse_service_start_target("svcstop:bthserv").ok());
    EF_CHECK(!cw::parse_service_start_target("svcstart:bthserv start").ok() && !cw::parse_service_start_target("svcstart:").ok());
}

EF_TEST(service_start_type_round_trip_restores_type_and_restarts_running_service) {
    StartRig r;
    auto active = r.engine.apply(r.tx({"bthserv"}));
    EF_CHECK(active.ok() && active.value().state == TxState::Active);
    EF_CHECK(r.scm.services["bthserv"].start == StartType::Disabled && !r.scm.services["bthserv"].running && r.scm.stops == 1);
    EF_CHECK(active.value().steps[0].snapshot.old_value == "automatic" && active.value().steps[0].snapshot.raw == "running");
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.scm.services["bthserv"].start == StartType::Automatic && r.scm.services["bthserv"].running && r.scm.starts == 1);
}

EF_TEST(service_start_type_keeps_delayed_auto_and_does_not_start_stopped_service) {
    StartRig r;
    r.scm.services["bthserv"].start = StartType::AutomaticDelayed;
    auto a = r.engine.apply(r.tx({"bthserv"}));
    EF_CHECK(a.ok() && r.engine.rollback(a.value()).value().state == TxState::Restored);
    EF_CHECK(r.scm.services["bthserv"].start == StartType::AutomaticDelayed);  // gecikmeli otomatik bayragi korunur
    // Onceden durmus Manuel hizmet: Kapali baslatmaz
    StartRig q;
    q.scm.services["bthserv"].start = StartType::Manual;
    q.scm.services["bthserv"].running = false;
    auto b = q.engine.apply(q.tx({"bthserv"}));
    EF_CHECK(b.ok() && q.scm.stops == 0 && q.scm.services["bthserv"].start == StartType::Disabled);
    EF_CHECK(q.engine.rollback(b.value()).value().state == TxState::Restored);
    EF_CHECK(q.scm.services["bthserv"].start == StartType::Manual && !q.scm.services["bthserv"].running && q.scm.starts == 0);
}

EF_TEST(service_start_type_rejects_non_table_services_fakes_and_bad_values) {
    StartRig r;
    r.scm.services["Audiosrv"] = ms_svc("Audiosrv", "c:\\windows\\system32\\svchost.exe -k x");
    r.scm.services["GamingServices"] = ms_svc("GamingServices", "c:\\program files\\windowsapps\\microsoft.gamingservices\\gamingservices.exe");
    EF_CHECK(!r.engine.apply(r.tx({"Audiosrv"})).ok() && !r.engine.apply(r.tx({"GamingServices"})).ok());
    EF_CHECK(r.scm.services["Audiosrv"].start == StartType::Automatic && r.scm.stops == 0);
    r.scm.services["bthserv"].microsoft = false;  // ayni adli sahte ikili
    EF_CHECK(!r.engine.apply(r.tx({"bthserv"})).ok());
    r.scm.services["bthserv"].microsoft = true;
    r.scm.services["bthserv"].image_path = "c:\\temp\\evil.exe";
    EF_CHECK(!r.engine.apply(r.tx({"bthserv"})).ok());
    r.scm.services["bthserv"].image_path = "c:\\windows\\system32\\svchost.exe -k bthsvcs";
    EF_CHECK(!r.engine.apply(r.tx({"bthserv"}, "manual")).ok());                                  // yalniz "disabled"
    EF_CHECK(!r.engine.apply(r.tx({"bthserv"}, "disabled", OperationScope::CurrentUser)).ok());  // yalniz makine kapsami
    EF_CHECK(r.scm.stops == 0 && r.scm.services["bthserv"].start == StartType::Automatic);
    EF_CHECK(r.engine.apply(r.tx({"bthserv"})).ok());  // gecerli hizmet hala uygulanir
}

EF_TEST(service_start_type_running_dependent_blocks_unless_in_same_plan_stopped_first) {
    StartRig r;
    r.scm.services["bthserv"].dependents = {"Fax"};
    r.scm.services["Fax"] = ms_svc("Fax", "c:\\windows\\system32\\fxssvc.exe");  // tabloda yok, calisiyor
    EF_CHECK(!r.engine.apply(r.tx({"bthserv"})).ok() && r.scm.services["bthserv"].start == StartType::Automatic);
    r.scm.services["Fax"].running = false;
    EF_CHECK(r.engine.apply(r.tx({"bthserv"})).ok());
    // Tablodaki bagimli once, ana hizmet sonra (dis sira): ayni islemde basarili, geri alinca ters sirada donulur
    auto ok = r.engine.apply(r.tx({"XblGameSave", "XblAuthManager"}));
    EF_CHECK(ok.ok() && ok.value().state == TxState::Active);
    EF_CHECK(r.scm.services["XblAuthManager"].start == StartType::Disabled && !r.scm.services["XblGameSave"].running);
    auto rb = r.engine.rollback(ok.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.scm.services["XblAuthManager"].start == StartType::Automatic && r.scm.services["XblGameSave"].running);
    // Ters sira (ana hizmet once, bagimli hala calisiyor): uygulamada reddedilir, hicbir sey kalici kalmaz
    StartRig q;
    EF_CHECK(!q.engine.apply(q.tx({"XblAuthManager", "XblGameSave"})).ok());
    EF_CHECK(q.scm.services["XblAuthManager"].start == StartType::Automatic && q.scm.services["XblGameSave"].start == StartType::Automatic);
}

EF_TEST(service_start_type_spooler_pending_jobs_or_unknown_is_refused) {
    StartRig r;
    r.probe.busy_by_service["Spooler"] = true;
    EF_CHECK(!r.engine.apply(r.tx({"Spooler"})).ok());
    r.probe.busy_by_service["Spooler"] = false;
    r.probe.fail = true;  // belirsizlik "mesgul degil" sayilmaz
    EF_CHECK(!r.engine.apply(r.tx({"Spooler"})).ok());
    EF_CHECK(r.scm.services["Spooler"].start == StartType::Automatic && r.scm.services["Spooler"].running);
    r.probe.fail = false;
    EF_CHECK(r.engine.apply(r.tx({"Spooler"})).ok());
    r.probe.calls = 0;
    EF_CHECK(r.engine.apply(r.tx({"bthserv"})).ok() && r.probe.calls == 0);  // is denetimi istemeyen hizmet probe'a sorulmaz
}

EF_TEST(service_start_type_scm_denied_fails_without_state_change) {
    StartRig r;
    r.scm.deny = true;
    EF_CHECK(!r.engine.apply(r.tx({"bthserv"})).ok());
    EF_CHECK(r.scm.services["bthserv"].start == StartType::Automatic && r.scm.services["bthserv"].running);
}
