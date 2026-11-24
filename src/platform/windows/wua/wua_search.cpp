#include "platform/windows/wua/wua_search.h"

#include <windows.h>
#include <oleauto.h>
#include <wuapi.h>
#include <wrl/client.h>

#include <atomic>
#include <string>

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

// BeginSearch tamamlanma geri cagrisi: yalniz olayi isaretler (WUA bunu kendi is parcaciginda cagirir).
class SearchDone final : public ISearchCompletedCallback {
public:
    SearchDone() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~SearchDone() {
        if (event_) CloseHandle(event_);
    }
    HANDLE event() const { return event_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ISearchCompletedCallback)) {
            *out = static_cast<ISearchCompletedCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const long n = --refs_;
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE Invoke(ISearchJob*, ISearchCompletedCallbackArgs*) override {
        if (event_) SetEvent(event_);
        return S_OK;
    }

private:
    std::atomic<long> refs_{1};
    HANDLE event_;
};

struct Com {
    bool owned;
    Com() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~Com() {
        if (owned) CoUninitialize();
    }
};

cd::SearchOutcome fail(HRESULT hr) {
    cd::SearchOutcome o;
    o.hresult = static_cast<std::int32_t>(hr);
    o.failure = cd::classify_hresult(o.hresult);
    if (o.failure == cd::ScanFailure::None) o.failure = cd::ScanFailure::Error;
    return o;
}

std::uint64_t decimal_to_u64(const DECIMAL& d) {
    if (d.sign != 0 || d.Hi32 != 0) return 0;
    std::uint64_t v = d.Lo64;
    for (int i = 0; i < d.scale; ++i) v /= 10;
    return v;
}

cd::DriverUpdate read_update(IUpdate* u) {
    cd::DriverUpdate d;
    d.title = bstr_field([&](BSTR* b) { return u->get_Title(b); });
    d.version = cd::version_from_title(d.title);
    ComPtr<IUpdateIdentity> id;
    if (SUCCEEDED(u->get_Identity(&id)) && id) {
        d.update_id = bstr_field([&](BSTR* b) { return id->get_UpdateID(b); });
        LONG rev = 0;
        if (SUCCEEDED(id->get_RevisionNumber(&rev))) d.revision = static_cast<int>(rev);
    }
    DECIMAL size{};
    if (SUCCEEDED(u->get_MaxDownloadSize(&size))) {
        const std::uint64_t b = decimal_to_u64(size);
        if (b > 0) d.size_bytes = b;
    }
    ComPtr<IWindowsDriverUpdate> drv;
    if (SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&drv))) && drv) {
        d.driver_class = bstr_field([&](BSTR* b) { return drv->get_DriverClass(b); });
        d.hardware_id = bstr_field([&](BSTR* b) { return drv->get_DriverHardwareID(b); });
        d.manufacturer = bstr_field([&](BSTR* b) { return drv->get_DriverManufacturer(b); });
        d.model = bstr_field([&](BSTR* b) { return drv->get_DriverModel(b); });
        d.provider = bstr_field([&](BSTR* b) { return drv->get_DriverProvider(b); });
        DATE date = 0;
        SYSTEMTIME st{};
        if (SUCCEEDED(drv->get_DriverVerDate(&date)) && date > 0 && VariantTimeToSystemTime(date, &st)) {
            d.year = st.wYear, d.month = st.wMonth, d.day = st.wDay;
        }
    }
    return d;
}

class WuaDriverSearch final : public cd::DriverSearchSource {
public:
    cd::SearchOutcome search(const std::function<bool()>& stop) override {
        Com com;
        ComPtr<IUpdateSession> session;
        HRESULT hr = CoCreateInstance(__uuidof(UpdateSession), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&session));
        if (FAILED(hr)) return fail(hr);
        BSTR app = SysAllocString(L"Ecu Fps");
        (void)session->put_ClientApplicationID(app);
        SysFreeString(app);
        ComPtr<IUpdateSearcher> searcher;
        hr = session->CreateUpdateSearcher(&searcher);
        if (FAILED(hr)) return fail(hr);
        (void)searcher->put_Online(VARIANT_TRUE);
        auto* done = new SearchDone();
        if (!done->event()) {
            done->Release();
            return fail(HRESULT_FROM_WIN32(GetLastError()));
        }
        const std::wstring criteria(core::drivers::kWuaDriverCriteria);
        BSTR crit = SysAllocString(criteria.c_str());
        VARIANT state;
        VariantInit(&state);
        ComPtr<ISearchJob> job;
        hr = searcher->BeginSearch(crit, done, state, &job);
        SysFreeString(crit);
        if (FAILED(hr) || !job) {
            done->Release();
            return fail(FAILED(hr) ? hr : E_FAIL);
        }
        bool aborted = false, completed = false;
        for (;;) {
            if (WaitForSingleObject(done->event(), 100) == WAIT_OBJECT_0) {
                completed = true;
                break;
            }
            if (stop && stop()) {
                (void)job->RequestAbort();
                aborted = true;
                // Iptal edilen arama kisa surede tamamlanir; beklemenin ust siniri vardir (kapanisi kilitlemez).
                completed = WaitForSingleObject(done->event(), 5000) == WAIT_OBJECT_0;
                break;
            }
        }
        cd::SearchOutcome out;
        ComPtr<ISearchResult> result;
        hr = completed ? searcher->EndSearch(job.Get(), &result) : E_ABORT;
        (void)job->CleanUp();
        done->Release();
        if (aborted) {
            out.failure = cd::ScanFailure::Cancelled;
            return out;
        }
        if (FAILED(hr) || !result) return fail(FAILED(hr) ? hr : E_FAIL);
        OperationResultCode code = orcNotStarted;
        (void)result->get_ResultCode(&code);
        if (code == orcAborted) {
            // Biz durdurmadan hizmet aramayi birakti: cekirdek bunu (iptal / sure yoksa) koduyla Error gosterir.
            out.failure = cd::ScanFailure::Cancelled;
            out.hresult = static_cast<std::int32_t>(0x8024000Bu);  // WU_E_CALL_CANCELLED
            return out;
        }
        if (code != orcSucceeded && code != orcSucceededWithErrors) return fail(E_FAIL);
        ComPtr<IUpdateCollection> list;
        hr = result->get_Updates(&list);
        if (FAILED(hr) || !list) return fail(FAILED(hr) ? hr : E_FAIL);
        LONG n = 0;
        if (FAILED(list->get_Count(&n))) return fail(E_FAIL);
        for (LONG i = 0; i < n; ++i) {
            if (stop && stop()) {
                out = cd::SearchOutcome{};
                out.failure = cd::ScanFailure::Cancelled;
                return out;
            }
            ComPtr<IUpdate> u;
            if (SUCCEEDED(list->get_Item(i, &u)) && u) out.updates.push_back(read_update(u.Get()));
        }
        out.ok = true;
        return out;
    }
};

}  // namespace

std::unique_ptr<core::drivers::DriverSearchSource> make_driver_search() { return std::make_unique<WuaDriverSearch>(); }

}  // namespace ef::platform::wua
