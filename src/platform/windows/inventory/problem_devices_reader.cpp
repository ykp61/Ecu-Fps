// T17: salt-okuma sorunlu aygit listesi. Yalniz SetupDiGetClassDevs + CM_Get_DevNode_Status; yazma yok.
#include "platform/windows/inventory/problem_devices_reader.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <algorithm>
#include <string>
#include <utility>

namespace ef::platform::inventory {

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

std::string reg_prop(HDEVINFO set, SP_DEVINFO_DATA& info, DWORD prop) {
    wchar_t buf[512]{};
    DWORD type = 0;
    if (SetupDiGetDeviceRegistryPropertyW(set, &info, prop, &type, reinterpret_cast<PBYTE>(buf), sizeof(buf) - sizeof(wchar_t), nullptr))
        return utf8(buf);
    return {};
}

}  // namespace

core::inventory::ProblemDeviceReport read_problem_devices() {
    core::inventory::ProblemDeviceReport rep;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) return rep;  // enumerated=false: "bulunamadi" denmez

    std::vector<core::inventory::ProblemDevice> found;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        DWORD status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS) continue;
        if ((status & DN_HAS_PROBLEM) == 0 || problem == 0) continue;
        core::inventory::ProblemDevice d;
        d.name = reg_prop(set, info, SPDRP_FRIENDLYNAME);
        if (d.name.empty()) d.name = reg_prop(set, info, SPDRP_DEVICEDESC);
        d.device_class = reg_prop(set, info, SPDRP_CLASS);
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(info.DevInst, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) d.instance_id = utf8(id);
        d.problem_code = problem;
        found.push_back(std::move(d));
    }
    // Enumerasyon dongusu SetupDiEnumDeviceInfo ERROR_NO_MORE_ITEMS ile biter; baska hata listeyi eksik birakir.
    const DWORD last = GetLastError();
    SetupDiDestroyDeviceInfoList(set);
    if (last != ERROR_NO_MORE_ITEMS) return rep;  // eksik liste: enumerated=false

    // Gercek arizalar once; kod 22 (kullanici kapatmis) bilgi olarak sona.
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return core::inventory::problem_is_informational(a.problem_code) < core::inventory::problem_is_informational(b.problem_code);
    });
    rep.enumerated = true;
    for (auto& d : found)
        if (!core::inventory::add_problem_device(rep, std::move(d))) break;
    return rep;
}

}  // namespace ef::platform::inventory
