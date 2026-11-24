#pragma once
// ADR-033: "Uygulamalar ve ozellikler" (ARP) kaydinin degerleri (saf). Yazma platform/windows tarafindadir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/setup/layout.h"

namespace ef::core::setup {

struct ArpValues {
    std::wstring display_name = L"Ecu Fps", display_version, publisher = L"Ecu Fps", display_icon, install_location,
                 uninstall_string;
    std::uint32_t estimated_size_kb = 0;
    std::wstring install_date;  // yyyymmdd
    bool no_modify = true, no_repair = true;
    friend bool operator==(const ArpValues&, const ArpValues&) = default;
};

// display_icon "<exe>,0", estimated_size_kb = ceil(exe_bytes / 1024) (uint32 sinirinda kirpilir).
ArpValues make_arp_values(const InstallLayout& l, std::wstring_view version, std::uint64_t exe_bytes, std::wstring_view yyyymmdd);

// Guncelleme sonrasi tazeleme (saf): mevcut ARP kaydi BIZIMSE (same_path(InstallLocation, layout.dir)) ve DisplayVersion calisan surumden
// farkliysa, yalniz DisplayVersion'u degistirilmis kopyayi dondurur. Kayit yok / baska konum / ayni surum / bos surum -> nullopt
// (baska urunun ya da elle degistirilmis kaydina dokunulmaz).
std::optional<ArpValues> plan_display_version_refresh(const std::optional<ArpValues>& current, const InstallLayout& l,
                                                      std::wstring_view running_version);

}  // namespace ef::core::setup
