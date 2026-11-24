#include "platform/windows/adapters/win_power_subs.h"

#include <windows.h>
#include <powrprof.h>

#include <cstdio>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Status;
using core::adapters::windows::PowerSubValue;

namespace {
struct SubDef {
    const char* target;
    GUID subgroup;
    GUID setting;
};

// Windows belgeli guc ayari GUID'leri (docs: options-for-perf-boost, options-for-core-parking,
// options-for-pci-express). Sabit liste; serbest GUID girişi yok.
constexpr GUID kProcessorSubgroup{0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
constexpr GUID kPerfBoostSetting{0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
constexpr GUID kCoreParkingSetting{0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
constexpr GUID kPcieSubgroup{0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
constexpr GUID kPcieAspmSetting{0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
// B1: USB ayarlari alt grubu 2a737441-1930-4402-8d77-b2bebba308a3, USB secici askiya alma 48e6b7a6-50f5-4782-a5d4-53bb8f07e226
// (ADR-040 dogrulama 2, kaynak S108); AC indeksi 0 = devre disi.
constexpr GUID kUsbSubgroup{0x2a737441, 0x1930, 0x4402, {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
constexpr GUID kUsbSelectiveSuspendSetting{0x48e6b7a6, 0x50f5, 0x4782, {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};

const SubDef* find_def(std::string_view target) noexcept {
    static constexpr SubDef kDefs[] = {
        {"power:boost:ac", kProcessorSubgroup, kPerfBoostSetting},
        {"power:parking:ac", kProcessorSubgroup, kCoreParkingSetting},
        {"power:aspm:ac", kPcieSubgroup, kPcieAspmSetting},
        {"power:usbss:ac", kUsbSubgroup, kUsbSelectiveSuspendSetting},
    };
    for (const auto& d : kDefs)
        if (target == d.target) return &d;
    return nullptr;
}

std::string guid_text(const GUID& g) {
    char b[40];
    std::snprintf(b, sizeof b, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", static_cast<unsigned long>(g.Data1), g.Data2,
                  g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}

bool parse_guid(const std::string& s, GUID& g) {
    unsigned long d1 = 0;
    unsigned int d2 = 0, d3 = 0, b[8] = {};
    if (sscanf_s(s.c_str(), "%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6],
                    &b[7]) != 11)
        return false;
    g.Data1 = d1;
    g.Data2 = static_cast<unsigned short>(d2);
    g.Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; ++i) g.Data4[i] = static_cast<unsigned char>(b[i]);
    return true;
}

ErrorCode map_err(DWORD e) {
    if (e == ERROR_ACCESS_DENIED) return ErrorCode::Denied;
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_INVALID_PARAMETER || e == ERROR_NOT_SUPPORTED) return ErrorCode::Unsupported;
    return ErrorCode::IoError;
}
}  // namespace

contracts::Result<PowerSubValue> WinPowerSubApi::read_active_ac(std::string_view target) {
    using R = contracts::Result<PowerSubValue>;
    const SubDef* d = find_def(target);
    if (!d) return R::fail(ErrorCode::ParseError, "power setting target");
    GUID* scheme = nullptr;
    DWORD e = PowerGetActiveScheme(nullptr, &scheme);
    if (e != ERROR_SUCCESS || !scheme) return R::fail(map_err(e), "PowerGetActiveScheme");
    DWORD index = 0;
    e = PowerReadACValueIndex(nullptr, scheme, &d->subgroup, &d->setting, &index);
    PowerSubValue out;
    if (e == ERROR_SUCCESS) out = PowerSubValue{guid_text(*scheme), static_cast<std::uint32_t>(index)};
    LocalFree(scheme);
    if (e != ERROR_SUCCESS) return R::fail(map_err(e), "PowerReadACValueIndex (" + std::to_string(e) + ")");
    return out;
}

Status WinPowerSubApi::write_active_ac(std::string_view target, std::uint32_t v) {
    const SubDef* d = find_def(target);
    if (!d) return Status::fail(ErrorCode::ParseError, "power setting target");
    GUID* scheme = nullptr;
    DWORD e = PowerGetActiveScheme(nullptr, &scheme);
    if (e != ERROR_SUCCESS || !scheme) return Status::fail(map_err(e), "PowerGetActiveScheme");
    e = PowerWriteACValueIndex(nullptr, scheme, &d->subgroup, &d->setting, static_cast<DWORD>(v));
    LocalFree(scheme);
    return e == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(map_err(e), "PowerWriteACValueIndex (" + std::to_string(e) + ")");
}

Status WinPowerSubApi::write_scheme_ac(const std::string& scheme_guid, std::string_view target, std::uint32_t v) {
    const SubDef* d = find_def(target);
    if (!d) return Status::fail(ErrorCode::ParseError, "power setting target");
    GUID scheme{};
    if (!parse_guid(scheme_guid, scheme)) return Status::fail(ErrorCode::ParseError, "power scheme guid");
    const DWORD e = PowerWriteACValueIndex(nullptr, &scheme, &d->subgroup, &d->setting, static_cast<DWORD>(v));
    return e == ERROR_SUCCESS ? contracts::ok_status() : Status::fail(map_err(e), "PowerWriteACValueIndex (" + std::to_string(e) + ")");
}

contracts::Result<std::uint32_t> WinPowerSubApi::read_scheme_ac(const std::string& scheme_guid, std::string_view target) {
    using R = contracts::Result<std::uint32_t>;
    const SubDef* d = find_def(target);
    if (!d) return R::fail(ErrorCode::ParseError, "power setting target");
    GUID scheme{};
    if (!parse_guid(scheme_guid, scheme)) return R::fail(ErrorCode::ParseError, "power scheme guid");
    DWORD index = 0;
    const DWORD e = PowerReadACValueIndex(nullptr, &scheme, &d->subgroup, &d->setting, &index);
    if (e != ERROR_SUCCESS) return R::fail(map_err(e), "PowerReadACValueIndex (" + std::to_string(e) + ")");
    return static_cast<std::uint32_t>(index);
}

}  // namespace ef::platform::adapters
