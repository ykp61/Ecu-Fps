#include "platform/windows/games/protection_services.h"

#include <windows.h>

#include <string>

namespace ef::platform::games {

namespace {

struct Probe {
    bool installed = false;
    bool running = false;
};

Probe probe(SC_HANDLE scm, const wchar_t* name) {
    Probe p;
    SC_HANDLE sv = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
    if (!sv) return p;
    p.installed = true;
    SERVICE_STATUS st{};
    if (QueryServiceStatus(sv, &st) && st.dwCurrentState == SERVICE_RUNNING) p.running = true;
    CloseServiceHandle(sv);
    return p;
}

}  // namespace

core::games::ProtectionEvidence scan_protection_services() {
    core::games::ProtectionEvidence e;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return e;  // query_ok=false: "kurulu değil" denmez
    e.query_ok = true;
    // Yaygın servis adları: EAC "EasyAntiCheat", BattlEye "BEService", Vanguard "vgc".
    const auto eac = probe(scm, L"EasyAntiCheat");
    const auto be = probe(scm, L"BEService");
    const auto vg = probe(scm, L"vgc");
    e.eac_installed = eac.installed;
    e.eac_running = eac.running;
    e.be_installed = be.installed;
    e.be_running = be.running;
    e.vanguard_installed = vg.installed;
    e.vanguard_running = vg.running;
    // T09: FACEIT servis adi sürümler arasinda degisebilir; bilinen adlar sorgulanir. Bulunamazsa "kurulu degil" demektir
    // (gercek cihazda dogrulanmadi: DOGRULANMADI). Plan ayrica FACEIT istemci klasorunu aramaz.
    const auto fc1 = probe(scm, L"FACEIT");
    const auto fc2 = probe(scm, L"FACEITService");
    e.faceit_installed = fc1.installed || fc2.installed;
    CloseServiceHandle(scm);
    return e;
}

}  // namespace ef::platform::games
