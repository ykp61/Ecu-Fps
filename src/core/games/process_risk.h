#pragma once
// F-43: çalışan oyun sürecinin öncelik sınıfı ve işlemci benzeşimi risk taraması — SALT OKUMA, saf.
//  - Realtime öncelik ve elle daraltılmış affinity sistem kararlılığını bozabilir; uyarı üretilir.
//  - High öncelik her sistemde kazanç sağlamaz; dikkat düzeyinde raporlanır.
//  - Değer okunamadıysa risk üretilmez ("temiz" denmez); ayrım çağıran tarafta `known` ile tutulur.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/games/config_risk.h"

namespace ef::core::games {

struct ProcessState {
    bool known = false;                       // en az bir alan gerçekten okundu
    std::optional<std::uint32_t> priority_class;  // GetPriorityClass ham değeri
    std::optional<std::uint64_t> affinity_mask;
    std::optional<std::uint64_t> system_mask;
};

// Bilinen risk kodları: risk.process.realtime, risk.process.high, risk.process.affinity_fixed
std::vector<ConfigRisk> scan_process_state(const ProcessState& s);

}  // namespace ef::core::games
