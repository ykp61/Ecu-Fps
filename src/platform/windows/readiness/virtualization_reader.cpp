// F-50: salt-okuma sanallastirma isaretleri. Hizmet anahtari/dosya varligi "kurulu olabilir"
// demektir; ozellik durumunun kesin kaniti degildir. Hicbir sey yazilmaz.
#include "platform/windows/readiness/virtualization_reader.h"

#include <windows.h>

#include <string>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define EF_HAVE_CPUID 1
#else
#define EF_HAVE_CPUID 0
#endif

namespace ef::platform::readiness {

namespace {

bool key_exists(HKEY root, const wchar_t* key) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}

bool file_exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::optional<std::uint32_t> reg_dword(HKEY root, const wchar_t* key, const wchar_t* name) {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return std::nullopt;
    return static_cast<std::uint32_t>(v);
}

}  // namespace

core::readiness::VirtualizationInfo read_virtualization() {
    core::readiness::VirtualizationInfo v;
#if EF_HAVE_CPUID
    int regs[4]{};
    __cpuid(regs, 1);
    v.hypervisor_present = (static_cast<unsigned>(regs[2]) & (1u << 31)) != 0;  // CPUID.01H:ECX[31]
#endif
    v.hyperv_service = key_exists(HKEY_LOCAL_MACHINE, LR"(SYSTEM\CurrentControlSet\Services\vmms)");
    v.vmp_service = key_exists(HKEY_LOCAL_MACHINE, LR"(SYSTEM\CurrentControlSet\Services\vmcompute)");

    wchar_t sys[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const std::wstring base(sys);
        v.whp_dll = file_exists(base + LR"(\WinHvPlatform.dll)");
        v.wsl_exe = file_exists(base + LR"(\wsl.exe)");
    }
    const auto wsl_version = reg_dword(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Lxss)",
                                       L"DefaultVersion");
    v.wsl_default_v2 = wsl_version.has_value() && *wsl_version >= 2;
    return v;
}

}  // namespace ef::platform::readiness
