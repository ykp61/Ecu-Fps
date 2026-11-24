#pragma once
// T18: cihaz sinifina gore otomatik varsayilan uyarlama (saf mantik; ayri mod/profil DEGIL).
// Tek Performans plani icinde, kullanicinin kayitli secimi yokken bazi gruplarin varsayilani cihaza gore secilir.
// Bilinmeyen bilgi hicbir bayrak kurmaz. Yuksek riskli / tartismali gruplar bu yolla Evet'e CEVRILMEZ; tek bilincli
// istisna plan.background'dur (dusuk onceligine alma; hicbir program kapatilmaz; yine Evet/Hayir onayi ve geri alma vardir).

#include <cstdint>
#include <optional>

namespace ef::core::perf {

struct PlanGroup;  // controller.h

struct DeviceFacts {
    std::optional<bool> laptop;
    std::optional<std::uint64_t> ram_bytes;
    std::optional<std::uint32_t> logical_cores;
};

struct DeviceClass {
    bool laptop = false;
    bool low_end = false;
    bool operator==(const DeviceClass&) const = default;
};

inline constexpr std::uint64_t kLowEndRamBytes = 8ull << 30;  // <= 8 GiB
inline constexpr std::uint32_t kLowEndCores = 4;              // <= 4 mantiksal cekirdek

DeviceClass classify(const DeviceFacts& f) noexcept;

// Grubun bu cihaz sinifinda secim kaydi yokken varsayilani: grubun kendi varsayilani; yalniz plan.background low_end'de Evet.
bool default_on_for(const PlanGroup& g, const DeviceClass& dc) noexcept;

}  // namespace ef::core::perf
