// F-62: SMBIOS bellek yerlesimini hazirlik yapisina cevirir (salt okuma).
// Okunamayan alan nullopt kalir; sifir veya "sorun yok" varsayilmaz.
#include "platform/windows/readiness/memory_reader.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <cmath>

#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/maintenance_win.h"

namespace ef::platform::readiness {

namespace {
std::optional<std::uint32_t> opt(const contracts::Field<std::uint32_t>& f) {
    return f.has() ? std::optional<std::uint32_t>(*f.value) : std::nullopt;
}
std::optional<std::uint64_t> opt(const contracts::Field<std::uint64_t>& f) {
    return f.has() ? std::optional<std::uint64_t>(*f.value) : std::nullopt;
}
std::string text(const contracts::Field<std::string>& f) { return f.has() ? *f.value : std::string(); }

// PDH "Memory / Free & Zero Page List Bytes": bos + sifirlanmis liste (bekleme DAHIL DEGIL). Okunamazsa nullopt.
std::optional<std::uint64_t> read_free_and_zero_bytes() {
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return std::nullopt;
    std::optional<std::uint64_t> out;
    PDH_HCOUNTER h = nullptr;
    if (PdhAddEnglishCounterW(q, L"\\Memory\\Free & Zero Page List Bytes", 0, &h) == ERROR_SUCCESS && h &&
        PdhCollectQueryData(q) == ERROR_SUCCESS) {
        PDH_FMT_COUNTERVALUE v{};
        if (PdhGetFormattedCounterValue(h, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) == ERROR_SUCCESS &&
            (v.CStatus == PDH_CSTATUS_VALID_DATA || v.CStatus == PDH_CSTATUS_NEW_DATA) && std::isfinite(v.doubleValue) && v.doubleValue >= 0.0)
            out = static_cast<std::uint64_t>(v.doubleValue);
    }
    PdhCloseQuery(q);
    return out;
}
}  // namespace

core::readiness::MemoryPressureInfo read_memory_pressure() {
    core::readiness::MemoryPressureInfo out;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0) out.total_bytes = ms.ullTotalPhys;
    if (const auto st = platform::read_standby_bytes(); st.ok()) out.standby_bytes = st.value();
    out.free_bytes = read_free_and_zero_bytes();
    return out;
}

core::readiness::MemoryLayoutInfo read_memory_layout() {
    namespace ci = core::inventory;
    core::readiness::MemoryLayoutInfo out;
    const auto mem = platform::inventory::read_memory();
    out.total_slots = opt(mem.smbios.total_slots);
    out.populated_slots = opt(mem.smbios.populated_slots);
    out.modules.reserve(mem.smbios.populated.size());
    for (const auto& mod : mem.smbios.populated) {
        core::readiness::MemoryModuleInfo m;
        m.size_mib = opt(mod.size_mib);
        m.configured_mts = opt(mod.configured_mts);
        m.locator = text(mod.locator);
        m.bank_locator = text(mod.bank_locator);
        out.modules.push_back(std::move(m));
    }
    if (const auto* t = platform::inventory::smbios_table(); t != nullptr)
        out.virtual_machine = ci::smbios_indicates_vm(*t);
    return out;
}

}  // namespace ef::platform::readiness
