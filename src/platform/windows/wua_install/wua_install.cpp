#include "platform/windows/wua_install/wua_install.h"

#include <windows.h>
#include <oleauto.h>
#include <wuapi.h>
#include <wrl/client.h>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace ef::platform::wua {

using Microsoft::WRL::ComPtr;
namespace cd = core::drivers;

namespace {

std::string utf8(BSTR b) {
    if (!b) return {};
    const int len = static_cast<int>(SysStringLen(b));
    if (len == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, b, len, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, b, len, s.data(), n, nullptr, nullptr);
    return s;
}

template <class Getter>
std::string bstr_field(Getter get) {
    BSTR b = nullptr;
    std::string s;
    if (SUCCEEDED(get(&b))) s = utf8(b);
    if (b) SysFreeString(b);
    return s;
}

bool flag(HRESULT hr, VARIANT_BOOL v) { return SUCCEEDED(hr) && v == VARIANT_TRUE; }

// Arama / indirme tamamlanma geri cagrisi: yalniz olayi isaretler (WUA kendi is parcaciginda cagirir). Ilerleme
// geri cagrisi bos gecer.
class Done final : public ISearchCompletedCallback, public IDownloadCompletedCallback, public IDownloadProgressChangedCallback {
public:
    Done() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Done() {
        if (event_) CloseHandle(event_);
    }
    HANDLE event() const { return event_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ISearchCompletedCallback)) *out = static_cast<ISearchCompletedCallback*>(this);
        else if (riid == __uuidof(IDownloadCompletedCallback)) *out = static_cast<IDownloadCompletedCallback*>(this);
        else if (riid == __uuidof(IDownloadProgressChangedCallback)) *out = static_cast<IDownloadProgressChangedCallback*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const long n = --refs_;
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE Invoke(ISearchJob*, ISearchCompletedCallbackArgs*) override { return signal(); }
    HRESULT STDMETHODCALLTYPE Invoke(IDownloadJob*, IDownloadCompletedCallbackArgs*) override { return signal(); }
    HRESULT STDMETHODCALLTYPE Invoke(IDownloadJob*, IDownloadProgressChangedCallbackArgs*) override { return S_OK; }

private:
    HRESULT signal() {
        if (event_) SetEvent(event_);
        return S_OK;
    }
    std::atomic<long> refs_{1};
    HANDLE event_;
};

// Olay gelene kadar bekler; stop() true olursa abort() cagrilir ve kisa sure daha beklenir. true: tamamlandi.
template <class Abort>
bool wait_done(Done* done, const std::function<bool()>& stop, Abort abort, bool& aborted) {
    for (;;) {
        if (WaitForSingleObject(done->event(), 100) == WAIT_OBJECT_0) return true;
        if (stop && stop()) {
            abort();
            aborted = true;
            return WaitForSingleObject(done->event(), 10000) == WAIT_OBJECT_0;
        }
    }
}

struct Com {
    bool owned;
    Com() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~Com() {
        if (owned) CoUninitialize();
    }
};

cd::InstallCandidate read_candidate(IUpdate* u, bool windows_update) {
    cd::InstallCandidate c;
    c.from_windows_update = windows_update;
    auto& d = c.update;
    d.title = bstr_field([&](BSTR* b) { return u->get_Title(b); });
    ComPtr<IUpdateIdentity> id;
    if (SUCCEEDED(u->get_Identity(&id)) && id) {
        d.update_id = bstr_field([&](BSTR* b) { return id->get_UpdateID(b); });
        LONG rev = 0;
        if (SUCCEEDED(id->get_RevisionNumber(&rev))) d.revision = static_cast<int>(rev);
    }
    UpdateType type = utSoftware;
    c.type_driver = SUCCEEDED(u->get_Type(&type)) && type == utDriver;
    VARIANT_BOOL v = VARIANT_FALSE;
    HRESULT hr = u->get_IsInstalled(&v);
    c.installed = flag(hr, v);
    v = VARIANT_FALSE;
    hr = u->get_IsHidden(&v);
    c.hidden = flag(hr, v);
    v = VARIANT_FALSE;
    hr = u->get_EulaAccepted(&v);
    c.eula_accepted = flag(hr, v);  // okunamazsa kabul edilmemis sayilir
    ComPtr<IInstallationBehavior> beh;
    if (SUCCEEDED(u->get_InstallationBehavior(&beh)) && beh) {
        v = VARIANT_TRUE;
        hr = beh->get_CanRequestUserInput(&v);
        c.needs_user_input = FAILED(hr) || v == VARIANT_TRUE;
    } else {
        c.needs_user_input = true;  // davranis okunamadi: kabul edilmez
    }
    ComPtr<IWindowsDriverUpdate> drv;
    if (SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&drv))) && drv) {
        d.driver_class = bstr_field([&](BSTR* b) { return drv->get_DriverClass(b); });
        d.hardware_id = bstr_field([&](BSTR* b) { return drv->get_DriverHardwareID(b); });
        d.provider = bstr_field([&](BSTR* b) { return drv->get_DriverProvider(b); });
    } else {
        c.type_driver = false;
    }
    return c;
}

