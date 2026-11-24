#include "platform/windows/inventory/update_status.h"

#include <windows.h>
#include <bits.h>
#include <wuapi.h>
#include <wrl/client.h>

#include "platform/windows/inventory/field_json.h"
#include "platform/windows/inventory/os_user.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;
using Microsoft::WRL::ComPtr;

namespace {
struct Com {
    bool owned;
    Com() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~Com() { if (owned) CoUninitialize(); }
};
}  // namespace

core::adapters::windows::UpdateStatus read_update_status() {
    Com com;
    core::adapters::windows::UpdateStatus s;
    // Bekleyen yeniden baslatma (WUA belgeli)
    ComPtr<ISystemInformation> si;
    if (SUCCEEDED(CoCreateInstance(CLSID_SystemInformation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&si)))) {
        VARIANT_BOOL r = VARIANT_FALSE;
        if (SUCCEEDED(si->get_RebootRequired(&r))) s.reboot_pending = Field<bool>::known(r != VARIANT_FALSE, Provenance::Api);
        else s.reboot_pending = Field<bool>::unknown("RebootRequired query failed");
    } else {
        s.reboot_pending = Field<bool>::unknown("Windows Update Agent unavailable");
    }
    // Hizmet durumu (yalniz sorgu)
    if (SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)) {
        if (SC_HANDLE sv = OpenServiceW(scm, L"wuauserv", SERVICE_QUERY_STATUS)) {
            SERVICE_STATUS st{};
            if (QueryServiceStatus(sv, &st)) s.update_service_running = Field<bool>::known(st.dwCurrentState == SERVICE_RUNNING, Provenance::Api);
            CloseServiceHandle(sv);
        }
        CloseServiceHandle(scm);
    }
    if (!s.update_service_running.has()) s.update_service_running = Field<bool>::unknown("service query failed");
    // BITS: bu kullanicinin gorebildigi aktif aktarim isleri (tum kullanicilar yonetici ister)
    ComPtr<IBackgroundCopyManager> bits;
    if (SUCCEEDED(CoCreateInstance(__uuidof(BackgroundCopyManager), nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&bits)))) {
        ComPtr<IEnumBackgroundCopyJobs> jobs;
        if (SUCCEEDED(bits->EnumJobs(0, &jobs))) {
            int active = 0;
            ComPtr<IBackgroundCopyJob> j;
            ULONG got = 0;
            while (jobs->Next(1, &j, &got) == S_OK && got == 1) {
                BG_JOB_STATE st{};
                if (SUCCEEDED(j->GetState(&st)) && (st == BG_JOB_STATE_TRANSFERRING || st == BG_JOB_STATE_CONNECTING)) ++active;
                j.Reset();
            }
            s.active_downloads = Field<int>::known(active, Provenance::Api);
        }
    }
    if (!s.active_downloads.has()) s.active_downloads = Field<int>::unknown("BITS query unavailable");
    // Politika (INV-01)
    for (const auto& p : read_policy().policies)
        if (p.id == "windows_update")
            s.managed_by_policy = p.state == contracts::PolicyState::Unknown ? Field<bool>::unknown(p.reason)
                                                                             : Field<bool>::known(p.state == contracts::PolicyState::Present, Provenance::Registry);
    return s;
}

core::json::Value update_status_json() {
    const auto s = read_update_status();
    auto o = core::json::Value::object();
    o.set("reboot_pending", field_json(s.reboot_pending));
    o.set("update_service_running", field_json(s.update_service_running));
    o.set("active_downloads", field_json(s.active_downloads));
    o.set("managed_by_policy", field_json(s.managed_by_policy));
    const auto adv = core::adapters::windows::advise_for_game_session(s);
    auto r = core::json::Value::array();
    for (auto x : adv.risks) r.push(core::json::Value::string(std::string(core::adapters::windows::text_key(x))));
    o.set("session_risks", std::move(r));
    return o;
}

}  // namespace ef::platform::inventory
