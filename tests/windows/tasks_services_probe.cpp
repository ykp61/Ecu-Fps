// WIN-04 Windows testi (salt okuma): oturum acma gorevleri ve servislerin uygunluk siniflandirmasi.
// Kritik servislerin (Audiosrv, Dnscache, WinDefend, BFE...) gercek sistemde uygunsuz cikmasini ve
// yukseltmesiz servis yazimin Denied raporlanmasini dogrular.
#include <cstdio>
#include <string>

#include "core/adapters/startup/tasks_services.h"
#include "platform/windows/adapters/win_tasks_services.h"
#include "platform/windows/elevation.h"

using namespace ef;
namespace cs = core::adapters::startup;

int wmain() {
    int rc = 0;
    platform::adapters::WinTaskApi tasks;
    auto tl = tasks.list();
    if (!tl.ok()) { std::printf("tasks: %s\n", tl.error().detail.c_str()); return 2; }
    int logon = 0, eligible = 0;
    for (const auto& t : tl.value()) {
        if (!t.logon_trigger) continue;
        ++logon;
        std::string why;
        if (cs::task_is_eligible(t, why)) {
            ++eligible;
            std::printf("task eligible: %s (%s)\n", t.path.c_str(), t.enabled ? "enabled" : "disabled");
        }
    }
    std::printf("tasks total=%zu logon=%d eligible=%d\n", tl.value().size(), logon, eligible);

    platform::adapters::WinServiceApi svc;
    auto sl = svc.list();
    if (!sl.ok()) { std::printf("services: %s\n", sl.error().detail.c_str()); return 3; }
    int ok = 0;
    for (const auto& s : sl.value()) {
        std::string why;
        if (cs::service_is_eligible(s, why)) {
            ++ok;
            std::printf("service eligible: %s [%s, %s]\n", s.name.c_str(), cs::to_string(s.start).data(), s.running ? "running" : "stopped");
        }
    }
    std::printf("services total=%zu eligible=%d\n", sl.value().size(), ok);
    for (const char* critical : {"Audiosrv", "Dnscache", "WinDefend", "BFE", "Dhcp", "EventLog"}) {
        auto s = svc.get(critical);
        std::string why;
        if (s.ok() && cs::service_is_eligible(s.value(), why)) { std::printf("CRITICAL ELIGIBLE: %s\n", critical); rc = 4; }
    }
    if (!platform::is_process_elevated()) {
        // Yazma reddi dogru raporlanmali (degisiklik yapilmaz: ayni degeri yazmayi dener).
        for (const auto& s : sl.value()) {
            std::string why;
            if (!cs::service_is_eligible(s, why)) continue;
            auto r = svc.set_start_type(s.name, s.start);
            std::printf("unelevated write %s -> %s\n", s.name.c_str(), r.ok() ? "ok?!" : r.error().detail.c_str());
            if (r.ok() || r.code() != contracts::ErrorCode::Denied) rc = 5;
            break;
        }
    }
    return rc;
}
