#pragma once
// F-81: depolama sagligi okuyucusu (salt okuma; yonetici gerekmez, pencere acmaz).
// Okunamayan alan nullopt kalir; tahmin uretilmez. Hicbir ayar yazilmaz.

#include <optional>
#include <string>

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

// game_dir: oyun kurulum dizini (yerel surucu yolu) veya bos. Verilirse oyun birimi isaretlenir.
core::readiness::StorageHealthInfo read_storage_health(const std::string& game_dir = {}, bool include_disks = true);

}  // namespace ef::platform::readiness
