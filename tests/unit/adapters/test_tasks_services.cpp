#include "core/adapters/startup/tasks_services.h"
#include "ef_test.h"

using namespace ef::core::adapters::startup;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
std::string hex(std::string_view s) {
    static constexpr char hx[] = "0123456789abcdef";
    std::string h;
    for (unsigned char c : s) { h += hx[c >> 4]; h += hx[c & 15]; }
    return h;
}
ServiceInfo svc(std::string name, std::string path, bool own = true, bool ms = false) {
    ServiceInfo s;
    s.name = std::move(name);
    s.display_name = s.name;
    s.image_path = std::move(path);
    s.own_process = own;
    s.microsoft = ms;
    s.start = StartType::Automatic;
    s.running = true;
    return s;
}
struct Rig {
    FakeTaskApi tasks;
    FakeServiceApi services;
    TaskAdapter task_adapter{tasks};
    ServiceStartAdapter service_adapter{services};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    int n = 0;
    Rig() {
        reg.add(OperationId::StartupTask, &task_adapter);
        reg.add(OperationId::StartupRunEntry, &service_adapter);
    }
    JournalTx tx(OperationId id, std::string target, const char* v) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcde" + std::to_string(n++ % 10);
        JournalStep s;
        s.op = SettingOperation{id, OperationScope::Machine, std::move(target), v, false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(critical_services_are_excluded) {
    std::string why;
    for (const auto& s : {svc("Audiosrv", "c:\\windows\\system32\\svchost.exe", false, true), svc("vgc", "c:\\program files\\riot vanguard\\vgc.exe"),
                          svc("EasyAntiCheat", "c:\\program files (x86)\\easyanticheat\\easyanticheat.exe"),
                          svc("NVDisplay.ContainerLocalSystem", "c:\\windows\\system32\\driverstore\\filerepository\\nv\\display.nvcontainer\\nvdisplay.container.exe"),
                          svc("BEService", "c:\\program files (x86)\\common files\\battleye\\beservice.exe"),
                          svc("WinDefend", "c:\\programdata\\microsoft\\windows defender\\msmpeng.exe", true, true),
                          svc("Dnscache", "c:\\windows\\system32\\svchost.exe", false, true)}) {
        EF_CHECK(!service_is_eligible(s, why));
    }
    // Bu PC'de gercek sinamada uygun cikan ama korunmasi gerekenler
    for (const auto& s : {svc("GamingServices", "c:\\program files\\windowsapps\\microsoft.gamingservices_33.1\\gamingservices.exe"),
                          svc("XboxService", "c:\\program files\\windowsapps\\microsoft.xbox\\xboxservice.exe"),
                          svc("GameInputRedistService", "c:\\program files\\microsoft gameinput\\x64\\gameinputredistservice.exe"),
                          svc("EpicOnlineServices", "c:\\program files (x86)\\epic games\\epic online services\\eosupdater.exe"),
                          svc("Steam Client Service", "c:\\program files (x86)\\common files\\steam\\steamservice.exe"),
                          svc("EasyTuneEngineService", "c:\\program files (x86)\\gigabyte\\easytuneengineservice\\etes.exe")}) {
        EF_CHECK(!service_is_eligible(s, why));
    }
    auto shared = svc("SomeVendorSvc", "c:\\windows\\system32\\svchost.exe -k x", false);
    EF_CHECK(!service_is_eligible(shared, why));
    auto dep = svc("VendorBase", "c:\\program files\\vendor\\base.exe");
    dep.dependents = {"VendorChild"};
    EF_CHECK(!service_is_eligible(dep, why));
    EF_CHECK(service_is_eligible(svc("AdobeARMservice", "c:\\program files (x86)\\common files\\adobe\\arm\\1.0\\armsvc.exe"), why));
}

EF_TEST(service_start_type_and_state_restored_separately) {
    Rig r;
    r.services.services["AdobeARMservice"] = svc("AdobeARMservice", "c:\\program files\\adobe\\armsvc.exe");
    auto active = r.engine.apply(r.tx(OperationId::StartupRunEntry, "svc:AdobeARMservice:start", "manual"));
    EF_CHECK(active.ok());
    EF_CHECK(r.services.services["AdobeARMservice"].start == StartType::Manual);
    EF_CHECK(r.services.services["AdobeARMservice"].running);  // calisan servis durdurulmadi
    EF_CHECK(r.services.stops == 0);
    r.services.services["AdobeARMservice"].running = false;  // sonradan durdu
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK(r.services.services["AdobeARMservice"].start == StartType::Automatic);
    EF_CHECK(r.services.services["AdobeARMservice"].running && r.services.starts == 1);  // onceden calisiyordu
}

EF_TEST(stopped_service_is_not_started_on_restore) {
    Rig r;
    auto s = svc("VendorUpdater", "c:\\program files\\vendor\\upd.exe");
    s.running = false;
    r.services.services["VendorUpdater"] = s;
    auto active = r.engine.apply(r.tx(OperationId::StartupRunEntry, "svc:VendorUpdater:start", "disabled"));
    EF_CHECK(active.ok());
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.services.starts == 0);
}

EF_TEST(service_api_denial_reported) {
    Rig r;
    r.services.services["VendorUpdater"] = svc("VendorUpdater", "c:\\program files\\vendor\\upd.exe");
    r.services.deny = true;
    auto res = r.engine.apply(r.tx(OperationId::StartupRunEntry, "svc:VendorUpdater:start", "manual"));
    EF_CHECK(!res.ok());
    EF_CHECK(r.services.services["VendorUpdater"].start == StartType::Automatic);
    auto prot = r.engine.apply(r.tx(OperationId::StartupRunEntry, "svc:Audiosrv:start", "disabled"));
    EF_CHECK(!prot.ok());  // liste disi (ve kayitli degil)
    EF_CHECK(!r.engine.apply(r.tx(OperationId::StartupRunEntry, "svc:VendorUpdater:start", "automatic")).ok());
}

EF_TEST(logon_tasks_toggle_enabled_only) {
    Rig r;
    r.tasks.tasks["\\Vendor\\AutoUpdate"] = {"\\Vendor\\AutoUpdate", true, true};
    r.tasks.tasks["\\Microsoft\\Windows\\Defrag\\ScheduledDefrag"] = {"\\Microsoft\\Windows\\Defrag\\ScheduledDefrag", true, true};
    r.tasks.tasks["\\Vendor\\Nightly"] = {"\\Vendor\\Nightly", true, false};
    auto active = r.engine.apply(r.tx(OperationId::StartupTask, "task:" + hex("\\Vendor\\AutoUpdate"), "disabled"));
    EF_CHECK(active.ok() && !r.tasks.tasks["\\Vendor\\AutoUpdate"].enabled);
    EF_CHECK(r.tasks.tasks.size() == 3);  // silinmedi/yaratilmadi
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored && r.tasks.tasks["\\Vendor\\AutoUpdate"].enabled);
    EF_CHECK(!r.engine.apply(r.tx(OperationId::StartupTask, "task:" + hex("\\Microsoft\\Windows\\Defrag\\ScheduledDefrag"), "disabled")).ok());
    EF_CHECK(!r.engine.apply(r.tx(OperationId::StartupTask, "task:" + hex("\\Vendor\\Nightly"), "disabled")).ok());
    r.tasks.deny = true;
    EF_CHECK(!r.engine.apply(r.tx(OperationId::StartupTask, "task:" + hex("\\Vendor\\AutoUpdate"), "disabled")).ok());
}

EF_TEST(protected_tasks_are_excluded) {
    std::string why;
    EF_CHECK(!task_is_eligible({"\\AmdX3DInstaller", true, true}, why));
    EF_CHECK(!task_is_eligible({"\\Agent Activation Runtime\\S-1-5-21-1-2-3-1001", true, true}, why));
    EF_CHECK(!task_is_eligible({"\\NvTmRep_CrashReport1", true, true}, why));
    EF_CHECK(task_is_eligible({"\\Opera scheduled Autoupdate 1791377487", true, true}, why));
}

EF_TEST(packaged_startup_is_unsupported_with_reason) {
    const auto c = packaged_startup_capability();
    EF_CHECK(c.state == SupportState::Unsupported && capability_is_well_formed(c));
}
