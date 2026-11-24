#pragma once
// F-57 platform okuyucu: oyun klasöründe bilinen upscaler DLL'lerini bulur.
//  - Salt okuma; özyineleme derinliği 3, toplam giriş bütçesi 4000.
//  - Sürüm yalnız dosya sürüm kaynağından okunur; okunamazsa boş kalır (uydurma yok).

#include <cstddef>
#include <string>
#include <vector>

#include "core/games/upscaler.h"

namespace ef::platform::games {

struct UpscalerScan {
    bool scanned = false;  // kurulum klasörü okunabildi mi
    bool truncated = false;  // giriş bütçesi doldu (sonuç kısmi)
    std::size_t files_seen = 0;
    std::vector<core::games::UpscalerAsset> assets;
};

// max_entries: bu çağrıda taranacak en fazla dizin girişi (çağıran toplam bütçeyi paylaştırır).
UpscalerScan scan_upscalers(const std::string& install_dir, std::size_t max_entries = 4000);

}  // namespace ef::platform::games
