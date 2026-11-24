#include "core/cleanup/cleanup_hint.h"
#include "ef_test.h"

using namespace ef::core::cleanup;

namespace {
HintScanInput scan_ok() {
    HintScanInput in;
    in.now_ms = 10ll * 3600 * 1000;
    return in;
}
HintNotifyInput notify_ok() {
    HintNotifyInput in;
    in.scan_ready = true;
    in.selected_bytes = kCleanupHintMinBytes;
    return in;
}
}  // namespace

EF_TEST(cleanup_hint_scan_starts_when_all_clear) {
    EF_CHECK(should_start_hint_scan(scan_ok()));
}

EF_TEST(cleanup_hint_scan_blocked_when_switch_off) {
    auto in = scan_ok();
    in.enabled = false;
    EF_CHECK(!should_start_hint_scan(in));
}

EF_TEST(cleanup_hint_scan_blocked_by_session_state) {
    auto a = scan_ok(); a.session_present = false;
    auto b = scan_ok(); b.hint_pending = true;
    auto c = scan_ok(); c.uninstall_running = true;
    auto d = scan_ok(); d.session_idle = false;  // kullanici Temizlik'te
    EF_CHECK(!should_start_hint_scan(a));
    EF_CHECK(!should_start_hint_scan(b));
    EF_CHECK(!should_start_hint_scan(c));
    EF_CHECK(!should_start_hint_scan(d));
}

EF_TEST(cleanup_hint_scan_six_hour_cooldown) {
    auto in = scan_ok();
    in.last_hint_ms = in.now_ms - (kCleanupHintCooldownMs - 1);  // 6 saatten 1 ms az
    EF_CHECK(!should_start_hint_scan(in));
    in.last_hint_ms = in.now_ms - kCleanupHintCooldownMs;  // tam 6 saat
    EF_CHECK(should_start_hint_scan(in));
    in.last_hint_ms = 0;  // hic oneri yok
    EF_CHECK(should_start_hint_scan(in));
    volatile std::int64_t cd = kCleanupHintCooldownMs;  // sabit-ifade uyarisi (C4127) olmasin
    EF_CHECK(cd == 6ll * 3600 * 1000);
}

EF_TEST(cleanup_hint_notify_threshold_500mb) {
    volatile std::uint64_t mb = kCleanupHintMinBytes;
    EF_CHECK(mb == (500ull << 20));
    auto in = notify_ok();
    EF_CHECK(should_notify_cleanup_hint(in));  // tam esik
    in.selected_bytes = kCleanupHintMinBytes - 1;
    EF_CHECK(!should_notify_cleanup_hint(in));
    in.selected_bytes = 0;
    EF_CHECK(!should_notify_cleanup_hint(in));
}

EF_TEST(cleanup_hint_notify_blocked_when_off_or_game_still_running) {
    auto off = notify_ok(); off.enabled = false;
    auto game = notify_ok(); game.game_running = true;  // oyun hala acik
    auto tray = notify_ok(); tray.tray_added = false;
    auto scan = notify_ok(); scan.scan_ready = false;
    EF_CHECK(!should_notify_cleanup_hint(off));
    EF_CHECK(!should_notify_cleanup_hint(game));
    EF_CHECK(!should_notify_cleanup_hint(tray));
    EF_CHECK(!should_notify_cleanup_hint(scan));
}
