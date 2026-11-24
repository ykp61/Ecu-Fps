#include "platform/windows/adapters/win_tasks_services.h"

#include <windows.h>
#include <comdef.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <cctype>
#include <memory>

#include "platform/windows/win_env.h"

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Status;
using Microsoft::WRL::ComPtr;
namespace cs = core::adapters::startup;

// ---------------- Gorevler ----------------
namespace {
struct Com {
    bool owned;
    Com() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~Com() { if (owned) CoUninitialize(); }
};

ComPtr<ITaskService> connect() {
    ComPtr<ITaskService> svc;
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc)))) return nullptr;
    if (FAILED(svc->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) return nullptr;
    return svc;
}

bool has_logon_trigger(IRegisteredTask* t) {
    ComPtr<ITaskDefinition> def;
    ComPtr<ITriggerCollection> trig;
    if (FAILED(t->get_Definition(&def)) || FAILED(def->get_Triggers(&trig))) return false;
    LONG n = 0;
    trig->get_Count(&n);
    for (LONG i = 1; i <= n; ++i) {
        ComPtr<ITrigger> tr;
        TASK_TRIGGER_TYPE2 type{};
        if (SUCCEEDED(trig->get_Item(i, &tr)) && SUCCEEDED(tr->get_Type(&type)) && (type == TASK_TRIGGER_LOGON || type == TASK_TRIGGER_BOOT))
            return true;
    }
    return false;
}

cs::TaskInfo info_of(IRegisteredTask* t) {
    cs::TaskInfo x;
    BSTR path = nullptr;
    if (SUCCEEDED(t->get_Path(&path)) && path) {
        x.path = to_utf8(path);
        SysFreeString(path);
    }
    VARIANT_BOOL en = VARIANT_FALSE;
    t->get_Enabled(&en);
    x.enabled = en != VARIANT_FALSE;
    x.logon_trigger = has_logon_trigger(t);
    return x;
}

void walk(ITaskFolder* f, std::vector<cs::TaskInfo>& out, int depth) {
    if (depth > 8 || out.size() > 4000) return;
    ComPtr<IRegisteredTaskCollection> tasks;
    if (SUCCEEDED(f->GetTasks(TASK_ENUM_HIDDEN, &tasks))) {
        LONG n = 0;
        tasks->get_Count(&n);
        for (LONG i = 1; i <= n; ++i) {
            ComPtr<IRegisteredTask> t;
            if (SUCCEEDED(tasks->get_Item(_variant_t(i), &t))) out.push_back(info_of(t.Get()));
        }
    }
    ComPtr<ITaskFolderCollection> subs;
    if (SUCCEEDED(f->GetFolders(0, &subs))) {
        LONG n = 0;
        subs->get_Count(&n);
        for (LONG i = 1; i <= n; ++i) {
            ComPtr<ITaskFolder> sub;
            if (SUCCEEDED(subs->get_Item(_variant_t(i), &sub))) walk(sub.Get(), out, depth + 1);
        }
    }
}

contracts::Result<ComPtr<IRegisteredTask>> open_task(ITaskService* svc, const std::string& path) {
    using R = contracts::Result<ComPtr<IRegisteredTask>>;
    const auto slash = path.find_last_of('\\');
    const std::wstring folder = to_wide(slash == 0 ? "\\" : path.substr(0, slash));
    const std::wstring name = to_wide(path.substr(slash + 1));
    ComPtr<ITaskFolder> f;
    if (FAILED(svc->GetFolder(_bstr_t(folder.c_str()), &f))) return R::fail(ErrorCode::MissingValue, "task folder");
    ComPtr<IRegisteredTask> t;
    const HRESULT hr = f->GetTask(_bstr_t(name.c_str()), &t);
    if (hr == E_ACCESSDENIED) return R::fail(ErrorCode::Denied, "task access denied");
    if (FAILED(hr)) return R::fail(ErrorCode::MissingValue, "task not found");
    return t;
}
}  // namespace

contracts::Result<std::vector<cs::TaskInfo>> WinTaskApi::list() {
    Com com;
    auto svc = connect();
    if (!svc) return contracts::Result<std::vector<cs::TaskInfo>>::fail(ErrorCode::Unsupported, "Task Scheduler unavailable");
    ComPtr<ITaskFolder> root;
    if (FAILED(svc->GetFolder(_bstr_t(L"\\"), &root))) return contracts::Result<std::vector<cs::TaskInfo>>::fail(ErrorCode::IoError, "root folder");
    std::vector<cs::TaskInfo> out;
    walk(root.Get(), out, 0);
    return out;
}

