// WIN-09 Windows testi (SALT OKUMA; hicbir hizmet durdurulmaz, hicbir girdi degistirilmez):
//   - izin listesindeki hizmetlerin gercek durumu, durdurma uygunlugu ve "is yok" denetimi
//   - yukseltmesiz surecte yetenek Denied olmali
//   - bu PC'nin gercek baslangic girdileri icin tavsiyeler
#include <cstdio>
#include <string>

#include "core/adapters/startup/recommend.h"
#include "core/adapters/windows/background.h"
#include "platform/windows/adapters/win_background.h"
#include "platform/windows/adapters/win_startup.h"
#include "platform/windows/adapters/win_tasks_services.h"
#include "platform/windows/elevation.h"

using namespace ef;
namespace cs = core::adapters::startup;
namespace cw = core::adapters::windows;

int wmain() {
    int rc = 0;
    const bool elevated = platform::is_process_elevated();
    const auto cap = cw::session_stop_capability(elevated);
    std::printf("elevated=%d capability=%s reason=%s\n", elevated ? 1 : 0, contracts::to_string(cap.state).data(), cap.reason.c_str());
    if (!elevated && cap.state != contracts::SupportState::Denied) rc = 2;

    platform::adapters::WinServiceApi scm;
    platform::adapters::WinServiceBusyProbe probe;
    for (const auto& e : cw::session_stoppable_services()) {
        auto s = scm.get(std::string(e.name));
        if (!s.ok()) {
            std::printf("svc %s: not present (%s)\n", e.name.data(), s.error().detail.c_str());
            continue;
        }
        const auto& v = s.value();
        std::string verdict = "already stopped (no-op)";
        if (v.running) {
            auto c = cw::check_session_stop(scm, probe, v);
            verdict = c.ok() ? "stoppable" : "refused: " + c.error().detail;
        }
        std::printf("svc %s | %s | start=%s | ms=%d | deps=%zu | %s\n", e.name.data(), v.running ? "running" : "stopped",
                    cs::to_string(v.start).data(), v.microsoft ? 1 : 0, v.dependents.size(), verdict.c_str());
        if (e.needs_idle_check) {
            auto b = probe.busy(std::string(e.name));
            std::printf("    busy=%s\n", b.ok() ? (b.value() ? "yes" : "no") : b.error().detail.c_str());
        }
    }
    // Kritik hizmetler izin listesinde olmamali
    for (const char* critical : {"Audiosrv", "Dnscache", "WinDefend", "BFE", "mpssvc", "wuauserv", "WSearch", "SysMain", "WerSvc",
                                 "TextInputManagementService", "DiagTrack"})
        if (cw::find_session_stoppable(critical)) { std::printf("CRITICAL IN ALLOW-LIST: %s\n", critical); rc = 3; }

    platform::adapters::WinStartupApi st;
    int counts[3] = {0, 0, 0};
    for (auto l : {cs::Location::UserRun, cs::Location::MachineRun64, cs::Location::MachineRun32, cs::Location::UserFolder, cs::Location::CommonFolder}) {
        auto list = st.list(l);
        if (!list.ok()) { std::printf("%s: error %s\n", cs::to_string(l).data(), list.error().detail.c_str()); continue; }
        for (const auto& x : list.value()) {
            const auto r = cs::recommend(cs::from_entry(x));
            ++counts[static_cast<int>(r.advice)];
            std::printf("startup %s | %s | %s | %s | %s\n", cs::to_string(l).data(), x.name.c_str(),
                        x.approval == cs::Approval::Disabled ? "disabled" : "enabled", cs::to_string(r.advice).data(), cs::to_string(r.rule).data());
            if (r.advice == cs::Advice::RecommendDisable && (r.rule == cs::AdviceRule::Security || r.rule == cs::AdviceRule::WindowsComponent)) rc = 4;
        }
    }
    std::printf("startup recommend_disable=%d keep=%d no_advice=%d\n", counts[0], counts[1], counts[2]);
    return rc;
}
