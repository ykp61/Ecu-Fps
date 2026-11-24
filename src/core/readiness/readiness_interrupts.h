#pragma once
// ADR-040 m.5 (B0 iskeleti, B5 doldurur): kesme (MSI) durumu teshisi. YALNIZ OKUMA: MSI modu yazilmaz, bulguda
// "Duzelt" yoktur. Oyun GPU'su ve etkin ag bagdastiricisi icin MSISupported ve kesme onceligi okunur; bulgu yalniz
// durumu soyler (Acik / Kapali / veri yetersiz). Aygit yoksa bulgu yok. Okunamayan = "veri yetersiz",
// ozelligi olmayan = "desteklenmiyor" (ayri anahtarlar). Okuma platform/windows/readiness/interrupt_reader.* (B5).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

enum class InterruptDeviceRole { GameGpu, ActiveNetwork };

struct InterruptDeviceInfo {
    InterruptDeviceRole role = InterruptDeviceRole::GameGpu;
    std::string name;                            // aygit gorunen adi (yalniz gosterim)
    bool readable = false;                       // kayit okunabildi mi
    std::optional<std::uint32_t> msi_supported;  // MSISupported (1/0); nullopt = deger yok
    std::optional<std::uint32_t> priority;       // DevicePriority; nullopt = deger yok
};

// B5: aygit basina bir bulgu (aygit yoksa bulgu yok); ActionKind::None (Duzelt yok). Okunamadi / tanimsiz deger =
// "veri yetersiz" (Unknown), okundu ama MSISupported yok = "desteklenmis degil" (Info); 1 = Acik, 0 = Kapali (Info).
std::vector<Check> interrupt_checks(const std::vector<InterruptDeviceInfo>& devices);

}  // namespace ef::core::readiness
