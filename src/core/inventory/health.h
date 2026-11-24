#pragma once
// INV-06: donanim/platform saglik bulgulari (saf kurallar). Girdi envanterden gelir; BIOS'a yazma yok.
// Bulgular otomatik duzeltme degildir: BIOS/surucu icin rehber metin, Windows icin aday ayar uretir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/models.h"

namespace ef::core::inventory {

enum class HealthCode {
    XmpLikelyOff,        // RAM anma hizinin altinda calisiyor (BIOS'ta XMP/EXPO)
    SingleDimm,          // tek modul algilandi: kanal kullanimi olculmedi (smbios.h: DIMM sayisindan kanal turetilmez)
    PcieWidthReduced,    // GPU yuvasi tam genislikte calismiyor (x8 / x4)
    ResizableBarOff,     // ReBAR/SAM kapali gorunuyor (BIOS)
    BiosOld,             // BIOS 18 aydan eski: guncelleme kontrol edilebilir
    LowDiskSpace,        // birimde %10'dan az bos alan
};
std::string_view to_string(HealthCode c) noexcept;
std::string_view text_key(HealthCode c) noexcept;  // i18n anahtari

enum class FixKind { GuidedBios, GuidedHardware, GuidedCleanup, GuidedUpdate };

struct HealthFinding {
    HealthCode code;
    FixKind fix;
    contracts::Confidence confidence = contracts::Confidence::Low;
    std::string evidence;  // ornek: "configured 4800 MT/s < rated 6000 MT/s (part F5-6000J...)"
};

struct RamModuleInput {
    std::optional<std::uint32_t> speed_mts, configured_mts;
    std::string part_number;
    std::string type;  // "DDR5"
};

struct PcieInput {
    std::optional<std::uint32_t> current_width, max_width;
    std::optional<std::uint32_t> current_gen, max_gen;
};

struct VolumeSpaceInput {
    std::string root;
    std::optional<std::uint64_t> total_gib, free_gib;
};

struct HealthInput {
    std::vector<RamModuleInput> ram;
    std::optional<PcieInput> dgpu_pcie;
    std::optional<bool> rebar_enabled;
    std::optional<std::string> bios_date;  // "MM/DD/YYYY"
    std::int64_t today_unix_days = 0;      // 1970'ten beri gun
    std::vector<VolumeSpaceInput> volumes;
};

// Parca numarasindaki hiz (ornek "F5-6000J3038F16G" -> 6000, "KF560C36-16" -> 5600). Yoksa nullopt.
std::optional<std::uint32_t> rated_speed_from_part(std::string_view part);
// "MM/DD/YYYY" -> 1970'ten gun; gecersizse nullopt.
std::optional<std::int64_t> parse_bios_date_days(std::string_view mmddyyyy);

std::vector<HealthFinding> evaluate_health(const HealthInput& in);

}  // namespace ef::core::inventory