contracts::Result<cs::TaskInfo> WinTaskApi::get(const std::string& path) {
    Com com;
    auto svc = connect();
    if (!svc) return contracts::Result<cs::TaskInfo>::fail(ErrorCode::Unsupported, "Task Scheduler unavailable");
    auto t = open_task(svc.Get(), path);
    if (!t.ok()) return contracts::Result<cs::TaskInfo>(t.error());
    return info_of(t.value().Get());
}

Status WinTaskApi::set_enabled(const std::string& path, bool enabled) {
    Com com;
    auto svc = connect();
    if (!svc) return Status::fail(ErrorCode::Unsupported, "Task Scheduler unavailable");
    auto t = open_task(svc.Get(), path);
    if (!t.ok()) return Status(t.error());
    const HRESULT hr = t.value()->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
    if (hr == E_ACCESSDENIED) return Status::fail(ErrorCode::Denied, "task change requires administrator");
    return SUCCEEDED(hr) ? contracts::ok_status() : Status::fail(ErrorCode::IoError, "put_Enabled");
}

// ---------------- Servisler ----------------
namespace {
struct ScHandle {
    SC_HANDLE h = nullptr;
    ~ScHandle() { if (h) CloseServiceHandle(h); }
};

cs::StartType start_of(DWORD t, bool delayed) {
    switch (t) {
        case SERVICE_BOOT_START: return cs::StartType::Boot;
        case SERVICE_SYSTEM_START: return cs::StartType::System;
        case SERVICE_AUTO_START: return delayed ? cs::StartType::AutomaticDelayed : cs::StartType::Automatic;
        case SERVICE_DISABLED: return cs::StartType::Disabled;
        default: return cs::StartType::Manual;
    }
}

bool fill(SC_HANDLE scm, const std::wstring& name, cs::ServiceInfo& s) {
    ScHandle sv;
    sv.h = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
    if (!sv.h) return false;
    DWORD need = 0;
    QueryServiceConfigW(sv.h, nullptr, 0, &need);
    if (!need) return false;
    auto buf = std::make_unique<unsigned char[]>(need);
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.get());
    if (!QueryServiceConfigW(sv.h, cfg, need, &need)) return false;
    s.name = to_utf8(name);
    s.display_name = cfg->lpDisplayName ? to_utf8(cfg->lpDisplayName) : s.name;
    s.image_path = cfg->lpBinaryPathName ? to_utf8(cfg->lpBinaryPathName) : "";
    s.own_process = (cfg->dwServiceType & SERVICE_WIN32_OWN_PROCESS) != 0 && (cfg->dwServiceType & SERVICE_WIN32_SHARE_PROCESS) == 0;
    s.kernel_driver = (cfg->dwServiceType & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER)) != 0;
    std::string lp = s.image_path;
    for (char& c : lp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Microsoft/Windows bileseni: Windows klasoru altindaki her sey, yolunda "\microsoft" gecenler
    // (Microsoft Visual Studio, Microsoft.NET, WindowsApps\Microsoft.*), Windows Defender, WMP ve WSL.
    s.microsoft = lp.find(":\\windows\\") != std::string::npos || lp.find("%systemroot%") != std::string::npos ||
                  lp.find("\\microsoft") != std::string::npos || lp.find("\\windows defender") != std::string::npos ||
                  lp.find("\\windows media player\\") != std::string::npos || lp.find("\\wsl\\") != std::string::npos;
    SERVICE_DELAYED_AUTO_START_INFO d{};
    DWORD dn = 0;
    const bool delayed = QueryServiceConfig2W(sv.h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, reinterpret_cast<LPBYTE>(&d), sizeof(d), &dn) && d.fDelayedAutostart;
    s.start = start_of(cfg->dwStartType, delayed);
    SERVICE_STATUS st{};
    s.running = QueryServiceStatus(sv.h, &st) && st.dwCurrentState == SERVICE_RUNNING;
    DWORD bytes = 0, count = 0;
    EnumDependentServicesW(sv.h, SERVICE_STATE_ALL, nullptr, 0, &bytes, &count);
    if (bytes) {
        auto db = std::make_unique<unsigned char[]>(bytes);
        if (EnumDependentServicesW(sv.h, SERVICE_STATE_ALL, reinterpret_cast<LPENUM_SERVICE_STATUSW>(db.get()), bytes, &bytes, &count))
            for (DWORD i = 0; i < count; ++i) s.dependents.push_back(to_utf8(reinterpret_cast<LPENUM_SERVICE_STATUSW>(db.get())[i].lpServiceName));
    }
    return true;
}
}  // namespace

