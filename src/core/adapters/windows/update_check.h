#pragma once
// WIN-07 (R28): guncelleme ve surucu denetimi. SALT OKUMA: urun surucu veya Windows guncellemesi
// KURMAZ, indirmez. Kurulu surum/tarih okunur, eski surucu ve bekleyen yeniden baslatma bildirilir,
// kullaniciya resmi uretici indirme sayfasi (sabit https izin listesi) gosterilir.
// Cevrimici "en son surum" kontrolu yoktur; ileride eklenirse yalniz kullanici acarsa calisir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/inventory.h"
#include "core/adapters/windows/update_guard.h"

namespace ef::core::adapters::windows {

enum class GpuVendor { Nvidia, Amd, Intel, Other };
GpuVendor vendor_from_pci(std::uint32_t vendor_id) noexcept;

struct DriverInfo {
    std::string device_name;
    std::uint32_t vendor_id = 0;
    contracts::Field<std::string> version;          // Windows surucu surumu a.b.c.d
    contracts::Field<std::int64_t> date_unix_days;  // DEVPKEY_Device_DriverDate (1970'ten gun)
    contracts::Field<std::string> provider;         // INF saglayici
};

// NVIDIA a.b.c.d -> pazarlama surumu ("32.0.16.2101" -> "621.01"). Diger ureticiler/gecersiz: nullopt.
std::optional<std::string> nvidia_display_version(std::string_view windows_version);

// Resmi indirme sayfasi (sabit). Bilinmeyen uretici: bos.
std::string_view official_driver_url(GpuVendor v) noexcept;
// Yalniz yukaridaki sabit adresler acilabilir.
bool is_allowed_web_url(std::string_view url) noexcept;

enum class UpdateCheckCode { RebootPending, DriverOld, DriverDateUnknown };
std::string_view to_string(UpdateCheckCode c) noexcept;
std::string_view text_key(UpdateCheckCode c) noexcept;

struct UpdateCheckFinding {
    UpdateCheckCode code;
    std::string evidence;
    std::string url;  // resmi sayfa (varsa); ms-settings icin bos, guided::Action kullanilir
};

constexpr std::int64_t kDriverOldDays = 180;

struct UpdateCheckReport {
    std::vector<DriverInfo> drivers;
    std::vector<UpdateCheckFinding> findings;
    bool installs_anything = false;  // sozlesme: daima false
};

UpdateCheckReport evaluate_update_check(const std::vector<DriverInfo>& drivers, const UpdateStatus& status,
                                        std::int64_t today_unix_days);

}  // namespace ef::core::adapters::windows
