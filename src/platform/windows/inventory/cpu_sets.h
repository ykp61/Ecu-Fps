#pragma once
// F-41: oyun surecinin/ana is parcaciginin calisabildigi CPU kumelerini P/E sinifina gore ozetler.
// Salt okuma; benzesme/kume DEGISTIRILMEZ. Okunamayan durumda Unknown doner (iddia yok).

#include <cstdint>

#include "core/inventory/cpu_topology.h"

namespace ef::platform::inventory {

// tid = 0 ise yalniz surec varsayilan kumeleri/benzesme maskesi kullanilir.
core::inventory::CpuSetCoverage read_game_cpu_coverage(std::uint32_t pid, std::uint32_t tid);

}  // namespace ef::platform::inventory