contracts::Result<std::vector<cs::ServiceInfo>> WinServiceApi::list() {
    using R = contracts::Result<std::vector<cs::ServiceInfo>>;
    ScHandle scm;
    scm.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!scm.h) return R::fail(ErrorCode::Denied, "OpenSCManager");
    DWORD bytes = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm.h, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0, &bytes, &count, &resume, nullptr);
    auto buf = std::make_unique<unsigned char[]>(bytes);
    resume = 0;
    if (!EnumServicesStatusExW(scm.h, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf.get(), bytes, &bytes, &count, &resume, nullptr))
        return R::fail(ErrorCode::IoError, "EnumServicesStatusEx");
    std::vector<cs::ServiceInfo> out;
    const auto* e = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buf.get());
    for (DWORD i = 0; i < count; ++i) {
        cs::ServiceInfo s;
        if (fill(scm.h, e[i].lpServiceName, s)) out.push_back(std::move(s));
    }
    return out;
}

contracts::Result<cs::ServiceInfo> WinServiceApi::get(const std::string& name) {
    ScHandle scm;
    scm.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm.h) return contracts::Result<cs::ServiceInfo>::fail(ErrorCode::Denied, "OpenSCManager");
    cs::ServiceInfo s;
    if (!fill(scm.h, to_wide(name), s)) return contracts::Result<cs::ServiceInfo>::fail(ErrorCode::MissingValue, "service not found");
    return s;
}

namespace {
Status with_service(const std::string& name, DWORD access, auto fn) {
    ScHandle scm, sv;
    scm.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm.h) return Status::fail(ErrorCode::Denied, "OpenSCManager");
    sv.h = OpenServiceW(scm.h, to_wide(name).c_str(), access);
    if (!sv.h) {
        const DWORD e = GetLastError();
        return Status::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::MissingValue, "OpenService (" + std::to_string(e) + ")");
    }
    return fn(sv.h);
}
}  // namespace

Status WinServiceApi::set_start_type(const std::string& name, cs::StartType t) {
    return with_service(name, SERVICE_CHANGE_CONFIG, [t](SC_HANDLE h) {
        DWORD type = SERVICE_DEMAND_START;
        if (t == cs::StartType::Automatic || t == cs::StartType::AutomaticDelayed) type = SERVICE_AUTO_START;
        else if (t == cs::StartType::Disabled) type = SERVICE_DISABLED;
        else if (t != cs::StartType::Manual) return Status::fail(ErrorCode::Denied, "boot/system start not settable");
        if (!ChangeServiceConfigW(h, SERVICE_NO_CHANGE, type, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            return Status::fail(ErrorCode::Denied, "ChangeServiceConfig");
        if (type == SERVICE_AUTO_START) {
            SERVICE_DELAYED_AUTO_START_INFO d{t == cs::StartType::AutomaticDelayed};
            ChangeServiceConfig2W(h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &d);
        }
        return contracts::ok_status();
    });
}

namespace {
// WIN-09: StartService/ControlService eszamansizdir; geri okuma *_PENDING gormesin diye hedef duruma
// kadar sinirli sure beklenir (en fazla 20 sn). Zaman asiminda hata doner, durum uydurulmaz.
Status wait_state(SC_HANDLE h, DWORD target, const char* what) {
    for (int i = 0; i < 200; ++i) {
        SERVICE_STATUS st{};
        if (!QueryServiceStatus(h, &st)) return Status::fail(ErrorCode::IoError, std::string("QueryServiceStatus ") + what);
        if (st.dwCurrentState == target) return contracts::ok_status();
        if (target == SERVICE_RUNNING && st.dwCurrentState == SERVICE_STOPPED && i > 0)
            return Status::fail(ErrorCode::IoError, std::string("service stopped while starting ") + what);
        Sleep(100);
    }
    return Status::fail(ErrorCode::IoError, std::string("timeout waiting for ") + what);
}
}  // namespace

Status WinServiceApi::start(const std::string& name) {
    return with_service(name, SERVICE_START | SERVICE_QUERY_STATUS, [](SC_HANDLE h) {
        if (!StartServiceW(h, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return Status::fail(ErrorCode::IoError, "StartService");
        return wait_state(h, SERVICE_RUNNING, "start");
    });
}

Status WinServiceApi::stop(const std::string& name) {
    return with_service(name, SERVICE_STOP | SERVICE_QUERY_STATUS, [](SC_HANDLE h) {
        SERVICE_STATUS st{};
        if (!ControlService(h, SERVICE_CONTROL_STOP, &st) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
            return Status::fail(ErrorCode::IoError, "ControlService stop");
        return wait_state(h, SERVICE_STOPPED, "stop");
    });
}

}  // namespace ef::platform::adapters
