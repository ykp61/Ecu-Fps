#include "platform/windows/inventory/driver_info.h"

#include <windows.h>
// initguid: DEVPKEY tanimlari icin once gelmeli (DECLSPEC_SELECTANY; diger TU ile cakismaz).
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>
#include <setupapi.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "platform/windows/inventory/field_json.h"
#include "platform/windows/inventory/update_status.h"
#include "platform/windows/win_env.h"

namespace ef::platform::inventory {

using contracts::Field;
using contracts::Provenance;
namespace aw = core::adapters::windows;

namespace {

std::optional<std::wstring> dev_string(HDEVINFO set, SP_DEVINFO_DATA& d, const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    DWORD need = 0;
    SetupDiGetDevicePropertyW(set, &d, &key, &type, nullptr, 0, &need, 0);
    if (need == 0 || need > 4096) return std::nullopt;
    std::wstring buf(need / sizeof(wchar_t) + 1, L'\0');
    if (!SetupDiGetDevicePropertyW(set, &d, &key, &type, reinterpret_cast<PBYTE>(buf.data()), need, &need, 0) ||
        type != DEVPROP_TYPE_STRING)
        return std::nullopt;
    buf.resize(wcsnlen(buf.c_str(), buf.size()));
    return buf;
}

std::optional<std::int64_t> dev_date_days(HDEVINFO set, SP_DEVINFO_DATA& d) {
    DEVPROPTYPE type = 0;
    FILETIME ft{};
    DWORD need = 0;
    if (!SetupDiGetDevicePropertyW(set, &d, &DEVPKEY_Device_DriverDate, &type, reinterpret_cast<PBYTE>(&ft), sizeof(ft),
                                   &need, 0) ||
        type != DEVPROP_TYPE_FILETIME)
        return std::nullopt;
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    constexpr std::uint64_t kEpochDiff = 116444736000000000ULL;  // 1601 -> 1970, 100 ns
    if (u.QuadPart < kEpochDiff) return std::nullopt;
    return static_cast<std::int64_t>((u.QuadPart - kEpochDiff) / 10000000ULL / 86400ULL);
}

std::optional<std::uint32_t> vendor_of_instance(const std::string& id) {
    const auto p = id.find("VEN_");
    if (p == std::string::npos || p + 8 > id.size()) return std::nullopt;
    unsigned v = 0;
    if (sscanf_s(id.c_str() + p + 4, "%4x", &v) != 1) return std::nullopt;
    return v;
}

}  // namespace

std::vector<aw::DriverInfo> read_display_drivers() {
    std::vector<aw::DriverInfo> out;
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d) && i < 16; ++i) {
        wchar_t id[512];
        if (!SetupDiGetDeviceInstanceIdW(set, &d, id, 512, nullptr)) continue;
        const auto ven = vendor_of_instance(to_utf8(id));
        if (!ven) continue;  // PCI olmayan (ornek: uzak/temel ekran) atlanir
        aw::DriverInfo info;
        info.vendor_id = *ven;
        if (auto n = dev_string(set, d, DEVPKEY_Device_DriverDesc)) info.device_name = to_utf8(*n);
        if (auto v = dev_string(set, d, DEVPKEY_Device_DriverVersion))
            info.version = Field<std::string>::known(to_utf8(*v), Provenance::Api);
        else
            info.version = Field<std::string>::unknown("driver version not reported");
        if (auto days = dev_date_days(set, d))
            info.date_unix_days = Field<std::int64_t>::known(*days, Provenance::Api);
        else
            info.date_unix_days = Field<std::int64_t>::unknown("driver date not reported");
        if (auto p = dev_string(set, d, DEVPKEY_Device_DriverProvider))
            info.provider = Field<std::string>::known(to_utf8(*p), Provenance::Api);
        else
            info.provider = Field<std::string>::unknown("provider not reported");
        out.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

core::json::Value update_check_json() {
    const auto today = static_cast<std::int64_t>(std::time(nullptr) / 86400);
    const auto rep = aw::evaluate_update_check(read_display_drivers(), read_update_status(), today);
    auto o = core::json::Value::object();
    auto drivers = core::json::Value::array();
    for (const auto& d : rep.drivers) {
        auto e = core::json::Value::object();
        e.set("device", core::json::Value::string(d.device_name));
        e.set("vendor_id", core::json::Value::integer(d.vendor_id));
        e.set("version", field_json(d.version));
        if (aw::vendor_from_pci(d.vendor_id) == aw::GpuVendor::Nvidia && d.version.has()) {
            const auto nv = aw::nvidia_display_version(*d.version.value);
            e.set("display_version", nv ? core::json::Value::string(*nv) : core::json::Value::null());
        }
        e.set("date_unix_days", field_json(d.date_unix_days));
        e.set("provider", field_json(d.provider));
        e.set("official_url", core::json::Value::string(std::string(aw::official_driver_url(aw::vendor_from_pci(d.vendor_id)))));
        drivers.push(std::move(e));
    }
    o.set("drivers", std::move(drivers));
    auto f = core::json::Value::array();
    for (const auto& x : rep.findings) {
        auto e = core::json::Value::object();
        e.set("code", core::json::Value::string(std::string(aw::to_string(x.code))));
        e.set("text_key", core::json::Value::string(std::string(aw::text_key(x.code))));
        e.set("evidence", core::json::Value::string(x.evidence));
        e.set("url", core::json::Value::string(x.url));
        f.push(std::move(e));
    }
    o.set("findings", std::move(f));
    o.set("installs_anything", core::json::Value::boolean(rep.installs_anything));
    o.set("online_check", core::json::Value::string("off"));
    return o;
}

}  // namespace ef::platform::inventory
