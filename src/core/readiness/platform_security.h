#pragma once
// EF-X09 (fTPM/C-State) ve EF-X10 (MPO/ULPS): salt okuma durum ve kapilar (saf; birim testlenir).
//  - TPM/Guvenli Onyukleme/BitLocker yalniz okunur; BitLocker kurtarma anahtari hic okunmaz, saklanmaz, loglanmaz.
//  - Bu bilgilerden biri bilinmiyorsa firmware degisikligi onerilmez; uretici cozumune yonlendirilir.
//  - Firmware C-State Windows'tan okunamaz: acik/kapali uydurulmaz. Windows guc ayari ayri kontroldur.
//  - fTPM bulteni ancak dogrulanmis katalogla eslesir; "sorun yok" hukmu veya otomatik duzeltme uretilmez.
//  - MPO yazimi resmi hedef bagimsiz dogrulanana kadar KAPALI; ULPS icin yazma hic yok (belgesiz anahtar taranmaz).

#include <optional>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

struct PlatformSecurity {
    std::optional<int> tpm_version;        // 2 = TPM 2.0, 1 = 1.2, 0 = yok; nullopt = okunamadi
    std::optional<bool> secure_boot;       // nullopt = okunamadi
    std::optional<bool> bitlocker_on;      // nullopt = okunamadi (yonetici gerekebilir)
};

// Firmware degisikligi (rehber dahil) onerilebilir mi: hepsi biliniyorsa true.
bool firmware_change_suggestable(const PlatformSecurity& s) noexcept;
std::vector<Check> platform_security_checks(const PlatformSecurity& s);

// MPO: HKLM\SOFTWARE\Microsoft\Windows\Dwm OverlayTestMode. nullopt = deger yok (Windows varsayilani).
inline constexpr bool kMpoWriteVerified = false;
std::vector<Check> mpo_checks(std::optional<std::uint32_t> overlay_test_mode, bool display_symptom, bool read_ok);

}  // namespace ef::core::readiness
