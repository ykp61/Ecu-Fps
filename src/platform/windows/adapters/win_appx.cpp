#include "platform/windows/adapters/win_appx.h"

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#pragma warning(push, 0)
#include <windows.applicationmodel.h>
#include <windows.management.deployment.h>
#pragma warning(pop)

#include <cstdio>
#include <string>
#include <string_view>

#include "platform/windows/win_env.h"

namespace ef::platform::adapters {

using contracts::ErrorCode;
namespace abi_am = ABI::Windows::ApplicationModel;
namespace abi_dep = ABI::Windows::Management::Deployment;
namespace abi_fnd = ABI::Windows::Foundation;

namespace {

constexpr wchar_t kInboxKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore\\InboxApplications";
constexpr wchar_t kDeprovKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore\\Deprovisioned";
constexpr DWORD kAsyncTimeoutMs = 180000;  // kaldirma/kayit icin ust sinir; asilirsa hata (uydurma basari yok)

struct Combase {
    decltype(&::RoInitialize) ro_init = nullptr;
    decltype(&::RoUninitialize) ro_uninit = nullptr;
    decltype(&::RoActivateInstance) activate = nullptr;
    decltype(&::WindowsCreateString) create = nullptr;
    decltype(&::WindowsDeleteString) del = nullptr;
    decltype(&::WindowsGetStringRawBuffer) buffer = nullptr;
    bool ok() const noexcept { return ro_init && ro_uninit && activate && create && del && buffer; }
};

template <class F>
F proc(HMODULE m, const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
}

const Combase& combase() {
    static const Combase c = [] {
        Combase out;
        if (const HMODULE m = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            out.ro_init = proc<decltype(out.ro_init)>(m, "RoInitialize");
            out.ro_uninit = proc<decltype(out.ro_uninit)>(m, "RoUninitialize");
            out.activate = proc<decltype(out.activate)>(m, "RoActivateInstance");
            out.create = proc<decltype(out.create)>(m, "WindowsCreateString");
            out.del = proc<decltype(out.del)>(m, "WindowsDeleteString");
            out.buffer = proc<decltype(out.buffer)>(m, "WindowsGetStringRawBuffer");
        }
        return out;
    }();
    return c;
}

template <class T>
struct Ref {
    T* p = nullptr;
    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() { reset(); }
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
};

struct Str {
    HSTRING h = nullptr;
    Str() = default;
    Str(const Str&) = delete;
    Str& operator=(const Str&) = delete;
    ~Str() { reset(); }
    void reset() {
        if (h) {
            combase().del(h);
            h = nullptr;
        }
    }
    bool set(std::wstring_view w) {
        reset();
        return SUCCEEDED(combase().create(w.data(), static_cast<UINT32>(w.size()), &h));
    }
};

std::string utf8_of(HSTRING h) {
    if (!h) return {};
    UINT32 n = 0;
    const PCWSTR w = combase().buffer(h, &n);
    return w ? to_utf8(std::wstring_view(w, n)) : std::string();
}

std::string hr_text(const char* what, HRESULT hr) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
    return buf;
}

ErrorCode hr_code(HRESULT hr) { return hr == E_ACCESSDENIED ? ErrorCode::Denied : ErrorCode::IoError; }

struct PmSession {
    HRESULT hr = E_FAIL;
    bool owns_init = false;
    Ref<IInspectable> inst;
    Ref<abi_dep::IPackageManager> v1;
    Ref<abi_dep::IPackageManager5> v5;
    PmSession() {
        const auto& cb = combase();
        if (!cb.ok()) {
            hr = REGDB_E_CLASSNOTREG;
            return;
        }
        const HRESULT init = cb.ro_init(RO_INIT_MULTITHREADED);
        owns_init = SUCCEEDED(init);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
            hr = init;
            return;
        }
        const std::wstring_view cls_name(RuntimeClass_Windows_Management_Deployment_PackageManager);
        Str cls;
        if (!cls.set(cls_name)) {
            hr = E_OUTOFMEMORY;
            return;
        }
        hr = cb.activate(cls.h, inst.put());
        if (FAILED(hr)) return;
        if (!inst.p) {
            hr = E_FAIL;
            return;
        }
        (void)inst->QueryInterface(__uuidof(abi_dep::IPackageManager), reinterpret_cast<void**>(v1.put()));
        (void)inst->QueryInterface(__uuidof(abi_dep::IPackageManager5), reinterpret_cast<void**>(v5.put()));
        hr = v1.p ? S_OK : E_NOINTERFACE;
    }
    ~PmSession() {
        v5.reset();  // nesneler RoUninitialize'dan ONCE birakilir
        v1.reset();
        inst.reset();
        if (owns_init) combase().ro_uninit();
    }
    PmSession(const PmSession&) = delete;
    PmSession& operator=(const PmSession&) = delete;
};

