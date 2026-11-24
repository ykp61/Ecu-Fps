#pragma once
// EF-X09/X10: salt okuma (yonetici gerekmez). Kurtarma anahtari okunmaz.
#include <cstdint>
#include <optional>

#include "core/readiness/platform_security.h"

namespace ef::platform::readiness {

core::readiness::PlatformSecurity read_platform_security();

struct MpoState {
    bool read_ok = false;
    std::optional<std::uint32_t> overlay_test_mode;
};
MpoState read_mpo_state();

}  // namespace ef::platform::readiness
