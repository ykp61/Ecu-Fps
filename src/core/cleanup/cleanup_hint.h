#pragma once
// R-C2: oyun kapaninca temizlik onerisi karar mantigi (saf, platformdan bagimsiz).
// Hicbir dosya kendiliginden silinmez; yalniz salt okuma tarama baslatilir ve tepsi bildirimi gosterilir.

#include <cstdint>

namespace ef::core::cleanup {

constexpr std::uint64_t kCleanupHintMinBytes = 500ull << 20;         // altinda bildirim gelmez
constexpr std::int64_t kCleanupHintCooldownMs = 6ll * 3600 * 1000;   // iki oneri arasi en az 6 saat

struct HintScanInput {
    bool enabled = true;           // Ayarlar anahtari (cleanup_hint_after_game)
    bool session_present = true;   // temizlik oturumu var
    bool hint_pending = false;     // onceki oyun kapanisi taramasi sonuc bekliyor
    bool uninstall_running = false;
    bool session_idle = true;      // kullanici Temizlik'te degil
    std::int64_t last_hint_ms = 0; // son oneri (0 = hic)
    std::int64_t now_ms = 0;
};

inline bool should_start_hint_scan(const HintScanInput& in) {
    if (!in.enabled || !in.session_present || in.hint_pending) return false;
    if (in.uninstall_running || !in.session_idle) return false;
    if (in.last_hint_ms != 0 && in.now_ms - in.last_hint_ms < kCleanupHintCooldownMs) return false;
    return true;
}

struct HintNotifyInput {
    bool scan_ready = false;       // tarama bitti (Ready)
    bool enabled = true;
    bool game_running = false;     // oyun hala acik / oyun kilidi (game_gate_blocked)
    std::uint64_t selected_bytes = 0;
    bool tray_added = true;
};

inline bool should_notify_cleanup_hint(const HintNotifyInput& in) {
    if (!in.scan_ready || !in.enabled || in.game_running) return false;
    return in.selected_bytes >= kCleanupHintMinBytes && in.tray_added;
}

}  // namespace ef::core::cleanup