// IAsyncOperationWithProgress'in bitmesini IAsyncInfo ile bekler (zaman asimi = hata; basari uydurulmaz).
contracts::Status wait_async(IInspectable* op) {
    if (!op) return contracts::Status::fail(ErrorCode::IoError, "no async operation");
    Ref<abi_fnd::IAsyncInfo> info;
    if (FAILED(op->QueryInterface(__uuidof(abi_fnd::IAsyncInfo), reinterpret_cast<void**>(info.put()))) || !info.p)
        return contracts::Status::fail(ErrorCode::Unsupported, "IAsyncInfo missing");
    const DWORD t0 = GetTickCount();
    abi_fnd::AsyncStatus st = abi_fnd::AsyncStatus::Started;
    for (;;) {
        if (FAILED(info->get_Status(&st))) return contracts::Status::fail(ErrorCode::IoError, "async status unreadable");
        if (st != abi_fnd::AsyncStatus::Started) break;
        if (GetTickCount() - t0 > kAsyncTimeoutMs) return contracts::Status::fail(ErrorCode::IoError, "package operation timed out");
        Sleep(100);
    }
    if (st == abi_fnd::AsyncStatus::Completed) return contracts::ok_status();
    HRESULT ec = E_FAIL;
    (void)info->get_ErrorCode(&ec);
    return contracts::Status::fail(st == abi_fnd::AsyncStatus::Canceled ? ErrorCode::IoError : hr_code(ec), hr_text("package operation", ec));
}

// Kayit defteri alt anahtar adlari (64-bit gorunum). Anahtar yoksa found=false.
bool enum_subkeys(HKEY root, const wchar_t* path, std::vector<std::wstring>& out, bool& found) {
    HKEY k = nullptr;
    const LONG rc = RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &k);
    if (rc == ERROR_FILE_NOT_FOUND) {
        found = false;
        return true;
    }
    if (rc != ERROR_SUCCESS) return false;
    found = true;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        DWORD n = 512;
        const LONG r = RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) {
            RegCloseKey(k);
            return false;
        }
        out.emplace_back(name, n);
    }
    RegCloseKey(k);
    return true;
}

// "Ad_surum_mimari_kaynak_YayinciKimligi" -> "Ad_YayinciKimligi".
std::wstring family_of_full_name(const std::wstring& full) {
    const auto first = full.find(L'_');
    const auto last = full.rfind(L'_');
    if (first == std::wstring::npos || last == first) return {};
    return full.substr(0, first) + L"_" + full.substr(last + 1);
}

bool ieq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

}  // namespace

contracts::Result<std::optional<std::string>> WinAppxApi::installed_full_name(const std::string& family) {
    using R = contracts::Result<std::optional<std::string>>;
    PmSession pm;
    if (FAILED(pm.hr)) return R::fail(hr_code(pm.hr), hr_text("PackageManager", pm.hr));
    Str fam, sid;  // bos SID = mevcut kullanici
    if (!fam.set(to_wide(family))) return R::fail(ErrorCode::IoError, "string alloc");
    Ref<__FIIterable_1_Windows__CApplicationModel__CPackage> found;
    HRESULT hr = pm.v1->FindPackagesByUserSecurityIdPackageFamilyName(sid.h, fam.h, found.put());
    if (FAILED(hr) || !found.p) return R::fail(hr_code(hr), hr_text("FindPackages", hr));
    Ref<__FIIterator_1_Windows__CApplicationModel__CPackage> it;
    boolean has = false;
    if (FAILED(found->First(it.put())) || !it.p || FAILED(it->get_HasCurrent(&has))) return R::fail(ErrorCode::IoError, "package iteration");
    if (!has) return std::optional<std::string>{};
    Ref<abi_am::IPackage> pkg;
    Ref<abi_am::IPackageId> id;
    Str full;
    if (FAILED(it->get_Current(pkg.put())) || !pkg.p || FAILED(pkg->get_Id(id.put())) || !id.p || FAILED(id->get_FullName(&full.h)))
        return R::fail(ErrorCode::IoError, "package identity unreadable");
    return std::optional<std::string>{utf8_of(full.h)};
}

