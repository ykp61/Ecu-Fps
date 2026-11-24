// F-68: salt-okuma surucu sagligi. SetupAPI ekran sinifi aygitlari ve DriverStore paket adlari
// okunur. Sorun kodu yalniz CM_Get_DevNode_Status ile kanitlanir; paket sayisi tek basina
// kalinti degildir ve metin bunu soyler. Hicbir surucu/ayar yazilmaz.
#include "platform/windows/readiness/driver_health_reader.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <cctype>
#include <cwctype>
#include <string>

namespace ef::platform::readiness {

namespace {

std::string utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(static_cast<std::size_t>(n - 1));
    return s;
}

std::string device_name(HDEVINFO set, SP_DEVINFO_DATA& info) {
    wchar_t buf[512]{};
    DWORD type = 0;
    if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_DEVICEDESC, &type,
                                         reinterpret_cast<PBYTE>(buf), sizeof(buf), nullptr))
        return utf8(buf);
    return {};
}

bool driver_package_name(const std::wstring& raw, std::wstring& lower) {
    lower = raw;
    for (auto& ch : lower) ch = static_cast<wchar_t>(std::tolower(ch));
    return lower.find(L".inf_amd64") != std::wstring::npos;
}

void count_driver_families(const std::wstring& repo, core::readiness::DisplayDriverHealthInfo& out) {
    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = repo + LR"(\*)";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;  // sayilar nullopt kalir: okunamadi
    std::uint32_t nvidia = 0, amd = 0, intel = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        std::wstring name;
        if (!driver_package_name(fd.cFileName, name)) continue;
        if (name.rfind(L"nv", 0) == 0) ++nvidia;
        else if (name.size() > 1 && name[0] == L'u' && std::iswdigit(name[1])) ++amd;
        else if (name.rfind(L"iigd", 0) == 0 || name.rfind(L"igdlh", 0) == 0) ++intel;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    out.nvidia_packages = nvidia;
    out.amd_packages = amd;
    out.intel_packages = intel;
}

}  // namespace

core::readiness::DisplayDriverHealthInfo read_display_driver_health() {
    core::readiness::DisplayDriverHealthInfo out;
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (set != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
            DWORD status = 0, problem = 0;
            if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS && problem != 0) {
                std::string name = device_name(set, info);
                out.problem_devices.push_back(name.empty() ? "display" : std::move(name));
            }
        }
        SetupDiDestroyDeviceInfoList(set);
        out.enumeration_ok = true;
    }
    wchar_t sys[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const std::wstring repo = std::wstring(sys) + LR"(\DriverStore\FileRepository)";
        count_driver_families(repo, out);
    }
    return out;
}

}  // namespace ef::platform::readiness
