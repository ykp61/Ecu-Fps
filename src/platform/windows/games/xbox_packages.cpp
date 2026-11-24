// EF-X11: kullanicinin Windows paketleri (Windows.Management.Deployment.PackageManager) — salt okuma.
// WinRT ABI dogrudan (C++/WinRT/WRL yok). combase.dll System32'den calisma aninda yuklenir: EXE import tablosu
// degismez (check_package.ps1 yalniz System32 dosyalarini kabul eder; api-ms-win-core-winrt-* dosya degildir).
// ACL degistirme, takeown, WindowsApps listeleme yok.
#include "platform/windows/games/library_scan.h"

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#pragma warning(push, 0)
#include <windows.applicationmodel.h>
#include <windows.management.deployment.h>
#pragma warning(pop)

#include <string>
#include <string_view>

#include "platform/windows/win_env.h"

namespace ef::platform::games {

namespace {

namespace abi_am = ABI::Windows::ApplicationModel;
namespace abi_dep = ABI::Windows::Management::Deployment;

constexpr std::size_t kMaxPackages = 8192;  // bellek siniri; cekirdek 4000 adayda keser

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
        if (const HMODULE m = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {  // surec boyunca yuklu
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
};

std::string utf8_of(HSTRING h) {
    if (!h) return {};
    UINT32 n = 0;
    const PCWSTR w = combase().buffer(h, &n);
    return w ? to_utf8(std::wstring_view(w, n)) : std::string();
}

// Tek sorgu icin WinRT oturumu. Is parcacigi zaten STA ise (UI) oldugu gibi kullanilir; MTA girisi dengelenir.
struct PmSession {
    HRESULT hr = E_FAIL;
    bool owns_init = false;
    Ref<IInspectable> inst;
    Ref<abi_dep::IPackageManager> v1;
    Ref<abi_dep::IPackageManager2> v2;
    Str sid;  // bos = mevcut kullanici
    explicit PmSession(const std::wstring& user_sid) {
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
        hr = cb.create(cls_name.data(), static_cast<UINT32>(cls_name.size()), &cls.h);
        if (FAILED(hr)) return;
        hr = cb.activate(cls.h, inst.put());
        if (FAILED(hr)) return;
        if (!inst.p) {
            hr = E_FAIL;
            return;
        }
        (void)inst->QueryInterface(__uuidof(abi_dep::IPackageManager), reinterpret_cast<void**>(v1.put()));
        (void)inst->QueryInterface(__uuidof(abi_dep::IPackageManager2), reinterpret_cast<void**>(v2.put()));
        if (!user_sid.empty()) {
            hr = cb.create(user_sid.c_str(), static_cast<UINT32>(user_sid.size()), &sid.h);
            if (FAILED(hr)) return;
        }
        hr = S_OK;
    }
    ~PmSession() {
        sid.reset();  // nesneler RoUninitialize'dan ONCE birakilir
        v2.reset();
        v1.reset();
        inst.reset();
        if (owns_init) combase().ro_uninit();
    }
    PmSession(const PmSession&) = delete;
    PmSession& operator=(const PmSession&) = delete;
};

}  // namespace

core::games::PackageList WinLibraryFs::user_packages() {
    using PL = core::games::PackageList;
    PL out;  // Unavailable
    if (overridden_) return out;  // test kokleri: paket envanteri okunmaz
    PmSession pm(sid_);
    if (FAILED(pm.hr)) {
        out.status = pm.hr == E_ACCESSDENIED ? PL::Status::Denied
                     : (pm.hr == REGDB_E_CLASSNOTREG || pm.hr == CLASS_E_CLASSNOTAVAILABLE) ? PL::Status::Unavailable
                                                                                             : PL::Status::Error;
        return out;
    }
    if (!pm.v2.p) return out;
    Ref<__FIIterable_1_Windows__CApplicationModel__CPackage> all;
    const HRESULT hr = pm.v2->FindPackagesByUserSecurityIdWithPackageTypes(pm.sid.h, abi_dep::PackageTypes_Main, all.put());
    if (FAILED(hr) || !all.p) {
        out.status = hr == E_ACCESSDENIED ? PL::Status::Denied : PL::Status::Error;
        return out;
    }
    Ref<__FIIterator_1_Windows__CApplicationModel__CPackage> it;
    boolean has = false;
    if (FAILED(all->First(it.put())) || !it.p || FAILED(it->get_HasCurrent(&has))) {
        out.status = PL::Status::Error;
        return out;
    }
    out.status = PL::Status::Ok;
    while (has) {
        if (out.items.size() >= kMaxPackages) {
            out.status = PL::Status::Truncated;
            break;
        }
        Ref<abi_am::IPackage> pkg;
        if (FAILED(it->get_Current(pkg.put())) || !pkg.p) {
            out.status = PL::Status::Error;
            break;
        }
        Ref<abi_am::IPackage8> p8;
        if (FAILED(pkg->QueryInterface(__uuidof(abi_am::IPackage8), reinterpret_cast<void**>(p8.put()))) || !p8.p) {
            out.items.clear();
            out.status = PL::Status::Unavailable;  // eski Windows: EffectivePath yok; yol tahmin edilmez
            break;
        }
        core::games::UserPackage up;
        boolean stub = false;
        if (SUCCEEDED(p8->get_IsStub(&stub))) up.stub = stub != 0;
        Str path, fam;
        Ref<abi_am::IPackageId> id;
        if (FAILED(p8->get_EffectivePath(&path.h)) || FAILED(pkg->get_Id(id.put())) || !id.p || FAILED(id->get_FamilyName(&fam.h))) {
            out.status = PL::Status::Error;  // kimligi/yolu okunamayan paket: liste tam degil
            break;
        }
        up.path = utf8_of(path.h);
        up.family = utf8_of(fam.h);
        out.items.push_back(std::move(up));
        if (FAILED(it->MoveNext(&has))) {
            out.status = PL::Status::Error;
            break;
        }
    }
    return out;
}

std::string WinLibraryFs::package_display_name(const std::string& family) {
    if (overridden_ || family.empty()) return {};
    PmSession pm(sid_);
    if (FAILED(pm.hr) || !pm.v1.p) return {};
    const std::wstring wf = to_wide(family);
    Str fam;
    if (FAILED(combase().create(wf.c_str(), static_cast<UINT32>(wf.size()), &fam.h))) return {};
    Ref<__FIIterable_1_Windows__CApplicationModel__CPackage> found;
    if (FAILED(pm.v1->FindPackagesByUserSecurityIdPackageFamilyName(pm.sid.h, fam.h, found.put())) || !found.p) return {};
    Ref<__FIIterator_1_Windows__CApplicationModel__CPackage> it;
    boolean has = false;
    if (FAILED(found->First(it.put())) || !it.p || FAILED(it->get_HasCurrent(&has)) || !has) return {};
    Ref<abi_am::IPackage> pkg;
    if (FAILED(it->get_Current(pkg.put())) || !pkg.p) return {};
    Ref<abi_am::IPackage2> p2;
    if (FAILED(pkg->QueryInterface(__uuidof(abi_am::IPackage2), reinterpret_cast<void**>(p2.put()))) || !p2.p) return {};
    Str name;
    if (FAILED(p2->get_DisplayName(&name.h))) return {};
    return utf8_of(name.h);
}

}  // namespace ef::platform::games