contracts::Result<bool> WinAppxApi::restorable(const std::string& family) {
    using R = contracts::Result<bool>;
    const std::wstring wfam = to_wide(family);
    std::vector<std::wstring> inbox, deprov;
    bool inbox_found = false, deprov_found = false;
    if (!enum_subkeys(HKEY_LOCAL_MACHINE, kInboxKey, inbox, inbox_found) || !inbox_found)
        return R::fail(ErrorCode::IoError, "provisioned package list unreadable");
    if (!enum_subkeys(HKEY_LOCAL_MACHINE, kDeprovKey, deprov, deprov_found)) return R::fail(ErrorCode::IoError, "deprovisioned list unreadable");
    for (const auto& d : deprov)
        if (ieq(d, wfam)) return false;  // kullanici/OS bunu makineden de kaldirmis: yeniden kayit garanti degil
    for (const auto& f : inbox)
        if (ieq(family_of_full_name(f), wfam)) return true;
    return false;
}

contracts::Status WinAppxApi::remove_for_user(const std::string& full_name) {
    PmSession pm;
    if (FAILED(pm.hr)) return contracts::Status::fail(hr_code(pm.hr), hr_text("PackageManager", pm.hr));
    Str full;
    if (!full.set(to_wide(full_name))) return contracts::Status::fail(ErrorCode::IoError, "string alloc");
    Ref<__FIAsyncOperationWithProgress_2_Windows__CManagement__CDeployment__CDeploymentResult_Windows__CManagement__CDeployment__CDeploymentProgress> op;
    const HRESULT hr = pm.v1->RemovePackageAsync(full.h, op.put());  // seceneksiz: yalniz mevcut kullanici
    if (FAILED(hr) || !op.p) return contracts::Status::fail(hr_code(hr), hr_text("RemovePackageAsync", hr));
    return wait_async(reinterpret_cast<IInspectable*>(op.p));
}

contracts::Status WinAppxApi::register_by_family(const std::string& family) {
    PmSession pm;
    if (FAILED(pm.hr)) return contracts::Status::fail(hr_code(pm.hr), hr_text("PackageManager", pm.hr));
    if (!pm.v5.p) return contracts::Status::fail(ErrorCode::Unsupported, "RegisterPackageByFamilyName needs a newer Windows");
    Str fam;
    if (!fam.set(to_wide(family))) return contracts::Status::fail(ErrorCode::IoError, "string alloc");
    Ref<__FIAsyncOperationWithProgress_2_Windows__CManagement__CDeployment__CDeploymentResult_Windows__CManagement__CDeployment__CDeploymentProgress> op;
    const HRESULT hr = pm.v5->RegisterPackageByFamilyNameAndOptionalPackagesAsync(fam.h, nullptr, abi_dep::DeploymentOptions_None, nullptr,
                                                                                  nullptr, op.put());
    if (FAILED(hr) || !op.p) return contracts::Status::fail(hr_code(hr), hr_text("RegisterPackageByFamilyName", hr));
    return wait_async(reinterpret_cast<IInspectable*>(op.p));
}

core::perf::BrowserInfo read_browser_info() {
    core::perf::BrowserInfo b;
    std::vector<std::wstring> names;
    bool found = false;
    if (!enum_subkeys(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Clients\\StartMenuInternet", names, found)) return b;  // okunamadi
    bool found_user = false;
    (void)enum_subkeys(HKEY_CURRENT_USER, L"SOFTWARE\\Clients\\StartMenuInternet", names, found_user);
    for (const auto& n : names) b.clients.push_back(to_utf8(n));
    // Varsayilan https islemcisi (kullanici secimi yoksa bos: Edge grubu sunulmaz).
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice", 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        wchar_t buf[256];
        DWORD n = sizeof buf - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, L"ProgId", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &n) == ERROR_SUCCESS && type == REG_SZ) {
            buf[n / sizeof(wchar_t)] = 0;
            b.default_progid = to_utf8(buf);
        }
        RegCloseKey(k);
    }
    b.read_ok = true;
    return b;
}

}  // namespace ef::platform::adapters