class WuaDriverInstall final : public cd::DriverInstallApi {
public:
    explicit WuaDriverInstall(std::function<cd::RestorePointState()> rp) : rp_(std::move(rp)) {}

    cd::FindOutcome find(const cd::InstallRef& ref, const std::function<bool()>& stop) override {
        cd::FindOutcome out;
        found_.clear();
        chosen_.Reset();
        HRESULT hr = CoCreateInstance(__uuidof(UpdateSession), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&session_));
        if (FAILED(hr)) return out.hresult = static_cast<std::int32_t>(hr), out;
        BSTR app = SysAllocString(L"Ecu Fps");
        (void)session_->put_ClientApplicationID(app);
        SysFreeString(app);
        ComPtr<IUpdateSearcher> searcher;
        hr = session_->CreateUpdateSearcher(&searcher);
        if (FAILED(hr)) return out.hresult = static_cast<std::int32_t>(hr), out;
        // Kaynak yalniz Windows Update (WSUS / ilke ile atanmis baska hizmet degil); geri okunur.
        hr = searcher->put_ServerSelection(ssWindowsUpdate);
        if (FAILED(hr)) return out.hresult = static_cast<std::int32_t>(hr), out;
        ServerSelection sel = ssDefault;
        const bool windows_update = SUCCEEDED(searcher->get_ServerSelection(&sel)) && sel == ssWindowsUpdate;
        (void)searcher->put_Online(VARIANT_TRUE);
        auto* done = new Done();
        if (!done->event()) {
            done->Release();
            return out.hresult = static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())), out;
        }
        const std::wstring criteria = cd::install_criteria(ref);
        BSTR crit = SysAllocString(criteria.c_str());
        VARIANT state;
        VariantInit(&state);
        ComPtr<ISearchJob> job;
        hr = searcher->BeginSearch(crit, static_cast<ISearchCompletedCallback*>(done), state, &job);
        SysFreeString(crit);
        if (FAILED(hr) || !job) {
            done->Release();
            return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        }
        bool aborted = false;
        const bool completed = wait_done(done, stop, [&] { (void)job->RequestAbort(); }, aborted);
        ComPtr<ISearchResult> result;
        hr = completed && !aborted ? searcher->EndSearch(job.Get(), &result) : E_ABORT;
        (void)job->CleanUp();
        done->Release();
        if (aborted) return out.cancelled = true, out;
        if (FAILED(hr) || !result) return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        OperationResultCode code = orcNotStarted;
        (void)result->get_ResultCode(&code);
        if (code != orcSucceeded) return out.hresult = static_cast<std::int32_t>(E_FAIL), out;  // yarim liste kabul edilmez
        ComPtr<IUpdateCollection> list;
        LONG n = 0;
        if (FAILED(result->get_Updates(&list)) || !list || FAILED(list->get_Count(&n))) return out.hresult = static_cast<std::int32_t>(E_FAIL), out;
        for (LONG i = 0; i < n; ++i) {
            ComPtr<IUpdate> u;
            if (FAILED(list->get_Item(i, &u)) || !u) return out.hresult = static_cast<std::int32_t>(E_FAIL), out;
            out.found.push_back(read_candidate(u.Get(), windows_update));
            found_.push_back(u);
        }
        out.ok = true;
        return out;
    }

    bool select(std::size_t index) override {
        if (index >= found_.size()) return false;
        chosen_ = found_[index];
        return true;
    }

    cd::RestorePointState restore_point() override { return rp_ ? rp_() : cd::RestorePointState::NotAttempted; }

    cd::StageOutcome download(const std::function<bool()>& stop) override {
        cd::StageOutcome out;
        out.result_code = cd::kWuaFailed;
        ComPtr<IUpdateCollection> coll;
        if (!one(coll, out)) return out;
        ComPtr<IUpdateDownloader> dl;
        HRESULT hr = session_->CreateUpdateDownloader(&dl);
        if (FAILED(hr) || !dl) return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        if (FAILED(hr = dl->put_Updates(coll.Get()))) return out.hresult = static_cast<std::int32_t>(hr), out;
        auto* done = new Done();
        if (!done->event()) {
            done->Release();
            return out.hresult = static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())), out;
        }
        VARIANT state;
        VariantInit(&state);
        ComPtr<IDownloadJob> job;
        hr = dl->BeginDownload(static_cast<IDownloadProgressChangedCallback*>(done), static_cast<IDownloadCompletedCallback*>(done), state, &job);
        if (FAILED(hr) || !job) {
            done->Release();
            return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        }
        bool aborted = false;
        const bool completed = wait_done(done, stop, [&] { (void)job->RequestAbort(); }, aborted);
        ComPtr<IDownloadResult> result;
        hr = completed ? dl->EndDownload(job.Get(), &result) : E_ABORT;
        (void)job->CleanUp();
        done->Release();
        if (aborted && (FAILED(hr) || !result)) return out.result_code = cd::kWuaAborted, out;
        if (FAILED(hr) || !result) return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        OperationResultCode code = orcFailed;
        (void)result->get_ResultCode(&code);
        LONG rhr = 0;
        (void)result->get_HResult(&rhr);
        out.result_code = aborted ? cd::kWuaAborted : static_cast<int>(code);
        out.hresult = static_cast<std::int32_t>(rhr);
        return out;
    }

    cd::StageOutcome install() override {
        cd::StageOutcome out;
        out.result_code = cd::kWuaFailed;
        ComPtr<IUpdateCollection> coll;
        if (!one(coll, out)) return out;
        ComPtr<IUpdateInstaller> inst;
        HRESULT hr = session_->CreateUpdateInstaller(&inst);
        if (FAILED(hr) || !inst) return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        if (FAILED(hr = inst->put_Updates(coll.Get()))) return out.hresult = static_cast<std::int32_t>(hr), out;
        ComPtr<IInstallationResult> result;
        hr = inst->Install(&result);  // esli: baslamis kurulum kesilmez
        if (FAILED(hr) || !result) return out.hresult = static_cast<std::int32_t>(FAILED(hr) ? hr : E_FAIL), out;
        OperationResultCode code = orcFailed;
        (void)result->get_ResultCode(&code);
        LONG rhr = 0;
        (void)result->get_HResult(&rhr);
        VARIANT_BOOL reboot = VARIANT_FALSE;
        hr = result->get_RebootRequired(&reboot);
        out.result_code = static_cast<int>(code);
        out.hresult = static_cast<std::int32_t>(rhr);
        out.reboot_required = flag(hr, reboot);
        return out;
    }

private:
    // Yalniz secilen tek oge.
    bool one(ComPtr<IUpdateCollection>& coll, cd::StageOutcome& out) {
        if (!session_ || !chosen_) return out.hresult = static_cast<std::int32_t>(E_UNEXPECTED), false;
        HRESULT hr = CoCreateInstance(__uuidof(UpdateCollection), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&coll));
        LONG at = 0;
        if (SUCCEEDED(hr)) hr = coll->Add(chosen_.Get(), &at);
        if (FAILED(hr)) return out.hresult = static_cast<std::int32_t>(hr), false;
        return true;
    }

    Com com_;
    std::function<cd::RestorePointState()> rp_;
    ComPtr<IUpdateSession> session_;
    std::vector<ComPtr<IUpdate>> found_;
    ComPtr<IUpdate> chosen_;
};

}  // namespace

std::unique_ptr<cd::DriverInstallApi> make_driver_install(std::function<cd::RestorePointState()> restore_point) {
    return std::make_unique<WuaDriverInstall>(std::move(restore_point));
}

}  // namespace ef::platform::wua
