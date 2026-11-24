#pragma once
// Uyumluluk B8 (bosluk 12): bu cihaz yapilandirmasi gercek cihazda dogrulanan bir kombinasyon mu? Ozellik kapatmaz;
// yalniz engellemeyen "bu cihazda dogrulanmadi" notu ve destek paketi icindir. Liste gercek kanita dayanir
// (artifacts/environment.md: gelistirici PC, Windows 11 26H2 build 26300 + NVIDIA). Kanit eklenmeden satir eklenmez.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/serialization/json.h"

namespace ef::core::capabilities {

struct VerifiedCombination {
    std::uint32_t os_build = 0;       // Windows ana build (UBR haric), ornek 26300
    std::uint32_t gpu_vendor_id = 0;  // oyunlarin kullandigi (yuksek performans) GPU'nun PCI ureticisi
    std::string_view evidence;        // kanit kaynagi (insan okur; UI'da gosterilmez)
};
std::span<const VerifiedCombination> verified_combinations() noexcept;

// "26300.9711" -> 26300. Bas rakam yoksa / tasarsa nullopt.
std::optional<std::uint32_t> os_build_major(std::string_view os_build) noexcept;

struct DeviceVerification {
    bool verified = false;       // OS + GPU ureticisi ayni dogrulanmis satirda
    bool os_known = false, gpu_known = false;
    bool os_verified = false;    // bu build herhangi bir dogrulanmis satirda var
    bool gpu_verified = false;   // bu uretici herhangi bir dogrulanmis satirda var
    std::optional<std::uint32_t> os_build;
    std::optional<std::uint32_t> gpu_vendor_id;
};
// Okunamayan deger "dogrulandi" sayilmaz (verified false, *_known false).
DeviceVerification verify_device(std::string_view os_build, std::optional<std::uint32_t> gpu_vendor_id);

json::Value to_json(const DeviceVerification& v);

}  // namespace ef::core::capabilities
