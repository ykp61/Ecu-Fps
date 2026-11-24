#include "platform/windows/vendor_runtime.h"

#include <windows.h>
#include <wincrypt.h>
#include <mscat.h>
#include <softpub.h>
#include <wintrust.h>

#include <cstdio>
#include <vector>

#include "platform/windows/signature.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using core::security::SignatureStatus;
using core::vendors::RuntimeState;
using core::vendors::Vendor;

void harden_dll_search() {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetDllDirectoryW(L"");  // gecerli dizini arama yolundan cikar
}

namespace {
std::wstring system32() {
    wchar_t buf[MAX_PATH];
    const UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf, n);
}

bool same_path(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::string file_version(const std::wstring& path) {
    DWORD h = 0;
    const DWORD n = GetFileVersionInfoSizeW(path.c_str(), &h);
    if (!n) return {};
    std::vector<unsigned char> buf(n);
    if (!GetFileVersionInfoW(path.c_str(), 0, n, buf.data())) return {};
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) return {};
    char s[48];
    std::snprintf(s, sizeof s, "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
                  HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    return s;
}

SignatureStatus catalog_signature(const std::filesystem::path& file) {
    HCATADMIN admin = nullptr;
    if (!CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) return SignatureStatus::Unknown;
    HANDLE fh = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        CryptCATAdminReleaseContext(admin, 0);
        return SignatureStatus::Unknown;
    }
    DWORD hlen = 0;
    CryptCATAdminCalcHashFromFileHandle2(admin, fh, &hlen, nullptr, 0);
    std::vector<BYTE> hash(hlen);
    SignatureStatus result = SignatureStatus::Unsigned;
    if (hlen && CryptCATAdminCalcHashFromFileHandle2(admin, fh, &hlen, hash.data(), 0)) {
        HCATINFO cat = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), hlen, 0, nullptr);
        if (cat) {
            CATALOG_INFO ci{};
            ci.cbStruct = sizeof(ci);
            if (CryptCATCatalogInfoFromContext(cat, &ci, 0)) {
                // Uye etiketi: hash'in buyuk harf hex metni
                std::wstring tag;
                static constexpr wchar_t hx[] = L"0123456789ABCDEF";
                for (BYTE b : hash) {
                    tag += hx[b >> 4];
                    tag += hx[b & 15];
                }
                WINTRUST_CATALOG_INFO wc{};
                wc.cbStruct = sizeof(wc);
                wc.pcwszCatalogFilePath = ci.wszCatalogFile;
                wc.pcwszMemberFilePath = file.c_str();
                wc.pcwszMemberTag = tag.c_str();
                wc.pbCalculatedFileHash = hash.data();
                wc.cbCalculatedFileHash = hlen;
                wc.hCatAdmin = admin;
                WINTRUST_DATA wd{};
                wd.cbStruct = sizeof(wd);
                wd.dwUIChoice = WTD_UI_NONE;
                wd.fdwRevocationChecks = WTD_REVOKE_NONE;
                wd.dwUnionChoice = WTD_CHOICE_CATALOG;
                wd.pCatalog = &wc;
                wd.dwStateAction = WTD_STATEACTION_VERIFY;
                wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
                GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
                const LONG r = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
                wd.dwStateAction = WTD_STATEACTION_CLOSE;
                WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
                result = r == ERROR_SUCCESS ? SignatureStatus::Valid : SignatureStatus::Untrusted;
            }
            CryptCATAdminReleaseCatalogContext(admin, cat, 0);
        }
    }
    CloseHandle(fh);
    CryptCATAdminReleaseContext(admin, 0);
    return result;
}
}  // namespace

SignatureStatus verify_file_or_catalog_signature(const std::filesystem::path& file) {
    const auto embedded = verify_file_signature(file);
    if (embedded != SignatureStatus::Unsigned) return embedded;
    return catalog_signature(file);
}

LoadedRuntime load_vendor_runtime(Vendor v) {
    LoadedRuntime r;
    r.info.vendor = v;
    const auto dll = core::vendors::runtime_dll(v);
    if (dll.empty()) return r;
    const std::wstring expected = system32() + L"\\" + std::wstring(dll);
    if (GetFileAttributesW(expected.c_str()) == INVALID_FILE_ATTRIBUTES) {
        r.info.state = RuntimeState::Missing;
        r.info.detail = "not present in System32";
        return r;
    }
    // Yuklemeden ONCE imza: kotu niyetli DllMain calismasin.
    const auto sig = verify_file_or_catalog_signature(expected);
    if (sig != SignatureStatus::Valid) {
        r.info.state = RuntimeState::Untrusted;
        r.info.detail = "signature " + std::string(core::security::to_string(sig));
        return r;
    }
    HMODULE h = LoadLibraryExW(std::wstring(dll).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        r.info.state = RuntimeState::LoadFailed;
        r.info.detail = "LoadLibraryEx failed (" + std::to_string(GetLastError()) + ")";
        return r;
    }
    wchar_t got[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(h, got, MAX_PATH * 2);
    const std::wstring loaded(got, n);
    if (!same_path(loaded, expected)) {  // baska bir kopya (onceden yuklenmis taklit) -> reddet
        FreeLibrary(h);
        r.info.state = RuntimeState::Untrusted;
        r.info.detail = "module resolved outside System32";
        return r;
    }
    r.module = h;
    r.info.state = RuntimeState::Loaded;
    r.info.path = to_utf8(loaded);
    r.info.file_version = file_version(expected);
    return r;
}

void unload_vendor_runtime(LoadedRuntime& r) {
    if (r.module) FreeLibrary(static_cast<HMODULE>(r.module));
    r.module = nullptr;
}

}  // namespace ef::platform
